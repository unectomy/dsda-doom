//
// Copyright(C) 2020 by Ryan Krafnick
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//	DSDA Demo
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doomtype.h"
#include "doomstat.h"
#include "g_game.h"
#include "m_argv.h"
#include "m_misc.h"
#include "lprintf.h"
#include "e6y.h"

#include "command_display.h"
#include "dsda/excmd.h"
#include "dsda/key_frame.h"
#include "dsda/map_format.h"
#include "dsda/settings.h"

#include "demo.h"

#define INITIAL_DEMO_BUFFER_SIZE 0x20000

static char* dsda_demo_name_base;
static byte* dsda_demo_write_buffer;
static byte* dsda_demo_write_buffer_p;
static int dsda_demo_write_buffer_length;
static int dsda_extra_demo_header_data_offset;
static int largest_real_offset;
static int demo_tics;
static int compatibility_level_unspecified;

#define DSDA_DEMO_VERSION 1
#define DEMOMARKER 0x80

static int dsda_demo_version;
static int bytes_per_tic;

const char* dsda_DemoNameBase(void) {
  return dsda_demo_name_base;
}

void dsda_SetDemoBaseName(const char* name) {
  size_t base_size;
  char* p;

  if (dsda_demo_name_base)
    free(dsda_demo_name_base);

  dsda_demo_name_base = strdup(name);

  p = dsda_demo_name_base + strlen(dsda_demo_name_base);
  while (--p > dsda_demo_name_base && *p != '/' && *p != '\\')
    if (*p == '.') {
      *p = '\0';
      break;
    }
}

// from crispy - incrementing demo file names
char* dsda_NewDemoName(void) {
  char* demo_name;
  size_t demo_name_size;
  FILE* fp;
  static unsigned int j = 2;

  if (!dsda_demo_name_base)
    dsda_SetDemoBaseName("null");

  demo_name_size = strlen(dsda_demo_name_base) + 11; // 11 = -12345.lmp\0
  demo_name = malloc(demo_name_size);
  snprintf(demo_name, demo_name_size, "%s.lmp", dsda_demo_name_base);

  for (; j <= 99999 && (fp = fopen(demo_name, "rb")) != NULL; j++) {
    snprintf(demo_name, demo_name_size, "%s-%05d.lmp", dsda_demo_name_base, j);
    fclose (fp);
  }

  return demo_name;
}

static int dsda_DemoBufferOffset(void) {
  return dsda_demo_write_buffer_p - dsda_demo_write_buffer;
}

int dsda_BytesPerTic(void) {
  return bytes_per_tic;
}

void dsda_EvaluateBytesPerTic(void) {
  bytes_per_tic = (longtics ? 5 : 4);
  if (raven) bytes_per_tic += 2;
  if (dsda_ExCmdDemo()) bytes_per_tic++;
}

static void dsda_EnsureDemoBufferSpace(size_t length) {
  int offset;

  offset = dsda_DemoBufferOffset();

  if (offset + length <= dsda_demo_write_buffer_length) return;

  while (offset + length > dsda_demo_write_buffer_length)
    dsda_demo_write_buffer_length *= 2;

  dsda_demo_write_buffer =
    (byte *)realloc(dsda_demo_write_buffer, dsda_demo_write_buffer_length);

  if (dsda_demo_write_buffer == NULL)
    I_Error("dsda_EnsureDemoBufferSpace: out of memory!");

  dsda_demo_write_buffer_p = dsda_demo_write_buffer + offset;

  lprintf(
    LO_INFO,
    "dsda_EnsureDemoBufferSpace: expanding demo buffer %d\n",
    dsda_demo_write_buffer_length
  );
}

void dsda_CopyPendingCmd(ticcmd_t* cmd) {
  if (demorecording && largest_real_offset - dsda_DemoBufferOffset() >= bytes_per_tic) {
    const byte* p = dsda_demo_write_buffer_p;

    G_ReadOneTick(cmd, &p);
  }
  else {
    memset(cmd, 0, sizeof(*cmd));
  }
}

void dsda_RestoreCommandHistory(void) {
  extern int dsda_command_history_size;

  ticcmd_t cmd = { 0 };

  if (demorecording && logictic && dsda_command_history_size) {
    const byte* p;
    int count;

    count = MIN(logictic, dsda_command_history_size);

    p = dsda_demo_write_buffer_p - bytes_per_tic * count;

    while (p < dsda_demo_write_buffer_p) {
      G_ReadOneTick(&cmd, &p);
      dsda_AddCommandToCommandDisplay(&cmd);
    }
  }
}

void dsda_MarkCompatibilityLevelUnspecified(void) {
  compatibility_level_unspecified = true;
}

void dsda_InitDemoRecording(void) {
  static dboolean demo_key_frame_initialized;

  if (compatibility_level_unspecified)
    I_Error("You must specify a compatibility level when recording a demo!\n"
            "Example: dsda-doom -iwad DOOM -complevel 3 -record demo");

  demorecording = true;

  // prboom+ has already cached its settings (with demorecording == false)
  // we need to reset things here to satisfy strict mode
  dsda_InitSettings();

  if (!demo_key_frame_initialized) {
    dsda_InitKeyFrame();
    demo_key_frame_initialized = true;
  }

  dsda_demo_write_buffer = malloc(INITIAL_DEMO_BUFFER_SIZE);
  if (dsda_demo_write_buffer == NULL)
    I_Error("dsda_InitDemo: unable to initialize demo buffer!");

  dsda_demo_write_buffer_p = dsda_demo_write_buffer;

  dsda_demo_write_buffer_length = INITIAL_DEMO_BUFFER_SIZE;

  demo_tics = 0;
}

void dsda_WriteToDemo(void* buffer, size_t length) {
  dsda_EnsureDemoBufferSpace(length);

  memcpy(dsda_demo_write_buffer_p, buffer, length);
  dsda_demo_write_buffer_p += length;
}

void dsda_WriteTicToDemo(void* buffer, size_t length) {
  dsda_WriteToDemo(buffer, length);
  ++demo_tics;
}

static void dsda_WriteIntToHeader(byte** p, int value) {
  byte* header_p = *p;

  *header_p++ = (byte)((value >> 24) & 0xff);
  *header_p++ = (byte)((value >> 16) & 0xff);
  *header_p++ = (byte)((value >>  8) & 0xff);
  *header_p++ = (byte)( value        & 0xff);

  *p = header_p;
}

static int dsda_ReadIntFromHeader(const byte* p) {
  int result;

  result  = *p++ & 0xff;
  result <<= 8;
  result += *p++ & 0xff;
  result <<= 8;
  result += *p++ & 0xff;
  result <<= 8;
  result += *p++ & 0xff;

  return result;
}

static void dsda_WriteExtraDemoHeaderData(int end_marker_location) {
  byte* header_p;

  if (!dsda_demo_version) return;

  header_p = dsda_demo_write_buffer + dsda_extra_demo_header_data_offset;
  dsda_WriteIntToHeader(&header_p, end_marker_location);
  dsda_WriteIntToHeader(&header_p, demo_tics);
}

void dsda_EndDemoRecording(void) {
  int end_marker_location;
  byte end_marker = DEMOMARKER;

  demorecording = false;

  end_marker_location = dsda_demo_write_buffer_p - dsda_demo_write_buffer;

  dsda_WriteToDemo(&end_marker, 1);

  dsda_WriteExtraDemoHeaderData(end_marker_location);

  G_WriteDemoFooter();

  dsda_WriteDemoToFile();

  lprintf(LO_INFO, "Demo finished recording\n");
}

void dsda_WriteDemoToFile(void) {
  int length;
  char* demo_name;

  length = dsda_DemoBufferOffset();

  demo_name = dsda_NewDemoName();

  if (!M_WriteFile(demo_name, dsda_demo_write_buffer, length))
    I_Error("dsda_WriteDemoToFile: Failed to write demo file.");

  free(dsda_demo_write_buffer);
  dsda_demo_write_buffer = NULL;
  dsda_demo_write_buffer_p = NULL;
  dsda_demo_write_buffer_length = 0;

  free(demo_name);
}

static void dsda_SetDemoBufferOffset(int offset) {
  int current_offset;

  if (dsda_demo_write_buffer == NULL) return;

  current_offset = dsda_DemoBufferOffset();

  // Cannot load forward (demo buffer would desync)
  if (offset > current_offset)
    I_Error("dsda_SetDemoBufferOffset: Impossible time traveling detected.");

  if (current_offset > largest_real_offset)
    largest_real_offset = current_offset;

  dsda_demo_write_buffer_p = dsda_demo_write_buffer + offset;
}

int dsda_DemoDataSize(byte complete) {
  int buffer_size;

  buffer_size = complete ? dsda_DemoBufferOffset() : 0;

  return sizeof(buffer_size) + sizeof(demo_tics) + buffer_size;
}

void dsda_StoreDemoData(byte** save_p, byte complete) {
  int demo_write_buffer_offset;

  demo_write_buffer_offset = dsda_DemoBufferOffset();

  memcpy(*save_p, &demo_write_buffer_offset, sizeof(demo_write_buffer_offset));
  *save_p += sizeof(demo_write_buffer_offset);

  memcpy(*save_p, &demo_tics, sizeof(demo_tics));
  *save_p += sizeof(demo_tics);

  if (complete && demo_write_buffer_offset) {
    memcpy(*save_p, dsda_demo_write_buffer, demo_write_buffer_offset);
    *save_p += demo_write_buffer_offset;
  }
}

void dsda_RestoreDemoData(byte** save_p, byte complete) {
  int demo_write_buffer_offset;

  memcpy(&demo_write_buffer_offset, *save_p, sizeof(demo_write_buffer_offset));
  *save_p += sizeof(demo_write_buffer_offset);

  memcpy(&demo_tics, *save_p, sizeof(demo_tics));
  *save_p += sizeof(demo_tics);

  if (complete && demo_write_buffer_offset) {
    dsda_SetDemoBufferOffset(0);
    dsda_WriteToDemo(*save_p, demo_write_buffer_offset);
    *save_p += demo_write_buffer_offset;
  }
  else
    dsda_SetDemoBufferOffset(demo_write_buffer_offset);
}

void dsda_JoinDemoCmd(ticcmd_t* cmd) {
  // Sometimes this bit is not available
  if (
    (demo_compatibility && !prboom_comp[PC_ALLOW_SSG_DIRECT].state) ||
    (cmd->buttons & BT_CHANGE) == 0
  )
    cmd->buttons |= BT_JOIN;
}

#define DSDA_DEMO_HEADER_START_SIZE 8 // version + signature (6) + dsda version
#define DSDA_DEMO_HEADER_DATA_SIZE (2*sizeof(int))

static const byte* dsda_ReadDSDADemoHeader(const byte* demo_p, const byte* header_p, size_t size) {
  dsda_demo_version = 0;

  // 7 = 6 (signature) + 1 (dsda version)
  if (demo_p - header_p + 7 > size)
    return NULL;

  if (*demo_p++ != 0x1d)
    return NULL;

  if (strncmp((const char *) demo_p, "DSDA", 4) != 0)
    return NULL;

  demo_p += 4;

  if (*demo_p++ != 0xe6)
    return NULL;

  dsda_demo_version = *demo_p++;

  if (dsda_demo_version > DSDA_DEMO_VERSION)
    return NULL;

  if (demo_p - header_p + DSDA_DEMO_HEADER_DATA_SIZE > size)
    return NULL;

  demo_p += DSDA_DEMO_HEADER_DATA_SIZE;

  dsda_EnableExCmd();
  dsda_EnableCasualExCmdFeatures();

  return demo_p;
}

// Strip off the defunct extended header (if we understand it) or abort (if we don't)
static const byte* dsda_ReadUMAPINFODemoHeader(const byte* demo_p, const byte* header_p, size_t size) {
  // 9 = 6 (signature) + 1 (version) + 2 (extension count)
  if (demo_p - header_p + 9 > size)
    return NULL;

  if (strncmp((const char *)demo_p, "PR+UM", 5) != 0)
    I_Error("G_ReadDemoHeader: Unknown demo format");

  demo_p += 6;

  // the defunct format had only version 1
  if (*demo_p++ != 1)
    I_Error("G_ReadDemoHeader: Unknown demo format");

  // the defunct format had only one extension (in two bytes)
  if (*demo_p++ != 1 || *demo_p++ != 0)
    I_Error("G_ReadDemoHeader: Unknown demo format");

  if (demo_p - header_p + 1 > size)
    return NULL;

  // the defunct extension had length 8
  if (*demo_p++ != 8)
    I_Error("G_ReadDemoHeader: Unknown demo format");

  if (demo_p - header_p + 8 > size)
    return NULL;

  if (strncmp((const char *)demo_p, "UMAPINFO", 8))
    I_Error("G_ReadDemoHeader: Unknown demo format");

  demo_p += 8;

  // the defunct extension stored the map lump (unused)
  if (demo_p - header_p + 8 > size)
    return NULL;

  demo_p += 8;

  return demo_p;
}

const byte* dsda_StripDemoVersion255(const byte* demo_p, const byte* header_p, size_t size) {
  const byte* dsda_p;

  dsda_p = dsda_ReadDSDADemoHeader(demo_p, header_p, size);

  if (dsda_p) return dsda_p;

  return dsda_ReadUMAPINFODemoHeader(demo_p, header_p, size);
}

void dsda_WriteDSDADemoHeader(byte** p) {
  byte* demo_p = *p;

  *demo_p++ = 255;

  // signature
  *demo_p++ = 0x1d;
  *demo_p++ = 'D';
  *demo_p++ = 'S';
  *demo_p++ = 'D';
  *demo_p++ = 'A';
  *demo_p++ = 0xe6;

  *demo_p++ = DSDA_DEMO_VERSION;

  dsda_demo_version = DSDA_DEMO_VERSION;
  dsda_extra_demo_header_data_offset = demo_p - *p;
  memset(demo_p, 0, DSDA_DEMO_HEADER_DATA_SIZE);
  demo_p += DSDA_DEMO_HEADER_DATA_SIZE;

  *p = demo_p;
}

void dsda_ApplyDSDADemoFormat(byte** demo_p) {
  dboolean use_dsda_format = false;

  if (map_format.zdoom)
  {
    if (!M_CheckParm("-baddemo"))
      I_Error("Experimental formats require the -baddemo option to record.");

    if (!mbf21)
      I_Error("You must use complevel 21 when recording on doom-in-hexen format.");

    use_dsda_format = true;
  }

  if (M_CheckParm("-dsdademo"))
  {
    use_dsda_format = true;
    dsda_EnableCasualExCmdFeatures();
  }

  if (use_dsda_format)
  {
    dsda_EnableExCmd();
    dsda_WriteDSDADemoHeader(demo_p);
  }
}

int dsda_DemoTicsCount(const byte* p, const byte* demobuffer, int demolength) {
  int count = 0;
  extern int demo_playerscount;

  if (dsda_demo_version)
    return dsda_ReadIntFromHeader(demobuffer + DSDA_DEMO_HEADER_START_SIZE + 4);

  do {
    count++;
    p += bytes_per_tic;
  } while ((p < demobuffer + demolength) && (*p != DEMOMARKER));

  return count / demo_playerscount;
}

const byte* dsda_DemoMarkerPosition(byte* buffer, size_t file_size) {
  const byte* p;

  // read demo header
  p = G_ReadDemoHeaderEx(buffer, file_size, RDH_SKIP_HEADER);

  if (dsda_demo_version) {
    int i;

    p = (const byte*) (buffer + dsda_ReadIntFromHeader(buffer + DSDA_DEMO_HEADER_START_SIZE));

    if (*p != DEMOMARKER)
      return NULL;

    return p;
  }

  // skip demo data
  while (p < buffer + file_size && *p != DEMOMARKER)
    p += bytes_per_tic;

  if (*p != DEMOMARKER)
    return NULL;

  return p;
}

/* Emacs style mode select   -*- C++ -*-
 *-----------------------------------------------------------------------------
 *
 *
 *  PrBoom: a Doom port merged with LxDoom and LSDLDoom
 *  based on BOOM, a modified and improved DOOM engine
 *  Copyright (C) 1999 by
 *  id Software, Chi Hoang, Lee Killough, Jim Flynn, Rand Phares, Ty Halderman
 *  Copyright (C) 1999-2000 by
 *  Jess Haas, Nicolas Kalkhof, Colin Phipps, Florian Schulze
 *  Copyright 2005, 2006 by
 *  Florian Schulze, Colin Phipps, Neil Stevens, Andrey Budko
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 *  02111-1307, USA.
 *
 * DESCRIPTION:
 *  Gamma correction LUT.
 *  Color range translation support
 *  Functions to draw patches (by post) directly to screen.
 *  Functions to blit a block to the screen.
 *
 *-----------------------------------------------------------------------------*/

#ifndef __V_VIDEO__
#define __V_VIDEO__

#include "doomtype.h"
#include "doomdef.h"
// Needed because we are refering to patches.
#include "r_data.h"

//
// VIDEO
//

// DWF 2012-05-10
// SetRatio sets the following global variables based on window geometry and
// user preferences. The integer ratio is hardly used anymore, so further
// simplification may be in order.
void SetRatio(int width, int height);
extern dboolean tallscreen;
extern unsigned int ratio_multiplier, ratio_scale;
extern float gl_ratio;
extern int psprite_offset; // Needed for "tallscreen" modes

#define CENTERY     (SCREENHEIGHT/2)

// Screen 0 is the screen updated by I_Update screen.
// Screen 1 is an extra buffer.

// array of pointers to color translation tables
extern const byte *colrngs[];

// symbolic indices into color translation table pointer array
typedef enum
{
  CR_BRICK,   //0
  CR_TAN,     //1
  CR_GRAY,    //2
  CR_GREEN,   //3
  CR_BROWN,   //4
  CR_GOLD,    //5
  CR_DEFAULT, //6
  CR_BLUE,    //7
  CR_ORANGE,  //8
  CR_YELLOW,  //9
  CR_BLUE2,   //10 // proff
  CR_BLACK,   //11
  CR_PURPLE,  //12
  CR_WHITE,   //13
  CR_RED,     //14
  CR_LIMIT    //15 //jff 2/27/98 added for range check
} crange_idx_e;
//jff 1/16/98 end palette color range additions

typedef struct {
  byte *data;          // pointer to the screen content
  dboolean not_on_heap; // if set, no malloc or free is preformed and
                       // data never set to NULL. Used i.e. with SDL doublebuffer.
  int width;           // the width of the surface
  int height;          // the height of the surface, used when mallocing
  int pitch;      // tha actual width of one line, used when mallocing
} screeninfo_t;

#define NUM_SCREENS 6
extern screeninfo_t screens[NUM_SCREENS];
extern int          usegamma;

// Varying bit-depth support -POPE
//
// For bilinear filtering, each palette color is pre-weighted and put in a
// table for fast blending operations. These macros decide how many weights
// to create for each color. The lower the number, the lower the blend
// accuracy, which can produce very bad artifacts in texture filtering.
#define VID_NUMCOLORWEIGHTS 64
#define VID_COLORWEIGHTMASK (VID_NUMCOLORWEIGHTS-1)
#define VID_COLORWEIGHTBITS 6

// The available bit-depth modes
typedef enum {
  VID_MODESW,
  VID_MODEGL,
  VID_MODEMAX
} video_mode_t;

void V_InitMode(video_mode_t mode);

// video mode query interface
dboolean V_IsSoftwareMode(void);
dboolean V_IsOpenGLMode(void);

//jff 4/24/98 loads color translation lumps
void V_InitColorTranslation(void);

void V_InitFlexTranTable(void);

// Allocates buffer screens, call before R_Init.
void V_Init (void);

// V_CopyRect
typedef void (*V_CopyRect_f)(int srcscrn, int destscrn,
                             int x, int y,
                             int width, int height,
                             enum patch_translation_e flags);
extern V_CopyRect_f V_CopyRect;

void V_CopyScreen(int srcscrn, int destscrn);

// V_FillRect
typedef void (*V_FillRect_f)(int scrn, int x, int y,
                             int width, int height, byte colour);
extern V_FillRect_f V_FillRect;

// CPhipps - patch drawing
// Consolidated into the 3 really useful functions:

// V_DrawNumPatch - Draws the patch from lump num
typedef void (*V_DrawNumPatch_f)(int x, int y, int scrn,
                                 int lump, int cm,
                                 enum patch_translation_e flags);
extern V_DrawNumPatch_f V_DrawNumPatch;

typedef void (*V_DrawNumPatchPrecise_f)(float x, float y, int scrn,
                                 int lump, int cm,
                                 enum patch_translation_e flags);
extern V_DrawNumPatchPrecise_f V_DrawNumPatchPrecise;

// V_DrawNamePatch - Draws the patch from lump "name"
#define V_DrawNamePatch(x,y,s,n,t,f) V_DrawNumPatch(x,y,s,W_GetNumForName(n),t,f)
#define V_DrawNamePatchPrecise(x,y,s,n,t,f) V_DrawNumPatchPrecise(x,y,s,W_GetNumForName(n),t,f)

/* cph -
 * Functions to return width & height of a patch.
 * Doesn't really belong here, but is often used in conjunction with
 * this code.
 */
#define V_NamePatchWidth(name) R_NumPatchWidth(W_GetNumForName(name))
#define V_NamePatchHeight(name) R_NumPatchHeight(W_GetNumForName(name))

// e6y
typedef void (*V_FillFlat_f)(int lump, int scrn, int x, int y, int width, int height, enum patch_translation_e flags);
extern V_FillFlat_f V_FillFlat;
#define V_FillFlatName(flatname, scrn, x, y, width, height, flags) \
  V_FillFlat(R_FlatNumForName(flatname), (scrn), (x), (y), (width), (height), (flags))

typedef void (*V_FillPatch_f)(int lump, int scrn, int x, int y, int width, int height, enum patch_translation_e flags);
extern V_FillPatch_f V_FillPatch;
#define V_FillPatchName(name, scrn, x, y, width, height, flags) \
  V_FillPatch(W_GetNumForName(name), (scrn), (x), (y), (width), (height), (flags))


/* cphipps 10/99: function to tile a flat over the screen */
typedef void (*V_DrawBackground_f)(const char* flatname, int scrn);
extern V_DrawBackground_f V_DrawBackground;

// CPhipps - function to set the palette to palette number pal.
void V_SetPalette(int pal);
void V_SetPlayPal(int playpal_index);

// Alt-Enter: fullscreen <-> windowed
void V_ToggleFullscreen(void);
void V_ChangeScreenResolution(void);

// CPhipps - function to plot a pixel

// V_PlotPixel
typedef void (*V_PlotPixel_f)(int,int,int,byte);
extern V_PlotPixel_f V_PlotPixel;

typedef struct
{
  int x, y;
  float fx, fy;
} fpoint_t;

typedef struct
{
  fpoint_t a, b;
} fline_t;

// V_DrawLine
typedef void (*V_DrawLine_f)(fline_t* fl, int color);
extern V_DrawLine_f V_DrawLine;

// V_DrawLineWu
typedef void (*V_DrawLineWu_f)(fline_t* fl, int color);
extern V_DrawLineWu_f V_DrawLineWu;

// V_PlotPixelWu
typedef void (*V_PlotPixelWu_f)(int scrn, int x, int y, byte color, int weight);
extern V_PlotPixelWu_f V_PlotPixelWu;

void V_AllocScreen(screeninfo_t *scrn);
void V_AllocScreens();
void V_FreeScreen(screeninfo_t *scrn);
void V_FreeScreens();

const unsigned char* V_GetPlaypal(void);
void V_FreePlaypal(void);

// e6y: wide-res
void V_FillBorder(int lump, byte color);

void V_GetWideRect(int *x, int *y, int *w, int *h, enum patch_translation_e flags);

int V_BestColor(const unsigned char *palette, int r, int g, int b);

// [FG] colored blood and gibs
int V_BloodColor(int blood);

#include "gl_struct.h"

void V_FillRectStretch(int scrn, int x, int y, int width, int height, byte color);

// heretic

void V_DrawRawScreen(const char *lump_name);
void V_DrawRawScreenSection(const char *lump_name, int source_offset, int dest_y_offset, int dest_y_limit);
void V_DrawShadowedNumPatch(int x, int y, int lump);
void V_DrawShadowedNamePatch(int x, int y, const char* name);
void V_DrawTLNumPatch(int x, int y, int lump);
void V_DrawTLNamePatch(int x, int y, const char* name);
void V_DrawAltTLNumPatch(int x, int y, int lump);

#endif

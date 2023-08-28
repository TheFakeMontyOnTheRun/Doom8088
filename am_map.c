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
 *  Copyright 2023 by
 *  Frenkel Smeijers
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
 *   the automap code
 *
 *-----------------------------------------------------------------------------
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include "doomstat.h"
#include "r_defs.h"
#include "st_stuff.h"
#include "r_main.h"
#include "p_setup.h"
#include "p_maputl.h"
#include "w_wad.h"
#include "v_video.h"
#include "p_spec.h"
#include "am_map.h"
#include "dstrings.h"
#include "g_game.h"

#include "globdata.h"


static const int32_t mapcolor_back = 247;    // map background
static const int32_t mapcolor_wall = 23;    // normal 1s wall color
static const int32_t mapcolor_fchg = 55;    // line at floor height change color
static const int32_t mapcolor_cchg = 215;    // line at ceiling height change color
static const int32_t mapcolor_clsd = 208;    // line at sector with floor=ceiling color
static const int32_t mapcolor_rdor = 175;    // red door color  (diff from keys to allow option)
static const int32_t mapcolor_bdor = 204;    // blue door color (of enabling one but not other )
static const int32_t mapcolor_ydor = 231;    // yellow door color
static const int32_t mapcolor_tele = 119;    // teleporter line color
static const int32_t mapcolor_secr = 252;    // secret sector boundary color
static const int32_t mapcolor_exit = 0;    // jff 4/23/98 add exit line color
static const int32_t mapcolor_unsn = 104;    // computer map unseen line color
static const int32_t mapcolor_flat = 88;    // line with no floor/ceiling changes
static const int32_t mapcolor_sngl = 208;    // single player arrow color
static const int32_t map_secret_after = 0;

static const int32_t f_w = (SCREENWIDTH*2);
static const int32_t f_h = SCREENHEIGHT-ST_SCALED_HEIGHT;// to allow runtime setting of width/height


typedef struct
{
 fixed_t x,y;
} mpoint_t;

static mpoint_t m_paninc;    // how far the window pans each tic (map coords)

static fixed_t m_x,  m_y;    // LL x,y window location on the map (map coords)
static fixed_t m_x2, m_y2;   // UR x,y window location on the map (map coords)

//
// width/height of window on map (map coords)
//
static fixed_t  m_w;
static fixed_t  m_h;

// based on level size
static fixed_t  min_x;
static fixed_t  min_y;
static fixed_t  max_x;
static fixed_t  max_y;

static fixed_t  max_w;          // max_x-min_x,
static fixed_t  max_h;          // max_y-min_y

static fixed_t  min_scale_mtof; // used to tell when to stop zooming out
static fixed_t  max_scale_mtof; // used to tell when to stop zooming in

// old location used by the Follower routine
static mpoint_t f_oldloc;

// used by MTOF to scale from map-to-frame-buffer coords
static fixed_t scale_mtof = (fixed_t)INITSCALEMTOF;
// used by FTOM to scale from frame-buffer-to-map coords (=1/scale_mtof)
static fixed_t scale_ftom;

static int32_t lastlevel   = -1;
static int32_t lastepisode = -1;

static boolean stopped = true;

static fixed_t mtof_zoommul = FRACUNIT; // how far the window zooms each tic (map coords)
static fixed_t ftom_zoommul = FRACUNIT; // how far the window zooms each tic (fb coords)

// how much the automap moves window per tic in frame-buffer coordinates
// moves 140 pixels in 1 second
#define F_PANINC  4
// how much zoom-in per tic
// goes to 2x in 1 second
#define M_ZOOMIN        ((int32_t) (1.02*FRACUNIT))
// how much zoom-out per tic
// pulls out to 0.5x in 1 second
#define M_ZOOMOUT       ((int32_t) (FRACUNIT/1.02))

#define MAPBITS 12
#define FRACTOMAPBITS (FRACBITS-MAPBITS)

#define PLAYERRADIUS    (16L*(1<<MAPBITS)) // e6y

// translates between frame-buffer and map distances
#define FTOM(x) FixedMul((((int32_t)x)<<16),scale_ftom)
#define MTOF(x) (FixedMul((x),scale_mtof)>>16)
// translates between frame-buffer and map coordinates
#define CXMTOF(x)  (MTOF((x)- m_x))
#define CYMTOF(y)  ((f_h - MTOF((y)- m_y)))

typedef struct
{
    mpoint_t a, b;
} mline_t;


typedef struct
{
  int32_t x, y;
} fpoint_t;


typedef struct
{
  fpoint_t a, b;
} fline_t;


//
// The vector graphics for the automap.
//  A line drawing of the player pointing right,
//   starting from the middle.
//
#define R ((8*PLAYERRADIUS)/7)
static const mline_t player_arrow[] =
{
  { { -R+R/8, 0 }, { R, 0 } }, // -----
  { { R, 0 }, { R-R/2, R/4 } },  // ----->
  { { R, 0 }, { R-R/2, -R/4 } },
  { { -R+R/8, 0 }, { -R-R/8, R/4 } }, // >---->
  { { -R+R/8, 0 }, { -R-R/8, -R/4 } },
  { { -R+3*R/8, 0 }, { -R+R/8, R/4 } }, // >>--->
  { { -R+3*R/8, 0 }, { -R+R/8, -R/4 } }
};
#undef R
#define NUMPLYRLINES (sizeof(player_arrow)/sizeof(mline_t))



//
// AM_activateNewScale()
//
// Changes the map scale after zooming or translating
//
// Passed nothing, returns nothing
//
static void AM_activateNewScale(void)
{
    m_x += m_w/2;
    m_y += m_h/2;
    m_w = FTOM(f_w);
    m_h = FTOM(f_h);
    m_x -= m_w/2;
    m_y -= m_h/2;
    m_x2 = m_x + m_w;
    m_y2 = m_y + m_h;
}

//
// AM_findMinMaxBoundaries()
//
// Determines bounding box of all vertices,
// sets global variables controlling zoom range.
//
// Passed nothing, returns nothing
//
static void AM_findMinMaxBoundaries(void)
{
    int32_t i;
    fixed_t a;
    fixed_t b;

    min_x = min_y =  INT32_MAX;
    max_x = max_y = -INT32_MAX;

    for (i=0;i<_g->numvertexes;i++)
    {
        if (_g->vertexes[i].x < min_x)
            min_x = _g->vertexes[i].x;
        else if (_g->vertexes[i].x > max_x)
            max_x = _g->vertexes[i].x;

        if (_g->vertexes[i].y < min_y)
            min_y = _g->vertexes[i].y;
        else if (_g->vertexes[i].y > max_y)
            max_y = _g->vertexes[i].y;
    }

    max_w = (max_x >>= FRACTOMAPBITS) - (min_x >>= FRACTOMAPBITS);//e6y
    max_h = (max_y >>= FRACTOMAPBITS) - (min_y >>= FRACTOMAPBITS);//e6y

    a = FixedDiv(f_w<<FRACBITS, max_w);
    b = FixedDiv(f_h<<FRACBITS, max_h);

    min_scale_mtof = a < b ? a : b;
    max_scale_mtof = FixedDiv(f_h<<FRACBITS, 2*PLAYERRADIUS);
}

//
// AM_changeWindowLoc()
//
// Moves the map window by the global variables m_paninc.x, m_paninc.y
//
// Passed nothing, returns nothing
//
static void AM_changeWindowLoc(void)
{
    if (m_paninc.x || m_paninc.y)
    {
        _g->automapmode &= ~am_follow;
        f_oldloc.x = INT32_MAX;
    }

    m_x += m_paninc.x;
    m_y += m_paninc.y;

    if (m_x + m_w/2 > max_x)
        m_x = max_x - m_w/2;
    else if (m_x + m_w/2 < min_x)
        m_x = min_x - m_w/2;

    if (m_y + m_h/2 > max_y)
        m_y = max_y - m_h/2;
    else if (m_y + m_h/2 < min_y)
        m_y = min_y - m_h/2;

    m_x2 = m_x + m_w;
    m_y2 = m_y + m_h;
}


//
// AM_initVariables()
//
// Initialize the variables for the automap
//
// Affects the automap global variables
// Status bar is notified that the automap has been entered
// Passed nothing, returns nothing
//
static void AM_initVariables(void)
{
    static const event_t st_notify = { ev_keyup, AM_MSGENTERED, 0, 0 };

    _g->automapmode |= am_active;

    f_oldloc.x = INT32_MAX;

    m_paninc.x = m_paninc.y = 0;

    m_w = FTOM(f_w);
    m_h = FTOM(f_h);


    m_x = (_g->player.mo->x >> FRACTOMAPBITS) - m_w/2;//e6y
    m_y = (_g->player.mo->y >> FRACTOMAPBITS) - m_h/2;//e6y
    AM_changeWindowLoc();

    // inform the status bar of the change
    ST_Responder(&st_notify);
}

//
// AM_LevelInit()
//
// Initialize the automap at the start of a new level
// should be called at the start of every level
//
// Passed nothing, returns nothing
// Affects automap's global variables
//
// CPhipps - get status bar height from status bar code
static void AM_LevelInit(void)
{
    AM_findMinMaxBoundaries();
    scale_mtof = FixedDiv(min_scale_mtof, (int32_t) (0.7*FRACUNIT));
    if (scale_mtof > max_scale_mtof)
        scale_mtof = min_scale_mtof;
    scale_ftom = FixedDiv(FRACUNIT, scale_mtof);
}

//
// AM_Stop()
//
// Cease automap operations, unload patches, notify status bar
//
// Passed nothing, returns nothing
//
void AM_Stop (void)
{
    static const event_t st_notify = { 0, ev_keyup, AM_MSGEXITED, 0 };

    _g->automapmode  = 0;
    ST_Responder(&st_notify);
    stopped = true;
}

//
// AM_Start()
//
// Start up automap operations,
//  if a new level, or game start, (re)initialize level variables
//  init map variables
//  load mark patches
//
// Passed nothing, returns nothing
//
static void AM_Start(void)
{
    if (!stopped)
        AM_Stop();

    stopped = false;
    if (lastlevel != _g->gamemap || lastepisode != 1)
    {
        AM_LevelInit();
        lastlevel = _g->gamemap;
        lastepisode = 1;
    }
    AM_initVariables();
}

//
// AM_minOutWindowScale()
//
// Set the window scale to the maximum size
//
// Passed nothing, returns nothing
//
static void AM_minOutWindowScale(void)
{
    scale_mtof = min_scale_mtof;
    scale_ftom = FixedDiv(FRACUNIT, scale_mtof);
    AM_activateNewScale();
}

//
// AM_maxOutWindowScale(void)
//
// Set the window scale to the minimum size
//
// Passed nothing, returns nothing
//
static void AM_maxOutWindowScale(void)
{
    scale_mtof = max_scale_mtof;
    scale_ftom = FixedDiv(FRACUNIT, scale_mtof);
    AM_activateNewScale();
}

//
// AM_Responder()
//
// Handle events (user inputs) in automap mode
//
// Passed an input event, returns true if its handled
//
boolean AM_Responder
( event_t*  ev )
{
    int32_t rc;
    int32_t ch;                                                       // phares

    rc = false;

    if (!(_g->automapmode & am_active))
    {
        if (ev->type == ev_keydown && ev->data1 == key_map)         // phares
        {
            AM_Start ();
            rc = true;
        }
    }
    else if (ev->type == ev_keydown)
    {
        rc = true;
        ch = ev->data1;                                             // phares

        if (ch == key_map_right)                                    //    |
            if (!(_g->automapmode & am_follow))                           //    V
                m_paninc.x = FTOM(F_PANINC);
            else
                rc = false;
        else if (ch == key_map_left)
            if (!(_g->automapmode & am_follow))
                m_paninc.x = -FTOM(F_PANINC);
            else
                rc = false;
        else if (ch == key_map_up)
            if (!(_g->automapmode & am_follow))
                m_paninc.y = FTOM(F_PANINC);
            else
                rc = false;
        else if (ch == key_map_down)
            if (!(_g->automapmode & am_follow))
                m_paninc.y = -FTOM(F_PANINC);
            else
                rc = false;
        else if (ch == key_map)
        {
            if(_g->automapmode & am_overlay)
                AM_Stop ();
            else
                _g->automapmode |= (am_overlay | am_rotate | am_follow);
        }
        else if (ch == key_map_follow && _g->gamekeydown[key_use])
        {
            _g->automapmode ^= am_follow;     // CPhipps - put all automap mode stuff into one enum
            f_oldloc.x = INT32_MAX;
            // Ty 03/27/98 - externalized
            _g->player.message = (_g->automapmode & am_follow) ? AMSTR_FOLLOWON : AMSTR_FOLLOWOFF;
        }                                                         //    |
        else if (ch == key_map_zoomout)
        {
            mtof_zoommul = M_ZOOMOUT;
            ftom_zoommul = M_ZOOMIN;
        }
        else if (ch == key_map_zoomin)
        {
            mtof_zoommul = M_ZOOMIN;
            ftom_zoommul = M_ZOOMOUT;
        }
        else                                                        // phares
        {
            rc = false;
        }
    }
    else if (ev->type == ev_keyup)
    {
        rc = false;
        ch = ev->data1;
        if (ch == key_map_right)
        {
            if (!(_g->automapmode & am_follow))
                m_paninc.x = 0;
        }
        else if (ch == key_map_left)
        {
            if (!(_g->automapmode & am_follow))
                m_paninc.x = 0;
        }
        else if (ch == key_map_up)
        {
            if (!(_g->automapmode & am_follow))
                m_paninc.y = 0;
        }
        else if (ch == key_map_down)
        {
            if (!(_g->automapmode & am_follow))
                m_paninc.y = 0;
        }
        else if ((ch == key_map_zoomout) || (ch == key_map_zoomin))
        {
            mtof_zoommul = FRACUNIT;
            ftom_zoommul = FRACUNIT;
        }
    }
    return rc;
}

//
// AM_rotate()
//
// Rotation in 2D.
// Used to rotate player arrow line character.
//
// Passed the coordinates of a point, and an angle
// Returns the coordinates rotated by the angle
//
// CPhipps - made static & enhanced for automap rotation

static void AM_rotate(fixed_t* x,  fixed_t* y, angle_t a, fixed_t xorig, fixed_t yorig)
{
    fixed_t tmpx;

    //e6y
    xorig>>=FRACTOMAPBITS;
    yorig>>=FRACTOMAPBITS;

    tmpx =
            FixedMul(*x - xorig,finecosine(a>>ANGLETOFINESHIFT)) -
            FixedMul(*y - yorig,finesine(  a>>ANGLETOFINESHIFT));

    *y   = yorig +
            FixedMul(*x - xorig,finesine(  a>>ANGLETOFINESHIFT)) +
            FixedMul(*y - yorig,finecosine(a>>ANGLETOFINESHIFT));

    *x = tmpx + xorig;
}

//
// AM_changeWindowScale()
//
// Automap zooming
//
// Passed nothing, returns nothing
//
static void AM_changeWindowScale(void)
{
    // Change the scaling multipliers
    scale_mtof = FixedMul(scale_mtof, mtof_zoommul);
    scale_ftom = FixedDiv(FRACUNIT, scale_mtof);

    if (scale_mtof < min_scale_mtof)
        AM_minOutWindowScale();
    else if (scale_mtof > max_scale_mtof)
        AM_maxOutWindowScale();
    else
        AM_activateNewScale();
}

//
// AM_doFollowPlayer()
//
// Turn on follow mode - the map scrolls opposite to player motion
//
// Passed nothing, returns nothing
//
static void AM_doFollowPlayer(void)
{
    if (f_oldloc.x != _g->player.mo->x || f_oldloc.y != _g->player.mo->y)
    {
        m_x = FTOM(MTOF(_g->player.mo->x >> FRACTOMAPBITS)) - m_w/2;//e6y
        m_y = FTOM(MTOF(_g->player.mo->y >> FRACTOMAPBITS)) - m_h/2;//e6y
        m_x2 = m_x + m_w;
        m_y2 = m_y + m_h;
        f_oldloc.x = _g->player.mo->x;
        f_oldloc.y = _g->player.mo->y;
    }
}

//
// AM_Ticker()
//
// Updates on gametic - enter follow mode, zoom, or change map location
//
// Passed nothing, returns nothing
//
void AM_Ticker (void)
{
    if (!(_g->automapmode & am_active))
        return;

    if (_g->automapmode & am_follow)
        AM_doFollowPlayer();

    // Change the zoom if necessary
    if (ftom_zoommul != FRACUNIT)
        AM_changeWindowScale();

    // Change x,y location
    if (m_paninc.x || m_paninc.y)
        AM_changeWindowLoc();
}

//
// AM_clipMline()
//
// Automap clipping of lines.
//
// Based on Cohen-Sutherland clipping algorithm but with a slightly
// faster reject and precalculated slopes. If the speed is needed,
// use a hash algorithm to handle the common cases.
//
// Passed the line's coordinates on map and in the frame buffer performs
// clipping on them in the lines frame coordinates.
// Returns true if any part of line was not clipped
//
static boolean AM_clipMline(mline_t*  ml, fline_t*  fl)
{
    enum
    {
        LEFT    =1,
        RIGHT   =2,
        BOTTOM  =4,
        TOP     =8
    };

    register int32_t outcode1 = 0;
    register int32_t outcode2 = 0;
    register int32_t outside;

    fpoint_t  tmp;
    int32_t   dx;
    int32_t   dy;


#define DOOUTCODE(oc, mx, my) \
    (oc) = 0; \
    if ((my) < 0) (oc) |= TOP; \
    else if ((my) >= f_h) (oc) |= BOTTOM; \
    if ((mx) < 0) (oc) |= LEFT; \
    else if ((mx) >= f_w) (oc) |= RIGHT;


    // do trivial rejects and outcodes
    if (ml->a.y > m_y2)
        outcode1 = TOP;
    else if (ml->a.y < m_y)
        outcode1 = BOTTOM;

    if (ml->b.y > m_y2)
        outcode2 = TOP;
    else if (ml->b.y < m_y)
        outcode2 = BOTTOM;

    if (outcode1 & outcode2)
        return false; // trivially outside

    if (ml->a.x < m_x)
        outcode1 |= LEFT;
    else if (ml->a.x > m_x2)
        outcode1 |= RIGHT;

    if (ml->b.x < m_x)
        outcode2 |= LEFT;
    else if (ml->b.x > m_x2)
        outcode2 |= RIGHT;

    if (outcode1 & outcode2)
        return false; // trivially outside

    // transform to frame-buffer coordinates.
    fl->a.x = CXMTOF(ml->a.x);
    fl->a.y = CYMTOF(ml->a.y);
    fl->b.x = CXMTOF(ml->b.x);
    fl->b.y = CYMTOF(ml->b.y);

    DOOUTCODE(outcode1, fl->a.x, fl->a.y)
            DOOUTCODE(outcode2, fl->b.x, fl->b.y)

            if (outcode1 & outcode2)
            return false;

    while (outcode1 | outcode2)
    {
        // may be partially inside box
        // find an outside point
        if (outcode1)
            outside = outcode1;
        else
            outside = outcode2;

        // clip to each side
        if (outside & TOP)
        {
            dy = fl->a.y - fl->b.y;
            dx = fl->b.x - fl->a.x;
            tmp.x = fl->a.x + (dx*(fl->a.y))/dy;
            tmp.y = 0;
        }
        else if (outside & BOTTOM)
        {
            dy = fl->a.y - fl->b.y;
            dx = fl->b.x - fl->a.x;
            tmp.x = fl->a.x + (dx*(fl->a.y-f_h))/dy;
            tmp.y = f_h-1;
        }
        else if (outside & RIGHT)
        {
            dy = fl->b.y - fl->a.y;
            dx = fl->b.x - fl->a.x;
            tmp.y = fl->a.y + (dy*(f_w-1 - fl->a.x))/dx;
            tmp.x = f_w-1;
        }
        else if (outside & LEFT)
        {
            dy = fl->b.y - fl->a.y;
            dx = fl->b.x - fl->a.x;
            tmp.y = fl->a.y + (dy*(-fl->a.x))/dx;
            tmp.x = 0;
        }

        if (outside == outcode1)
        {
            fl->a = tmp;
            DOOUTCODE(outcode1, fl->a.x, fl->a.y)
        }
        else
        {
            fl->b = tmp;
            DOOUTCODE(outcode2, fl->b.x, fl->b.y)
        }

        if (outcode1 & outcode2)
            return false; // trivially outside
    }

    return true;
}
#undef DOOUTCODE


static void V_PlotPixel(int32_t x, int32_t y, int32_t color)
{
    byte* fb = (byte*)_g->screen;

    byte* dest = &fb[(ScreenYToOffset(y) << 1) + x];

    //The GBA must write in 16bits.
    if((uint32_t)dest & 1)
    {
        //Odd addreses, we combine existing pixel with new one.
        uint16_t* dest16 = (uint16_t*)(dest - 1);

        uint16_t old = *dest16;

        *dest16 = (old & 0xff) | (color << 8);
    }
    else
    {
        uint16_t* dest16 = (uint16_t*)dest;

        uint16_t old = *dest16;

        *dest16 = ((color & 0xff) | (old & 0xff00));
    }
}


//
// WRAP_V_DrawLine()
//
// Draw a line in the frame buffer.
// Classic Bresenham w/ whatever optimizations needed for speed
//
// Passed the frame coordinates of line, and the color to be drawn
// Returns nothing
//
static void V_DrawLine(fline_t* fl, int32_t color)
{
    int32_t x0 = fl->a.x;
    int32_t x1 = fl->b.x;

    int32_t y0 = fl->a.y;
    int32_t y1 = fl->b.y;

    int32_t dx =  D_abs(x1-x0);
    int32_t sx = x0<x1 ? 1 : -1;

    int32_t dy = -D_abs(y1-y0);
    int32_t sy = y0<y1 ? 1 : -1;

    int32_t err = dx + dy;

    while(true)
    {
        V_PlotPixel(x0, y0, color);

        if (x0==x1 && y0==y1)
            break;

        int32_t e2 = 2*err;

        if (e2 >= dy)
        {
            err += dy;
            x0 += sx;
        }

        if (e2 <= dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}


//
// AM_drawMline()
//
// Clip lines, draw visible parts of lines.
//
// Passed the map coordinates of the line, and the color to draw it
// Color -1 is special and prevents drawing. Color 247 is special and
// is translated to black, allowing Color 0 to represent feature disable
// in the defaults file.
// Returns nothing.
//
static void AM_drawMline(mline_t* ml,int32_t color)
{
    fline_t fl;

    if (color==-1)  // jff 4/3/98 allow not drawing any sort of line
        return;       // by setting its color to -1
    if (color==247) // jff 4/3/98 if color is 247 (xparent), use black
        color=0;

    if (AM_clipMline(ml, &fl))
        V_DrawLine(&fl, color); // draws it on frame buffer using fb coords
}

//
// AM_DoorColor()
//
// Returns the 'color' or key needed for a door linedef type
//
// Passed the type of linedef, returns:
//   -1 if not a keyed door
//    0 if a red key required
//    1 if a blue key required
//    2 if a yellow key required
//    3 if a multiple keys required
//
// jff 4/3/98 add routine to get color of generalized keyed door
//
static int32_t AM_DoorColor(int32_t type)
{
    if (GenLockedBase <= type && type< GenDoorBase)
    {
        type -= GenLockedBase;
        type = (type & LockedKey) >> LockedKeyShift;
        if (!type || type==7)
            return 3;  //any or all keys
        else return (type-1)%3;
    }
    switch (type)  // closed keyed door
    {
    case 26: case 32: case 99: case 133:
        /*bluekey*/
        return 1;
    case 27: case 34: case 136: case 137:
        /*yellowkey*/
        return 2;
    case 28: case 33: case 134: case 135:
        /*redkey*/
        return 0;
    default:
        return -1; //not a keyed door
    }
}

//
// Determines visible lines, draws them.
// This is LineDef based, not LineSeg based.
//
// jff 1/5/98 many changes in this routine
// backward compatibility not needed, so just changes, no ifs
// addition of clauses for:
//    doors opening, keyed door id, secret sectors,
//    teleports, exit lines, key things
// ability to suppress any of added features or lines with no height changes
//
// support for gamma correction in automap abandoned
//
// jff 4/3/98 changed mapcolor_xxxx=0 as control to disable feature
// jff 4/3/98 changed mapcolor_xxxx=-1 to disable drawing line completely
//
static void AM_drawWalls(void)
{
    int32_t i;
    mline_t l;

    // draw the unclipped visible portions of all lines
    for (i=0;i<_g->numlines;i++)
    {
        l.a.x = _g->lines[i].v1.x >> FRACTOMAPBITS;//e6y
        l.a.y = _g->lines[i].v1.y >> FRACTOMAPBITS;//e6y
        l.b.x = _g->lines[i].v2.x >> FRACTOMAPBITS;//e6y
        l.b.y = _g->lines[i].v2.y >> FRACTOMAPBITS;//e6y


        const sector_t* backsector = LN_BACKSECTOR(&_g->lines[i]);
        const sector_t* frontsector = LN_FRONTSECTOR(&_g->lines[i]);

        const uint32_t line_special =  LN_SPECIAL(&_g->lines[i]);

        if (_g->automapmode & am_rotate)
        {
            AM_rotate(&l.a.x, &l.a.y, ANG90-_g->player.mo->angle, _g->player.mo->x, _g->player.mo->y);
            AM_rotate(&l.b.x, &l.b.y, ANG90-_g->player.mo->angle, _g->player.mo->x, _g->player.mo->y);
        }

        // if line has been seen or IDDT has been used
        if (_g->linedata[i].r_flags & ML_MAPPED)
        {
            if (_g->lines[i].flags & ML_DONTDRAW)
                continue;
            {
                /* cph - show keyed doors and lines */
                int32_t amd;
                if (!(_g->lines[i].flags & ML_SECRET) && (amd = AM_DoorColor(line_special)) != -1)
                {
                    {
                        switch (amd) /* closed keyed door */
                        {
                        case 1:
                            /*bluekey*/
                            AM_drawMline(&l,mapcolor_bdor);
                            continue;
                        case 2:
                            /*yellowkey*/
                            AM_drawMline(&l,mapcolor_ydor);
                            continue;
                        case 0:
                            /*redkey*/
                            AM_drawMline(&l,mapcolor_rdor);
                            continue;
                        case 3:
                            /*any or all*/
                            AM_drawMline(&l, mapcolor_clsd);
                            continue;
                        }
                    }
                }
            }
            if /* jff 4/23/98 add exit lines to automap */
            (
            mapcolor_exit &&
                    (
                        line_special==11 ||
                        line_special==52 ||
                        line_special==197 ||
                        line_special==51  ||
                        line_special==124 ||
                        line_special==198
                        )
                    ) {
                AM_drawMline(&l, mapcolor_exit); /* exit line */
                continue;
            }

            if(!backsector)
            {
                // jff 1/10/98 add new color for 1S secret sector boundary
                if (mapcolor_secr && //jff 4/3/98 0 is disable
                        (
                            (
                                map_secret_after &&
                                P_WasSecret(frontsector) &&
                                !P_IsSecret(frontsector)
                                )
                            ||
                            (
                                !map_secret_after &&
                                P_WasSecret(frontsector)
                                )
                            )
                        )
                    AM_drawMline(&l, mapcolor_secr); // line bounding secret sector
                else                               //jff 2/16/98 fixed bug
                    AM_drawMline(&l, mapcolor_wall); // special was cleared
            }
            else /* now for 2S lines */
            {
                // jff 1/10/98 add color change for all teleporter types
                if
                        (
                         mapcolor_tele && !(_g->lines[i].flags & ML_SECRET) &&
                         (line_special == 39 || line_special == 97 ||
                          line_special == 125 || line_special == 126)
                         )
                { // teleporters
                    AM_drawMline(&l, mapcolor_tele);
                }
                else if (_g->lines[i].flags & ML_SECRET)    // secret door
                {
                    AM_drawMline(&l, mapcolor_wall);      // wall color
                }
                else if
                        (
                         mapcolor_clsd &&
                         !(_g->lines[i].flags & ML_SECRET) &&    // non-secret closed door
                         ((backsector->floorheight==backsector->ceilingheight) ||
                          (frontsector->floorheight==frontsector->ceilingheight))
                         )
                {
                    AM_drawMline(&l, mapcolor_clsd);      // non-secret closed door
                } //jff 1/6/98 show secret sector 2S lines
                else if
                        (
                         mapcolor_secr && //jff 2/16/98 fixed bug
                         (                    // special was cleared after getting it
                                              (map_secret_after &&
                                               (
                                                   (P_WasSecret(frontsector)
                                                    && !P_IsSecret(frontsector)) ||
                                                   (P_WasSecret(backsector)
                                                    && !P_IsSecret(backsector))
                                                   )
                                               )
                                              ||  //jff 3/9/98 add logic to not show secret til after entered
                                              (   // if map_secret_after is true
                                                  !map_secret_after &&
                                                  (P_WasSecret(frontsector) ||
                                                   P_WasSecret(backsector))
                                                  )
                                              )
                         )
                {
                    AM_drawMline(&l, mapcolor_secr); // line bounding secret sector
                } //jff 1/6/98 end secret sector line change
                else if (backsector->floorheight !=
                         frontsector->floorheight)
                {
                    AM_drawMline(&l, mapcolor_fchg); // floor level change
                }
                else if (backsector->ceilingheight !=
                         frontsector->ceilingheight)
                {
                    AM_drawMline(&l, mapcolor_cchg); // ceiling level change
                }
            }
        } // now draw the lines only visible because the player has computermap
        else if (_g->player.powers[pw_allmap]) // computermap visible lines
        {
            if (!(_g->lines[i].flags & ML_DONTDRAW)) // invisible flag lines do not show
            {
                if
                        (
                         mapcolor_flat
                         ||
                         !backsector
                         ||
                         backsector->floorheight
                         != frontsector->floorheight
                         ||
                         backsector->ceilingheight
                         != frontsector->ceilingheight
                         )
                    AM_drawMline(&l, mapcolor_unsn);
            }
        }
    }
}

//
// AM_drawLineCharacter()
//
// Draws a vector graphic according to numerous parameters
//
// Passed the structure defining the vector graphic shape, the number
// of vectors in it, the scale to draw it at, the angle to draw it at,
// the color to draw it with, and the map coordinates to draw it at.
// Returns nothing
//
static void AM_drawLineCharacter(const mline_t* lineguy, int32_t lineguylines, fixed_t scale, angle_t angle, int32_t color, fixed_t x, fixed_t y)
{
    int32_t   i;
    mline_t l;

    if (_g->automapmode & am_rotate) angle -= _g->player.mo->angle - ANG90; // cph

    for (i=0;i<lineguylines;i++)
    {
        l.a.x = lineguy[i].a.x;
        l.a.y = lineguy[i].a.y;

        if (scale)
        {
            l.a.x = FixedMul(scale, l.a.x);
            l.a.y = FixedMul(scale, l.a.y);
        }

        if (angle)
            AM_rotate(&l.a.x, &l.a.y, angle, 0, 0);

        l.a.x += x;
        l.a.y += y;

        l.b.x = lineguy[i].b.x;
        l.b.y = lineguy[i].b.y;

        if (scale)
        {
            l.b.x = FixedMul(scale, l.b.x);
            l.b.y = FixedMul(scale, l.b.y);
        }

        if (angle)
            AM_rotate(&l.b.x, &l.b.y, angle, 0, 0);

        l.b.x += x;
        l.b.y += y;

        AM_drawMline(&l, color);
    }
}

//
// AM_drawPlayers()
//
// Draws the player arrow in single player,
//
// Passed nothing, returns nothing
//
static void AM_drawPlayers(void)
{    
    AM_drawLineCharacter
            (
                player_arrow,
                NUMPLYRLINES,
                0,
                _g->player.mo->angle,
                mapcolor_sngl,      //jff color
                _g->player.mo->x >> FRACTOMAPBITS,//e6y
                _g->player.mo->y >> FRACTOMAPBITS);//e6y

}


//
// V_FillRect
//
// CPhipps - New function to fill a rectangle with a given colour
static void V_FillRect(void)
{
    byte* dest = (byte*)_g->screen;
    int32_t height = f_h;
    while (height--)
    {
        memset(dest, mapcolor_back, f_w);
        dest += (SCREENPITCH << 1);
    }
}


//
// AM_Drawer()
//
// Draws the entire automap
//
// Passed nothing, returns nothing
//
void AM_Drawer (void)
{
    // CPhipps - all automap modes put into one enum
    if (!(_g->automapmode & am_active)) return;

    if (!(_g->automapmode & am_overlay)) // cph - If not overlay mode, clear background for the automap
        V_FillRect(); //jff 1/5/98 background default color

    AM_drawWalls();
    AM_drawPlayers();
}
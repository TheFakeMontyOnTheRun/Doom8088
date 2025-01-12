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
 *      Game completion, final screen animation.
 *
 *-----------------------------------------------------------------------------
 */

#include <stdint.h>
#include "doomstat.h"
#include "d_event.h"
#include "v_video.h"
#include "w_wad.h"
#include "s_sound.h"
#include "sounds.h"
#include "f_finale.h" // CPhipps - hmm...
#include "d_englsh.h"

#include "globdata.h"


// defines for the end mission display text                     // phares

#define TEXTSPEED    300   // original value                    // phares
#define TEXTWAIT     250   // original value                    // phares
#define NEWTEXTSPEED 1     // new value                         // phares
#define NEWTEXTWAIT  1000  // new value                         // phares

// CPhipps - removed the old finale screen text message strings;
// they were commented out for ages already
// Ty 03/22/98 - ... the new s_WHATEVER extern variables are used
// in the code below instead.

void WI_checkForAccelerate(void);    // killough 3/28/98: used to

//
// F_StartFinale
//
void F_StartFinale (void)
{
    _g->gameaction = ga_nothing;
    _g->gamestate = GS_FINALE;
    _g->automapmode &= ~am_active;

    // killough 3/28/98: clear accelerative text flags
    _g->acceleratestage = _g->midstage = false;

    // Okay - IWAD dependend stuff.
    // This has been changed severly, and
    //  some stuff might have changed in the process.

    S_ChangeMusic(mus_victor, true);

    _g->finalestage = false;
    _g->finalecount = 0;
}



boolean F_Responder (event_t *event)
{
	UNUSED(event);
	return false;
}

// Get_TextSpeed() returns the value of the text display speed  // phares
// Rewritten to allow user-directed acceleration -- killough 3/28/98

static int32_t Get_TextSpeed(void)
{
    return _g->midstage ? NEWTEXTSPEED : (_g->midstage=_g->acceleratestage) ?
                              _g->acceleratestage=false, NEWTEXTSPEED : TEXTSPEED;
    }


//
// F_Ticker
//
// killough 3/28/98: almost totally rewritten, to use
// player-directed acceleration instead of constant delays.
// Now the player can accelerate the text display by using
// the fire/use keys while it is being printed. The delay
// automatically responds to the user, and gives enough
// time to read.
//
// killough 5/10/98: add back v1.9 demo compatibility
//

    void F_Ticker(void)
    {

    WI_checkForAccelerate();  // killough 3/28/98: check for acceleration

    // advance animation
    _g->finalecount++;

    if (!_g->finalestage)
    {
        int32_t speed = Get_TextSpeed();
        /* killough 2/28/98: changed to allow acceleration */
        if (_g->finalecount > strlen(E1TEXT)*speed/100 +
                (_g->midstage ? NEWTEXTWAIT : TEXTWAIT) ||
                (_g->midstage && _g->acceleratestage))
        {
       // Doom 1 end
                               // with enough time, it's automatic
            _g->finalecount = 0;
            _g->finalestage = true;
            _g->wipegamestate = -1;         // force a wipe
        }
    }
}


/*
 * V_DrawBackground tiles a 64x64 patch over the entire screen, providing the
 * background for the Help and Setup screens, and plot text between levels.
 * cphipps - used to have M_DrawBackground, but that was used the framebuffer
 * directly, so this is my code from the equivalent function in f_finale.c
 */
static void V_DrawBackground(const char* flatname)
{
    /* erase the entire screen to a tiled background */
    const byte* src = W_GetLumpByName(flatname);
    uint16_t *dest = _g->screen;

    for(uint8_t y = 0; y < SCREENHEIGHT; y++)
    {
        for(uint16_t x = 0; x < 240; x+=64)
        {
            uint16_t* d = &dest[ ScreenYToOffset(y) + (x >> 1)];
            const byte* s = &src[((y&63) * 64) + (x&63)];

            uint8_t len = 64;

            if( (240-x) < 64)
                len = 240-x;

            memcpy(d, s, len);
        }
    }

    Z_Free(src);
}


//
// F_TextWrite
//
// This program displays the background and text at end-mission     // phares
// text time. It draws both repeatedly so that other displays,      //   |
// like the main menu, can be drawn over it dynamically and         //   V
// erased dynamically. The TEXTSPEED constant is changed into
// the Get_TextSpeed function so that the speed of writing the      //   ^
// text can be increased, and there's still time to read what's     //   |
// written.                                                         // phares
// CPhipps - reformatted

#include "hu_stuff.h"

static void F_TextWrite (void)
{
	V_DrawBackground("FLOOR4_8");

	// load the heads-up font
	int8_t		i;
	int8_t		j;
	char	buffer[9];
	const patch_t* hu_font[HU_FONTSIZE];

	j = HU_FONTSTART;
	for (i = 0; i < HU_FONTSIZE; i++)
	{
		sprintf(buffer, "STCFN%.3d", j++);
		hu_font[i] = (const patch_t *) W_GetLumpByName(buffer);
	}

	// draw some of the text onto the screen
	int16_t         cx = 10;
	int16_t         cy = 10;
	const char* ch = E1TEXT; // CPhipps - const
	int32_t         count = (_g->finalecount - 10)*100/Get_TextSpeed(); // phares

	if (count < 0)
		count = 0;

	for ( ; count ; count-- )
	{
		char c = *ch++;

		if (!c)
			break;
		if (c == '\n')
		{
			cx = 10;
			cy += 11;
			continue;
		}

		c = toupper(c);
		if (HU_FONTSTART <= c && c <= HU_FONTEND) {
			// CPhipps - patch drawing updated
			i = c - HU_FONTSTART;
			V_DrawPatchNoScale(cx, cy, hu_font[i]);
			cx += hu_font[i]->width;
		} else {
			cx += HU_FONT_SPACE_WIDTH;
		}
	}

	// free the heads-up font
	for (i = 0; i < HU_FONTSIZE; i++)
		Z_Free(hu_font[i]);
}


//
// F_Drawer
//
void F_Drawer (void)
{
    if (!_g->finalestage)
        F_TextWrite ();
    else
        W_ReadLumpByName("HELP2", _g->screen);
}

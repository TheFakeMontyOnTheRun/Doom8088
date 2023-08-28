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
 * DESCRIPTION:  Heads-up displays
 *
 *-----------------------------------------------------------------------------
 */

// killough 5/3/98: remove unnecessary headers

#include <stdint.h>
#include "doomstat.h"
#include "r_defs.h"
#include "hu_stuff.h"
#include "st_stuff.h" /* jff 2/16/98 need loc of status bar */
#include "w_wad.h"
#include "s_sound.h"
#include "dstrings.h"
#include "sounds.h"
#include "g_game.h"
#include "r_main.h"

#include "globdata.h"

// global heads up display controls

//
// Locally used constants, shortcuts.
//
// Ty 03/28/98 -
// These shortcuts modifed to reflect char ** of mapnames[]
#define HU_TITLE  (mapnames[_g->gamemap-1])
#define HU_TITLEX 0
//jff 2/16/98 change 167 to ST_Y-1
// CPhipps - changed to ST_TY
// proff - changed to 200-ST_HEIGHT for stretching
#define HU_TITLEY ((SCREENHEIGHT-ST_SCALED_HEIGHT) - 1 - HU_FONT_HEIGHT)


#define HU_MSGX         0
#define HU_MSGY         0


//
// Builtin map names.
// The actual names can be found in DStrings.h.
//
// Ty 03/27/98 - externalized map name arrays - now in d_deh.c
// and converted to arrays of pointers to char *
// DOOM map names.
// CPhipps - const**const
static const char *const mapnames[] =
{
    HUSTR_E1M1,
    HUSTR_E1M2,
    HUSTR_E1M3,
    HUSTR_E1M4,
    HUSTR_E1M5,
    HUSTR_E1M6,
    HUSTR_E1M7,
    HUSTR_E1M8,
    HUSTR_E1M9,
};


//
// HU_Init()
//
// Initialize the heads-up display, text that overwrites the primary display
//
// Passed nothing, returns nothing
//
void HU_Init(void)
{
}

//
// HU_Stop()
//
// Make the heads-up displays inactive
//
// Passed nothing, returns nothing
//
static void HU_Stop(void)
{
    _g->headsupactive = false;
}


//
// HUlib_clearTextLine()
//
// Blank the internal text line in a hu_textline_t widget
//
// Passed a hu_textline_t, returns nothing
//
static void HUlib_clearTextLine(hu_textline_t* t)
{
    t->linelen =         // killough 1/23 98: support multiple lines
            t->len = 0;
    t->l[0] = 0;
    t->needsupdate = true;
}


//
// HUlib_initTextLine()
//
// Initialize a hu_textline_t widget. Set the position.
//
// Passed a hu_textline_t, and the values used to initialize
// Returns nothing
//
static void HUlib_initTextLine(hu_textline_t* t, int16_t x, int16_t y)
//jff 2/16/98 add color range parameter
{
    t->x = x;
    t->y = y;
    HUlib_clearTextLine(t);
}


//
// HUlib_initSText()
//
// Initialize a hu_stext_t widget. Set whether enabled.
//
// Passed a hu_stext_t, and the values used to initialize
// Returns nothing
//
static void HUlib_initSText(hu_stext_t* s, boolean* on)
{
	s->on     = on;
	s->laston = true;

	HUlib_initTextLine(&s->l, HU_MSGX, HU_MSGY);
}


//
// HUlib_addCharToTextLine()
//
// Adds a character at the end of the text line in a hu_textline_t widget
//
// Passed the hu_textline_t and the char to add
//
static void HUlib_addCharToTextLine(hu_textline_t* t,char ch)
{
	if (t->linelen != HU_MAXLINELENGTH)
	{
		t->linelen++;
		if (ch == '\n')
			t->linelen=0;

		t->l[t->len++] = ch;
		t->l[t->len] = 0;
		t->needsupdate = 4;
	}
}


//
// HU_Start(void)
//
// Create and initialize the heads-up widgets, software machines to
// maintain, update, and display information over the primary display
//
// This routine must be called after any change to the heads up configuration
// in order for the changes to take effect in the actual displays
//
// Passed nothing, returns nothing
//
void HU_Start(void)
{
    const char* s; /* cph - const */

    if (_g->headsupactive)                    // stop before starting
        HU_Stop();


    _g->message_on = false;
    _g->message_dontfuckwithme = false;

    // create the message widget
    // messages to player in upper-left of screen
    HUlib_initSText(&_g->w_message, &_g->message_on);

    //jff 2/16/98 added some HUD widgets
    // create the map title widget - map title display in lower left of automap
    HUlib_initTextLine(&_g->w_title, HU_TITLEX, HU_TITLEY);

    // initialize the automap's level title widget
    if (_g->gamestate == GS_LEVEL) /* cph - stop SEGV here when not in level */
        s = HU_TITLE;
    else s = "";
    while (*s)
        HUlib_addCharToTextLine(&_g->w_title, *(s++));


    // now allow the heads-up display to run
    _g->headsupactive = true;
}


//
// HUlib_drawTextLine()
//
// Draws a hu_textline_t widget
//
// Passed the hu_textline_t and flag whether to draw a cursor
// Returns nothing
//
static void HUlib_drawTextLine(hu_textline_t* l)
{
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


	int16_t y = l->y;           // killough 1/18/98 -- support multiple lines

	// draw the new stuff
	int16_t x = l->x;
	for (i = 0; i < l->len; i++)
	{
		char c = toupper(l->l[i]); //jff insure were not getting a cheap toupper conv.

		if (c=='\n')         // killough 1/18/98 -- support multiple lines
			x=0,y+=8;
		else if (c=='\t')    // killough 1/23/98 -- support tab stops
			x=x-x%80+80;
		else if (HU_FONTSTART <= c && c <= HU_FONTEND)
		{
			int16_t j = c - HU_FONTSTART;
			int16_t w = hu_font[j]->width;
			if (x + w > 240)
				break;
			// killough 1/18/98 -- support multiple lines:
			// CPhipps - patch drawing updated
			V_DrawPatchNoScale(x, y, hu_font[j]);
			x += w;
		}
		else
		{
			x += HU_FONT_SPACE_WIDTH;
			if (x >= 240)
				break;
		}
	}


	// free the heads-up font
	for (i = 0; i < HU_FONTSIZE; i++)
		Z_Free(hu_font[i]);
}


//
// HUlib_drawSText()
//
// Displays a hu_stext_t widget
//
// Passed a hu_stext_t
// Returns nothing
//
static void HUlib_drawSText(hu_stext_t* s)
{
	if (!*s->on)
		return; // if not on, don't draw

	// draw everything
	HUlib_drawTextLine(&s->l); // no cursor, please
}


//
// HU_Drawer()
//
// Draw all the pieces of the heads-up display
//
// Passed nothing, returns nothing
//
void HU_Drawer(void)
{
    // draw the automap widgets if automap is displayed
    if (_g->automapmode & am_active)
    {
        // map title
        HUlib_drawTextLine(&_g->w_title);
    }

    //jff 3/4/98 display last to give priority
    HU_Erase(); // jff 4/24/98 Erase current lines before drawing current
    // needed when screen not fullsize


    HUlib_drawSText(&_g->w_message);
}


//
// HUlib_eraseTextLine()
//
// Erases a hu_textline_t widget when screen border is behind text
// Sorta called by HU_Erase and just better darn get things straight
//
// Passed the hu_textline_t
// Returns nothing
//
static void HUlib_eraseTextLine(hu_textline_t* l)
{
    if (l->needsupdate)
        l->needsupdate--;
}


//
// HUlib_eraseSText()
//
// Erases a hu_stext_t widget, when the screen is not fullsize
//
// Passed a hu_stext_t
// Returns nothing
//
static void HUlib_eraseSText(hu_stext_t* s)
{
	if (s->laston && !*s->on)
		s->l.needsupdate = 4;

	HUlib_eraseTextLine(&s->l);

	s->laston = *s->on;
}


//
// HU_Erase()
//
// Erase hud display lines that can be trashed by small screen display
//
// Passed nothing, returns nothing
//
void HU_Erase(void)
{
    // erase the message display or the message review display
    HUlib_eraseSText(&_g->w_message);

    // erase the automap title
    HUlib_eraseTextLine(&_g->w_title);
}


//
// HUlib_addLineToSText()
//
// Adds a blank line to a hu_stext_t widget
//
// Passed a hu_stext_t
// Returns nothing
//
static void HUlib_addLineToSText(hu_stext_t* s)
{
	HUlib_clearTextLine(&s->l);

	s->l.needsupdate = 4;
}


//
// HUlib_addMessageToSText()
//
// Adds a message line to a hu_stext_t widget
//
// Passed a hu_stext_t and a message string
// Returns nothing
//
static void HUlib_addMessageToSText(hu_stext_t* s, const char* msg)
{
	HUlib_addLineToSText(s);

	while (*msg)
		HUlib_addCharToTextLine(&s->l, *(msg++));
}


//
// HU_Ticker()
//
// Update the hud displays once per frame
//
// Passed nothing, returns nothing
//

#define HU_MSGTIMEOUT   (4*TICRATE)

void HU_Ticker(void)
{
    player_t* plr = &_g->player;        // killough 3/7/98

    // tick down message counter if message is up
    if (_g->message_counter && !--_g->message_counter)
    {
        _g->message_on = false;
    }


    // if messages on, or "Messages Off" is being displayed
    // this allows the notification of turning messages off to be seen
    if (_g->showMessages || _g->message_dontfuckwithme)
    {
        // display message if necessary
        if (plr->message)
        {
            //post the message to the message widget
            HUlib_addMessageToSText(&_g->w_message, plr->message);

            // clear the message to avoid posting multiple times
            plr->message = 0;
            // note a message is displayed
            _g->message_on = true;
            // start the message persistence counter
            _g->message_counter = HU_MSGTIMEOUT;

            // clear the flag that "Messages Off" is being posted
            _g->message_dontfuckwithme = 0;
        }
    }
}
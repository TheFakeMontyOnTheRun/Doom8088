/* Emacs style mode select   -*- C++ -*-
 *-----------------------------------------------------------------------------
 *
 *
 *  PrBoom: a Doom port merged with LxDoom and LSDLDoom
 *  based on BOOM, a modified and improved DOOM engine
 *  Copyright (C) 1999 by
 *  id Software, Chi Hoang, Lee Killough, Jim Flynn, Rand Phares, Ty Halderman
 *  Copyright (C) 1999-2000,2002 by
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
 *      Thinker, Ticker.
 *
 *-----------------------------------------------------------------------------*/

#include "doomstat.h"
#include "p_user.h"
#include "p_spec.h"
#include "p_tick.h"
#include "p_map.h"

#include "globdata.h"


//
// THINKERS
// All thinkers should be allocated by Z_Malloc
// so they can be operated on uniformly.
// The actual structures will vary in size,
// but the first element must be thinker_t.
//



//
// P_InitThinkers
//

void P_InitThinkers(void)
{
  _g->thinkerclasscap.prev = _g->thinkerclasscap.next  = &_g->thinkerclasscap;
}

//
// P_AddThinker
// Adds a new thinker at the end of the list.
//

void P_AddThinker(thinker_t* thinker)
{
  _g->thinkerclasscap.prev->next = thinker;
  thinker->next = &_g->thinkerclasscap;
  thinker->prev = _g->thinkerclasscap.prev;
  _g->thinkerclasscap.prev = thinker;
}

//
// killough 11/98:
//
// Make currentthinker external, so that P_RemoveThinkerDelayed
// can adjust currentthinker when thinkers self-remove.


//
// P_RemoveThinkerDelayed()
//
// Called automatically as part of the thinker loop in P_RunThinkers(),
// on nodes which are pending deletion.
//
// If this thinker has no more pointers referencing it indirectly,
// remove it, and set currentthinker to one node preceeding it, so
// that the next step in P_RunThinkers() will get its successor.
//

static void P_RemoveThinkerDelayed(thinker_t *thinker)
{

    thinker_t *next = thinker->next;
    /* Note that currentthinker is guaranteed to point to us,
         * and since we're freeing our memory, we had better change that. So
         * point it to thinker->prev, so the iterator will correctly move on to
         * thinker->prev->next = thinker->next */
    (next->prev = thinker->prev)->next = next;

    Z_Free(thinker);
}

static void P_RemoveThingDelayed(thinker_t *thinker)
{

    thinker_t *next = thinker->next;
    /* Note that currentthinker is guaranteed to point to us,
         * and since we're freeing our memory, we had better change that. So
         * point it to thinker->prev, so the iterator will correctly move on to
         * thinker->prev->next = thinker->next */
    (next->prev = thinker->prev)->next = next;

    mobj_t* thing = (mobj_t*)thinker;

    if(thing->flags & MF_POOLED)
        thing->type = MT_NOTHING;
    else
        Z_Free(thinker);
}

//
// P_RemoveThinker
//
// Deallocation is lazy -- it will not actually be freed
// until its thinking turn comes up.
//
// killough 4/25/98:
//
// Instead of marking the function with -1 value cast to a function pointer,
// set the function to P_RemoveThinkerDelayed(), so that later, it will be
// removed automatically as part of the thinker process.
//

void P_RemoveThinker(thinker_t *thinker)
{
  thinker->function = P_RemoveThinkerDelayed;
}

void P_RemoveThing(mobj_t *thing)
{
  thing->thinker.function = P_RemoveThingDelayed;
}


/* cph 2002/01/13 - iterator for thinker list
 * WARNING: Do not modify thinkers between calls to this function
 */
thinker_t* P_NextThinker(thinker_t* th)
{
  thinker_t* top = &_g->thinkerclasscap;
  if (!th) th = top;
  th = th->next;
  return th == top ? NULL : th;
}

/*
 * P_SetTarget
 *
 * This function is used to keep track of pointer references to mobj thinkers.
 * In Doom, objects such as lost souls could sometimes be removed despite
 * their still being referenced. In Boom, 'target' mobj fields were tested
 * during each gametic, and any objects pointed to by them would be prevented
 * from being removed. But this was incomplete, and was slow (every mobj was
 * checked during every gametic). Now, we keep a count of the number of
 * references, and delay removal until the count is 0.
 */

void P_SetTarget(mobj_t **mop, mobj_t *targ)
{
    *mop = targ;    // Set new target and if non-NULL, increase its counter
}


//
// P_RunThinkers
//
// killough 4/25/98:
//
// Fix deallocator to stop using "next" pointer after node has been freed
// (a Doom bug).
//
// Process each thinker. For thinkers which are marked deleted, we must
// load the "next" pointer prior to freeing the node. In Doom, the "next"
// pointer was loaded AFTER the thinker was freed, which could have caused
// crashes.
//
// But if we are not deleting the thinker, we should reload the "next"
// pointer after calling the function, in case additional thinkers are
// added at the end of the list.
//
// killough 11/98:
//
// Rewritten to delete nodes implicitly, by making currentthinker
// external and using P_RemoveThinkerDelayed() implicitly.
//

static void P_RunThinkers (void)
{
    thinker_t* th = _g->thinkerclasscap.next;
    thinker_t* th_end = &_g->thinkerclasscap;

    while(th != th_end)
    {
        thinker_t* th_next = th->next;
        if(th->function)
            th->function(th);

        th = th_next;
    }
}


void P_Ticker (void)
{
  /* pause if in menu and at least one tic has been run
   *
   * killough 9/29/98: note that this ties in with basetic,
   * since G_Ticker does the pausing during recording or
   * playback, and compenates by incrementing basetic.
   *
   * All of this complicated mess is used to preserve demo sync.
   */

  if (_g->menuactive && !_g->demoplayback && _g->player.viewz != 1)
    return;

  P_MapStart();
               // not if this is an intermission screen
  if(_g->gamestate==GS_LEVEL)
    if (_g->playeringame)
      P_PlayerThink(&_g->player);

  P_RunThinkers();
  P_UpdateSpecials();
  P_RespawnSpecials();
  P_MapEnd();
  _g->leveltime++;                       // for par times
}


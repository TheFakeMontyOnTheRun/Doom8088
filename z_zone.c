// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 2023 by Frenkel Smeijers
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//
// $Log:$
//
// DESCRIPTION:
//	Zone Memory Allocation. Neat.
//
//-----------------------------------------------------------------------------

#include <malloc.h>
#include <stdint.h>
#include "compiler.h"
#include "z_zone.h"
#include "doomdef.h"
#include "doomtype.h"
#include "i_system.h"


//
// ZONE MEMORY
// PU - purge tags.
// Tags < 100 are not overwritten until freed.
#define PU_STATIC		1	// static entire execution time
#define PU_LEVEL		2	// static until level exited
#define PU_LEVSPEC		3      // a special thinker in a level
#define PU_CACHE		4

#define PU_PURGELEVEL PU_CACHE


//
// ZONE MEMORY ALLOCATION
//
// There is never any space between memblocks,
//  and there will never be two contiguous free memblocks.
// The rover can be left pointing at a non-empty block.
//
// It is of no value to free a cachable block,
//  because it will get overwritten automatically if needed.
//

#if defined INSTRUMENTED
    static int32_t running_count = 0;
#endif


#define	ZONEID	0x1dea

typedef struct
{
    uint32_t size:24;	// including the header and possibly tiny fragments
    uint32_t tag:4;		// purgelevel
    void**   user;		// NULL if a free block
    segment  next;
    segment  prev;
#if defined _M_I86
    uint16_t id;		// should be ZONEID
#endif
} memblock_t;

#define PARAGRAPH_SIZE 16

typedef char assertMemblockSize[sizeof(memblock_t) <= PARAGRAPH_SIZE ? 1 : -1];


static uint8_t    *mainzone;
static uint8_t     mainzone_blocklist_buffer[32];
static memblock_t *mainzone_blocklist;
static segment     mainzone_rover;


static segment pointerToSegment(const memblock_t* ptr)
{
	if ((((uint32_t) ptr) & (PARAGRAPH_SIZE - 1)) != 0)
		I_Error("pointerToSegment: pointer is not aligned: 0x%lx", ptr);

	uint32_t seg = FP_SEG(ptr);
	uint16_t off = FP_OFF(ptr);
	uint32_t linear = seg * PARAGRAPH_SIZE + off;
	return linear / PARAGRAPH_SIZE;
}

static memblock_t* segmentToPointer(segment seg)
{
	return MK_FP(seg, 0);
}


//
// Z_Init
//
void Z_Init (void)
{
    memblock_t*	block;

    uint32_t heapSize;
    int32_t hallocNumb = 640 * 1024L / PARAGRAPH_SIZE;

    //Try to allocate memory.
    do
    {
        mainzone = halloc(hallocNumb, PARAGRAPH_SIZE);
        hallocNumb--;

    } while (mainzone == NULL);

    hallocNumb++;
    heapSize = hallocNumb * PARAGRAPH_SIZE;

    //align mainzone
    uint32_t m = (uint32_t) mainzone;
    if ((m & (PARAGRAPH_SIZE - 1)) != 0)
    {
        heapSize -= PARAGRAPH_SIZE;
        while ((m & (PARAGRAPH_SIZE - 1)) != 0)
            m = (uint32_t) ++mainzone;
    }

    printf("\t%ld bytes allocated for zone\n", heapSize);

    //align blocklist
    uint_fast8_t i = 0;
    uint32_t b = (uint32_t) &mainzone_blocklist_buffer[i++];
    while ((b & (PARAGRAPH_SIZE - 1)) != 0)
        b = (uint32_t) &mainzone_blocklist_buffer[i++];
    mainzone_blocklist = (memblock_t *)b;

    // set the entire zone to one free block
    block = (memblock_t *)mainzone;
    mainzone_blocklist->next =
    mainzone_blocklist->prev = pointerToSegment(block);

    mainzone_blocklist->user = (void *)mainzone;
    mainzone_blocklist->tag  = PU_STATIC;

    mainzone_rover = pointerToSegment(block);

    block->prev = block->next = pointerToSegment(mainzone_blocklist);

    // NULL indicates a free block.
    block->user = NULL;

    block->size = heapSize;
}


//
// Z_Free
//
void Z_Free (const void* ptr)
{
    memblock_t*		block;
    memblock_t*		other;

    if (ptr == NULL)
        return;

    block = segmentToPointer(pointerToSegment(ptr) - 1);

#if defined _M_I86
    if (block->id != ZONEID)
        I_Error("Z_Free: freed a pointer without ZONEID");
#endif

    if (block->user > (void **)0x100)
    {
        // smaller values are not pointers
        // Note: OS-dependend?

        // clear the user's mark
        *block->user = 0;
    }

    // mark as free
    block->user = NULL;
    block->tag  = 0;


#if defined INSTRUMENTED
    running_count -= block->size;
    printf("Free: %ld\n", running_count);
#endif

    other = segmentToPointer(block->prev);

    if (!other->user)
    {
        // merge with previous free block
        other->size += block->size;
        other->next = block->next;
        segmentToPointer(other->next)->prev = pointerToSegment(other);

        if (pointerToSegment(block) == mainzone_rover)
            mainzone_rover = pointerToSegment(other);

        block = other;
    }

    other = segmentToPointer(block->next);
    if (!other->user)
    {
        // merge the next free block onto the end
        block->size += other->size;
        block->next = other->next;
        segmentToPointer(block->next)->prev = pointerToSegment(block);

        if (pointerToSegment(other) == mainzone_rover)
            mainzone_rover = pointerToSegment(block);
    }
}


static uint32_t Z_GetLargestFreeBlockSize(void)
{
	uint32_t largestFreeBlockSize = 0;

	for (memblock_t* block = segmentToPointer(mainzone_blocklist->next); pointerToSegment(block) != pointerToSegment(mainzone_blocklist); block = segmentToPointer(block->next))
		if (!block->user && block->size > largestFreeBlockSize)
			largestFreeBlockSize = block->size;

	return largestFreeBlockSize;
}

static uint32_t Z_GetTotalFreeMemory(void)
{
	uint32_t totalFreeMemory = 0;

	for (memblock_t* block = segmentToPointer(mainzone_blocklist->next); pointerToSegment(block) != pointerToSegment(mainzone_blocklist); block = segmentToPointer(block->next))
		if (!block->user)
			totalFreeMemory += block->size;

	return totalFreeMemory;
}


//
// Z_Malloc
// You can pass a NULL user if the tag is < PU_PURGELEVEL.
//
#define MINFRAGMENT		64


static void* Z_Malloc(int32_t size, int32_t tag, void **user)
{
    size = (size + (PARAGRAPH_SIZE - 1)) & ~(PARAGRAPH_SIZE - 1);

    // scan through the block list,
    // looking for the first free block
    // of sufficient size,
    // throwing out any purgable blocks along the way.

    // account for size of block header
    size += PARAGRAPH_SIZE;

    // if there is a free block behind the rover,
    //  back up over them
    memblock_t* base = segmentToPointer(mainzone_rover);

    if (!segmentToPointer(base->prev)->user)
        base = segmentToPointer(base->prev);

    memblock_t* rover = base;
    memblock_t* start = segmentToPointer(base->prev);

    do
    {
        if (rover == start)
        {
            // scanned all the way around the list
            I_Error ("Z_Malloc: failed to allocate %li B, max free block %li B, total free %li", size, Z_GetLargestFreeBlockSize(), Z_GetTotalFreeMemory());
        }

        if (rover->user)
        {
            if (rover->tag < PU_PURGELEVEL)
            {
                // hit a block that can't be purged,
                //  so move base past it
                base = rover = segmentToPointer(rover->next);
            }
            else
            {
                // free the rover block (adding the size to base)

                // the rover can be the base block
                base  = segmentToPointer(base->prev);
                Z_Free(segmentToPointer(pointerToSegment(rover) + 1));
                base  = segmentToPointer(base->next);
                rover = segmentToPointer(base->next);
            }
        }
        else
            rover = segmentToPointer(rover->next);

    } while (base->user || base->size < size);


    // found a block big enough
    int32_t extra = base->size - size;

    if (extra > MINFRAGMENT)
    {
        // there will be a free fragment after the allocated block
        memblock_t* newblock = segmentToPointer(pointerToSegment(base) + size / PARAGRAPH_SIZE);
        newblock->size = extra;

        // NULL indicates free block.
        newblock->user = NULL;
        newblock->tag  = 0;
        newblock->prev = pointerToSegment(base);
        newblock->next = base->next;
        segmentToPointer(newblock->next)->prev = pointerToSegment(newblock);

        base->next = pointerToSegment(newblock);
        base->size = size;
    }

    if (user)
    {
        // mark as an in use block
        base->user = user;
        *(void **)user = segmentToPointer(pointerToSegment(base) + 1);
    }
    else
    {
        if (tag >= PU_PURGELEVEL)
            I_Error ("Z_Malloc: an owner is required for purgable blocks");

        // mark as in use, but unowned
        base->user = (void *)2;
    }

    base->tag = tag;
#if defined _M_I86
    base->id  = ZONEID;
#endif

    // next allocation will start looking here
    mainzone_rover = base->next;

#if defined INSTRUMENTED
    running_count += base->size;
    printf("Alloc: %ld (%ld)\n", base->size, running_count);
#endif

    return segmentToPointer(pointerToSegment(base) + 1);
}


void* Z_MallocStatic(int32_t size)
{
	return Z_Malloc(size, PU_STATIC, NULL);
}


void* Z_MallocLevel(int32_t size, void **user)
{
	return Z_Malloc(size, PU_LEVEL, user);
}


void* Z_CallocLevSpec(int32_t size)
{
	void *ptr = Z_Malloc(size, PU_LEVSPEC, NULL);
	memset(ptr, 0, size);
	return ptr;
}


void* Z_CallocLevel(int32_t size)
{
    void* ptr = Z_Malloc(size, PU_LEVEL, NULL);
    memset(ptr, 0, size);
    return ptr;
}


//
// Z_FreeTags
//
void Z_FreeTags(void)
{
    memblock_t*	next;

    for (memblock_t* block = segmentToPointer(mainzone_blocklist->next); pointerToSegment(block) != pointerToSegment(mainzone_blocklist); block = next)
    {
        // get link before freeing
        next = segmentToPointer(block->next);

        // already a free block?
        if (!block->user)
            continue;

        if (PU_LEVEL <= block->tag && block->tag <= (PU_PURGELEVEL - 1))
            Z_Free(segmentToPointer(pointerToSegment(block) + 1));
    }
}

//
// Z_CheckHeap
//
void Z_CheckHeap (void)
{
    for (memblock_t* block = segmentToPointer(mainzone_blocklist->next); ; block = segmentToPointer(block->next))
    {
        if (block->next == pointerToSegment(mainzone_blocklist))
        {
            // all blocks have been hit
            break;
        }

#if defined _M_I86
        if (block->id != ZONEID)
            I_Error("Z_CheckHeap: block has id %x instead of ZONEID", block->id);
#endif

        if (pointerToSegment(block) + (block->size / PARAGRAPH_SIZE) != block->next)
            I_Error ("Z_CheckHeap: block size does not touch the next block\n");

        if (segmentToPointer(block->next)->prev != pointerToSegment(block))
            I_Error ("Z_CheckHeap: next block doesn't have proper back link\n");

        if (!block->user && !segmentToPointer(block->next)->user)
            I_Error ("Z_CheckHeap: two consecutive free blocks\n");
    }
}

/* *****************************************************************************
 * Copyright (c) 2013-2014 Qingtao Cao [harry.cao@nextdc.com]
 *
 * This file is part of oBIX.
 *
 * oBIX is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * oBIX is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with oBIX.  If not, see <http://www.gnu.org/licenses/>.
 *
 * *****************************************************************************/

#ifndef _BITMAP_H
#define _BITMAP_H

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "list.h"

/**
 * Describes a bitmap node.
 *
 * Once a bit is requested by calls bitmap_get_id(), it's ID integer is calculates
 * and marked in this structure as in-use.
 *
 * Access to this list is thread-safe, and synchronized with the bitmap_t's mutex
 * member, and therefore memory should not be accessed in this structure directly.
 * Use the public bitmap APIs to manipulate these structures.
 */
typedef struct bitmap_node {
	/* Joining bitmap_t's list that contains all bitmap nodes */
	struct list_head list_all;

	/* Joining bitmap_t's list that contains nodes with unused bits */
	struct list_head list_notfull;

	/* The bitmap of this node */
	unsigned long map;

	/* The starting ID integer of this node */
	int start;
} bitmap_node_t;

/**
 * Describes a bitmap structure.
 *
 * Bitmaps contain an unlimited amount of 'nodes'.  One bitmap node contains 64-bits
 * of bit space, which act as a slot mechanism.  Once an ID number has been retrieved
 * from the bitmap by calls to bitmap_get_id(), it is marked in the bitmap as in-use
 * and cannot be used again.
 *
 * An ID number can be released back into the bitmap pool with calls to bitmap_put_id()
 * which can then be re-used with subsequent calls to bitmap_get_id.
 *
 * This structure is thread-safe, and uses the mutex member of the bitmap structure to
 * synchronize access to the entire bitmap structrure and all it's nodes, and therefore
 * you should not attempt to access memory of a bitmap directly, use only it's public
 * functions.
 */
typedef struct bitmap {
	/* The queue of all bitmap nodes */
	struct list_head nodes_all;

	/*
	 * The queue of bitmap nodes with unused bits, organized in ascending
	 * order of start. Important not only for performance when getting an
	 * unused bit, but also to recycle all released IDs also in ascending
	 * order
	 */
	struct list_head nodes_notfull;

	/* The highest ID integer of this bitmap */
	int highest;

	/* Mutex to protect the whole data structure */
	pthread_mutex_t mutex;
} bitmap_t;

/**
 * Retrieves the first unused bit in the bitmap pointed to by
 * @a b.
 *
 * @param	b	A pointer to an initialized bitmap as constructed with
 *				@a bitmap_init.
 *
 * @returns		An integer less than zero if an error occured, or another
 *				value containing the next available ID.
 */
int bitmap_get_id(bitmap_t *b);

/**
 * Releases an ID integer specified by @a id in the bitmap pointed
 * to by @a b so that it may be reused in the future by subsequent calls
 * to @a bitmap_get_id().
 *
 * @param	b	A pointer to an initialized bitmap as constructed with
 *				@a bitmap_init.
 * @param	id	An integer specifying the ID to be released.
 */
void bitmap_put_id(bitmap_t *b, int id);

/**
 * Allocates a new bitmap and create and insert the first bitmap node to it.
 *
 * @returns		A pointer to a newly-created bitmap structure, or NULL
 *				if an error occured.
 *
 * @remarks		This is an allocating function.  It's up to the caller to free
 *				the memory allocated by the successful return of this function
 *				with calls to @a bitmap_dispose().
 */
bitmap_t *bitmap_init(void);

/**
 * Disposes and frees all memory assigned to the bitmap structure pointed to
 * by @a b.
 *
 * @param	b	A pointer to an initialized bitmap structure as allocated by
 *				calls to @a bitmap_init().
 */
void bitmap_dispose(bitmap_t *b);

#endif

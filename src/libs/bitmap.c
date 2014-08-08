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
/*
 * A flexible and extensible bitmap facility to help resolve the overflow
 * issue of watch ID. With the help of it, the ID of deleted watches are
 * able to be recycled, while still imposing no limitation on the number of
 * watches allowable on the oBIX server
 */

#include "bitmap.h"

/*
 * The bitmap size for each bitmap node structure
 */
#define MAPSIZE		(sizeof(unsigned long) * 8)

/*
 * Macros used in bit operation with a bitmap
 *
 * IMPORTANT!!
 *
 * All constant integers are SIGNED in C standard! Therefore
 * "UL" must be explicitly specified. Otherwise unsigned long
 * bit OR with signed long will end up with signed long
 */
#define MAPFULL		(~0UL)
#define MAPBIT		(1UL)

/**
 * Get the first unused ID and set relevant bit in bitmap
 * to 1
 *
 * Return >=0 on success, -1 if no unused bit in the bitmap
 */
static int bitmap_get_unused(bitmap_t *b)
{
	bitmap_node_t *node;
	int ret = -1, count;
	unsigned long map;

	pthread_mutex_lock(&b->mutex);
	/*
	 * Either all nodes have been consumed, or no
	 * node created yet after initialization
	 */
	if (list_empty(&b->nodes_notfull) == 1) {
		pthread_mutex_unlock(&b->mutex);
		return -1;
	}

	list_for_each_entry(node, &b->nodes_notfull, list_notfull) {
		if (node->map == MAPFULL) {
			continue;	/* Shouldn't happen */
		}

		for (map = node->map, count = 0; count < MAPSIZE; count++) {
			if ((map & MAPBIT) == 0) {
				node->map |= (MAPBIT << count);

				/*
				 * Dequeue from notfull list if it is all consumed, and
				 * re-initialize its list_head structure so as to prepare
				 * to be added back when any its bit gets released
				 */
				if (node->map == MAPFULL) {
					list_del(&node->list_notfull);
					INIT_LIST_HEAD(&node->list_notfull);
				}

				break;
			}

			map >>= 1;
		}

		ret = count + node->start;
		break;
	}
	pthread_mutex_unlock(&b->mutex);

	return ret;
}

/**
 * Insert the given bitmap node to b->notfull queue according
 * to its start value so as to enfore strict ascending order of
 * that value
 *
 * Note,
 * 1. Callers must held bitmap->mutex
 */
static void __bitmap_insert_notfull(bitmap_t *b, bitmap_node_t *node)
{
	bitmap_node_t *pos;

	if (list_empty(&b->nodes_notfull) == 1) {
		list_add(&node->list_notfull, &b->nodes_notfull);
	} else {
		list_for_each_entry(pos, &b->nodes_notfull, list_notfull) {
			/*
			 * Insert the node before the current one if
			 * its start value is smaller
			 */
			if (node->start < pos->start) {
				__list_add(&node->list_notfull, pos->list_notfull.prev,
						   &pos->list_notfull);
				break;
			}
		}
	}
}

/**
 * Create and insert a bitmap node into the given bitmap
 *
 * Return 0 on success, -1 on failure
 */
static int bitmap_create_insert_node(bitmap_t *b)
{
	bitmap_node_t *node;

	if (!(node = (bitmap_node_t *)malloc(sizeof(bitmap_node_t)))) {
		return -1;
	}
	memset(node, 0, sizeof(bitmap_node_t));

	INIT_LIST_HEAD(&node->list_all);
	INIT_LIST_HEAD(&node->list_notfull);

	pthread_mutex_lock(&b->mutex);
	list_add_tail(&node->list_all, &b->nodes_all);
	__bitmap_insert_notfull(b, node);
	node->start = b->highest + 1;
	b->highest += MAPSIZE;
	pthread_mutex_unlock(&b->mutex);

	return 0;
}

/**
 * Get an ID integer from the bitmap facility
 *
 * Return >=0 on success, -1 on failure
 */
int bitmap_get_id(bitmap_t *b)
{
	int ret;

	if ((ret = bitmap_get_unused(b)) >= 0) {
		return ret;
	}

	return ((bitmap_create_insert_node(b)) == 0) ?
				bitmap_get_unused(b) : -1;
}

/**
 * Release the given ID to the bitmap facility
 */
void bitmap_put_id(bitmap_t *b, int id)
{
	bitmap_node_t *node;

	pthread_mutex_lock(&b->mutex);
	if (id >= 0 && id <= b->highest) {
		list_for_each_entry(node, &b->nodes_all, list_all) {
			if (id >= node->start && id < node->start + MAPSIZE) {
				node->map &= ~(MAPBIT << (id - node->start));

				/* Insert into notfull queue if not there yet */
				if (node->list_notfull.prev == &node->list_notfull) {
					__bitmap_insert_notfull(b, node);
				}

				break;
			}
		}
	}
	pthread_mutex_unlock(&b->mutex);
}

void bitmap_dispose(bitmap_t *b)
{
	bitmap_node_t *node, *n;

	pthread_mutex_lock(&b->mutex);
	list_for_each_entry_safe(node, n, &b->nodes_all, list_all) {
		list_del(&node->list_all);
		list_del(&node->list_notfull);
		free(node);
	}
	pthread_mutex_unlock(&b->mutex);

	pthread_mutex_destroy(&b->mutex);
	free(b);
}

bitmap_t *bitmap_init(void)
{
	bitmap_t *b;

	if (!(b = (bitmap_t *)malloc(sizeof(bitmap_t)))) {
		return NULL;
	}
	memset(b, 0, sizeof(bitmap_t));

	INIT_LIST_HEAD(&b->nodes_all);
	INIT_LIST_HEAD(&b->nodes_notfull);
	pthread_mutex_init(&b->mutex, NULL);
	b->highest = -1;

	return b;
}

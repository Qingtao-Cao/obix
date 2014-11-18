/* *****************************************************************************
 * Copyright (c) 2013-2015 Qingtao Cao [harry.cao@nextdc.com]
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
 * along with oBIX. If not, see <http://www.gnu.org/licenses/>.
 *
 * *****************************************************************************/

/*
 * Cache to take advantage of the "locality principle" when finding a core
 * data structure with a specified href.
 */

#ifndef _CACHE_H
#define _CACHE_H

#include <pthread.h>

typedef struct cache_item {
	/* reference to a data structure */
	const void *item;

	/* the data structure's unique absolute href */
	unsigned char *href;
} cache_item_t;

typedef struct cache {
	/* cache slots */
	cache_item_t *items;

	/* cache size */
	int len;

	/* statistics variables */
	long hit, miss;

	/* lock to protect the whole structure */
	pthread_mutex_t mutex;
} cache_t;

long cache_get_hit(cache_t *c);
long cache_get_miss(cache_t *c);
cache_t *cache_init(const int len);
void cache_dispose(cache_t *c);
void cache_update(cache_t *c, const unsigned char *href, const void *item);
const void *cache_search(cache_t *c, const unsigned char *href);
void cache_invalidate(cache_t *c, const unsigned char *href);

#endif

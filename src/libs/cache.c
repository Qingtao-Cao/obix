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

#include <stdlib.h>
#include <string.h>
#include "obix_utils.h"
#include "cache.h"

long cache_get_hit(cache_t *c)
{
	long val;

	if (!c) {
		return -1;
	}

	pthread_mutex_lock(&c->mutex);
	val = c->hit;
	pthread_mutex_unlock(&c->mutex);

	return val;
}

long cache_get_miss(cache_t *c)
{
	long val;

	if (!c) {
		return -1;
	}

	pthread_mutex_lock(&c->mutex);
	val = c->miss;
	pthread_mutex_unlock(&c->mutex);

	return val;
}

/*
 * NOTE: to make the most sense, the size of the cache should be
 * less than the average length of "conflict queues" of the accompanied
 * hash table
 *
 * Therefore the more number of structures recorded in the hash
 * table, the longer the conflict queues, the larger the cache
 */
cache_t *cache_init(const int len)
{
	cache_t *cache = NULL;

	if (len <= 0) {
		return NULL;
	}

	if (!(cache = (cache_t *)malloc(sizeof(cache_t))) ||
		!(cache->items = (cache_item_t *)malloc(sizeof(cache_item_t) * len))) {
		goto failed;
	}
	memset(cache->items, 0, sizeof(cache_item_t) * len);

	cache->len = len;
	cache->hit = cache->miss = 0;
	pthread_mutex_init(&cache->mutex, NULL);

	return cache;

failed:
	if (cache) {
		free(cache);
	}

	return NULL;
}

static void __cache_dispose_slot(cache_t *c, int i)
{
	/* Sanity checks on arguments already done by internal callers */

	if (c->items[i].href) {
		free(c->items[i].href);
		c->items[i].href = NULL;
	}

	c->items[i].item = NULL;
}

void cache_dispose(cache_t *c)
{
	int i;

	if (!c) {
		return;
	}

	pthread_mutex_lock(&c->mutex);
	for (i = 0; i < c->len; i++) {
		__cache_dispose_slot(c, i);
	}
	pthread_mutex_unlock(&c->mutex);

	pthread_mutex_destroy(&c->mutex);
	free(c->items);
	free(c);
}

/*
 * Make room for the latest search result by shuffling existing
 * cache slots forward/downard by one offset thus getting rid of
 * the recently least accessed one
 *
 * NOTE: since "cache_search + hash_search + cache_update" are
 * not in an atomic manner, multi-thread may add duplicate entries
 * into the cache. However, it would be too costy to fully prevent
 * this by checking the entire cache for potential duplicate before
 * pushing in a new one, especially considering that such scenario
 * is very rare.
 */
void cache_update(cache_t *c, const unsigned char *href, const void *item)
{
	int loc;	/* index within the cache */

	if (!c || !href || !item) {
		return;
	}

	pthread_mutex_lock(&c->mutex);

	/* Try to minimise duplicate but not traverse the entire cache */
	if (is_str_identical((const char *)c->items[0].href,
						 (const char *)href) == 1) {
		pthread_mutex_unlock(&c->mutex);
		return;
	}

	loc = c->len - 1;

	if (c->items[loc].href != NULL) {
		free(c->items[loc].href);
	}

	while (loc > 0) {
		c->items[loc].item = c->items[loc - 1].item;
		c->items[loc].href = c->items[loc - 1].href;
		loc--;
	}

	if (!(c->items[0].href = (unsigned char *)strdup((const char *)href))) {
		c->items[0].item = NULL;
	} else {
		c->items[0].item = item;
	}

	pthread_mutex_unlock(&c->mutex);
}

const void *cache_search(cache_t *c, const unsigned char *href)
{
	const void *item;
	int i;

	if (!c || !href) {
		return NULL;
	}

	pthread_mutex_lock(&c->mutex);

	for (i = 0; i < c->len; i++) {
		if (!(item = c->items[i].item) || !c->items[i].href) {
			/*
			 * Keep searching the rest of cache instead of break
			 * since the current reference may have been nullified
			 */
			continue;
		}

		/*
		 * Cache hit
		 *
		 * NOTE: no more cache_update to ensure:
		 * 1. no more duplicated slots in the cache when the matching
		 *	  one is not the first;
		 * 2. no performance loss on cache hit
		 */
		if (is_str_identical((const char *)c->items[i].href,
							 (const char *)href) == 1) {
			c->hit++;
			pthread_mutex_unlock(&c->mutex);
			return item;
		}
	}

	/* Cache miss */
	c->miss++;
	pthread_mutex_unlock(&c->mutex);
	return NULL;
}

void cache_invalidate(cache_t *c, const unsigned char *href)
{
	const void *item;
	int i;

	if (!c || !href) {
		return;
	}

	pthread_mutex_lock(&c->mutex);

	for (i = 0; i < c->len; i++) {
		if (!(item = c->items[i].item) || !c->items[i].href) {
			continue;
		}

		if (is_str_identical((const char *)c->items[i].href,
							 (const char *)href) == 1) {
			__cache_dispose_slot(c, i);

			/* Continue since there may be duplicated slots in the cache */
		}
	}

	pthread_mutex_unlock(&c->mutex);
}

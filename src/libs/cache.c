/* *****************************************************************************
 * Copyright (c) 2013-2015 Qingtao Cao
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
#include "xml_utils.h"
#include "cache.h"

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
	pthread_mutex_init(&cache->mutex, NULL);

#ifdef DEBUG
	cache->hit = cache->miss = 0;
#endif

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
	c->items[i].href = c->items[i].item = NULL;
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
 * is very rare
 *
 * NOTE: given that the cache slot won't duplicate href strings, the
 * href passed in must have the same life cycle as the item parameter
 */
static void __cache_update(cache_t *c, const unsigned char *href,
						   const void *item)
{
	int loc;

	/* Try to minimise duplicate but not traverse the entire cache */
	if (is_str_identical(c->items[0].href, href, 1) == 1) {
		pthread_mutex_unlock(&c->mutex);
		return;
	}

	loc = c->len - 1;	/* the last slot */

	while (loc > 0) {
		c->items[loc].href = c->items[loc - 1].href;
		c->items[loc].item = c->items[loc - 1].item;
		loc--;
	}

	c->items[0].href = href;
	c->items[0].item = item;
}

void cache_update(cache_t *c, const unsigned char *href, const void *item)
{
	if (!c || !href || !item) {
		return;
	}

	pthread_mutex_lock(&c->mutex);
	__cache_update(c, href, item);
	pthread_mutex_unlock(&c->mutex);
}

const void *cache_search(cache_t *c, const unsigned char *href,
						 void (*get)(void *))
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
			 *
			 * NOTE: since the entire cache is iterated, the cache
			 * size must remain small when oBIX clients are quickly
			 * accessing different URIs, as a result, the locality
			 * principle won't work
			 */
			continue;
		}

		if (is_str_identical(c->items[i].href, href, 1) == 1) {
#ifdef DEBUG
			c->hit++;
#endif
			/*
			 * NOTE: invoke cache_update to ensure the recently most
			 * accessed on always on top, although this will incur
			 * duplication in the cache
			 */
			if (i > 0) {
				__cache_update(c, c->items[i].href, c->items[i].item);
			}

			/*
			 * Atomically increase the reference count for relevant
			 * structure before returning its address
			 */
			get((void *)item);

			pthread_mutex_unlock(&c->mutex);
			return item;
		}
	}

#ifdef DEBUG
	c->miss++;
#endif
	pthread_mutex_unlock(&c->mutex);
	return NULL;
}

void cache_invalidate(cache_t *c, const unsigned char *href)
{
	const void *item;
	int i, len;

	if (!c || !href) {
		return;
	}

	len = strlen((const char *)href);
	if (href[len - 1] == '/') {
		len--;
	}

	pthread_mutex_lock(&c->mutex);

	for (i = 0; i < c->len; i++) {
		if (!(item = c->items[i].item) || !c->items[i].href) {
			continue;
		}

		/*
		 * NOTE: the entire cache should be traversed due to duplicated slots
		 *
		 * Note: although the oBIX server can help ensure the device won't be
		 * deleted if it has any children, for sake of consisitency any hrefs
		 * in the cache that are prefixed with that of the deleted device need
		 * to be invalidated as well
		 */
		if (strncmp((const char *)c->items[i].href, (const char *)href,
					len) == 0) {
			__cache_dispose_slot(c, i);
		}
	}

	pthread_mutex_unlock(&c->mutex);
}

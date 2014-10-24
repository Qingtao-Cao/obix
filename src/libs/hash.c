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

#include <stdlib.h>
#include <stdio.h>
#include "hash.h"

/*
 * Prototype of the callback functions used by hash table APIs,
 * they are called with hash table'e mutex held
 */
typedef void (*hash_cb_t)(hash_head_t *head, hash_node_t *node, void *arg);

static void hash_init_head(hash_head_t *head)
{
	INIT_LIST_HEAD(&head->head);
	head->count = 0;
	pthread_mutex_init(&head->mutex, NULL);
}

static void hash_destroy_head(hash_head_t *head)
{
	hash_node_t *node, *n;

	pthread_mutex_lock(&head->mutex);
	if (head->count > 0) {
		list_for_each_entry_safe(node, n, &head->head, list) {
			list_del(&node->list);
			free(node);
		}
	}
	pthread_mutex_unlock(&head->mutex);

	pthread_mutex_destroy(&head->mutex);
	/* Hash heads are released along with the entire hash table */
}

/*
 * Check if the given number is a prime or not
 *
 * Return 1 if it is a prime, 0 otherwise
 *
 * A prime does not have any divisor other than 1 or itself.
 * If any divisors of n that is < n is available then n is
 * a composite.
 *
 * Since divisors of n that are >= sqrt(n) simply flip and
 * repeat, only those numbers <= sqrt(n) need be checked to
 * see if they are divisors of n.
 *
 * Furthermore, since all integers can be represented as 6k+i,
 * where i belongs to [-1, 0, 1, 2, 3, 4]. Since (6k + 0),
 * (6k + 2) and (6k + 4) are divisible by 2, and (6k + 3) is
 * divisble by 3, they are all composites and have been covered
 * when n is tested whether divisible by 2 or 3, therefore only
 * those numbers in form of (6k +/- 1) need further to be tested.
 *
 * http://en.wikipedia.org/wiki/Primality_test
 */
static int is_prime(unsigned int n)
{
	int i, w;

	if (n == 2 || n == 3) {
		return 1;
	}

	if ((n % 2 == 0) || (n % 3 == 0)) {
		return 0;
	}

	/*
	 * Now we only need to test if n is divisible by
	 * any integer <= sqrt(n) and in form of (6k +/- 1)
	 */
	i = 5;
	w = 2;
	while (i * i <= n) {
		if (n % i == 0) {	/* composite */
			return 0;
		}

		/*
		 * w rotates between 2 and 4, so as to numerate
		 * all integers in form of (6k +/- 1)
		 */
		i += w;
		w = 6 - w;
	}

	return 1;	/* prime */
}

/*
 * Get the smallest prime no smaller than the given number
 */
static unsigned int get_prime(unsigned int num)
{
	unsigned int i;

	for (i = num; is_prime(i) == 0; i++);	/* do nothing */

	return i;
}

hash_table_t *hash_init_table(unsigned int size, hash_ops_t *op)
{
	hash_table_t *tab;
	int i;

	if (size == 0 || !op) {
		return NULL;
	}

	if (!(tab = (hash_table_t *)malloc(sizeof(hash_table_t)))) {
		return NULL;
	}

	tab->size = get_prime(size);
	tab->op = op;

	if (!(tab->table = (hash_head_t *)malloc(sizeof(hash_head_t) * tab->size))) {
		goto failed;
	}

	for (i = 0; i < tab->size; i++) {
		hash_init_head(tab->table + i);
	}

	return tab;

failed:
	free(tab);
	return NULL;
}

void hash_destroy_table(hash_table_t *tab)
{
	int i;

	if (tab->table) {
		for (i = 0; i < tab->size; i++) {
			hash_destroy_head(tab->table + i);
		}

		free(tab->table);
	}

	free(tab);
}

/* Apply the given callback on the matching node if found */
static void hash_helper(hash_table_t *tab, const unsigned char *key,
						hash_cb_t cb, void *arg)
{
	hash_head_t *head;
	hash_node_t *node, *n;

	if (!tab || !key || !tab->op || !tab->op->get || !tab->op->cmp) {
		return;
	}

	head = &tab->table[tab->op->get(key, tab->size)];

	pthread_mutex_lock(&head->mutex);
	list_for_each_entry_safe(node, n, &head->head, list) {
		if (tab->op->cmp(key, node) == 0) {
			if (cb) {
				cb(head, node, arg);
			}

			break;
		}
	}

	/* Not found, or empty collision list */
	pthread_mutex_unlock(&head->mutex);
}

static void hash_get_cb(hash_head_t *head, hash_node_t *node, void *arg)
{
	 *(const void **)arg = node->item;
}

const void *hash_get(hash_table_t *tab, const unsigned char *key)
{
	const void *item = NULL;

	hash_helper(tab, key, hash_get_cb, &item);

	return item;
}

static void hash_del_cb(hash_head_t *head, hash_node_t *node, void *arg)
{
	list_del(&node->list);
	free(node);

	head->count--;
}

void hash_del(hash_table_t *tab, const unsigned char *key)
{
	hash_helper(tab, key, hash_del_cb, NULL);
}

int hash_add(hash_table_t *tab, const unsigned char *key, void *item)
{
	hash_head_t *head;
	hash_node_t *node, *new;

	if (!tab || !key || !item || !tab->op || !tab->op->get || !tab->op->cmp) {
		return -1;
	}

	head = &tab->table[tab->op->get(key, tab->size)];

	/*
	 * NOTE:
	 * The search and add operations must be atomic to avoid
	 * race conditions.
	 */
	pthread_mutex_lock(&head->mutex);
	list_for_each_entry(node, &head->head, list) {
		if (tab->op->cmp(key, node) == 0) {	/* match found */
			pthread_mutex_unlock(&head->mutex);
			return 0;
		}
	}

	/* Not found or empty collision list, add to the tail of it */
	if (!(new = (hash_node_t *)malloc(sizeof(hash_node_t)))) {
		pthread_mutex_unlock(&head->mutex);
		return -1;
	}

	new->item = item;
	INIT_LIST_HEAD(&new->list);

	list_add_tail(&new->list, &head->head);
	head->count++;

	pthread_mutex_unlock(&head->mutex);

	return 0;
}

unsigned int hash_bkdr(const unsigned char *str, const unsigned int size)
{
	unsigned int seed = 31;	/* 31 131 1313 13131 131313 etc */
	unsigned int hash = 0;

	if (!str || size == 0) {
		return 0;
	}

	while (*str) {
		hash = hash * seed + (*str++);
	}

	return hash % size;
}

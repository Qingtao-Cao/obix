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
#include <stdio.h>
#include "hash.h"

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

static void hash_init_head(hash_head_t *head)
{
	INIT_LIST_HEAD(&head->head);
	head->count = 0;
	tsync_init(&head->sync);
}

static void hash_destroy_head(hash_head_t *head)
{
	hash_node_t *node, *n;

	tsync_shutdown(&head->sync);

	list_for_each_entry_safe(node, n, &head->head, list) {
		list_del(&node->list);
		free(node);
	}

	tsync_cleanup(&head->sync);

	/* Hash heads are released along with the entire hash table */
}

hash_table_t *hash_init_table(unsigned int size, hash_ops_t *op)
{
	hash_table_t *tab;
	int i;

	if (size == 0 || !op || !op->get || !op->cmp) {
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

const void *hash_search(hash_table_t *tab, const unsigned char *key)
{
	const void *item = NULL;
	hash_head_t *head;
	hash_node_t *node;

	if (!tab || !key) {
		return NULL;
	}

	head = &tab->table[tab->op->get(key, tab->size)];

	if (tsync_reader_entry(&head->sync) < 0) {
		return NULL;
	}

	list_for_each_entry(node, &head->head, list) {
		if (tab->op->cmp(key, node) == 1) {
			item = node->item;
			break;
		}
	}

	tsync_reader_exit(&head->sync);

	return item;
}

void hash_del(hash_table_t *tab, const unsigned char *key)
{
	hash_head_t *head;
	hash_node_t *node, *n;

	if (!tab || !key) {
		return;
	}

	head = &tab->table[tab->op->get(key, tab->size)];

	if (tsync_writer_entry(&head->sync) < 0) {
		return;
	}

	list_for_each_entry_safe(node, n, &head->head, list) {
		if (tab->op->cmp(key, node) == 1) {
			list_del(&node->list);
			free(node);
			head->count--;
			break;
		}
	}

	tsync_writer_exit(&head->sync);
}

/*
 * Add an item into the given hash table
 *
 * Return 0 on success, < 0 for error code
 */
int hash_add(hash_table_t *tab, const unsigned char *key, void *item)
{
	hash_head_t *head;
	hash_node_t *node, *new;
	int ret = 0;

	if (!tab || !key || !item) {
		return -1;
	}

	head = &tab->table[tab->op->get(key, tab->size)];

	if (tsync_writer_entry(&head->sync) < 0) {
		return -1;
	}

	list_for_each_entry(node, &head->head, list) {
		if (tab->op->cmp(key, node) == 1) {
			/* Already added, return success */
			goto out;
		}
	}

	/* Not exist or empty collision list, add to the tail of it */
	if (!(new = (hash_node_t *)malloc(sizeof(hash_node_t)))) {
		ret = -1;
	} else {
		new->item = item;
		INIT_LIST_HEAD(&new->list);
		list_add_tail(&new->list, &head->head);
		head->count++;
	}

out:
	tsync_writer_exit(&head->sync);
	return ret;
}

unsigned int hash_bkdr(const unsigned char *str, const int len,
					   const unsigned int tab_size)
{
	unsigned int seed = 31;	/* 31 131 1313 13131 131313 etc */
	unsigned int hash = 0;
	int pos = 0;

	if (!str || len == 0 || tab_size == 0) {
		return 0;
	}

	while (*str) {
		/* Skip the trailing slash */
		if ((pos++ == len - 1) && (*str == '/')) {
			break;
		}

		hash = hash * seed + (*str++);
	}

	return hash % tab_size;
}

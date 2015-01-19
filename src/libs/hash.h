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

#ifndef _HASH_H
#define _HASH_H

#include "list.h"
#include "tsync.h"

typedef struct hash_node {
	const void *item;
	struct list_head list;
} hash_node_t;

typedef struct hash_head {
	unsigned int count;
	struct list_head head;
	tsync_t sync;
} hash_head_t;

typedef struct hash_table_ops {
	/*
	 * Calculate the hash value for the given string
	 */
	unsigned int (*compute)(const unsigned char *str, const unsigned int prime);

	/*
	 * Compare if the given string matches with relevant counterpart
	 * in the given hash node
	 */
	int (*compare)(const unsigned char *str, hash_node_t *node);

	/*
	 * Increase the reference count of relevant hosting structure before
	 * returning its pointer to users
	 */
	void (*get)(void *);
} hash_table_ops_t;

typedef struct hash_table {
	unsigned int size;
	hash_head_t *table;
	hash_table_ops_t *op;
} hash_table_t;

hash_table_t *hash_init_table(unsigned int size, hash_table_ops_t *op);
void hash_destroy_table(hash_table_t *tab);
void *hash_search(hash_table_t *tab, const unsigned char *key);
int hash_add(hash_table_t *tab, const unsigned char *key, void *item);
void hash_del(hash_table_t *tab, const unsigned char *key);

unsigned int hash_bkdr(const unsigned char *str, const int len, const unsigned int tab_size);

#endif

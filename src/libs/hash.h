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

/*
 * Calculate the hash value for the given string
 */
typedef unsigned int (*get_hash)(const unsigned char *str, const unsigned int prime);

/*
 * Compare if the given string matches with relevant counterpart
 * in the given hash node
 *
 * Return 1 if they match, 0 otherwise
 */
typedef int (*cmp_hash)(const unsigned char *str, hash_node_t *node);

typedef struct hash_ops {
	get_hash get;
	cmp_hash cmp;
} hash_ops_t;

typedef struct hash_table {
	unsigned int size;
	hash_head_t *table;
	hash_ops_t *op;
} hash_table_t;

hash_table_t *hash_init_table(unsigned int size, hash_ops_t *op);
void hash_destroy_table(hash_table_t *tab);
const void *hash_search(hash_table_t *tab, const unsigned char *key);
int hash_add(hash_table_t *tab, const unsigned char *key, void *item);
void hash_del(hash_table_t *tab, const unsigned char *key);

unsigned int hash_bkdr(const unsigned char *str, const int len, const unsigned int tab_size);

#endif

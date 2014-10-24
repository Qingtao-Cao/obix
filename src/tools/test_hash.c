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
 * An instrument to test the hash table APIs
 *
 * Build below command:
 *
 *	$ gcc -g -Wall -Werror test_hash.c ../libs/hash.c
 *		  -I../libs/ -I/usr/include/libxml2/
 *		  -lxml2 -lobix-common -o test_hash
 *
 * Run with following arguments:
 *
 *	$ ./test_hash <size of table> <XML file path>
 *
 * Where
 *	<size of table>: the size of the hash table, ideally a prime
 *	<XML file path>: the absolute path of an XML file that contains
 *					 a list of reference nodes, each of which has a
 *					 unique href attribute
 */

#include <stdio.h>
#include <string.h>
#include "obix_utils.h"
#include "hash.h"

typedef struct obix_dev {
	xmlChar *href;
	struct list_head list;
} obix_dev_t;

static LIST_HEAD(devlist);
static hash_table_t *devtab;

unsigned int device_get_hash(const unsigned char *str, const unsigned int size);
int device_cmp_hash(const unsigned char *str, hash_node_t *node);

static hash_ops_t device_hash_ops = {
	.get = device_get_hash,
	.cmp = device_cmp_hash
};

unsigned int device_get_hash(const unsigned char *str, const unsigned int size)
{
	if (xmlStrncmp(str, BAD_CAST OBIX_DEVICE_ROOT, OBIX_DEVICE_ROOT_LEN) == 0) {
		str += OBIX_DEVICE_ROOT_LEN;
	}

	return hash_bkdr(str, size);
}

int device_cmp_hash(const unsigned char *str, hash_node_t *node)
{
	obix_dev_t *dev = (obix_dev_t *)(node->item);

	return xmlStrcmp(str, dev->href);
}

static void xml_parser_init(void)
{
	xmlKeepBlanksDefault(0);
	xmlInitParser();
}

static void xml_parser_exit(void)
{
	xmlCleanupParser();
}

int main(int argc, char *argv[])
{
	xmlDoc *doc;
	xmlNode *root, *child;
	xmlChar *href;
	obix_dev_t *new, *dev, *n;
	char *file;
	int size, i;

	if (argc != 3) {
		printf("Usage: %s <size of hash table> <device lobby XML file>\n", argv[0]);
		return -1;
	}

	size = atoi(argv[1]);
	file = argv[2];

	INIT_LIST_HEAD(&devlist);

	if (!(devtab = hash_init_table(size, &device_hash_ops))) {
		printf("Failed to initialise hash table\n");
		return -1;
	}
	printf("size of hash table: %d\n", devtab->size);

	xml_parser_init();

	if (!(doc = xmlReadFile(file, NULL, XML_PARSE_OPTIONS_COMMON))) {
		printf("Failed to parse XML file %s", file);
		goto failed;
	}

	root = xmlDocGetRootElement(doc);

	for (child = root->children; child; child = child->next) {
		if (child->type != XML_ELEMENT_NODE) {
			continue;
		}

		if (!(href = xmlGetProp(child, BAD_CAST "href"))) {
			continue;
		}

		if (!(new = (obix_dev_t *)malloc(sizeof(obix_dev_t)))) {
			printf("Failed to allocate a device descriptor");
			xmlFree(href);
			goto dev_failed;
		}
		memset(new, 0, sizeof(obix_dev_t));

		new->href = href;
		INIT_LIST_HEAD(&new->list);

		list_add_tail(&new->list, &devlist);

		if (hash_add(devtab, new->href, (void *)new) != 0) {
			printf("Failed to add %s to hash table\n", new->href);
			goto dev_failed;
		}
	}

	for (i = 0; i < devtab->size; i++) {
		printf("#%d, %d items:\n", i, devtab->table[i].count);
#if 0
		hash_node_t *node;
		list_for_each_entry(node, &(devtab->table[i].head), list) {
			printf("%s\n", ((obix_dev_t *)(node->item))->href);
		}
		printf("\n");
#endif
	}

	/* Fall through */

dev_failed:
	list_for_each_entry_safe(dev, n, &devlist, list) {
		list_del(&dev->list);
		xmlFree(dev->href);
		free(dev);
	}

	xmlFreeDoc(doc);

failed:
	xml_parser_exit();
	hash_destroy_table(devtab);

	return -1;
}

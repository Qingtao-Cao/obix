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

#ifndef _DEVICE_H
#define _DEVICE_H

#include <libxml/tree.h>
#include <time.h>
#include "list.h"
#include "xml_utils.h"
#include "tsync.h"

/*
 * Descriptor of a device registered on to the oBIX server, acting
 * as a wrapper or extension of relevant xmlNode in the global DOM
 * tree and providing meta information for access control and
 * multi-thread support
 */
typedef struct obix_dev {
	/* The absolute href of the device */
	unsigned char *href;

	/* The unique identifier of the owner */
	char *owner_id;

	/* The absolute pathname of the device's folder on hard-drive */
	char *dir;

	/* The absolute pathname of the device's contract file on hard-drive */
	char *file;

	/* The absolute pathname of the device's meta file on hard-drive */
	char *meta;

	/*
	 * Pointing to the root node of the device contract
	 * in the global DOM tree
	 */
	xmlNode *node;

	/* Pointing to a copy of ref node of the device */
	xmlNode *ref;

	/* Synchronisation method of multi-thread support */
	tsync_t sync;

	/*
	 * NOTE: direct children devices may reside on different levels
	 * in the hierarchy of the parent device's subtree. So the Device
	 * subsystem has to organise the relationships among device
	 * descriptors separately from the XML DOM tree
	 */

	/* Pointing to the parent device's descriptor */
	struct obix_dev *parent;

	/* A list of DIRECT children devices' descriptors */
	struct list_head children;

	/* Joining the parent device's children list */
	struct list_head siblings;

	/* Last updated timestamp of the persistent file */
	time_t mtime;
} obix_dev_t;

int is_device_href(const xmlChar *href);
int is_device_root_href(const xmlChar *href);

xmlNode *device_get_node(obix_dev_t *dev, const xmlChar *href);
xmlChar *device_node_path(obix_dev_t *dev, xmlNode *node);
xmlNode *device_copy_node(const xmlNode *src, xml_copy_exclude_flags_t flag);
int device_update_node(xmlNode *target, const xmlChar *newValue);

obix_dev_t *device_search(const xmlChar *href);
obix_dev_t *device_search_parent(const xmlChar *href);

int device_del(obix_dev_t *child, const char *requester_id, int sign_off);
int device_add(xmlNode *input, const xmlChar *href, const char *requester_id, int sign_up);

int device_add_node(xmlNode *parent, xmlNode *child);
int device_delete_node(xmlNode *node);

int device_get_op_id(const xmlNode *node, long *id);

void obix_devices_dispose(void);
int obix_devices_init(const char *resdir, const int table_size,
					  const int cache_size, const int backup_period);

xmlNode *device_dump_ref(void);

int device_write_file(obix_dev_t *dev);

#ifdef DEBUG
xmlNode *device_dump(void);
xmlNode *device_cache_dump(void);
#endif

#endif

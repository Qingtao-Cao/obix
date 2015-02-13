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

#ifndef _DEVICE_H
#define _DEVICE_H

#include <libxml/tree.h>
#include "xml_utils.h"

extern const xmlChar *OBIX_DEVICES;

int is_device_root_href(const xmlChar *href);

xmlNode *device_copy_uri(const xmlChar *href, xml_copy_flags_t flags);
int device_update_uri(const xmlChar *href, const xmlChar *new);
int device_backup_uri(const xmlChar *href);
int device_get_op_id(const xmlChar *href, long *id);
int device_link_single_node(const xmlChar *href, xmlNode *node,
							xmlNode **updated, int backup);
int device_unlink_single_node(const xmlChar *href, xmlNode *node, int backup);

int device_del(const xmlChar *href, const char *requester_id, int sign_off);
int device_add(xmlNode *input, const xmlChar *href, const char *requester_id, int sign_up);

void obix_devices_dispose(void);
int obix_devices_init(const char *resdir, const int table_size,
					  const int cache_size, const int backup_period);

xmlNode *device_dump_ref(void);

#ifdef DEBUG
xmlNode *device_dump(void);
xmlNode *device_cache_dump(void);
#endif

#endif

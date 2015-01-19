/* *****************************************************************************
 * Copyright (c) 2013-2015 Qingtao Cao
 * Copyright (c) 2014 Tyler Watson
 * Copyright (c) 2009 Andrey Litvinov
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

#ifndef XML_STORAGE_H_
#define XML_STORAGE_H_

#include <libxml/tree.h>
#include "obix_request.h"
#include "xml_utils.h"

typedef enum sys_stubs {
	ERROR_STUB = 0,
	FATAL_ERROR_STUB,
	WATCH_STUB,
	WATCH_OUT_STUB,
	BATCH_OUT_STUB,
	HIST_DEV_STUB,
	HIST_ABS_STUB,
	HIST_AOUT_STUB
} sys_stubs_t;

extern xmlDoc* _storage;

/*
 * Impose refined control on how to update the XML global database
 */
typedef enum xmldb_dom_action {
	/*
	 * Actions for xmldb_put_node
	 */
	CREATE_ANCESTORS_WATCH = 1,
	CREATE_ANCESTORS_HISTORY = (1 << 1),

	/*
	 * Actions for xmldb_delete_node
	 */
	DELETE_EMPTY_ANCESTORS_WATCH = (1 << 2)
} xmldb_dom_action_t;

void xmldb_delete_any_hidden(xmlNode *node);

xmlNode *xmldb_fatal_error(void);

int obix_xmldb_init(const char *resdir);
void obix_xmldb_dispose(void);

int xmldb_put_node(xmlNode *, const xmlChar *, xmldb_dom_action_t);

xmlNode *xmldb_get_node(const xmlChar *href);

xmlNode *xmldb_get_node_core(xmlNode *start, const xmlChar *href);

xmlNode *xmldb_copy_node(const xmlNode *orig, xml_copy_flags_t flag);

void xmldb_delete_node(xmlNode *node, xmldb_dom_action_t action);

void xmldb_delete_meta(xmlNode *node);

void xmldb_delete_comment(xmlNode *node);

void xmldb_delete_hidden(xmlNode *node);

xmlNode *xmldb_copy_uri(const xmlChar *href, xml_copy_flags_t flag);

int xmldb_update_uri(const xmlChar *href, const xmlChar *val);

xmlNode *xmldb_copy_sys(sys_stubs_t which);

int xmldb_add_child(xmlNode *parent, xmlNode *node, int unlink, int relative);

xmlNode *xmldb_set_relative_href(xmlNode *node);

int xmldb_get_op_id(const xmlChar *uri, long *id);

int xmldb_get_op_id_core(const xmlNode *node, long *id);

#ifdef DEBUG
xmlNode *xmldb_dump(obix_request_t *request);
#endif

#endif /*XML_STORAGE_H_*/

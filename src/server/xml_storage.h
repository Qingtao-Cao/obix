/* *****************************************************************************
 * Copyright (c) 2014 Tyler Watson <tyler.watson@nextdc.com>
 * Copyright (c) 2013-2015 Qingtao Cao [harry.cao@nextdc.com]
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
	DOM_CREATE_ANCESTORS_WATCH = 1,
	DOM_CREATE_ANCESTORS_HISTORY = (1 << 1),

	/*
	 * Actions for xmldb_delete_node
	 */
	DOM_DELETE_EMPTY_WATCH_PARENT = (1 << 2)
} xmldb_dom_action_t;

void xmldb_delete_any_hidden(xmlNode *node);

/**
 * Returns a pointer to a statically allocated read-only oBIX Error contract,
 * for use in adverse conditions and mallocs fail.
 */
xmlNode *xmldb_fatal_error(void);

#ifdef DEBUG
/**
 * Generates a well-formed oBIX Server dump contract for responding to clients.
 *
 * @param response      A pointer to the Response object that contains FASTCGI
 *                      environment variables
 * @return              A pointer to the allocated oBIX Server dump contract
 * @remarks             This is an allocating function. It's up to the caller to
 *                      release the memory returned by this function with xmlFreeNode().
 */
xmlNode *xmldb_dump(obix_request_t *request);

/**
 * Dumps the full contents of an xmlNode to a string buffer.
 *
 * This is an allocating function. It is up to the caller to free
 * the memory pointed to by the char.
 */
char *xmlDebugDumpNode(const xmlNode *node);
#endif

int obix_xmldb_init(const char *resdir);
void obix_xmldb_dispose(void);

/**
 * Inserts the provided node pointed to by @a node into the XML storage database.
 * @param node		A pointer to the node to be added into the XML database
 * @param action	The action when adding the given node
 * @return			0 if success, > 0 for error codes
 */
int xmldb_put_node_legacy(xmlNode *node, xmldb_dom_action_t action);

/**
 * Updates XML node to the storage. Only @a val attribute is
 * updated
 *
 * @param input         A pointer to the XML node that has a val attribute to be
 *                      written to the destination href.
 * @param target		A pointer to the target node in the XML tree
 * @param href          URI of the object to be updated.
 * @returns				0 on success, < 0 otherwise
 */
int xmldb_update_node(xmlNode *input, xmlNode *target, const char *href);

/**
 * Retrieves a node from the XML storage by the href pointed to by @a href
 *
 * @param href		A pointer to the href to retrieve from the XML database.
 * @remark		This function returns a constant pointer, and should not be
 *				manipulated or freed unless explicitly desired.
 */
xmlNode *xmldb_get_node(const xmlChar *href);
xmlNode *xmldb_get_node_legacy(xmlNode *start, const xmlChar *href);

void xmldb_delete_node(xmlNode *node, xmldb_dom_action_t action);

void xmldb_delete_meta(xmlNode *node);

void xmldb_delete_comment(xmlNode *node);

void xmldb_delete_hidden(xmlNode *node);

xmlNode *xmldb_copy_node(const xmlNode *orig, xml_copy_exclude_flags_t flag);
xmlNode *xmldb_copy_node_legacy(const xmlNode *orig, xml_copy_exclude_flags_t flag);

xmlNode *xmldb_copy_sys(sys_stubs_t which);

/**
 * Builds and returns a pointer to a string containing the full path of the
 * provided node in the XML database.  Used so clients performing GET operations
 * get sent back an object that has a proper path.
 *
 * @param	node			A pointer to an XML node in the DOM tree
 */
xmlChar *xmldb_node_path(xmlNode *node);
xmlChar *xmldb_node_path_legacy(xmlNode *start, xmlNode *top_node,
								const xmlChar *top_href);

int xmldb_add_child(xmlNode *parent, xmlNode *node, int unlink, int relative);

xmlNode *xmldb_set_relative_href(xmlNode *node);

int xmldb_get_op_id(const xmlNode *node, long *id);
int xmldb_get_op_id_legacy(const xmlNode *node, long *id);

#endif /*XML_STORAGE_H_*/

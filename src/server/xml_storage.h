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

extern xmlDoc* _storage;

/** @name URIs of various system objects.
 * These objects are not accessible for a client. They are used by server to
 * generate quickly XML data structures.
 * @{ */
/** URI of Watch object stub. */
extern const char* OBIX_SYS_WATCH_STUB;
/** URI of Error object stub. */
extern const char* OBIX_SYS_ERROR_STUB;
/** URI of WatchOut object stub. */
extern const char* OBIX_SYS_WATCH_OUT_STUB;
/** URI of BatchOut object stub. */
extern const char* OBIX_SYS_BATCH_OUT_STUB;

extern const char *OBIX_SYS_HIST_DEV_STUB;
extern const char *OBIX_SYS_ABS_STUB;
extern const char *OBIX_SYS_AOUT_STUB;
/** @} */

/**
 * Indicates the result of the update_node function.
 */
typedef enum _xmldb_errcode {
	ERR_SUCCESS = 0,
	ERR_UPDATE_NODE_BAD_BOOL,
	ERR_UPDATE_NODE_NO_SUCH_URI,
	ERR_UPDATE_NODE_NOT_WRITABLE,
	ERR_UPDATE_NODE_NO_MEM,
	ERR_UPDATE_NODE_REPARENT,
	ERR_UPDATE_NODE_BAD_INPUT,
	ERR_PUT_NODE_NO_HREF,
	ERR_PUT_NODE_NO_PARENT_URI,
	ERR_PUT_NODE_NO_PARENT_OBJ,
	ERR_PUT_NODE_EXIST,
	ERR_PUT_NODE_ADD_FAILED
} xmldb_errcode_t;

/*
 * Impose refined control on how to update the XML global database
 */
typedef enum xmldb_dom_action {
	/*
	 * Actions for xmldb_put_node
	 */
	DOM_CHECK_SANITY = 1,
	DOM_CREATE_ANCESTORS = (1 << 1),
	DOM_NOTIFY_WATCHES = (1 << 2),
	DOM_CREATE_ANCESTORS_HIST = (1 << 3),

	/*
	 * Actions for xmldb_delete_node
	 */
	DOM_DELETE_EMPTY_PARENT = (1 << 4)
} xmldb_dom_action_t;

void xmldb_delete_any_hidden(xmlNode *node);

/** Returns a pointer to a statically allocated read-only oBIX Error contract,
 * for use in adverse conditions and mallocs fail.
 */
xmlNode * const xmldb_fatal_error();

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
 * @return			0 if success, negative value otherwise.
 */
xmldb_errcode_t xmldb_put_node(xmlNode *node, xmldb_dom_action_t action);

/**
 * Updates XML node to the storage. Only @a val attribute is
 * updated and only for nodes which have @a writable attribute
 * equal to @a TRUE.
 *
 * @param input         A pointer to the XML node that has a val attribute to be
 *                      written to the destination href.
 * @param href          URI of the object to be updated.
 * @param updatedNode   An [out] pointer to the node in the XML tree that has been updated
 * @returns             An xmldb_errcode_t indicating 0 success on success,
 *                      or a positive value otherwise.
 */
xmldb_errcode_t xmldb_update_node(xmlNode *input, const char *href,
								  xmlNode **updatedNode);

/**
 * Retrieves a node from the XML storage by the href pointed to by @a href
 *
 * @param href		A pointer to the href to retrieve from the XML database.
 * @remark		This function returns a constant pointer, and should not be
 *				manipulated or freed unless explicitly desired.
 */
xmlNode *xmldb_get_node(const xmlChar *href);

void xmldb_delete_node(xmlNode *node, xmldb_dom_action_t action);

void xmldb_delete_meta(xmlNode *node);

void xmldb_delete_comment(xmlNode *node);

void xmldb_delete_hidden(xmlNode *node);

xmlNode *xmldb_copy_node(const xmlNode *orig, xml_copy_exclude_flags_t flag);

xmlNode *xmldb_copy_uri(const char *uri);

xmlNode *xmldb_copy_sys(const char *sys);

/**
 * Builds and returns a pointer to a string containing the full path of the
 * provided node in the XML database.  Used so clients performing GET operations
 * get sent back an object that has a proper path.
 *
 * @param	node			A pointer to an XML node in the DOM tree
 */
xmlChar *xmldb_node_path(xmlNode *node);

xmlNode *xmldb_create_ref(const char *lobby, xmlNode *newDevice,
						  const xmlChar *deviceHref, int *existed);

xmlNode *xmldb_add_child(xmlNode *parent, xmlNode *node, int unlink, int relative);

xmlNode *xmldb_set_relative_href(xmlNode *node);

#endif /*XML_STORAGE_H_*/

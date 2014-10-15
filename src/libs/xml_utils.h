/* *****************************************************************************
 * Copyright (c) 2014 Tyler Watson <tyler.watson@nextdc.com>
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

#ifndef _XML_UTILS_H_
#define _XML_UTILS_H_

#include <libxml/tree.h>

extern const char *XML_HEADER;
extern const char *XML_VERSION;

/**
 * Defines a list of exclusion elements that xml_copy is to obey.
 *
 * TODO:
 * 1. If xml_copy is solely used by oBIX server than any clients,
 * then move that function to xml_storage.c and combine the enum
 * defined below with xmldb_dom_action_t
 */
typedef enum {
	XML_COPY_EXCLUDE_HIDDEN = 1,
	XML_COPY_EXCLUDE_META = (1 << 1),
	XML_COPY_EXCLUDE_COMMENTS = (1 << 2)
} xml_copy_exclude_flags_t;

xmlNode *xml_copy(const xmlNode *sourceNode, xml_copy_exclude_flags_t excludeFlags);

/**
 * Prototype describing the callback function that gets called once for every
 * node in the DOM tree when being called by @a xml_for_each_node, or
 * @a xml_for_each_node_type.
 *
 * @param	node	A pointer's pointer to the current item in the DOM tree
 * @param	arg1	A pointer to a user-defined structure to be passed in
 * @param	arg2	A pointer to a user-defined structrure to be passed in
 *
 * @returns	Return -1 if @a xml_for_each_node is to break execution and return,
 *			or another value otherwise.  The calling function will always return
 *			the value returned from it's last callback iteration.
 *
 * @remarks	If callbacks are to unlink and free nodes from the tree that was pointed
 *			to by @a node, they MUST point @a node to NULL after deleting, to prevent
 *			the caller from deferencing the node they just freed.  Do NOT free
 *			@a node without pointing it to NULL.
 */
typedef int (*xml_item_cb_t)(xmlNode **item, void *arg1, void *arg2);

/**
 * Prototype of the callback function invoked by xml_xpath_for_each_item
 * on each matching node.
 *
 * Note,
 * 1. The arg0 would always point to the current matching node
 */
typedef void (*xpath_item_cb_t)(xmlNode *node, void *arg1, void *arg2);


void xml_xpath_for_each_item(xmlNode *rootNode, const char *pattern,
							 xpath_item_cb_t cb, void *arg1, void *arg2);


/**
 * Invokes @a callback for every possible node starting at @a rootNode, and matching
 * @a type.  @a type may be set to 0 to invoke the callback on any node in
 * the document, no matter what type it is.
 *
 * @param	rootNode	A pointer to the rootNode to start matching from
 * @param	callback	A pointer to the function to call on each matching element
 * @param	arg1		A user-defined pointer to carry into each callback
 * @param	arg2		A user-defined pointer to carry into each callback
 *
 * @returns		0 if all possible callbacks executed return successfully, or a callback can
 *				elect to return -1 indicating that the for_each statement should break
 *				execution and not process any further callbacks.
 *
 * @remarks		IMPORTANT:  If a callback is to free an xmlNode at any point, it MUST set
 *				the pointer to NULL, to avoid this function attempting to dereference freed
 *				memory.  If your callbacks xmlFreeNode() any node at any time, set that pointer
 *				to NULL.
 */
int xml_for_each_node_type(xmlNode *rootNode, xmlElementType type, xml_item_cb_t callback,
						   void *arg1, void *arg2);

/**
 * Invokes @a callback for every possible child element starting at the node pointed to by
 * @a rootNode.
 *
 * @param	rootNode	A pointer to the rootNode to start matching from
 * @param	callback	A pointer to the function to call on each matching element
 * @param	arg1		A user-defined pointer to carry into each callback
 * @param	arg2		A user-defined pointer to carry into each callback
 *
 * @returns		0 if all possible callbacks executed return successfully, or a callback can
 *				elect to return -1 indicating that the for_each statement should break
 *				execution and not process any further callbacks
 *
 * @remarks		IMPORTANT:  If a callback is to free an xmlNode at any point, it MUST set
 *				the pointer to NULL, to avoid this function attempting to dereference freed
 *				memory.  If your callbacks xmlFreeNode() any node at any time, set that pointer
 *				to NULL.
 */
int xml_for_each_element(xmlNode *rootNode, xml_item_cb_t callback,
					     void *arg1, void *arg2);

int xml_for_each_comment(xmlNode *rootNode, xml_item_cb_t callback,
					     void *arg1, void *arg2);

xmlNode *xml_find_child(const xmlNode *parent, const char *tag,
						const char *attrName, const char *attrVal);

long xml_get_long(xmlNode *inputNode, const char *attrName);

char *xml_get_child_href(const xmlNode *parent, const char *tag,
						const char *nameVal);

char *xml_get_child_val(const xmlNode *parent, const char *tag,
						const char *nameVal);

long xml_get_child_long(const xmlNode *parent, const char *tag,
						const char *nameVal);

int xml_is_hidden(const xmlNode *node);

int xml_is_null(const xmlNode *node);

int xml_for_each_ancestor_or_self(xmlNode *child, xml_item_cb_t callback,
								  void *arg1, void *arg2);

char *xml_dump_node(const xmlNode *node);

xmlNode *obix_obj_null(void);

void xml_delete_node(xmlNode *node);
void xml_remove_children(xmlNode *parent);

#ifdef DEBUG
int xml_is_valid_doc(const char *, const char *);
#endif

/*
 * Return 1 if the given href is valid, 0 otherwise including
 * the following cases:
 *  . empty string;
 *  . a single "/";
 *  . containing any "." (".." inclusive) in any position;
 *  . starting with any whitespace characters, but they are allowed
 *	  in the middle;
 *  . containing more than one consecutive slashes in any position.
 */
int xml_is_valid_href(xmlChar *);

#endif	/* _XML_UTILS_H_ */

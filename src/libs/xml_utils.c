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
 * along with oBIX. If not, see <http://www.gnu.org/licenses/>.
 *
 * *****************************************************************************/

#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <ctype.h>		/* isblank */
#include <string.h>		/* strlen */
#include "xml_utils.h"
#include "obix_utils.h"
#include "log_utils.h"

const char *XML_HEADER = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n";
const char *XML_VERSION = "1.0";

/**
 * Apply the given callback on the given node and all its ancestors
 * all the way up to the topest level.
 *
 * Note,
 * 1. The callback function should check the xmlNode->type of the
 * current node and return if it is not desirable.
 */
int xml_for_each_ancestor_or_self(xmlNode *child,
								  xml_item_cb_t callback,
								  void *arg1, void *arg2)
{
	int ret;

	if (!child) {
		return 0;
	}

	if ((ret = callback(&child, arg1, arg2)) < 0) {
		return ret;
	}

	return xml_for_each_ancestor_or_self(child->parent, callback, arg1, arg2);
}

/**
 * Apply the specified callback function on every single node
 * with satisfactory type in the given subtree
 */
int xml_for_each_node_type(xmlNode *rootNode, xmlElementType type,
						   xml_item_cb_t callback, void *arg1, void *arg2)
{
	xmlNode *nextNode = NULL;
	int ret = 0;

	if (!rootNode) {
		return 0;
	}

	do {
		/*
		 * Save the next node's pointer in case the callback function
		 * may delete the current node
		 */
		nextNode = rootNode->next;

		/* If type equals to 0 then skip the comparison of it */
		if (type > 0 && rootNode->type != type) {
			continue;
		}

		/*
		 * IMPORTANT !!
		 *
		 * If the callback function deletes the given node, it MUST
		 * NULLIFY its pointer so as to avoid touching its descendant
		 */
		if ((ret = callback(&rootNode, arg1, arg2)) < 0) {
			break;
		}

		if (rootNode != NULL) {
			if ((ret = xml_for_each_node_type(rootNode->children, type,
											  callback, arg1, arg2)) < 0) {
				break;
			}
		}

	} while ((rootNode = nextNode) != NULL);

	return ret;
}

int xml_for_each_element(xmlNode *rootNode, xml_item_cb_t callback,
						 void *arg1, void *arg2)
{
	return xml_for_each_node_type(rootNode, XML_ELEMENT_NODE,
								  callback, arg1, arg2);
}

int xml_for_each_comment(xmlNode *rootNode, xml_item_cb_t callback,
						 void *arg1, void *arg2)
{
	return xml_for_each_node_type(rootNode, XML_COMMENT_NODE,
								  callback, arg1, arg2);
}

/**
 * Check if the given node has the specified attribute
 * and set as true
 *
 * Return 1 if this is the case, 0 otherwise (No such attribute
 * or its value is false)
 */
static int xml_attr_true(const xmlNode *node, const char *attr)
{
	xmlChar *prop;
	int ret;

	if (!(prop = xmlGetProp((xmlNode *)node, BAD_CAST attr))) {
		return 0;
	}

	ret = xmlStrcmp(prop, BAD_CAST XML_TRUE);
	xmlFree(prop);

	return (ret == 0) ? 1 : 0;
}

int xml_is_hidden(const xmlNode *node)
{
	return xml_attr_true(node, OBIX_ATTR_HIDDEN);
}

int xml_is_null(const xmlNode *node)
{
	return xml_attr_true(node, OBIX_ATTR_NULL);
}

/**
 * Re-enterant version of xml_copy.
 *
 * Note:
 * 1. recursionDepth keeps tracks of the number of times this function
 * recalls itself, it is used to return meta or hidden or comments
 * object if and only if they are explicitly requested, that is, when
 * recursionDepth equals to 1. Otherwise all such objects will be
 * skipped over according to the exludeFlags
 */
static xmlNode *xml_copy_r(const xmlNode *sourceNode,
						   xml_copy_exclude_flags_t excludeFlags,
						   int recursionDepth)
{
	xmlNode *nextNode = NULL;
	xmlNode *copyRoot = NULL;
	xmlNode *copyChild = NULL;

	if (!sourceNode) {
		return NULL;
	}

	if (recursionDepth > 0
		&& (excludeFlags & XML_COPY_EXCLUDE_HIDDEN) == XML_COPY_EXCLUDE_HIDDEN
		&& xml_is_hidden(sourceNode) == 1) {
		return NULL;
	}

	if (recursionDepth > 0
		&& (excludeFlags & XML_COPY_EXCLUDE_META) == XML_COPY_EXCLUDE_META
		&& xmlStrcmp(sourceNode->name, BAD_CAST OBIX_OBJ_META) == 0) {
		return NULL;
	}

	if (recursionDepth > 0
		&& (excludeFlags & XML_COPY_EXCLUDE_COMMENTS) == XML_COPY_EXCLUDE_COMMENTS
		&& sourceNode->type == XML_COMMENT_NODE) {
		return NULL;
	}

	/* Note:
	 * 2 to xmlCopyNode() means to copy node and all attributes,
	 * but no child elements.
	 */
	if ((copyRoot = xmlCopyNode((xmlNode *)sourceNode, 2)) == NULL) {
		log_error("Failed to copy the node");
		return NULL;
	}

	for (nextNode = sourceNode->children; nextNode; nextNode = nextNode->next) {
		if (nextNode->type != XML_ELEMENT_NODE ||
			!(copyChild = xml_copy_r(nextNode, excludeFlags, ++recursionDepth))) {
			continue;
		}

		if (xmlAddChild(copyRoot, copyChild) == NULL) {
			log_error("Failed to add the child copy into the current node");
			xmlFreeNode(copyChild);
		};
	}

	return copyRoot;
}

xmlNode *xml_copy(const xmlNode *sourceNode, xml_copy_exclude_flags_t excludeFlags)
{
	return xml_copy_r(sourceNode, excludeFlags, 0);
}

/**
 * Find in the specified DOM tree for a set of nodes that match the
 * given pattern, then invoke the provided callback function on each
 * of them
 */
void xml_xpath_for_each_item(xmlNode *rootNode, const char *pattern,
							 xpath_item_cb_t cb, void *arg1, void *arg2)
{
	xmlDoc *doc = NULL;
	xmlXPathObject *objects;
	xmlXPathContext *context;
	xmlNode *member;
	int i;

	if (!rootNode || !pattern || !cb) {		/* parameters are optional */
		return;
	}

	/*
	 * Create a temporary document for the standalone node
	 * that is not part of the global DOM tree
	 */
	if (!rootNode->doc) {
		if (!(doc = xmlNewDoc(BAD_CAST XML_VERSION))) {
			log_error("Failed to generate temp doc for XPath context");
			return;
		}

		xmlDocSetRootElement(doc, rootNode);
	}

	if (!(context = xmlXPathNewContext(rootNode->doc))) {
		log_warning("Failed to create a XPath context");
		goto ctx_failed;
	}

	/*
	 * If the provide node is not standalone but in the global DOM tree,
	 * have the search start from the current node instead of from
	 * the root of the global DOM tree
	 */
	if (!doc) {
		context->node = rootNode;
	}

	if (!(objects = xmlXPathEval(BAD_CAST pattern, context))) {
		log_warning("Unable to compile XPath expression: %s", pattern);
		goto xpath_failed;
	}

	/*
	 * Apply callback function on each matching node
	 */
	if (xmlXPathNodeSetIsEmpty(objects->nodesetval) == 0) {
		for (i = 0; i < xmlXPathNodeSetGetLength(objects->nodesetval); i++) {
			member = xmlXPathNodeSetItem(objects->nodesetval, i);
			/*
			 * Apply the callback function on each node tracked by
			 * the node set, and more importantly, nullify relevant
			 * pointer in the node set table just in case the callback
			 * function will release the pointed node. Since the whole
			 * node set will be released soon, this won't bring about
			 * any side effect at all.
			 *
			 * Or otherwise valgrind will detect invalid read at below
			 * address:
			 *
			 *		xmlXPathFreeNodeSet (xpath.c:4185)
			 *		xmlXPathFreeObject (xpath.c:5492)
			 *
			 * when it tries to access the xmlNode(its type member)
			 * that has been deleted!
			 */
			if (member != NULL) {
				cb(member, arg1, arg2);
				objects->nodesetval->nodeTab[i] = NULL;
			}
		}
	}

	/*
	 * If a temporary doc has been manipulated, unlink
	 * the original rootNode node from it so that the doc
	 * could be freed with no side effects on the rootNode
	 */
	if (doc) {
		xmlUnlinkNode(rootNode);
	}

	/* Fall through */

	xmlXPathFreeObject(objects);

xpath_failed:
	xmlXPathFreeContext(context);

ctx_failed:
	if (doc) {
		xmlFreeDoc(doc);
	}
}

/**
 * Helper function to find a direct child with matching tag
 * that has the specified attribute. If the val parameter is
 * not NULL, the value of that attribute must be the same
 * as val.
 *
 * If @a tag points to NULL, no comparison on the tag name is
 * performed (ie. it's ignored).
 *
 * Note,
 * 1. For sake of performance, oBIX server should strive to establish
 * a hierarchy organization of all XML objects, the global DOM tree
 * should strike a proper balance among its depth and width. If too
 * many direct children organized directly under one same parent,
 * this function will suffer hugh performance loss.
 */
xmlNode *xml_find_child(const xmlNode *parent, const char *tag,
						const char *attrName, const char *attrVal)
{
	xmlNode *node;
	xmlChar *attr_val;
	int ret;

	if (!parent || !attrName) {		/* attrVal, tag are optional */
		return NULL;
	}

	for (node = parent->children; node; node = node->next) {
		if (node->type != XML_ELEMENT_NODE) {
			continue;		/* only interested in normal nodes */
		}

		if (tag != NULL && xmlStrcmp(node->name, BAD_CAST tag) != 0) {
			continue;		/* tag not matching */
		}

		if (!(attr_val = xmlGetProp(node, BAD_CAST attrName))) {
			continue;		/* no specified attribute */
		}

		if (!attrVal) {			/* no need to compare attr's value */
			xmlFree(attr_val);
			return node;
		}

		ret = xmlStrcmp(attr_val, BAD_CAST attrVal);
		xmlFree(attr_val);

		if (ret == 0) {
			return node;	/* Found */
		}
	}

	return NULL;
}

/**
 * Get the value of the specified attribute of the given
 * node, and try to convert it to a long integer
 */
long xml_get_long(xmlNode *node, const char *attrName)
{
	char *attr_val;
	long val;
	int ret;

	if (!(attr_val = (char *)xmlGetProp(node, BAD_CAST attrName))) {
		return -1;
	}

	ret = str_to_long(attr_val, &val);
	free(attr_val);

    return (ret == 0) ? val : ret;
}

/**
 * Get the href attribute of a matching children of the given node
 * with specific tag and name attribute.
 *
 * Note,
 * 1. Callers should release the returned string once done with it
 */
char *xml_get_child_href(const xmlNode *parent, const char *tag,
						 const char *nameVal)
{
	xmlNode *node;

	if (!(node = xml_find_child(parent, tag, OBIX_ATTR_NAME, nameVal))) {
		return NULL;
	}

	return (char *)xmlGetProp(node, BAD_CAST OBIX_ATTR_HREF);
}

/**
 * Get the value attribute of a matching children of the given node
 * with specific tag and name attribute.
 *
 * Note,
 * 1. Callers should release the returned string once done with it
 */
char *xml_get_child_val(const xmlNode *parent, const char *tag,
						const char *nameVal)
{
	xmlNode *node;

	if (!(node = xml_find_child(parent, tag, OBIX_ATTR_NAME, nameVal))) {
		return NULL;
	}

	return (char *)xmlGetProp(node, BAD_CAST OBIX_ATTR_VAL);
}

/**
 * Get the value attribute of a matching children of the given node
 * with specific tag and attribute, and try to convert it into a
 * long integer
 */
long xml_get_child_long(const xmlNode *parent, const char *tag,
						const char *nameVal)
{
	char *value;
	long l;
	int ret;

	if (!(value = xml_get_child_val(parent, tag, nameVal))) {
		return -1;
	}

	ret = str_to_long(value, &l);
	free(value);

    return (ret == 0) ? l : ret;
}

/*
 * Copy the implementation of this function since
 * it does not exist on libxml2-2.7
 */
static char *xml_buffer_detach(xmlBuffer *buf)
{
	xmlChar *ret;

	if (!buf || buf->alloc == XML_BUFFER_ALLOC_IMMUTABLE) {
		return NULL;
	}

	ret = buf->content;
	buf->content = NULL;
	buf->size = buf->use = 0;

	return (char *)ret;
}

char *xml_dump_node(const xmlNode *node)
{
	xmlBuffer *buffer = NULL;
	char *data;

	if (!node) {
		return NULL;
	}

	if (!(buffer = xmlBufferCreate())) {
		return NULL;
	}

	/*
	 * level == 0, since it is undesirable to add extra indenting
	 * for the closing </obj> for each record;
	 *
	 * format == 1, providing node indenting when xmlKeepBlanksDefault(0)
	 * has been called
	 */
	data = xmlNodeDump(buffer, NULL, (xmlNode *)node, 0, 1) > 0 ?
					   xml_buffer_detach(buffer) : NULL;

	xmlBufferFree(buffer);

	return data;
}

xmlNode *obix_obj_null(void)
{
    xmlNode *node;

	if (!(node = xmlNewNode(NULL, BAD_CAST OBIX_OBJ)) ||
	    !xmlSetProp(node, BAD_CAST OBIX_ATTR_NULL, BAD_CAST XML_TRUE)) {
		xmlFreeNode(node);
		node = NULL;
	}

    return node;
}

void xml_delete_node(xmlNode *node)
{
	if (!node) {
		return;
	}

	/*
	 * Always unlink the to-be-deleted node
	 * just in case it belongs to any document
	 */
	xmlUnlinkNode(node);
	xmlFreeNode(node);
}

/*
 * Remove all chidlren from a given node
 */
void xml_remove_children(xmlNode *parent)
{
	xmlNode *child, *sibling;

	if (!parent) {
		return;
	}

	for (child = parent->children, sibling = ((child) ? child->next : NULL);
		 child;
		 child = sibling, sibling = ((child) ? child->next : NULL)) {
		if (child->type != XML_ELEMENT_NODE) {
			continue;		/* only interested in normal nodes */
		}

		xml_delete_node(child);
	}
}

#ifdef DEBUG
int xml_is_valid_doc(const char *data, const char *contract)
{
	xmlDoc *doc = NULL;
	xmlNode *root;
	xmlChar *is_attr = NULL;
	xmlChar *href = NULL;
	int ret = 0;

	if (!(doc = xmlReadMemory(data, strlen(data),
							  NULL, NULL,
							  XML_PARSE_OPTIONS_COMMON))) {
		log_error("The provided data is not a valid XML document: %s", data);
		return 0;
	}

	if (!(root = xmlDocGetRootElement(doc))) {
		log_error("The provided XML document has no root node: %s", data);
		goto failed;
	}

	if ((href = xmlGetProp(root, BAD_CAST OBIX_ATTR_HREF)) != NULL &&
		xml_is_valid_href(href) == 0) {
		log_error("The provided XML document has invalid href in its "
				  "root node: %s", data);
		goto failed;
	}

	if (contract &&
		(!(is_attr = xmlGetProp(root, BAD_CAST OBIX_ATTR_IS)) ||
		 xmlStrcmp(is_attr, BAD_CAST contract) != 0)) {
		log_error("The provided data contains an illegal contract: %s "
				  "(Required %s)", is_attr, contract);
		goto failed;
	}

	ret = 1;	/* Success */

	/* Fall through */

failed:
	if (href) {
		xmlFree(href);
	}

	if (is_attr) {
		xmlFree(is_attr);
	}

	xmlFreeDoc(doc);
	return ret;
}
#endif

int xml_is_valid_href(xmlChar *href)
{
	int len, i;

	if (!href) {
		return 0;
	}

	/* Empty string, or a single slash */
	if ((len = strlen((char *)href)) == 0 ||
		(len == 1 && *href == '/')) {
		return 0;
	}

	/*
	 * Start with any whitespace characters, which will make
	 * oBIX server create node with href containing just
	 * whitespace characters
	 *
	 * NOTE: whitespace characters such as spaces or even tabs
	 * are allowed in the middle of a href
	 */
	if (isspace(*href) != 0) {
		return 0;
	}

	/*
	 * dirname and basename both regard "." or ".."
	 * as empty string. To live with them, no dot allowed
	 * anywhere
	 */
	for (i = 0; i < len; i++) {
		if (*(href + i) == '.') {
			return 0;
		}
	}

	if (strstr((char *)href, "//") != NULL) {
		return 0;
	}

	return 1;	/* valid */
}

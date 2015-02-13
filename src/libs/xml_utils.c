/* *****************************************************************************
 * Copyright (c) 2013-2015 Qingtao Cao
 * Copyright (c) 2014 Tyler Watson
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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>		/* isblank */
#include <string.h>		/* strlen */
#include <sys/uio.h>	/* writev */
#include <errno.h>
#include "xml_utils.h"
#include "obix_utils.h"
#include "log_utils.h"

const char *XML_HEADER = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>";
const const int XML_HEADER_LEN = 38;
const char *XML_VERSION = "1.0";

static const xmlChar *DOUBLE_SLASHES = (xmlChar *)"//";

/*
 * The root href for various subsystems on the oBIX server
 *
 * NOTE: they must end up with a slash as expected by the source code
 */
href_info_t obix_roots[] = {
	[OBIX_BATCH] = {
		.root = (xmlChar *)"/obix/batch/",
		.len = 12
	},
	[OBIX_DEVICE] = {
		.root = (xmlChar *)"/obix/deviceRoot/",
		.len = 17
	},
	[OBIX_WATCH] = {
		.root = (xmlChar *)"/obix/watchService/",
		.len = 19
	},
	[OBIX_HISTORY] = {
		.root = (xmlChar *)"/obix/historyService/",
		.len = 21
	}
};

/*
 * Return 1 if the given href belongs to the specific
 * subsystem, 0 otherwise
 *
 * NOTE: the given href must start with the root href of a particular
 * subsystem, and the next byte must be either NULL or slash and
 * shouldn't be any other character
 */
int is_given_type(const xmlChar *href, obix_root_t type)
{
	xmlChar next;
	const xmlChar *root = obix_roots[type].root;
	int len = obix_roots[type].len - 1;		/* root hrefs suffixed by a slash */

	if (xmlStrncmp(href, root, len) == 0) {
		next = href[len];
		if (next == '\0' || next == '/') {
			return 1;
		}
	}

	return 0;
}

/**
 * Compare whether the given two strings are identical. If lenient == 1,
 * ignore any trailing slashes if present
 *
 * Return 1 if this is the case, 0 otherwise
 */
int is_str_identical(const xmlChar *str1, const xmlChar *str2,
					 const int lenient)
{
	int len1, len2, i;

	if (!str1 || !str2) {
		return 0;
	}

	len1 = xmlStrlen(str1);
	len2 = xmlStrlen(str2);

	if (lenient == 1) {
		if (str1[len1 - 1] == '/') {
			len1--;
		}

		if (str2[len2 - 1] == '/') {
			len2--;
		}
	}

	if (len1 != len2) {
		return 0;
	}

	if (len1 == 0 && len2 == 0) {
		return 1;	/* both strings are "/" */
	}

	/*
	 * NOTE: for sake of performance, compare from the tail to the head
	 * since strings tend to be different in their tails
	 */
	for (i = len1 - 1; i >= 0; i--) {
		if (str1[i] != str2[i]) {
			return 0;
		}
	}

	return 1;
}

/*
 * Create and set up a reference node for the given node
 *
 * Return the reference node address on success, NULL otherwise
 */
xmlNode *xml_create_ref_node(xmlNode *src, const xmlChar *href)
{
	xmlNode *ref = NULL;
	xmlChar *val;
	int i;

	/*
	 * NOTE: avoid declaring this pointer array as a global variable
	 * which requires initialisation at compiling time and existing
	 * OBIX_ATTR_XXXX globals can't be used as its initialiser
	 */
	const char *ref_attrs[] = {
		OBIX_ATTR_NAME,
		OBIX_ATTR_DISPLAY,
		OBIX_ATTR_DISPLAY_NAME,
		OBIX_ATTR_IS,
		NULL
	};

	if (!src || !href) {
		return NULL;
	}

	if (!(ref = xmlNewNode(NULL, BAD_CAST OBIX_OBJ_REF))) {
		log_error("Failed to allocate a ref node for %s", href);
		return NULL;
	}

	if (!xmlSetProp(ref, BAD_CAST OBIX_ATTR_HREF, href)) {
		log_error("Failed to setup href for the ref node of %s", href);
		xmlFreeNode(ref);
		return NULL;
	}

	/* Set up other good-to-have attributes */
	for (i = 0; ref_attrs[i]; i++) {
		if ((val = xmlGetProp(src, BAD_CAST ref_attrs[i])) != NULL) {
			xmlSetProp(ref, BAD_CAST ref_attrs[i], val);
			xmlFree(val);
		}
	}

	return ref;
}

/**
 * Apply the given callback on the given node and its ancestors
 * until reaching the limit
 *
 * Note,
 * 1. The callback function should check the xmlNode->type of the
 * current node and return if it is not desirable.
 */
int xml_for_each_ancestor_or_self(xmlNode *start, xmlNode *stop,
								  xml_item_cb_t callback,
								  void *arg1, void *arg2)
{
	int ret;

	if (!start || (stop && start == stop)) {
		return 0;
	}

	if ((ret = callback(&start, arg1, arg2)) < 0) {
		return ret;
	}

	return xml_for_each_ancestor_or_self(start->parent, stop,
										 callback, arg1, arg2);
}

/**
 * Apply the specified callback function on every single node
 * with satisfactory type in the given subtree
 */
int xml_for_each_node_type(xmlNode *rootNode, xmlElementType type,
						   xml_item_cb_t callback, void *arg1, void *arg2,
						   int depth)
{
	xmlNode *sibling = NULL;
	int ret = 0;

	if (!rootNode) {
		return 0;
	}

	do {
		/*
		 * NOTE: Only move on to siblings when the invocation depth
		 * is greater than 0, so as to prevent trespassing on siblings
		 * of the original given node
		 *
		 * Save the sibling's pointer in case the callback function
		 * may delete the current node
		 */
		sibling = (depth == 0) ? NULL : rootNode->next;

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
											  callback, arg1, arg2,
											  ++depth)) < 0) {
				break;
			}
		}

	} while ((rootNode = sibling) != NULL);

	return ret;
}

int xml_for_each_element(xmlNode *rootNode, xml_item_cb_t callback,
						 void *arg1, void *arg2)
{
	return xml_for_each_node_type(rootNode, XML_ELEMENT_NODE,
								  callback, arg1, arg2, 0);
}

int xml_for_each_comment(xmlNode *rootNode, xml_item_cb_t callback,
						 void *arg1, void *arg2)
{
	return xml_for_each_node_type(rootNode, XML_COMMENT_NODE,
								  callback, arg1, arg2, 0);
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
 * NOTE: depth parameter keeps tracks of the number of times this
 * function recalls itself, it is used to return meta or hidden or
 * comments object if and only if they are explicitly requested,
 * that is, when it equals to 0. Otherwise all such objects will be
 * skipped over according to the excluding flag
 */
static xmlNode *xml_copy_r(const xmlNode *src, xml_copy_flags_t flags, int depth)
{
	xmlNode *child, *copyRoot, *copyChild;

	child = copyRoot = copyChild = NULL;

	if (!src) {
		return NULL;
	}

    if (depth > 0) {
        if (((flags & EXCLUDE_HIDDEN) > 0 && xml_is_hidden(src) == 1) ||
            ((flags & EXCLUDE_META) > 0 &&
			 xmlStrcmp(src->name, BAD_CAST OBIX_OBJ_META) == 0) ||
            ((flags & EXCLUDE_COMMENTS) > 0 && src->type == XML_COMMENT_NODE)) {
			return NULL;
        }
    }

	/*
	 * NOTE: 2 to xmlCopyNode() means to copy node and all attributes,
	 * but no child elements.
	 */
	if (!(copyRoot = xmlCopyNode((xmlNode *)src, 2))) {
		log_error("Failed to copy the node");
		return NULL;
	}

	for (child = src->children; child; child = child->next) {
		if (!(copyChild = xml_copy_r(child, flags, ++depth))) {
			/*
			 * The current child may have been deliberatly excluded,
			 * move on to the next one
			 */
			continue;
		}

		if (!xmlAddChild(copyRoot, copyChild)) {
			log_error("Failed to add the child copy into the current node");
			xmlFreeNode(copyChild);
			goto failed;
		}
	}

	return copyRoot;

failed:
	xmlFreeNode(copyRoot);
	return NULL;
}

xmlNode *xml_copy(const xmlNode *src, xml_copy_flags_t flags)
{
	return xml_copy_r(src, flags, 0);	/* start from depth == 0 */
}

/**
 * Helper function to find a direct child with matching tag
 * that has the specified attribute.
 *
 * NOTE: for sake of performance, oBIX server should strive to establish
 * a hierarchy organization of all XML objects, the global DOM tree
 * should strike a proper balance among its depth and width. If too
 * many direct children organized directly under one same parent,
 * this function will inflict hugh performance loss.
 */
xmlNode *xml_find_child(const xmlNode *parent, const char *tag,
						const char *attrName, const char *attrVal)
{
	xmlNode *node;
	xmlChar *attr_val = NULL;
	int ret;

	/* Callers must specify at least one of tag and attrName */
	if (!parent || (!attrName && !tag)) {
		return NULL;
	}

	for (node = parent->children; node; node = node->next) {
		if (node->type != XML_ELEMENT_NODE) {
			continue;			/* only interested in normal nodes */
		}

		if (tag && xmlStrcmp(node->name, BAD_CAST tag) != 0) {
			continue;			/* tag not matching */
		}

		if (!attrName) {		/* no need to worry about the attribute at all */
			return node;
		}

		if (!(attr_val = xmlGetProp(node, BAD_CAST attrName))) {
			continue;			/* no specified attribute */
		}

		if (!attrVal) {			/* no need to compare attr's value */
			xmlFree(attr_val);
			return node;
		}

		ret = xmlStrcmp(attr_val, BAD_CAST attrVal);
		xmlFree(attr_val);

		if (ret == 0) {
			return node;		/* Found */
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

	if (nameVal) {
		node = xml_find_child(parent, tag, OBIX_ATTR_NAME, nameVal);
	} else {
		node = xml_find_child(parent, tag, NULL, NULL);
	}

	return (node) ? (char *)xmlGetProp(node, BAD_CAST OBIX_ATTR_VAL) : NULL;
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

xmlNode *obix_obj_null(const xmlChar *href)
{
    xmlNode *node = NULL;

	if (!(node = xmlNewNode(NULL, BAD_CAST OBIX_OBJ)) ||
		(href && !xmlSetProp(node, BAD_CAST OBIX_ATTR_HREF, href)) ||
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

/*
 * Return 1 if the given href is valid, 0 otherwise
 */
int xml_is_valid_href(const xmlChar *href)
{
	int len, i;

	if (!href) {
		return 0;
	}

	/* Empty string, or a single slash */
	if ((len = xmlStrlen(href)) == 0 ||
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

	if (xmlStrstr(href, DOUBLE_SLASHES) != NULL) {
		return 0;
	}

	return 1;	/* valid */
}

/*
 * Write into the specified file with the given data provisioning
 * it with a XML header
 *
 * Return > 0 on success, < 0 otherwise
 */
int xml_write_file(const char *path, int flags, const char *data, int size)
{
	struct iovec iov[2];
	int fd;
	int ret = -1;

	iov[0].iov_base = (char *)XML_HEADER;
	iov[0].iov_len = XML_HEADER_LEN;
	iov[1].iov_base = (char *)data;
	iov[1].iov_len = size;

	/*
	 * NOTE: without the usage of O_TRUNC, the latest file size must be
	 * explicitly setup in order to get rid of any leftover of previous
	 * snapshot, otherwise the updated XML file will become mal-formed!
	 */
	if ((fd = open(path, flags)) >= 0) {
		errno = 0;
		if ((ret = writev(fd, iov, 2)) < 0) {
			log_error("Failed to write into %s due to %s",
					  path, strerror(errno));
		} else {
			errno = 0;
			if (ftruncate(fd, size + XML_HEADER_LEN) < 0) {
				log_warning("Failed to truncate %s due to %s",
							path, strerror(errno));
			}
		}

		close(fd);
	}

	return ret;
}

static int xml_setup_private_helper(xmlNode **element, void *arg1, void *arg2)
{
	if (!*element) {
		return -1;
	}

	(*element)->_private = arg1;

	return 0;
}

/*
 * Traverse the given subtree and setup each node's _private pointer
 * according to the given parameter
 */
void xml_setup_private(xmlNode *node, void *arg)
{
	xml_for_each_node_type(node, 0, xml_setup_private_helper, arg, NULL, 0);
}

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

#include <regex.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <libgen.h>
#include <libxml/tree.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include "xml_config.h"
#include "xml_utils.h"
#include "obix_utils.h"
#include "log_utils.h"
#include "xml_storage.h"
#include "watch.h"
#include "device.h"
#include "errmsg.h"

/*
 * System stubs manipulated by oBIX server, which are basically
 * template for relevant oBIX contracts
 */
const char *obix_sys_stubs[] = {
	[ERROR_STUB] = "/sys/error-stub/",
	[FATAL_ERROR_STUB] = "/sys/fatal-error-stub/",
	[WATCH_STUB] = "/sys/watch-stub/",
	[WATCH_OUT_STUB] = "/sys/watch-out-stub/",
	[BATCH_OUT_STUB] = "/sys/batch-out-stub/",
	[HIST_DEV_STUB] = "/sys/hist-dev-stub/",
	[HIST_ABS_STUB] = "/sys/hist-abstract-stub/",
	[HIST_AOUT_STUB] = "/sys/hist-aout-stub/",
};

static const char *SERVER_DB_DIR_CORE = "core";
static const char *SERVER_DB_DIR_SYS = "sys";
static const char *SERVER_DB_FILE_PREFIX = "server_";
static const char *SERVER_DB_FILE_SUFFIX = ".xml";

/*
 * Descriptor to assemble absolute href of a node, used by
 * xmldb_node_path
 */
typedef struct list_href {
	xmlChar *href;
	struct list_head list;
} list_href_t;

/*
 * The type of the stub that new node should be cloned from
 */
typedef enum xmldb_stub_type {
	STUB_NORMAL = 0,
	STUB_HISTORY = 1
}xmldb_stub_type_t;


/* The place where all data is stored. */
xmlDoc *_storage = NULL;

/*
 * The error contract allocated at the initialization of the XML
 * database, so as to be returned to clients when oBIX server would
 * have to be re-booted and have potential memory leaks eliminated
 */
static xmlNode *__xmldb_fatal_error;

/**
 * Returns a pre-allocated fatal error contract, which will be released
 * once sent back to oBIX clients as normal response, therefore its
 * pointer should be nullified.
 *
 * Note,
 * 1. There is only one fatal error contract and it will be released
 * once being sent out to oBIX client. Therefore oBIX server should not
 * continue operating in case of fatal error, otherwise there won't be
 * any more fatal error contract to fall back on.
 */
xmlNode *xmldb_fatal_error(void)
{
	xmlNode *node = __xmldb_fatal_error;

	__xmldb_fatal_error = NULL;

	return node;
}

static int xmldb_set_relative_href_helper(xmlNode **current, void *arg1, void *arg2)
{
	/* arg1 and arg2 are simply ignored */
	char *href;
	int hrefLen, i, ret = 0;
	xmlNode *node = *current;

	/*
	 * The href attribute of a reference node will be absolute
	 * URI and must be left intact, since it's used to specify
	 * the real location of relevant object in the DOM tree
	 */
	if (xmlStrcmp(node->name, BAD_CAST OBIX_OBJ_REF) == 0 ||
		!(href = (char *)xmlGetProp(node, BAD_CAST OBIX_ATTR_HREF))) {
		return 0;
	}

	if (slash_preceded(href) == 0) {	/* Already relative */
		goto out;
	}

	hrefLen = strlen(href);

	for (i = hrefLen - 1; i >= 0; i--) {
		if (href[i] == '/') {
			hrefLen--;
		} else {
			break;
		}
	}

	if (hrefLen > 0) {
		/*
		 * Discard all potential trailing slashes.
		 *
		 * This is EXTREMELY important since the href of the root node
		 * of some XML setting files are ended with slash, therefore
		 * trailing slashes must be discarded so that basename can
		 * retrieve their parent href properly
		 */
		href[hrefLen] = '\0';
		if (xmlSetProp(node, BAD_CAST OBIX_ATTR_HREF,
					   BAD_CAST basename(href)) == NULL) {
			log_error("could not set @href on the provided node.");
			ret = -1;
		}
	} else if (hrefLen == 0) {
		log_error("Invalid href with all slashes");
		ret = -1;
	}

out:
	free(href);
	return ret;
}

/**
 * Sets the href attribute of every single node in the given
 * subtree relative, that is, not preceded or followed by
 * any slashes.
 *
 * @param node		A pointer to xmlNode structure containing
 *					a fully allocated oBIX Object
 */
xmlNode *xmldb_set_relative_href(xmlNode *node)
{
	if (node != NULL) {
		xml_for_each_element(node, xmldb_set_relative_href_helper, NULL, NULL);
	}

	return node;
}

/*
 * Add the given node as a child of the specified parent node
 *
 * Return 0 on success, > 0 for error code
 */
int xmldb_add_child(xmlNode *parent, xmlNode *node,
					int unlink,			/* 0 for newly created node */
					int relative)		/* 1 for copied node */
{
	int ret = 0;

	if (!parent || !node) {
		log_error("Illegal parameters provided to xmldb_add_child");
		return ERR_INVALID_ARGUMENT;
	}

	/*
	 * Detach the current node from its original document if existing
	 * so that deleting the latter eventually won't impact the global
	 * DOM tree.
	 */
	if (unlink == 1) {
		xmlUnlinkNode(node);
	}

	/*
	 * EXTREMELY IMPORTANT
	 *
	 * Set the node's href relative, that is, not preceded or followed
	 * by any slashes before inserting into global DOM tree. This is
	 * critical for inserting the root node of loaded document or
	 * otherwise xmldb_node_path will fail on its subtree.
	 */
	if (relative == 1) {
		xmldb_set_relative_href(node);
	}

	/*
	 * xmlAddChild will take care of setting up required context of the
	 * newly added child, e.g., relationships in parent sub tree and
	 * having child's document pointer pointing to parent's owner document
	 */
	if (xmlAddChild(parent, node) == NULL) {
		log_error("Failed to add node into global DOM tree!");
		ret = ERR_NO_MEM;
	}

	return ret;
}

/**
 * Copy a node, unlink the copy from the original document.
 *
 * In case the orig points to NULL, then duplicate a "null" object.
 * This happens when the monitored object of a watch_item_t has
 * been deleted.
 *
 * Anyway, a "null" object will help notify clients that relevant
 * object does not exist any more.
 */
xmlNode *xmldb_copy_node_legacy(const xmlNode *orig,
								xml_copy_exclude_flags_t flag)
{
	xmlNode *node;

	node = (orig == NULL) ? obix_obj_null(NULL) : xml_copy(orig, flag);

	if (orig && node) {
		xmlUnlinkNode(node);
	}

	return node;
}

xmlNode *xmldb_copy_node(const xmlNode *node, xml_copy_exclude_flags_t flag)
{
	return (node->_private) ? device_copy_node(node, flag) :
							  xmldb_copy_node_legacy(node, flag);
}

/**
 * Get a copy from the specified system template and remove
 * the href from it, which is useless to oBIX clients
 */
xmlNode *xmldb_copy_sys(sys_stubs_t which)
{
	xmlNode *root = xmlDocGetRootElement(_storage);
	xmlNode *node, *copy;

	if (!(node = xmldb_get_node_legacy(root, (xmlChar *)obix_sys_stubs[which])) ||
		!(copy = xmldb_copy_node_legacy(node, 0))) {
		log_error("Failed to copy from %s", obix_sys_stubs[which]);
		return NULL;
	}

	xmlUnsetProp(copy, BAD_CAST OBIX_ATTR_HREF);

	return copy;
}

/**
 * Delete the specified node/subtree from the global DOM tree,
 * and strive for XML cache coherence by nullifying any cache
 * reference to any node in the deleted subtree.
 *
 * If the direct parent of the deleted node contains no
 * descendants, then delete the parent node as well. This is
 * the case for watch objects. However, this function won't
 * go further than that.
 */
void xmldb_delete_node(xmlNode *node, xmldb_dom_action_t action)
{
	xmlNode *parent = node->parent;

	xml_delete_node(node);

	if ((action & DOM_DELETE_EMPTY_WATCH_PARENT) > 0 &&
		parent != NULL &&
		xmlChildElementCount(parent) == 0) {
		xmldb_delete_node(parent, 0);
	}
}

static int xmldb_delete_comment_helper(xmlNode **comment, void *arg1, void *arg2)
{
	if (!*comment) {
		return -1;
	}

	xmldb_delete_node(*comment, 0);
	*comment = NULL;

	return 0;
}

/**
 * Remove potential comments in the sub-tree as rooted by
 * the specified node
 */
void xmldb_delete_comment(xmlNode *node)
{
	xml_for_each_comment(node, xmldb_delete_comment_helper, NULL, NULL);
}

static int xmldb_delete_meta_helper(xmlNode **element, void *arg1, void *arg2)
{
	/* both parameters are ignored */

	if (!*element) {
		return -1;
	}

	if (xmlStrcmp((*element)->name, BAD_CAST OBIX_OBJ_META) == 0) {
		xmldb_delete_node(*element, 0);
		*element = NULL;
	}

	return 0;
}

/**
 * Remove potential meta tags from the entire DOM tree which
 * contains the specified node.
 */
void xmldb_delete_meta(xmlNode *node)
{
	xml_for_each_element(node, xmldb_delete_meta_helper, NULL, NULL);
}


static int xmldb_delete_hidden_helper(xmlNode **element, void *arg1, void *arg2)
{
	/* both parameters are ignored */

	if (!*element) {
		return -1;
	}

	if (xml_is_hidden(*element) == 1) {
		xmldb_delete_node(*element, 0);
		*element = NULL;
	}

	return 0;
}

/**
 * Remove potential hidden nodes in the given subtree
 */
void xmldb_delete_hidden(xmlNode *node)
{
	xml_for_each_element(node, xmldb_delete_hidden_helper, NULL, NULL);
}

static void list_href_dispose(list_href_t *item)
{
	if (item->href) {
		xmlFree(item->href);
	}

	free(item);
}

static int xmldb_node_path_legacy_helper(xmlNode **current,
										 void *arg1, void *arg2)
{
	xmlNode *node = *current;
	xmlChar *href;
	list_href_t *head = (list_href_t *)arg1;
	list_href_t *item;

	if (node->type != XML_ELEMENT_NODE) {
		return 0;
	}

	if (!(href = xmlGetProp(node, BAD_CAST OBIX_ATTR_HREF))) {
		return -1;
	}

	/* Skip adding "/" href into the queue of list_href_t */
	if (xmlStrcmp(href, BAD_CAST "/") == 0) {
		xmlFree(href);
		return 0;
	}

	if (!(item = (list_href_t *)malloc(sizeof(list_href_t)))) {
		return -1;
	}
	memset(item, 0, sizeof(list_href_t));

	item->href = href;
	list_add(&item->list, &head->list);

	return 0;
}

/**
 * Get the absolute URI of the given node.
 *
 * Note,
 * 1. Callers should release the URI string once done with it
 */
xmlChar *xmldb_node_path_legacy(xmlNode *start, xmlNode *top_node,
								const xmlChar *top_href)
{
	list_href_t head, *item, *n;
	char *href = NULL;

	INIT_LIST_HEAD(&head.list);

	if (!(head.href = xmlStrdup(top_href))) {
		return NULL;
	}

	if (xml_for_each_ancestor_or_self(start, top_node,
									  xmldb_node_path_legacy_helper,
									  &head, NULL) < 0) {
		goto failed;
	}

	list_for_each_entry(item, &head.list, list) {
		if (link_pathname(&href, (char *)head.href, NULL,
						  (char *)item->href, NULL) < 0) {
			goto failed;
		}

		xmlFree(head.href);
		head.href = (xmlChar *)href;
	}

	goto out;

failed:
	xmlFree(head.href);
	head.href = NULL;

out:
	list_for_each_entry_safe(item, n, &head.list, list) {
		list_href_dispose(item);
	}

	return head.href;
}

xmlChar *xmldb_node_path(xmlNode *node)
{
	return (node->_private) ? device_node_path(node->_private, node) :
					xmldb_node_path_legacy(node, NULL, (const xmlChar *)"/");
}

#ifdef DEBUG
/**
 * Iterates through the request environment pointed to by @a response and inserts
 * all the request environment variables as child nodes to the environment root node
 * pointed to by @a environmentRoot.
 *
 * @param request	    A pointer to the @a oBIX Request object that holds request
 *                          information.
 * @returns                 A pointer to the newly created obix:List XML subtree
 *                          with all the FastCGI variables listed as <str>'s in it,
 *                          or NULL if an error occurred.
 * @remark                  This is an allocating function.  It's up to the caller
 *                          to free the memory returned from this function with xmlFree().
 */
static xmlNode *xmldb_fcgi_var_list(obix_request_t *request)
{
	char **envp;
	xmlNode *envList = NULL;
	xmlNode *curEnvNode = NULL;

	if (!(envList = xmlNewDocNode(_storage, NULL, BAD_CAST OBIX_OBJ_LIST, NULL))) {
		log_error("Failed to allocate the oBIX:List contract");
		return NULL;
	}

	if (xmlSetProp(envList, BAD_CAST OBIX_ATTR_IS,
				   BAD_CAST "obix:FastCGIEnvironment") == NULL ||
		xmlSetProp(envList, BAD_CAST OBIX_ATTR_OF,
				   BAD_CAST "obix:Str") == NULL) {
		log_error("Failed to set attributes on the environment list.");
		goto failed;
	}

	for (envp = request->request->envp; *envp != NULL; ++envp) {
		if (!(curEnvNode = xmlNewDocNode(_storage, NULL, BAD_CAST OBIX_OBJ_STR, NULL))) {
			log_error("Failed to allocate the oBIX:str value for FCGI variable");
			break;
		}

		if (xmlSetProp(curEnvNode, BAD_CAST OBIX_ATTR_VAL, BAD_CAST *envp) == NULL) {
			log_error("Failed to set the \"val\" attribute");
			break;
		}

		if (xmldb_add_child(envList, curEnvNode, 0, 0) != 0) {
			log_error("Failed to add the child str node to the environment list.");
			break;
		}
	}

	if (*envp == NULL) {	/* Successfully reaching the end of env list */
		return envList;
	}

	if (curEnvNode) {
		xmlFreeNode(curEnvNode);
	}

failed:
	xmlFreeNode(envList);

	return NULL;
}

xmlNode *xmldb_dump(obix_request_t *request)
{
	xmlNode *dump = NULL;
	xmlNode *fcgiVarList = NULL;
	xmlNode *storageCopy = NULL;

	if (!(dump = xmlNewNode(NULL, BAD_CAST OBIX_OBJ))) {
		log_error("Failed to allocate a XML node to build up response");
		goto failed;
	}

	xmlSetProp(dump, BAD_CAST OBIX_ATTR_IS, BAD_CAST "obix:EnvironmentDump");

	if (!(fcgiVarList = xmldb_fcgi_var_list(request))) {
		log_error("Failed to return the FASTCGI environment contract");
		goto failed;
	}

	if (!(storageCopy =
			xml_copy(xmlDocGetRootElement(_storage),
					 XML_COPY_EXCLUDE_COMMENTS | XML_COPY_EXCLUDE_HIDDEN))) {
		log_error("Failed to copy the XML storage");
		goto storage_failed;
	}

	if (xmldb_add_child(dump, fcgiVarList, 0, 0) != 0 ||
		xmldb_add_child(dump, storageCopy, 0, 0) != 0) {
		log_error("Failed to add children to the output element");
	} else {
		return dump;	/* Success */
	}

	/* Failure */

	xmlFreeNode(storageCopy);

storage_failed:
	xmlFreeNode(fcgiVarList);

failed:
	if (dump) {
		xmlFreeNode(dump);
	}

	return xmldb_fatal_error();
}

char *xmlDebugDumpNode(const xmlNode *node)
{
	return (node != NULL) ? xml_dump_node(node) : NULL;
}
#endif

static int xmldb_get_node_legacy_helper(const char *token,
										void *arg1, void *arg2)
{
	xmlNode **node = (xmlNode **)arg1;		/* arg2 is ignored */

	if (!*node || !(*node = xml_find_child(*node, NULL, OBIX_ATTR_HREF, token))) {
		return -1;
	}

	return 0;
}

xmlNode *xmldb_get_node_legacy(xmlNode *start, const xmlChar *href)
{
	xmlNode *node = start;

	if (for_each_str_token(STR_DELIMITER_SLASH, (const char *)href,
						   xmldb_get_node_legacy_helper, &node, NULL) < 0) {
		node = NULL;
	}

	return node;
}

xmlNode *xmldb_get_node(const xmlChar *href)
{
	obix_dev_t *dev;
	xmlNode *root = xmlDocGetRootElement(_storage);
	xmlNode *node = NULL;

	if (xmlStrcmp(href, BAD_CAST "/") == 0) {
		return root;
	}

	if (is_device_href(href) == 0 || is_device_root_href(href) == 1) {
		return xmldb_get_node_legacy(root, href);
	}

	/* href points to a device node */
	if ((dev = device_search(href)) != NULL ||
		(dev = device_search_parent(href)) != NULL) {
		node = device_get_node(dev, href);
	}

	return node;
}

static int xmldb_create_ancestors_helper(const char *token, void *arg1,
										 void *arg2)
{
	xmlNode *parent = *(xmlNode **)arg1;
	xmldb_stub_type_t *type = (xmldb_stub_type_t *)arg2;
	xmlNode *node;

	if (!(node = xml_find_child(parent, NULL, OBIX_ATTR_HREF, token))) {

		if (*type == STUB_NORMAL) {
			node = xmlNewNode(NULL, BAD_CAST OBIX_OBJ);
		} else  if (*type == STUB_HISTORY) {
			node = xmldb_copy_sys(HIST_DEV_STUB);
		}

		if (!node) {
			return -1;
		}

		if (xmlSetProp(node, BAD_CAST OBIX_ATTR_HREF, BAD_CAST token) == NULL ||
			xmldb_add_child(parent, node, 0, 0) != 0) {
			xmlFreeNode(node);
			return -1;
		}
	}

	*(xmlNode **)arg1 = node;
	return 0;
}

/**
 * Create any missing ancestor from the root of global DOM tree
 * all the way down to the node with the specified href
 *
 * Note,
 * 1. Practice caution when calling this function since misuse
 * will easily lead to a mess in the global DOM tree. Therefore
 * this function should not be used by signUp handler
 */
static xmlNode *xmldb_create_ancestors(const xmlChar *href,
									   xmldb_stub_type_t *type)
{
	xmlNode *parent = xmlDocGetRootElement(_storage);

	if (for_each_str_token(STR_DELIMITER_SLASH, (const char *)href,
						   xmldb_create_ancestors_helper, &parent, type) < 0) {
		parent = NULL;
	}

	return parent;
}

/**
 * Adds provided node to the XML storage by behaving in the
 * specified manner.
 *
 * NOTE: this function will add node into the global DOM tree
 * directly and should be used to add non-Device nodes such as
 * watch objects or history facilities, whereas all device nodes
 * must be registered via the signUp handler
 *
 * return 0 on success, > 0 for error codes
 */
int xmldb_put_node_legacy(xmlNode *node, xmldb_dom_action_t action)
{
	xmlNode *parentNode = NULL;
	xmlChar *href = NULL;
	xmlChar *parentHref = NULL;
	xmldb_stub_type_t type = STUB_NORMAL;
	int ret = 0;

	if (!(href = xmlGetProp(node, BAD_CAST OBIX_ATTR_HREF))) {
		return ERR_NO_HREF;
	}

	if (xml_is_valid_href(href) == 0) {
		ret = ERR_INVALID_HREF;
		goto failed;
	}

	if (!(parentHref = xmlStrdup(href)) ||
		!(parentHref = (xmlChar *)dirname((char *)parentHref))) {
		ret = ERR_NO_MEM;
		goto failed;
	}

	if (!(parentNode = xmldb_get_node(parentHref))) {
		if ((action & (DOM_CREATE_ANCESTORS_WATCH |
					   DOM_CREATE_ANCESTORS_HISTORY)) == 0) {
			ret = ERR_NO_SUCH_URI;
			goto failed;
		} else {
			if ((action & DOM_CREATE_ANCESTORS_HISTORY) > 0) {
				type = STUB_HISTORY;
			}

			if (!(parentNode = xmldb_create_ancestors(parentHref, &type))) {
				ret = ERR_NO_MEM;
				goto failed;
			}
		}
	}

	ret = xmldb_add_child(parentNode, node, 1, 1);

	/* Fall through */

failed:
	if (parentHref) {
		xmlFree(parentHref);
	}

	if (href) {
		xmlFree(href);
	}

	return ret;
}

/**
 * Reparent children of the "from" node to the "to" node.
 *
 * Extra copy of the child node in the "from" tree is performed
 * when necessary.
 *
 * Return 0 on success, > 0 for error code
 */
static int xmldb_reparent_children(xmlNode *from, xmlNode *to,
								   int dict_used)
{
	xmlNode *child, *sibling, *peer, *copy;
	xmlChar *name, *href;

	if (!from || !to) {
		return 0;
	}

	/*
	 * An extra sibling pointer is a must-have since child->next
	 * will be changed once the child node is re-parented!
	 */
	for (child = from->children, sibling = ((child) ? child->next : NULL);
		 child;
		 child = sibling, sibling = ((child) ? child->next : NULL)) {
		if (child->type != XML_ELEMENT_NODE) {
			continue;
		}

		/*
		 * Find its peer under the destination node
		 *
		 * Since reference nodes can't be addressed by their hrefs which
		 * point to other nodes, their name attributes are checked instead
		 *
		 * For other tags, the href attributes are examined
		 */
		if (xmlStrcmp(child->name, BAD_CAST OBIX_OBJ_REF) == 0) {
			if (!(name = xmlGetProp(child, BAD_CAST OBIX_ATTR_NAME))) {
				continue;
			}

			peer = xml_find_child(to, OBIX_OBJ_REF, OBIX_ATTR_NAME,
								  (const char *)name);
			xmlFree(name);
		} else {
			if (!(href = xmlGetProp(child, BAD_CAST OBIX_ATTR_HREF))) {
				continue;
			}

			peer = xml_find_child(to, (const char *)child->name,
								  OBIX_ATTR_HREF, (const char *)href);
			xmlFree(href);
		}

		/*
		 * Reparent the child node if it is not a null object and
		 * its peer does not exist
		 */
		if (!peer && xml_is_null(child) == 0) {
			/*
			 * NOTE: when a XML parser dictionary is used, the child in
			 * the "from" tree is copied before inserted into the global
			 * DOM tree to ensure its integrity from any thread-specific
			 * parser dictionary
			 */
			if (dict_used == 1) {
				if (!(copy = xml_copy(child, XML_COPY_EXCLUDE_COMMENTS))) {
					return ERR_NO_MEM;
				}
			} else {	/* no dictionary used, from tree is standalone */
				copy = child;
			}

			if (xmldb_add_child(to, copy, 1, 1) != 0) {
				xmlFreeNode(copy);
				return ERR_NO_MEM;
			}
		}
	}

	return 0;
}

static int xmldb_load_files_helper(const char *dir, const char *file, void *arg)
{
	xmlDoc *doc = NULL;
	xmlNode *root = NULL;
	xmlNode *duplicated = NULL;
	xmlChar *href = NULL;
	char *path;
	int ret = -1;

	if (link_pathname(&path, dir, NULL, file, NULL) < 0) {
		log_error("Failed to assemble absolute path name for %s/%s", dir, file);
		return -1;
	}

	/*
	 * No parser dictionary is used when oBIX server configuration files
	 * are parsed and then inserted into the global DOM tree
	 */
	if (!(doc = xmlReadFile(path, NULL,
							XML_PARSE_OPTIONS_COMMON | XML_PARSE_NODICT))) {
		log_error("Unable to parse XML document %s", path);
		goto failed;
	}

	if (!(root = xmlDocGetRootElement(doc))) {
		log_error("The provided XML document %s doesn't have a root node",
				  file);
		goto failed;
	}

	if (!(href = xmlGetProp(root, BAD_CAST OBIX_ATTR_HREF))) {
		log_error("No href to insert the provided object from %s "
				  "into the XML database", file);
		goto failed;
	}

	xmldb_delete_comment(root);

	/*
	 * If the root node of the current XML document already exists
	 * in the DOM tree and is not a reference, re-parent all its
	 * children to that node.
	 *
	 * Otherwise, add the whole root subtree under its parent,
	 * which will reset all descendants' doc pointer pointing
	 * to the owner document of the parent node.
	 */
	if ((duplicated = xmldb_get_node(href)) != NULL &&
		(xmlStrcmp(duplicated->name, BAD_CAST OBIX_OBJ_REF)) != 0) {
		if (xmldb_reparent_children(root, duplicated, 0) != 0) {
			log_error("Failed to re-parent children of the root node "
					  "loaded from %s into existing node", href);
			goto failed;
		}
	} else {
		if (xmldb_put_node_legacy(root, 0) != 0) {
			log_error("Failed to add root node from %s "
					  "into the XML database.", path);
			/*
			 * The root node of the loaded config file may have
			 * been unlinked from that file already, therefore
			 * needs to be deleted explicitly
			 */
			xmlFreeNode(root);
			goto failed;
		}
	}

	log_debug("%s loaded successfully", file);
	ret = 0;

	/* Fall through */

failed:
	if (href) {
		xmlFree(href);
	}

	if (doc) {
		xmlFreeDoc(doc);
	}

	free(path);

	return ret;
}

/**
 * Loads all static XML setting files in different sub-folders,
 * it doesn't matter in what order files in one sub-folder are
 * loaded
 *
 * NOTE: persistent device files are loaded later by the Device
 * subsystem
 *
 * Return 0 on success, > 0 for error code
 */
static int xmldb_load_files(const char *resdir)
{
	char *dir;

	if (link_pathname(&dir, resdir, NULL, SERVER_DB_DIR_CORE, NULL) < 0) {
		log_error("Failed to assemble absolute pathname for %s",
				  SERVER_DB_DIR_CORE);
		return ERR_NO_MEM;
	}

	if (for_each_file_name(dir, SERVER_DB_FILE_PREFIX, SERVER_DB_FILE_SUFFIX,
						   xmldb_load_files_helper, NULL) < 0) {
		log_error("Failed to load XML files under %s", dir);
		free(dir);
		return ERR_NO_MEM;
	}
	free(dir);

	if (link_pathname(&dir, resdir, NULL, SERVER_DB_DIR_SYS, NULL) < 0) {
		log_error("Failed to assemble absolute pathname for %s",
				  SERVER_DB_DIR_SYS);
		return ERR_NO_MEM;
	}

	if (for_each_file_name(dir, SERVER_DB_FILE_PREFIX, SERVER_DB_FILE_SUFFIX,
						   xmldb_load_files_helper, NULL) < 0) {
		log_error("Failed to load XML DB files under %s", dir);
		free(dir);
		return ERR_NO_MEM;
	}
	free(dir);

	return 0;
}

/*
 * Initialise the whole global DOM tree of the oBIX server
 *
 * Return 0 on success, > 0 for error code
 */
int obix_xmldb_init(const char *resdir)
{
	xmlNode *newRootNode = NULL;
	int ret = ERR_NO_MEM;

	if (_storage) {
		return 0;
	}

	if (!(_storage = xmlNewDoc(BAD_CAST XML_VERSION))) {
		log_error("Unable to allocate a new doc node for the XML database");
		return ERR_NO_MEM;
	}

	if (!(newRootNode = xmlNewNode(NULL, BAD_CAST OBIX_OBJ))) {
		log_error("Failed to allocate a new root node for the XML database");
		goto failed;
	}

	if (!xmlSetProp(newRootNode, BAD_CAST OBIX_ATTR_HREF, BAD_CAST "/")) {
		log_error("Failed to set @href on XML storage root node.");
		goto root_node_failed;
	}

	xmlDocSetRootElement(_storage, newRootNode);

	if ((ret = xmldb_load_files(resdir)) != 0 ||
		!(__xmldb_fatal_error = xmldb_copy_sys(FATAL_ERROR_STUB))) {
		goto failed;
	}

	log_debug("The XML database initialised");
	return 0;

root_node_failed:
	xmlFreeNode(newRootNode);

failed:
	obix_xmldb_dispose();
	return ret;
}

/*
 * Dispose the whole XML database on oBIX server.
 *
 * Note,
 * 1. All global pointers would have to be nullified because
 * this cleanup function is likely to be invoked again somewhere
 * in when bringing down oBIX server due to errors
 */
void obix_xmldb_dispose(void)
{
	if (__xmldb_fatal_error) {
		xmlFreeNode(__xmldb_fatal_error);
		__xmldb_fatal_error = NULL;
	}

	if (_storage != NULL) {
		xmlFreeDoc(_storage);
		_storage = NULL;
	}

	log_debug("The XML database disposed");
}

/*
 * Update the val attribute of the target node
 *
 * Return 0 for success, > 0 for error codes
 */
int xmldb_update_node(xmlNode *input, xmlNode *target, const char *href)
{
	xmlChar *newValue = NULL;
	char *href_src, *href_dst, *href_copy;
	int ret = 0;

	/* check on href only if provided */
	href_src = (char *)xmlGetProp(input, BAD_CAST OBIX_ATTR_HREF);
	if (href_src) {
		if (!(href_copy = strdup(href))) {
			free(href_src);
			return ERR_NO_MEM;
		}

		href_dst = (slash_preceded(href_src) == 1) ? href_copy : basename(href_copy);

		ret = strcmp(href_dst, href_src);
		free(href_src);
		free(href_copy);

		if (ret != 0) {
			return ERR_INVALID_HREF;
		}
	}

	if (!(newValue = xmlGetProp(input, BAD_CAST OBIX_ATTR_VAL))) {
		return ERR_NO_MEM;
	}

	/*
	 * TODO:
	 * The sanity check on data types should be enforced here
	 */
	if (xmlStrcmp(input->name, BAD_CAST OBIX_OBJ_BOOL) == 0 &&
		xmlStrcmp(newValue, BAD_CAST XML_TRUE) != 0 &&
		xmlStrcmp(newValue, BAD_CAST XML_FALSE) != 0) {
		xmlFree(newValue);
		return ERR_INVALID_OBJ;
	}

	if (target->_private) {
		ret = device_update_node(target, newValue);
	} else if (!xmlSetProp(target, BAD_CAST OBIX_ATTR_VAL, newValue)) {
		ret = ERR_NO_MEM;
	}

	xmlFree(newValue);
	return ret;
}

int xmldb_get_op_id_legacy(const xmlNode *node, long *id)
{
	xmlNode *meta;

	*id = 0;

	if (xmlStrcmp(node->name, BAD_CAST OBIX_OBJ_OP) != 0) {
		return ERR_NO_OP_NODE;
	}

	if (!(meta = xml_find_child(node, OBIX_OBJ_META,
								OBIX_META_ATTR_OP, NULL))) {
		return ERR_NO_META_NODE;
	}

	return (*id = xml_get_long(meta, OBIX_META_ATTR_OP)) < 0 ?
			ERR_INVALID_META : 0;
}

/*
 * Get the value of the "op" meta node from the given node
 *
 * Return 0 on success, > 0 for error code
 */
int xmldb_get_op_id(const xmlNode *node, long *id)
{
	return (node->_private) ? device_get_op_id(node, id) :
							  xmldb_get_op_id_legacy(node, id);
}

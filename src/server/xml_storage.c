/* *****************************************************************************
 * Copyright (c) 2014 Tyler Watson <tyler.watson@nextdc.com>
 * Copyright (c) 2013-2014 Qingtao Cao [harry.cao@nextdc.com]
 * Copyright (c) 2009 Andrey Litvinov
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 * *****************************************************************************/

#include <assert.h>
#include <regex.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <libgen.h>
#include <libxml/tree.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include "libxml_config.h"
#include "xml_utils.h"
#include "hist_utils.h"
#include "obix_utils.h"
#include "log_utils.h"
#include "xml_storage.h"
#include "watch.h"

/*
 * System stubs manipulated by oBIX server, which are basically
 * template for relevant oBIX contracts
 */
const char *OBIX_SYS_WATCH_STUB = "/sys/watch-stub/";
const char *OBIX_SYS_WATCH_OUT_STUB = "/sys/watch-out-stub/";
const char *OBIX_SYS_BATCH_OUT_STUB = "/sys/batch-out-stub/";
const char *OBIX_SYS_ERROR_STUB = "/sys/error-stub/";
const char *OBIX_SYS_FATAL_ERROR_STUB = "/sys/fatal-error-stub/";
const char *OBIX_SYS_HIST_DEV_STUB = "/sys/hist-dev-stub/";
const char *OBIX_SYS_ABS_STUB = "/sys/hist-abstract-stub/";
const char *OBIX_SYS_AOUT_STUB = "/sys/hist-aout-stub/";

/*
 * Lobby to accommodate reference nodes for all signed up devices
 */
const char *OBIX_DEVICE_LOBBY_URI = "/obix/devices/";

/*
 * Lobby to accommodate all history facilities
 */
const char *OBIX_HISTORY_LOBBY_URI = "/obix/historyService/histories/";

/*
 * All permanent XML setting files that should be loaded during
 * oBIX server starts-up, which are all at <res>/server/ folder
 * with particular prefix and suffix
 *
 * The sequence to load files under different sub-folders is:
 *
 *	core -> sys -> devices
 *
 * Whereas files in the same folder would be loaded in
 * an random order
 */
static const char *SERVER_DB_DIR_CORE = "core";
static const char *SERVER_DB_DIR_SYS = "sys";
static const char *SERVER_DB_DIR_DEVICES = "devices";
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

/* The place where all data is stored. */
xmlDoc *_storage = NULL;

/*
 * The error contract allocated at the initialization of the XML
 * database, so as to be returned to clients when oBIX server would
 * have to be re-booted and have potential memory leaks eliminated
 */
static xmlNode *__xmldb_fatal_error;

/*
 * The maximal number of iterations performed when checking if one
 * given node or its parent are writable
 *
 * The limitation on searching depth is introduced to help reduce
 * potential mistake of inadvertantly setting up 'writable="true"'
 * attribute to one node which will result in any of its descendants
 * writable
 */
/* static int SEARCH_WRITABLE_SELF = 1;	*/		/* not used yet */
static int SEARCH_WRITABLE_PARENT = 2;
static int SEARCH_WRITABLE_INDEFINITE = -1;		/* use with care! */

/**
 * Cache of the XML database to take advantage of the "locality
 * principle" used when finding a node with the specified href.
 *
 * !! IMPORTANT !!
 * 1. Since xmldb_cache_update will move existing members in the
 * cache forward by one offset, and xmldb_invalid_cache will have
 * to copy every deleted node with ALL cache slot to ensure cache
 * coherence, a relatively large cache size will be increasingly
 * cumbersome when a subtree having lots of descendants is deleted
 */
#define XMLDB_CACHE_LEN		4

typedef struct cache_node {
	xmlNode *node;			/* reference to node in the global DOM tree */
	xmlChar *href;			/* the node's absolute href */
} cache_node_t;

static cache_node_t *_xmldb_cache;

static long _xmldb_cache_hit;
static long _xmldb_cache_miss;

#ifdef DEBUG_CACHE
long xmldb_get_cache_hit(void)
{
	return _xmldb_cache_hit;
}

long xmldb_get_cache_miss(void)
{
	return _xmldb_cache_miss;
}
#endif

static int xmldb_init_cache(void)
{
	if (!(_xmldb_cache = (cache_node_t *)malloc(sizeof(cache_node_t) *
												XMLDB_CACHE_LEN))) {
		return -1;
	}
	memset(_xmldb_cache, 0, sizeof(cache_node_t) * XMLDB_CACHE_LEN);

	_xmldb_cache_hit = _xmldb_cache_miss = 0;

	return 0;
}

static void xmldb_dispose_cache_node(int i)
{
	assert(i >= 0 && i < XMLDB_CACHE_LEN);

	if (_xmldb_cache[i].href != NULL) {
		xmlFree(_xmldb_cache[i].href);
		_xmldb_cache[i].href = NULL;
	}

	_xmldb_cache[i].node = NULL;
}

static void xmldb_dispose_cache(void)
{
	int i;

	for (i = 0; i < XMLDB_CACHE_LEN; i++) {
		xmldb_dispose_cache_node(i);
	}

	free(_xmldb_cache);
}

/**
 * Update the cache of XML database, the first pointer at index 0
 * will always point to the latest search result, while the rest
 * of pointers in the cache will be moved down by one offset, so
 * that xmldb_search_cache can manipulate the latest search result
 */
static void xmldb_update_cache(xmlNode *node, const xmlChar *href)
{
	int loc = XMLDB_CACHE_LEN - 1;

	/*
	 * Could not eliminate but avoid duplicates in cache
	 * as much as possible
	 */
	if (_xmldb_cache[0].node == node) {
		return;
	}

	if (_xmldb_cache[loc].href != NULL) {
		free(_xmldb_cache[loc].href);
	}

	while (loc > 0) {
		_xmldb_cache[loc].node = _xmldb_cache[loc - 1].node;
		_xmldb_cache[loc].href = _xmldb_cache[loc - 1].href;
		loc--;
	}

	if (!(_xmldb_cache[0].href = xmlStrdup(href))) {
		_xmldb_cache[0].node = NULL;
	} else {
		_xmldb_cache[0].node = node;
	}
}

static int xmldb_invalid_cache_helper(xmlNode **current, void *arg1, void *arg2)
{
	/*both arg1 and arg2 are ignored */
	int i;
	xmlNode *node = *current;

	for (i = 0; i < XMLDB_CACHE_LEN; i++) {
		if (_xmldb_cache[i].node == node) {
			log_warning("Nullify cached node at %d with href %s", i,
						_xmldb_cache[i].href);
			xmldb_dispose_cache_node(i);
		}

		/*
		 * Have to keep on iterating the whole cache since one node
		 * could be cached up more than once
		 */
	}

	return 0;
}

/**
 * Nullify any reference in the cache of XML database to any nodes
 * in the subtree that is to be deleted.
 *
 * Node deletion happens in signOff handler and Watch.Delete handler.
 *
 * TODO:
 * 1. It could be both time-consuming and tricky to maintain cache
 * coherence.
 *
 * To this end, there is a need to search if the cache has any
 * reference to every node in the subtree to be deleted. Moreover,
 * xmldb_delete_node would have to be made the only entry point
 * to delete a node from the global DOM tree so as to ensure the
 * cache will get properly updated, otherwise dangling pointers
 * will bring about segfault.
 *
 * Furthermore, in a less likely case that a node's href is
 * changed, relvant cache node would have to be invalidated as
 * well.
 */
static void xmldb_invalid_cache(xmlNode *root)
{
	if (root->doc != _storage) {
		return;
	}

	xml_for_each_element(root, xmldb_invalid_cache_helper, NULL, NULL);
}

/**
 * Search the cache of XML database.
 *
 * Return a xmlNode pointer on cache hit, while NULL on cache miss
 */
static xmlNode *xmldb_search_cache(const xmlChar *href)
{
	xmlNode *node;
	int i;

	for (i = 0; i < XMLDB_CACHE_LEN; i++) {
		if (!(node = _xmldb_cache[i].node) || !_xmldb_cache[i].href) {
			/*
			 * Keep searching the rest of cache instead of break
			 * since the current reference may have been nullified
			 */
			continue;
		}

		/* Cache hit */
		if (str_is_identical((const char *)_xmldb_cache[i].href,
							 (const char *)href) == 0) {
			xmldb_update_cache(node, href);
			_xmldb_cache_hit++;
			return node;
		}
	}

	/* Cache miss */
	_xmldb_cache_miss++;
	return NULL;
}

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
xmlNode *const xmldb_fatal_error()
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

	assert(node);

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
 * subtree relative, that is, not preceded or folllowed by
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

xmlNode *xmldb_add_child(xmlNode *parent, xmlNode *node,
						 int unlink,		/* 0 for newly created node */
						 int relative)		/* 1 for copied node */
{
	assert(parent && node);

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
		return NULL;
	}

	return node;
}

/**
 * Copy a node, unlink the copy from the original document and return it.
 */
xmlNode *xmldb_copy_node(const xmlNode *orig, xml_copy_exclude_flags_t flag)
{
	xmlNode *node;

	if (!(node = xml_copy(orig, flag))) {
		log_error("Failed to copy a node");
		return NULL;
	}

	xmlUnlinkNode(node);

	return node;
}

/**
 * Copy a node at the specified URI.
 *
 * Return NULL if no matching node found in the DOM tree.
 *
 * Note,
 * 1. The caller needs to release the copied node once done with it.
 */
xmlNode *xmldb_copy_uri(const char *uri)
{
	xmlNode *node;

	if (!(node = xmldb_get_node((xmlChar *)uri))) {
		log_error("Failed to locate a node at %s", uri);
		return NULL;
	}

	return xmldb_copy_node(node, 0);
}

/**
 * Get a copy from the specified template and remove
 * the href from it, which is useless to oBIX clients
 */
xmlNode *xmldb_copy_sys(const char *sys)
{
	xmlNode *instance;
	xmlAttr *href;

	if (!(instance = xmldb_copy_uri(sys))) {
		return NULL;
	}

	if ((href = xmlHasProp(instance, BAD_CAST OBIX_ATTR_HREF)) != NULL) {
		if (xmlRemoveProp(href) != 0) {
			log_error("Failed to remove attr % from sys object %s",
						OBIX_ATTR_HREF, sys);
		}
	}

	return instance;
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

	xmldb_invalid_cache(node);
	xmlUnlinkNode(node);
	xmlFreeNode(node);

	if ((action & DOM_DELETE_EMPTY_PARENT) > 0 &&
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

/**
 * Creates a reference to the new object in the specified lobby.
 */
xmlNode *xmldb_put_ref(const char *lobby, xmlNode *newDevice, int *existed)
{
	xmlNode *deviceTable = NULL;
	xmlNode *node = NULL;
	xmlNode *deviceRefNode = NULL;
	xmlChar *deviceHref = NULL;
	xmlChar *deviceName = NULL;
	xmlChar *deviceDisplayName = NULL;
	xmlChar *deviceDisplay = NULL;
	xmlChar *deviceIs = NULL;

	assert(lobby && newDevice);

	*existed = 0;

	if (!(deviceTable = xmldb_get_node(BAD_CAST lobby))) {
		log_error("Failed to locate the device table");
		return NULL;
	}

	if (!(deviceHref = xmlGetProp(newDevice, BAD_CAST OBIX_ATTR_HREF))) {
		log_error("No href attribute in the device node");
		return NULL;
	}

	if ((node = xml_find_child(deviceTable, OBIX_OBJ_REF,
							   OBIX_ATTR_HREF, (const char *)deviceHref)) != NULL) {
		log_debug("Ref with href %s already exist", deviceHref);
		xmlFree(deviceHref);
		*existed = 1;
		return node;
	}

	if (!(deviceName = xmlGetProp(newDevice, BAD_CAST OBIX_ATTR_NAME))) {
		log_error("No name attribute in the device node");
		goto failed;
	}

	deviceDisplayName = xmlGetProp(newDevice, BAD_CAST OBIX_ATTR_DISPLAY_NAME);
	deviceDisplay = xmlGetProp(newDevice, BAD_CAST OBIX_ATTR_DISPLAY);
	deviceIs = xmlGetProp(newDevice, BAD_CAST OBIX_ATTR_IS);

	if (!(deviceRefNode = xmlNewDocNode(_storage, NULL, BAD_CAST OBIX_OBJ_REF, NULL))) {
		log_error("Failed to allocate a device ref node.");
		goto new_node_failed;
	}

	if (xmlSetProp(deviceRefNode, BAD_CAST OBIX_ATTR_HREF, deviceHref) == NULL ||
		xmlSetProp(deviceRefNode, BAD_CAST OBIX_ATTR_NAME, deviceName) == NULL ||
		(deviceDisplayName != NULL &&
		 xmlSetProp(deviceRefNode, BAD_CAST OBIX_ATTR_DISPLAY_NAME,
					deviceDisplayName) == NULL) ||
		(deviceDisplay != NULL &&
		 xmlSetProp(deviceRefNode, BAD_CAST OBIX_ATTR_DISPLAY,
					deviceDisplay) == NULL) ||
		(deviceIs != NULL &&
		 xmlSetProp(deviceRefNode, BAD_CAST OBIX_ATTR_IS,
					deviceIs) == NULL)) {
		log_error("Failed to set attributes on the device reference node");
		xmlFreeNode(deviceRefNode);
		deviceRefNode = NULL;
		goto new_node_failed;
	}

	if (xmldb_add_child(deviceTable, deviceRefNode, 0, 0) == NULL) {
		log_error("Failed to add the device reference to the XML database");
		xmlFreeNode(deviceRefNode);
		deviceRefNode = NULL;
	}

	/*
	 * According to oBIX spec 1.1 11.4 section, watches should exclude
	 * refs or feeds. Therefore not notify any watches that may have
	 * been incorrectly installed for device lobby
	 */

	/* Fall through */

new_node_failed:
	if (deviceIs) {
		xmlFree(deviceIs);
	}

	if (deviceDisplayName) {
		xmlFree(deviceDisplayName);
	}

	if (deviceDisplay) {
		xmlFree(deviceDisplay);
	}

	xmlFree(deviceName);

failed:
	xmlFree(deviceHref);

	return deviceRefNode;
}

static void list_href_dispose(list_href_t *item)
{
	if (item->href) {
		xmlFree(item->href);
	}

	free(item);
}

int xmldb_node_path_helper(xmlNode **current, void *arg1, void *arg2)
{
	xmlNode *node = *current;
	list_href_t *head = (list_href_t *)arg1;	/* arg2 is ignored */
	list_href_t *item;
	xmlChar *href;

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
xmlChar *xmldb_node_path(xmlNode *node)
{
	list_href_t head, *item, *n;
	char *href = NULL;

	INIT_LIST_HEAD(&head.list);

	if (!(head.href = xmlStrdup(BAD_CAST "/"))) {
		return NULL;
	}

	if (xml_for_each_ancestor_or_self(node, xmldb_node_path_helper,
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

/**
 * Iterates through the request environment pointed to by @a response and inserts
 * all the request environment variables as child nodes to the environment root node
 * pointed to by @a environmentRoot.
 *
 * @param response          A pointer to the @a Response object that holds request
 *                          information
 * @returns                 A pointer to the newly created obix:List XML subtree
 *                          with all the FastCGI variables listed as <str>'s in it,
 *                          or NULL if an error occured.
 * @remark                  This is an allocating function.  It's up to the caller
 *                          to free the memory returned from this function with xmlFree().
 */
static xmlNode *xmldb_fcgi_var_list(response_t *response)
{
	char **envp;
	xmlNode *envList = NULL;
	xmlNode *curEnvNode = NULL;

	assert(response && response->request);

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

	for (envp = response->request->envp; *envp != NULL; ++envp) {
		if (!(curEnvNode = xmlNewDocNode(_storage, NULL, BAD_CAST OBIX_OBJ_STR, NULL))) {
			log_error("Failed to allocate the oBIX:str value for FCGI varabile");
			break;
		}

		if (xmlSetProp(curEnvNode, BAD_CAST OBIX_ATTR_VAL, BAD_CAST *envp) == NULL) {
			log_error("Failed to set the \"val\" attribute");
			break;
		}

		if (xmldb_add_child(envList, curEnvNode, 0, 0) == NULL) {
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

#ifdef DEBUG
xmlNode *xmldb_dump(response_t *response)
{
	xmlNode *obixDump = NULL;
	xmlNode *fcgiVarList = NULL;
	xmlNode *storageCopy = NULL;

	assert(response);

	if (!(obixDump = xmlNewNode(NULL, BAD_CAST OBIX_OBJ))) {
		log_error("Failed to allocate an XML node for the response");
		goto failed;
	}

	if (xmlSetProp(obixDump, BAD_CAST OBIX_ATTR_IS,
				   BAD_CAST "obix:EnvironmentDump") == NULL) {
		log_error("Failed to set the \"is\" attribute on the root element");
		goto failed;
	}

	if (!(fcgiVarList = xmldb_fcgi_var_list(response))) {
		log_error("Failed to return the FASTCGI environment contract");
		goto failed;
	}

	if (!(storageCopy = xml_copy(xmlDocGetRootElement(_storage),
								 XML_COPY_EXCLUDE_COMMENTS |
								 XML_COPY_EXCLUDE_HIDDEN))) {
		log_error("Failed to copy the XML storage");
		goto storage_failed;
	}

	if (xmldb_add_child(obixDump, fcgiVarList, 0, 0) == NULL ||
		xmldb_add_child(obixDump, storageCopy, 0, 0) == NULL) {
		log_error("Failed to add children to the output element");
	} else {
		return obixDump;	/* Success */
	}

	/* Failure */

	xmlFreeNode(storageCopy);

storage_failed:
	xmlFreeNode(fcgiVarList);

failed:
	if (obixDump) {
		xmlFreeNode(obixDump);
	}

	return xmldb_fatal_error();
}

char *xmlDebugDumpNode(const xmlNode *node)
{
	return (node != NULL) ? xmldb_dump_node(node) : NULL;
}
#endif

static int xmldb_get_node_helper(const char *token, void *arg1, void *arg2)
{
	xmlNode **currentContextP = (xmlNode **)arg1;		/* arg2 is ignored */

	if (*currentContextP == NULL ||
		(*currentContextP = xml_find_child(*currentContextP, NULL,
										   OBIX_ATTR_HREF, token)) == NULL) {
		return -1;
	}

	return 0;
}

xmlNode *xmldb_get_node(const xmlChar *href)
{
	xmlNode *node;

	if ((node = xmldb_search_cache(href)) != NULL) {
		return node;	/* Cache hit */
	}

	/* Otherwise fall back on normal search method */

	node = xmlDocGetRootElement(_storage);

	if (xmlStrcmp(href, BAD_CAST "/") == 0) {
		goto out;
	}

	if (for_each_str_token(STR_DELIMITER_SLASH, (const char *)href,
						   xmldb_get_node_helper, &node, NULL) < 0) {
		node = NULL;
	}

out:
	if (node != NULL) {
		xmldb_update_cache(node, href);
	}

	return node;
}

/*
 * Copy the implementation of this function since
 * it does not exist on libxml2-2.7
 */
static char *xmldb_buffer_detach(xmlBuffer *buf)
{
	xmlChar *ret;

	if (!buf) {
		return NULL;
	}

	if (buf->alloc == XML_BUFFER_ALLOC_IMMUTABLE) {
		return NULL;
	}

	ret = buf->content;
	buf->content = NULL;
	buf->size = buf->use = 0;

	return (char *)ret;
}

char *xmldb_dump_node(const xmlNode *node)
{
	xmlBuffer *buffer = NULL;
	char *data;

	assert(node);

	if (!(buffer = xmlBufferCreate())) {
		return NULL;
	}

	/*
	 * level == 0, since it is undesirable to add extra indenting
	 * for the closing </obj> for each record;
	 *
	 * format == 1, so that "\r\n" will be applied after each
	 * child of the current record
	 */
	data = xmlNodeDump(buffer, NULL, (xmlNode *)node, 0, 1) > 0 ?
					   xmldb_buffer_detach(buffer) : NULL;

	xmlBufferFree(buffer);

	return data;
}

static int xmldb_create_ancestors_helper(const char *token, void *arg1,
										 void *arg2)
{
	xmlNode *parent = *(xmlNode **)arg1;	/* arg2 is ignored */
	xmlNode *node;

	if (!(node = xml_find_child(parent, NULL, OBIX_ATTR_HREF, token))) {
		if (!(node = xmlNewNode(NULL, BAD_CAST OBIX_OBJ))) {
			return -1;
		}

		if (xmlSetProp(node, BAD_CAST OBIX_ATTR_HREF, BAD_CAST token) == NULL ||
			xmldb_add_child(parent, node, 0,0) == NULL) {
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
 * 1. Practice caution when calling this function since mis-use
 * will easily lead to a mess in the global DOM tree. Therefore
 * this fucntion should not be used by signUp handler
 */
static xmlNode *xmldb_create_ancestors(const xmlChar *href)
{
	xmlNode *parent = xmlDocGetRootElement(_storage);

	if (for_each_str_token(STR_DELIMITER_SLASH, (const char *)href,
						   xmldb_create_ancestors_helper, &parent, NULL) < 0) {
		parent = NULL;
	}

	return parent;
}

/**
 * Adds provided node to the XML storage by behaving in the
 * specified manner.
 *
 * Note,
 * 1. This function will set the given node's href relative
 * therefore it should be called only *after* a reference node
 * has been created for it properly which requires an absolute
 * href.
 *
 * 2. Be cautious to set check_existing 0 as this will bypass
 * the sanity check. NEVER use 0 in signUp handler since contracts
 * provided by clients are not trustworthy.
 *
 * @return @a 0 on success; error code otherwise.
 */
xmldb_errcode_t xmldb_put_node(xmlNode *node, xmldb_dom_action_t action)
{
	xmlNode *parentNode = NULL;
	xmlChar *href = NULL;
	xmlChar *parentHref = NULL;
	int ret;

	assert(node);

	if (!(href = xmlGetProp(node, BAD_CAST OBIX_ATTR_HREF))) {
		return ERR_PUT_NODE_NO_HREF;
	}

	if (!(parentHref = xmlStrdup(href)) ||
		!(parentHref = (xmlChar *)dirname((char *)parentHref))) {
		ret = ERR_PUT_NODE_NO_PARENT_URI;
		goto failed;
	}

	if (!(parentNode = xmldb_get_node(parentHref))) {
		if ((action & DOM_CREATE_ANCESTORS) == 0) {
			ret = ERR_PUT_NODE_NO_PARENT_OBJ;
			goto failed;
		} else {
			if (!(parentNode = xmldb_create_ancestors(parentHref))) {
				ret = ERR_PUT_NODE_NO_PARENT_OBJ;
				goto failed;
			}
		}
	}

	if ((action & DOM_CHECK_SANITY) > 0 &&
		xml_find_child(parentNode, (const char *)node->name,
					   OBIX_ATTR_HREF, basename((char *)href)) != NULL) {
		ret = ERR_PUT_NODE_EXIST;
		goto failed;
	}

	if (xmldb_add_child(parentNode, node, 1, 1) == NULL) {
		ret = ERR_PUT_NODE_ADD_FAILED;
		goto failed;
	}

	if ((action & DOM_NOTIFY_WATCHES) > 0) {
		xmldb_notify_watches(parentNode);
	}

	ret = 0;

	/* Fall through */

failed:
	if (parentHref) {
		xmlFree(parentHref);
	}

	xmlFree(href);
	return ret;
}

/**
 * Reparent children of the "from" node to the "to" node.
 * However, if the "null" attribute is set then relevant
 * node is deleted.
 *
 * Return >= 0 on success, < 0 otherwise
 *
 * TODO:
 * 1. By far only implement the deletion of reference nodes
 * so that their href values could be changed on the fly.
 *
 * However, deletion on normal nodes definitely requires more
 * work, for example, maintain consistency in the watch subsystem
 * since there could be watch metas installed in the deleted
 * node. In this case, relevant watch items should be deleted
 * and poll tasks activated at least.
 */
static int xmldb_reparent_children(xmlNode *from, xmlNode *to)
{
	xmlNode *child, *sibling, *node;
	xmlChar *null, *name, *href;
	int count = 0;

	if (!from) {
		return 0;
	}

	/*
	 * An extra sibling pointer is a must-have since child->next
	 * will be changed once re-parented!
	 */
	for (child = from->children, sibling = ((child) ? child->next : NULL);
		 child;
		 child = sibling, sibling = ((child) ? child->next : NULL)) {
		/* Only interested in normal nodes */
		if (child->type != XML_ELEMENT_NODE) {
			continue;
		}

		/* Delete an existing reference node */
		if ((null = xmlGetProp(child, BAD_CAST OBIX_ATTR_NULL)) != NULL) {
			if (xmlStrcmp(null, BAD_CAST XML_TRUE) != 0) {
				xmlFree(null);
				continue;
			}
			xmlFree(null);

			if (!(name = xmlGetProp(child, BAD_CAST OBIX_ATTR_NAME))) {
				continue;
			}

			/*
			 * So far confine deletion only on reference node.
			 *
			 * Since reference nodes can't be addressed by their hrefs
			 * which point to other nodes, the name attribute is used
			 * to find a matching children.
			 */
			node = xml_find_child(to, OBIX_OBJ_REF, OBIX_ATTR_NAME,
								  (const char *)name);
			xmlFree(name);

			if (node != NULL) {
				xmldb_delete_node(node, 0);
				count++;
			}

			continue;
		}

		if (!(href = xmlGetProp(child, BAD_CAST OBIX_ATTR_HREF))) {
			continue;
		}

		node = xml_find_child(to, (const char *)child->name,
							  OBIX_ATTR_HREF, (const char *)href);
		xmlFree(href);

		/* Skip the current child if it is there already */
		if (node != NULL) {
			continue;
		}

		/*
		 * Detach the current children from the from node so that
		 * deleting the input document that contains the from node
		 * won't impact its descendants that have been reparented
		 * to the global DOM tree.
		 *
		 * Also, set any descendants' href relative so as to ensure
		 * href consisitency in the global DOM tree.
		 */
		if (xmldb_add_child(to, child, 1, 1) == NULL) {
			xmlFreeNode(child);
			return -1;
		}

		count++;
	}

	return count;
}

static int xmldb_load_files_helper(const char *dir, const char *file)
{
	xmlDoc *doc = NULL;
	xmlNode *root = NULL;
	xmlNode *duplicated = NULL;
	xmlChar *href = NULL;
	char *path;
	int ret = -1;

	assert(file);

	if (link_pathname(&path, dir, NULL, file, NULL) < 0) {
		log_error("Failed to assemble absolute path name for %s/%s", dir, file);
		return -1;
	}

	if (!(doc = xmlParseFile(path))) {
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

	/**
	 * If the root node of the current XML document already exists
	 * in the DOM tree and is not a refernece, re-parent all its
	 * children to that node.
	 *
	 * Otherwise, add the whole root subtree under its parent,
	 * which will reset all descendants' doc pointer pointing
	 * to the owner document of the parent node.
	 */
	if ((duplicated = xmldb_get_node(href)) != NULL &&
		(xmlStrcmp(duplicated->name, BAD_CAST OBIX_OBJ_REF)) != 0) {
		if (xmldb_reparent_children(root, duplicated) < 0) {
			log_error("Failed to re-parent children of the root node "
					  "loaded from %s into existing node", href);
			goto failed;
		}
	} else {
		if (xmldb_put_node(root, 0) != 0) {
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
 * Loads all static XML setting files for oBIX server in different
 * sub-folders following below sequence:
 *
 * core -> sys -> devices
 *
 * It doesn't matter in what order files in one sub-folder are loaded
 */
static int xmldb_load_files(const xml_config_t *context)
{
	char *dir;

	assert(context);

	if (link_pathname(&dir, context->resourcePath, NULL,
					  SERVER_DB_DIR_CORE, NULL) < 0) {
		log_error("Failed to assemble absolute pathname for %s", SERVER_DB_DIR_CORE);
		return -1;
	}

	if (for_each_file_name(dir, SERVER_DB_FILE_PREFIX, SERVER_DB_FILE_SUFFIX,
						   xmldb_load_files_helper) < 0) {
		log_error("Failed to load XML files under %s", dir);
		free(dir);
		return -1;
	}
	free(dir);

	if (link_pathname(&dir, context->resourcePath, NULL,
					  SERVER_DB_DIR_SYS, NULL) < 0) {
		log_error("Failed to assemble absolute pathname for %s", SERVER_DB_DIR_SYS);
		return -1;
	}

	if (for_each_file_name(dir, SERVER_DB_FILE_PREFIX, SERVER_DB_FILE_SUFFIX,
						   xmldb_load_files_helper) < 0) {
		log_error("Failed to load XML DB files under %s", dir);
		free(dir);
		return -1;
	}
	free(dir);

	if (link_pathname(&dir, context->resourcePath, NULL,
					  SERVER_DB_DIR_DEVICES, NULL) < 0) {
		log_error("Failed to assemble absolute pathname for %s", SERVER_DB_DIR_DEVICES);
		return -1;
	}

	if (for_each_file_name(dir, SERVER_DB_FILE_PREFIX, SERVER_DB_FILE_SUFFIX,
						   xmldb_load_files_helper) < 0) {
		log_error("Failed to load XML DB files under %s", dir);
		free(dir);
		return -1;
	}
	free(dir);

	return 0;
}

int xmldb_init(const xml_config_t *context)
{
	xmlNode *newRootNode = NULL;

	if (_storage != NULL) {
		return 0;
	}

	if (xmldb_init_cache() < 0) {
		log_error("Failed to initialize XML Cache");
		return -1;
	}

	if (!(_storage = xmlNewDoc(BAD_CAST "1.0"))) {
		log_error("Unable to initialize the storage.");
		return -1;
	}

	if (!(newRootNode = xmlNewDocNode(_storage, NULL, BAD_CAST OBIX_OBJ, NULL))) {
		log_error("Failed to allocate a new root node for the XML database");
		goto failed;
	}

	if (xmlSetProp(newRootNode, BAD_CAST OBIX_ATTR_HREF,
				   BAD_CAST "/") == NULL) {
		log_error("Failed to set @href on XML storage root node.");
		goto root_node_failed;
	}

	xmlDocSetRootElement(_storage, newRootNode);

	if (xmldb_load_files(context) < 0) {
		log_error("Failed to load XML files in %s", context->resourcePath);
		goto failed;
	}

	if (!(__xmldb_fatal_error = xmldb_copy_sys(OBIX_SYS_FATAL_ERROR_STUB))) {
		log_error("Failed to allocate the fatel Error contract.");
		goto failed;
	}

	log_debug("XML database is initialized.");

	return 0;

root_node_failed:
	xmlFreeNode(newRootNode);

failed:
	xmldb_dispose();
	return -1;
}

/*
 * Dispose the whole XML database on oBIX server.
 *
 * Note,
 * 1. All global pointers would have to be nullified because
 * this cleanup function is likely to be invoked again somewhere
 * in when bringing down oBIX server due to errors
 */
void xmldb_dispose()
{
	if (__xmldb_fatal_error) {
		xmlFreeNode(__xmldb_fatal_error);
		__xmldb_fatal_error = NULL;
	}

	if (_storage != NULL) {
		xmlFreeDoc(_storage);
		_storage = NULL;
	}

	if (_xmldb_cache != NULL) {
		xmldb_dispose_cache();
		_xmldb_cache = NULL;
	}

	log_debug("XML Database is disposed");
}

static int xmldb_node_writable_helper(xmlNode **current, void *arg1, void *arg2)
{
	xmlNode *node = *current;
	xmlChar *writable;
	int *depth = (int *)arg2;

	if (node->type != XML_ELEMENT_NODE) {
		return 0;
	}

	if (*depth != SEARCH_WRITABLE_INDEFINITE) {
		*depth -= 1;

		/*
		 * Bail out if the maximal search threshold
		 * has been consumed
		 */
		if (*depth < 0) {
			return -1;
		}
	}

	/*
	 * Keep on iterating all ancestors if the current node has no
	 * writable attribute
	 */
	if (!(writable = xmlGetProp(node, BAD_CAST OBIX_ATTR_WRITABLE))) {
		return 0;
	}

	if (xmlStrcmp(writable, BAD_CAST XML_TRUE) == 0) {
		*(int *)arg1 = 1;
	} else {
		*(int *)arg1 = 0;
	}

	xmlFree(writable);

	/* Return < 0 so as to stop the caller's loop */
	return -1;
}

/*
 * Check if the given node is writable, which not necessarily
 * means the node must have a 'writable="true"' attribute since
 * one of its ancestors may have such attribute established.
 *
 * The maximal search depth from the given node all the way to
 * the topest level of global DOM tree is decided by the second
 * parameter.
 *
 * Return 1 if the given node is writable, 0 non-writable.
 */
static int xmldb_node_writable(xmlNode *node, int max_depth)
{
	int writable = 0;

	/*
	 * Skip checking return value since the helper function will
	 * bail out if the writable attribute has been found on the
	 * nearest ancestor
	 */
	xml_for_each_ancestor_or_self(node, xmldb_node_writable_helper,
								  &writable, &max_depth);

	return (writable == 1) ? 1 : 0;
}

/*
 * Update the val attribute of the destination node by that of the
 * given input node. Also copy any existing children of the input
 * node to the destination node.
 */
xmldb_errcode_t xmldb_update_node(xmlNode *input, const char *href,
								  xmlNode **updatedNode, int *changed)
{
	xmldb_errcode_t result;
	xmlNode *node, *copy;
	xmlChar *newValue, *oldValue;
	int ret;

	*changed = 0;

	if (!(node = xmldb_get_node(BAD_CAST href))) {
		return ERR_UPDATE_NODE_NO_SUCH_URI;
	}

	/*
	 * Allow the writable attribute on a parent node also have
	 * effects on its direct children
	 */
	if (xmldb_node_writable(node, SEARCH_WRITABLE_PARENT) != 1) {
		return ERR_UPDATE_NODE_NOT_WRITABLE;
	}

	/*
	 * If the input node contains a val attribute, then have the target
	 * node updated. Otherwise skip to copy any existing children to the
	 * target node.
	 */
	if ((newValue = xmlGetProp(input, BAD_CAST OBIX_ATTR_VAL)) != NULL) {
		if (xmlStrcmp(node->name, BAD_CAST OBIX_OBJ_BOOL) == 0 &&
			xmlStrcmp(newValue, BAD_CAST XML_TRUE) != 0 &&
			xmlStrcmp(newValue, BAD_CAST XML_FALSE) != 0) {
			result = ERR_UPDATE_NODE_BAD_BOOL;
			goto failed;
		}

		if ((oldValue = xmlGetProp(node, BAD_CAST OBIX_ATTR_VAL)) != NULL) {
			*changed = ((xmlStrcmp(oldValue, newValue) == 0) ? 0 : 1);
			xmlFree(oldValue);
		} else {
			/* Raise the changed flag if "val" attribute is first set */
			*changed = 1;
		}

		if (xmlSetProp(node, BAD_CAST OBIX_ATTR_VAL, newValue) == NULL) {
			result = ERR_UPDATE_NODE_NO_MEM;
			goto failed;
		}
	}

	if (xmlStrcmp(node->name, BAD_CAST OBIX_OBJ_REF) != 0) {
		/*
		 * TODO:
		 * In theroy, input node should be directly passed in as the first
		 * parameter since its children will be unlinked and then reparented
		 * to the target node. However, xmlUnlinkNode is not good enough
		 * to wipe out all connections between the input node and its children,
		 * after the input node is deleted (along with its owner document),
		 * the tag names of its children in the global DOM tree will be
		 * removed as well.
		 *
		 * A workaround is to make a copy of the input node
		 */
		copy = xml_copy(input, XML_COPY_EXCLUDE_COMMENTS);
		ret = xmldb_reparent_children(copy, node);
		xmlFreeNode(copy);	/* Only needed when the input is copied */

		if (ret < 0) {
			result = ERR_UPDATE_NODE_REPARENT;
			goto failed;
		} else if (ret > 0) {
			/* Raise the changed flag if more children are inserted */
			*changed = 1;
		}
	}

	*updatedNode = node;
	result = 0;

	/* Fall through */

failed:
	if (newValue) {
		xmlFree(newValue);
	}

	return result;
}

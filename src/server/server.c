/* *****************************************************************************
 * Copyright (c) 2014 Tyler Watson <tyler.watson@nextdc.com>
 * Copyright (c) 2013-2014 Qingtao Cao [harry.cao@nextdc.com]
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

#include <libxml/xpath.h>
#include <stdlib.h>
#include <string.h>
#include "obix_utils.h"
#include "log_utils.h"
#include "xml_storage.h"
#include "watch.h"
#include "server.h"
#include "xml_utils.h"
#include "obix_request.h"
#include "history.h"
#include "batch.h"

#ifdef DEBUG
/*
 * The special URI to expose all visible(not hidden) objects
 * in oBIX server XML database. For debug purpose only
 */
static const char *OBIX_SRV_DUMP_URI = "/obix-dump/";
#endif

/*
 * Prototype of a POST Handler function.
 */
typedef xmlNode * (*obix_server_postHandler)(obix_request_t *, xmlNode *);

/*
 * NOTE: each handler's index must be the same as specified
 * in relevant <meta op="%d"/> settings in oBIX server's
 * configuration files
 */
static const obix_server_postHandler POST_HANDLER[] = {
	[0] = handlerError,
	[1] = handlerWatchServiceMake,
	[2] = handlerWatchAdd,
	[3] = handlerWatchRemove,
	[4] = handlerWatchPollChanges,
	[5] = handlerWatchPollRefresh,
	[6] = handlerWatchDelete,
	[7] = handlerSignUp,
	[8] = handlerBatch,
	[9] = handlerHistoryGet,
	[10] = handlerHistoryQuery,
	[11] = handlerHistoryAppend
};

/** Amount of available post handlers. */
static const int POST_HANDLERS_COUNT = 12;

enum {
	ERR_NO_INPUT = 1,	/* Leave 0 as success */
	ERR_NO_SUCH_URI,
	ERR_NO_URI_FETCHED,
	ERR_NO_OPERATION,
	ERR_NO_OP_META,
	ERR_NO_OP_HANDLERID,
	ERR_NO_MEM,
	ERR_NO_REF,
	ERR_XMLDB_ERR_OFFSET

	/*
	 * Error codes from xml_storage.h will follow from here
	 */
};

static err_msg_t server_err_msg[] = {
	[ERR_NO_INPUT] = {
		.type = OBIX_CONTRACT_ERR_UNSUPPORTED,
		.msgs = "No input available from oBIX clients"
	},
	[ERR_NO_SUCH_URI] = {
		.type = OBIX_CONTRACT_ERR_BAD_URI,
		.msgs = "Requested URI could not be found on this server"
	},
	[ERR_NO_URI_FETCHED] = {
		.type = OBIX_CONTRACT_ERR_SERVER,
		.msgs = "Failed to retrieve full URI for the requested object "
				"in the XML database"
	},
	[ERR_NO_OPERATION] = {
		.type = OBIX_CONTRACT_ERR_UNSUPPORTED,
		.msgs = "Requested URI is not an operation"
	},
	[ERR_NO_OP_META] = {
		.type = OBIX_CONTRACT_ERR_SERVER,
		.msgs = "Failed to retrieve operation meta from the XML database"
	},
	[ERR_NO_OP_HANDLERID] = {
		.type = OBIX_CONTRACT_ERR_SERVER,
		.msgs = "Failed to retrieve operation handler ID from the XML database"
	},
	[ERR_NO_MEM] = {
		.type = OBIX_CONTRACT_ERR_SERVER,
		.msgs = "Failed to copy a node from the XML database"
	},
	[ERR_NO_REF] = {
		.type = OBIX_CONTRACT_ERR_SERVER,
		.msgs = "Failed to insert a reference node into the XML database"
	},

	[ERR_XMLDB_ERR_OFFSET + ERR_UPDATE_NODE_BAD_BOOL] = {
		.type = OBIX_CONTRACT_ERR_UNSUPPORTED,
		.msgs = "@val on the source input data not a valid boolean"
	},
	[ERR_XMLDB_ERR_OFFSET + ERR_UPDATE_NODE_NO_SUCH_URI] = {
		.type = OBIX_CONTRACT_ERR_BAD_URI,
		.msgs = "The destination object can't be found"
	},
	[ERR_XMLDB_ERR_OFFSET + ERR_UPDATE_NODE_NOT_WRITABLE] = {
		.type = OBIX_CONTRACT_ERR_PERMISSION,
		.msgs = "The destination object or its direct parent is not writable"
	},
	[ERR_XMLDB_ERR_OFFSET + ERR_UPDATE_NODE_NO_MEM] = {
		.type = OBIX_CONTRACT_ERR_SERVER,
		.msgs = "A memory error occurred when updating existing node"
	},
	[ERR_XMLDB_ERR_OFFSET + ERR_UPDATE_NODE_REPARENT] = {
		.type = OBIX_CONTRACT_ERR_SERVER,
		.msgs = "Failed to reparent children of input doc to the XML database"
	},
	[ERR_XMLDB_ERR_OFFSET + ERR_UPDATE_NODE_BAD_INPUT] = {
		.type = OBIX_CONTRACT_ERR_UNSUPPORTED,
		.msgs = "The input root node mis-matches with the target node"
	},
	[ERR_XMLDB_ERR_OFFSET + ERR_PUT_NODE_NO_HREF] = {
		.type = OBIX_CONTRACT_ERR_UNSUPPORTED,
		.msgs = "No href in the provided node"
	},
	[ERR_XMLDB_ERR_OFFSET + ERR_PUT_NODE_NO_PARENT_URI] = {
		.type = OBIX_CONTRACT_ERR_SERVER,
		.msgs = "Failed to get parent node's href"
	},
	[ERR_XMLDB_ERR_OFFSET + ERR_PUT_NODE_NO_PARENT_OBJ] = {
		.type = OBIX_CONTRACT_ERR_UNSUPPORTED,
		.msgs = "Parent object not existing"
	},
	[ERR_XMLDB_ERR_OFFSET + ERR_PUT_NODE_EXIST] = {
		.type = OBIX_CONTRACT_ERR_UNSUPPORTED,
		.msgs = "The to-be-added node already exists in the XML database"
	},
	[ERR_XMLDB_ERR_OFFSET + ERR_PUT_NODE_ADD_FAILED] = {
		.type = OBIX_CONTRACT_ERR_SERVER,
		.msgs = "Failed to add the given node into the XML database"
	}
};

static obix_server_postHandler obix_server_post_handler(const int id)
{
	obix_server_postHandler handler;

	if (id < 0 || id >= POST_HANDLERS_COUNT) {
		handler = POST_HANDLER[0];
	} else {
		handler = POST_HANDLER[id];
	}

	return handler;
}

void obix_server_exit(void)
{
	obix_hist_dispose();
	obix_watch_dispose();
	obix_xmldb_dispose();

	log_debug("oBIX server has been shutdown properly");
}

int obix_server_init(const xml_config_t *config)
{
	int threads;

	if ((threads = xml_config_get_int(config, XP_POLL_THREAD_COUNT)) < 0) {
		log_error("Failed to get %s settings", XP_POLL_THREAD_COUNT);
		return -1;
	}

	/* Initialise the global DOM tree before any other facilities */
	if (obix_xmldb_init(config->resdir) < 0) {
		log_error("Failed to initialise the global XML DOM tree");
		return -1;
	}

	if (obix_watch_init(threads) < 0) {
		log_error("Failed to initialize the watch subsystem");
		goto failed;
	}

	if (obix_hist_init(config->resdir) < 0) {
		log_error("Failed to initialize the history subsystem");
		goto hist_failed;
	}

	return 0;

hist_failed:
	obix_watch_dispose();

failed:
	obix_xmldb_dispose();
	return -1;
}

xmlNode *obix_server_generate_error(const char *href, const char *contract,
									const char *name, const char *desc)
{
	xmlNode *errorNode;

	if (!(errorNode = xmldb_copy_sys(OBIX_SYS_ERROR_STUB))) {
		log_error("Failed to copy from %s", OBIX_SYS_ERROR_STUB);
		return NULL;
	}

	if ((contract != NULL &&
		 xmlSetProp(errorNode, BAD_CAST OBIX_ATTR_IS,
					BAD_CAST contract) == NULL) ||
		(href != NULL &&
		 xmlSetProp(errorNode, BAD_CAST OBIX_ATTR_HREF,
					BAD_CAST href) == NULL) ||
		xmlSetProp(errorNode, BAD_CAST OBIX_ATTR_DISPLAY_NAME,
					BAD_CAST name) == NULL ||
		xmlSetProp(errorNode, BAD_CAST OBIX_ATTR_DISPLAY,
					BAD_CAST desc) == NULL) {
		log_error("Failed to set attributes on the error object");
		xmlFreeNode(errorNode);
		errorNode = NULL;
	}

	return errorNode;
}

xmlNode *obix_server_read(obix_request_t *request, const char *overrideUri)
{
	xmlNode *copy = NULL;
	xmlAttr *hidden = NULL;
	const xmlNode *storageNode = NULL;
	int ret;
	xmlChar *href = NULL;
	const char *uri;

	uri = (overrideUri != NULL) ? overrideUri : request->request_decoded_uri;

	if (!(storageNode = xmldb_get_node(BAD_CAST uri))) {
		ret = ERR_NO_SUCH_URI;
		goto failed;
	}

	/*
	 * URIs provided by clients may have random number of slashes
	 * trailing, that's why it is desirable to have oBIX server
	 * retrieve its full URI once again
	 */
	if (!(href = xmldb_node_path((xmlNode *)storageNode))) {
		ret = ERR_NO_URI_FETCHED;
		goto failed;
	}

	if (!(copy = xml_copy(storageNode,
						  XML_COPY_EXCLUDE_HIDDEN | XML_COPY_EXCLUDE_META)) ||
		!xmlSetProp(copy, BAD_CAST OBIX_ATTR_HREF, href)) {
		ret = ERR_NO_MEM;
		goto failed;
	}

	/*
	 * Remove @a @hidden from any responses to the client, if they
	 * request a hidden node explicitly. BTW, all hidden nodes will
	 * be wiped out from response object before sending it out
	 */
	if ((hidden = xmlHasProp(copy, BAD_CAST OBIX_ATTR_HIDDEN)) != NULL) {
		xmlRemoveProp(hidden);
	}

	xmlFree(href);
	return copy;

failed:
	if (href) {
		xmlFree(href);
	}

	if (copy) {
		xml_delete_node(copy);
	}

	log_error("%s : %s", uri, server_err_msg[ret].msgs);

	return obix_server_generate_error(uri, server_err_msg[ret].type,
									  "oBIX Server", server_err_msg[ret].msgs);
}

void obix_server_handleError(obix_request_t *request, const char *msg)
{
	xmlNode *node;

	/*
	 * NOTE: Doesn't matter if the href argument is NULL, e.g., when
	 * the decoded request URI not initialised yet due to invalid
	 * request URI in the first place
	 */
	node = obix_server_generate_error(request->request_decoded_uri,
									  OBIX_CONTRACT_ERR_UNSUPPORTED,
									  "oBIX Server", msg);

	obix_server_reply_object(request, ((node != NULL) ? node : xmldb_fatal_error()));
}

void obix_server_handleGET(obix_request_t *request)
{
	xmlNode *node = NULL;

#ifdef DEBUG
	if (str_is_identical(request->request_decoded_uri, OBIX_SRV_DUMP_URI) == 0) {
		node = xmldb_dump(request);
	} else {
		node = obix_server_read(request, NULL);
	}
#else
	node = obix_server_read(request, NULL);
#endif

	obix_server_reply_object(request, ((node != NULL) ? node : xmldb_fatal_error()));
}

/**
 * Update the destination node if it is writable in the following
 * aspects:
 *  . Delete it if null="true" is set in the request;
 *  . Update its val attribute if provided;
 *  . Install new nodes as its direct children if provided;
 *  . Remove its direct children if null="true" is set in the request
 *    for such nodes.
 *
 * However, a write request won't be able to remove a device
 * contract and the signOff request should be used instead.
 */
xmlNode *obix_server_write(obix_request_t *request, const char *overrideUri,
						   xmlNode *input)
{
	xmlNode *updatedNode = NULL;
	xmlNode *nodeCopy = NULL;
	const char *uri;
	xmlChar *href = NULL;
	int ret = 0;

	uri = (overrideUri != NULL) ? overrideUri : request->request_decoded_uri;

	if (!input) {
		ret = ERR_NO_INPUT;
		goto failed;
	}

	if ((ret = xmldb_update_node(input, uri, &updatedNode)) > 0) {
		ret += ERR_XMLDB_ERR_OFFSET;
		goto failed;
	}

	if (!(href = xmldb_node_path(updatedNode))) {
		ret = ERR_NO_URI_FETCHED;
		goto failed;
	}

	if (!(nodeCopy = xmldb_copy_node(updatedNode,
							XML_COPY_EXCLUDE_META | XML_COPY_EXCLUDE_HIDDEN)) ||
		!xmlSetProp(nodeCopy, BAD_CAST OBIX_ATTR_HREF, href)) {
		ret = ERR_NO_MEM;
		goto failed;
	}

	xmlFree(href);
	return nodeCopy;

failed:
	if (href) {
		xmlFree(href);
	}

	if (nodeCopy) {
		xml_delete_node(nodeCopy);
	}

	log_error("%s : %s", uri, server_err_msg[ret].msgs);

	return obix_server_generate_error(uri, server_err_msg[ret].type,
									  "obix:Write", server_err_msg[ret].msgs);
}

void obix_server_handlePUT(obix_request_t *request, const xmlDoc *input)
{
	xmlNode *node = NULL;

	if (input != NULL) {
		node = obix_server_write(request, NULL,
								 xmlDocGetRootElement((xmlDoc *)input));
	} else {
		node = obix_server_generate_error(request->request_decoded_uri, NULL,
						  "Unknown request format",
						  "The server could not understand the PUT request.");
	}

	obix_server_reply_object(request, ((node != NULL) ? node : xmldb_fatal_error()));

}

xmlNode *obix_server_invoke(obix_request_t *request, const char *overrideUri,
							xmlNode *input)
{
	const xmlNode *node;
	const char *uri;
	xmlNode *meta;
	long handlerId = 0;
	int ret;

	uri = (overrideUri != NULL) ? overrideUri : request->request_decoded_uri;

	if (!(node = xmldb_get_node(BAD_CAST uri))) {
		ret = ERR_NO_SUCH_URI;
		goto failed;
	}

	if (xmlStrcmp(node->name, BAD_CAST OBIX_OBJ_OP) != 0) {
		ret = ERR_NO_OPERATION;
		goto failed;
	}

	if (!(meta = xml_find_child(node, OBIX_OBJ_META,
								OBIX_META_ATTR_OP, NULL))) {
		ret = ERR_NO_OP_META;
		goto failed;
	}

	if ((handlerId = xml_get_long(meta, OBIX_META_ATTR_OP)) < 0) {
		ret = ERR_NO_OP_HANDLERID;
		goto failed;
	}

	return obix_server_post_handler(handlerId)(request, input);

failed:
	log_error("%s : %s", uri, server_err_msg[ret].msgs);

	return obix_server_generate_error(uri, server_err_msg[ret].type,
									  "oBIX Server", server_err_msg[ret].msgs);
}

void obix_server_handlePOST(obix_request_t *request, const xmlDoc *input)
{
	xmlNode *node;

	node = obix_server_invoke(request, NULL,
				  ((input != NULL) ? xmlDocGetRootElement((xmlDoc *)input) : NULL));

	/*
	 * If the current request is a long poll request, the long poll
	 * thread will process and release it in an asynchronous manner
	 */
	if (request->no_reply == 1) {
		return;
	}

	/*
	 * If the current request is a history request, relevant methods
	 * will take care of sending the responses by themselves but wait
	 * until here to have POST handler release them properly
	 */
	if (request->is_history == 1) {
		obix_request_destroy(request);
		request = NULL;
		return;
	}

	obix_server_reply_object(request, ((node != NULL) ? node : xmldb_fatal_error()));
}

/**
 * Send through FCGI channel the oBIX object generated by relevant
 * oBIX server handler as response to oBIX client.
 *
 * Note,
 * 1. In the end, before this function returns, [response, request]
 * pair and the oBIX object will ALL be released, regardless of
 * whether the response has been sent out or not.
 */
void obix_server_reply_object(obix_request_t *request, xmlNode *node)
{
	xmlDoc *doc = NULL;
	xmlChar *mem;
	int size = 0;

	/*
	 * Due to the fact that glibc free() won't nullify the released memory
	 * region(for sake of performance) it is insufficient to only check
	 * the response pointer, since it may point to somewhere already freed.
	 * Its request pointer should be checked as well, which will have been
	 * nullified deliberately in obix_response_destroy
	 */
	if (!request || !request->request) {
		log_warning("Nasty things happen! response has been freed!");
		return;
	}

	if (!node) {
		/*
		 * In extreme case xmldb_fatal_error contract has been consumed
		 * and released, no object generated by oBIX server to send back
		 * as response. In such case simply release the request and
		 * response pair
		 */
		log_warning("Even xmldb_fatal_error contract has been consumed! "
					"Too little memory for oBIX server to continue");
		obix_request_destroy(request);
		return;
	}

	if (!(doc = xmlNewDoc(BAD_CAST XML_VERSION))) {
		log_error("Could not generate obix document for reply.");
		goto failed;
	}

	/*
	 * Reparent the answer node to the newly created temp document
	 * to generate a response. To this end, the original XML parser
	 * dictionary should be referenced so that the release of
	 * the temp document won't touch the dictionary at all
	 */
	if (node->doc) {
		doc->dict = node->doc->dict;
		if (doc->dict) {
			xmlDictReference(doc->dict);
		}
	}

	xmlDocSetRootElement(doc, node);

#ifdef DEBUG
	xmlDocDumpFormatMemory(doc, &mem, &size, 1);
#else
	xmlDocDumpFormatMemory(doc, &mem, &size, 0);
#endif

	/*
	 * Create a response item to carry the result and send it back
	 * to oBIX client. Then the response and relevant FCGI request
	 * would be released in the end.
	 *
	 * If failed to create an item, destroy them right away
	 */
	if (obix_request_create_append_response_item(request, (char *)mem, size, 0) < 0) {
		log_error("Failed to create a response item");
		free(mem);
		obix_request_destroy(request);
	} else {
		obix_request_send_response(request);
		obix_request_destroy(request);
	}

	xmlFreeDoc(doc);
	return;

failed:
	/*
	 * The provided oBIX contract needs to be released before
	 * this function exits, even if it fails.
	 */
	xmlFreeNode(node);

	obix_request_destroy(request);
}

/**
 * Default handler, which sends error message telling that this operation
 * is not supported.
 *
 * @see obix_server_postHandler
 */
xmlNode *handlerError(obix_request_t *request, xmlNode *input)
{
	log_error("Requested operation \"%s\" not implemented.",
			  request->request_decoded_uri);

	return obix_server_generate_error(request->request_decoded_uri,
						  OBIX_CONTRACT_ERR_UNSUPPORTED,
						  "Unsupported Request",
						  "The requested operation is not yet implemented.");
}

/**
 * Handles signUp operation. Adds new device data to the server.
 *
 * @see obix_server_postHandler
 */
xmlNode *handlerSignUp(obix_request_t *request, xmlNode *input)
{
	xmlNode *inputCopy, *ref, *node, *pos;
	xmlChar *href;
	int ret, existed = 0;
	xmldb_dom_action_t action;

	if (!input) {
		ret = ERR_NO_INPUT;
		goto failed;
	}

	if (!(ref = xmldb_create_ref(OBIX_DEVICE_LOBBY_URI, input, &existed))) {
		ret = ERR_NO_REF;
		goto failed;
	}

	if (existed == 1) {
		/*
		 * Return success when the device already registered to
		 * enable a re-started client handle the signUp gracefully.
		 *
		 * TODO:
		 * However, such practice will have a side-effect that
		 * the existing device may be altered and thus different
		 * than what has been provided.
		 */
		goto out;
	}

	if (!(inputCopy = xml_copy(input, XML_COPY_EXCLUDE_COMMENTS))) {
		ret = ERR_NO_MEM;
		goto copy_failed;
	}

	/*
	 * Remove the "writable" attribute so that a device contract
	 * cannot be deleted through a normal write request, but via
	 * the signOff request
	 */
	xmlUnsetProp(inputCopy, BAD_CAST OBIX_ATTR_WRITABLE);

	/*
	 * Always enforce sanity checks for all contracts registered regardless
	 * of from clients or privileged adapters. However, create any missing
	 * ancestors if they are from privileged adapters.
	 */
	action = DOM_NOTIFY_WATCHES | DOM_CHECK_SANITY;
	if (is_privileged_mode(request) == 1) {
		action |= DOM_CREATE_ANCESTORS;
	}

	if ((ret = xmldb_put_node(inputCopy, action)) != 0) {
		ret += ERR_XMLDB_ERR_OFFSET;
		goto put_failed;
	}

out:
	/*
	 * The input document have had any absolute href set to relative
	 * before inserted to global DOM tree, therefore it's desirable to
	 * have the response object reflect this. However, the href of the
	 * root node must remain absolute so as to let clients side know
	 * where the registered devices are. To this end, each direct
	 * children of the copied node would need to be traversed.
	 */
	for (pos = input->children; pos; pos = pos->next) {
		if (pos->type != XML_ELEMENT_NODE) {
			continue;
		}

		xmldb_set_relative_href(pos);
	}

	xmlUnsetProp(input, BAD_CAST OBIX_ATTR_WRITABLE);
	return input;

put_failed:
	xmlFreeNode(inputCopy);

copy_failed:
	xmldb_delete_node(ref, 0);

failed:
	href = xmlGetProp(input, BAD_CAST OBIX_ATTR_HREF);

	log_error("SignUp %s : %s", ((href) ? (char *)href :
					"(No Href in Device Contract)"), server_err_msg[ret].msgs);

	node = obix_server_generate_error(request->request_decoded_uri,
									  server_err_msg[ret].type,
									  "SignUp", server_err_msg[ret].msgs);

	if (href) {
		xmlFree(href);
	}

	return node;
}

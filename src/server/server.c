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
 * along with oBIX.  If not, see <http://www.gnu.org/licenses/>.
 *
 * *****************************************************************************/

#include <libxml/xpath.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "obix_utils.h"
#include "libxml_config.h"
#include "log_utils.h"
#include "xml_storage.h"
#include "post_handler.h"
#include "watch.h"
#include "server.h"
#include "xml_utils.h"
#include "obix_request.h"
#include "history.h"

#ifdef DEBUG
/*
 * The special URI to expose all visible(not hidden) objects
 * in oBIX server XML database. For debug purpose only
 */
static const char *OBIX_SRV_DUMP_URI = "/obix-dump/";
#endif

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

int obix_server_init(xml_config_t *context)
{
	if (xmldb_init(context) < 0) {
		log_error("Unable to start the server");
		return -1;
	}

	if (obix_watch_init(context) < 0) {
		log_error("Failed to initialize the watch subsystem");
		return -1;
	}

	if (obix_hist_init(context->resourcePath) < 0) {
		log_error("Failed to initialize the history subsystem");
		return -1;
	}

	return 0;
}

xmlNode *obix_server_generate_error(const char *href, const char *contract,
									const char *name, const char *desc)
{
	xmlNode *errorNode;

	assert(name && desc);	/* while href and contract are optional */

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
	xmlChar *obixUri;
	const char *uri;

	assert(request);

	uri = (overrideUri != NULL) ? overrideUri : request->request_decoded_uri;

	if (!(storageNode = xmldb_get_node(BAD_CAST uri))) {
		ret = ERR_NO_SUCH_URI;
		goto failed;
	}

	if ((copy = xml_copy(storageNode, XML_COPY_EXCLUDE_HIDDEN |
									  XML_COPY_EXCLUDE_META)) == NULL) {
		ret = ERR_NO_MEM;
		goto failed;
	}

	/*
	 * URIs provided by clients may have random number of slashes
	 * trailing, that's why it is desirable to have oBIX server
	 * retrieve its full URI once again
	 */
	if (!(obixUri = xmldb_node_path((xmlNode *)storageNode))) {
		ret = ERR_NO_URI_FETCHED;
		goto failed;
	}

	if (xmlSetProp(copy, BAD_CAST OBIX_ATTR_HREF, obixUri) == NULL) {
		log_warning("Failed to set absolute URI on the response node");
	}

	/*
	 * Remove @a @hidden from any responses to the client, if they
	 * request a hidden node explicitly. BTW, all hidden nodes will
	 * be wiped out from response object before sending it out
	 */
	if ((hidden = xmlHasProp(copy, BAD_CAST OBIX_ATTR_HIDDEN)) != NULL) {
		xmlRemoveProp(hidden);
	}

	xmlFree(obixUri);
	return copy;

failed:
	log_error("%s", server_err_msg[ret].msgs);

	return obix_server_generate_error(uri, server_err_msg[ret].type,
				"oBIX Server", server_err_msg[ret].msgs);
}

void obix_server_handleError(obix_request_t *request, const char *msg)
{
	xmlNode *node;

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

	if (!(nodeCopy = xmldb_copy_node(updatedNode,
							XML_COPY_EXCLUDE_META | XML_COPY_EXCLUDE_HIDDEN))) {
		ret = ERR_NO_MEM;
		goto failed;
	}

	return nodeCopy;

failed:
	log_error("Failed to update the XML database: %s",
				server_err_msg[ret].msgs);

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

	return (*obix_server_getPostHandler(handlerId))(request, input);

failed:
	log_error("%s", server_err_msg[ret].msgs);

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

void obix_server_shutdown()
{
	obix_hist_dispose();

	obix_watch_dispose();

	xmldb_dispose();

	log_debug("oBIX server has been shutdown");
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
void obix_server_reply_object(obix_request_t *request, xmlNode *obixObject)
{
	xmlDoc *doc = NULL;
	xmlChar *mem;
	int size = 0;

	/*
	 * Due to the fact that glibc free() won't nullify the released memory
	 * region(for sake of performance) it is insufficient to only check
	 * the response pointer, since it may point to somewhere already freed.
	 * Its request pointer should be checked as well, which whill have been
	 * nullified deliberately in obix_response_destroy
	 */
	if (!request || !request->request) {
		log_warning("Nasty things happen! response has been freed!");
		return;
	}

	if (!obixObject) {
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
	 * xmlDocSetRootElement will unlink the node from its original
	 * parent document and then have all its descendants' doc pointer
	 * pointing to the new document
	 */
	xmlDocSetRootElement(doc, obixObject);

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

	/*
	 * Release the document containing the provided object
	 * and its namespace
	 */
	xmlFreeDoc(doc);

	return;

failed:
	/*
	 * The provided oBIX contract needs to be released before
	 * this function exits, even if it fails.
	 *
	 * Note,
	 * 1. This will also have associated namespace released
	 * properly.
	 */
	xmlFreeNode(obixObject);

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
	log_debug("Requested operation \"%s\" exists but not implemented.", request->request_decoded_uri);

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

	/*
	 * TODO:
	 * In theory the input node should be directly returned so as to
	 * be used as part of response. However, perhaps due to an error
	 * in obix_fcgi_read that generate a not well-formed XML document,
	 * releasing the input root along with a temporary document by
	 * xmlFreeDoc will segfault by double-free
	 *
	 * So a viable workaround is to copy the input node to be send
	 * as response, which may incur some performance penalty but won't
	 * bring about memory leaks nor segfaults.
	 */
out:
	if ((node = xmlCopyNode(input, 1)) != NULL) {
		/*
		 * The input document have had any absolute href set to relative
		 * before inserted to global DOM tree, therefore it's desirable to
		 * have the response object reflect this. However, the href of the
		 * root node must remain absolute so as to let clients side know
		 * where the registered devices are. To this end, each direct
		 * children of the copied node would need to be traversed.
		 */
		for (pos = node->children; pos; pos = pos->next) {
			if (pos->type != XML_ELEMENT_NODE) {
				continue;
			}

			xmldb_set_relative_href(pos);
		}

		xmlUnsetProp(node, BAD_CAST OBIX_ATTR_WRITABLE);
	}

	return node;

put_failed:
	xmlFreeNode(inputCopy);

copy_failed:
	xmldb_delete_node(ref, 0);

failed:
	log_error("%s", server_err_msg[ret].msgs);

	href = xmlGetProp(input, BAD_CAST OBIX_ATTR_HREF);

	node = obix_server_generate_error((href != NULL) ? (const char *)href : request->request_decoded_uri,
									  server_err_msg[ret].type,
									  "SignUp", server_err_msg[ret].msgs);

	if (href) {
		xmlFree(href);
	}

	return node;
}

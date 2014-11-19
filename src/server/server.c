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

#include <libxml/xpath.h>
#include <stdlib.h>
#include <string.h>
#include "obix_utils.h"
#include "log_utils.h"
#include "xml_storage.h"
#include "watch.h"
#include "server.h"
#include "xml_utils.h"
#include "obix_fcgi.h"
#include "history.h"
#include "batch.h"
#include "device.h"
#include "errmsg.h"

#ifdef DEBUG
/*
 * The special URI to expose all visible(not hidden) objects
 * in oBIX server XML database. For debug purpose only
 */
static const char *OBIX_SRV_DUMP_URI = "/obix-dump/";
static const char *OBIX_DEV_DUMP_URI = "/obix-dev-dump/";
static const char *OBIX_DEV_CACHE_DUMP_URI = "/obix-dev-cache-dump/";
#endif

/*
 * Prototype of a POST Handler function.
 *
 * The second parameter is to support the batch mechanism to further
 * redirect sub requests to specific facilities as specified by the
 * "val" attributes of batch commands in the batchIn contract
 */
typedef xmlNode *(*obix_server_postHandler)(obix_request_t *, const char *,
											xmlNode *);

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
	[8] = handlerSignOff,
	[9] = handlerBatch,
	[10] = handlerHistoryGet,
	[11] = handlerHistoryQuery,
	[12] = handlerHistoryAppend
};

/** Amount of available post handlers. */
static const int POST_HANDLERS_COUNT = 13;

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
	obix_devices_dispose();
	obix_hist_dispose();
	obix_watch_dispose();
	obix_xmldb_dispose();

	log_debug("oBIX server has been shutdown properly");
}

int obix_server_init(const xml_config_t *config)
{
	int poll_threads, table_size, cache_size, backup_period;

	if ((poll_threads = xml_config_get_int(config, XP_POLL_THREADS)) < 0 ||
		(table_size = xml_config_get_int(config, XP_DEV_TABLE_SIZE)) < 0 ||
		(cache_size = xml_config_get_int(config, XP_DEV_CACHE_SIZE)) < 0 ||
		(backup_period = xml_config_get_int(config, XP_DEV_BACKUP_PERIOD)) < 0) {
		log_error("Failed to get server settings");
		return -1;
	}

	/* Initialise the global DOM tree before any other facilities */
	if (obix_xmldb_init(config->resdir) != 0) {
		log_error("Failed to initialise the global XML DOM tree");
		return -1;
	}

	if (obix_watch_init(poll_threads) != 0) {
		log_error("Failed to initialise the watch subsystem");
		goto failed;
	}

	if (obix_hist_init(config->resdir) != 0) {
		log_error("Failed to initialise the history subsystem");
		goto hist_failed;
	}

	if (obix_devices_init(config->resdir, table_size,
						  cache_size, backup_period) != 0) {
		log_error("Failed to initialise the Device subsystem");
		goto device_failed;
	}

	return 0;

device_failed:
	obix_hist_dispose();

hist_failed:
	obix_watch_dispose();

failed:
	obix_xmldb_dispose();
	return -1;
}

xmlNode *obix_server_generate_error(const char *href, const char *contract,
									const char *name, const char *desc)
{
	xmlNode *node;

	if (!(node = xmldb_copy_sys(ERROR_STUB))) {
		return NULL;
	}

	if ((contract && !xmlSetProp(node, BAD_CAST OBIX_ATTR_IS, BAD_CAST contract)) ||
		(href && !xmlSetProp(node, BAD_CAST OBIX_ATTR_HREF, BAD_CAST href)) ||
		(name && !xmlSetProp(node, BAD_CAST OBIX_ATTR_DISPLAY_NAME, BAD_CAST name)) ||
		(desc && !xmlSetProp(node, BAD_CAST OBIX_ATTR_DISPLAY, BAD_CAST desc))) {
		log_error("Failed to set attributes on the error object");
		xmlFreeNode(node);
		node = NULL;
	}

	return node;
}

xmlNode *obix_server_read(obix_request_t *request, const char *overrideUri)
{
	xmlNode *copy = NULL;
	const xmlNode *target = NULL;
	int ret = 0;
	const char *uri;

	uri = (overrideUri != NULL) ? overrideUri : request->request_decoded_uri;

	if (!(target = xmldb_get_node(BAD_CAST uri))) {
		ret = ERR_NO_SUCH_URI;
		goto failed;
	}

	if (!(copy = xmldb_copy_node(target, XML_COPY_EXCLUDE_HIDDEN |
										 XML_COPY_EXCLUDE_META)) ||
		!xmlSetProp(copy, BAD_CAST OBIX_ATTR_HREF, BAD_CAST uri)) {
		ret = ERR_NO_MEM;
		goto failed;
	}

	/*
	 * Remove @a @hidden from any responses to the client, if they
	 * request a hidden node explicitly. BTW, all hidden nodes will
	 * be wiped out from response object before sending it out
	 */
	xmlUnsetProp(copy, BAD_CAST OBIX_ATTR_HIDDEN);

	/* Fall through */

failed:
	if (ret > 0) {
		log_error("%s : %s", uri, server_err_msg[ret].msgs);

		if (copy) {
			xml_delete_node(copy);
		}

		copy = obix_server_generate_error(uri, server_err_msg[ret].type,
									  "oBIX Server", server_err_msg[ret].msgs);
	}

	return copy;
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
	if (is_str_identical(request->request_decoded_uri, OBIX_SRV_DUMP_URI) == 1) {
		node = xmldb_dump(request);
	} else if (is_str_identical(request->request_decoded_uri,
								OBIX_DEV_DUMP_URI) == 1) {
		node = device_dump();
	} else if (is_str_identical(request->request_decoded_uri,
								OBIX_DEV_CACHE_DUMP_URI) == 1) {
		node = device_cache_dump();
	} else if (is_str_identical(request->request_decoded_uri,
								OBIX_DEVICES) == 1) {
		node = device_dump_ref();
	} else {
		node = obix_server_read(request, NULL);
	}
#else
	if (is_str_identical(request->request_decoded_uri, OBIX_DEVICES) == 1) {
		node = device_dump_ref();
	} else {
		node = obix_server_read(request, NULL);
	}
#endif

	obix_server_reply_object(request, ((node != NULL) ? node : xmldb_fatal_error()));
}

/*
 * Update the val attribute of the destination node
 */
xmlNode *obix_server_write(obix_request_t *request, const char *overrideUri,
						   xmlNode *input)
{
	xmlNode *target, *copy = NULL;
	const char *uri;
	int ret = 0;

	uri = (overrideUri != NULL) ? overrideUri : request->request_decoded_uri;

	if (!input) {
		ret = ERR_NO_INPUT;
		goto failed;
	}

	if (is_history_href(BAD_CAST uri) == 1) {
		ret = ERR_READONLY_HREF;
		goto failed;
	}

	if (!(target = xmldb_get_node(BAD_CAST uri))) {
		ret = ERR_NO_SUCH_URI;
		goto failed;
	}

	if ((ret = xmldb_update_node(input, target, uri)) != 0) {
		goto failed;
	}

	if (!(copy = xmldb_copy_node(target, XML_COPY_EXCLUDE_META |
										 XML_COPY_EXCLUDE_HIDDEN)) ||
		!xmlSetProp(copy, BAD_CAST OBIX_ATTR_HREF, BAD_CAST uri)) {
		ret = ERR_NO_MEM;
	}

	/* Fall through */

failed:
	if (ret > 0) {
		log_error("%s : %s", uri, server_err_msg[ret].msgs);

		if (copy) {
			xml_delete_node(copy);
		}

		copy = obix_server_generate_error(uri, server_err_msg[ret].type,
									  "obix:Write", server_err_msg[ret].msgs);
	}

	return copy;
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
	long handlerId = 0;
	int ret;

	uri = (overrideUri != NULL) ? overrideUri : request->request_decoded_uri;

	if (!(node = xmldb_get_node(BAD_CAST uri))) {
		ret = ERR_NO_SUCH_URI;
		goto failed;
	}

	if ((ret = xmldb_get_op_id(node, &handlerId)) == 0) {
		return obix_server_post_handler(handlerId)(request, overrideUri, input);
	}

	/* Fall through */

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
	 * Reparent the answer node to the newly created temp document to generate
	 * response. If it comes from the original input document that may have
	 * used a XML parser dictionary (e.g., returned by the signUp handler),
	 * the dictionary should be referenced so that the release of the temp
	 * document won't interfere with it
	 *
	 * However, such logic will be bypassed if the node using dictionary is not
	 * added as the root node of the temp document, e.g., as the child of the
	 * batchOut contract (which is created from a template and have no relation
	 * to any document), an extra copy is a must-have to de-associate with the
	 * dictionary. See comments in obix_batch_add_item
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
 */
xmlNode *handlerError(obix_request_t *request, const char *overrideUri,
					  xmlNode *input)
{
	const char *uri;

	uri = (overrideUri != NULL) ? overrideUri : request->request_decoded_uri;

	log_error("Requested operation \"%s\" not implemented.", uri);

	return obix_server_generate_error(uri, OBIX_CONTRACT_ERR_UNSUPPORTED,
						  "Unsupported Request",
						  "The requested operation is not yet implemented.");
}

xmlNode *handlerSignOff(obix_request_t *request, const char *overrideUri,
						xmlNode *input)
{
	xmlChar *href = NULL;
	const char *uri;
	char *requester_id = NULL;
	obix_dev_t *dev = NULL;
	xmlNode *node = NULL;
	int ret = 0;

	uri = (overrideUri != NULL) ? overrideUri : request->request_decoded_uri;

	if (!(requester_id = obix_fcgi_get_requester_id(request))) {
		ret = ERR_NO_REQUESTER_ID;
		goto failed;
	}

	if (!input) {
		ret = ERR_NO_INPUT;
		goto failed;
	}

	if (!(href = xmlGetProp(input, BAD_CAST OBIX_ATTR_HREF))) {
		ret = ERR_NO_HREF;
		goto failed;
	}

	if (xml_is_valid_href(href) == 0) {
		ret = ERR_INVALID_HREF;
		goto failed;
	}

	if (!(dev = device_search(href)) ||
		is_str_identical((const char *)dev->href, (const char *)href) == 0) {
		ret = ERR_DEVICE_NO_SUCH_URI;
		goto failed;
	}

	if (is_device_root_href(href) == 1) {
		ret = ERR_PERM_DENIED;
		goto failed;
	}

	if ((ret = device_del(dev, requester_id, 1)) == 0) {
		node = obix_obj_null(href);
	}

	/* Fall through */

failed:
	if (ret > 0) {
		log_error("SignOff \"%s\" : %s",
				  ((href) ? (char *)href : "(No Href from Device Contract)"),
				  server_err_msg[ret].msgs);

		node = obix_server_generate_error(uri, server_err_msg[ret].type,
										  "SignOff", server_err_msg[ret].msgs);
	}

	if (href) {
		xmlFree(href);
	}

	if (requester_id) {
		free(requester_id);
	}

	return node;
}

xmlNode *handlerSignUp(obix_request_t *request, const char *overrideUri,
					   xmlNode *input)
{
	xmlNode *inputCopy, *pos, *node = NULL;
	xmlChar *href = NULL;
	int ret = 0;
	const char *uri;
	char *requester_id = NULL;
	obix_dev_t *dev = NULL;

	uri = (overrideUri != NULL) ? overrideUri : request->request_decoded_uri;

	if (!(requester_id = obix_fcgi_get_requester_id(request))) {
		ret = ERR_NO_REQUESTER_ID;
		goto failed;
	}

	if (!input) {
		ret = ERR_NO_INPUT;
		goto failed;
	}

	if (!(href = xmlGetProp(input, BAD_CAST OBIX_ATTR_HREF))) {
		ret = ERR_NO_HREF;
		goto failed;
	}

	if (xml_is_valid_href(href) == 0) {
		ret = ERR_INVALID_HREF;
		goto failed;
	}

	if (is_device_href(href) == 0) {
		ret = ERR_DEVICE_NO_SUCH_URI;
		goto failed;
	}

	if (is_device_root_href(href) == 1) {
		ret = ERR_PERM_DENIED;
		goto failed;
	}

	if ((dev = device_search(href)) != NULL) {
		/*
		 * Return success when the device already registered to
		 * enable a re-started client handle the signUp gracefully.
		 *
		 * TODO:
		 * However, such practice will have a side-effect that
		 * the existing device may be altered and thus different
		 * than what has been provided.
		 */
		if (strcmp(dev->owner_id, requester_id) == 0) {
			log_debug("The same client (\"%s\") has already registered "
					  "the device of %s", requester_id, href);
			goto out;
		} else {
			log_error("The client (\"%s\") failed to override a device of %s "
					  "owned by \"%s\"", requester_id, href, dev->owner_id);
			ret = ERR_DEVICE_CONFLICT_OWNER;
			goto failed;
		}
	}

	xmlUnsetProp(input, BAD_CAST OBIX_ATTR_WRITABLE);

	/*
	 * An extra copy of the input node can be avoided for the signUp
	 * handler. However, both read and write handlers would benefit
	 * from an extra copy of the node read from or written into the
	 * global DOM tree so as to remove meta or hidden content before
	 * returning back to clients.
	 *
	 * To conform with the behaviour of the read and write operations,
	 * the copy is preserved here.
	 */
	if (!(inputCopy = xml_copy(input, XML_COPY_EXCLUDE_COMMENTS))) {
		ret = ERR_NO_MEM;
		goto failed;
	}

	if ((ret = device_add(inputCopy, href, requester_id, 1)) != 0) {
		xmlFreeNode(inputCopy);
		goto failed;
	}

	/* Fall through */

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

	node = input;

	/* Fall through */

failed:
	if (ret > 0) {
		log_error("SignUp \"%s\" : %s",
				  ((href) ? (char *)href : "(No Href from Device Contract)"),
				  server_err_msg[ret].msgs);

		node = obix_server_generate_error(uri, server_err_msg[ret].type,
										  "SignUp", server_err_msg[ret].msgs);
	}

	if (href) {
		xmlFree(href);
	}

	if (requester_id) {
		free(requester_id);
	}

	return node;
}

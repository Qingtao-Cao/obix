/* *****************************************************************************
 * Copyright (c) 2014 Tyler Watson <tyler.watson@nextdc.com>
 * Copyright (c) 2013-2015 Qingtao Cao [harry.cao@nextdc.com]
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

#include "log_utils.h"
#include "obix_utils.h"
#include "xml_utils.h"
#include "xml_storage.h"
#include "server.h"
#include "errmsg.h"
#include "device.h"

static void obix_batch_add_item(xmlNode *batchOut, xmlNode *item)
{
	xmlNode *copy = NULL;

	if (!item) {
		log_error("No item provided to add into BatchIn contract!");
		return;
	}

	/*
	 * The GET, PUT and most POST handlers except the signUp handler return
	 * a copy of some node in the global DOM tree which does not rely on any
	 * XML parser dictionary at all. Therefore adding such copy to the batchOut
	 * contract and further into a temporary document will yield no trouble to
	 * release them altogether
	 *
	 * However, the signUp handler returns directly some nodes from the original
	 * input document to avoid an extra copy of it, which are added as child of
	 * the batchOut contract instead of as the root node of the temporary
	 * document, resulting in incorrect interference with the dictionary when
	 * they are released
	 *
	 * A viable solution is to make a copy so as to de-associate with any
	 * original dictionary that may have been used before re-parenting to the
	 * batchOut contract
	 *
	 * NOTE, if NO dictionary is used when reading the input document
	 * in the first place, no copy is ever needed
	 */
	copy = (item->doc && item->doc->dict) ? xmlCopyNode(item, 1) : item;

	if (copy && xmldb_add_child(batchOut, copy, 1, 0) != 0) {
		log_error("could not add item into the provided BatchOut contract.");

		/*
		 * The copy should be freed manually since it could not be released
		 * along with the batchOut contract if failed to be added into it
		 * in the first place
		 */
		xmlFree(copy);
	}
}

static void obix_batch_process_item(obix_request_t *request, xmlNode *batchItem,
									xmlNode *batch_out, obix_dev_t **dev)
{
	xmlNode *node = NULL;
	xmlChar *is_attr = NULL;
	char *href = NULL;
	int ret = 0;

	if (!(href = (char *)xmlGetProp(batchItem, BAD_CAST OBIX_ATTR_VAL))) {
		ret = ERR_INVALID_INPUT;
		goto failed;
	}

	/*
	 * Sanity checks on the redirected-to href must be done, e.g.,
	 * to prevent client reading the content of the entire DOM tree
	 * by specifying "/"
	 */
	if (xml_is_valid_href((xmlChar *)href) == 0) {
		ret = ERR_INVALID_HREF;
		goto failed;
	}

	if (!(is_attr = xmlGetProp(batchItem, BAD_CAST OBIX_ATTR_IS))) {
		ret = ERR_INVALID_INPUT;
		goto failed;
	}

	if (xmlStrcasecmp(is_attr, BAD_CAST OBIX_CONTRACT_OP_READ) == 0) {
		node = obix_server_read(request, href);
	} else if (xmlStrcasecmp(is_attr, BAD_CAST OBIX_CONTRACT_OP_WRITE) == 0) {
		node = obix_server_write(request, href, batchItem->children);

		/*
		 * If writing a subnode of a device contract succeeds and it is
		 * the first device found, cache up its descriptor
		 */
		if (!*dev && xmlStrcmp(node->name, BAD_CAST OBIX_OBJ_ERR) != 0) {
			*dev = device_search_parent((xmlChar *)href);
		}
	} else if (xmlStrcasecmp(is_attr, BAD_CAST OBIX_CONTRACT_OP_INVOKE) == 0) {
		if (strncmp(href, OBIX_BATCH, OBIX_BATCH_LEN) == 0) {
			/*
			 * Prohibit recursive batch invocation
			 */
			ret = ERR_BATCH_RECURSIVE;
		} else if (strncmp(href, OBIX_HISTORY_SERVICE,
						   OBIX_HISTORY_SERVICE_LEN) == 0) {
			/*
			 * Histroy handlers take care of sending back responses by
			 * themselves which are likely too massive to be sent via
			 * the batchOut contract. Moreover, they are sent independently
			 * from the batchOut contract. Therefore no history requests
			 * are allowed through a batch request.
			 */
			ret = ERR_BATCH_HISTORY;
		} else if (strncmp(href, OBIX_WATCH_SERVICE,
						   OBIX_WATCH_SERVICE_LEN) == 0 &&
				   strstr(href, OBIX_WATCH_POLLCHANGES) != NULL) {
			/*
			 * The polling threads handling watch.pollChanges requests will
			 * compete against the current thread handling the batchIn contract
			 * on sending through the same FCGI request the watchOut and the
			 * batchOut contract independently and then having the FCGI request
			 * released which will result in segfault. Therefore no pollChanges
			 * requests are allowed through a batch request.
			 */
			ret = ERR_BATCH_POLLCHANGES;
		} else {
			node = obix_server_invoke(request, href, batchItem->children);
		}
	} else {
		ret = ERR_INVALID_INPUT;
	}

	/* Fall Through */

failed:
	if (ret > 0) {
		log_error("%s", server_err_msg[ret].msgs);

		node = obix_server_generate_error(href, server_err_msg[ret].type,
									 "obix:Batch", server_err_msg[ret].msgs);
	}

	if (is_attr) {
		xmlFree(is_attr);
	}

	if (href) {
		free(href);
	}

	obix_batch_add_item(batch_out, node);
}

/**
 * Handles Batch operation
 *
 * NOTE: recursive batch invocation is disabled, therefore the batch
 * facility can't be redirected to and the overrideUri is ignored
 */
xmlNode *handlerBatch(obix_request_t *request, const char *overrideUri,
					  xmlNode *input)
{
	xmlNode *batch_out = NULL, *item;
	xmlChar *is_attr = NULL;
	obix_dev_t *dev = NULL;
	int ret = 0;

	if (!input) {
		ret = ERR_NO_INPUT;
		goto failed;
	}

	if (!(batch_out = xmldb_copy_sys(BATCH_OUT_STUB))) {
		ret = ERR_NO_MEM;
		goto failed;
	}

	if (xmlStrcmp(input->name, BAD_CAST OBIX_OBJ_LIST) != 0 ||
		!(is_attr = xmlGetProp(input, BAD_CAST OBIX_ATTR_IS)) ||
		xmlStrcmp(is_attr, BAD_CAST OBIX_CONTRACT_BATCH_IN) != 0) {
		ret = ERR_INVALID_INPUT;
		goto failed;
	}

	/*
	 * If the batchIn contract is malformed, then not all of its commands
	 * may have a chance to be processed
	 */
	for (item = input->children; item; item = item->next) {
		if (item->type != XML_ELEMENT_NODE ||
			xmlStrcmp(item->name, BAD_CAST OBIX_OBJ_URI) != 0) {
			continue;
		}

		/*
		 * Keep on processing the whole batchIn contract regardless of
		 * whether the current one generates an error contract or not
		 *
		 * If batch operations involve writting into devices, return
		 * the first device's descriptor
		 *
		 * Normally speaking oBIX client applications should manipulate
		 * one batch object to update the entire contract of one device
		 * and then back it up to persistent files on the hard drive
		 * if needed
		 */
		obix_batch_process_item(request, item, batch_out, &dev);
	}

	if (dev) {
		device_write_file(dev);
	}

	/* Fall through */

failed:
	if (is_attr) {
	    xmlFree(is_attr);
	}

	if (ret > 0) {
		log_error("%s", server_err_msg[ret].msgs);

		if (batch_out) {
			xmlFreeNode(batch_out);
		}

		batch_out = obix_server_generate_error(request->request_decoded_uri,
									  server_err_msg[ret].type,
									  "obix:Batch", server_err_msg[ret].msgs);
	}

	return batch_out;
}

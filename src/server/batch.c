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

#include <assert.h>
#include "log_utils.h"
#include "obix_utils.h"
#include "xml_utils.h"
#include "xml_storage.h"
#include "server.h"

enum {
	ERR_NO_VAL = 1,
	ERR_NO_IS,
	ERR_NO_RESP,
	ERR_NO_MEM,
	ERR_NO_CONTENT,
	ERR_INVALID_INPUT
};

static err_msg_t batch_err_msg[] = {
	[ERR_NO_VAL] = {
		.type = OBIX_CONTRACT_ERR_UNSUPPORTED,
		.msgs = "No required val attribute in BatchIn item"
	},
	[ERR_NO_IS] = {
		.type = OBIX_CONTRACT_ERR_UNSUPPORTED,
		.msgs = "No required is attribute in BatchIn item"
	},
	[ERR_NO_RESP] = {
		.type = OBIX_CONTRACT_ERR_SERVER,
		.msgs = "The handler for this BatchIn item did not respond"
	},
	[ERR_NO_MEM] = {
		.type = OBIX_CONTRACT_ERR_SERVER,
		.msgs = "Failed to allocate BatchOut contract"
	},
	[ERR_INVALID_INPUT] = {
		.type = OBIX_CONTRACT_ERR_SERVER,
		.msgs = "Provided obix:BatchIn document is invalid"
	}
};

/**
 * Adds an oBIX object pointed to by @a item to the oBIX:BatchOut contract
 * pointed to by @a batchOutContract.
 *
 * @param	batchOutContract	A pointer to an initialized obix:BatchOut contract
 * @param	item				A pointer to an oBIX object to add to the batch
 *								out contract.
 * @remark	This is a consuming function, the XML node pointed to by @a item
 *			will be unlinked from it's previous tree and added to the contract
 *			pointed to by @a batchOutContract. Do not pass @a item to xmlFree() or
 *			undefined behaviour will occur in the response, the pointer to item
 *			should be freed when @a xmlFree() is called on @a batchOutContract.
 */
static void obix_batch_add_item(xmlNode *batchOutContract, xmlNode *item)
{
	assert(batchOutContract && item);

	if (xmldb_add_child(batchOutContract, item, 1, 0) == NULL) {
		log_error("could not add item into the provided BatchOut contract.");
		/*
		 * The item should be freed manually since it could not be released
		 * along with the batch_out contract if failed to be added into it
		 * in the first place
		 */
		xmlFree(item);
	}
}

/**
 * Processes an obix:BatchIn item as pointed to by @a batchItem, and returns its response
 * or an obix:Err contract if the handler for the item returned an error.
 *
 * @param	batchItem	A pointer to an obix:Batch item, pulled from a BatchIn contract.
 * @remark	In catastrophic cases, this function may return NULL.
 */
static int obix_batch_process_item(xmlNode *batchItem, void *arg1, void *arg2)
{
	xmlNode *batch_out = *(xmlNode **)arg1;
	obix_request_t *request = (obix_request_t *)arg2;
	xmlNode *itemVal = NULL;
	xmlChar *itemContract = NULL;
	char *itemHref = NULL;
	int ret;

	assert(batchItem);

	if (!(itemHref = (char *)xmlGetProp(batchItem, BAD_CAST OBIX_ATTR_VAL))) {
		ret = ERR_NO_VAL;
		goto failed;
	}

	if (!(itemContract = xmlGetProp(batchItem, BAD_CAST OBIX_ATTR_IS))) {
		ret = ERR_NO_IS;
		goto failed;
	}

	if (xmlStrcasecmp(itemContract, BAD_CAST OBIX_CONTRACT_OP_READ) == 0) {
		itemVal = obix_server_read(request, itemHref);
	} else if (xmlStrcasecmp(itemContract, BAD_CAST OBIX_CONTRACT_OP_WRITE) == 0) {
		itemVal = obix_server_write(request, itemHref, batchItem->children);
	} else if (xmlStrcasecmp(itemContract, BAD_CAST OBIX_CONTRACT_OP_INVOKE) == 0) {
		itemVal = obix_server_invoke(request, itemHref, batchItem->children);
	}

	ret = ((itemVal == NULL) ? ERR_NO_RESP : 0);

	/* Fall Through */

failed:
	if (ret) {
		log_error("%s", batch_err_msg[ret].msgs);
		itemVal = obix_server_generate_error(itemHref, batch_err_msg[ret].type,
											 "obix:Batch", batch_err_msg[ret].msgs);
	}

	if (itemContract) {
		xmlFree(itemContract);
	}

	if (itemHref) {
		free(itemHref);
	}

	obix_batch_add_item(batch_out, itemVal);

	return 0;
}

static xmlNode *obix_batch_process(obix_request_t *request, xmlNode *batchInInput)
{
	xmlNode *batchOutContract = NULL;
	xmlNode *uriItem = NULL;
	xmlChar *isAttr = NULL;
	int ret;

	assert(batchInInput);

	if (!(batchOutContract = xmldb_copy_sys(OBIX_SYS_BATCH_OUT_STUB))) {
		ret = ERR_NO_MEM;
		goto failed;
	}

	if (xmlStrcasecmp(batchInInput->name, BAD_CAST OBIX_OBJ_LIST) != 0
			|| xmlHasProp(batchOutContract, BAD_CAST OBIX_ATTR_IS) == false
			|| (isAttr = xmlGetProp(batchInInput, BAD_CAST OBIX_ATTR_IS)) == NULL
			|| xmlStrcmp(isAttr, BAD_CAST OBIX_CONTRACT_BATCH_IN) != 0) {
		ret = ERR_INVALID_INPUT;
		goto failed;
	}

	for (uriItem = batchInInput->children; uriItem; uriItem = uriItem->next) {
		if (xmlStrcasecmp(uriItem->name, BAD_CAST OBIX_OBJ_URI) != 0) {
			continue;
		}

		if (obix_batch_process_item(uriItem, &batchOutContract, request) < 0) {
			break;
		}
	}

	xmlFree(isAttr);

	return batchOutContract;

failed:
	if (isAttr != NULL) {
		xmlFree(isAttr);
	}

	log_error("%s", batch_err_msg[ret].msgs);
	return obix_server_generate_error(request->request_decoded_uri, batch_err_msg[ret].type,
									  "obix:Batch", batch_err_msg[ret].msgs);
}

/**
 * Handles Batch operation.
 *
 * @see obix_server_postHandler
 */
xmlNode *handlerBatch(obix_request_t *request, xmlNode *input)
{
	return obix_batch_process(request, input);
}

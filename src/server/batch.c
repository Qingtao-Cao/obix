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

#include "log_utils.h"
#include "obix_utils.h"
#include "xml_utils.h"
#include "xml_storage.h"
#include "server.h"

/*
 * XPath Predict for any node contained in obix:BatchIn contract
 * (as children of its list node)
 */
static const char *XP_BATCHIN = "/list[@is='obix:BatchIn']/*";

enum {
	ERR_NO_VAL = 1,
	ERR_NO_IS,
	ERR_NO_MEM,
	ERR_BAD_IS,
	ERR_NO_INPUT
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
	[ERR_BAD_IS] = {
		.type = OBIX_CONTRACT_ERR_UNSUPPORTED,
		.msgs = "The required is attribute is NOT recognisable"
	},
	[ERR_NO_MEM] = {
		.type = OBIX_CONTRACT_ERR_SERVER,
		.msgs = "Failed to allocate BatchOut contract"
	},
	[ERR_NO_INPUT] = {
		.type = OBIX_CONTRACT_ERR_UNSUPPORTED,
		.msgs = "No required BatchIn document at all"
	}
};

static void obix_batch_add_item(xmlNode *batchOut, xmlNode *item)
{
	if (!item) {
		log_error("No item provided to add into BatchIn contract!");
		return;
	}

	if (xmldb_add_child(batchOut, item, 1, 0) == NULL) {
		log_error("could not add item into the provided BatchOut contract.");

		/*
		 * The item should be freed manually since it could not be released
		 * along with the batchOut contract if failed to be added into it
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
static void obix_batch_process_item(xmlNode *batchItem, void *arg1, void *arg2)
{
	xmlNode *batch_out = (xmlNode *)arg1;
	obix_request_t *request = (obix_request_t *)arg2;
	xmlNode *itemVal = NULL;
	xmlChar *itemContract = NULL;
	char *itemHref = NULL;
	int ret = 0;

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
	} else {
		ret = ERR_BAD_IS;
	}

	/* Fall Through */

failed:
	if (ret != 0) {
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
}

static xmlNode *obix_batch_process(obix_request_t *request, xmlNode *input)
{
	xmlNode *batch_out = NULL;
	int ret;

	if (!input) {
		ret = ERR_NO_INPUT;
		goto failed;
	}

	if (!(batch_out = xmldb_copy_sys(OBIX_SYS_BATCH_OUT_STUB))) {
		ret = ERR_NO_MEM;
		goto failed;
	}

	xml_xpath_for_each_item(input, XP_BATCHIN,
							obix_batch_process_item, batch_out, request);
	return batch_out;

failed:
	log_error("%s", batch_err_msg[ret].msgs);
	return obix_server_generate_error(request->request_decoded_uri,
									  batch_err_msg[ret].type,
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

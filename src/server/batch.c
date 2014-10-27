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

enum {
	ERR_NO_VAL = 1,
	ERR_NO_IS,
	ERR_NO_MEM,
	ERR_BAD_IS,
	ERR_NO_INPUT,
	ERR_RECURSIVE
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
	},
	[ERR_RECURSIVE] = {
		.type = OBIX_CONTRACT_ERR_UNSUPPORTED,
		.msgs = "Recursive batch commands not supported"
	}
};

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

	if (copy && xmldb_add_child(batchOut, copy, 1, 0) == NULL) {
		log_error("could not add item into the provided BatchOut contract.");

		/*
		 * The copy should be freed manually since it could not be released
		 * along with the batchOut contract if failed to be added into it
		 * in the first place
		 */
		xmlFree(copy);
	}
}

static void obix_batch_process_item(xmlNode *batchItem, xmlNode *batch_out,
									obix_request_t *request)
{
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
		/* Prohibit recursive batch invocation */
		if (strncmp(itemHref, OBIX_BATCH, OBIX_BATCH_LEN) == 0) {
			ret = ERR_RECURSIVE;
		} else {
			itemVal = obix_server_invoke(request, itemHref, batchItem->children);
		}
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
	xmlNode *batch_out = NULL, *item;
	xmlChar *is_attr = NULL;;
	int ret;

	if (!input) {
		ret = ERR_NO_INPUT;
		goto failed;
	}

	if (!(batch_out = xmldb_copy_sys(OBIX_SYS_BATCH_OUT_STUB))) {
		ret = ERR_NO_MEM;
		goto failed;
	}

	if (xmlStrcmp(input->name, BAD_CAST OBIX_OBJ_LIST) != 0 ||
		!(is_attr = xmlGetProp(input, BAD_CAST OBIX_ATTR_IS)) ||
		xmlStrcmp(is_attr, BAD_CAST OBIX_CONTRACT_BATCH_IN) != 0) {
		ret = ERR_NO_IS;
		goto failed;
	}

	for (item = input->children; item; item = item->next) {
		if (item->type != XML_ELEMENT_NODE) {
			continue;
		}

		if (xmlStrcmp(item->name, BAD_CAST OBIX_OBJ_URI) != 0) {
			continue;
		}

		obix_batch_process_item(item, batch_out, request);
	}

	xmlFree(is_attr);

	return batch_out;

failed:
	if (is_attr) {
	    xmlFree(is_attr);
	}

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

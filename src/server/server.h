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

#ifndef OBIX_SERVER_H_
#define OBIX_SERVER_H_

#include <libxml/tree.h>
#include "libxml_config.h"
#include "response.h"

/**
 * Initializes request processing engine.
 * @param settings Server's settings loaded from #server_config.xml.
 * @return @a 0 on success; @a -1 on failure.
 */
int obix_server_init(xml_config_t *context);

/**
 * Stops request processing engine and releases all allocated memory.
 */
void obix_server_shutdown();


/**
 * Generates an oBIX Error to be sent to the client with the specified contract
 *
 * @param href          The URI requested by the client which generated this error.
 * @param contract      The error contract type
 *			error contracts: Bad URI error (#OBIX_CONTRACT_ERR_BAD_URI),
 *			Permission error (#OBIX_CONTRACT_ERR_PERMISSION) and
 *			Unsupported error (#OBIX_CONTRACT_ERR_UNSUPPORTED). If none of these
 *			contracts suit for the particular error, than @a NULL should be
 *			provided.
 * @param name The name of the error
 * @param desc The description of the error.
 *
 * @remarks This is an allocating function.  It's up to the caller to free the xmlNode pointer once it has finished with it.
 */
xmlNode *obix_server_generate_error(const char *href, const char *contract, const char *name, const char *desc);

/**
 * Handles an unknown or unsupported HTTP request.
 * @param response      A pointer to the response object that contains the response stream
 *                      for the client that requested it
 * @param uri           A pointer to the requested URI by the client.
 * @param msg			A pointer to static error message.
 * @remark              This function simply sends an error contract.
 */
void obix_server_handleError(response_t *response, const char *uri, const char *msg);

/**
 * Handles GET request and sends response back to the client.
 * @param response Response object, which should be used to generate an answer.
 * @param uri URI, which was requested by client.
 */
void obix_server_handleGET(response_t* response, const char* uri);

/**
 * Reads an XML structure from the XML database and returns it to the caller.
 *
 * @remark This is an allocating function.  It's up to the caller to free the memory allocated.
 */
xmlNode *obix_server_read(const char* uri);

/**
 * Handles PUT request and sends response back to the client.
 * @param response Response object, which should be used to generate an answer.
 * @param uri URI, which was requested by client.
 * @param input Clients message (body of the PUT request).
 */
void obix_server_handlePUT(response_t* response, const char* uri, const xmlDoc *input);

/**
 * Writes an oBIX Document pointed to by @a input to the XML storage at the location
 * pointed to by @a uri, and returns a copy copy of the element it inserted or an oBIX
 * error document.
 *
 * @remark this is an allocating function.  It's up to the caller to free the XML
 * node allocated by this function.
 */
xmlNode *obix_server_write(const char* uri, xmlNode* input);

/**
 * Handles POST request and sends response back to the client.
 * @param response Response object, which should be used to generate an answer.
 * @param uri URI, which was requested by client.
 * @param input Clients message (body of the POST request).
 */
void obix_server_handlePOST(response_t* response, const char* uri, const xmlDoc *input);

/**
 * Invokes an oBIX Operation pointed to by @a uri with the parsed input document
 * pointed to by @a input
 * @param response      A pointer to the response object containing the client request.
 * @param uri           The href of the oBIX operation invoked by the client
 * @param input         A pointer to the parsed XML document that should be regarded
 *                      by the target oBIX operation as oBIX parameters
 * @return              A pointer to the XML response returned by the oBIX operation,
 *                      if any.  If the response is NULL, then the caller should *NOT*
 *                      respond to the client; the Response mechanism pointed to by
 *                      @a response should be responsible for responding to the client
 *                      at some point in the future.
 *
 * @remark              This is an allocating function.  It's up to the caller to free
 *                      the memory allocated by this function with @a xmlFree.
 */
xmlNode *obix_server_invoke(response_t* response, const char* uri, xmlNode* input);

/**
 * Removes all sub-nodes in the XML tree pointed to by @a obixObject that has a
 * tag-name matching @a meta. (i.e. <meta> tags)
 * @param obixObject A pointer to the XML node to strip <meta> tags from.
 */
void obix_server_remove_meta(xmlNode *obixObject);

/**
 * Instructs the oBIX Server to reply an oBIX Object pointed to by @a obixObject to the requester
 * pointed to by @a responseStream.  Overrides the responded URI if the string pointed to by @a overrideUri
 * is not NULL.
 * @param responseStream    A pointer to the response object of the client associated with this request
 * @param obixObject        A pointer to the oBIX Object that will be replied
 * @param overrideUri       A pointer to a string with the URI to be pushed to the client, if different from
 *                          the request.
 */
void obix_server_reply_object(response_t *responseStream, xmlNode *obixObject, const char *overrideUri);

xmlNode *handlerError(response_t *response, const char *uri, xmlNode *input);
xmlNode *handlerSignUp(response_t *response, const char *uri, xmlNode *input);

/**
 * Descriptor of an error message and relevant error type
 */
typedef struct err_msg {
	const char *type;
	const char *msgs;
} err_msg_t;

#endif /* OBIX_SERVER_H_ */

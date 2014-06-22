/* *****************************************************************************
 * Copyright (C) 2014 Tyler Watson <tyler.watson@nextdc.com>
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

#include <assert.h>
#include <string.h>
#include <pthread.h>
#include "log_utils.h"
#include "obix_utils.h"
#include "obix_request.h"
#include "xml_utils.h"

/*
 * Parameters used in FCGI connection settings
 */
#define LISTENSOCK_FILENO	0
#define LISTENSOCK_FLAGS	0

static obix_request_listener _request_listener = NULL;

void obix_request_set_listener(obix_request_listener listener)
{
	_request_listener = listener;
}

void obix_request_send_response(obix_request_t *request)
{
	(*_request_listener)(request);
}

/**
 * Create a response descriptor and pair it up with relevant
 * FCGI request, which is the vehicle to send response back
 * to oBIX client.
 *
 * Note,
 * 1. Each oBIX server thread will create and manipulate a pair
 * of [response, request] for EACH request received on ONE FCGI
 * channel, which supports multiplex communication.
 */
obix_request_t *obix_request_create(FCGX_Request *request)
{
	obix_request_t *obixRequest;

	assert(request);

	if (!(obixRequest = (obix_request_t *)malloc(sizeof(obix_request_t)))) {
		log_error("Failed to create an oBIX request structure.");
		return NULL;
	}
	memset(obixRequest, 0, sizeof(obix_request_t));

	obixRequest->request = request;
	INIT_LIST_HEAD(&obixRequest->response_items);
	pthread_mutex_init(&obixRequest->mutex, NULL);

	return obixRequest;
}

/**
 * Destroy a response_item_t descriptor
 *
 * Note,
 * 1. Caller must have grabbed response->mutex
 */
void obix_request_destroy_response_item(response_item_t *item)
{
	if (item->body) {
		free(item->body);
	}

	free(item);
}

/**
 * Finish, close and release the given request
 */
void obix_fcgi_request_destroy(FCGX_Request *request)
{
	assert(request);

	FCGX_Finish_r(request);
	FCGX_Free(request, 1);

	/*
	 * FCGX_Free is not sufficient to release the malloced
	 * request. Otherwise memory leaks.
	 */
	free(request);
}

/**
 * Create a brand-new FCGI Request, initialize it and listen on
 * FCGI channel until a request has been successfully accepted
 */
FCGX_Request *obix_fcgi_request_create(void)
{
	FCGX_Request *request;
	int error;

	if (!(request = (FCGX_Request *)malloc(sizeof(FCGX_Request)))) {
		log_error("Failed to create FCGI Request structure");
		return NULL;
	}

	if ((FCGX_InitRequest(request, LISTENSOCK_FILENO, LISTENSOCK_FLAGS)) != 0) {
		log_error("Failed to initialize the FCGI request");
		goto failed;
	}

	if ((error = FCGX_Accept_r(request)) == 0) {
		return request;
	}

	log_error("Failed to accept FCGI request, returned %d: %s",
			  error, strerror(error * -1));

	/* Fall through */

failed:
	obix_fcgi_request_destroy(request);
	return NULL;
}

void obix_request_destroy_response_items(obix_request_t *request)
{
	response_item_t *item, *n;

	pthread_mutex_lock(&request->mutex);
	list_for_each_entry_safe(item, n, &request->response_items, list) {
		list_del(&item->list);
		obix_request_destroy_response_item(item);
	}
	pthread_mutex_unlock(&request->mutex);
}

/**
 * Destroy a response_t descriptor, including its queue
 * of response_item_t AND the accompanied FCGI Request
 */
void obix_request_destroy(obix_request_t *request)
{
	assert(request);

	obix_fcgi_request_destroy(request->request);

	/*
	 * glibc free() won't nullify the freed memory region, therefore
	 * explicitly nullify the pointer so as to prevent program from
	 * going insane in error conditions
	 */
	request->request = NULL;

	obix_request_destroy_response_items(request);

	pthread_mutex_destroy(&request->mutex);

	/*
	 * strdup may have failed to duplicated the overriden URI
	 * as response's uri
	 */
	if (request->response_uri) {
		free(request->response_uri);
	}

	if (request->request_decoded_uri) {
		free((char *)request->request_decoded_uri);
	}

	free(request);
}

response_item_t *obix_request_create_response_item(char *text, int size, int copy)
{
	response_item_t *item;

	assert(text && size > 0);

	if (!(item = (response_item_t *)malloc(sizeof(response_item_t)))) {
		return NULL;
	}

	memset(item, 0, sizeof(response_item_t));

	INIT_LIST_HEAD(&item->list);
	item->len = size;

	if (copy == 0) {
		item->body = text;
	} else {
		if (!(item->body = strdup(text))) {
			free(item);
			item = NULL;
		}
	}

	return item;
}

void obix_request_add_response_item(obix_request_t *request, response_item_t *item)
{
	assert(request && item);

	pthread_mutex_lock(&request->mutex);
	list_add(&item->list, &request->response_items);
	request->response_len += item->len;
	request->response_items_count++;
	pthread_mutex_unlock(&request->mutex);
}

void obix_request_append_response_item(obix_request_t *request, response_item_t *item)
{
	assert(request && item);

	pthread_mutex_lock(&request->mutex);
	list_add_tail(&item->list, &request->response_items);
	request->response_len += item->len;
	request->response_items_count++;
	pthread_mutex_unlock(&request->mutex);
}

/**
 * Create a response_item_t descriptor to carry the text
 * that should be sent back to oBIX clients and append it to
 * the response items queue of a response_t
 *
 * Note,
 * 1. Since the string pointed to by reponse_item_t.body
 * will unconditionally released once being sent out,
 * caller should specify copy == 0 for dynamically allocated
 * strings so that they can be released on behalf of their
 * creator, whereas copy == 1 for those static strings so
 * that the copy instead of the orignal, static ones will be
 * released.
 *
 * 2. On failure callers should pay attention to release
 * the text before exit.
 */
int obix_request_create_append_response_item(obix_request_t *request, char *text, int size, int copy)
{
	response_item_t *item;

	if (!(item = obix_request_create_response_item(text, size, copy))) {
		return -1;
	}

	obix_request_append_response_item(request, item);

	return 0;
}

/**
 * Get the length of response
 */
long obix_request_get_response_len(obix_request_t *request)
{
	long len;

	pthread_mutex_lock(&request->mutex);
	len = request->response_len;
	pthread_mutex_unlock(&request->mutex);

	return len;
}

/**
 * Get the number of response items
 */
int obix_request_get_response_items(obix_request_t *request)
{
	int items;

	pthread_mutex_lock(&request->mutex);
	items = request->response_items_count;
	pthread_mutex_unlock(&request->mutex);

	return items;
}

/**
 * Add XML document header as the very first response item
 */
int obix_request_add_response_xml_header(obix_request_t *request)
{
	response_item_t *item;
	int len = strlen(XML_HEADER);

	if (!(item = obix_request_create_response_item((char *)XML_HEADER, len, 1))) {
		return -1;
	}

	obix_request_add_response_item(request, item);

	return 0;
}

/*
 * TODO:
 * Check if the current request comes from a privileged adapter.
 * Return 1 if this is the case, 0 otherwise
 *
 * An environmental variable of current request could be manipulated
 * for this purpose. And the web server responsible for authentication
 * should set/reset such variable according to the client's IP address.
 */
int is_privileged_mode(obix_request_t *request)
{
	return 1;
}


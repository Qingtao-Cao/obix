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

#ifndef _OBIX_REQUEST_H
#define _OBIX_REQUEST_H

#include <fcgiapp.h>
#include <pthread.h>
#include "list.h"

typedef struct response_item {
	/* Full or a part of response from oBIX server */
	char *body;

	/* The length of the response carried by this item */
	int len;

	/* Joining response->items queue */
	struct list_head list;
} response_item_t;

typedef struct obix_request {
	/*
	 * For what the response is generated, used to setup
	 * HTTP Content-Location header
	 * 
	 * Clients will be 'redirected' to the URI presented
	 * in this strucuture if not NULL.
	 */
	char *response_uri;

	/*
	 * The FastCGI requested requested URI
	 */
	const char *request_uri;

	/*
	 * Contains a URL decoded request URI.
	 */
	const char *request_decoded_uri;

	/*
	 * Raised for long poll requests which will be handled in
	 * an asynchronous manner
	 */
	int no_reply;

	/*
	 * Raised for history relevant requests. History methods will
	 * take care of sending and destroying their responses by themselves
	 */
	int is_history;

	/*
	 * The overall body length of current response
	 *
	 * Note,
	 * 1. History.Query result could have more than 4GB bytes,
	 * therefore long instead of int must be used.
	 */
	long response_len;

	/*
	 * Accompanied FCGI request, which should and would released
	 * along with the paired response after it has been sent out
	 */
	FCGX_Request *request;

	/*
	 * Queue of response items
	 *
	 * Normally one response item will suffice to carry result
	 * for oBIX clients, however, history.Query results COULD
	 * consist of a vast number of response items, each one of
	 * which will carry the content of one single raw history
	 * data file
	 */
	struct list_head response_items;

	/*
	 * Counter of the number of items in the queue, for debug purpose
	 */
	int response_items_count;

	/* Mutex to protect the whole data structure */
	pthread_mutex_t mutex;
} obix_request_t;

typedef void (*obix_request_listener)(obix_request_t *);

obix_request_t *obix_request_create(FCGX_Request *);

void obix_request_destroy(obix_request_t *);

void obix_request_destroy_response_item(response_item_t *);

void obix_request_destroy_response_items(obix_request_t *);

response_item_t *obix_request_create_response_item(char *text, int size, int copy);

int obix_request_create_append_response_item(obix_request_t *, char *, int, int);

void obix_request_add_response_item(obix_request_t *, response_item_t *);

void obix_request_append_response_item(obix_request_t *, response_item_t *);

int obix_request_add_response_xml_header(obix_request_t *resp);

void obix_request_set_listener(obix_request_listener);

void obix_request_send_response(obix_request_t *);

long obix_request_get_response_len(obix_request_t *);

int obix_request_get_response_items(obix_request_t *);

void obix_fcgi_request_destroy(FCGX_Request *);

FCGX_Request *obix_fcgi_request_create(void);

int is_privileged_mode(obix_request_t *);

#endif

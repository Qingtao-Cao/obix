/* *****************************************************************************
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

#ifndef _RESPONSE_H
#define _RESPONSE_H

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

typedef struct response {
	/*
	 * For what the response is generated, used to setup
	 * HTTP Content-Location header
	 */
	char *uri;

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
	long len;

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
	struct list_head items;

	/*
	 * Counter of the number of items in the queue, for debug purpose
	 */
	int items_count;

	/* Mutex to protect the whole data structure */
	pthread_mutex_t mutex;
} response_t;

typedef void (*obix_response_listener)(response_t *);

response_t *obix_response_create(FCGX_Request *);

void obix_response_destroy(response_t *);

void obix_response_destroy_item(response_item_t *);

void obix_response_destroy_items(response_t *);

response_item_t *obix_response_create_item(char *text, int size, int copy);

int obix_response_create_append_item(response_t *, char *, int, int);

void obix_response_add_item(response_t *, response_item_t *);

void obix_response_append_item(response_t *, response_item_t *);

int obix_response_add_xml_header(response_t *resp);

void obix_response_set_listener(obix_response_listener);

void obix_response_send(response_t *);

long obix_response_get_len(response_t *);

int obix_response_get_items(response_t *);

void obix_request_destroy(FCGX_Request *);

FCGX_Request *obix_request_create(void);

int is_privileged_mode(response_t *response);

#endif

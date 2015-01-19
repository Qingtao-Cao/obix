/******************************************************************************
 * Copyright (c) 2013-2015 Qingtao Cao
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
 ******************************************************************************/

#ifndef _OBIX_FCGI_H
#define _OBIX_FCGI_H

#include "obix_request.h"

/*
 * Descriptor for the FCGX channel
 */
typedef struct obix_fcgi {
	/*
	 * The open file descriptor of the FCGX listening socket that
	 * oBIX server threads accept simualtaneously
	 */
	int fd;

	/*
	 * The number of server threads handling different requests
	 * in parallel, excluding those watch threads that handle
	 * watch.pollChanges requests in asynchronous manner
	 */
	int multi_threads;

	/* The array of pthread_t for above sync threads */
	pthread_t *id;

	/*
	 * Method used by a server thread to send response back for
	 * the given request
	 *
	 * NOTE: each FCGX request represents a separate established
	 * connection between the web server and the oBIX server, that's
	 * how FCGX supports multiplex communication concurrently
	 */
	void (*send_response)(obix_request_t *);

	/* The mutex to prevent races on accept(), needed on some platform */
	pthread_mutex_t mutex;
} obix_fcgi_t;

extern obix_fcgi_t *__fcgi;

typedef enum fcgi_env {
	FCGI_ENV_REQUEST_URI = 0,
	FCGI_ENV_REQUEST_METHOD,
	FCGI_ENV_REMOTE_PORT,
	FCGI_ENV_REMOTE_ADDR,
	FCGI_ENV_REQUESTER_ID
} fcgi_env_t;

char *obix_fcgi_get_requester_id(obix_request_t *request);

void obix_fcgi_request_destroy(FCGX_Request *request);

#endif

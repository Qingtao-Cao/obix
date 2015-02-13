/* *****************************************************************************
 * Copyright (c) 2013-2015 Qingtao Cao
 * Copyright (c) 2009 Andrey Litvinov
 *
 * This file is part of oBIX
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

#ifndef CURL_EXT_H_
#define CURL_EXT_H_

#include <libxml/tree.h>
#include <curl/curl.h>
#include "list.h"

/*
 * A set of quantums that store scattered chunk of data received by libcurl.
 * A curl handle will have a list of qset that would be able to accommodate
 * several GB amount of data(such as the query result from history facility)
 */
typedef struct qset {
	char **data;			/* Array of quantum pointers */
	struct list_head list;
} qset_t;

/*
 * Prototype of libcurl's callbacks
 */
typedef size_t (*curl_cb_t)(char *, size_t, size_t, void *);

/*
 * Defines a handle for HTTP client, which wraps CURL handle.
 */
typedef struct
{
    /* CURL handle */
    CURL *curl;

    /*
	 * A list of scattered quantum set for storing incoming data.
	 *
	 * More quantums and quantum sets would be allocated on-the-fly
	 * to accommodate response data sent by peer. However, they
	 * would not be released after a transfer completes, so as to
	 * be recycled later.
	 */
	struct list_head data;

	/*
	 * The amount of data that has been received, which indicates
	 * the size of the response from oBIX server once the current
	 * transfer completes, or the position that newly arrived data
	 * should be written to in relevant callback.
	 */
	long input_pos;

	/*
	 * The value of the Content-Length header returned by oBIX server
	 * for the current request, so that libcurl can have a precise
	 * idea on the amount of incoming data.
	 *
	 * If curl_easy_perform returns successfully, cl should have the
	 * same value as input_pos. However, if it returns pre-maturely
	 * due to timedout, less data would have been received.
	 */
	long cl;

	/* The size of a quantum */
	int quantum;

	/* The number of quantums contained in a qset */
	int qset_size;

	/*
	 * Points to a consecutive memory region with received data
	 * terminated with NULL byte. If the whole string could be
	 * accommodated within one quantum(the very first quantum)
	 * then the separated flag is cleared otherwise it is set.
	 */
	char *inputBuffer;
	int separated;

	/* Buffer for storing sending data.*/
	const char *outputBuffer;

	/* size of output data */
	int outputSize;

	/* number of bytes already sent */
	int outputPos;

	/* buffer for storing CURL error messages.*/
	char *errorBuffer;

	/*
	 * The times out threshold for current CURL handle. Zero means
	 * never times out.
	 */
	int timeout;

	/*
	 * Indicator whether to disable the use of signal in timeout
	 * during DNS lookup
	 */
	int nosignal;

	/*
	 * Callbacks invoked by libcurl to copy data from application's
	 * output buffer and to save received data from peer into
	 * application's input buffer respectively
	 */
	curl_cb_t read;
	curl_cb_t write;

	/*
	 * Callback to receive HTTP headers
	 */
	curl_cb_t header;
} CURL_EXT;

int curl_ext_init(void);
void curl_ext_dispose(void);

int curl_ext_create(CURL_EXT **, const int, const int, const int);
void curl_ext_free(CURL_EXT *);

int curl_ext_get(CURL_EXT *, const char *);
int curl_ext_put(CURL_EXT *, const char *);
int curl_ext_post(CURL_EXT *, const char *);

int curl_ext_getDOM(CURL_EXT *, const char *, xmlDoc **);
int curl_ext_putDOM(CURL_EXT *, const char *, xmlDoc **);
int curl_ext_postDOM(CURL_EXT *, const char *, xmlDoc **);

/*
 * Get a consecutive memory region that holds all received data and
 * append a NULL terminator.
 *
 * Callers need to explicitly invoke this function before accessing
 * received data via the inputBuffer pointer, however, they don't
 * have to worry about free inputBuffer after use.
 */
int curl_get_data(CURL_EXT *, char **, int *);

/*
 * Save all incoming data stored in a scattered manner into a file
 */
int curl_save_data(CURL_EXT *, int);

#endif /* CURL_EXT_H_ */

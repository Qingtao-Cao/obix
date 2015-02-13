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

#include <stdlib.h>
#include <string.h>
#include <unistd.h>			/* sysconf */
#include <errno.h>
#include "log_utils.h"
#include "curl_ext.h"
#include "obix_utils.h"
#include "xml_utils.h"

/*
 * DEBUG_CURL is defined in CMAKE_C_FLAGS_DEBUG, however,
 * they may easily become annoying and overwhelm the whole
 * debug log
 */
#ifdef DEBUG_CURL
#undef DEBUG_CURL
#endif

#define REQUEST_HTTP_PUT 0
#define REQUEST_HTTP_POST 1

static struct curl_slist* _header;

static const char *HTTP_CONTENT_LENGTH_HEADER =
"Content-Length:";

/*
 * Decide quantum size, which should be mulitple of system's page
 * size.
 *
 * It's a double-edged sword to increase quantum size. On the one
 * hand, the larger it is, the less chances to invoke malloc/free
 * and the less number of write() system calls. On the other hand
 * the more memory fragments inside a quantum and the more likely
 * allocation may fail in adverse memory conditions.
 *
 * According to testing results 2 consecutive pages seem able to
 * strike a best balance.
 *
 * For other CURL handles designated for other operations responses
 * from oBIX server would be around several hundred bytes, so 1/4
 * of a page will suffice in most cases.
 */
static int quantum_size(int bulky)
{
	long page_size = sysconf(_SC_PAGESIZE);

	return (bulky == 1) ? (page_size * 2) : (page_size / 4);
}

/*
 * Walk through the list of qset until reaching the #n one.
 * Create any missing qset if needed
 *
 * Return the address of the specified qset on success, NULL otherwise
 *
 * Note,
 * 1. The first qset in the list is indexed from 0.
 */
static qset_t *qset_follow(CURL_EXT *h, int n)
{
	qset_t *qs;

	if (list_empty(&h->data)) {
		qs = (qset_t *)malloc(sizeof(qset_t));
		if (!qs)
			return NULL;

		qs->data = NULL;
		list_add_tail(&qs->list, &h->data);
	} else
		qs = list_first_entry(&h->data, qset_t, list);

	while (n--) {
		if (qs == list_last_entry(&h->data, qset_t, list)) {
			qs = (qset_t *)malloc(sizeof(qset_t));
			if (!qs)
				return NULL;

			qs->data = NULL;
			list_add_tail(&qs->list, &h->data);
		} else
			qs = list_next_entry(qs, list);
	}

	return qs;
}

static void qset_trim(CURL_EXT *h)
{
	int i;
	qset_t *pos, *n;

	list_for_each_entry_safe(pos, n, &h->data, list) {
		if (pos->data) {
			for (i = 0; i < h->qset_size; i++) {
					if (pos->data[i])
						free(pos->data[i]);
			}

			free(pos->data);
		}

		free(pos);
	}

	h->input_pos = 0;
}

/*
 * Get a consecutive memory region that holds all received data and
 * append a NULL terminator (so that they could be passed on to XML
 * parser to generate DOM tree from it)
 *
 * If input_pos + 1 <= quantum size, then the very first quantum will
 * be used directly with NULL terminator appended to received data.
 * Otherwise, a separate memory region will be allocated to
 * concatenate scattered data altogether.
 *
 * NOTE: users would have to call this function before manipulating
 * inputBuffer member in current curl handle.
 */
int curl_get_data(CURL_EXT *h, char **data, int *size)
{
	qset_t *qs;
	int node, node_size;
	int i, j;
	long num, copied, to_read;

	*data = NULL;
	*size = 0;

	if (h->input_pos == 0) {
		return 0;
	}

	if (h->separated == 1) {
		h->separated = 0;
		free(h->inputBuffer);
	}

	if (h->input_pos + 1 <= h->quantum) {
		qs = list_first_entry(&h->data, qset_t, list);
		h->inputBuffer = (char *)(qs->data[0]);
	} else {
		if (!(h->inputBuffer = (char *)malloc(h->input_pos + 1))) {
			return -1;
		}

		h->separated = 1;

		node_size = h->quantum * h->qset_size;
		node = h->input_pos / node_size;
		num = h->input_pos;

		for (i = 0, copied = 0; i <= node && copied < h->input_pos; i++) {
			qs = qset_follow(h, i);

			for (j = 0; j < h->qset_size; j++) {
				to_read = (num <= h->quantum) ? num : h->quantum;
				memcpy(h->inputBuffer + copied, qs->data[j], to_read);

				copied += to_read;
				num -= to_read;

				if (copied == h->input_pos) {
					break;
				}
			}
		}
	}

	*(h->inputBuffer + h->input_pos) = '\0';

	if (strlen(h->inputBuffer) != h->input_pos) {
		log_warning("CURL Handle inputBuffer strlen differs from input_pos!"
					"%d vs %d", strlen(h->inputBuffer), h->input_pos);
	}

	*data = h->inputBuffer;
	*size = h->input_pos;

	return 0;
}

/*
 * Save received data into an already-opened file on the hard drive.
 *
 * Larger quantum size will definitely help promote the performance
 * of this function by cutting back the number of write() syscalls.
 */
int curl_save_data(CURL_EXT *h, int fd)
{
	int node, node_size;
	int i, j;
	long num, to_read, copied;
	qset_t *qs;

	node_size = h->quantum * h->qset_size;
	node = h->input_pos / node_size;
	num = h->input_pos;

	for (i = 0, copied = 0; i <= node && copied < h->input_pos; i++) {
		qs = qset_follow(h, i);

		for (j = 0; j < h->qset_size; j++) {
			to_read = (num <= h->quantum) ? num : h->quantum;

			if (write(fd, qs->data[j], to_read) < to_read) {
				return -1;
			}

			copied += to_read;
			num -= to_read;

			if (copied == h->input_pos)
				break;
		}
	}

	return 0;
}

/*
 * Get the value of the Content-Length header, if it exists.
 *
 * Note,
 * 1. Do not assume inputData passed in by libcurl is NULL
 * terminated, therefore a separate memory buffer would have
 * to be used.
 */
static size_t get_cl(char *inputData,
					 size_t size,
					 size_t nmemb,
					 void *arg)
{
	char *buf, *s, *endptr;
	CURL_EXT *h = (CURL_EXT *)arg;
	size_t dsize = size * nmemb;	/* size of received data */

	if (!(buf = (char *)malloc(dsize + 1))) {
		log_error("Failed to allocate buffer to save header");
		return -1;
	}

	memcpy(buf, inputData, dsize);
	buf[dsize] = '\0';

	/* Ignore other HTTP header than Content-Length */
	if (!(s = strstr(buf, HTTP_CONTENT_LENGTH_HEADER)))
		goto out;

	s += strlen(HTTP_CONTENT_LENGTH_HEADER);

	errno = 0;
	h->cl = strtol(s, &endptr,10);
	if ((errno == ERANGE && (h->cl == LONG_MAX || h->cl == LONG_MIN)) ||
		(errno != 0 && h->cl == 0)) {
		log_error("Failed to convert Content-Length header into number");
		goto out;
	}

	/*
	 * For compatibility purpose be lenient on the HTTP header delimiter
	 * by not checking what endptr points to.
	 */

#ifdef DEBUG_CURL
	log_debug("%ld bytes to be received.", h->cl);
#endif

out:
	free(buf);
	return dsize;
}

/**
 * libcurl write callback function. Called by libcurl whenever
 * it has got data from peer that should be saved into input
 * buffer(in fact they are saved in a scattered manner)
 *
 * Note,
 * 1. Return the number of bytes that has been saved. If that
 * differs from the amount passed to the callback, libcurl will
 * regard it as an error and abort the current transfer.
 * 2. The chunk of data passed in by libcurl will not be NULL
 * terminated(despite that inputData is a string pointer instead
 * of a void pointer, however, its strlen is bigger than
 * size * nmemb) and this callback won't concern to append it,
 * since it would be of a policy issue how to manipulate the
 * received data by the user.
 */
static size_t inputWriter(char *inputData,
						  size_t size,
						  size_t nmemb,
						  void *arg)
{
	CURL_EXT *h = (CURL_EXT *)arg;
	size_t dsize = size * nmemb;	/* size of received data */
	size_t num;				/* remaining amount of data to save */
	int node_size;			/* maximum capacity of a quantum set */
	int node, n_pos;		/* index of a quantum set and its internal offset */
	int quantum, q_pos;		/* index of a quantum and its internal offset */
	int room;				/* remaining space within a quantum */
	int copied;				/* iterator among inputData */
	qset_t *qs;
	char **tmp;

	/*
	 * libcurl may receive a number of chunks of data from oBIX server as
	 * response to the last request, thus this callback would be invoked
	 * multiple times. For each invocation inputData will point to different
	 * data region and the copied counter has to be reset so as to append
	 * them from start quantum-size by quantum-size into existing qset_t.
	 *
	 * The input_pos keeps preserved and increased across invocations of
	 * the write callback
	 */
	copied = 0;

	num = dsize;
	node_size = h->quantum * h->qset_size;

	while (num) {
		node = h->input_pos / node_size;
		n_pos = h->input_pos % node_size;
		quantum = n_pos / h->quantum;
		q_pos = n_pos % h->quantum;

		room = h->quantum - q_pos;

#ifdef DEBUG_CURL
		log_debug("[%d,%d], q_pos %d, remaining: %d", node, quantum, q_pos, num);
#endif

		qs = qset_follow(h, node);
		if (!qs) {
			log_error("Failed to allocate qset_t");
			return 0;
		}

		if (!qs->data) {
			qs->data = malloc(h->quantum);
			if (!qs->data) {
				log_error("Failed to allocate quantum pointer array");
				return 0;
			}

			/*
			 * The pointer array must be zeroed out before usage!
			 */
			memset(qs->data, 0, h->quantum);
		}

		tmp = qs->data + quantum;	/* Address of current quantum pointer */
		if (!*tmp) {
			*tmp = (char *)malloc(h->quantum);
			if (!*tmp) {
				log_error("Failed to allocate quantum");
				return 0;
			}
		}

		if (num <= room) {
			memcpy(*tmp + q_pos, inputData + copied, num);
			h->input_pos += num;
			copied += num;
			num = 0;
		} else {
			memcpy(*tmp + q_pos, inputData + copied, room);
			h->input_pos += room;
			copied += room;
			num -= room;
		}
	}

	return dsize;
}

/**
 * libcurl read callback function. Called by libcurl
 * to transfer data from output buffer to its peer.
 *
 * Note,
 * 1. Returning 0 means the end-of-file and will signal libcurl
 * to stop current transfer
 */
static size_t outputReader(char *outputData,
						   size_t size, size_t nmemb,
						   void *arg)
{
	CURL_EXT *handle = (CURL_EXT *)arg;
	size_t bytesToSend = (handle->outputSize - handle->outputPos);

	if (bytesToSend == 0) {
		return 0;
	}

#ifdef DEBUG_CURL
	log_debug("CURL outputReader: sending new data portion. pos=%d; total=%d.",
			  handle->outputPos, handle->outputSize);
#endif

	size = size * nmemb;
	if (size < bytesToSend) {
		bytesToSend = size;
	}

	memcpy(outputData, (handle->outputBuffer + handle->outputPos),
		   bytesToSend);

	handle->outputPos += bytesToSend;

	return bytesToSend;
}

void curl_ext_free(CURL_EXT *h)
{
	if (!h) {
		return;
	}

	if (h->curl) {
		curl_easy_cleanup(h->curl);
	}

	if (h->errorBuffer) {
		free(h->errorBuffer);
	}

	if (h->separated == 1) {
		free(h->inputBuffer);
	}

	qset_trim(h);

	free(h);
}

static CURL_EXT *curl_ext_allocateMemory(int bulky, int timeout, int nosignal)
{
	CURL_EXT *handle;

	/*
	 * It's always a good practice to request resources in the first place
	 * (such as allocating memory).
	 *
	 * The output buffer would be provided by caller before using CURL
	 * while the input buffer would be allocated as needed on the fly during
	 * write callback
	 */
	if (!(handle = (CURL_EXT *)malloc(sizeof(CURL_EXT)))) {
		return NULL;
	}

	if (!(handle->errorBuffer = (char *)malloc(CURL_ERROR_SIZE))) {
		free(handle);
		return NULL;
	}

	*(handle->errorBuffer) = '\0';

	handle->curl = NULL;
	handle->outputBuffer = NULL;
	handle->outputPos = 0;
	handle->outputSize = 0;

	INIT_LIST_HEAD(&handle->data);
	handle->input_pos = 0;
	handle->quantum = quantum_size(bulky);
	handle->qset_size = handle->quantum / sizeof(char *);
	handle->inputBuffer = NULL;
	handle->separated = 0;

	handle->cl = 0;
	handle->timeout = timeout;
	handle->nosignal = nosignal;

	handle->write = inputWriter;
	handle->read = outputReader;
	handle->header = get_cl;

	log_debug("Bulky is %s, quantum size: %d, qset_t size: %d, timeout: %d",
			  bulky, handle->quantum, handle->qset_size, handle->timeout);

	return handle;
}

int curl_ext_init(void)
{
	CURLcode code = curl_global_init(CURL_GLOBAL_DEFAULT);

	if (code != CURLE_OK) {
		log_error("Unable to initialize CURL: %d", code);
		return -1;
	}

	_header = curl_slist_append(_header, "Content-Type: text/xml");
	_header = curl_slist_append(_header, "Expect:");

	return 0;
}

void curl_ext_dispose()
{
	curl_global_cleanup();
	curl_slist_free_all(_header);
	_header = NULL;
}

int curl_ext_create(CURL_EXT **handle, const int bulky,
					const int timeout, const int nosignal)
{
	CURL_EXT *h;
	CURL *curl;
	CURLcode code;

	if (*handle && (*handle)->curl) {		/* already initialised */
		return 0;
	}

	if (!(h = curl_ext_allocateMemory(bulky, timeout, nosignal))) {
		log_error("Unable to allocate memory for CURL handle.");
		goto failed;
	}

	if (!(curl = curl_easy_init())) {
		log_error("Unable to initialize CURL handle.");
		goto failed;
	}

	if ((code = curl_easy_setopt(curl, CURLOPT_ERRORBUFFER,
								 h->errorBuffer)) != CURLE_OK) {
		log_error("Unable to initialize CURL handle: "
					"Failed to set error buffer (%d).", code);
		goto failed;
	}

	if ((code = curl_easy_setopt(curl, CURLOPT_HTTPHEADER,
								 _header)) != CURLE_OK) {
		log_error("Unable to initialize CURL handle: "
					"Failed to set custom header (%d).", code);
		goto failed;
	}

	if ((code = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
								 h->write))	!= CURLE_OK) {
		log_error("Unable to initialize CURL handle: "
					"Failed to set input writer (%d).", code);
		goto failed;
	}

	if ((code = curl_easy_setopt(curl, CURLOPT_WRITEDATA,
								 h)) != CURLE_OK) {
		log_error("Unable to initialize CURL handle: "
					"Failed to set input data (%d).", code);
		goto failed;
	}

	if ((code = curl_easy_setopt(curl, CURLOPT_READFUNCTION,
								 h->read)) != CURLE_OK) {
		log_error("Unable to initialize CURL handle: "
					"Failed to set output reader (%d).", code);
		goto failed;
	}

	if ((code = curl_easy_setopt(curl, CURLOPT_READDATA,
								 h)) != CURLE_OK) {
		log_error("Unable to initialize CURL handle: "
					"Failed to set output data (%d).", code);
		goto failed;
	}

	if ((code = curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION,
								 h->header)) != CURLE_OK) {
		log_error("Unable to initialize CURL handle: "
					"Failed to set get header func (%d).", code);
		goto failed;
	}

	if ((code = curl_easy_setopt(curl, CURLOPT_WRITEHEADER,
								 h)) != CURLE_OK) {
		log_error("Unable to initialize CURL handle: Failed to set get "
				  "argument to get header func (%d).", code);
		goto failed;
	}

	if (h->timeout > 0 && (code = curl_easy_setopt(curl, CURLOPT_TIMEOUT,
												   h->timeout)) != CURLE_OK) {
		log_error("Unable to initialize CURL handle: Failed to set "
				  "timeout threshold (%d).", code);
		goto failed;
	}

	if ((code = curl_easy_setopt(curl, CURLOPT_NOSIGNAL,
								 h->nosignal)) != CURLE_OK) {
		log_error("Unable to initialize CURL handle: Failed to set "
				  "nosignal option (%d).", code);
		goto failed;
	}

	h->curl = curl;
	*handle = h;

    return 0;

failed:
	curl_ext_free(h);

	*handle = NULL;
	return -1;
}

/**
 * Helper function which performs actual HTTP request
 * assumes that type of request was already set by a caller.
 */
static int sendRequest(CURL_EXT* handle, const char* uri)
{
	CURLcode code = curl_easy_setopt(handle->curl, CURLOPT_URL, uri);
	if (code != CURLE_OK) {
		log_error("Unable to initialize HTTP request: "
					"Failed to set URL (%d).", code);
		return -1;
	}

	/*
	 * Reset cl and input_pos before sending new requests, while
	 * existing qset_t would be recycled to save the response of
	 * the new requests.
	 */
	handle->cl = handle->input_pos = 0;

	code = curl_easy_perform(handle->curl);

	/* Allow empty response from oBIX server */
	if ((code != CURLE_OK) && (code != CURLE_GOT_NOTHING)) {
		log_error("HTTP request to \"%s\" failed (%d): %s.",
					uri, code, handle->errorBuffer);
		return -1;
	}

	/*
	 * Not care about the amount of data received if no
	 * Content-Length header available or failed to get
	 * its value for whatever reason.
	 */
	if (handle->cl > 0 && handle->cl != handle->input_pos) {
		log_error("HTTP Content-Length header: %ld whereas %ld bytes received",
					handle->cl, handle->input_pos);
		return -1;
	}

	return 0;
}

int curl_ext_get(CURL_EXT *handle, const char *uri)
{
	CURLcode code;

	if (!handle || !uri) {
		log_error("Illegal parameter provided");
		return -1;
	}

	if ((code = curl_easy_setopt(handle->curl, CURLOPT_HTTPGET, 1L)) != CURLE_OK) {
		log_error("Unable to initialize HTTP GET request: "
				  "Failed to switch to GET (%d).", code);
		return -1;
	}

	return sendRequest(handle, uri);
}

int curl_ext_put(CURL_EXT* handle, const char* uri)
{
	CURLcode code;

	if (!handle || !uri) {
		log_error("Illegal parameter provided");
		return -1;
	}

	if ((code = curl_easy_setopt(handle->curl, CURLOPT_UPLOAD, 1L)) != CURLE_OK) {
		log_error("Unable to initialize HTTP PUT request: "
				  "Failed to switch to PUT (%d).", code);
		return -1;
	}

	handle->outputPos = 0;
	if (handle->outputBuffer == NULL) {
		log_error("Trying to perform PUT request with empty body.");
		return -1;
	}
	handle->outputSize = strlen(handle->outputBuffer);

	if ((code = curl_easy_setopt(handle->curl, CURLOPT_INFILESIZE,
								 handle->outputSize)) != CURLE_OK) {
		log_error("Unable to initialize HTTP PUT request: "
				  "Failed to set output size (%d).", code);
		return -1;
	}

	return sendRequest(handle, uri);
}

int curl_ext_post(CURL_EXT *handle, const char *uri)
{
	CURLcode code;

	if (!handle || !uri) {
		log_error("Illegal parameter provided");
		return -1;
	}

	if ((code = curl_easy_setopt(handle->curl, CURLOPT_UPLOAD, 0L)) != CURLE_OK) {
		log_error("Unable to initialize HTTP UPLOAD request: "
				  "Failed to switch of upload (%d).", code);
		return -1;
	}

	if ((code = curl_easy_setopt(handle->curl, CURLOPT_POST, 1L)) != CURLE_OK) {
		log_error("Unable to initialize HTTP POST request: "
				  "Failed to switch to POST (%d).", code);
		return -1;
	}

	handle->outputPos = 0;

	if (!handle->outputBuffer) {
		handle->outputSize = 0;
	} else {
		handle->outputSize = strlen(handle->outputBuffer);
	}

	if ((code = curl_easy_setopt(handle->curl, CURLOPT_POSTFIELDSIZE,
								 handle->outputSize)) != CURLE_OK) {
		log_error("Unable to initialize HTTP POST request: "
				  "Failed to set output size (%d).", code);
		return -1;
	}

	return sendRequest(handle, uri);
}

/**
 * Helper function for parsing received response at input buffer of provided
 * handle.
 * @return @a 0 on success; @a -1 on error.
 */
static int parseXmlInput(CURL_EXT *h, xmlDoc **doc)
{
	char *data;
	int size;

	*doc = NULL;

	if (h->input_pos == 0) {
		/* we do not consider empty answer as an error */
		return 0;
	}

	if (curl_get_data(h, &data, &size) < 0) {
		log_error("Failed to allocate consecutive memory to assemble response");
		return -1;
	}

	if (!(*doc = xmlReadMemory(data, size, NULL, NULL,
							   XML_PARSE_OPTIONS_COMMON))) {
		log_error("Server response is not an XML document:\n%s",
				  h->inputBuffer);
		return -1;
	}

	return 0;
}

int curl_ext_getDOM(CURL_EXT *handle, const char *uri, xmlDoc **doc)
{
	if (curl_ext_get(handle, uri) < 0) {
		return -1;
	}

	return parseXmlInput(handle, doc);
}

int curl_ext_putDOM(CURL_EXT* handle, const char* uri, xmlDoc **doc)
{
	if (curl_ext_put(handle, uri) < 0) {
		return -1;
	}

	return parseXmlInput(handle, doc);
}

int curl_ext_postDOM(CURL_EXT* handle, const char* uri, xmlDoc **doc)
{
	if (curl_ext_post(handle, uri) < 0) {
		return -1;
	}

	return parseXmlInput(handle, doc);
}

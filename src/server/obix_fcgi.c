/* *****************************************************************************
 * Copyright (c) 2013-2015 Qingtao Cao
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
 * along with oBIX. If not, see <http://www.gnu.org/licenses/>.
 *
 * *****************************************************************************/

#include <ctype.h>
#include <pthread.h>
#include "obix_fcgi.h"
#include "log_utils.h"
#include "server.h"
#include "xml_utils.h"
#include "xml_config.h"
#include "obix_utils.h"
#include "xml_utils.h"

/*
 * This macro will be enabled in the debug version image built for test purpose.
 * The oBIX server threads will shut down after receiving a certain amount of
 * FCGX requests so as to have valgrind validate whether they can exit cleanly.
 */
#ifdef DEBUG_VALGRIND
#define MAX_REQUESTS_SERVED		(-1)
#endif

#undef SYNC_FCGX_ACCEPT

obix_fcgi_t *__fcgi;

static char *fcgi_envp[] = {
	[FCGI_ENV_REQUEST_URI] = "REQUEST_URI",
	[FCGI_ENV_REQUEST_METHOD] = "REQUEST_METHOD",
	[FCGI_ENV_REMOTE_PORT] = "REMOTE_PORT",
	[FCGI_ENV_REMOTE_ADDR] = "REMOTE_ADDR",
	[FCGI_ENV_REQUESTER_ID] = "REQUESTER_ID"
};

static const char *FCGI_ENV_REQUEST_METHOD_GET = "GET";
static const char *FCGI_ENV_REQUEST_METHOD_PUT = "PUT";
static const char *FCGI_ENV_REQUEST_METHOD_POST = "POST";

static const char *FCGI_DEF_REQUESTER_ID = "UNDEFINED:UNDEFINED";

static const char *SERVER_CONFIG_FILE = "server_config.xml";

static const char *HTTP_STATUS_OK =
"Status: 200 OK\r\n"
"Content-Type: text/xml\r\n";

static const char *HTTP_CONTENT_LOCATION = "Content-Location: %s\r\n";
static const char *HTTP_CONTENT_LENGTH = "Content-Length: %lu\r\n";
static const char *HTTP_HEADER_SEPARATOR = "\r\n";

char *obix_fcgi_get_requester_id(obix_request_t *request)
{
	const char *val;

	if (!(val = FCGX_GetParam(fcgi_envp[FCGI_ENV_REQUESTER_ID],
							  request->request->envp))) {
		val = FCGI_DEF_REQUESTER_ID;
	}

	return strdup(val);
}

/*
 * Decodes a URL-Encoded string.
 * See http://www.w3schools.com/tags/ref_urlencode.asp
 */
static void obix_fcgi_url_decode(char *dst, const char *src)
{
	char a, b;

	while (*src) {
		if ((*src == '%') &&
			((a = src[1]) && (b = src[2])) &&
			(isxdigit(a)  && isxdigit(b))) {
			if (a >= 'a') {
				a -= 'a' - 'A';
			}

			if (a >= 'A') {
				a -= ('A' - 10);
			} else {
				a -= '0';
			}

			if (b >= 'a') {
				b -= 'a' - 'A';
			}

			if (b >= 'A') {
				b -= ('A' - 10);
			} else {
				b -= '0';
			}

			*dst++ = 16 * a + b;
			src += 3;
		} else {
			*dst++ = *src++;
		}
	}

	*dst++ = '\0';
}

static void obix_fcgi_exit(void)
{
	obix_server_exit();

	FCGX_ShutdownPending();
	FCGX_Finish();

	if (__fcgi) {
		pthread_mutex_destroy(&__fcgi->mutex);

		if (__fcgi->id) {
			free(__fcgi->id);
		}

		free(__fcgi);
		__fcgi = NULL;
	}

	log_debug("FCGI connection has been shutdown");
}

static void obix_fcgi_send_response(obix_request_t *request)
{
	FCGX_Request *fcgiRequest = request->request;
	response_item_t *item, *n;
	long len = obix_request_get_response_len(request);
	int items = obix_request_get_response_items(request);
	int i = 0;
	const char *response_uri;

	/* Header section: HTTP/1.1 200 OK */
	if (FCGX_FPrintF(fcgiRequest->out, "%s", HTTP_STATUS_OK) == EOF) {
		log_error("Failed to send HTTP_STATUS_OK header");
		goto failed;
	}

	/*
	 * Header section: content-location
	 *
	 * If response_uri is not set, then fall back on the decoded request_uri.
	 * This way, handlers can have a chance to specify another URI as the
	 * Content-Location, for example, for newly generated history facilities
	 * or watch objects
	 *
	 * NOTE: in case the decoded request_uri is NULL, e.g., when the requested
	 * uri fails to be read from FCGI channel in the first place or failed to
	 * allocate a string for the decoded one, then no content-location header
	 * could ever be provided
	 */
	response_uri = (request->response_uri) ? (char *)request->response_uri :
											 request->request_decoded_uri;
	if (response_uri &&
		FCGX_FPrintF(fcgiRequest->out, HTTP_CONTENT_LOCATION,
					 response_uri) == EOF) {
		log_error("Failed to write HTTP \"Content-Location\" header");
		goto failed;
	}

	if (len > 0) {
		if (FCGX_FPrintF(fcgiRequest->out, HTTP_CONTENT_LENGTH, len) == EOF) {
			log_error("Failed to write HTTP \"Content-Length\" header");
			goto failed;
		}
	}

	/* Separate headers from response body */
	if (FCGX_FPrintF(fcgiRequest->out, "%s", HTTP_HEADER_SEPARATOR) == EOF) {
		log_error("Failed to write delimiter after HTTP headers");
		goto failed;
	}

	pthread_mutex_lock(&request->mutex);
	list_for_each_entry_safe(item, n, &request->response_items, list) {
		list_del(&item->list);
		pthread_mutex_unlock(&request->mutex);

		/*
		 * Now that the current item has been dequeued, mutex could be
		 * safely dropped during lengthy operations
		 */
		if (FCGX_FPrintF(fcgiRequest->out, "%s", item->body) == EOF) {
			goto failed;
		}

		i++;

		/*
		 * Take advantage of this chance to have the response item
		 * removed as well
		 */
		obix_request_destroy_response_item(item);
		pthread_mutex_lock(&request->mutex);
	}
	pthread_mutex_unlock(&request->mutex);

	/* Fall through */

failed:
	if (i < items) {
		log_warning("%d out of %d response items(%ld bytes in total) have NOT "
					"been sent due to FCGI error", items - i, items, len);
	}
}

/*
 * Initialise the FCGX channel
 *
 * Return the address of an obix_fcgi_t structure
 */
static obix_fcgi_t *obix_fcgi_init(xml_config_t *config)
{
	obix_fcgi_t *fcgi;
	char *sock;
	int backlog, ret, multi_threads;

	if (!(sock = xml_config_get_str(config, XP_LISTEN_SOCKET)) ||
		(backlog = xml_config_get_int(config, XP_LISTEN_BACKLOG)) < 0 ||
		(multi_threads = xml_config_get_int(config, XP_MULTI_THREADS)) < 0) {
		log_error("Failed to get server's FCGX settings");
		goto failed;
	}

	if (!(fcgi = (obix_fcgi_t *)malloc(sizeof(obix_fcgi_t))) ||
		!(fcgi->id = (pthread_t *)malloc(sizeof(pthread_t) * multi_threads))) {
		log_error("Failed to allocate an obix_fcgi_t structure");
		goto mem_failed;
	}

	fcgi->multi_threads = multi_threads;
	fcgi->send_response = obix_fcgi_send_response;
	pthread_mutex_init(&fcgi->mutex, NULL);

	if ((ret = FCGX_Init()) != 0) {
		log_error("Failed to initialize FCGX channel: %d", ret);
		goto init_failed;
	}

	if ((fcgi->fd = FCGX_OpenSocket(sock, backlog)) < 0) {
		log_error("Failed to create FCGX listen socket");
		goto fcgi_failed;
	}

	if ((ret = obix_server_init(config)) < 0) {
		log_error("Failed to initialise oBIX server");
		goto fcgi_failed;
	}

	log_debug("\"/proc/%u/fd\" and strace illustrate how server threads "
			  "use FCGX listen socket and established connections", get_tid());

	free(sock);
	return fcgi;

fcgi_failed:
	FCGX_ShutdownPending();
	FCGX_Finish();

init_failed:
#ifdef SYNC_FCGX_ACCEPT
	pthread_mutex_destroy(&fcgi->mutex);
#endif

	free(fcgi->id);

mem_failed:
	if (fcgi) {
		free(fcgi);
	}

failed:
	if (sock) {
		free(sock);
	}

	return NULL;
}

static xmlDoc *obix_fcgi_read(FCGX_Request *request)
{
	static const int chunkSize = 2048; /** read 2KiB chunks */
	xmlParserCtxt *parserContext = NULL;
	xmlDoc *document = NULL;
	char chunk[chunkSize];
	int bytesRead = 0;

	memset(chunk, 0, chunkSize);

	bytesRead = FCGX_GetStr(chunk, chunkSize, request->in);

	parserContext = xmlCreatePushParserCtxt(NULL, NULL, chunk, chunkSize, NULL);
	if (parserContext == NULL) {
		log_error("Failed to allocate an XML Push parser context");
		return NULL;
	}

	/* No XML_PARSE_NODICT applied, see comments above */
	xmlCtxtUseOptions(parserContext, XML_PARSE_OPTIONS_COMMON);

	while ((bytesRead = FCGX_GetStr(chunk, chunkSize, request->in)) > 0) {
		xmlParseChunk(parserContext, chunk, bytesRead, 0); /* Non-terminating */

		if (bytesRead < chunkSize) {
			break;		/** eof has been reached */
		}
	}

	xmlParseChunk(parserContext, chunk, 0, 1); /** terminate the stream */
	document = parserContext->myDoc;

#if 0
	/* TODO:
	 * So far all input XML doc generated are NOT well formed, regardless of
	 * whether XML header is available or not. Worse still, if it is there,
	 * the parsed myDoc would be somehow nullified!
	 */
	if (parserContext->wellFormed == 0) {
		log_warning("Parsed XML doc NOT well formed!: %d", parserContext->errNo);
	}
#endif

	xmlFreeParserCtxt(parserContext); /** does not free myDoc ptr */

	return document;
}

static void obix_handle_request(obix_request_t *request)
{
	FCGX_Request *fcgiRequest = request->request;
	xmlDoc *doc = NULL;
	const char *requestType;

	if (!(request->request_uri = FCGX_GetParam(fcgi_envp[FCGI_ENV_REQUEST_URI],
											   fcgiRequest->envp)) ||
		slash_preceded(request->request_uri) == 0 ||			/* not started with slash */
		slash_preceded(request->request_uri + 1) == 1) {        /* double slashes */
		log_error("Invalid URI env in current request: %s",
				  (request->request_uri) ? request->request_uri : "(null)");
		obix_server_handleError(request, "Invalid URI");
		return;
	}

	if (!(request->request_decoded_uri =
				(char *)malloc(strlen(request->request_uri) + 1))) {
		log_error("Could not allocate enough memory to decode the input URI");
		obix_server_handleError(request, "Failed to decode input URI");
		return;
	}

	obix_fcgi_url_decode(request->request_decoded_uri, request->request_uri);

	if (!(requestType = FCGX_GetParam(fcgi_envp[FCGI_ENV_REQUEST_METHOD],
									  fcgiRequest->envp))) {
		log_error("Invalid METHOD env in current request: %s", requestType);
		obix_server_handleError(request, "Missing HTTP verb");
		return;
	}

	if (strcmp(requestType, FCGI_ENV_REQUEST_METHOD_GET) == 0) {
		obix_server_handleGET(request);
	} else if (strcmp(requestType, FCGI_ENV_REQUEST_METHOD_PUT) == 0) {
		doc = obix_fcgi_read(fcgiRequest);
		obix_server_handlePUT(request, doc);
		if (doc) {
			xmlFreeDoc(doc);
		}
	} else if (strcmp(requestType, FCGI_ENV_REQUEST_METHOD_POST) == 0) {
		doc = obix_fcgi_read(fcgiRequest);
		obix_server_handlePOST(request, doc);
		if (doc) {
			xmlFreeDoc(doc);
		}
	} else {
		obix_server_handleError(request, "Illegal HTTP verb");
	}
}

void obix_fcgi_request_destroy(FCGX_Request *request)
{
	if (!request) {
		return;
	}

	FCGX_Finish_r(request);
	FCGX_Free(request, 1);

	free(request);
}

static FCGX_Request *obix_fcgi_request_create(obix_fcgi_t *fcgi)
{
	FCGX_Request *request;
	int ret;

	if (!(request = (FCGX_Request *)malloc(sizeof(FCGX_Request)))) {
		log_error("Failed to create FCGX Request structure");
		return NULL;
	}

	if ((FCGX_InitRequest(request, fcgi->fd, 0)) != 0) {
		log_error("Failed to initialize the FCGX request");
		goto failed;
	}

#ifdef SYNC_FCGX_ACCEPT
	pthread_mutex_lock(&__fcgi->mutex);
#endif
	ret = FCGX_Accept_r(request);
#ifdef SYNC_FCGX_ACCEPT
	pthread_mutex_unlock(&__fcgi->mutex);
#endif

	if (ret == 0) {
		return request;
	}

	log_error("Failed to accept FCGX request, returned %d", ret);

	/* Fall through */

failed:
	obix_fcgi_request_destroy(request);
	return NULL;
}

/*
 * The payload for each oBIX server thread, which is basically
 * accepting pending FCGI request and invoking a proper handle to
 * take care of that repeatedly
 *
 * Note:
 * 1. this function should never return unless on errors.
 *
 * 2. the FCGI website suggests that on some platform there is a
 * need to serialize the accept(), if that is the case, we need
 * to pass in a pointer to a common mutex that all threads need
 * to compete to grab during the invocation of accept().
 */
static void *payload(void *arg)
{
	FCGX_Request *fcgiRequest = NULL;
	obix_request_t *request = NULL;
	obix_fcgi_t *fcgi = (obix_fcgi_t *)arg;

#ifdef DEBUG_VALGRIND
	int count = 0;
#endif

	while (1) {

#ifdef DEBUG_VALGRIND
		if ((MAX_REQUESTS_SERVED > 0) && (count++ == MAX_REQUESTS_SERVED)) {
			log_debug("Threshold %d reached, [%u] exiting...",
					  MAX_REQUESTS_SERVED, get_tid());
			fcgiRequest = NULL;		/* avoid double-free */
			break;
		}
#endif

		if (!(fcgiRequest = obix_fcgi_request_create(fcgi))) {
			log_error("Failed to create FCGI Request structure");
			break;
		}

		if (!(request = obix_request_create(fcgiRequest))) {
			log_error("Failed to create Response structure due to no memory");
			break;
		}

		obix_handle_request(request);

		/*
		 * The [request, response] pair will be released regardless on error
		 * conditions or after response is sent out, which may take place in an
		 * asynchronous manner
		 */
	}

	if (fcgiRequest) {
		obix_fcgi_request_destroy(fcgiRequest);
	}

	return NULL;
}

/**
 * Entry point of the oBIX server
 */
int main(int argc, char **argv)
{
	xml_config_t *config;
	int i;

	if (argc != 2) {
		printf("Usage: %s <resource-dir>\n"
			   "Where resrouce-dir is the folder containing all oBIX "
			   "configuration and data files\n", argv[0]);
	}

	xml_parser_init();

	if (!(config = xml_config_create(argv[1], SERVER_CONFIG_FILE))) {
		printf("Failed to create xml_config_t for %s\n", SERVER_CONFIG_FILE);
		goto failed;
	}

	/*
	 * Setup log facility so that log utility APIs
	 * can be used as early as possible
	 */
	if (xml_config_log(config) < 0) {
		printf("Failed to config server log\n");
		goto log_failed;
	}

	if (!(__fcgi = obix_fcgi_init(config))) {
		log_error("Failed to initialise FCGX channel");
		goto log_failed;
	}

	for (i = 0; i < __fcgi->multi_threads; i++) {
		if (pthread_create(__fcgi->id + i, NULL, payload, (void *)__fcgi) != 0) {
			log_warning("Failed to start thread%d", i);
		}
	}

	for (i = 0; i < __fcgi->multi_threads; i++) {
		if (pthread_join(__fcgi->id[i], NULL) != 0) {
			log_warning("Failed to join thread%d and it could be left zombie", i);
		}
	}

	obix_fcgi_exit();

	/* Fall through */

log_failed:
	xml_config_free(config);

failed:
	xml_parser_exit();
	return -1;
}

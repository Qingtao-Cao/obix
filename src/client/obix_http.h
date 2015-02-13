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

#ifndef OBIX_HTTP_H_
#define OBIX_HTTP_H_

#include <libxml/tree.h>
#include "ptask.h"
#include "obix_client.h"

/*
 * HTTP specific settings for a Connection
 */
typedef struct
{
	/* Pointing back to higher level, oBIX Connection descriptor */
	Connection *p;

	/*
	 * Generic CURL handle for this connection used by the main
	 * thread of oBIX client application and the mutex to help
	 * serialise the usage of it
	 */
	CURL_EXT *handle;
	pthread_mutex_t curl_mutex;

	/* Attributes for the generic CURL handle */
	int timeout;
	int bulky;
	int nosignal;

	/* Polling intervals, and attributes for the long-poll intervals */
	long poll_int;
	long poll_min;
	long poll_max;

	/* The IP address of the oBIX server */
	char *ip;

	/*
	 * Absolute URIs of various public facilities on the oBIX server
	 * The lobby URI is set in the configuration file, while the rest
	 * are read from the lobby facility on the oBIX server
	 *
	 * NOTE: all URIs are prefixed by server IP so as to be used
	 * by CURL API directly
	 *
	 * NOTE: facilities URIs of a particular device reside in the
	 * device descriptor
	 *
	 * The CURL handles used when accessing relevant hrefs are listed
	 * as well, which should be ensured to be used in an atomic manner
	 */
	char *lobby;				/* hc->handle */
	char *signup;				/* hc->handle */
	char *signoff;				/* hc->handle */
	char *batch;				/* user defined or hc->handle */
	char *hist_get;				/* user defined or hc->handle */
	char *watch_make;			/* hd->watch_handle */
} Http_Connection;

/*
 * HTTP specific settings for a Device
 */
typedef struct
{
	/* Pointing back to higher level, oBIX Device descriptor */
	Device *p;

	/* Absolute href of the device, not prefixed by server IP address */
	char *href;

	/*
	 * The CURL handle used during creation/deletion of watch object
	 * and listeners (watchItem equivalent) so that each device can
	 * manipulate its own watch and listeners without impacting
	 * others
	 *
	 * NOTE: the curl_mutex should be grabbed to ensure this handle
	 * is used in an atomic manner
	 */
	CURL_EXT *watch_handle;
	pthread_mutex_t curl_mutex;

	/*
	 * The CURL handle used exclusively by the polling thread for
	 * this Device. Due to the adoption of the long poll mode
	 * watches, the Watch.pollChanges requests will block for a
	 * long time, therefore a separate CURL handle than the above
	 * watch_handle should be used
	 *
	 * NOTE: the polling thread uses this handle exclusively, therefore
	 * no mutex is needed
	 */
	CURL_EXT *poll_handle;

	/*
	 * The watch thread used by this device. Created only when the
	 * watch object of this device is actually used for the first
	 * time
	 */
	obix_task_t watch_thread;

	/*
	 * Absolute URIs of device specific facilities on the oBIX server
	 *
	 * NOTE: all URIs are prefixed by server IP so as to be used
	 * by CURL API directly
	 *
	 * The CURL handles used when accessing relevant hrefs are listed
	 * as well, which should be ensured to be used in an atomic manner
	 */
	char *hist_append;				/* user defined or hc->handle */
	char *hist_query;				/* same as above */
	char *hist_index;				/* same as above */

	char *watch_pwi_min;			/* hd->watch_handle */
	char *watch_pwi_max;			/* same as above */
	char *watch_add;				/* same as above */
	char *watch_remove;				/* same as above */
	char *watch_pollRefresh;		/* same as above */
	char *watch_delete;				/* same as above */
	char *watch_pollChanges;		/* hd->poll_handle */
} Http_Device;

typedef struct {
	/* Pointing back to the oBIX Listener descriptor */
	Listener *p;

	/* The absolute href of the monitored sub node of the Device */
	char *href;

	/*
	 * The Indicator whether this Listener has been handled
	 * in one execution of the watch thread
	 */
	int handled;
} Http_Listener;

/* Stack of HTTP communication functions. */
extern const Comm_Stack OBIX_HTTP_COMM_STACK;

int http_setup_connection(xmlNode *, Connection *);
int http_open_connection(Connection *);
void http_destroy_connection(Connection *);

int http_register_device(Device *, const char *);
int http_unregister_device(Device *);
int http_unregister_device_local(Device *);

int http_register_listener(Listener *);
int http_unregister_listener(Listener *);
int http_refresh_listeners(Device *, xmlDoc **);

int http_read(CURL_EXT *, Device *, const char *, xmlDoc **);
int http_read_value(CURL_EXT *, Device *, const char *, xmlChar **);

int http_write(CURL_EXT *, Device *, const char *, const char *);
int http_write_value(CURL_EXT *, Device *, const char *, const char *, OBIX_DATA_TYPE);

int http_send_batch(CURL_EXT *, Batch *);

int http_get_history(CURL_EXT *, Device *);
int http_get_history_index(CURL_EXT *, Device *, xmlDoc **);
int http_append_history(CURL_EXT *, Device *, const char *);
int http_query_history(CURL_EXT *, Device *, const char *, char **, int *);

int is_err_contract(const xmlNode *root);

#endif /* OBIX_HTTP_H_ */

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

#ifndef OBIX_CLIENT_H_
#define OBIX_CLIENT_H_

#include <pthread.h>
#include <libxml/tree.h>
#include "list.h"
#include "curl_ext.h"
#include "obix_utils.h"

/*
 * Standard oBIX data types
 */
typedef enum {
	OBIX_T_BOOL = 0,
	OBIX_T_INT,
	OBIX_T_REAL,
	OBIX_T_STR,
	OBIX_T_ENUM,
	OBIX_T_ABSTIME,
	OBIX_T_RELTIME,
	OBIX_T_URI
} OBIX_DATA_TYPE;

extern const char *OBIX_DATA_TYPE_NAMES[];

/*
 * Defined possible communication methods
 * So far only HTTP binding is supported
 */
typedef enum {
    CONN_TYPE_HTTP = 0
} Connection_Type;

extern const char *conn_type[];

typedef struct _Comm_Stack Comm_Stack;

/*
 * Descriptor for a connection with a particular oBIX server
 */
typedef struct {
	/*
	 * The unique connection ID for this connection
	 *
	 * oBIX client applications should use unique IDs to name after
	 * different connections with different oBIX servers
	 */
	int id;

	/*
	 * The type of binding
	 *
	 * So far only HTTP binding is supported
	 */
	Connection_Type type;

	/* Binding specific operations table */
	const Comm_Stack *comm;

	/*
	 * The list of device descriptors of current connection
	 * with a particular oBIX server
	 */
	struct list_head devices;

	/*
	 * The lock to protect above list of devices
	 */
	pthread_mutex_t mutex;

	/* To join the global connection descriptors list */
	struct list_head list;

	/* Binding specific data structure */
	void *priv;
} Connection;

/*
 * Descriptor for a particular device on a particular connection
 */
typedef struct {
	/* Pointing back to the host Connection */
	Connection *conn;

	/* The unique device name on the host Connection */
	char *name;

	/* The list of listeners related to this device */
	struct list_head listeners;

	/*
	 * The lock to protect above list of listeners from race
	 * conditions between the polling thread of this device
	 * and the run-time requests to add or delete listeners
	 */
	pthread_mutex_t mutex;

	/*
	 * Indicator of whether the watch facilities for this Device
	 * is being deleted
	 */
	int watch_being_deleted;

	/*
	 * The wait queue of the threads that would like to add listeners
	 * to an empty Device.listeners queue, if relevant watch objects
	 * are being deleted, they needs to wait on this queue until the
	 * deletion finishes
	 */
	pthread_cond_t wq;

	/* To join the device descriptors list of a Connection */
	struct list_head list;

	/* Binding specific data structure */
	void *priv;
} Device;

/*
 * The callback function invoked by a device specific polling thread
 * upon reception of notification of changes from the oBIX server
 *
 * The first parameter is the CURL handle that this callback can make
 * use of. Since the polling thread won't temporarily use its own CURL
 * handle after receiving the watchOut contract, it can safely be taken
 * advantage of by the application.
 *
 * The second parameter is the matching node in the watchOut contract
 * returned by oBIX server.
 *
 * The last parameter can be used to point to application specific
 * structures
 */
typedef int (*listener_cb_t)(CURL_EXT *, xmlNode *, void *);

/*
 * Descriptor for a particular subnode of a device contract
 * that is being monitored by a watch object on relevant
 * oBIX server
 *
 * Counterpart to the watchItem structure on the oBIX server
 */
typedef struct {
	/* Pointing to the host Device */
	Device *dev;

	/*
	 * The relative href of the watched upon sub node of the Device
	 *
	 * NOTE: if param equals to "/" the entire Device is monitored
	 */
	char *param;

	/*
	 * See comments above
	 */
	listener_cb_t cb;

	/*
	 * The parameter passed for the above callback function. Normally
	 * speaking it can be a pointer to an application specific descriptor
	 */
	void *arg;

	/* To join the listener descriptors list of a Device */
	struct list_head list;

	/* Binding specific data structure */
	void *priv;
} Listener;

/*
 * Descriptor for a batch object on a particular connection,
 * which may carry a number of commands for different devices
 * on the connection
 *
 * NOTE: a batch object is NOT thread-safe, it should be used
 * by one application thread exclusively in a one-shot manner,
 * that is, to be deleted after each usage
 */
typedef struct {
	/* The target Connection for this batch object */
	Connection *conn;

	/* The list of batch Commands */
	struct list_head cmds;

	/*
	 * The input document for the current batch object, whenever
	 * a new command is added, a new node is inserted into it
	 */
	xmlDoc *in;

	/*
	 * The output document for the current batch object, generated
	 * by CURL API to store the response sent by the oBIX server
	 */
	xmlDoc *out;
} Batch;

/*
 * Descriptor for a particular batch command that targets
 * a particular subnode on a device on a connection
 */
typedef struct {
	/* The target Device */
	Device *dev;

	/* The target parameter (subnode) on the Device */
	char *param;

	/*
	 * The absolute href of the target node
	 *
	 * NOTE: no separation of the bottom-level, HTTP relevant
	 * attribute from high-level, oBIX counterpart is introduced
	 */
	char *href;

	/* Point to relevant node in the batch.out document */
	xmlNode *result;

	/* Join the Batch.cmds list */
	struct list_head list;
} Command;

/*
 * Binding-neutral operations prototypes
 */
typedef int (*comm_setup_connection)(xmlNode *, Connection *);
typedef int (*comm_open_connection)(Connection *);
typedef void (*comm_destroy_connection)(Connection *);

typedef int (*comm_register_device)(Device *, const char *);
typedef int (*comm_unregister_device)(Device *);
typedef int (*comm_unregister_device_local)(Device *);

typedef int (*comm_register_listener)(Listener *);
typedef int (*comm_unregister_listener)(Listener *);
typedef int (*comm_refresh_listeners)(Device *, xmlDoc **);

typedef int (*comm_read)(CURL_EXT *, Device *, const char *, xmlDoc **);
typedef int (*comm_read_value)(CURL_EXT *, Device *, const char *, xmlChar **);
typedef int (*comm_write)(CURL_EXT *, Device *, const char *, const char *);
typedef int (*comm_write_value)(CURL_EXT *, Device *, const char *, const char *, OBIX_DATA_TYPE);

typedef int (*comm_send_batch)(CURL_EXT *, Batch *);

typedef int (*comm_get_history)(CURL_EXT *, Device *);
typedef int (*comm_get_history_index)(CURL_EXT *, Device *, xmlDoc **);
typedef int (*comm_append_history)(CURL_EXT *, Device *, const char *);
typedef int (*comm_query_history)(CURL_EXT *, Device *, const char *, char **, int *);

/*
 * Defines full set of operations, which should be implemented by any
 * communication layer.
 */
struct _Comm_Stack
{
	/*
	 * NOTE: there is no "close" operation for a connection, since
	 * the oBIX server doesn't know or care about the deletion of
	 * a "Connection" descriptor on a client side
	 */
	comm_setup_connection setup_connection;
	comm_open_connection open_connection;
	comm_destroy_connection destroy_connection;

	comm_register_device register_device;
	comm_unregister_device unregister_device;
	comm_unregister_device unregister_device_local;

	comm_register_listener register_listener;
	comm_unregister_listener unregister_listener;
	comm_refresh_listeners refresh_listeners;

	comm_read read;
	comm_read_value read_value;

	comm_write write;
	comm_write_value write_value;

	comm_send_batch send_batch;

	comm_get_history get_history;
	comm_get_history_index get_history_index;
	comm_append_history append_history;
	comm_query_history query_history;
};

/*
 * ID of the default oBIX connection
 *
 * oBIX applications are free to introduce more IDs when
 * more than one oBIX connections are required
 */
#define OBIX_CONNECTION_ID		0

/*
 * IMPORTANT: some APIs expect a CURL handle pointer as the first
 * parameter. Multi-thread applications should manipulate one specific
 * CURL handle for EACH thread since they are not thread-safe, whereas
 * for serialised operations NULL can be passed instead to fall back
 * on the default CURL handle of a connection
 */
int obix_setup_connections(const char *);
int obix_destroy_connection(const int);
int obix_open_connection(const int);
void obix_destroy_connections(void);

int obix_register_device(const int, const char *, const char *);
int obix_unregister_device(const int, const char *);

int obix_register_listener(const int, const char *, const char *, listener_cb_t, void *);
int obix_unregister_listener(const int, const char *, const char *);
int obix_refresh_listeners(const int, const char *, xmlDoc **);

int obix_read(CURL_EXT *, const int, const char *, const char *, xmlDoc **);
int obix_read_value(CURL_EXT *, const int, const char *, const char *, xmlChar **);

int obix_write(CURL_EXT *, const int, const char *, const char *, const char *);
int obix_write_value(CURL_EXT *, const int, const char *, const char *, const char *, OBIX_DATA_TYPE);

Batch *obix_batch_create(const int);
void obix_batch_destroy(Batch *);
int obix_batch_read(Batch *, const char *, const char *, OBIX_DATA_TYPE);
int obix_batch_write_value(Batch *, const char *, const char *, const char *, OBIX_DATA_TYPE);
int obix_batch_get_result(Batch *, const char *, xmlNode **);
int obix_batch_send(CURL_EXT *, Batch *);
int obix_batch_remove_command(Batch *, const char *, const char *);

int obix_get_history(CURL_EXT *, const int, const char *);
int obix_get_history_ts(CURL_EXT *, const int, const char *, char **, char **);
int obix_append_history(CURL_EXT *, const int, const char *, const char *);
int obix_query_history(CURL_EXT *, const int, const char *, const char *, char **, int *);

int obix_create_history_ain(char **, const char *, const int, const char **, float *);
char *obix_create_history_flt(const int, const char *, const char *, const char *, const int);
#endif

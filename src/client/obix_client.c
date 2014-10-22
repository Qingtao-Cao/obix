/* *****************************************************************************
 * Copyright (c) 2013-2014 Qingtao Cao [harry.cao@nextdc.com]
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

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>		/* open */
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>			/* close */
#include "log_utils.h"
#include "xml_config.h"
#include "xml_utils.h"
#include "obix_client.h"
#include "obix_http.h"

const char *OBIX_DATA_TYPE_NAMES[] = {
	[OBIX_T_BOOL] = OBIX_OBJ_BOOL,
	[OBIX_T_INT] = OBIX_OBJ_INT,
	[OBIX_T_REAL] = OBIX_OBJ_REAL,
	[OBIX_T_STR] = OBIX_OBJ_STR,
	[OBIX_T_ENUM] = OBIX_OBJ_ENUM,
	[OBIX_T_ABSTIME] = OBIX_OBJ_ABSTIME,
	[OBIX_T_RELTIME] = OBIX_OBJ_RELTIME,
	[OBIX_T_URI] = OBIX_OBJ_URI
};

const char *conn_type[] = {
	[CONN_TYPE_HTTP] = OBIX_CONN_HTTP
};

/*
 * Segments of the obix:HistoryAppendIn contract that
 * carries a number of float values
 *
 * oBIX applications can either use the obix_create_history_ain()
 * to get a string representation of this contract directly, or
 * setup the required xmlNode structures and then print them out
 */
static const char *HIST_APPEND_IN_PREFIX =
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n"
"<obj is=\"obix:HistoryAppendIn\">\r\n"
"<list name=\"data\" of=\"obix:HistoryRecord\">\r\n"
"<obj is=\"obix:HistoryRecord\">\r\n"
"<abstime name=\"timestamp\" val=\"%s\"/>\r\n";

static const char *HIST_APPEND_IN_SUFFIX =
"</obj>\r\n</list>\r\n</obj>";

static const char *HIST_APPEND_IN_CONTENT =
"<real name=\"%s\" val=\"%f\"/>\r\n";

/*
 * Segments of the obix:HistoryFilter contract
 *
 * oBIX applications can either use the obix_create_history_flt()
 * to get a string representation of this contract directly, or
 * setup the required xmlNode structures and then print them out
 */
static const char *HIST_FLT_PREFIX =
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n"
"<obj is=\"obix:HistoryFilter\">\r\n";

static const char *HIST_FLT_LIMIT_TEMPLATE =
"<int name=\"limit\" val=\"%d\"/>\r\n";

static const char *HIST_FLT_START_TEMPLATE =
"<abstime name=\"start\" val=\"%s\"/>\r\n";

static const char *HIST_FLT_END_TEMPLATE =
"<abstime name=\"end\" val=\"%s\"/>\r\n";

static const char *HIST_FLT_FMT_TEMPLATE =
"<str name=\"format\" val=\"%s\"/>\r\n";

static const char *HIST_FLT_CMPT_TEMPLATE =
"<bool name=\"compact\" val=\"%s\"/>\r\n";

static const char *HIST_FLT_SUFFIX =
"</obj>";

typedef enum {
	BATCH_CMD_READ = 0,
	BATCH_CMD_WRITE_VALUE
} BATCH_CMD_TYPE;

static const char *OBIX_BATCH_IN_DOC =
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n"
"<list is=\"obix:BatchIn\" of=\"obix:uri\"/>";

static const char *OBIX_BATCH_IN_NAME = "in";

/*
 * The list of all Connections of an oBIX client application
 * and the mutex to protect it
 *
 * NOTE: the LIST_HEAD macro should be used to initialise the
 * list head right at creation just in case applications may
 * mistakenly access it before any connection is setup
 */
static LIST_HEAD(_connections);
static pthread_mutex_t _connections_mutex;

/*
 * NOTE: These "get" functions should not be used in the deletion
 * operation where the searching and deletion should be handled
 * in an atomic manner
 */
static Connection *connection_get(const int conn_id)
{
	Connection *conn;

	pthread_mutex_lock(&_connections_mutex);
	list_for_each_entry(conn, &_connections, list) {
		if (conn->id == conn_id) {
			pthread_mutex_unlock(&_connections_mutex);
			return conn;
		}
	}

	pthread_mutex_unlock(&_connections_mutex);
	return NULL;
}

static Device *_device_get(Connection *conn, const char *name)
{
	Device *dev;

	list_for_each_entry(dev, &conn->devices, list) {
		if (strcmp(dev->name, name) == 0) {
			return dev;
		}
	}

	return NULL;
}


static Device *device_get(Connection *conn, const char *name)
{
	Device *dev;

	pthread_mutex_lock(&conn->mutex);
	dev = _device_get(conn, name);
	pthread_mutex_unlock(&conn->mutex);

	return dev;
}

static Listener *listener_get(Device *dev, const char *name)
{
	Listener *l;

	pthread_mutex_lock(&dev->mutex);
	list_for_each_entry(l, &dev->listeners, list) {
		if (strcmp(l->param, name) == 0) {
			pthread_mutex_unlock(&dev->mutex);
			return l;
		}
	}

	pthread_mutex_unlock(&dev->mutex);
	return NULL;
}

int obix_open_connection(const int conn_id)
{
	Connection *conn;

	if (!(conn = connection_get(conn_id))) {
		log_error("Failed to get Connection %d", conn_id);
		return OBIX_ERR_INVALID_STATE;
	}

	return conn->comm->open_connection(conn);
}

static void obix_free_device(Device *dev)
{
	if (!dev) {
		return;
	}

	pthread_mutex_destroy(&dev->mutex);
	pthread_cond_destroy(&dev->wq);

	if (dev->name) {
		free(dev->name);
	}

	free(dev);
}

int obix_unregister_device(const int conn_id, const char *name)
{
	Connection *conn;
	Device *dev, *n;

	if (!(conn = connection_get(conn_id))) {
		log_error("Failed to get connection %d", conn_id);
		return OBIX_ERR_INVALID_ARGUMENT;
	}

	/*
	 * NOTE: Mutex of the Connection should be held before the
	 * mutex of a Device
	 */
	pthread_mutex_lock(&conn->mutex);
	list_for_each_entry_safe(dev, n, &conn->devices, list) {
		if (strcmp(dev->name, name) == 0) {
			pthread_mutex_lock(&dev->mutex);
			if (list_empty(&dev->listeners) == 0) {
				pthread_mutex_unlock(&dev->mutex);
				pthread_mutex_unlock(&conn->mutex);
				log_error("Device %s still have active listeners installed");
				return OBIX_ERR_INVALID_STATE;
			}
			pthread_mutex_unlock(&dev->mutex);

			list_del(&dev->list);
			pthread_mutex_unlock(&conn->mutex);

			conn->comm->unregister_device(dev);

			/*
			 * The removal of Device should be unconditional to prevent
			 * memory leaks now that it has been dequeued from
			 * Connection.devices
			 */
			obix_free_device(dev);

			return OBIX_SUCCESS;
		}
	}

	pthread_mutex_unlock(&conn->mutex);
	return OBIX_ERR_INVALID_ARGUMENT;
}

/*
 * Register a new device through the specified connection.
 *
 * The obixData points to url-encoded content of the whole device.
 */
int obix_register_device(const int conn_id, const char *name, const char *data)
{
	Connection *conn;
	Device *dev;
	int ret = OBIX_ERR_INVALID_ARGUMENT;

	if (!(conn = connection_get(conn_id))) {
		log_error("Failed to get connection %d", conn_id);
		return OBIX_ERR_INVALID_ARGUMENT;
	}

	if ((dev = device_get(conn, name)) != NULL) {
		log_error("Device %s already registered", name);
		return OBIX_ERR_INVALID_ARGUMENT;
	}

	if (!(dev = (Device *)malloc(sizeof(Device)))) {
		log_error("Failed to allocate device descriptor for %s", name);
		return OBIX_ERR_NO_MEMORY;
	}
	memset(dev, 0, sizeof(Device));

	if (!(dev->name = strdup(name))) {
		log_error("Failed to duplicate device name for %s", name);
		free(dev);
		return OBIX_ERR_NO_MEMORY;
	}

	INIT_LIST_HEAD(&dev->listeners);
	INIT_LIST_HEAD(&dev->list);
	pthread_mutex_init(&dev->mutex, NULL);
	pthread_cond_init(&dev->wq, NULL);
	dev->conn = conn;

	if ((ret = conn->comm->register_device(dev, data)) != OBIX_SUCCESS) {
		log_error("Failed to register device for %s", name);
		goto failed;
	}

	/*
	 * NOTE: if two threads race to register one same device, both attempts
	 * will succeed in current implementation. Therefore the devices queue
	 * should be checked again for any existing device before insertion.
	 * Return success if this is the case
	 *
	 * NOTE: this means the client side solely depends on the oBIX server
	 * to ensure no race condition during device registeration
	 */
	pthread_mutex_lock(&conn->mutex);
	if (_device_get(conn, name) != NULL) {
		pthread_mutex_unlock(&conn->mutex);
		ret = OBIX_SUCCESS;
		goto existed;
	}

	list_add_tail(&dev->list, &conn->devices);
	pthread_mutex_unlock(&conn->mutex);

	return OBIX_SUCCESS;

existed:
	/*
	 * In case device already registered, just delete client side
	 * descriptors but not requesting to sign it off on server side
	 */
	conn->comm->unregister_device_local(dev);

failed:
	obix_free_device(dev);
	return ret;
}

static void obix_free_listener(Listener *l)
{
	if (!l) {
		return;
	}

	if (l->param) {
		free(l->param);
	}

	free(l);
}

/*
 * Get a list of all listeners on a particular device and reset
 * their changes counter to zero on the oBIX server
 *
 * NOTE: callers are expected to release the returned document
 * of the watchOut contract
 */
int obix_refresh_listeners(const int conn_id, const char *name, xmlDoc **doc)
{
	Connection *conn;
	Device *dev;

	if (!(conn = connection_get(conn_id)) || !(dev = device_get(conn, name))) {
		log_error("Either Connection %d or Device %s not exist", conn_id, name);
		return OBIX_ERR_INVALID_ARGUMENT;
	}

	return conn->comm->refresh_listeners(dev, doc);
}

int obix_register_listener(const int conn_id, const char *name,
						   const char *param, listener_cb_t cb, void *arg)
{
	Connection *conn;
	Device *dev;
	Listener *l;
	int ret;

	if (!(conn = connection_get(conn_id)) || !(dev = device_get(conn, name))) {
		log_error("Either Connection %d or Device %s not exist", conn_id, name);
		return OBIX_ERR_INVALID_ARGUMENT;
	}

	if ((l = listener_get(dev, param))) {
		log_error("Listener %s already registered on Device %s", param, name);
		return OBIX_ERR_INVALID_STATE;
	}

	if (!(l = (Listener *)malloc(sizeof(Listener)))) {
		log_error("Failed to allocate Listener descriptor for %s:%s", name, param);
		return OBIX_ERR_NO_MEMORY;
	}
	memset(l, 0, sizeof(Listener));

	if (!(l->param = strdup(param))) {
		log_error("Failed to duplicate name for %s:%s", name, param);
		ret = OBIX_ERR_NO_MEMORY;
		goto failed;
	}

	INIT_LIST_HEAD(&l->list);
	l->cb = cb;
	l->arg = arg;
	l->dev = dev;

	if ((ret = conn->comm->register_listener(l)) != OBIX_SUCCESS) {
		log_error("Failed to register listener for %s:%s", name, param);
		goto failed;
	}

	return OBIX_SUCCESS;

failed:
	obix_free_listener(l);
	return ret;
}

int obix_unregister_listener(const int conn_id, const char *name,
							const char *param)
{
	Connection *conn;
	Device *dev;
	Listener *l, *n;
	int ret = OBIX_ERR_INVALID_ARGUMENT;

	if (!(conn = connection_get(conn_id)) || !(dev = device_get(conn, name))) {
		log_error("Either Connection %d or Device %s not exist", conn_id, name);
		return OBIX_ERR_INVALID_ARGUMENT;
	}

	/*
	 * Search for the target Listener and dequeue it should be
	 * done in an atomic manner
	 */
	pthread_mutex_lock(&dev->mutex);
	list_for_each_entry_safe(l, n, &dev->listeners, list) {
		if (strcmp(l->param, param) == 0) {
			list_del(&l->list);
			pthread_mutex_unlock(&dev->mutex);

			ret = conn->comm->unregister_listener(l);

			/*
			 * Unconditionally remove the listener now that it has been
			 * dequeued from relevant Device.listeners list
			 */
			obix_free_listener(l);
			return ret;
		}
	}

	pthread_mutex_unlock(&dev->mutex);
	return ret;
}

int obix_read(CURL_EXT *user_handle, const int conn_id, const char *name,
			  const char *param, xmlDoc **doc)
{
	Connection *conn;
	Device *dev;

	if (!(conn = connection_get(conn_id)) || !(dev = device_get(conn, name))) {
		log_error("Either Connection %d or Device %s not exist", conn_id, name);
		return OBIX_ERR_INVALID_ARGUMENT;
	}

    return conn->comm->read(user_handle, dev, param, doc);
}

/*
 * NOTE: callers are expected to release the returned val string
 * after usage
 */
int obix_read_value(CURL_EXT *user_handle, const int conn_id, const char *name,
					const char *param, xmlChar **val)
{
	Connection *conn;
	Device *dev;

	if (!(conn = connection_get(conn_id)) || !(dev = device_get(conn, name))) {
		log_error("Either Connection %d or Device %s not exist", conn_id, name);
		return OBIX_ERR_INVALID_ARGUMENT;
	}

    return conn->comm->read_value(user_handle, dev, param, val);
}

int obix_write(CURL_EXT *user_handle, const int conn_id, const char *name,
			   const char *param, const char *data)
{
	Connection *conn;
	Device *dev;

	if (!(conn = connection_get(conn_id)) || !(dev = device_get(conn, name))) {
		log_error("Either Connection %d or Device %s not exist", conn_id, name);
		return OBIX_ERR_INVALID_ARGUMENT;
	}

    return conn->comm->write(user_handle, dev, param, data);
}

int obix_write_value(CURL_EXT *user_handle, const int conn_id, const char *name,
					 const char *param, const char *val, OBIX_DATA_TYPE tag)
{
	Connection *conn;
	Device *dev;

	if (!(conn = connection_get(conn_id)) || !(dev = device_get(conn, name))) {
		log_error("Either Connection %d or Device %s not exist", conn_id, name);
		return OBIX_ERR_INVALID_ARGUMENT;
	}

    return conn->comm->write_value(user_handle, dev, param, val, tag);
}

int obix_get_history(CURL_EXT *user_handle, const int conn_id, const char *name)
{
	Connection *conn;
	Device *dev;

	if (!(conn = connection_get(conn_id)) || !(dev = device_get(conn, name))) {
		log_error("Either Connection %d or Device %s not exist", conn_id, name);
		return OBIX_ERR_INVALID_ARGUMENT;
	}

	return conn->comm->get_history(user_handle, dev);
}

/*
 * NOTE: The ain should point to the string representation of
 * the obix:HistoryAppendIn contract, either assembled by the
 * obix_create_history_ain, or printed directly from a XML node
 * used by application as relevant template.
 */
int obix_append_history(CURL_EXT *user_handle, const int conn_id,
						const char *name, const char *ain)
{
	Connection *conn;
	Device *dev;

	if (!(conn = connection_get(conn_id)) || !(dev = device_get(conn, name))) {
		log_error("Either Connection %d or Device %s not exist", conn_id, name);
		return OBIX_ERR_INVALID_ARGUMENT;
	}

	return conn->comm->append_history(user_handle, dev, ain);
}

/*
 * NOTE: The flt should point to the string representation of
 * the obix:HistoryFilter contract
 *
 * NOTE: User should NOT release the returned pointer
 */
int obix_query_history(CURL_EXT *user_handle, const int conn_id,
					   const char *name, const char *flt,
					   char **data, int *len)
{
	Connection *conn;
	Device *dev;

	if (!(conn = connection_get(conn_id)) || !(dev = device_get(conn, name))) {
		log_error("Either Connection %d or Device %s not exist", conn_id, name);
		return OBIX_ERR_INVALID_ARGUMENT;
	}

	return conn->comm->query_history(user_handle, dev, flt, data, len);
}

#if 0
/*
 * Not used yet
 *
 * The the full content of the index file of a specific history facility
 */
static int obix_get_history_index(CURL_EXT *user_handle, const int conn_id,
								  const char *name, xmlDoc **doc)
{
	Connection *conn;
	Device *dev;
	xmlNode *root;
	int ret;

	*doc = NULL;

	if (!(conn = connection_get(conn_id)) || !(dev = device_get(conn, name))) {
		log_error("Either Connection %d or Device %s not exist", conn_id, name);
		return OBIX_ERR_INVALID_ARGUMENT;
	}

	if ((ret = conn->comm->get_history_index(user_handle, dev,
											 doc)) != OBIX_SUCCESS) {
		log_error("Failed to get history index for Device %s", name);
		return ret;
	}

	if (!(root = xmlDocGetRootElement(*doc)) ||
		xmlStrcmp(root->name, BAD_CAST OBIX_OBJ_ERR) == 0) {
		log_error("Failed to get history index for Device %s", name);
		ret = OBIX_ERR_SERVER_ERROR;
	} else {
		ret = OBIX_SUCCESS;
	}

	return ret;
}
#endif

/*
 * Get the timestamp for the first and the last history record of
 * a history facility, a special historyQuery request with "-n 0"
 * is taken advantage to this end
 *
 * NOTE: for newly created history facility that contains no data
 * at all, currently the oBIX server returns an error contract
 * for the historyQuery request
 */
int obix_get_history_ts(CURL_EXT *user_handle, const int conn_id,
						const char *name, char **start_ts, char **end_ts)
{
	xmlDoc *doc = NULL;
	xmlNode *root;
	char *flt, *data;
	int size, ret;

	if (start_ts) {
		*start_ts = NULL;
	}

	if (end_ts) {
		*end_ts = NULL;
	}

	if (!(flt = obix_create_history_flt(0, NULL, NULL, NULL, 0))) {
		return OBIX_ERR_NO_MEMORY;
	}

	if ((ret = obix_query_history(user_handle, conn_id, name, flt,
								  &data, &size)) != OBIX_SUCCESS) {
		goto failed;
	}

	if (!(doc = xmlReadMemory(data, size, NULL, NULL,
							  XML_PARSE_OPTIONS_COMMON)) ||
		!(root = xmlDocGetRootElement(doc)) ||
		xmlStrcmp(root->name, BAD_CAST OBIX_OBJ_ERR) == 0) {
		ret = OBIX_ERR_SERVER_ERROR;
		goto failed;
	}

	if (start_ts && !(*start_ts = xml_get_child_val(root, OBIX_OBJ_ABSTIME,
													HIST_ABS_START))) {
		ret = OBIX_ERR_NO_MEMORY;
		goto failed;
	}

	if (end_ts && !(*end_ts = xml_get_child_val(root, OBIX_OBJ_ABSTIME,
												HIST_ABS_END))) {
		ret = OBIX_ERR_NO_MEMORY;
		if (start_ts) {
			free(start_ts);
		}
	}

	/* Fall through */

failed:
	free(flt);

	if (doc) {
		xmlFreeDoc(doc);
	}

	return ret;
}

void obix_free_connection(Connection *conn)
{
	if (!conn) {
		return;
	}

	pthread_mutex_destroy(&conn->mutex);
	free(conn);
}

static int obix_setup_connections_helper(xmlNode *node, void *arg1, void *arg2)
{
	struct list_head *head = (struct list_head *)arg1;
	pthread_mutex_t *mutex = (pthread_mutex_t *)arg2;
	Connection *conn;
	char *type = NULL;
	int ret = OBIX_ERR_NO_MEMORY;

	if (!(conn = (Connection *)malloc(sizeof(Connection)))) {
		log_error("Failed to allocate Connection");
		return ret;
	}
	memset(conn, 0, sizeof(Connection));

	INIT_LIST_HEAD(&conn->devices);
	INIT_LIST_HEAD(&conn->list);
	pthread_mutex_init(&conn->mutex, NULL);

	if ((conn->id = xml_get_child_long(node, OBIX_OBJ_INT, CT_ID)) < 0 ||
		!(type = xml_get_child_val(node, OBIX_OBJ_STR, CT_TYPE))) {
		log_error("Failed to get connection settings");
		goto failed;
	}

	if (strcmp(type, conn_type[CONN_TYPE_HTTP]) != 0) {
		log_error("Unsupported connection type %s", type);
		goto failed;
	}

	conn->type = CONN_TYPE_HTTP;
	conn->comm = &OBIX_HTTP_COMM_STACK;

	if ((ret = conn->comm->setup_connection(node, conn)) != OBIX_SUCCESS) {
		log_error("Failed to initialise Http Connection descriptor for "
				  "Connection %d", conn->id);
		goto failed;
	}

	free(type);

	pthread_mutex_lock(mutex);
	list_add_tail(&conn->list, head);
	pthread_mutex_unlock(mutex);

	return OBIX_SUCCESS;

failed:
	if (type) {
		free(type);
	}

	obix_free_connection(conn);
	return ret;
}

/*
 * NOTE: callers should have grabbed the mutex protecting the
 * global _connections list
 */
static int _obix_destroy_connection_helper(Connection *conn)
{
	pthread_mutex_lock(&conn->mutex);
	if (list_empty(&conn->devices) == 0) {
		pthread_mutex_unlock(&conn->mutex);
		log_error("Failed to destroy connection due to existing devices");
		return OBIX_ERR_INVALID_STATE;
	}
	pthread_mutex_unlock(&conn->mutex);

	/* Dequeue from the global connection descriptors list */
	list_del(&conn->list);

	conn->comm->destroy_connection(conn);

	/*
	 * Unconditionally release the current connection now that
	 * it has been dequeued from the global list
	 */
	obix_free_connection(conn);

    return OBIX_SUCCESS;
}

int obix_destroy_connection(const int conn_id)
{
	Connection *conn, *n;
	int ret;

	pthread_mutex_lock(&_connections_mutex);
	list_for_each_entry_safe(conn, n, &_connections, list) {
		if (conn->id == conn_id) {
			ret = _obix_destroy_connection_helper(conn);
			pthread_mutex_unlock(&_connections_mutex);
			return ret;
		}
	}

	pthread_mutex_unlock(&_connections_mutex);
	return OBIX_ERR_INVALID_ARGUMENT;
}

void obix_destroy_connections(void)
{
	Connection *conn, *n;

	pthread_mutex_lock(&_connections_mutex);
	list_for_each_entry_safe(conn, n, &_connections, list) {
		if (_obix_destroy_connection_helper(conn) != OBIX_SUCCESS) {
			log_error("Memory leaks - failed to destroy Connection %d",
					  conn->id);
			/* continue; */
		}
	}
	pthread_mutex_unlock(&_connections_mutex);

	pthread_mutex_destroy(&_connections_mutex);

	curl_ext_dispose();
}

/*
 * Setup client side connection descriptors based on
 * the application's configuration file
 */
int obix_setup_connections(const char *file)
{
	xml_config_t *config;
	int ret;

	/*
	 * CURL initialisation should be done only once for
	 * the entire application
	 */
	if (curl_ext_init() < 0) {
		return OBIX_ERR_NO_MEMORY;
	}

	INIT_LIST_HEAD(&_connections);
	pthread_mutex_init(&_connections_mutex, NULL);

	if (!(config = xml_config_create(NULL, file)) ||
		xml_config_log(config) < 0) {
		log_error("Failed to setup the log facility");
		ret = OBIX_ERR_INVALID_ARGUMENT;
		goto failed;
	}

	if ((ret = xml_config_for_each_obj(config, XP_CT,
									   obix_setup_connections_helper,
									   &_connections,
									   &_connections_mutex)) != OBIX_SUCCESS) {
		log_error("Failed to setup connections");
	}

	/* Fall through */

failed:
	if (ret != OBIX_SUCCESS) {
		obix_destroy_connections();
	}

	if (config) {
		xml_config_free(config);
	}

	return ret;
}

static void obix_batch_destroy_command(Command *cmd)
{
	if (!cmd) {
		return;
	}

	if (cmd->param) {
		free(cmd->param);
	}

	if (cmd->href) {
		free(cmd->href);
	}

	free(cmd);
}

void obix_batch_destroy(Batch *batch)
{
	Command *cmd, *n;

	if (!batch) {
		return;
	}

	list_for_each_entry_safe(cmd, n, &batch->cmds, list) {
		list_del(&cmd->list);
		obix_batch_destroy_command(cmd);
	}

	if (batch->in) {
		xmlFreeDoc(batch->in);
	}

	if (batch->out) {
		xmlFreeDoc(batch->out);
	}

	free(batch);
}

Batch *obix_batch_create(const int conn_id)
{
	Connection *conn;
	Batch *batch;

	if (!(conn = connection_get(conn_id))) {
		log_error("Failed to get Connection %d", conn_id);
		return NULL;
	}

	if (!(batch = (Batch *)malloc(sizeof(Batch)))) {
		log_error("Failed to allocate a batch object");
		return NULL;
	}
	memset(batch, 0, sizeof(Batch));

	INIT_LIST_HEAD(&batch->cmds);
	batch->conn = conn;

	if (!(batch->in = xmlReadMemory(OBIX_BATCH_IN_DOC,
									strlen(OBIX_BATCH_IN_DOC),
									NULL, NULL,
									XML_PARSE_OPTIONS_COMMON))) {
		log_error("Failed to create a batch.in document");
		obix_batch_destroy(batch);
		batch = NULL;
	}

	/*
	 * The batch.out document is setup by relevant CURL API
	 * when receiving the batchOut contract from the oBIX server
	 */
	return batch;
}

int obix_batch_remove_command(Batch *batch, const char *name, const char *param)
{
	Connection *conn = batch->conn;
	Device *dev;
	Http_Device *hd;
	Command *cmd, *n;
	char *href;
	xmlNode *root, *node;

	if (!batch->in || !(root = xmlDocGetRootElement(batch->in))) {
		log_error("Illegal batchIn document for batch object on Connection %d",
				  conn->id);
		return OBIX_ERR_INVALID_STATE;
	}

	if (!(dev = device_get(conn, name))) {
		log_error("Failed to find Device %s on Connection %d", name, conn->id);
		return OBIX_ERR_INVALID_ARGUMENT;
	}
	hd = dev->priv;

	if (link_pathname(&href, hd->href, NULL, param, NULL) < 0) {
		log_error("Failed to assemble batch command's href for %s on Device %s "
				  "on Connection %d", param, name, conn->id);
		return OBIX_ERR_NO_MEMORY;
	}

	/*
	 * Delete relevant node from the batchIn document
	 *
	 * The absolute href of the target node on the oBIX server is
	 * represented by the val attribute of the <uri/> node
	 */
	node = xml_find_child(root, OBIX_OBJ_URI, OBIX_ATTR_VAL, href);

	if (node) {
		xml_delete_node(node);
	}

	/*
	 * Dequeue and release relevant command descriptor
	 */
	list_for_each_entry_safe(cmd, n, &batch->cmds, list) {
		if (strcmp(cmd->href, href) == 0) {
			list_del(&cmd->list);
			obix_batch_destroy_command(cmd);
			free(href);
			return OBIX_SUCCESS;
		}
	}

	free(href);

	/* Not found */
	return OBIX_ERR_INVALID_STATE;
}

static int obix_batch_add_command(Batch *batch, BATCH_CMD_TYPE type,
								  const char *name, const char *param,
								  const char *val, OBIX_DATA_TYPE tag)
{
	Connection *conn = batch->conn;
	Device *dev;
	Http_Device *hd;
	Command *cmd;
	const char *is_attr;
	xmlNode *parent = NULL, *child = NULL;
	int ret = OBIX_ERR_NO_MEMORY;

	switch(type) {
	case BATCH_CMD_WRITE_VALUE:
		is_attr = OBIX_CONTRACT_OP_WRITE;
		break;
	case BATCH_CMD_READ:
		is_attr = OBIX_CONTRACT_OP_READ;
		break;
	default:
		log_error("Illegal Batch Command type %d for %s on Device %s",
				  type, param, name);
		return OBIX_ERR_INVALID_ARGUMENT;
	}

	if (!(dev = device_get(conn, name))) {
		log_error("Failed to find Device %s on Connection %d", name, conn->id);
		return OBIX_ERR_INVALID_ARGUMENT;
	}
	hd = dev->priv;

	if (!(cmd = (Command *)malloc(sizeof(Command))) ||
		!(cmd->param = strdup(param)) ||
		link_pathname(&cmd->href, hd->href, NULL, param, NULL) < 0) {
		log_error("Failed to setup batch command for %s on Device %s",
				  param, dev->name);
		goto failed;
	}

	cmd->dev = dev;
	INIT_LIST_HEAD(&cmd->list);
	cmd->result = NULL;

	/*
	 * Now create a node for current Batch Command and add it
	 * into the batch.in document. The node format for the READ
	 * command:
	 *
	 *	<uri is="obix:Read" val="%s"/>
	 *
	 * The node format for the WRITE_VALUE command:
	 *
	 *	<uri is="obix:Write" val="%s">
	 *		<%s name="in" val="%s"/>
	 *	</uri>
	 *
	 * The val attribute of the <uri/> node is always the absolute
	 * href of the target node, whereas the val attribute of the
	 * child node of a obix:Write node is the new value
	 */
	if (!(parent = xmlNewNode(NULL, BAD_CAST OBIX_OBJ_URI)) ||
		!xmlSetProp(parent, BAD_CAST OBIX_ATTR_IS, BAD_CAST is_attr) ||
		!xmlSetProp(parent, BAD_CAST OBIX_ATTR_VAL, BAD_CAST cmd->href)) {
		log_error("Failed to setup xmlNode for Batch Command on %s",
				  cmd->href);
		goto failed;
	}

	if (type == BATCH_CMD_WRITE_VALUE) {
		if (!(child = xmlNewNode(NULL, BAD_CAST OBIX_DATA_TYPE_NAMES[tag])) ||
			!xmlSetProp(child, BAD_CAST OBIX_ATTR_NAME,
						BAD_CAST OBIX_BATCH_IN_NAME) ||
			!xmlSetProp(child, BAD_CAST OBIX_ATTR_VAL, BAD_CAST val) ||
			!xmlAddChild(parent, child)) {
			log_error("Failed to setup xmlNode for Batch Command on %s",
					  cmd->href);
			goto failed;
		}
	}

	if (!xmlAddChild(xmlDocGetRootElement(batch->in), parent)) {
		log_error("Failed to add xmlNode to batch.in document for %s",
				  cmd->href);
		goto failed;
	}

	list_add_tail(&cmd->list, &batch->cmds);
	return OBIX_SUCCESS;

failed:
	/*
	 * Free the child first just in case it may have been
	 * added into the parent subtree
	 */
	if (child) {
		xmlFreeNode(child);
	}

	if (parent) {
		xmlFreeNode(parent);
	}

	obix_batch_destroy_command(cmd);
	return ret;
}

int obix_batch_write_value(Batch *batch,
						   const char *name, const char *param,
						   const char *val, OBIX_DATA_TYPE tag)
{
	return obix_batch_add_command(batch, BATCH_CMD_WRITE_VALUE,
								  name, param, val, tag);
}

int obix_batch_read(Batch *batch,
					const char *name, const char *param,
					OBIX_DATA_TYPE tag)
{
	return obix_batch_add_command(batch, BATCH_CMD_READ,
								  name, param, NULL, tag);
}

int obix_batch_send(CURL_EXT *user_handle, Batch *batch)
{
	Connection *conn = batch->conn;

	return conn->comm->send_batch(user_handle, batch);
}

/*
 * Get the returned xmlNode for a particular batch command
 */
int obix_batch_get_result(Batch *batch, const char *param, xmlNode **node)
{
	Command *cmd;

	*node = NULL;

	list_for_each_entry(cmd, &batch->cmds, list) {
		if (strcmp(param, cmd->param) == 0) {
			*node = cmd->result;
			return OBIX_SUCCESS;
		}
	}

	return OBIX_ERR_INVALID_ARGUMENT;
}

/*
 * Assemble an obix:HistoryFilter contract from the given
 * parameters
 *
 * NOTE: normally speaking oBIX client won't raise the
 * History.Query requests as frequent as the History.Append
 * requests, no pre-allocated buffer is reused
 */
char *obix_create_history_flt(const int limit, const char *start, const char *end,
							  const char *fmt, const int compact)
{
	char *buf;
	int len, pos;

	/*
	 * Decide the length of HistoryFilter contract, all sub-elements
	 * are optional
	 */
	len = strlen(HIST_FLT_PREFIX) + strlen(HIST_FLT_SUFFIX);

	/*
	 * If limit < 0 then all available history records will be queried,
	 * in which case the "limit" sub node could be safely ignored
	 *
	 * If limit == 0, then the timestamp for the first and the last
	 * record will be returned
	 */
	if (limit >= 0) {
		len += strlen(HIST_FLT_LIMIT_TEMPLATE) + HIST_FLT_VAL_MAX_BITS - 2;
	}

	if (start) {
		len += strlen(HIST_FLT_START_TEMPLATE) + strlen(start) - 2;
	}

	if (end) {
		len += strlen(HIST_FLT_END_TEMPLATE) + strlen(end) - 2;
	}

	if (fmt) {
		len += strlen(HIST_FLT_FMT_TEMPLATE) + strlen(fmt) - 2;
	}

	len += strlen(HIST_FLT_CMPT_TEMPLATE) - 2 + XML_BOOL_MAX_LEN;

	if (!(buf = (char *)malloc(len + 1))) {
		log_error("Failed to allocate memory for HistoryFilter");
		return NULL;
	}

	pos = sprintf(buf, "%s", HIST_FLT_PREFIX);

	if (limit > 0) {
		pos += sprintf(buf+pos, HIST_FLT_LIMIT_TEMPLATE, limit);
	}

	if (start) {
		pos += sprintf(buf+pos, HIST_FLT_START_TEMPLATE, start);
	}

	if (end) {
		pos += sprintf(buf+pos, HIST_FLT_END_TEMPLATE, end);
	}

	if (fmt) {
		pos += sprintf(buf+pos, HIST_FLT_FMT_TEMPLATE, fmt);
	}

	pos += sprintf(buf+pos, HIST_FLT_CMPT_TEMPLATE,
					((compact == 1) ? XML_TRUE : XML_FALSE));

	pos += sprintf(buf+pos, "%s", HIST_FLT_SUFFIX);

	buf[pos] = '\0';

	return buf;
}

/*
 * Assemble a HistoryAppendIn contract in the user provided buffer
 * with sz obix:real elements, the name and value for each of them
 * are specified by name[] and val[] respectively.
 *
 * NOTE: Not use a static buffer since it would NOT be thread-safe,
 * any invocation to this function would change the content of the
 * static buffer if used. Instead, threads should pass in their own
 * buffer.
 *
 * NOTE: Create the buffer if needed, but recycle existing one for
 * sake of efficiency. Callers therefore must practice caution across
 * each invocation with a name[] array of the same size and fixed
 * content for each string, otherwise buffer overflow may ensue.
 */
int obix_create_history_ain(char **buf, const char *ts, const int sz,
							const char *name[], float val[])
{
	int len, pos, i;

	if (!*buf) {
		len = strlen(HIST_APPEND_IN_PREFIX) + HIST_REC_TS_MAX_LEN - 2 +
				strlen(HIST_APPEND_IN_SUFFIX);

		for (i = 0; i < sz; i++) {
			len += strlen(HIST_APPEND_IN_CONTENT) + strlen(name[i]) - 2 +
					FLOAT_MAX_BITS - 2;
		}

		if (!(*buf = (char *)malloc(len + 1))) {
			log_error("Failed to allocate memory for HistoryAppendIn contract");
			return OBIX_ERR_NO_MEMORY;
		}
	}

	pos = sprintf(*buf, HIST_APPEND_IN_PREFIX, ts);

	for (i = 0; i < sz; i++) {
		pos += sprintf(*buf+pos, HIST_APPEND_IN_CONTENT, name[i], val[i]);
	}

	pos += sprintf(*buf+pos, "%s", HIST_APPEND_IN_SUFFIX);

	*(*buf + pos) = '\0';

	return OBIX_SUCCESS;
}


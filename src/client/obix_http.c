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

#include <stdlib.h>
#include <string.h>
#include <unistd.h>		/* write */
#include "xml_config.h"
#include "log_utils.h"
#include "curl_ext.h"
#include "ptask.h"
#include "obix_utils.h"
#include "obix_client.h"
#include "obix_http.h"
#include "xml_utils.h"

static const char *OBIX_WRITE_NODE_DOC =
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n"
"<%s href=\"%s\" val=\"%s\"/>\r\n";

/*
 * The watchIn contract is used for both Watch.Add
 * and Watch.Remove operations
 */
static const char *OBIX_WATCH_IN_DOC =
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n"
"<obj is=\"obix:WatchIn\">\r\n"
"<list name=\"hrefs\" of=\"obix:Uri\">\r\n"
"<uri val=\"%s\"/>\r\n"
"</list>\r\n"
"</obj>";

static const char *OBIX_HISTORY_GET_DOC =
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n"
"<obj is=\"obix:HistoryGet\">\r\n"
"<str name=\"dev_id\" val=\"%s\"/>\r\n"
"</obj>";

static const char *WATCH_PWI = "pollWaitInterval";
static const char *WATCH_ADD = "add";
static const char *WATCH_REMOVE = "remove";
static const char *WATCH_POLL_CHANGES = "pollChanges";
static const char *WATCH_POLL_REFRESH = "pollRefresh";
static const char *WATCH_DELETE = "delete";

static const char *OBIX_WATCH_OUT_LIST_NAME = "values";

static const char *OBIX_LOBBY_SIGNUP = "signUp";
static const char *OBIX_LOBBY_BATCH = "batch";
static const char *OBIX_LOBBY_WATCH_SERVICE = "watchService";
static const char *OBIX_LOBBY_HISTORY_SERVICE = "historyService";

/*
 * No slash automatically appended before suffix,
 * therefore they need to be explicitly included
 * in the suffix
 */
static const char *OBIX_LOBBY_WATCH_SERVICE_MAKE = "/make";
static const char *OBIX_LOBBY_HISTORY_SERVICE_GET = "/get";

static const char *WATCH_PWI_MIN = "min";
static const char *WATCH_PWI_MAX = "max";

/*
 * Part of the content of the display attribute of the obix:err
 * contract returned by oBIX server when a device already signed
 * up earlier
 */
static const char *SERVER_ERRMSG_DEV_EXIST = "already exists";

/*
 * Defines HTTP communication stack.
 */
const Comm_Stack OBIX_HTTP_COMM_STACK = {
	.setup_connection = http_setup_connection,
	.open_connection = http_open_connection,
	.destroy_connection = http_destroy_connection,

	.register_device = http_register_device,
	.unregister_device = http_unregister_device,
	.unregister_device = http_unregister_device_local,

	.register_listener = http_register_listener,
	.unregister_listener = http_unregister_listener,
	.refresh_listeners = http_refresh_listeners,

	.read = http_read,
	.read_value = http_read_value,

	.write = http_write,
	.write_value = http_write_value,

	.send_batch = http_send_batch,

	.get_history = http_get_history,
	.get_history_index = http_get_history_index,
	.append_history = http_append_history,
	.query_history = http_query_history,
};

static Listener *_listener_get(Device *dev, const char *name)
{
	Listener *l;

	list_for_each_entry(l, &dev->listeners, list) {
		if (strcmp(l->param, name) == 0) {
			return l;
		}
	}

	return NULL;
}

/*
 * NOTE: Once the "IP address + facility href" strings released,
 * relevant pointers in Http_Connection structure needs to be
 * nullified to prevent double free when destroying a connection
 */
static void http_destroy_connection_hrefs(Http_Connection *hc)
{
	if (!hc) {
		return;
	}

	if (hc->signup) {
		free(hc->signup);
		hc->signup = NULL;
	}

	if (hc->batch) {
		free(hc->batch);
		hc->batch = NULL;
	}

	if (hc->watch_make) {
		free(hc->watch_make);
		hc->watch_make = NULL;
	}

	if (hc->hist_get) {
		free(hc->hist_get);
		hc->hist_get = NULL;
	}
}

/* Release http specific fields in one connection descriptor */
void http_destroy_connection(Connection *conn)
{
	Http_Connection *hc = conn->priv;

	if (!hc) {
		return;
	}

	pthread_mutex_destroy(&hc->curl_mutex);

	if (hc->handle) {
		curl_ext_free(hc->handle);
	}

	if (hc->ip) {
		free(hc->ip);
	}

	if (hc->lobby) {
		free(hc->lobby);
	}

	http_destroy_connection_hrefs(hc);

	free(hc);
	conn->priv = NULL;
}

/*
 * Allocate and initialize a Http_Connection for the current
 * connection tag in XML file
 */
int http_setup_connection(xmlNode *node, Connection *conn)
{
	Http_Connection *hc;
	int ret = OBIX_ERR_NO_MEMORY;

	if (!(hc = (Http_Connection *)malloc(sizeof(Http_Connection)))) {
		log_error("Failed to allocate Http Connection descriptor");
		return OBIX_ERR_NO_MEMORY;
	}
	memset(hc, 0, sizeof(Http_Connection));

	if ((hc->timeout = xml_get_child_long(node, OBIX_OBJ_INT, CT_CURL_TIMEOUT)) < 0 ||
		(hc->bulky = xml_get_child_long(node, OBIX_OBJ_INT, CT_CURL_BULKY)) < 0 ||
		(hc->poll_int = xml_get_child_long(node, OBIX_OBJ_INT, CT_POLL_INTERVAL)) < 0 ||
		(hc->poll_min = xml_get_child_long(node, OBIX_OBJ_INT, CT_LP_MIN)) < 0 ||
		(hc->poll_max = xml_get_child_long(node, OBIX_OBJ_INT, CT_LP_MAX)) < 0 ||
		!(hc->ip = xml_get_child_val(node, OBIX_OBJ_STR, CT_SERVER_IP)) ||
		!(hc->lobby = xml_get_child_val(node, OBIX_OBJ_STR, CT_SERVER_LOBBY))) {
		log_error("Failed to get config settings for connection %d", conn->id);
		ret = OBIX_ERR_INVALID_ARGUMENT;
		goto failed;
	}

	if (curl_ext_create(&hc->handle, hc->bulky, hc->timeout) < 0) {
		log_error("Failed to setup CURL handle for connection %d", conn->id);
		goto failed;
	}

	pthread_mutex_init(&hc->curl_mutex, NULL);

	conn->priv = hc;
	hc->p = conn;

	return OBIX_SUCCESS;

failed:
	/*
	 * If setup failed, the connection will be released right away
	 * and won't be destroyed again, therefore no need to nullify
	 * these two pointers
	 */
	if (hc->ip) {
		free(hc->ip);
	}

	if (hc->lobby) {
		free(hc->lobby);
	}

	return ret;
}

int http_open_connection(Connection *conn)
{
	Http_Connection *hc = conn->priv;
	xmlDoc *doc = NULL;
	xmlNode *root;
	char *href = NULL, *ip_lobby;
	int ret;

	if (link_pathname(&ip_lobby, hc->ip, NULL, hc->lobby, NULL) < 0) {
		log_error("Failed to assemble the IP address for %s", hc->lobby);
		return OBIX_ERR_NO_MEMORY;
	}

	hc->handle->outputBuffer = NULL;

	pthread_mutex_lock(&hc->curl_mutex);
	ret = curl_ext_getDOM(hc->handle, ip_lobby, &doc);
	pthread_mutex_unlock(&hc->curl_mutex);

	free(ip_lobby);

	if (ret < 0 || !(root = xmlDocGetRootElement(doc)) ||
		xmlStrcmp(root->name, BAD_CAST OBIX_OBJ_ERR) == 0) {
		log_error("Failed to read oBIX server's lobby facility");
		ret = OBIX_ERR_SERVER_ERROR;
		goto failed;
	}

	if (!(href = xml_get_child_href(root, OBIX_OBJ_OP, OBIX_LOBBY_SIGNUP)) ||
		link_pathname(&hc->signup, hc->ip, hc->lobby, href, NULL) < 0) {
		log_error("Failed to get href of %s from oBIX server", OBIX_LOBBY_SIGNUP);
		ret = OBIX_ERR_NO_MEMORY;
		goto failed;
	}
	free(href);

	if (!(href = xml_get_child_href(root, OBIX_OBJ_OP, OBIX_LOBBY_BATCH)) ||
		link_pathname(&hc->batch, hc->ip, hc->lobby, href, NULL) < 0) {
		log_error("Failed to get href of %s from oBIX server", OBIX_LOBBY_BATCH);
		ret = OBIX_ERR_NO_MEMORY;
		goto failed;
	}
	free(href);

	/*
	 * NOTE: pay attention that ref nodes contain absolute hrefs
	 * of the real, target nodes already
	 */
	if (!(href = xml_get_child_href(root, OBIX_OBJ_REF,
								   OBIX_LOBBY_WATCH_SERVICE)) ||
		link_pathname(&hc->watch_make, hc->ip, NULL, href,
					  OBIX_LOBBY_WATCH_SERVICE_MAKE) < 0) {
		log_error("Failed to get href of %s from oBIX server",
				  OBIX_LOBBY_WATCH_SERVICE);
		ret = OBIX_ERR_NO_MEMORY;
		goto failed;
	}
	free(href);

	if (!(href = xml_get_child_href(root, OBIX_OBJ_REF,
								   OBIX_LOBBY_HISTORY_SERVICE)) ||
		link_pathname(&hc->hist_get, hc->ip, NULL, href,
					  OBIX_LOBBY_HISTORY_SERVICE_GET) < 0) {
		log_error("Failed to get href of %s from oBIX server",
				  OBIX_LOBBY_HISTORY_SERVICE);
		ret = OBIX_ERR_NO_MEMORY;
		goto failed;
	}

	log_debug("Successfully opened Connection %d", conn->id);

	ret = OBIX_SUCCESS;

	/* Fall through */

failed:
	if (href) {
		free(href);
	}

	if (doc) {
		xmlFreeDoc(doc);
	}

	if (ret != OBIX_SUCCESS) {
		http_destroy_connection_hrefs(hc);
	}

	return ret;
}

int http_unregister_device_local(Device *device)
{
	Http_Device *hd = device->priv;

	if (hd->href) {
		free(hd->href);
	}

	/*
	 * No Listener is assured in the Device.listeners list
	 * in the first place, and all watch facilities are deleted
	 * with the removal of the last Listener
	 *
	 * However, the oBIX server doesn't support the deletion of
	 * history facilities and there is no relevant API on the
	 * client side
	 */
	if (hd->hist_append) {
		free(hd->hist_append);
	}

	if (hd->hist_query) {
		free(hd->hist_query);
	}

	if (hd->hist_index) {
		free(hd->hist_index);
	}

	free(hd);
	device->priv = NULL;

	return OBIX_SUCCESS;
}

int http_unregister_device(Device *dev)
{
	static int count = 0;

	/*
	 * TODO: Raise signoff request to oBIX server to sign off
	 * the device.
	 *
	 * NOTE: the Http_Device should be unconditionally deleted
	 * now that its parent Device descriptor has been dequeued
	 * and will be released once this function returns
	 */
	if (count++ < 1) {
	    log_warning("Unfortunately driver unregistering is not supported yet.");
	}

	return http_unregister_device_local(dev);
}

int http_register_device(Device *dev, const char *data)
{
	Connection *conn = dev->conn;
	Http_Connection *hc = conn->priv;
	Http_Device *hd;
	xmlDoc *doc = NULL;
	xmlNode *root;
	char *display = NULL;
	int ret;

#ifdef DEBUG
	if (xml_is_valid_doc(data, NULL) == 0) {
		return OBIX_ERR_INVALID_ARGUMENT;
	}
#endif

	if (!(hd = (Http_Device *)malloc(sizeof(Http_Device)))) {
		log_error("Failed to allocate http specific part for a device");
		return OBIX_ERR_NO_MEMORY;
	}
	memset(hd, 0, sizeof(Http_Device));

	/*
	 * Connection.mutex must be grabbed during the communication
	 * with the oBIX server to ensure no race conditions in the
	 * usage of the global CURL handle for the current connection
	 */
	hc->handle->outputBuffer = data;

	pthread_mutex_lock(&hc->curl_mutex);
	ret = curl_ext_postDOM(hc->handle, hc->signup, &doc);
	pthread_mutex_unlock(&hc->curl_mutex);

	if (ret < 0 || !(root = xmlDocGetRootElement(doc))) {
		log_error("SignUp failed for Device %s on Connection %d",
				  dev->name, conn->id);
		ret = OBIX_ERR_SERVER_ERROR;
		goto failed;
	}

	/*
	 * If oBIX server returned an error contract, further check if the
	 * device has been registered.
	 *
	 * NOTE: must return success in this case to support the re-run
	 * of adaptors as recovery after crash but the oBIX server is not
	 * rebooted therefore all device contracts are still available
	 */
	if (xmlStrcmp(root->name, BAD_CAST OBIX_OBJ_ERR) == 0) {
		display = (char *)xmlGetProp(root, BAD_CAST OBIX_ATTR_DISPLAY);

		if (!display || strstr(display, SERVER_ERRMSG_DEV_EXIST) == NULL) {
			/* No display, or other types of error */
			ret = OBIX_ERR_SERVER_ERROR;
			goto failed;
		}

		log_warning("Device already registered on oBIX server, NOT necessarily "
					"registered by the previous instance of this application! "
					"Try to live with it anyway");
	}

	if (!(hd->href = (char *)xmlGetProp(root, BAD_CAST OBIX_ATTR_HREF))) {
		log_error("No href in the device contract returned from oBIX server");
		ret = OBIX_ERR_NO_MEMORY;
		goto failed;
	}

	dev->priv = hd;
	hd->p = dev;

	ret = OBIX_SUCCESS;

	/* Fall through */

failed:
	if (display) {
		free(display);
	}

	if (doc) {
		xmlFreeDoc(doc);
	}

	if (ret != OBIX_SUCCESS) {
		free(hd);
	}

	return ret;
}

/*
 * Send watchIn contract to oBIX server, either to add or to delete
 * the specified href from relevant watch object
 */
static int http_watch_item_helper(Device *dev, Listener *l, const int add)
{
	Connection *conn = dev->conn;
	Http_Device *hd = dev->priv;
	Http_Listener *hl = l->priv;
	xmlDoc *doc = NULL;
	xmlNode *root;
	char *buf;
	int len, ret;

	len = strlen(OBIX_WATCH_IN_DOC) + strlen(hl->href) - 2;

	if (!(buf = (char *)malloc(len + 1))) {
		log_error("Failed to assemble watchIn contract");
		return OBIX_ERR_NO_MEMORY;
	}
	sprintf(buf, OBIX_WATCH_IN_DOC, hl->href);

	hd->watch_handle->outputBuffer = buf;

	/* curl_mutex helps serialise the usage of watch_handle */
	pthread_mutex_lock(&hd->curl_mutex);
	ret = curl_ext_postDOM(hd->watch_handle,
						   (add == 1) ? hd->watch_add : hd->watch_remove,
						   &doc);
	pthread_mutex_unlock(&hd->curl_mutex);

	if (ret < 0 || !(root = xmlDocGetRootElement(doc)) ||
		xmlStrcmp(root->name, BAD_CAST OBIX_OBJ_ERR) == 0) {
		log_error("%s failed for Device %s on Connection %d",
				  (add == 1) ? "Watch.Add" : "Watch.Remove",
				  dev->name, conn->id);
		ret = OBIX_ERR_SERVER_ERROR;
	} else {
		ret = OBIX_SUCCESS;
	}

	if (doc) {
		xmlFreeDoc(doc);
	}

	free(buf);

	return ret;
}

/*
 * NOTE: The Device.mutex must remain grabbed to ensure consistency
 * while accessing Device.listeners queue
 */
static int _http_add_watch_item(Device *dev, Listener *l)
{
	Http_Device *hd = dev->priv;
	int ret;

	if (_listener_get(dev, l->param) != NULL) {	/* already existed */
		return OBIX_ERR_INVALID_STATE;
	}

	/*
	 * No need to stop the watch thread even if it has been started
	 * since it will be blocked on Device.mutex now that it has
	 * being grabbed by this thread.
	 *
	 * More importantly, waiting for the stop of the watch thread with
	 * the Device.mutex held will incur deadlock
	 */

	if ((ret = http_watch_item_helper(dev, l, 1)) == OBIX_SUCCESS) {
		list_add_tail(&l->list, &dev->listeners);
	}

	/* Start the watch thread after the first Listener is added */
	if (&l->list == dev->listeners.prev && &l->list == dev->listeners.next) {
		obix_schedule_task(&hd->watch_thread);
	}

	return ret;
}

static void http_remove_watch_core(Device *dev)
{
	Http_Device *hd = dev->priv;

	obix_cancel_task(&hd->watch_thread);
	obix_destroy_task(&hd->watch_thread);

	if (hd->watch_handle) {
		curl_ext_free(hd->watch_handle);
	}

	if (hd->poll_handle) {
		curl_ext_free(hd->poll_handle);
	}

	pthread_mutex_destroy(&hd->curl_mutex);

	if (hd->watch_pwi_min) {
		free(hd->watch_pwi_min);
		hd->watch_pwi_min = NULL;
	}

	if (hd->watch_pwi_max) {
		free(hd->watch_pwi_max);
		hd->watch_pwi_max = NULL;
	}

	if (hd->watch_add) {
		free(hd->watch_add);
		hd->watch_add = NULL;
	}

	if (hd->watch_remove) {
		free(hd->watch_remove);
		hd->watch_remove = NULL;
	}

	if (hd->watch_pollChanges) {
		free(hd->watch_pollChanges);
		hd->watch_pollChanges = NULL;
	}

	if (hd->watch_pollRefresh) {
		free(hd->watch_pollRefresh);
		hd->watch_pollRefresh = NULL;
	}

	if (hd->watch_delete) {
		free(hd->watch_delete);
		hd->watch_delete = NULL;
	}
}

static int http_remove_watch(Device *dev)
{
	Connection *conn = dev->conn;
	Http_Device *hd = dev->priv;
	xmlDoc *doc = NULL;
	xmlNode *root;
	int	ret;

	hd->watch_handle->outputBuffer = NULL;

	/* curl_mutex helps serialise the usage of watch_handle */
	pthread_mutex_lock(&hd->curl_mutex);
	ret = curl_ext_postDOM(hd->watch_handle, hd->watch_delete, &doc);
	pthread_mutex_unlock(&hd->curl_mutex);

	if (ret < 0 || !(root = xmlDocGetRootElement(doc)) ||
		xmlStrcmp(root->name, BAD_CAST OBIX_OBJ_ERR) == 0) {
		log_error("Watch.Delete failed for Device %s on Connection %d",
				  dev->name, conn->id);
		ret = OBIX_ERR_SERVER_ERROR;
	} else {
		ret = OBIX_SUCCESS;
	}

	http_remove_watch_core(dev);

	if (doc) {
		xmlFreeDoc(doc);
	}

	return ret;
}

static void watch_poll_task(void *arg)
{
	Device *dev = (Device *)arg;
	Connection *conn = dev->conn;
	Http_Device *hd = dev->priv;
	Listener *l;
	Http_Listener *hl;
	xmlDoc *doc = NULL;
	xmlNode *list, *node, *root;
	xmlChar *href = NULL;

	pthread_mutex_lock(&dev->mutex);
	if (list_empty(&dev->listeners) == 1) {
		pthread_mutex_unlock(&dev->mutex);
		return;
	}
	pthread_mutex_unlock(&dev->mutex);

	/*
	 * The polling thread exclusively uses its own CURL handle so as not
	 * to impact other watch related requests, so no mutex is needed
	 */
	hd->poll_handle->outputBuffer = NULL;
	if (curl_ext_postDOM(hd->poll_handle, hd->watch_pollChanges, &doc) < 0 ||
		!(root = xmlDocGetRootElement(doc)) ||
		xmlStrcmp(root->name, BAD_CAST OBIX_OBJ_ERR) == 0) {
		log_error("Watch.pollChanges failed for Device %s on Connection %d",
				  dev->name, conn->id);
		goto failed;
	}

	if (!(list = xml_find_child(root, OBIX_OBJ_LIST, OBIX_ATTR_NAME,
								OBIX_WATCH_OUT_LIST_NAME))) {
		log_error("Illegal watchOut contract returned for Device %s "
				  "on Connection %d", dev->name, conn->id);
		goto failed;
	}

	/*
	 * Reset the handled flag for all listeners for this round of execution
	 */
	pthread_mutex_lock(&dev->mutex);
	list_for_each_entry(l, &dev->listeners, list) {
		hl = l->priv;
		hl->handled = 0;
	}
	pthread_mutex_unlock(&dev->mutex);

	/* Interpret the watchOut contract returned by oBIX server */
	for (node = list->children; node; node = node->next) {
		if (node->type != XML_ELEMENT_NODE) {
			continue;
		}

		if (href) {
			xmlFree(href);
		}

		if (!(href = xmlGetProp(node, BAD_CAST OBIX_ATTR_HREF))) {
			log_error("No href provided in watchOut contract for Device %s"
					  "on Connection %d", dev->name, conn->id);
			continue;
		}

		if (xmlStrcmp(node->name, BAD_CAST OBIX_OBJ_ERR) == 0) {
			log_error("Err contract for href %s in watchOut contract for "
					  "Device %s", href, dev->name);
			continue;
		}

restart:
		pthread_mutex_lock(&dev->mutex);
		list_for_each_entry(l, &dev->listeners, list) {
			hl = l->priv;
			if (hl->handled == 1) {
				continue;
			}

			/*
			 * Only invoke the current listener's callback if the current
			 * child of watchOut relates with the monitored node
			 *
			 * Device.mutex should be released during the invocation of the
			 * callback, however, this will invite race conditions which may
			 * attempt to add or delete listeners from the queue. Therefore
			 * always restart from the beginning of the queue and skip already
			 * handled listeners once a callbck is returned
			 */
			if (xmlStrcmp(href, BAD_CAST hl->href) == 0) {
				pthread_mutex_unlock(&dev->mutex);
				if (l->cb && l->cb(hd->poll_handle, node, l->arg) != OBIX_SUCCESS) {
					log_error("Callback failed for %s:%s on Connection %d",
							  dev->name, l->param, conn->id);
				}

				hl->handled = 1;
				goto restart;
			}
		}
		pthread_mutex_unlock(&dev->mutex);
	}

	/* Fall through */

failed:
	if (doc) {
		xmlFreeDoc(doc);
	}

	if (href) {
		xmlFree(href);
	}
}

static int http_write_core(CURL_EXT *handle,
						   pthread_mutex_t *lock,	/* To serialise the usage of the curl handle */
						   const char *ip,			/* The IP address of the oBIX server */
						   const char *href,
						   const char *data,
						   OBIX_DATA_TYPE tag,		/* ignored if is_doc == 1 */
						   const int is_doc)
{
	xmlDoc *doc = NULL;
	xmlNode *root;
	char *buf = NULL, *uri;
	int ret, len = 0;

	if (link_pathname(&uri, ip, NULL, href, NULL) < 0) {
		log_error("Failed to assemble URI for target href of %s", href);
		return OBIX_ERR_NO_MEMORY;
	}

	if (is_doc == 0) {
		len = strlen(OBIX_WRITE_NODE_DOC) + strlen(OBIX_DATA_TYPE_NAMES[tag]) +
			  strlen(href) + strlen(data) - 6;

		if (!(buf = (char *)malloc(len + 1))) {
			log_error("Failed to fill in single node document");
			ret = OBIX_ERR_NO_MEMORY;
			goto failed;
		}
		sprintf(buf, OBIX_WRITE_NODE_DOC,
				OBIX_DATA_TYPE_NAMES[tag], href, data);

		handle->outputBuffer = buf;
	} else {
		handle->outputBuffer = data;
	}

#ifdef DEBUG
	if (xml_is_valid_doc(handle->outputBuffer, NULL) == 0) {
		ret = OBIX_ERR_INVALID_ARGUMENT;
		goto failed;
	}
#endif

	if (lock) {
		pthread_mutex_lock(lock);
	}
	ret = curl_ext_putDOM(handle, uri, &doc);
	if (lock) {
		pthread_mutex_unlock(lock);
	}

	if (ret < 0 || !(root = xmlDocGetRootElement(doc)) ||
		xmlStrcmp(root->name, BAD_CAST OBIX_OBJ_ERR) == 0) {
		log_error("Write operation failed for %s", href);
		ret = OBIX_ERR_SERVER_ERROR;
	} else {
		ret = OBIX_SUCCESS;
	}

	/* Fall through */

failed:
	free(uri);

	if (is_doc == 0 && buf) {
		free(buf);
	}

	if (doc) {
		xmlFreeDoc(doc);
	}

	return ret;
}

static int _http_create_watch(Device *dev)
{
	Connection *conn = dev->conn;
	Http_Connection *hc = conn->priv;
	Http_Device *hd = dev->priv;
	xmlDoc *doc = NULL;
	xmlNode *root;
	char *href = NULL, *reltime = NULL;
	int ret = OBIX_ERR_NO_MEMORY;

	pthread_mutex_init(&hd->curl_mutex, NULL);

	/*
	 * Create the watch thread and relevant CURL handle when
	 * the watch object is actually needed.
	 *
	 * They will be released if the watch object no longer has
	 * any node to monitor.
	 *
	 * The CURL handles for watch operations will never time out
	 * but blocking to receive notifications from the oBIX server
	 */
	if (curl_ext_create(&hd->watch_handle, 0, 0) < 0 ||
		curl_ext_create(&hd->poll_handle, 0, 0) < 0) {
		log_error("Failed to setup watch CURL handles for device %s",
				  dev->name);
		goto failed;
	}

	/*
	 * IMPORTANT: the period of the polling thread must be set as ZERO
	 * so as to raise the Watch.pollChanges request as frequently as
	 * possible. If no such request is raised, the oBIX server can't
	 * send back notification of changes.
	 *
	 * Even though such request is raised as fast as possible, the
	 * oBIX server will still hold processing it with a maximal delay
	 * of poll_max in the long poll mode.
	 *
	 * These two concepts are totally different
	 */
	if (obix_setup_task(&hd->watch_thread, NULL, watch_poll_task, dev,
						0, EXECUTE_INDEFINITE) < 0) {
		log_error("Failed to setup watch thread for Device %s on Connection %d",
				  dev->name, conn->id);
		goto failed;
	}

	hd->watch_handle->outputBuffer = NULL;

	pthread_mutex_lock(&hd->curl_mutex);
	ret = curl_ext_postDOM(hd->watch_handle, hc->watch_make, &doc);
	pthread_mutex_unlock(&hd->curl_mutex);

	if (ret < 0 || !(root = xmlDocGetRootElement(doc)) ||
		xmlStrcmp(root->name, BAD_CAST OBIX_OBJ_ERR) == 0) {
		log_error("Watch.Make failed for Device %s on Connection %d",
				  dev->name, conn->id);
		ret = OBIX_ERR_SERVER_ERROR;
		goto failed;
	}

	if (!(href = (char *)xmlGetProp(root, BAD_CAST OBIX_ATTR_HREF))) {
		log_error("Illegal watch contract for Device %s on Connection %d",
				  dev->name, conn->id);
		ret = OBIX_ERR_SERVER_ERROR;
		goto failed;
	}

	/*
	 * Assemble device specific, oBIX server IP address prefixed
	 * absolute hrefs. However, the min/max thresholds of a watch
	 * object are not operations therefore no IP address is needed
	 * for them
	 */
	if (link_pathname(&hd->watch_pwi_min, href, WATCH_PWI,
					  WATCH_PWI_MIN, NULL) < 0 ||
		link_pathname(&hd->watch_pwi_max, href, WATCH_PWI,
					  WATCH_PWI_MAX, NULL) < 0 ||
		link_pathname(&hd->watch_add, hc->ip, href, WATCH_ADD,
					  NULL) < 0 ||
		link_pathname(&hd->watch_remove, hc->ip, href, WATCH_REMOVE,
					  NULL) < 0 ||
		link_pathname(&hd->watch_pollChanges, hc->ip, href, WATCH_POLL_CHANGES,
					  NULL) < 0 ||
		link_pathname(&hd->watch_pollRefresh, hc->ip, href, WATCH_POLL_REFRESH,
					  NULL) < 0 ||
		link_pathname(&hd->watch_delete, hc->ip, href, WATCH_DELETE,
					  NULL) < 0) {
		log_error("Failed to get hrefs from watch %s", href);
		ret = OBIX_ERR_NO_MEMORY;
		goto failed;
	}

	if (!(reltime = obix_reltime_from_long(hc->poll_min, RELTIME_SEC)) ||
		http_write_core(hd->watch_handle, &hd->curl_mutex, hc->ip,
						hd->watch_pwi_min, reltime, OBIX_T_RELTIME,
						0) != OBIX_SUCCESS) {
		log_warning("Failed to update %s on the watch for Device %s, "
					"use server's default settings", WATCH_PWI_MIN, dev->name);
	}

	if (reltime) {
		free(reltime);
	}

	if (!(reltime = obix_reltime_from_long(hc->poll_max, RELTIME_SEC)) ||
		http_write_core(hd->watch_handle, &hd->curl_mutex, hc->ip,
						hd->watch_pwi_max, reltime, OBIX_T_RELTIME,
						0) != OBIX_SUCCESS) {
		log_warning("Failed to update %s on the watch for Device %s, "
					"use server's default settings", WATCH_PWI_MAX, dev->name);
	}

	if (reltime) {
		free(reltime);
	}

	ret = OBIX_SUCCESS;

	/* Fall through */

failed:
	if (href) {
		free(href);
	}

	if (doc) {
		xmlFreeDoc(doc);
	}

	if (ret != OBIX_SUCCESS) {
		http_remove_watch_core(dev);
	}

	return ret;
}

static void http_free_listener(Http_Listener *hl)
{
	if (!hl) {
		return;
	}

	if (hl->href) {
		free(hl->href);
	}

	free(hl);
}

int http_register_listener(Listener *l)
{
	Device *dev = l->dev;
	Http_Device *hd = dev->priv;
	Http_Listener *hl;
	int ret = OBIX_ERR_NO_MEMORY;

	if (!(hl = (Http_Listener *)malloc(sizeof(Http_Listener)))) {
		log_error("Failed to allocate Http specific part of a Listener");
		return OBIX_ERR_NO_MEMORY;
	}
	memset(hl, 0, sizeof(Http_Listener));

	if (link_pathname(&hl->href, hd->href, NULL, l->param, NULL) < 0) {
		log_error("Failed to assemble absolute href for %s on Device %s",
				  l->param, dev->name);
		goto failed;
	}

	l->priv = hl;
	hl->p = l;

	/*
	 * The watch thread, relevant CURL handle and the watch object
	 * on the oBIX server are created in the lazy mode, that is,
	 * until the creation of the first Listener
	 *
	 * Since the new listener is added in the _http_add_watch_item,
	 * the Device.mutex should be grabbed all the way until it
	 * finishes to prevent race conditions on their creation
	 *
	 * Also if another thread is trying to remove the last listener
	 * from the queue, wait for it to destroy all watch facilities
	 * then re-create them (should be improved in the future)
	 */
	pthread_mutex_lock(&dev->mutex);

	while (dev->watch_being_deleted == 1) {
		pthread_cond_wait(&dev->wq, &dev->mutex);
	}

	if (list_empty(&dev->listeners) == 1) {
		if ((ret = _http_create_watch(dev)) != OBIX_SUCCESS) {
			log_error("Failed to create watch object and thread for %s "
					  "on Device %s", l->param, dev->name);
			pthread_mutex_unlock(&dev->mutex);
			goto failed;
		}
	}

	if ((ret = _http_add_watch_item(dev, l)) != OBIX_SUCCESS) {
		log_error("Failed to add a watch item for %s on Device %d",
				  l->param, dev->name);
		pthread_mutex_unlock(&dev->mutex);
		goto failed;
	}
	pthread_mutex_unlock(&dev->mutex);

	return OBIX_SUCCESS;

failed:
	http_free_listener(hl);
	return ret;
}

int http_unregister_listener(Listener *l)
{
	Device *dev = l->dev;
	Http_Listener *hl = l->priv;
	int ret = OBIX_ERR_NO_MEMORY;

	if ((ret = http_watch_item_helper(dev, l, 0)) != OBIX_SUCCESS) {
		log_warning("Failed to remove server side watchItem for %s on %s, "
					"continue to remove client side descriptor",
					l->param, dev->name);
	}

	/*
	 * Remove the watch facilities if no listeners in the queue
	 *
	 * NOTE: watch facilities must be deleted in this case in order
	 * to avoid inconsistency when the watch object is deleted by
	 * the oBIX server after its lease threshold expires
	 *
	 * A flag is raised to avoid race conditions with potential thread
	 * which tries to add a new listener into the queue, because the
	 * mutex will be released when deleting the watch facilities
	 */
	pthread_mutex_lock(&dev->mutex);
	if (list_empty(&dev->listeners) == 1) {
		dev->watch_being_deleted = 1;

		/*
		 * NOTE: the Device.mutex must be released while waiting for
		 * the completion of the polling thread to avoid a deadlock
		 */
		pthread_mutex_unlock(&dev->mutex);
		if ((ret = http_remove_watch(dev)) != OBIX_SUCCESS) {
			log_error("Failed to remove watch object from %s", dev->name);
		}
		pthread_mutex_lock(&dev->mutex);

		/*
		 * Wake up the pending thread which adds a new listener to the
		 * queue, note it won't be waken up until the mutex is released
		 * by this thread
		 */
		dev->watch_being_deleted = 0;
		pthread_cond_signal(&dev->wq);
	}
	pthread_mutex_unlock(&dev->mutex);

	http_free_listener(hl);
	return ret;
}

int http_refresh_listeners(Device *dev, xmlDoc **doc)
{
	Connection *conn = dev->conn;
	Http_Device *hd = dev->priv;
	xmlNode *root;
	int ret;

	*doc = NULL;

	hd->watch_handle->outputBuffer = NULL;

	/* curl_mutex helps serialise the usage of watch_handle */
	pthread_mutex_lock(&hd->curl_mutex);
	ret = curl_ext_postDOM(hd->watch_handle, hd->watch_pollRefresh, doc);
	pthread_mutex_unlock(&hd->curl_mutex);

	if (ret < 0 || !(root = xmlDocGetRootElement(*doc)) ||
		xmlStrcmp(root->name, BAD_CAST OBIX_OBJ_ERR) == 0) {
		log_error("Watch.pollRefresh failed for Device %s of Connection %d",
				  dev->name, conn->id);

		if (*doc) {
			xmlFreeDoc(*doc);
			*doc = NULL;
		}

		ret = OBIX_ERR_SERVER_ERROR;
	} else {
		ret = OBIX_SUCCESS;
	}

	return ret;
}

int http_read(CURL_EXT *user_handle, Device *dev, const char *param,
			  xmlDoc **doc)
{
	Connection *conn = dev->conn;
	Http_Connection *hc = conn->priv;
	Http_Device *hd = dev->priv;
	CURL_EXT *handle = (user_handle) ? user_handle : hc->handle;
	xmlNode *root;
	char *href;
	int ret;

	*doc = NULL;

	/* Does not matter if param is NULL */
	if (link_pathname(&href, hc->ip, hd->href, param, NULL) < 0) {
		log_error("Failed to assemble URI for part of Device %s on Connection %d",
				  dev->name, conn->id);
		return OBIX_ERR_NO_MEMORY;
	}

	handle->outputBuffer = NULL;
	if (!user_handle) {
		pthread_mutex_lock(&hc->curl_mutex);
	}
	ret = curl_ext_getDOM(handle, href, doc);
	if (!user_handle) {
		pthread_mutex_unlock(&hc->curl_mutex);
	}
	free(href);

	if (ret < 0 || !(root = xmlDocGetRootElement(*doc)) ||
		xmlStrcmp(root->name, BAD_CAST OBIX_OBJ_ERR) == 0) {
		log_error("Failed to read from Device %s on Connection %d",
				  dev->name, conn->id);

		if (*doc) {
			xmlFreeDoc(*doc);
			*doc = NULL;
		}

		ret = OBIX_ERR_SERVER_ERROR;
	} else {
		ret = OBIX_SUCCESS;
	}

	return ret;
}

/*
 * Get and copy the current value of the 'val' attribute of the
 * specified object by paramUri.
 *
 * NOTE: On success, calls are expected to release the returned
 * value strings.
 */
int http_read_value(CURL_EXT *user_handle, Device *dev, const char *param,
					xmlChar **val)
{
	xmlDoc *doc = NULL;
	xmlNode *root;
	int ret;

	*val = NULL;

	if ((ret = http_read(user_handle, dev, param, &doc)) != OBIX_SUCCESS) {
		goto failed;
	}

	if (!(root = xmlDocGetRootElement(doc)) ||
		xmlStrcmp(root->name, BAD_CAST OBIX_OBJ_ERR) == 0) {
		ret = OBIX_ERR_SERVER_ERROR;
		goto failed;
	}

	if (!(*val = xmlGetProp(root, BAD_CAST OBIX_ATTR_VAL))) {
		ret = OBIX_ERR_INVALID_STATE;
	} else {
		ret = OBIX_SUCCESS;
	}

	/* Fall through */

failed:
	if (doc) {
		xmlFreeDoc(doc);
	}

	if (ret != OBIX_SUCCESS) {
		log_error("Failed to read from %s on Device %s",
				  (param) ? param : "(NULL)", dev->name);
	}

	return ret;
}

static int http_write_helper(CURL_EXT *user_handle, Device *dev,
							 const char *param, const char *data,
							 OBIX_DATA_TYPE tag, const int is_doc)
{
	Connection *conn = dev->conn;
	Http_Connection *hc = conn->priv;
	Http_Device *hd = dev->priv;
	CURL_EXT *handle = (user_handle) ? user_handle : hc->handle;
	pthread_mutex_t *lock = (user_handle) ? NULL : &hc->curl_mutex;
	char *href;
	int ret;

	if (link_pathname(&href, hd->href, NULL, param, NULL) < 0) {
		log_error("Failed to assemble href for param %s on Device %s",
				  param, dev->name);
		return OBIX_ERR_NO_MEMORY;
	}

	ret = http_write_core(handle, lock, hc->ip, href, data, tag, is_doc);

	free(href);

	return ret;
}

int http_write(CURL_EXT *user_handle, Device *dev, const char *param,
			   const char *data)
{
	return http_write_helper(user_handle, dev, param, data, 0 /* ignored */,
							 1);	/* is_doc == 1 */
}

int http_write_value(CURL_EXT *user_handle, Device *dev, const char *param,
					 const char *val, OBIX_DATA_TYPE tag)
{
	/*
	 * Since the href of the target node is assembled in the helper
	 * function, the wrapping up of the val string into a well-formed
	 * XML document would have to be done there as well
	 */

	return http_write_helper(user_handle, dev, param, val, tag,
							 0);	/* is_doc == 0 */
}

int http_get_history(CURL_EXT *user_handle, Device *dev)
{
	Connection *conn = dev->conn;
	Http_Connection *hc = conn->priv;
	Http_Device *hd = dev->priv;
	CURL_EXT *handle = (user_handle) ? user_handle : hc->handle;
	xmlDoc *doc = NULL;
	xmlNode *root;
	char *buf, *href = NULL;
	int len, ret;

	if (hd->hist_index) {	/* history facility already created */
		return OBIX_SUCCESS;
	}

	len = strlen(OBIX_HISTORY_GET_DOC) + strlen(dev->name) - 2;

	if (!(buf = (char *)malloc(len + 1))) {
		log_error("Failed to allocate buffer for History.Get request");
		return OBIX_ERR_NO_MEMORY;
	}
	sprintf(buf, OBIX_HISTORY_GET_DOC, dev->name);

	handle->outputBuffer = buf;
	if (!user_handle) {
		pthread_mutex_lock(&hc->curl_mutex);
	}
	ret = curl_ext_postDOM(handle, hc->hist_get, &doc);
	if (!user_handle) {
		pthread_mutex_unlock(&hc->curl_mutex);
	}
	free(buf);

	if (ret < 0 || !(root = xmlDocGetRootElement(doc)) ||
		xmlStrcmp(root->name, BAD_CAST OBIX_OBJ_ERR) == 0 ||
		!(href = (char *)xmlGetProp(root, BAD_CAST OBIX_ATTR_HREF))) {
		log_error("History.Get failed for Device %s on Connection %d",
				  dev->name, conn->id);
		ret = OBIX_ERR_SERVER_ERROR;
		goto failed;
	}

	if (link_pathname(&hd->hist_append, hc->ip, href, HIST_OP_APPEND,
					  NULL) < 0 ||
		link_pathname(&hd->hist_query, hc->ip, href, HIST_OP_QUERY,
					  NULL) < 0 ||
		link_pathname(&hd->hist_index, hc->ip, href, HIST_INDEX,
					  NULL) < 0) {
		log_error("Failed to assemble History facility hrefs for Device %s "
				  "on Connection %d", dev->name, conn->id);
		ret = OBIX_ERR_NO_MEMORY;
		goto failed;
	}

	ret = OBIX_SUCCESS;

	/* Fall through */

failed:
	if (href) {
		free(href);
	}

	if (doc) {
		xmlFreeDoc(doc);
	}

	if (ret != OBIX_SUCCESS) {
		if (hd->hist_append) {
			free(hd->hist_append);
			hd->hist_append = NULL;
		}

		if (hd->hist_query) {
			free(hd->hist_query);
			hd->hist_query = NULL;
		}

		if (hd->hist_index) {
			free(hd->hist_index);
			hd->hist_index = NULL;
		}
	}

	return ret;
}

int http_get_history_index(CURL_EXT *user_handle, Device *dev, xmlDoc **doc)
{
	Connection *conn = dev->conn;
	Http_Connection *hc = conn->priv;
	Http_Device *hd = dev->priv;
	CURL_EXT *handle = (user_handle) ? user_handle : hc->handle;
	xmlNode *root;
	int ret;

	handle->outputBuffer = NULL;
	if (!user_handle) {
		pthread_mutex_lock(&hc->curl_mutex);
	}
	ret = curl_ext_getDOM(handle, hd->hist_index, doc);
	if (!user_handle) {
		pthread_mutex_unlock(&hc->curl_mutex);
	}

	if (ret < 0 || !(root = xmlDocGetRootElement(*doc)) ||
		xmlStrcmp(root->name, BAD_CAST OBIX_OBJ_ERR) == 0) {
		log_error("Failed to read history index for Device %s on Connection %d",
				  dev->name, conn->id);

		if (*doc) {
			xmlFreeDoc(*doc);
			*doc = NULL;
		}

		ret = OBIX_ERR_SERVER_ERROR;
	} else {
		ret = OBIX_SUCCESS;
	}

	return ret;
}

int http_append_history(CURL_EXT *user_handle, Device *dev, const char *ain)
{
	Connection *conn = dev->conn;
	Http_Connection *hc = conn->priv;
	Http_Device *hd = dev->priv;
	CURL_EXT *handle = (user_handle) ? user_handle : hc->handle;
	xmlDoc *doc = NULL;
	xmlNode *root;
	int ret;

#ifdef DEBUG
	if (xml_is_valid_doc(ain, OBIX_CONTRACT_HIST_AIN) == 0) {
		return OBIX_ERR_INVALID_ARGUMENT;
	}
#endif

	handle->outputBuffer = ain;

	if (!user_handle) {
		pthread_mutex_lock(&hc->curl_mutex);
	}
	ret = curl_ext_postDOM(handle, hd->hist_append, &doc);
	if (!user_handle) {
		pthread_mutex_unlock(&hc->curl_mutex);
	}

	if (ret < 0 || !(root = xmlDocGetRootElement(doc)) ||
		xmlStrcmp(root->name, BAD_CAST OBIX_OBJ_ERR) == 0) {
		log_error("History.Append failed for Device %s on Connection %d",
				  dev->name, conn->id);
		ret = OBIX_ERR_SERVER_ERROR;
	} else {
		ret = OBIX_SUCCESS;
	}

	if (doc) {
		xmlFreeDoc(doc);
	}

	return ret;
}

int http_query_history(CURL_EXT *user_handle, Device *dev, const char *flt,
					   char **data, int *size)
{
	Connection *conn = dev->conn;
	Http_Connection *hc = conn->priv;
	Http_Device *hd = dev->priv;
	CURL_EXT *handle = (user_handle) ? user_handle : hc->handle;
	int ret;

#ifdef DEBUG
	if (xml_is_valid_doc(flt, OBIX_CONTRACT_HIST_FLT) == 0) {
		return OBIX_ERR_INVALID_ARGUMENT;
	}
#endif

	handle->outputBuffer = flt;

	if (!user_handle) {
		pthread_mutex_lock(&hc->curl_mutex);
	}

	/*
	 * Considering that History.Query request may return huge amount
	 * of data, by no means a XML DOM tree is built from it!
	 */
	if ((ret = curl_ext_post(handle, hd->hist_query)) < 0) {
		if (!user_handle) {
			pthread_mutex_unlock(&hc->curl_mutex);
		}
		log_error("History.Query failed for Device %s on Connection %d",
				  dev->name, conn->id);
	} else {
		ret = curl_get_data(handle, data, size);

		if (!user_handle) {
			pthread_mutex_unlock(&hc->curl_mutex);
		}
	}

	return (ret == 0) ? OBIX_SUCCESS : OBIX_ERR_NO_MEMORY;
}

static void http_cleanup_batch_out(Batch *batch)
{
	Command *cmd;

	if (batch->out) {
		xmlFreeDoc(batch->out);
		batch->out = NULL;
	}

	list_for_each_entry(cmd, &batch->cmds, list) {
		cmd->result = NULL;
	}
}

int http_send_batch(CURL_EXT *user_handle, Batch *batch)
{
	Connection *conn = batch->conn;
	Http_Connection *hc = conn->priv;
	CURL_EXT *handle = (user_handle) ? user_handle : hc->handle;
	Command *cmd;
	xmlNode *root, *node;
	char *data, *href = NULL;
	int ret;

	if (!batch->in || !(root = xmlDocGetRootElement(batch->in)) ||
		!(data = xml_dump_node(root))) {
		log_error("Failed to dump batch.in document for Connection %d",
				  conn->id);
		return OBIX_ERR_NO_MEMORY;
	}

	/*
	 * Wipe out existing batch->out document to make room
	 * for the new result
	 */
	http_cleanup_batch_out(batch);

	handle->outputBuffer = data;
	if (!user_handle) {
		pthread_mutex_lock(&hc->curl_mutex);
	}
	ret = curl_ext_postDOM(handle, hc->batch, &batch->out);
	if (!user_handle) {
		pthread_mutex_unlock(&hc->curl_mutex);
	}
	free(data);

	if (ret < 0 || !(root = xmlDocGetRootElement(batch->out)) ||
		xmlStrcmp(root->name, BAD_CAST OBIX_OBJ_ERR) == 0) {
		log_error("Batch sending failed for Connection %d", conn->id);
		return OBIX_ERR_SERVER_ERROR;
	}

	for (node = root->children; node; node = node->next) {
		if (node->type != XML_ELEMENT_NODE) {
			continue;
		}

		if (href) {
			free(href);
		}

		if (!(href = (char *)xmlGetProp(node, BAD_CAST OBIX_ATTR_HREF))) {
			continue;
		}

		/*
		 * Setup the cross reference from each Command to
		 * its result node in the batch.out document
		 */
		list_for_each_entry(cmd, &batch->cmds, list) {
			if (strcmp(href, cmd->href) == 0) {
				cmd->result = node;
				break;		/* from inner loop */
			}
		}
	}

	if (href) {
		free(href);
	}

	return OBIX_SUCCESS;
}

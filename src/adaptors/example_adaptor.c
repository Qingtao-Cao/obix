/* *****************************************************************************
 * Copyright (c) 2013-2014 Qingtao Cao [harry.cao@nextdc.com]
 * Copyright (c) 2009 Andrey Litvinov
 *
 * This file is part of obix-adaptors
 *
 * obix-adaptors is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * obix-adaptors is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with obix-adaptors. If not, see <http://www.gnu.org/licenses/>.
 *
 * *****************************************************************************/

#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include "obix_client.h"
#include "log_utils.h"
#include "ptask.h"
#include "xml_utils.h"
#include "xml_config.h"

/* XPath predicates used when parsing the configuration files */
static const char *XP_NAME = "/config/meta/name";
static const char *XP_UPDATER_PERIOD = "/config/meta/updater_period";
static const char *XP_PARENT_HREF = "/config/meta/parent_href";
static const char *XP_HISTORY_ROOT = "/config/meta/history_root";
static const char *XP_HISTORY_LOGFILE = "/config/meta/history_logfile";
static const char *XP_DEV_CONTRACT = "/config/device/obj";

/* XPath predicates used when manipulating history templates */
static const char *XP_HIST_AIN = "/history/obj[@name='ain']";
static const char *XP_HIST_AIN_TS = "/history/obj[@name='ain']/list[@name='data']/obj[@is='obix:HistoryRecord']/abstime[@name='timestamp']";
static const char *XP_HIST_AIN_TIME = "/history/obj[@name='ain']/list[@name='data']/obj[@is='obix:HistoryRecord']/reltime[@name='time']";

typedef struct example_dev {
	/*
	 * The name of the device from the config file
	 *
	 * NOTE: in case different devices on one connection (with one
	 * particular oBIX server) have same names, each of their
	 * history_name is used as their unique ID by the oBIX client
	 * side APIs
	 */
	char *name;

	/* The root of relevant history facility's HREF on the oBIX server */
	char *history_root;

	/* The parent contract's HREF on the oBIX Server */
	char *parent_href;

	/* The unique name of the device's history facility throughout one whole oBIX server */
	char *history_name;

	/* The href of the device's contract on oBIX server */
	char *href;

	/* The absolute path of the history log file created before exiting */
	char *history_logfile;

	/* The period of the updater thread */
	int updater_period;

	/*
	 * The periodic timer thread that increment the time count by 1
	 * and have relevant contract on the oBIX server updated and history
	 * record appended
	 */
	obix_task_t obix_updater;

	/*
	 * The relative time since starts-up in unit of miliseconds, which
	 * is a critical region between the timer thread and the callback
	 * function (the polling thread for the current device) and the mutex
	 * to pretect it from race conditions
	 */
	long time;
	pthread_mutex_t mutex;

	/* The time in string format */
	char *mtime_ts;

	/*
	 * The Batch object used by the callback function upon reception of
	 * the notification of changes from the oBIX server
	 */
	Batch *batch;

	/*
	 * The XML template of the obix:HistoryAppendIn contract
	 */
	xml_config_t *history;

	/*
	 * The device contract registered on the oBIX server
	 */
	xmlNode *contract;
} example_dev_t;

/*
 * Descriptors for sub nodes of the example device, containing a node's
 * tag, relative href, default value.
 *
 * NOTE: when registering a listener on a sub node, a special usage is
 * to set param as "/" which will have the entire device monitored
 */
typedef struct dev_node {
	const OBIX_DATA_TYPE tag;
	const char *param;
	const char *defval;
} dev_node_t;

typedef enum {
	DEV_NODE_TIME = 0,
	DEV_NODE_RESET = 1,
	DEV_NODE_COUNTER = 2,
	DEV_NODE_MAX = 3
} DEV_NODE_TYPE;

/*
 * NOTE: Settings here must be the same as the device contract defined in
 * the configuration file
 */
dev_node_t example_nodes[] = {
	[DEV_NODE_TIME] = {
		.tag = OBIX_T_RELTIME,
		.param = "time",
		.defval = "PT0S"
	},
	[DEV_NODE_RESET] = {
		.tag = OBIX_T_BOOL,
		.param = "reset",
		.defval = "false"
	},
	[DEV_NODE_COUNTER] = {
		.tag = OBIX_T_INT,
		.param = "counter",
		.defval = "0"
	}
};

void obix_updater_task(void *arg);
int example_reset_cb(CURL_EXT *handle, xmlNode *node, void *arg);

/* Flag shared among signal handler and the main thread */
static int flag_exit;

/* The signal captured for cleanup purpose */
static const int CLEANUP_SIGNAL = SIGINT;

static void example_signal_handler(int signo)
{
	if (signo == CLEANUP_SIGNAL) {
		flag_exit = 1;
	}
}

static void example_destroy_param(example_dev_t *dev)
{
	if (dev->name) {
		free(dev->name);
	}

	if (dev->history_name) {
		free(dev->history_name);
	}

	if (dev->href) {
		free(dev->href);
	}

	if (dev->parent_href) {
		free(dev->parent_href);
	}

	if (dev->history_root) {
		free(dev->history_root);
	}

	if (dev->history_logfile) {
		free(dev->history_logfile);
	}

	if (dev->contract) {
		xml_delete_node(dev->contract);
	}
}

static int example_setup_param(example_dev_t *dev, xml_config_t *config)
{
	xmlNode *node;
	int ret = OBIX_ERR_INVALID_ARGUMENT;

	if (!(node= xml_config_get_node(config, XP_DEV_CONTRACT))) {
		log_error("Failed to get device contract from config file");
		goto failed;
	}

	if (!(dev->contract = xmlCopyNode(node, 1))) {
		log_error("Failed to copy device contract from config file");
		ret = OBIX_ERR_NO_MEMORY;
		goto failed;
	}

	if (!(dev->name = xml_config_get_str(config, XP_NAME)) ||
		!(dev->parent_href = xml_config_get_str(config, XP_PARENT_HREF)) ||
		!(dev->history_root = xml_config_get_str(config, XP_HISTORY_ROOT)) ||
		!(dev->history_logfile = xml_config_get_str(config, XP_HISTORY_LOGFILE)) ||
		(dev->updater_period = xml_config_get_int(config, XP_UPDATER_PERIOD)) < 0) {
		log_error("Failed to get settings from config file");
		goto failed;
	}

	if (link_pathname(&dev->history_name, dev->history_root, NULL,
					  dev->name, NULL) < 0 ||
		link_pathname(&dev->href, OBIX_DEVICE_ROOT, dev->parent_href,
					  dev->name, NULL) < 0) {
		log_error("Failed to assemble BMS device href or history_name");
		ret = OBIX_ERR_NO_MEMORY;
		goto failed;
	}

	return OBIX_SUCCESS;

failed:
	example_destroy_param(dev);
	return ret;
}

static void example_destroy_dev(example_dev_t *dev)
{
	if (!dev) {
		return;
	}

	obix_destroy_task(&dev->obix_updater);

	if (dev->mtime_ts) {
		free(dev->mtime_ts);
	}

	pthread_mutex_destroy(&dev->mutex);

	example_destroy_param(dev);

	free(dev);
}

static example_dev_t *example_setup_dev(const char *path)
{
	example_dev_t *dev;
	xml_config_t *config;

	if (!(config = xml_config_create(NULL, path))) {
		log_error("%s is not a valid XML file", path);
		return NULL;
	}

	if (!(dev = (example_dev_t *)malloc(sizeof(example_dev_t)))) {
		log_error("Failed to malloc software descriptor");
		goto failed;
	}
	memset(dev, 0, sizeof(example_dev_t));

	/* Setup parameters from the device configuration file */
	if (example_setup_param(dev, config) != OBIX_SUCCESS) {
		log_error("Failed to setup BMS parameters");
		goto failed;
	}

	if (obix_setup_task(&dev->obix_updater, NULL, obix_updater_task, dev,
						dev->updater_period, EXECUTE_INDEFINITE) < 0) {
		log_error("Failed to create obix_updater thread");
		goto failed;
	}

	xml_config_free(config);
	log_debug("Successfully setup example device descriptor");

	return dev;

failed:
	example_destroy_dev(dev);
	xml_config_free(config);
	return NULL;
}

static int example_append_history(example_dev_t *dev, const char *reltime)
{
	xmlNode *ain, *ts, *time;
	char *data;
	int ret = OBIX_SUCCESS;

	ain = ts = time = NULL;
	xml_config_for_each_obj(dev->history, XP_HIST_AIN, xml_config_get_template,
							&ain, NULL);
	xml_config_for_each_obj(dev->history, XP_HIST_AIN_TS, xml_config_get_template,
							&ts, NULL);
	xml_config_for_each_obj(dev->history, XP_HIST_AIN_TIME, xml_config_get_template,
							&time, NULL);

	if (!ain || !ts || !time) {
		log_error("Failed to find history templates");
		return OBIX_ERR_INVALID_ARGUMENT;
	}

	if (!xmlSetProp(ts, BAD_CAST OBIX_ATTR_VAL, BAD_CAST dev->mtime_ts) ||
		!xmlSetProp(time, BAD_CAST OBIX_ATTR_VAL, BAD_CAST reltime)) {
		log_error("Failed to set TS value in history record of %s",
				  dev->history_name);
		return OBIX_ERR_NO_MEMORY;
	}

	if (!(data = xml_dump_node(ain))) {
		log_error("Failed to dump content of history record of %s",
				  dev->history_name);
		return OBIX_ERR_NO_MEMORY;
	}

	ret = obix_append_history(NULL, OBIX_CONNECTION_ID,
							  dev->history_name, data);
	free(data);

	if (ret != OBIX_SUCCESS) {
		log_error("Failed to append history record for %s", dev->history_name);
	}

	return ret;
}

static void example_unregister_dev(example_dev_t *dev)
{
	obix_unregister_device(OBIX_CONNECTION_ID, dev->history_name);
}

static int example_register_dev(example_dev_t *dev)
{
	char *data, *start_ts, *end_ts;
	int ret;

	if (!xmlSetProp(dev->contract, BAD_CAST OBIX_ATTR_NAME,
					BAD_CAST dev->name) ||
		!xmlSetProp(dev->contract, BAD_CAST OBIX_ATTR_HREF,
					BAD_CAST dev->href)) {
		log_error("Failed to setup device contract");
		return OBIX_ERR_NO_MEMORY;
	}

	if (!(data = xml_dump_node(dev->contract))) {
		log_error("Failed to print out device contract as a string");
		return OBIX_ERR_NO_MEMORY;
	}

	ret = obix_register_device(OBIX_CONNECTION_ID, dev->history_name, data);
	free(data);

	if (ret != OBIX_SUCCESS) {
		log_error("Failed to register Device %s", dev->history_name);
		return ret;
	}

	ret = obix_get_history(NULL, OBIX_CONNECTION_ID, dev->history_name);
	if (ret != OBIX_SUCCESS) {
		log_error("Failed to get history facility for Device %s",
				  dev->history_name);
	} else if (obix_get_history_ts(NULL, OBIX_CONNECTION_ID, dev->history_name,
								   &start_ts, &end_ts) == OBIX_SUCCESS) {
		log_debug("The timestamp of the first history facility is %s", start_ts);
		log_debug("The timestamp of the last history facility is %s", end_ts);
		free(start_ts);
		free(end_ts);
	}

	return ret;
}

static void example_save_history(example_dev_t *dev)
{
	char *flt = NULL, *data;
	int size;
	int fd;

	if ((fd = open(dev->history_logfile, O_CREAT | O_WRONLY, 0644)) < 0) {
		log_error("Failed to open %s", dev->history_logfile);
		return;
	}

	/*
	 * Query as much data as possible
	 */
	if (!(flt = obix_create_history_flt(-1, NULL, NULL, NULL, 0))) {
		log_error("Failed to create historyFilter contract");
		goto failed;
	}

	if (obix_query_history(NULL, OBIX_CONNECTION_ID, dev->history_name, flt,
						   &data, &size) != OBIX_SUCCESS) {
		log_error("Failed to query history facility from %s", dev->history_name);
		goto failed;
	}

	errno = 0;
	if (write(fd, data, size) != size) {
		log_error("Failed to save history data into %s: %s",
				  dev->history_logfile, strerror(errno));
	}

	/* Fall through */

failed:
	if (flt) {
		free(flt);
	}

	close(fd);
}

/*
 * The callback function to handle the changes on the "reset" node
 * of the example device
 */
int example_reset_cb(CURL_EXT *handle, xmlNode *node, void *arg)
{
	example_dev_t *dev = (example_dev_t *)arg;
	xmlNode *res;
	xmlChar *val;
	char buf[UINT32_MAX_BITS];
	long count;
	int ret;

	if (!(val = xmlGetProp(node, BAD_CAST OBIX_ATTR_VAL))) {
		log_error("Illegal watchOut member without val attr:\n%s",
				  xml_dump_node(node));
		return OBIX_ERR_SERVER_ERROR;
	}

	ret = xmlStrcmp(val, BAD_CAST XML_FALSE);
	xmlFree(val);

	/*
	 * Ignore the false-positive notification of changes, since
	 * we are waiting for the reset to be toggled as "true"
	 */
	if (ret == 0) {
		return OBIX_SUCCESS;
	}

	/*
	 * Reset core data structures
	 */
	pthread_mutex_lock(&dev->mutex);
	dev->time = 0;
	pthread_mutex_unlock(&dev->mutex);

	if (obix_reset_task(&dev->obix_updater) < 0) {
		log_error("Failed to reset the timer task");
	}

	/*
	 * Increase reset counter by 1
	 */
	if ((ret = obix_read_value(handle, OBIX_CONNECTION_ID, dev->history_name,
							   example_nodes[DEV_NODE_COUNTER].param,
							   &val)) != OBIX_SUCCESS) {
		log_error("Failed to read %s", example_nodes[DEV_NODE_COUNTER].param);
	} else if (str_to_long((char *)val, &count) == 0) {
		count++;
		sprintf(buf, "%ld", count);

		if ((ret = obix_write_value(handle, OBIX_CONNECTION_ID,
									dev->history_name,
									example_nodes[DEV_NODE_COUNTER].param,
									buf,
									example_nodes[DEV_NODE_COUNTER].tag)) != OBIX_SUCCESS) {
			log_error("Failed to update %s", example_nodes[DEV_NODE_COUNTER].param);
		}
	}

	if (val) {
		xmlFree(val);
	}

	/*
	 * Reset device contract on the oBIX server and verify
	 * the result
	 */
	if ((ret = obix_batch_send(handle, dev->batch)) != OBIX_SUCCESS) {
		log_error("Failed to send batchIn contract to oBIX server");
		return ret;
	}

	if ((ret = obix_batch_get_result(dev->batch,
									 example_nodes[DEV_NODE_TIME].param,
									 &res)) != OBIX_SUCCESS) {
		log_error("Failed to get relevant node in batchOut contract");
		return ret;
	}

	if (!(val = xmlGetProp(res, BAD_CAST OBIX_ATTR_VAL))) {
		log_error("Illegal watchOut member without val attr:\n%s",
				  xml_dump_node(res));
		return OBIX_ERR_SERVER_ERROR;
	}

	ret = xmlStrcmp(val, BAD_CAST example_nodes[DEV_NODE_TIME].defval);
	xmlFree(val);

	if (ret != 0) {
		log_error("%s not properly reset", example_nodes[DEV_NODE_TIME].param);
		return OBIX_ERR_INVALID_STATE;
	}

	return OBIX_SUCCESS;
}

void obix_updater_task(void *arg)
{
	example_dev_t *dev = (example_dev_t *)arg;
	char *reltime;
	long t;

	if (flag_exit == 1) {
		return;
	}

	pthread_mutex_lock(&dev->mutex);
	dev->time += dev->updater_period;
	t = dev->time;
	pthread_mutex_unlock(&dev->mutex);

	reltime = obix_reltime_from_long(t, RELTIME_DAY);

	/*
	 * For a simple application that won't race for one
	 * CURL handle, the default one of the current connection
	 * can be safely manipulated
	 */
	if (obix_write_value(NULL, OBIX_CONNECTION_ID, dev->history_name,
						 example_nodes[DEV_NODE_TIME].param,
						 reltime,
						 example_nodes[DEV_NODE_TIME].tag) != OBIX_SUCCESS) {
		log_error("Failed to update %s on Device %s",
				  example_nodes[DEV_NODE_TIME].param, dev->history_name);

		/* Go on to try to append history record */
	}

	if (dev->mtime_ts) {
		free(dev->mtime_ts);
	}

	if (!(dev->mtime_ts = get_utc_timestamp(time(NULL)))) {
		log_error("Failed to get timestamp for current moment");
		goto failed;
	}

	if (example_append_history(dev, reltime) != OBIX_SUCCESS) {
		log_error("Failed to append history record for %s", dev->history_name);
	}

	/* Fall through */

failed:
	free(reltime);
}

static int example_destroy_listener(example_dev_t *dev)
{
	int ret;

	if ((ret = obix_unregister_listener(OBIX_CONNECTION_ID, dev->history_name,
						example_nodes[DEV_NODE_RESET].param)) != OBIX_SUCCESS) {
		log_error("Failed to unregister the listener on %s",
				  example_nodes[DEV_NODE_RESET].param);
	} else {
		log_debug("Successfully unregistered the listener on %s",
				  example_nodes[DEV_NODE_RESET].param);
	}

	return ret;
}

static int example_setup_listener(example_dev_t *dev)
{
	xmlDoc *doc = NULL;
	char *data;
	int ret;

	if ((ret = obix_register_listener(OBIX_CONNECTION_ID, dev->history_name,
									  example_nodes[DEV_NODE_RESET].param,
									  example_reset_cb,
									  dev)) != OBIX_SUCCESS) {
		log_error("Failed to register a listener for %s on Device %s",
				  example_nodes[DEV_NODE_RESET].param, dev->history_name);
		return ret;
	}

	if ((ret = obix_refresh_listeners(OBIX_CONNECTION_ID, dev->history_name,
									  &doc)) != OBIX_SUCCESS) {
		log_error("Failed to get a list of all listeners");
		return ret;
	}

	data = xml_dump_node(xmlDocGetRootElement(doc));
	xmlFreeDoc(doc);

	if (data) {
		log_debug("The list of monitored objects are:\n %s", data);
		free(data);
	}

	return OBIX_SUCCESS;
}

static void example_destroy_batch(example_dev_t *dev)
{
	obix_batch_destroy(dev->batch);
}

static int example_setup_batch(example_dev_t *dev)
{
	int ret, i;

	if (!(dev->batch = obix_batch_create(OBIX_CONNECTION_ID))) {
		log_error("Failed to create a batch object");
		return OBIX_ERR_NO_MEMORY;
	}

	for (i = 0; i < DEV_NODE_MAX; i++) {
		if ((ret = obix_batch_write_value(dev->batch, dev->history_name,
										  example_nodes[i].param,
										  example_nodes[i].defval,
										  example_nodes[i].tag)) != OBIX_SUCCESS) {
			log_error("Failed to load the batch object with cmd for %s on "
					  "Device %s", example_nodes[i].param, dev->history_name);
			goto failed;
		}
	}

	/*
	 * Reset the device contract just in case it existed on the oBIX server
	 * in the first place
	 */
	obix_batch_send(NULL, dev->batch);

	/*
	 * The value of the reset counter should be carried on for each reset
	 * event, therefore remove relevant command from the batch object
	 */
	if (obix_batch_remove_command(dev->batch, dev->history_name,
					  example_nodes[DEV_NODE_COUNTER].param) != OBIX_SUCCESS) {
		log_error("Failed to remove command for %s on Device %s from batch",
				  example_nodes[DEV_NODE_COUNTER].param, dev->history_name);
	}

	return OBIX_SUCCESS;

failed:
	obix_batch_destroy(dev->batch);
	return ret;
}

int main(int argc, char *argv[])
{
	example_dev_t *dev;
	int ret = OBIX_SUCCESS;

	if (argc != 4) {
		printf("Usage: %s <devices_config_file> <obix_config_file> "
			   "<history_template_file>\n", argv[0]);
		return -1;
	}

	if (signal(CLEANUP_SIGNAL, example_signal_handler) == SIG_ERR) {
		log_error("Failed to register cleanup signal handler");
		return -1;
	}

	xml_parser_init();

	if (!(dev = example_setup_dev(argv[1]))) {
		ret = OBIX_ERR_INVALID_ARGUMENT;
		goto setup_failed;
	}

	if ((ret = obix_setup_connections(argv[2])) != OBIX_SUCCESS) {
		goto obix_failed;
	}

	if ((ret = obix_open_connection(OBIX_CONNECTION_ID)) != OBIX_SUCCESS) {
		goto open_failed;
	}

	if (!(dev->history = xml_config_create(NULL, argv[3]))) {
		log_error("Failed to setup history template: %s not valid", argv[3]);
		ret = OBIX_ERR_INVALID_ARGUMENT;
		goto template_failed;
	}

	if ((ret = example_register_dev(dev)) != OBIX_SUCCESS) {
		goto register_failed;
	}

	if ((ret = example_setup_batch(dev)) != OBIX_SUCCESS) {
		log_error("Failed to setup batch object");
		goto batch_failed;
	}

	if (obix_schedule_task(&dev->obix_updater) < 0) {
		log_error("Failed to start the obix_updater thread");
		ret = OBIX_ERR_BAD_CONNECTION_HW;
		goto task_failed;
	}

	if ((ret = example_setup_listener(dev)) != OBIX_SUCCESS) {
		log_error("Failed to setup listener thread");
		goto listener_failed;
	}

	/*
	 * Suspend and wait for a signal sent from command line by human user.
	 * Since human users are not likely to hit the Ctrl+C keys twice with
	 * an interval so short as to have the second signal handler race
	 * against the main thread reading from the flag, there is no need to
	 * block relevant signal while accessing the flag.
	 */
	while (1) {
		pause();

		if (flag_exit == 1) {
			log_debug("Begin to shutdown gracefully...");
			break;
		}
	}

	example_destroy_listener(dev);

	/* Fall through */

listener_failed:
	obix_cancel_task(&dev->obix_updater);

	/*
	 * Since the obix_updater thread uses the default CURL handle
	 * of current Connection as well, query history records only
	 * after it has been stopped properly
	 */
	if (ret == OBIX_SUCCESS) {
		example_save_history(dev);
	}

task_failed:
	example_destroy_batch(dev);

batch_failed:
	example_unregister_dev(dev);

register_failed:
	xml_config_free(dev->history);

template_failed:
	obix_destroy_connection(OBIX_CONNECTION_ID);

open_failed:
	obix_destroy_connections();

obix_failed:
	example_destroy_dev(dev);

setup_failed:
	xml_parser_exit();

	return ret;
}

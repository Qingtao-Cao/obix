/* *****************************************************************************
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
 * *****************************************************************************/

#include "obix_utils.h"
#include "xml_utils.h"
#include "xml_config.h"
#include "log_utils.h"

/*
 * XPath predicates used by both the client side and server side
 */
static const char *XP_LOG_LEVEL = "/config/log/level";
static const char *XP_LOG_FACILITY = "/config/log/facility";

/*
 * XPath predicates used by the server side
 */
const char *XP_LISTEN_SOCKET = "/config/listen_socket";
const char *XP_LISTEN_BACKLOG = "/config/listen_backlog";
const char *XP_MULTI_THREADS = "/config/multi_threads";
const char *XP_POLL_THREADS = "/config/poll_threads";
const char *XP_DEV_TABLE_SIZE = "/config/dev_table_size";
const char *XP_DEV_CACHE_SIZE = "/config/dev_cache_size";
const char *XP_DEV_BACKUP_PERIOD = "/config/dev_backup_period";

/*
 * XPath predicates used by the client side
 */
const char *XP_CT = "/config/connection";
const char *CT_ID = "id";
const char *CT_TYPE = "type";
const char *CT_SERVER_IP = "server_ip";
const char *CT_SERVER_LOBBY = "server_lobby";
const char *CT_POLL_INTERVAL= "poll_interval";
const char *CT_LP_MIN = "long_poll_min";
const char *CT_LP_MAX = "long_poll_max";
const char *CT_CURL_TIMEOUT = "curl_timeout";
const char *CT_CURL_BULKY = "curl_bulky";
const char *CT_CURL_NOSIGNAL = "curl_nosignal";

#define ARRAY_LEN(a) (size_t) (sizeof(a) / sizeof(a[0]))

typedef struct {
	const char *name;
	const int level;
} log_level_t;

static const log_level_t log_levels[] = {
	{
		.name = "debug",
		.level = LOG_LEVEL_DEBUG
	},
	{
		.name = "error",
		.level = LOG_LEVEL_ERROR
	},
	{
		.name = "warning",
		.level = LOG_LEVEL_WARNING
	},
	{
		.name = "no",
		.level = LOG_LEVEL_NO
	}
};

typedef struct {
	const char *name;
	int facility;
} syslog_facility_t;

static const syslog_facility_t syslog_facilities[] = {
	{
		.name = "user",
		.facility = LOG_USER
	},
	{
		.name = "local0",
		.facility = LOG_LOCAL0
	},
	{
		.name = "local1",
		.facility = LOG_LOCAL1
	},
	{
		.name = "local2",
		.facility = LOG_LOCAL2
	},
	{
		.name = "local3",
		.facility = LOG_LOCAL3
	},
	{
		.name = "local4",
		.facility = LOG_LOCAL4
	},
	{
		.name = "local5",
		.facility = LOG_LOCAL5
	},
	{
		.name = "local6",
		.facility = LOG_LOCAL6
	},
	{
		.name = "local7",
		.facility = LOG_LOCAL7
	}
};

static int get_log_facility(const char *name)
{
	int facility = LOG_USER;
	int i;

	for (i = 0; i < ARRAY_LEN(syslog_facilities); i++) {
		if (strcmp(syslog_facilities[i].name, name) == 0) {
			facility = syslog_facilities[i].facility;
			break;
		}
	}

	return facility;
}

static int get_log_level(const char *name)
{
	int level = LOG_LEVEL_DEBUG;
	int i;

	for (i = 0; i < ARRAY_LEN(log_levels); i++) {
		if (strcmp(log_levels[i].name, name) == 0) {
			level = log_levels[i].level;
			break;
		}
	}

	return level;
}

void xml_parser_init(void)
{
	xmlKeepBlanksDefault(0);
	xmlInitParser();
}

void xml_parser_exit(void)
{
	xmlCleanupParser();
}

void xml_config_free(xml_config_t *config)
{
	if (!config) {
		return;
	}

	if (config->resdir) {
		free(config->resdir);
	}

	if (config->file) {
		free(config->file);
	}

	if (config->doc) {
		xmlFreeDoc(config->doc);
	}

	if (config->xpc) {
		xmlXPathFreeContext(config->xpc);
	}

	free(config);
}

xml_config_t *xml_config_create(const char *dir, const char *file)
{
	xml_config_t *config;

	if (!file) {	/* dir could be NULL */
		log_error("Illegal parameters provided");
		return NULL;
	}

	if (!(config = (xml_config_t *)malloc(sizeof(xml_config_t)))) {
		log_error("Failed to allocate xml_config_t");
		return NULL;
	}
	memset(config, 0, sizeof(xml_config_t));

	if (dir) {
		if (!(config->resdir = strdup(dir))) {
			log_error("Failed to duplicate the resource dir path");
			goto failed;
		}

		if (link_pathname(&config->file, dir, NULL, file, NULL) < 0) {
			log_error("Failed to assemble absolute path for %s", file);
			goto failed;
		}
	} else {
		if (!(config->file = strdup(file))) {
			goto failed;
		}
	}

	if (!(config->doc = xmlReadFile(config->file, NULL,
									XML_PARSE_OPTIONS_COMMON)) ||
		!(config->xpc = xmlXPathNewContext(config->doc))) {
		log_error("Failed to setup XPath for %s", config->file);
		goto failed;
	}

	config->root = xmlDocGetRootElement(config->doc);
	return config;

failed:
	xml_config_free(config);
	return NULL;
}

xmlNode *xml_config_get_node(const xml_config_t *config, const char *pattern)
{
	xmlXPathObject *objs;
	xmlNode *node = NULL;

	if (!(objs = xmlXPathEval(BAD_CAST pattern, config->xpc))) {
		log_error("Failed to apply XPath Predicate %s", pattern);
		return NULL;
	}

	/*
	 * Return the pointer of the first matching node
	 */
	if (xmlXPathNodeSetIsEmpty(objs->nodesetval) == 0) {
		node = xmlXPathNodeSetItem(objs->nodesetval, 0);
	}

	/*
	 * Release the table of xmlNode pointers won't impact
	 * the nodes pointed to in relevant document at all
	 */
	xmlXPathFreeObject(objs);
	return node;
}

char *xml_config_get_str(const xml_config_t *config, const char *pattern)
{
	xmlNode *node;

	if (!(node = xml_config_get_node(config, pattern))) {
		log_error("The XPath Predicate %s does not match anything", pattern);
		return NULL;
	}

	return (char *)xmlGetProp(node, BAD_CAST OBIX_ATTR_VAL);
}

int xml_config_get_int(const xml_config_t *config, const char *pattern)
{
	char *str;
	long l;
	int ret;

	if (!(str = xml_config_get_str(config, pattern))) {
		return -1;
	}

	ret = str_to_long(str, &l);
	free(str);

	return (ret == 0) ? l : ret;
}

int xml_config_get_float(const xml_config_t *config, const char *pattern,
						 float *val)
{
	char *str;
	int ret;

	if (!(str = xml_config_get_str(config, pattern))) {
		return -1;
	}

	ret = str_to_float(str, val);
	free(str);

	return ret;
}

int xml_config_for_each_obj(const xml_config_t *config, const char *pattern,
							xml_config_cb_t cb, void *arg1, void *arg2)
{
	xmlXPathObject *objs;
	xmlNode *node;
	int i, ret = 0;

	if (!(objs = xmlXPathEval(BAD_CAST pattern, config->xpc))) {
		return -1;
	}

	if (xmlXPathNodeSetIsEmpty(objs->nodesetval) == 0) {
		for (i = 0; i < xmlXPathNodeSetGetLength(objs->nodesetval); i++) {
			if (!(node = xmlXPathNodeSetItem(objs->nodesetval, i))) {
				continue;
			}

			if ((ret = cb(node, arg1, arg2)) < 0) {
				break;
			}
		}
	}

	xmlXPathFreeObject(objs);
	return ret;
}

int xml_config_log(const xml_config_t *config)
{
	char *facility = NULL, *level = NULL;
	int ret = OBIX_ERR_INVALID_ARGUMENT;

	if (!(level = xml_config_get_str(config, XP_LOG_LEVEL))) {
		log_error("Failed to get %s settings", XP_LOG_LEVEL);
		goto failed;
	}

	if (!(facility = xml_config_get_str(config, XP_LOG_FACILITY))) {
		log_error("Failed to get %s settings", XP_LOG_FACILITY);
		goto failed;
	}

	log_useSyslog(get_log_facility(facility));
	log_setLevel(get_log_level(level));

	ret = OBIX_SUCCESS;

	/* Fall through */

failed:
	if (facility) {
		free(facility);
	}

	if (level) {
		free(level);
	}

	return ret;
}

int xml_config_get_template(xmlNode *node, void *arg1, void *arg2)
{
	*(xmlNode **)arg1 = node;	/* arg2 ignored */

	/*
	 * Deliberately return an error code in order to
	 * return the pointer of the first matching node
	 */
	return OBIX_ERR_INVALID_ARGUMENT;
}

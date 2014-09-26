/* *****************************************************************************
 * Copyright (c) 2013-2014 Qingtao Cao [harry.cao@nextdc.com]
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

#ifndef XML_CONFIG_H
#define	XML_CONFIG_H

#include <string.h>
#include <syslog.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>

extern const char *XP_POLL_THREAD_COUNT;
extern const char *XP_CT;

extern const char *CT_ID;
extern const char *CT_TYPE;
extern const char *CT_SERVER_IP;
extern const char *CT_SERVER_LOBBY;
extern const char *CT_POLL_INTERVAL;
extern const char *CT_LP_MIN;
extern const char *CT_LP_MAX;
extern const char *CT_CURL_TIMEOUT;
extern const char *CT_CURL_BULKY;

typedef struct xml_config {
	char *resdir;
	char *file;
	xmlDoc *doc;
	xmlXPathContext *xpc;
	xmlNode *root;
} xml_config_t;

typedef int (*xml_config_cb_t)(xmlNode *, void *, void *);

void xml_parser_init(void);
void xml_parser_exit(void);

xml_config_t *xml_config_create(const char *, const char *);
void xml_config_free(xml_config_t *);

xmlNode *xml_config_get_node(const xml_config_t *, const char *);
char *xml_config_get_str(const xml_config_t *, const char *);
int xml_config_get_int(const xml_config_t *, const char *);
int xml_config_get_float(const xml_config_t *, const char *, float *);

int xml_config_for_each_obj(const xml_config_t *, const char *,
							xml_config_cb_t, void *, void *);

int xml_config_log(const xml_config_t *);
int xml_config_get_template(xmlNode *, void *, void *);

#endif

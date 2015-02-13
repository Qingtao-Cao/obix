/* *****************************************************************************
 * Copyright (c) 2013-2015 Qingtao Cao
 * Copyright (c) 2014 Tyler Watson
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

#ifndef _XML_UTILS_H_
#define _XML_UTILS_H_

#include <libxml/tree.h>
#include <libxml/parser.h>

/*
 * Common XML parser options, a XML parser user may OR other options
 *
 * XML_PARSE_NONET - prevent XXE attacks
 * XML_PARSE_NOBLANKS - skip blank content
 */
#define XML_PARSE_OPTIONS_COMMON	(XML_PARSE_NONET | XML_PARSE_NOBLANKS)

extern const char *XML_HEADER;
extern const int XML_HEADER_LEN;
extern const char *XML_VERSION;

/**
 * Defines a list of exclusion elements that xml_copy is to obey.
 *
 * TODO:
 * 1. If xml_copy is solely used by oBIX server than any clients,
 * then move that function to xml_storage.c and combine the enum
 * defined below with xmldb_dom_action_t
 */
typedef enum {
	EXCLUDE_HIDDEN = 1,
	EXCLUDE_META = (1 << 1),
	EXCLUDE_COMMENTS = (1 << 2)
} xml_copy_flags_t;

typedef enum obix_root {
	OBIX_BATCH = 0,
	OBIX_DEVICE,
	OBIX_WATCH,
	OBIX_HISTORY
} obix_root_t;

typedef struct href_info {
	const xmlChar *root;
	const int len;
} href_info_t;

extern href_info_t obix_roots[];

int is_given_type(const xmlChar *href, obix_root_t type);

int is_str_identical(const xmlChar *str1, const xmlChar *str2, const int lenient);

xmlNode *xml_copy(const xmlNode *src, xml_copy_flags_t flag);

typedef int (*xml_item_cb_t)(xmlNode **item, void *arg1, void *arg2);

int xml_for_each_node_type(xmlNode *rootNode, xmlElementType type,
						   xml_item_cb_t callback,
						   void *arg1, void *arg2, int depth);

int xml_for_each_element(xmlNode *rootNode, xml_item_cb_t callback,
					     void *arg1, void *arg2);

int xml_for_each_comment(xmlNode *rootNode, xml_item_cb_t callback,
					     void *arg1, void *arg2);

xmlNode *xml_find_child(const xmlNode *parent, const char *tag,
						const char *attrName, const char *attrVal);

long xml_get_long(xmlNode *inputNode, const char *attrName);

char *xml_get_child_href(const xmlNode *parent, const char *tag,
						const char *nameVal);

char *xml_get_child_val(const xmlNode *parent, const char *tag,
						const char *nameVal);

long xml_get_child_long(const xmlNode *parent, const char *tag,
						const char *nameVal);

int xml_is_hidden(const xmlNode *node);

int xml_is_null(const xmlNode *node);

int xml_for_each_ancestor_or_self(xmlNode *start, xmlNode *stop,
								  xml_item_cb_t callback,
								  void *arg1, void *arg2);

char *xml_dump_node(const xmlNode *node);

xmlNode *obix_obj_null(const xmlChar *href);

void xml_delete_node(xmlNode *node);
void xml_remove_children(xmlNode *parent);

#ifdef DEBUG
int xml_is_valid_doc(const char *, const char *);
#endif

int xml_is_valid_href(const xmlChar *);

xmlNode *xml_create_ref_node(xmlNode *src, const xmlChar *href);

int xml_write_file(const char *path, int flags, const char *data, int size);

void xml_setup_private(xmlNode *node, void *arg);

#endif	/* _XML_UTILS_H_ */

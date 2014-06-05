/* *****************************************************************************
 * Copyright (c) 2014 Tyler Watson <tyler.watson@nextdc.com>
 * Copyright (c) 2013-2014 Qingtao Cao [harry.cao@nextdc.com]
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 * *****************************************************************************/

#ifndef LIBXML_CONFIG_H
#define	LIBXML_CONFIG_H

#define ARRAY_LEN(a) (size_t) (sizeof(a) / sizeof(a[0]))

#include <string.h>
#include <syslog.h>

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>

#ifdef	__cplusplus
extern "C" {
#endif

    /* An XML configuration structure, holding the path to the document and pointers to the entire DOM structure
     */
    typedef struct _xml_config {
        char *resourcePath;
        xmlDoc *document;
        xmlNode *rootNode;
        xmlXPathContext *xpathContext;
    } xml_config_t;

	void xml_parser_init(void);

    /**
     * Allocates a new xml_config structure
     * @return  a pointer to a new xml_config structure, ready to call xml_config_initialize on
     * @remark  This function returns an allocated object.  It is up to the caller to free the memory
     *          allocated by this function.
     */
    xml_config_t *xml_config_create(const char *resourcePath, const char *configFileName);

    /**
     * Frees an allocated xml_config structure as well as all it's managed resources
     */
    void xml_config_free(xml_config_t *context);

    /**
     * Parses log nodes and sets up logging parameters.
     * @param context a pointer to an allocated and initialized xml_config structure.
     */
    void xml_parse_logging(xml_config_t *context);

	int xml_parse_threads(xml_config_t *context, const char *tag);

    void xml_path_combine(char *destination, const char *path1, const char *path2);

#ifdef	__cplusplus
}
#endif

#endif	/* LIBXML_CONFIG_H */

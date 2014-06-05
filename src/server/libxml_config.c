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

#include "obix_utils.h"
#include "assert.h"
#include "libxml_config.h"
#include "log_utils.h"

/*
 * Default value of the maximum number of oBIX server threads which
 * run in parallel. If it is not specified in the server config file,
 * fall back on the default value
 */
#define THREAD_COUNT_MAX	20

   /* Static map construct that maps log level strings to their numerical counterparts.
     */
    static const struct {
        const char *logName;
        const int logLevel;
    } log_levels[] = {
        { "debug", LOG_LEVEL_DEBUG},
        { "error", LOG_LEVEL_ERROR},
        { "warning", LOG_LEVEL_WARNING},
        { "no", LOG_LEVEL_NO}
    };

    /**
     * Static map struct that maps friendly names in a string to their compiled macro definitions.
     */
    static const struct {
        const char *facilityName;
        int facility;
    } syslog_facilities[] = {
        /* POSIX only specifies USER and LOCAL0 - LOCAL7 */
        { "user", LOG_USER},
        { "local0", LOG_LOCAL0},
        { "local1", LOG_LOCAL1},
        { "local2", LOG_LOCAL2},
        { "local3", LOG_LOCAL3},
        { "local4", LOG_LOCAL4},
        { "local5", LOG_LOCAL5},
        { "local6", LOG_LOCAL6},
        { "local7", LOG_LOCAL7},
#if defined(HAVE_SYSLOG_FACILITY_LOG_KERN)
        { "kern", LOG_KERN},
#endif
#if defined(HAVE_SYSLOG_FACILITY_LOG_MAIL)
        { "mail", LOG_MAIL},
#endif
#if defined(HAVE_SYSLOG_FACILITY_LOG_DAEMON)
        { "daemon", LOG_DAEMON},
#endif
#if defined(HAVE_SYSLOG_FACILITY_LOG_AUTH)
        { "auth", LOG_AUTH},
        { "security", LOG_AUTH},
#endif
#if defined(HAVE_SYSLOG_FACILITY_LOG_AUTHPRIV)
        { "authpriv", LOG_AUTHPRIV},
#endif
#if defined(HAVE_SYSLOG_FACILITY_LOG_SYSLOG)
        { "syslog", LOG_SYSLOG},
#endif
#if defined(HAVE_SYSLOG_FACILITY_LOG_FTP)
        { "ftp", LOG_FTP},
#endif
#if defined(HAVE_SYSLOG_FACILITY_LOG_LPR)
        { "lpr", LOG_LPR},
#endif
#if defined(HAVE_SYSLOG_FACILITY_LOG_NEWS)
        { "news", LOG_NEWS},
#endif
#if defined(HAVE_SYSLOG_FACILITY_LOG_UUCP)
        { "uucp", LOG_UUCP},
#endif
#if defined(HAVE_SYSLOG_FACILITY_LOG_CRON)
        { "cron", LOG_CRON},
#endif
    };

/**
 * Prepares the XML parser for use in the oBIX FCGI program
 */
void xml_parser_init(void)
{
	xmlKeepBlanksDefault(0);
	xmlInitParser();
}

static int xml_syslog_facility(const char *facilityName) {
	int facility = LOG_USER;
	int i;

	for (i = 0; i < ARRAY_LEN(syslog_facilities); i++) {
		if (!strcasecmp(syslog_facilities[i].facilityName, facilityName)) {
			facility = syslog_facilities[i].facility;
			break;
		}
	}

	return facility;
}

/**
 * Returns the log level index for the provided log level name pointed to by @a lLevel
 * @param lLevel A pointer to the syslog level name
 * @return an integer indicating the log level of the provided name, or the default @a LOG_LEVEL_DEBUG
 */
static int xml_log_level(const char *lLevel) {
	int logLevel = LOG_LEVEL_DEBUG;
	int i;

	for (i = 0; i < ARRAY_LEN(log_levels); i++) {
		if (!strcasecmp(log_levels[i].logName, lLevel)) {
			logLevel = log_levels[i].logLevel;
			break;
		}
	}

	return logLevel;
}

xml_config_t *xml_config_create(const char *resourcePath,
								const char *configFileName)
{
	xml_config_t *config = NULL;
	char *concatenatedPath = NULL;

	assert(resourcePath && configFileName);

	if (link_pathname(&concatenatedPath, resourcePath,
					  NULL, configFileName, NULL) < 0) {
		log_error("Failed to assemble absolute path for %s", configFileName);
		return NULL;
	}

	config = (xml_config_t *)malloc(sizeof(xml_config_t));
	if (config == NULL) {
		log_error("Failed to create xml_config_t");
		goto failed;
	}

	memset(config, 0, sizeof(xml_config_t));

	if (!(config->resourcePath = (char *)malloc(strlen(resourcePath) + 1))) {
		log_error("Failed to duplicate %s", resourcePath);
		goto resource_path_failed;
	}
	strcpy(config->resourcePath, resourcePath);

	config->document = xmlParseFile(concatenatedPath);
	if (config->document == NULL) {
		log_error("Could not load XML configuration file path %s.", concatenatedPath);
		goto document_failed;
	}

	config->xpathContext = xmlXPathNewContext(config->document);
	if (config->xpathContext == NULL) {
		log_error("Could not allocate an XPath context for XML document %s", configFileName);
		goto xpathcontext_failed;
	}

	config->rootNode = xmlDocGetRootElement(config->document);

	free(concatenatedPath);

	return config;

xpathcontext_failed:
	xmlFreeDoc(config->document);

document_failed:
	free(config->resourcePath);

resource_path_failed:
	free(config);

failed:
	free(concatenatedPath);
	return NULL;
}

void xml_config_free(xml_config_t *context)
{
	if (context->resourcePath) {
		free(context->resourcePath);
	}

	if (context->document) {
		xmlFreeDoc(context->document);
	}

	if (context->xpathContext) {
		xmlXPathFreeContext(context->xpathContext);
	}

	free(context);
}


void xml_parse_logging(xml_config_t *context)
{
	xmlXPathObject *logLevelObject;
	xmlXPathObject *useSyslogObject;

	int logFacility = LOG_USER; //default syslog facility is user
	int logLevel = LOG_LEVEL_DEBUG; //default logging logLevel is debug

	if ((logLevelObject = xmlXPathEvalExpression(BAD_CAST "/config/log/level/@val", context->xpathContext)) != NULL) {
		xmlChar *nodeValue = xmlNodeGetContent(logLevelObject->nodesetval->nodeTab[0]); //alloc

		if (nodeValue != NULL) {
			logLevel = xml_log_level((char *) nodeValue);
			xmlFree(nodeValue);
		} else {
			log_error("could not retrieve the value of XPath Query /config/log/level/@val");
		}

		xmlXPathFreeObject(logLevelObject);
	}

	//use-syslog is not required, if it isn't present than messages don't go to syslog
	if ((useSyslogObject = xmlXPathEval(BAD_CAST "/config/log/use-syslog/@facility", context->xpathContext)) != NULL) {
		xmlChar *nodeValue = xmlNodeGetContent(xmlXPathNodeSetItem(useSyslogObject->nodesetval,0));
		if (nodeValue != NULL) {
			logFacility = xml_syslog_facility((char *) nodeValue);
			log_useSyslog(logFacility);
			xmlFree(nodeValue);
		} else {
			log_error("Could not load content from node /config/log/use-syslog/@facility.");
		}
		xmlXPathFreeObject(useSyslogObject);
	} else {
		log_usePrintf();
	}

	log_setLevel(logLevel);
}

/*
 * Get the optional threads settings of oBIX server
 *
 * Return the threads number on success, < 0 on errors.
 */
int xml_parse_threads(xml_config_t *config, const char *tag)
{
	xmlXPathObject *threadMaxCountObj;
	xmlChar *nodeValue = NULL;
	int num = THREAD_COUNT_MAX;

	threadMaxCountObj = xmlXPathEval(BAD_CAST tag, config->xpathContext);
	if (threadMaxCountObj != NULL) {
		if (xmlXPathNodeSetIsEmpty(threadMaxCountObj->nodesetval) == 0) {
			nodeValue = xmlNodeGetContent(
							xmlXPathNodeSetItem(threadMaxCountObj->nodesetval, 0));
			if (nodeValue != NULL) {
				num = atoi((const char *)nodeValue);
				xmlFree(nodeValue);
			}
		}

		xmlXPathFreeObject(threadMaxCountObj);
	}

	return num;
}

/* *****************************************************************************
 * Copyright (c) 2013-2014 Qingtao Cao [harry.cao@nextdc.com]
 * Copyright (c) 2009 Andrey Litvinov
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
 * along with oBIX.  If not, see <http://www.gnu.org/licenses/>.
 *
 * *****************************************************************************/

#ifndef OBIX_UTILS_H_
#define OBIX_UTILS_H_

#include <libxml2/libxml/tree.h>
#include <stdbool.h>

/** @name oBIX Error Contracts' URIs
 * Can be used to define the error type returned by an oBIX server.
 *
 * Note,
 * 1. They must be defined as macro instead of const char * so as to
 * be qualified to initialize global variables at compile time.
 * @{
 */
#define OBIX_CONTRACT_ERR_BAD_URI		"obix:BadUriErr"
#define OBIX_CONTRACT_ERR_UNSUPPORTED	"obix:UnsupportedErr"
#define OBIX_CONTRACT_ERR_PERMISSION	"obix:PermissionErr"
#define OBIX_CONTRACT_ERR_SERVER		"obix:ServerErr"
/** @} */

/** @name oBIX Object Types (XML Element Types)
 * @{
 */
/** oBIX Object (@a obj) */
extern const char* OBIX_OBJ;
/** oBIX Reference (@a ref) */
extern const char* OBIX_OBJ_REF;
/** oBIX Operation (@a op) */
extern const char* OBIX_OBJ_OP;
/** oBIX List (@a list) */
extern const char* OBIX_OBJ_LIST;
/** oBIX Error (@a err) */
extern const char* OBIX_OBJ_ERR;
/** oBIX Boolean (@a bool) */
extern const char* OBIX_OBJ_BOOL;
/** oBIX Integer (@a int) */
extern const char* OBIX_OBJ_INT;
/** oBIX Real (@a real) */
extern const char* OBIX_OBJ_REAL;
/** oBIX String (@a str) */
extern const char* OBIX_OBJ_STR;
/** oBIX Enumeration (@a enum) */
extern const char* OBIX_OBJ_ENUM;
/** oBIX Absolute Time (@a abstime) */
extern const char* OBIX_OBJ_ABSTIME;
/** oBIX Relative Duration of Time (@a reltime) */
extern const char* OBIX_OBJ_RELTIME;
/** oBIX URI (@a uri) */
extern const char* OBIX_OBJ_URI;
/** oBIX Feed (@a feed) */
extern const char* OBIX_OBJ_FEED;

extern const char *OBIX_OBJ_META;
extern const char *OBIX_OBJ_DATE;

/** @} */

/** @name oBIX Object Names
 * Object names (value of @a name attributes), which are used in oBIX contracts.
 * @{
 */
/** Name of @a signUp operation in the Lobby object. */
extern const char* OBIX_NAME_SIGN_UP;
/** Name of @a batch operation in the Lobby object. */
extern const char* OBIX_NAME_BATCH;
/** Name of the Watch Service in the Lobby object. */
extern const char* OBIX_NAME_WATCH_SERVICE;
/** Name of the @a watchService.make operation. */
extern const char* OBIX_NAME_WATCH_SERVICE_MAKE;
/** Name of the @a Watch.add operation. */
extern const char* OBIX_NAME_WATCH_ADD;
/** Name of the @a Watch.remove operation. */
extern const char* OBIX_NAME_WATCH_REMOVE;
/** Name of the @a Watch.pollChanges operation. */
extern const char* OBIX_NAME_WATCH_POLLCHANGES;
/** Name of the @a Watch.pollRefresh operation. */
extern const char* OBIX_NAME_WATCH_POLLREFRESH;
/** Name of the @a Watch.delete operation. */
extern const char* OBIX_NAME_WATCH_DELETE;
/** Name of the @a Watch.lease parameter. */
extern const char* OBIX_NAME_WATCH_LEASE;
/** Name of the @a Watch.pollWaitInterval object. */
extern const char* OBIX_NAME_WATCH_POLL_WAIT_INTERVAL;
/** Name of the @a Watch.pollWaitInterval.min parameter. */
extern const char* OBIX_NAME_WATCH_POLL_WAIT_INTERVAL_MIN;
/** Name of the @a Watch.pollWaitInterval.max parameter. */
extern const char* OBIX_NAME_WATCH_POLL_WAIT_INTERVAL_MAX;
/** @} */

/** String representation of oBIX @a NULL object. */
extern const char* OBIX_OBJ_NULL_TEMPLATE;

/** @name oBIX Object Attributes and Facets
 * @{
 */
/** Object attribute @a "is". */
extern const char* OBIX_ATTR_IS;
/** Object attribute @a "name". */
extern const char* OBIX_ATTR_NAME;
/** Object attribute @a "of" */
extern const char *OBIX_ATTR_OF;
/** Object attribute @a "href". */
extern const char* OBIX_ATTR_HREF;
/** Object attribute @a "val". */
extern const char* OBIX_ATTR_VAL;
/** Object attribute @a "null". */
extern const char* OBIX_ATTR_NULL;
/** oBIX facet @a "writable". */
extern const char* OBIX_ATTR_WRITABLE;
/** oBIX facet @a "display". */
extern const char* OBIX_ATTR_DISPLAY;
/** oBIX facet @a "displayName". */
extern const char* OBIX_ATTR_DISPLAY_NAME;

extern const char *OBIX_ATTR_HIDDEN;
extern const char *OBIX_META_ATTR_OP;
extern const char *OBIX_META_ATTR_WATCH_ID;

/** @} */

/** String representation of boolean @a true value. */
extern const char* XML_TRUE;
/** String representation of boolean @a false value. */
extern const char* XML_FALSE;

extern const char *OBIX_CONTRACT_BATCH_IN;

/** obix:BatchOut */
extern const char *OBIX_CONTRACT_BATCH_OUT;
/** obix:Read */
extern const char *OBIX_CONTRACT_OP_READ;
/** obix:Write */
extern const char *OBIX_CONTRACT_OP_WRITE;
/** obix:Invoke */
extern const char *OBIX_CONTRACT_OP_INVOKE;

/**
 * Specifies a format of @a reltime value, generated by #obix_reltime_fromLong
 */
typedef enum {
    RELTIME_SEC,
    RELTIME_MIN,
    RELTIME_HOUR,
    RELTIME_DAY,
    RELTIME_MONTH,
    RELTIME_YEAR
} RELTIME_FORMAT;

/**
 * Returns the first child node that matches the tag name pointed to by @a tagName from the
 * specified input tree @a inputTree.
 * @param inputTree     The XML Node to search
 * @param tagName       The tag name to compare with
 * @return              The child node, or NULL if not found.
 */
xmlNode *xmlNodeGetFirstChildByTag(const xmlNode *inputTree, const xmlChar *tagName);

/**
 * Returns the value of the XML attribute pointed to by @a attrName from the XML node pointed
 * to by @a inputNode as an integer.
 */
int xmlGetPropInt(const xmlNode *inputNode, const xmlChar *attrName, const int defaultValue);


/**
 * Returns an oBIX Null object, that conforms to the obix:nil contract.
 */
xmlNode *obix_obj_null();

/**
 * Parses string value of @a reltime object and returns corresponding time in
 * milliseconds. Follows @a xs:duration format.
 *
 * @note Durations which can overload @a long variable are not parsed.
 *
 * @param str String value of a @a reltime object which should be parsed.
 * @param duration If parsing is successful than the parsed value will be
 *                 written there.
 * @return @li @a 0 - Operation completed successfully;
 *         @li @a -1 - Parsing error (provided string has bad format);
 *         @li @a -2 - Provided @a reltime value is bigger than or equal to 24
 *                     days (The maximum possible value is
 *                     @a "P23DT23H59M59.999S"). Also this error code is
 *                     returned when the input value is not normalized: If some
 *                     of time components (e.g. hours) presents, than all
 *                     smaller components (minutes and seconds) should represent
 *                     less time than previous component. For example
 *                     @a "PT60M" and @a "PT60S" are allowed, but @a "PT1H60M"
 *                     and @a "PT1H60S" are not.
 */
int obix_reltime_parseToLong(const char* str, long* duration);

/**
 * Generates @a reltime value from the provided time in milliseconds.
 *
 * @param duration Time in milliseconds which should be converted.
 * @param format Format of the generated @a reltime value. Specifies the maximum
 *               time component for the output value. For example, converting
 *               2 minutes with @a RELTIME_MIN will result in @a "PT2M";
 *               @a RELTIME_SEC - @a "PT120S".
 * @return String, which represents provided time in @a xs:duration format, or
 *         @a NULL if memory allocation failed.
 */
char* obix_reltime_fromLong(long duration, RELTIME_FORMAT format);

/**
 * Checks whether oBIX object implements specified contract. Object implements
 * a contract when contract's URI is listed in object's @a is attribute.
 *
 * @param obj XML DOM structure representing an oBIX object.
 * @param contract URI of the contract which should be checked.
 * @return #TRUE if the object implements specified contract, #FALSE otherwise.
 */
bool obix_obj_implementsContract(xmlNode* obj, const char* contract);

long str_to_long(const char *str);
int timespec_compare(const struct timespec *m1, const struct timespec *m2);

typedef int (*load_file_cb_t)(const char *dir, const char *file, void *arg);

int for_each_file_name(const char *dir, const char *prefix,
						const char *suffix, load_file_cb_t cb, void *arg);

int slash_preceded(const char *s);
int slash_followed(const char *s);

typedef int (*token_cb_t)(const char *token, void *arg1, void *arg2);

int for_each_str_token(const char *delimiter, const char *str,
					   token_cb_t cb, void *arg1, void *arg2);

extern const char *STR_DELIMITER_SLASH;
extern const char *STR_DELIMITER_DOT;

int str_token_count_helper(const char *token, void *arg1, void *arg2);

pid_t get_tid(void);

int str_is_identical(const char *str1, const char *str2);

int link_pathname(char **, const char *, const char *, const char *, const char *);

int timestamp_split(const char *, char **, char **);

int timestamp_compare(const char *ts1, const char *ts2, int *res_d, int *res_t);

int timestamp_has_common(const char *start, const char *end,
						 const char *oldest, const char *latest);

int timestamp_find_common(char **start, char **end,
						  const char *oldest, const char *latest);

int time_compare(const char *str1, const char *str2, int *res, int delimiter);

/*
 * Note:
 * No assignment should ever be passed in the macro, or
 * unwanted effect ensue!
 */
#define min(a, b)	(((a) <= (b)) ? (a) : (b))
#define max(a, b)	(((a) >= (b)) ? (a) : (b))

#endif /* OBIX_UTILS_H_ */

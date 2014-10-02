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

#include <libxml/tree.h>

/*
 * Error codes which are returned by library functions
 */
typedef enum {
	OBIX_SUCCESS				= 0,
	OBIX_ERR_INVALID_ARGUMENT	= -1,
	OBIX_ERR_INVALID_STATE		= -2,
	OBIX_ERR_NO_MEMORY			= -3,
	OBIX_ERR_SERVER_ERROR		= -4,
	OBIX_ERR_BAD_CONNECTION_HW	= -5
} OBIX_ERRORCODE;

/*
 * Defined as macro so as to be constant and therefore
 * able to be used as initialiser
 */
#define OBIX_CONTRACT_ERR_BAD_URI		"obix:BadUriErr"
#define OBIX_CONTRACT_ERR_UNSUPPORTED	"obix:UnsupportedErr"
#define OBIX_CONTRACT_ERR_PERMISSION	"obix:PermissionErr"
#define OBIX_CONTRACT_ERR_SERVER		"obix:ServerErr"

#define HIST_REC_TS_MAX_LEN				19

#define OBIX_OBJ				"obj"
#define OBIX_OBJ_REF			"ref"
#define OBIX_OBJ_OP				"op"
#define OBIX_OBJ_LIST			"list"
#define OBIX_OBJ_ERR			"err"
#define OBIX_OBJ_BOOL			"bool"
#define OBIX_OBJ_INT			"int"
#define OBIX_OBJ_REAL			"real"
#define OBIX_OBJ_STR			"str"
#define OBIX_OBJ_ENUM			"enum"
#define OBIX_OBJ_ABSTIME		"abstime"
#define OBIX_OBJ_RELTIME		"reltime"
#define OBIX_OBJ_URI			"uri"
#define OBIX_OBJ_FEED			"feed"
#define OBIX_OBJ_META			"meta"
#define OBIX_OBJ_DATE			"date"

#define HIST_ABS_DATE			"date"
#define HIST_ABS_COUNT			"count"
#define HIST_ABS_START			"start"
#define HIST_ABS_END			"end"

#define OBIX_CONN_HTTP			"http"

extern const char *OBIX_ATTR_IS;
extern const char *OBIX_ATTR_NAME;
extern const char *OBIX_ATTR_OF;
extern const char *OBIX_ATTR_HREF;
extern const char *OBIX_ATTR_VAL;
extern const char *OBIX_ATTR_NULL;
extern const char *OBIX_ATTR_WRITABLE;
extern const char *OBIX_ATTR_DISPLAY;
extern const char *OBIX_ATTR_DISPLAY_NAME;
extern const char *OBIX_ATTR_HIDDEN;

extern const char *OBIX_META_ATTR_OP;
extern const char *OBIX_META_ATTR_WATCH_ID;

extern const char *XML_TRUE;
extern const char *XML_FALSE;
extern const int XML_BOOL_MAX_LEN;

extern const char *OBIX_CONTRACT_OP_READ;
extern const char *OBIX_CONTRACT_OP_WRITE;
extern const char *OBIX_CONTRACT_OP_INVOKE;
extern const char *OBIX_CONTRACT_HIST_AIN;
extern const char *OBIX_CONTRACT_HIST_FLT;
extern const char *OBIX_CONTRACT_HIST_FILE_ABS;
extern const char *OBIX_CONTRACT_BATCH_IN;

extern const char *HIST_OP_APPEND;
extern const char *HIST_OP_QUERY;
extern const char *HIST_INDEX;
extern const char *HIST_REC_TS;
extern const char *HIST_AIN_DATA;
extern const char *HIST_AIN_TS_UND;
extern const char *HIST_TS_INIT;

extern const char *STR_DELIMITER_SLASH;
extern const char *STR_DELIMITER_DOT;

extern const char *OBIX_RELTIME_ZERO;
extern const int OBIX_RELTIME_ZERO_LEN;

extern const char *OBIX_DEVICE_ROOT;
extern const int OBIX_DEVICE_ROOT_LEN;

/*
 * Reserve 10 bits for a physical value
 */
#define HIST_REC_VAL_MAX_LEN	10

/*
 * There would be 315,360,000 records if one oBIX adapter
 * generates one record on each second over 10 years.
 */
#define HIST_FLT_VAL_MAX_BITS	9

/* The number of decimal fraction desirable */
#define FLOAT_FRACT_MAX_BITS    8

/*
 * The maximum string length of a converted 32bit float value,
 * which consists of 1 byte for sign, 38 bytes for integral part,
 * 1 byte for dot, and extra bytes for fractional part.
 */
#define FLOAT_MAX_BITS		(1 + 38 + 1 + FLOAT_FRACT_MAX_BITS)

#define UINT32_MAX_BITS		10

int str_to_long(const char *str, long *val);
int str_to_float(const char *str, float *val);

int timespec_compare(const struct timespec *m1, const struct timespec *m2);

typedef int (*load_file_cb_t)(const char *dir, const char *file, void *arg);

int for_each_file_name(const char *dir, const char *prefix,
						const char *suffix, load_file_cb_t cb, void *arg);

int slash_preceded(const char *s);
int slash_followed(const char *s);

typedef int (*token_cb_t)(const char *token, void *arg1, void *arg2);

int for_each_str_token(const char *delimiter, const char *str,
					   token_cb_t cb, void *arg1, void *arg2);

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
 * The string format of obix:reltime contract
 *
 * NOTE: only supports the format of "PnDTnHnMnS" and the maximal
 * unit is day to avoid the complexity with month and year
 */
typedef enum {
	RELTIME_SEC,
	RELTIME_MIN,
	RELTIME_HOUR,
	RELTIME_DAY
} RELTIME_FORMAT;

int obix_reltime_to_long(const char *str, long *duration);
char *obix_reltime_from_long(long millis, RELTIME_FORMAT format);

/*
 * NOTE: No assignment should ever be passed in the macro, or
 * unwanted effect ensue!
 */
#define min(a, b)	(((a) <= (b)) ? (a) : (b))
#define max(a, b)	(((a) >= (b)) ? (a) : (b))

#endif /* OBIX_UTILS_H_ */

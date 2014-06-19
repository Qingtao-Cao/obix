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

#include <assert.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <libxml/tree.h>
#include "obix_utils.h"
#include "log_utils.h"

#define SERVER_DB_PREFIX	"server_"
#define SERVER_DB_SUFFIX	".xml"

/** @name oBIX Object Types (XML Element Types)
 * @{
 */
const char *OBIX_OBJ = "obj";
const char *OBIX_OBJ_REF = "ref";
const char *OBIX_OBJ_OP = "op";
const char *OBIX_OBJ_LIST = "list";
const char *OBIX_OBJ_ERR = "err";
const char *OBIX_OBJ_BOOL = "bool";
const char *OBIX_OBJ_INT = "int";
const char *OBIX_OBJ_REAL = "real";
const char *OBIX_OBJ_STR = "str";
const char *OBIX_OBJ_ENUM = "enum";
const char *OBIX_OBJ_ABSTIME = "abstime";
const char *OBIX_OBJ_RELTIME = "reltime";
const char *OBIX_OBJ_URI = "uri";
const char *OBIX_OBJ_FEED = "feed";
const char *OBIX_OBJ_META = "meta";
const char *OBIX_OBJ_DATE = "date";
/** @} */

/** @name oBIX Object Names
 * Object names which are used in oBIX contracts.
 * @{
 */
const char *OBIX_NAME_SIGN_UP = "signUp";
const char *OBIX_NAME_BATCH = "batch";
const char *OBIX_NAME_WATCH_SERVICE = "watchService";
const char *OBIX_NAME_WATCH_SERVICE_MAKE = "make";
const char *OBIX_NAME_WATCH_ADD = "add";
const char *OBIX_NAME_WATCH_REMOVE = "remove";
const char *OBIX_NAME_WATCH_POLLCHANGES = "pollChanges";
const char *OBIX_NAME_WATCH_POLLREFRESH = "pollRefresh";
const char *OBIX_NAME_WATCH_DELETE = "delete";
const char *OBIX_NAME_WATCH_LEASE = "lease";
const char *OBIX_NAME_WATCH_POLL_WAIT_INTERVAL = "pollWaitInterval";
const char *OBIX_NAME_WATCH_POLL_WAIT_INTERVAL_MIN = "min";
const char *OBIX_NAME_WATCH_POLL_WAIT_INTERVAL_MAX = "max";
/** @} */

const char *OBIX_OBJ_NULL_TEMPLATE = "<obj null=\"true\"/>";

/** @name oBIX Object Attributes and Facets
 * @{
 */
const char *OBIX_ATTR_IS = "is";
const char *OBIX_ATTR_OF = "of";
const char *OBIX_ATTR_NAME = "name";
const char *OBIX_ATTR_HREF = "href";
const char *OBIX_ATTR_VAL = "val";
const char *OBIX_ATTR_NULL = "null";
const char *OBIX_ATTR_WRITABLE = "writable";
const char *OBIX_ATTR_DISPLAY = "display";
const char *OBIX_ATTR_DISPLAY_NAME = "displayName";
const char *OBIX_ATTR_HIDDEN = "hidden";
const char *OBIX_META_ATTR_OP = "op";
const char *OBIX_META_ATTR_WATCH_ID = "watch_id";
/** @} */

const char *XML_TRUE = "true";
const char *XML_FALSE = "false";

const char *OBIX_CONTRACT_BATCH_OUT = "obix:BatchOut";
const char *OBIX_CONTRACT_OP_READ = "obix:Read";
const char *OBIX_CONTRACT_OP_WRITE = "obix:Write";
const char *OBIX_CONTRACT_OP_INVOKE = "obix:Invoke";

const char *STR_DELIMITER_SLASH = "/";
const char *STR_DELIMITER_DOT = ".";

/*
 * Get the TID of the calling thread
 *
 * Note,
 * 1. Callers should use "%u" to print pid_t value properly
 */
pid_t get_tid(void)
{
	return syscall(SYS_gettid);
}

/*
 * Return 1 if the string is preceded with slash, 0 otherwise
 */
int slash_preceded(const char *s)
{
	assert(s);

	return (s[0] == '/') ? 1 : 0;
}

/*
 * Return 1 if the string is ended up with slash, 0 otherwise
 */
int slash_followed(const char *s)
{
	assert(s);

	return (s[strlen(s) - 1] == '/') ? 1 : 0;
}

/**
 * Compare whether the given two strings are identical
 *
 * Return 0 if two strings are same with each other ignoring
 * any potential trailing slash in any one of them
 */
int str_is_identical(const char *str1, const char *str2)
{
	int len1 = strlen(str1);
	int len2 = strlen(str2);

	if (slash_followed(str1) == 1) {
		len1--;
	}

	if (slash_followed(str2) == 1) {
		len2--;
	}

	/* No assignment passed in the macro, or unwanted effect ensue */
	return strncmp(str1, str2, max(len1, len2));
}

int str_token_count_helper(const char *tok, void *arg1, void *arg2)
{
	/* arg2 is ignored */

	/* log_debug("Found token: %s", tok); */

	*((int *)arg1) += 1;

	return 0;
}

/**
 * Apply callback function for each token in the string.
 * The first parameter to the callback is always the current
 * token, the rest of parameters are passed in after that.
 *
 * Note,
 * 1. The callback function must return < 0 on failure if
 * subsequent token are NOT to be processed further.
 */
int for_each_str_token(const char *delimiter, const char *str,
					   token_cb_t cb, void *arg1, void *arg2)
{
	char *copy;
	char *tok, *saveptr;
	int ret = -1;

	if (!(copy = strdup(str))) {
		log_error("Failed to duplicate string %s", str);
		return -1;
	}

	tok = strtok_r(copy, delimiter, &saveptr);
	if (tok != NULL) {
		do {
			if ((ret = cb(tok, arg1, arg2)) < 0) {
				break;
			}
		} while ((tok = strtok_r(NULL, delimiter, &saveptr)) != NULL);
	}

	free(copy);
	return ret;
}

/**
 * Apply the given callback function on each file under
 * the specified directory with matching prefix and suffix
 *
 * Note,
 * 1. Callers should not make any assumption about the sequence
 * of file name retrieved by readdir
 */
int for_each_file_name(const char *dir, const char *prefix,
					   const char *suffix, load_file_cb_t cb)
{
	struct stat	statbuf;
	struct dirent *dirp;
	DIR *dp;

	assert(dir && cb);

	if (lstat(dir, &statbuf) < 0) {
		log_error("Unable to stat %s", dir);
		return -1;
	}

	if (S_ISDIR(statbuf.st_mode) == 0) {
		log_error("%s not a directory", dir);
		return -1;
	}

	if ((dp = opendir(dir)) == NULL) {
		log_error("Failed to read directory %s", dir);
		return -1;
	}

	while ((dirp = readdir(dp)) != NULL) {
		/* Skip dot and dotdot */
		if (strcmp(dirp->d_name, ".") == 0 ||
			strcmp(dirp->d_name, "..") == 0) {
			continue;
		}

		/* Skip if not matching specified prefix and suffix */
		if (strstr(dirp->d_name, prefix) != dirp->d_name ||
			strstr(dirp->d_name, suffix) !=
				dirp->d_name + strlen(dirp->d_name) - strlen(suffix)) {
			continue;
		}

		if (cb(dir, dirp->d_name) < 0) {
			closedir(dp);
			return -1;
		}
	}

	closedir(dp);
	return 0;
}

/**
 * Convert a number character string without '+/-' prefix
 * into an integer
 */
long str_to_long(const char *str)
{
	char *endptr;
	long val;

	errno = 0;
	val = strtol(str, &endptr, 10);
	if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN)) ||
		(errno != 0 && val == 0)) {
		return -1;
	}

	/* empty string, no number characters */
	if (endptr == str) {
		return -1;
	}

	/* Simply ignore any trailing slashes or non-number characters */

	return val;
}

/**
 * Compare a pair of timespec, return -
 *  -1, if m1 is less than m2;
 *  0, if m1 equals to m2;
 *  1, if m1 is bigger than m2;
 */
int timespec_compare(const struct timespec *m1, const struct timespec *m2)
{
	if (m1->tv_sec < m2->tv_sec ||
		(m1->tv_sec == m2->tv_sec && m1->tv_nsec < m2->tv_nsec)) {
		return -1;
	}

	if (m1->tv_sec > m2->tv_sec ||
		(m1->tv_sec == m2->tv_sec && m1->tv_nsec > m2->tv_nsec)) {
		return 1;
	}

	return 0;
}

xmlNode *obix_obj_null(void)
{
    xmlNode *nullNode = xmlNewNode(NULL, BAD_CAST OBIX_OBJ);

    xmlSetProp(nullNode, BAD_CAST OBIX_ATTR_NULL, BAD_CAST XML_TRUE);

    return nullNode;
}

/*
 * Get and duplicate the date and time parts from a full timestamp
 * in format of yyyy-mm-ddThh:mm:ss[+-]hh:00, where date the time
 * are separated by "T" and time and timezone are separated by "+/-"
 *
 * Return 0 on success, < 0 otherwise
 *
 * Note,
 * 1. Callers are expected to release the date and time strings
 * after use
 */
int timestamp_split(const char *ts, char **date, char **time)
{
	char *end;
	int len;

	if (!(end = strstr(ts, "T"))) {
		return -1;
	}

	len = end - ts;

	*date = (char *)malloc(len + 1);
	if (!*date) {
		return -1;
	}

	strncpy(*date, ts, len);
	*(*date + len) = '\0';

	if (time) {
		ts = end + 1;	/* Skip the "T" character */

		/* In case timezone is omitted */
		if (!(end = strstr(ts, "-")) && !(end = strstr(ts, "+"))) {
			end = (char *)(ts + strlen(ts));
		}

		len = end - ts;

		*time = (char *)malloc(len + 1);
		if (!*time) {
			free(*date);
			return -1;
		}

		strncpy(*time, ts, len);
		*(*time + len) = '\0';
	}

	return 0;
}

/*
 * Concatenate potential root, parent folder, file name and suffix
 * strings into one. Extra slash will be inserted after folder name
 * if needed
 *
 * Return 0 on success, < 0 otherwise
 */
int link_pathname(char **p, const char *root, const char *parent,
				  const char *file, const char *sfx)
{
	int len;
	char *buf;
	char *fmt;

	assert(root);

	len = strlen(root);

	/* Extra "/" after root */
	if (slash_followed(root) == 0) {
		len++;
	}

	if (parent) {
		len += strlen(parent);

		/* Avoid double slashes between root and parent */
		if (slash_preceded(parent) == 1) {
			len--;
		}

		/* Extra "/" after parent */
		if (slash_followed(parent) == 0) {
			len++;
		}
	}

	if (file) {
		len += strlen(file);

		/* Avoid double slashes between parent or root with file */
		if (slash_preceded(file) == 1) {
			len--;
		}

	}

	if (sfx) {
		len += strlen(sfx);
	}

	buf = (char *)malloc(len + 1);	/* Extra byte for string terminator */
	if (!buf) {
		*p = NULL;
		return -1;
	}

	fmt = (slash_followed(root) == 0) ? "%s/" : "%s";
	len = sprintf(buf, fmt, root);

	if (parent) {
		/* Discard preceding "/" */
		if (slash_preceded(parent) == 1) {
			parent++;
		}

		fmt = (slash_followed(parent) == 0) ? "%s/" : "%s";

		len += sprintf(buf+len, fmt, parent);
	}

	if (file) {
		/* Discard preceding "/" */
		if (slash_preceded(file) == 1) {
			file++;
		}

		len += sprintf(buf+len, "%s", file);
	}

	if (sfx) {
		len += sprintf(buf+len, "%s", sfx);
	}

	buf[len] = '\0';

	*p = buf;
	return 0;
}

/*
 * Convert "yyyy-mm-dd" string into numbers of year, month and
 * day respectively, or convert "hh:mm:ss" string into numbers
 * of hour, minute and second respectively.
 *
 * Return 0 on success, -1 otherwise
 */
static int time_to_long(const char *str, long *x, long *y, long *z, int delimiter)
{
	const char *orig = str;
	char *endptr;

	/* X */
	errno = 0;
	*x = strtol(str, &endptr, 10);
	if ((errno == ERANGE && (*x == LONG_MAX || *x == LONG_MIN)) ||
		(errno != 0 && *x == 0)) {
		log_error("Failed to convert X string to value");
		return -1;
	}

	if (endptr == str) {
		log_error("Invalid format: no X contained in string %s", orig);
		return -1;
	}

	if (*endptr != delimiter) {
		log_error("Invalid format: Delimiter \"%d\" instead of other non-digit "
					"character should be used between X and Y", delimiter);
		return -1;
	}

	/* Y */
	str = endptr + 1;
	errno = 0;
	*y = strtol(str, &endptr, 10);
	if ((errno == ERANGE && (*y == LONG_MAX || *y == LONG_MIN)) ||
		(errno != 0 && *y == 0)) {
		log_error("Failed to convert Y string to value");
		return -1;
	}

	if (endptr == str) {
		log_error("Invalid format: no Y contained in string %s", orig);
		return -1;
	}

	if (*endptr != delimiter) {
		log_error("Invalid format: Delimiter \"%d\" instead of other non-digit "
					"character should be used between X and Y", delimiter);
		return -1;
	}

	/* Z */
	str = endptr + 1;
	errno = 0;
	*z = strtol(str, &endptr, 10);
	if ((errno == ERANGE && (*z == LONG_MAX || *z == LONG_MIN)) ||
		(errno != 0 && *z == 0)) {
		log_error("Failed to convert Z string to value");
		return -1;
	}

	if (endptr == str) {
		log_error("Invalid format: no Z contained in string %s", orig);
		return -1;
	}

	if (*endptr != '\0') {
		log_error("Further non-digit characters pending number: %s", endptr);
		return -1;
	}

	return 0;
}

/*
 * Compare two time relevant strings in either "yyyy-mm-dd" or
 * "hh:mm:ss" format
 *
 * Return 0 on success, < 0 otherwise.
 *
 * On success, res will be set as:
 *	-1, if str1 before str2;
 *	0, if str1 is the same as str2;
 *	1, if str1 after str2;
 */
int time_compare(const char *str1, const char *str2, int *res, int delimiter)
{
	long x1, y1, z1;
	long x2, y2, z2;

	if (time_to_long(str1, &x1, &y1, &z1, delimiter) < 0 ||
		time_to_long(str2, &x2, &y2, &z2, delimiter) < 0) {
		log_error("Failed to convert time string to numbers");
		return -1;
	}

	if (x1 < x2) {
		*res = -1;
	} else if (x1 > x2) {
		*res = 1;
	} else {
		if (y1 < y2) {
			*res = -1;
		} else if (y1 > y2) {
			*res = 1;
		} else {
			if (z1 < z2) {
				*res = -1;
			} else if (z1 > z2) {
				*res = 1;
			} else {
				*res = 0;
			}
		}
	}

	return 0;
}

/*
 * Compare two timestamps
 *
 * Return 0 on success, < 0 otherwise
 *
 * On success, res_d will be set as:
 *	-1, if date in ts1 is before that of ts2;
 *	0, if date in ts1 is the same as that of ts2;
 *	1, if date in ts1 is after that of ts2;
 *
 * res_t will be set in a similar manner
 */
int timestamp_compare(const char *ts1, const char *ts2, int *res_d, int *res_t)
{
	char *date1 = NULL, *time1 = NULL;
	char *date2 = NULL, *time2 = NULL;
	int ret = -1;

	assert(ts1 && ts2);

	if (timestamp_split(ts1, &date1, &time1) < 0 ||
		timestamp_split(ts2, &date2, &time2) < 0) {
		log_error("Failed to get dates and times from %s and %s", ts1, ts2);
		goto failed;
	}

	if (time_compare(date1, date2, res_d, '-') < 0 ||
		time_compare(time1, time2, res_t, ':') < 0) {
		log_error("Failed to compare dates or times in %s and %s", ts1, ts2);
	} else {
		ret = 0;	/* Success */
	}

	/* Fall through */

failed:
	if (date1) {
		free(date1);
	}

	if (date2) {
		free(date2);
	}

	if (time1) {
		free(time1);
	}

	if (time2) {
		free(time2);
	}

	return ret;
}

/*
 * Check if [start, end] has something in common with
 * [oldest, latest]
 *
 * Return 1 when common part exists, < 0 otherwise,
 * in particular:
 *
 *		-2, unable to compare timestamp strings;
 *		-3, the start TS is after the latest TS;
 *		-4, the end TS is before the oldest TS;
 */
int timestamp_has_common(const char *start, const char *end,
						 const char *oldest, const char *latest)
{
	int res_d, res_t;

	if (timestamp_compare(start, latest, &res_d, &res_t) < 0) {
		return -2;
	}

	if (res_d > 0 || (res_d == 0 && res_t > 0)) {
		return -3;
	}

	if (timestamp_compare(end, oldest, &res_d, &res_t) < 0) {
		return -2;
	}

	if (res_d < 0 || (res_d == 0 && res_t < 0)) {
		return -4;
	}

	return 1;
}

/*
 * Adjust *start and *end to be the common part of
 * [*start, *end] and [oldest, latest]
 *
 * Return 0 on success, -1 otherwise
 *
 * Note,
 * 1. Due to the fact that xml_get_child_val > xmlGetProp
 * will return a copy of the value of relevant attribute,
 * before changing the start and end pointer, the original
 * string pointed to must be released to prevent memory
 * leaks.
 */
int timestamp_find_common(char **start, char **end,
						  const char *oldest, const char *latest)
{
	int res_d, res_t;

	if (timestamp_compare(*start, oldest, &res_d, &res_t) < 0) {
		return -1;
	}

	if (res_d < 0 || (res_d == 0 && res_t < 0)) {
		free(*start);
		*start = strdup(oldest);
	}

	if (timestamp_compare(*end, latest, &res_d, &res_t) < 0) {
		return -1;
	}

	if (res_d > 0 || (res_d == 0 && res_t > 0)) {
		free(*end);
		*end = strdup(latest);
	}

	return 0;
}


int obix_reltime_parseToLong(const char *str, long *duration) {
    int negativeFlag = 0;
    int parsedSomething = 0;
    long result = 0;
    const char *startPos = str;
    char *endPos;

    // parsing xs:duration string. Format: {-}PnYnMnDTnHnMnS
    // where n - amount of years, months, days, hours, minutes and seconds
    // respectively. Seconds can have 3 fraction digits after '.' defining
    // milliseconds.

    /**
     * Parses positive integer. Does the same as strtol(startPos, endPos, 10),
     * but assumes that negative result is an error. Thus returns -1 instead of
     * 0, when nothing is parsed. Also sets flag parsedSomething if parsing
     * did not fail.
     */
    long strtoposl(const char * startPos, char **endPos) {
        long l = strtol(startPos, endPos, 10);
        if (startPos == *endPos) {
            return -1;
        }
        parsedSomething = 1;

        return l;
    }

    if (*startPos == '-') {
        negativeFlag = 1;
        startPos++;
    }

    if (*startPos != 'P') {
        return -1;
    }
    startPos++;

    long l = strtoposl(startPos, &endPos);

    // if we parsed years or months...
    if ((*endPos == 'Y') || (*endPos == 'M')) {
        // we can't convert years and months to milliseconds it will overflow
        // long type. Maximum that long can hold is 24 days.
        // ( 24 days * 24 hours * 60 min * 60 sec * 1000 ms < 2 ^ 31 )
        return -2;
    } else
        if (*endPos == 'D') {
        // if we parsed days...
        if (l < 0) {
            // nothing is parsed, but a number must be specified before 'D'
            // or negative number is parsed
            return -1;
        }
        // again check for buffer overflow (check the comments above)
        if (l > 23) {
            return -2;
        }

        result += l * 86400000;

        startPos = endPos + 1;
    } else {
        // no "Y" no "M" no "D"
        if (parsedSomething != 0) {
            // there was some value, but no character, defining what it was.
            return -1;
        }
    }

    if (*startPos == 'T') {
        // we reached time section
        startPos++;
        // reset parsing flag, because there must be something after 'T'
        parsedSomething = 0;

        l = strtoposl(startPos, &endPos);

        // if we parsed hours...
        if (*endPos == 'H') {
            if (l < 0) {
                // 'H' occurred, but there was no number before it
                return -1;
            }
            // if days already parsed, than restrict hours to be < 24,
            // otherwise, hours must be < 595 (or it will overflow long)
            if ((l > 595) || ((result > 0) && (l > 23))) {
                return -2;
            }

            result += l * 3600000;

            // parse further
            startPos = endPos + 1;
            l = strtoposl(startPos, &endPos);
        }

        // if we parsed minutes...
        if (*endPos == 'M') {
            if (l < 0) {
                // 'M' occurred, but there was no number before it
                return -1;
            }
            // if something is already parsed, than restrict minutes to be < 60
            // otherwise, minutes must be < 35790 (or it will overflow long)
            if ((l > 35790) || ((result > 0) && (l > 59))) {
                return -2;
            }

            result += l * 60000;

            // parse further
            startPos = endPos + 1;
            l = strtoposl(startPos, &endPos);
        }

        // if we parsed seconds...
        if ((*endPos == 'S') || (*endPos == '.')) {
            if (l < 0) {
                // 'S' occurred, but there was no number before it
                return -1;
            }
            // if something is already parsed, than restrict seconds to be < 60
            // otherwise, seconds must be < 2147482 (or it will overflow long)
            if ((l > 2147482) || ((result > 0) && (l > 59))) {
                return -2;
            }

            result += l * 1000;

            if (*endPos == '.') {
                // parse also milliseconds
                startPos = endPos + 1;
                // reset parsed flag because there always should be
                // something after '.'
                parsedSomething = 0;
                l = strtoposl(startPos, &endPos);
                if (*endPos != 'S') {
                    // only seconds can have fraction point
                    return -1;
                }

                // we parsed fraction which is displaying milliseconds -
                // drop everything that is smaller than 0.001
                while (l > 1000) {
                    l /= 10;
                }
                // if we parsed '.5', than it should be 500
                int parsedDigits = endPos - startPos;
                for (; parsedDigits < 3; parsedDigits++) {
                    l *= 10;
                }

                result += l;
            }

            // l == -1 shows that no more values are parsed
            l = -1;
        }

        if (l != -1) {
            // something was parsed, but it was not hours, minutes or seconds
            return -1;
        }
    }

    if (parsedSomething == 0) {
        // we did not parse any value
        return -1;
    }

    // save result at the output variable
    *duration = (negativeFlag == 0) ? result : -result;

    return 0;
}

char *obix_reltime_fromLong(long millis, RELTIME_FORMAT format) {
    // helper function which calculates required length of a string for
    // storing positive integer value + one symbol

    int plonglen(long l) {
        // quite nice stuff, don't you think so? :)
        // but I think it is better than divide/multiply number on each step.
        const long size[] = {
            0, 9, 99, 999, 9999, 99999, 999999,
            9999999, 99999999, 999999999, LONG_MAX
        };

        int length;

        for (length = 0; l > size[length]; length++)
            ;

        // reserve one more byte for terminating symbol
        if (length > 0) {
            length++;
        }

        return length;
    }

    int days = 0;
    int hours = 0;
    long minutes = 0;
    long seconds = 0;
    int negativeFlag = 0;

    int stringSize = 3;

    if (millis == 0) {
        // quick shortcut for zero case
        char *reltime = (char *) malloc(5);
        if (reltime == NULL) {
            return NULL;
        }

        strcpy(reltime, "PT0S");
        return reltime;
    }

    if (millis < 0) {
        millis = -millis;
        negativeFlag = 1;
        stringSize++;
    }

    seconds = millis / 1000;
    millis %= 1000;

    if (format >= RELTIME_MIN) {
        minutes = seconds / 60;
        seconds -= minutes * 60;

        if (format >= RELTIME_HOUR) {
            hours = minutes / 60;
            minutes %= 60;

            if (format >= RELTIME_DAY) {
                days = hours / 24;
                hours %= 24;
            }
        }
    }

    stringSize += plonglen(days);
    stringSize += plonglen(hours);
    stringSize += plonglen(minutes);
    stringSize += plonglen(seconds);
    if (millis > 0) {
        stringSize += 4;
    }

    char *reltime = (char *) malloc(stringSize);
    if (reltime == NULL) {
        return NULL;
    }

    // generating string of a kind 'PnDTnHnMnS'
    int pos = 0;
    if (negativeFlag == 1) {
        reltime[pos++] = '-';
    }

    // obligatory symbol
    reltime[pos++] = 'P';

    if (days > 0) {
        pos += sprintf(reltime + pos, "%dD", days);
    }

    if ((millis > 0) || (seconds > 0) || (minutes > 0) || (hours > 0)) {
        reltime[pos++] = 'T';
    }

    if (hours > 0) {
        pos += sprintf(reltime + pos, "%dH", hours);
    }

    if (minutes > 0) {
        pos += sprintf(reltime + pos, "%ldM", minutes);
    }

    if ((seconds > 0) || (millis > 0)) {
        pos += sprintf(reltime + pos, "%ld", seconds);

        if (millis > 0) {
            pos += sprintf(reltime + pos, ".%03ld", millis);
            // remove all trailing zeros'
            while (reltime[pos - 1] == '0') {
                pos--;
            }
        }

        reltime[pos++] = 'S';
        reltime[pos] = '\0';
    }

    return reltime;
}


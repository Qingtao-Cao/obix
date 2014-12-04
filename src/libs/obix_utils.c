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

#include <unistd.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdlib.h>
#include <stdio.h>			/* sprintf */
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <math.h>			/* HUGE_VAL */
#include <float.h>			/* FLT_EPSILON */
#include <ctype.h>			/* isdigit */
#define __USE_BSD		1	/* tm_gmtoff */
#define __USE_MISC		1	/* timegm */
#define __USE_XOPEN		1	/* strptime */
#include <time.h>			/* time & gmtime_r */
#include "obix_utils.h"
#include "log_utils.h"

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

const char *XML_TRUE = "true";
const char *XML_FALSE = "false";
const int XML_BOOL_MAX_LEN = 5;

const char *STR_DELIMITER_SLASH = "/";
const char *STR_DELIMITER_DOT = ".";

const char *HIST_REC_TS = "timestamp";
const char *HIST_AIN_DATA = "data";
const char *HIST_AIN_TS_UND = "UNSPECIFIED";
const char *HIST_OP_APPEND = "append";
const char *HIST_OP_QUERY = "query";
const char *HIST_INDEX = "index";
const char *HIST_TS_INIT = "1970-01-01T0:0:0Z";
const char *HIST_DATE_INIT = "1970-01-01";

/*
 * oBIX contracts used by the server side
 */
const char *OBIX_CONTRACT_OP_READ = "obix:Read";
const char *OBIX_CONTRACT_OP_WRITE = "obix:Write";
const char *OBIX_CONTRACT_OP_INVOKE = "obix:Invoke";
const char *OBIX_CONTRACT_HIST_FILE_ABS = "obix:HistoryFileAbstract";

/*
 * oBIX contracts used by the client side
 */
const char *OBIX_CONTRACT_HIST_AIN = "obix:HistoryAppendIn";
const char *OBIX_CONTRACT_HIST_FLT = "obix:HistoryFilter";
const char *OBIX_CONTRACT_BATCH_IN = "obix:BatchIn";
const char *OBIX_CONTRACT_WATCH_IN = "obix:WatchIn";

const char *OBIX_RELTIME_ZERO = "PT0S";
const int OBIX_RELTIME_ZERO_LEN = 4;

const char *OBIX_DEVICE_ROOT = "/obix/deviceRoot/";
const int OBIX_DEVICE_ROOT_LEN = 17;

const char *OBIX_BATCH = "/obix/batch";
const int OBIX_BATCH_LEN = 11;

const char *OBIX_DEVICES = "/obix/devices/";

const char *OBIX_HISTORY_LOBBY = "/obix/historyService/histories/";

const char *OBIX_HISTORY_SERVICE = "/obix/historyService";
const int OBIX_HISTORY_SERVICE_LEN = 20;

const char *OBIX_WATCH_SERVICE = "/obix/watchService";
const int OBIX_WATCH_SERVICE_LEN = 18;

const char *OBIX_WATCH_POLLCHANGES = "pollChanges";

/*
 * Timestamps are in "yyyy-mm-ddThh:mm:ssZ" format which has
 * 20 bytes without the NULL terminator.
 *
 * NOTE: the timestamp strings are in UTC timezone by default
 */
static const char *HIST_REC_TS_FORMAT = "%4d-%.2d-%.2dT%.2d:%.2d:%.2dZ";
static const char *HIST_REC_DATE_FORMAT = "%4d-%.2d-%.2d";

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
	if (!s) {
		return 0;
	}

	return (s[0] == '/') ? 1 : 0;
}

/*
 * Return 1 if the string is ended up with slash, 0 otherwise
 */
int slash_followed(const char *s)
{
	int len;

	if (!s) {
		return 0;
	}

	if ((len = strlen(s)) == 0) {	/* empty string */
		return 0;
	}

	return (s[len - 1] == '/') ? 1 : 0;
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
int for_each_file_name(const char *dir,
					   const char *prefix, const char *suffix,
					   load_file_cb_t cb, void *arg)
{
	struct stat	statbuf;
	struct dirent *dirp;
	DIR *dp;

	if (!dir || !cb) {		/* prefix, suffix are optional */
		log_error("Illegal parameters provided");
		return -1;
	}

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
		if ((prefix && strstr(dirp->d_name, prefix) != dirp->d_name) ||
			(suffix && strstr(dirp->d_name, suffix) !=
				dirp->d_name + strlen(dirp->d_name) - strlen(suffix))) {
			continue;
		}

		if (cb(dir, dirp->d_name, arg) < 0) {
			closedir(dp);
			return -1;
		}
	}

	closedir(dp);
	return 0;
}

/*
 * Convert a number character string without '+/-' prefix
 * into an integer
 */
int str_to_long(const char *str, long *val)
{
	char *endptr;

	*val = 0;

	errno = 0;
	*val = strtol(str, &endptr, 10);
	if ((errno == ERANGE && (*val == LONG_MAX || *val == LONG_MIN)) ||
		(errno != 0 && *val == 0)) {
		return -1;
	}

	/* empty string, no number characters */
	if (endptr == str) {
		return -2;
	}

	/* Simply ignore any trailing slashes or non-number characters */

	return 0;
}

/*
 * Convert a string into a float
 *
 * Return 0 on success, -1 otherwise
 *
 * TODO:
 * Should float be compared against HUGE_VALX directly?
 */
int str_to_float(const char *str, float *val)
{
	char *endptr;

	errno = 0;
	*val = strtof(str, &endptr);
	if ((errno = ERANGE && (*val == HUGE_VALF || *val == HUGE_VALL)) ||
		(errno != 0 && fabs(*val) < FLT_EPSILON)) {
		return -1;
	}

	/* empty string */
	if (endptr == str) {
		return -2;
	}

	/* Simply ignore any trailing slashes or non-number characters */

	return 0;
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

	*p = NULL;

	if (!root || ((len = strlen(root)) == 0)) {
		return -1;
	}

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

		/*
		 * If the parent parameter equals to "/", then it should be
		 * skipped over directly instead of appending an extra,
		 * unnecessary slash
		 */
		if (*parent != '\0') {
			fmt = (slash_followed(parent) == 0) ? "%s/" : "%s";
			len += sprintf(buf+len, fmt, parent);
		}
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

int obix_reltime_to_long(const char *str, long *duration)
{
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

/*
 * Get the number of digits of a unsigned long value
 */
static int long_len(const unsigned long l)
{
	int len;

	long size[] = {
		0, 9, 99, 999, 9999, 99999, 999999,
		9999999, 99999999, 999999999, LONG_MAX
	};

	for (len = 0; l > size[len]; len++) ;

	if (len) {	/* One more byte for string terminator */
		len++;
	}

	return len;
}

/*
 * Generate a string representation in format of "PnDTnHnMnS"
 * from the given long value.
 *
 * The 'P' and 'T' are obligatory symbols
 *
 * NOTE: the second value may contain decimal part
 */
char *obix_reltime_from_long(long millis, RELTIME_FORMAT format)
{
	int seconds, minutes, hours, days;
	int negativeFlag = 0, pos;
	char *reltime;
	int stringSize = 1;		/* 'P' */

	if (millis == 0) {
		if (!(reltime = (char *)malloc(OBIX_RELTIME_ZERO_LEN + 1))) {
			return NULL;
		}

		return strcpy(reltime, OBIX_RELTIME_ZERO);
	}

	if (millis < 0) {
		millis *= -1;
		negativeFlag = 1;
		stringSize++;		/* '-' */
	}

	minutes = hours = days = 0;

	seconds = millis / 1000;
	millis %= 1000;

	if (format >= RELTIME_MIN) {
		minutes = seconds / 60;
		seconds %= 60;

		if (format >= RELTIME_HOUR) {
			hours = minutes / 60;
			minutes %= 60;

			if (format >= RELTIME_DAY) {
				days = hours / 24;
				hours %= 24;
			}
	    }
	}

	if (millis > 0) {
		stringSize += long_len(millis);
		stringSize++;	/* '.' */
	}

	if (seconds > 0) {
		stringSize += long_len(seconds);
		stringSize++;	/* 'S' */
	}

	if (minutes > 0) {
		stringSize += long_len(minutes);
		stringSize++;	/* 'M' */
	}

	if (hours > 0) {
		stringSize += long_len(hours);
		stringSize++;	/* 'H' */
	}

	if (days > 0) {
		stringSize += long_len(days);
		stringSize++;	/* 'D' */
	}

	if (millis > 0 || seconds > 0 || minutes > 0 || hours > 0) {
		stringSize++;	/* 'T' */
	}

	if (!(reltime = (char *)malloc(stringSize + 1))) {
		return NULL;
	}

	pos = 0;
	if (negativeFlag == 1) {
		reltime[pos++] = '-';
	}

	reltime[pos++] = 'P';

	if (days > 0) {
		pos += sprintf(reltime + pos, "%dD", days);
	}

	if (millis > 0 || seconds > 0 || minutes > 0 || hours > 0) {
		reltime[pos++] = 'T';
	}

	if (hours > 0) {
		pos += sprintf(reltime + pos, "%dH", hours);
	}

	if (minutes > 0) {
		pos += sprintf(reltime + pos, "%dM", minutes);
	}

	if (seconds > 0 || millis > 0) {
		pos += sprintf(reltime + pos, "%d", seconds);

		if (millis > 0) {
			pos += sprintf(reltime + pos, ".%3ld", millis);
		}

		reltime[pos++] = 'S';
	}

	reltime[pos] = '\0';
	return reltime;
}

char *get_utc_timestamp(time_t t)
{
	char *ts;
	struct tm tm;

	if (t < 0) {
		return NULL;
	}

	/* Short-cut if the given calender time is zero */
	if (t == 0) {
		return strdup(HIST_TS_INIT);
	}

	if (!(ts = (char *)malloc(HIST_REC_TS_MAX_LEN + 1))) {
		log_error("Failed to allocate memory for timestamp string");
		return NULL;
	}

	/*
	 * Get a broken-down time in terms of UTC time zone, that is, GMT+0
	 */
	if (gmtime_r(&t, &tm)) {
		sprintf(ts, HIST_REC_TS_FORMAT,
				tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday,
				tm.tm_hour, tm.tm_min, tm.tm_sec);
	} else {
		free(ts);
		ts = NULL;
	}

	return ts;
}

char *get_utc_date(time_t t)
{
	char *date;
	struct tm tm;

	if (t < 0) {
		return NULL;
	}

	/* Short-cut if the given calender time is zero */
	if (t == 0) {
		return strdup(HIST_DATE_INIT);
	}

	if (!(date = (char *)malloc(HIST_REC_DATE_MAX_LEN + 1))) {
		log_error("Failed to allocate memory for timestamp string");
		return NULL;
	}

	/*
	 * Get a broken-down time in terms of UTC time zone, that is, GMT+0
	 */
	if (gmtime_r(&t, &tm)) {
		sprintf(date, HIST_REC_DATE_FORMAT,
				tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday);
	} else {
		free(date);
		date = NULL;
	}

	return date;
}

/*
 * Split a timestamp string in ISO-8601 format such as %FT%T%z
 * into sub strings of date, time and timezone
 */
int timestamp_split(const char *ts, char **date, char **time, char **tz)
{
	char *end;
	int len, no_tz = 0;

	/* Get date */
	if (!(end = strstr(ts, "T"))) {
		return -1;
	}

	len = end - ts;

	if (!(*date = (char *)malloc(len + 1))) {
		return -1;
	}

	strncpy(*date, ts, len);
	*(*date + len) = '\0';

	/* Get time */
	ts = end + 1;	/* Skip the "T" character */
	if (*ts == '\0') {
		free(*date);
		return -1;
	}

	/*
	 * If timezone is not available, then it will be treated
	 * in UTC by default
	 */
	if (!(end = strstr(ts, "Z")) &&
		!(end = strstr(ts, "-")) &&
		!(end = strstr(ts, "+"))) {
		end = (char *)(ts + strlen(ts));
		no_tz = 1;
	}

	len = end - ts;

	if (!(*time = (char *)malloc(len + 1))) {
		free(*date);
		return -1;
	}

	strncpy(*time, ts, len);
	*(*time + len) = '\0';

	/* Get timezone */
	if (no_tz == 1) {
		*tz = NULL;
		return 0;
	}

	if (*end == 'Z') {
		*tz = strdup("Z");
		return 0;
	}

	ts = end + 1;	/* Skip the "+/-" character */
	if (*ts == '\0') {
		free(*date);
		free(*time);
		return -1;
	}

	end = (char *)(ts + strlen(ts));

	len = end - ts;

	if (!(*tz = (char *)malloc(len + 1))) {
		free(*date);
		free(*time);
		return -1;
	}

	strncpy(*tz, ts, len);
	*(*tz + len) = '\0';

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

	if (!str) {
		return -1;
	}

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

static int timezone_is_valid(const char *tz)
{
	char val[3];
	int len, i, hour;

	/*
	 * The timezone designator could be a single 'Z' letter which
	 * stands for UTC timezone. If no timezone designator is available
	 * it will be treated as in UTC timezone
	 */
	if (!tz || strcmp(tz, "Z") == 0) {
		return 1;
	}

	len = strlen(tz);

	/*
	 * Please refer to docs/timezone.md file for the discussion of
	 * what kind of timezone formats are supported
	 */
	for (i = 0; i < len; i++) {
		if (isdigit(*(tz + i)) == 0){
			return 0;
		}
	}

	/* In "hh" format */
	if (len == 2) {
		return (atoi(tz) <= 12) ? 1 : 0;
	}

	/* In "hhmm" format */
	if (len == 4) {
		strncpy(val, tz, 2);
		val[2] = '\0';

		hour = atoi(val);

		strncpy(val, tz+2, 2);
		val[2] = '\0';

		if (hour < 12) {
			/* 15 minutes difference from an integral timezone is permitted */
			return (atoi(val) % 15 == 0) ? 1 : 0;
		} else if (hour == 12) {
			/* the "mm" part must be zero */
			return (atoi(val) == 0) ? 1: 0;
		}
	}

	/* All the rest format other than "hh" or "hhmm" are regarded as invalid */
	return 0;
}

/*
 * Sanity check if the timestamp string is in "%FT%T%z" format
 *
 * NOTE: This function can't tell if a specific component in
 * the timestamp string contains meaningful values, for example,
 * whether the seconds value is larger than 61, or the minute
 * value is larger than 60 etc. Moreover, C library strptime()
 * and timegm() APIs may tolerate invalid values in the "%F" part
 * more than in the "%T" part. See timezone.md for further details
 */
int timestamp_is_valid(const char *ts)
{
	char *date, *time, *tz;
	long x, y, z;
	int ret = 0;

	if (!ts) {
		return 0;
	}

	date = time = tz = NULL;

	/* Wrong format */
	if (timestamp_split(ts, &date, &time, &tz) < 0) {
		return 0;
	}

	/* Sanity checks on date and time */
	if (time_to_long(date, &x, &y, &z, '-') < 0 ||
		time_to_long(time, &x, &y, &z, ':') < 0) {
		goto failed;
	}

	/* Sanity checks on timezone */
	ret = timezone_is_valid(tz);

	/* Fall through */

failed:
	if (date) {
		free(date);
	}

	if (time) {
		free(time);
	}

	if (tz) {
		free(tz);
	}

	return ret;
}

/*
 * Convert a timestamp string in ISO-8601 format as in
 * "%FT%T%z" to calender time in UTC timezone
 */
time_t timestamp_to_utc_time(const char *ts)
{
	struct tm tm;
	time_t time;

	if (!ts) {
		return 0;
	}

	if (timestamp_is_valid(ts) == 0) {
		return -1;
	}

	/* Convert timestampe string into broken-down struct tm */
	memset(&tm, 0, sizeof(struct tm));
	strptime(ts, "%FT%T%z", &tm);	/* ignore return value */

	/*
	 * Apply timezone offset from UTC
	 *
	 * NOTE: the timezone offset should be DEDUCTED from the
	 * second value to rebase to UTC timezone
	 */
	tm.tm_sec -= tm.tm_gmtoff;
	tm.tm_gmtoff = 0;

	/*
	 * Convert struct tm to calender time
	 *
	 * NOTE: unlike mktime(), timegm() will not take into account
	 * local timezone, which is desirable since the timezone offset
	 * from GMT in the original timestamp string has been considered.
	 */
	if ((time = timegm(&tm)) == -1) {
		log_error("Failed to convert broken-down struct tm to time_t");
	}

	return time;
}

/*
 * Compare two timestamps that may in different timezone
 *
 * Return 0 on success, < 0 otherwise and *res should not be
 * used.
 *
 * On success, res will be set as:
 *	-1, if ts1 is earlier than ts2;
 *	0, if ts1 is the same as ts2;
 *	1, if ts1 is later than ts2;
 *
 * Furthermore, if res == 1, the new_day will be set to 1 if ts1
 * is on a new day than ts2, 0 otherwise.
 */
int timestamp_compare(const char *ts1, const char *ts2, int *res, int *new_day)
{
	time_t time1, time2;
	char *day1 = NULL, *day2 = NULL;
	int ret = -1;

	*res = 0;
	if (new_day) {
		*new_day = 0;
	}

	if (!ts1 || !ts2) {
		return -1;
	}

	if ((time1 = timestamp_to_utc_time(ts1)) < 0 ||
		(time2 = timestamp_to_utc_time(ts2)) < 0) {
		return -1;
	}

	if (time1 < time2) {
		*res = -1;
	} else if (time1 == time2) {
		*res = 0;
	} else {
		*res = 1;

		/*
		 * Only compare the date if required and only when ts1 is later
		 * than ts2. As long as the date strings are different, we can
		 * safely assume ts1 has a new day later than ts2
		 */
		if (new_day) {
			/* Short-cut if ts2 is the Epoch time */
			if (time2 == 0) {
				*new_day = 1;
			} else {
				if (!(day1 = get_utc_date(time1)) ||
					!(day2 = get_utc_date(time2))) {
					goto failed;
				}

				*new_day = (strcmp(day1, day2) != 0) ? 1 : 0;
			}
		}
	}

	ret = 0;

	/* Fall through */

failed:
	if (day1) {
		free(day1);
	}

	if (day2) {
		free(day2);
	}

	return ret;
}

/*
 * Compare two date strings
 *
 * Return 0 on success, < 0 otherwise and *res should not be
 * used.
 *
 * On success, res will be set as:
 *	-1, if date1 is earlier than date2;
 *	0, if date1 is the same as date2;
 *	1, if date1 is later than date2;
 */
int timestamp_compare_date(const char *date1, const char *date2, int *res)
{
	long y1, m1, d1;
	long y2, m2, d2;

	*res = 0;

	if (!date1 || !date2) {
		return -1;
	}

	/*
	 * Break down date string into year, month and day values
	 * and also do sanity checks
	 */
	if (time_to_long(date1, &y1, &m1, &d1, '-') < 0 ||
		time_to_long(date2, &y2, &m2, &d2, '-') < 0) {
		return -1;
	}

	if (y1 < y2 ||
		(y1 == y2 && m1 < m2) ||
		(y1 == y2 && m1 == m2 && d1 < d2)) {
		*res = -1;
	} else if (y1 == y2 && m1 == m2 && d1 == d2) {
		*res = 0;
	} else {
		*res = 1;
	}

	return 0;
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
	int res;

	if (timestamp_compare(start, latest, &res, NULL) < 0) {
		return -2;
	}

	if (res == 1) {
		return -3;
	}

	if (timestamp_compare(end, oldest, &res, NULL) < 0) {
		return -2;
	}

	if (res == -1) {
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
 * NOTE: due to the fact that xml_get_child_val > xmlGetProp
 * will return a copy of the value of relevant attribute,
 * before changing the start and end pointer, the original
 * string pointed to must be released to prevent memory
 * leaks.
 *
 * NOTE: callers should have invoked timestamp_has_common()
 * to ensure there is common part of two timestamp ranges
 */
int timestamp_find_common(char **start, char **end,
						  const char *oldest, const char *latest)
{
	int res;

	if (timestamp_compare(*start, oldest, &res, NULL) < 0) {
		return -1;
	}

	if (res == -1) {
		free(*start);
		*start = strdup(oldest);
	}

	if (timestamp_compare(*end, latest, &res, NULL) < 0) {
		return -1;
	}

	if (res == 1) {
		free(*end);
		*end = strdup(latest);
	}

	return 0;
}

char *timestamp_get_utc_date(const char *ts)
{
	time_t time;

	if ((time = timestamp_to_utc_time(ts)) < 0) {
		return NULL;
	}

	return get_utc_date(time);
}

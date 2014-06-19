/*
 * Copyright (c) 2014 Qingtao Cao [harry.cao@nextdc.com]
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
 * *******************************************************************
 */

#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>

static int get_date_time(const char *ts, char **date, char **time);

int main(int argc, char *argv[])
{
	char *date, *time;
	char *ts = argv[1];

    if (argc != 2) {
        printf("Usage: %s <yyyy-mm-ddThh:mm:ss-hh:mm>\n", argv[0]);
        return -1;
    }

	if (get_date_time(ts, &date, NULL) == 0) {
		printf("date %s, time %s\n", date);
	}

	return 0;
}

static int get_date_time(const char *ts, char **date, char **time)
{
	char *end;
	int len;

	if (!(end = strstr(ts, "T")))
		return -1;

	len = end - ts;

	*date = (char *)malloc(len + 1);
	if (!*date)
		return -1;

	strncpy(*date, ts, len);
	*(*date + len) = '\0';

	if (time) {
		ts = end + 1;	/* Skip the "T" char */

		/* TODO: assume current timezone has negative offset */
		if (!(end = strstr(ts, "-")) &&
			!(end = strstr(ts, "+"))) {
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


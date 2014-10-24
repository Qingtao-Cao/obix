/******************************************************************************
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

/*
 * An instrument to test functions to convert timestamp strings
 *
 * Build with below command:
 *
 *	$ gcc -g -Wall -Werror -I ../libs -lobix-common
 *		  ts2utc.c -o ts2utc
 *
 * Run with following argument:
 *
 *	$ ./ts2utc [timestamp string in ISO_8601 format]
 *
 * NOTE: please refer to docs/timezone.md file for the discussion
 * about what kind of timezone format has been supported by the
 * strptime() API in glibc
 */

#define _XOPEN_SOURCE			/* strptime */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define __USE_BSD			/* tm_gmtoff */
#define __USE_MISC			/* timegm */
#include <time.h>
#include "obix_utils.h"

int main(int argc, char *argv[])
{
	struct tm tm;
	char *remnant, *ts;
	time_t time;

	if (argc != 2) {
		printf("Usage: ./%s <timestamp string in ISO_8601 format>\n", argv[0]);
		return -1;
	}

	if (timestamp_is_valid(argv[1]) == 0) {
		printf("Provided timestamp is invalid, all or part of it will "
			   "be ignored: %s\n", argv[1]);
	}

	/* Convert timestampe string into broken-down struct tm */
	memset(&tm, 0, sizeof(struct tm));
	errno = 0;
	remnant = strptime(argv[1], "%FT%T%z", &tm);
	printf("Input timestamp: %s, remnant: %s, errno: %s\n",
		   argv[1], remnant, strerror(errno));

	/*
	 * Apply timezone offset from GMT
	 *
	 * NOTE: the timezone offset should be DEDUCTED from the
	 * second value to rebase to GMT timezone
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
		printf("Failed to convert broken-down struct tm to time_t\n");
		return -1;
	}

	printf("Calender time in UTC (GMT+0) timezone: %ld\n",  time);

	/* Convert calender time to timestamp string */
	if (!(ts = get_utc_timestamp(time))) {
		printf("Failed to convert calender time to timestamp string\n");
		return -1;
	}

	printf("New timestamp: %s\n\n", ts);
	free(ts);
	return 0;
}

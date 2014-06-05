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


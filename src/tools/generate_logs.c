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
 * 
 * A small program to generate a number of log files and their
 * index file for test purpose.
 *
 * Run follow command to compile it:
 *
 *		gcc generate_logs.c -o generate_logs
 *
 * Then run with following arguments:
 *
 *		./generate_logs <dev_id> <year> <number of month>
 *
 * 31 log files for each month since Jan would be generated, the
 * year and the number of months are specified via arguments, as
 * well as a unique device ID string.
 *
 * Each log file contains 86400 records for one single day, that is,
 * one record for each second. Then move all log files(*.fragment)
 * and their index(index.xml) to oBIX server's history facility:
 *
 *		<oBIX resource folder>/histories/<dev_id>/
 *
 * then change owner and group to "lighttpd" for all files under
 * <dev_id>/(including the folder itself) and lastly re-start oBIX
 * server.
 */

#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>

#define INDEX_FILENAME	"index.xml"

static const char *INDEX_HEADER =
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n"
"<list name=\"index\" href=\"/obix/historyService/histories/%s/index\" of=\"obix:HistoryFileAbstract\">\r\n";

static const char *INDEX_FOOTER =
"</list>\r\n";

static const char *ABSTRACT =
"<obj is=\"obix:HistoryFileAbstract\">\r\n"
"<date name=\"date\" val=\"%s\" />\r\n"
"<int name=\"count\" val=\"%d\" />\r\n"
"<abstime name=\"start\" val=\"%s\" />\r\n"
"<abstime name=\"end\" val=\"%s\" />\r\n"
"</obj>\r\n";

static const char *RECORD =
"<obj is=\"obix:HistoryRecord\">\r\n"
"<abstime name=\"timestamp\" val=\"%sT%d:%d:%d\"></abstime>\r\n"
"<real name=\"value\" val=\"%d\"></real>\r\n"
"</obj>\r\n";

static int append_index(const char *date, int fd)
{
	int pos;
	static char index[512];
	static char start[64];
	static char end[64];

	sprintf(start, "%sT00:00:00", date);
	sprintf(end, "%sT23:59:59", date);

	pos = sprintf(index, ABSTRACT, date, 86400, start, end);

	if (write(fd, index, pos) < pos)
		return -1;
	else
		return 0;
}

static int create_fragment(const char *date)
{
	int h, m, s, fd, c = 0, len;
	char buf[256];
	char filename[64];

	sprintf(filename, "%s%s", date, ".fragment");

	fd = open(filename, O_CREAT | O_WRONLY | O_APPEND, 0644);
	if (fd < 0)
		return -1;

	for (h = 0; h < 24; h++) {
		for (m = 0; m < 60; m++) {
			for (s = 0; s < 60; s++) {
				len = sprintf(buf, RECORD, date, h, m, s, c++);
				buf[len] = '\0';
				if (write(fd, buf, len) < len)
					return -1;
			}
		}
	}

	close(fd);

	return 0;
}

int main(int argc, char *argv[])
{
	char date[32];
	char *buf;
	int i, j, mon;
	int len, ret;
	int index_fd;

	if (argc != 4) {
		printf("Usage: ./generate_logs <dev_id> <year> <number of month>\n");
		return -1;
	}

	mon = atoi(argv[3]);

	index_fd = open(INDEX_FILENAME, O_CREAT | O_TRUNC | O_WRONLY, 0644);
	if (index_fd < 0)
		return -1;

	buf = (char *)malloc(strlen(INDEX_HEADER) + strlen(argv[1]) + 1 - 2);
	if (!buf)
		return -1;

	len = sprintf(buf, INDEX_HEADER, argv[1]);

	ret = write(index_fd, buf, len);
	free(buf);

	if (ret < len)
		return -1;

	for (i = 1; i <= mon; i++) {
		for (j = 1; j <= 31; j++) {
			sprintf(date, "%s-%.2d-%.2d", argv[2], i, j);
			if (create_fragment(date) < 0 ||
				append_index(date, index_fd) < 0)
			return -1;
		}
	}

	len = strlen(INDEX_FOOTER);
	if (write(index_fd, INDEX_FOOTER, len) < len)
		return -1;

	close(index_fd);

	return 0;
}

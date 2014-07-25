/* *****************************************************************************
 * Copyright (c) 2014 Tyler Watson <tyler.watson@nextdc.com>
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
 * along with oBIX.  If not, see <http://www.gnu.org/licenses/>.
 *
 * *****************************************************************************/

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#define SERVER_CONFIG		"server_config.xml"
#define SERVER_DB_PREFIX	"server_"
#define SERVER_DB_SUFFIX	".xml"

int
main(int argc, char **argv)
{
	struct stat	statbuf;
	struct dirent *dirp;
	DIR *dp;
	int len;

	if (argc != 2) {
		printf("Usage: %s <path to xml files>\n", argv[0]);
		return -1;
	}

	if (lstat(argv[1], &statbuf) < 0) {
		printf("Unable to stat %s\n", argv[1]);
		return -1;
	}

	if (S_ISDIR(statbuf.st_mode) == 0) {
		printf("%s not a directory\n", argv[1]);
		return -1;
	}

	if ((dp = opendir(argv[1])) == NULL) {
		printf("Unable to read directory\n");
		return -1;
	}

	while ((dirp = readdir(dp)) != NULL) {
		if (strcmp(dirp->d_name, ".") == 0 ||
			strcmp(dirp->d_name, "..") == 0 ||
			strcmp(dirp->d_name, SERVER_CONFIG) == 0 ||
			strstr(dirp->d_name, SERVER_DB_PREFIX) != dirp->d_name)
				continue;

		if (strstr(dirp->d_name, SERVER_DB_SUFFIX) !=
				dirp->d_name + strlen(dirp->d_name) - strlen(SERVER_DB_SUFFIX))
			continue;

		printf("%s\n", dirp->d_name);
	}

	if (closedir(dp) < 0) {
		printf("Unable to close directory\n");
		return -1;
	}

	return 0;
}

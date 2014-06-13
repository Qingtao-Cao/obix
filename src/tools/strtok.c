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

#include <string.h>
#include <stdio.h>

int main(int argc, char **argv)
{
	char *test = "/obix/test/testdevice/bool/////";
	char *src = NULL;
	char *token = NULL;
	char *reentrant_ptr;

	src = strdup(test);

	token = strtok_r(src, "/", &reentrant_ptr);
	if ( token == NULL ) {
		printf("%s: no token in provided string.\n");
		return -1;
	}

	do {
		printf("Token: %s\n", token);
	} while ( (token = strtok_r(NULL, "/", &reentrant_ptr)) != NULL);

	free(src);

	return 0;
}

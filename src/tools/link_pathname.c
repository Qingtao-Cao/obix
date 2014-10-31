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

/*
 * A unit test case for the link_pathname() API
 *
 * Build with the following command:
 *	gcc -g -Wall -Werror link_pathname.c -I ../libs -lobix-common -o link_pathname
 *
 * Use valgrind to check memory access of this API:
 *	valgrind --leak-check=full ./link_pathname
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include "obix_utils.h"

int main(int argc, char **argv)
{
	char *args[][4] = {
		[0] = { NULL, "/", "/", "/" },
		[1] = { "/", NULL, "/", "/" },
		[2] = { "/", "/", NULL, "/" },
		[3] = { "/", "/", "/", NULL },
		[4] = { "/", NULL, NULL, "/" },
		[5] = { "/", "/", NULL, NULL },
		[6] = { "/", NULL, "/", NULL },
		[7] = { "/", NULL, NULL, NULL },
		[8] = { "/", "/", "/", "/" },
		[9] = { "", "/", "/", "/" },
		[10] = { "/", "", "/", "/" },
		[11] = { "/", "/", "", "/" },
		[12] = { "/", "/", "/", "" },
		[13] = { "/", "", "", "/" },
		[14] = { "/", "/", "", "" },
		[15] = { "/", "", "/", "" },
		[16] = { "/", "", "", "" },
	};
	char *res;
	int i;

	for (i = 0; i < 17; i++) {
		if (link_pathname(&res, args[i][0], args[i][1], args[i][2], args[i][3]) < 0) {
#if VERBOSE
			printf("#%i, failed\n", i);
#endif
		} else {
#if VERBOSE
			printf("#%i, res = %s\n", i, res);
#endif
			free(res);
		}
	}

	return 0;
}

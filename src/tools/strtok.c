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

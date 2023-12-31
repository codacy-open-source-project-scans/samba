#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <libsmbclient.h>
#include "get_auth_data_fn.h"


int main(int argc, char * argv[])
{
	int             fd;
	int             ret;
	int             debug = 0;
	int             savedErrno;
	char            buffer[2048];
	char            path[2048];
	char *          p;

	smbc_init(get_auth_data_fn, debug);

	for (;;)
	{
		fprintf(stdout, "Path: ");
		*path = '\0';
		p = fgets(path, sizeof(path) - 1, stdin);
		if (p == NULL) {
			fprintf(stderr, "failed to read from stdin\n");
			return 1;
		}
		if (strlen(path) == 0)
		{
			return 0;
		}

		p = path + strlen(path) - 1;
		if (*p == '\n')
		{
			*p = '\0';
		}

		if ((fd = smbc_open(path, O_RDONLY, 0)) < 0)
		{
			perror("smbc_open");
			continue;
		}

		do
		{
			ret = smbc_read(fd, buffer, sizeof(buffer));
			savedErrno = errno;
			if (ret > 0) fwrite(buffer, 1, ret, stdout);
		} while (ret > 0);

		smbc_close(fd);

		if (ret < 0)
		{
			errno = savedErrno;
			perror("read");
		}
	}

	return 0;
}

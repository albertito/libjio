
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

#include <libjio.h>


#define STR "TESTTESTTEST1234\n"

static int jio(void)
{
	int rv;
	jfs_t *fs;

	fs = jopen("test2", O_RDWR | O_CREAT | O_TRUNC, 0660, 0);
	if (fs == NULL)
		perror("jopen()");

	rv = jwrite(fs, STR, strlen(STR));
	if (rv != strlen(STR))
		perror("jwrite()");

	if (jclose(fs))
		perror("jclose()");

	return 0;
}

static int classic(void)
{
	int fd, rv;

	fd = open("test2", O_RDWR | O_CREAT | O_TRUNC, 0660);
	if (fd < 0)
		perror("open()");

	rv = write(fd, STR, strlen(STR));
	if (rv != strlen(STR))
		perror("write()");

	if (close(fd))
		perror("close()");

	return 0;
}


int main(int argc, char **argv)
{
	int i, n;

	if (argc != 3) {
		printf("Use: jio2 [c|j] N\n");
		return 1;
	}

	n = atoi(argv[2]);

	if (*argv[1] == 'c')
		for (i = 0; i < n; i++)
			classic();
	else if (*argv[1] == 'j')
		for (i = 0; i < n; i++)
			jio();
	else
		printf("Use: jio2 [c|j] n\n");

	return 0;
}


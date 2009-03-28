
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <libjio.h>

#define STR "TESTTESTTEST1234\n"

static int jio(void)
{
	int fd, rv;
	struct jfs fs;

	fd = jopen(&fs, "test1", O_RDWR | O_CREAT | O_TRUNC, 0660, 0);
	if (fd < 0)
		perror("jopen()");

	rv = jwrite(&fs, STR, strlen(STR));
	if (rv != strlen(STR))
		perror("jwrite()");

	return 0;
}

static int classic(void)
{
	int fd, rv;

	fd = open("test1", O_RDWR | O_CREAT | O_TRUNC, 0660);
	if (fd < 0)
		perror("open()");

	rv = write(fd, STR, strlen(STR));
	if (rv != strlen(STR))
		perror("write()");

	return 0;
}


int main(int argc, char **argv)
{
	if (argc != 2) {
		printf("Use: jio1 [c|j]\n");
		return 1;
	}

	if (*argv[1] == 'c')
		classic();
	else if (*argv[1] == 'j')
		jio();
	else
		printf("Use: jio1 [c|j]\n");

	return 0;
}




#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <libjio.h>

#define str "TESTTESTTEST1234\n"

int jio(void)
{
	int fd, rv;
	struct jfs fs;

	fd = jopen(&fs, "test1", O_RDWR | O_CREAT | O_TRUNC | O_SYNC, 0660, 0);
	if (fd < 0)
		perror("OPEN");

	rv = jwrite(&fs, str, strlen(str));
	if (rv != strlen(str))
		perror("WRITE");

	return 0;

}

int classic(void)
{
	int fd, rv;

	fd = open("test1", O_RDWR | O_CREAT | O_TRUNC | O_SYNC, 0660);
	if (fd < 0)
		perror("OPEN");

	rv = write(fd, str, strlen(str));
	if (rv != strlen(str))
		perror("WRITE");

	return 0;

}


int main(int argc, char **argv) {
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

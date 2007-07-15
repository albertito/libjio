

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

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
	int i;
	int N;
	
	if (argc != 2) {
		printf("Use: jio1 [c|j] N\n");
		return 1;
	}
	
	N = 0;
	N = atoi(argv[2]);

	if (*argv[1] == 'c')
		for (i = 0; i < N; i++)
			classic();
	else if (*argv[1] == 'j')
		for (i = 0; i < N; i++)
			jio();
	else
		printf("Use: jio1 [c|j] N\n");

	return 0;
}


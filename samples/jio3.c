
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <libjio.h>


int main(int argc, char **argv)
{
	int fd, rv;
	struct jfs fs;
	struct jtrans ts;

	fd = jopen(&fs, "test3", O_RDWR | O_CREAT, 0660, 0);
	if (fd < 0)
		perror("jopen()");

	jtrans_init(&fs, &ts);

#define str1 "1ROLLBACKTEST1!\n"
	jtrans_add(&ts, str1, strlen(str1), 0);

#define str2 "2ROLLBACKTEST2!\n"
	jtrans_add(&ts, str2, strlen(str2), strlen(str1));

#define str3 "3ROLLBACKTEST3!\n"
	jtrans_add(&ts, str3, strlen(str3), strlen(str1) + strlen(str2));

	rv = jtrans_commit(&ts);
	if (rv != strlen(str1) + strlen(str2) + strlen(str3))
		perror("jtrans_commit()");
	printf("commit ok: %d\n", rv);

	rv = jtrans_rollback(&ts);
	if (rv < 0)
		perror("jtrans_rollback()");
	printf("rollback ok: %d\n", rv);

	return 0;
}



#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <libjio.h>


int main(int argc, char **argv)
{
	int rv;
	jfs_t *fs;
	jtrans_t *ts;

	fs = jopen("test3", O_RDWR | O_CREAT, 0660, 0);
	if (fs == NULL)
		perror("jopen()");

	ts = jtrans_new(fs, 0);
	if (ts == NULL)
		perror("jtrans_new()");

#define str1 "1ROLLBACKTEST1!\n"
	jtrans_add_w(ts, str1, strlen(str1), 0);

#define str2 "2ROLLBACKTEST2!\n"
	jtrans_add_w(ts, str2, strlen(str2), strlen(str1));

#define str3 "3ROLLBACKTEST3!\n"
	jtrans_add_w(ts, str3, strlen(str3), strlen(str1) + strlen(str2));

	rv = jtrans_commit(ts);
	if (rv < 0)
		perror("jtrans_commit()");
	printf("commit ok: %d\n", rv);

	rv = jtrans_rollback(ts);
	if (rv < 0)
		perror("jtrans_rollback()");
	printf("rollback ok: %d\n", rv);

	jtrans_free(ts);

	if (jclose(fs))
		perror("jclose()");

	return 0;
}


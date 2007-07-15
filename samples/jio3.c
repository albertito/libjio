

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

	fd = jopen(&fs, "test1", O_RDWR | O_CREAT | O_TRUNC | O_SYNC, 0660, 0);
	if (fd < 0)
		perror("OPEN");

#define str "ROLLBACKTEST!\n"

	jtrans_init(&fs, &ts);

	ts.offset = 0;
	ts.buf = str;
	ts.len = strlen(str);
		
	rv = jtrans_commit(&ts);
	if (rv != strlen(str))
		perror("COMMIT");

	rv = jtrans_rollback(&ts);
	if (rv != 0)
		perror("ROLLBACK");

	return 0;

}



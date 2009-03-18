
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <libjio.h>

#define FILENAME "test1"
#define TEXT "Hello world!\n"

int main(void)
{
	int r;
	struct jfs file;
	struct jtrans trans;
	struct jfsck_result result;

	/* check the file is OK */
	jfsck(FILENAME, NULL, &result);
	jfsck_cleanup(FILENAME, NULL);

	/* and open it */
	r = jopen(&file, FILENAME, O_RDWR | O_CREAT | O_TRUNC, 0600, 0);
	if (r < 0) {
		perror("jopen");
		return 1;
	}

	/* write two "Hello world"s next to each other */
	jtrans_init(&file, &trans);
	jtrans_add(&trans, TEXT, strlen(TEXT), 0);
	jtrans_add(&trans, TEXT, strlen(TEXT), strlen(TEXT));
	r = jtrans_commit(&trans);
	if (r < 0) {
		perror("jtrans_commit");
		return 1;
	}

	/* at this point the file has "Hello world!\nHello world!\n" */

	/* now we rollback */
	r = jtrans_rollback(&trans);
	if (r < 0) {
		perror("jtrans_rollback");
		return 1;
	}

	/* and now the file is empty! */

	jtrans_free(&trans);
	jclose(&file);
	return 0;
}


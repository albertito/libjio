
/*
 * jiofsck - A journal checker and recovery tool for libjio
 * Alberto Bertogli (albertito@blitiri.com.ar)
 */

#include <stdio.h>
#include <string.h>
#include "libjio.h"


static void usage(void)
{
	printf("\
Use: jiofsck [clean=1] [dir=DIR] FILE\n\
\n\
Where \"FILE\" is the name of the file you want to check the journal from,\n\
and the optional parameter \"clean\" makes jiofsck to clean up the journal\n\
after recovery.\n\
The parameter \"dir=DIR\", also optional, is used to indicate the position\n\
of the journal directory.\n\
\n\
Examples:\n\
# jiofsck file\n\
# jiofsck clean=1 file\n\
# jiofsck dir=/tmp/journal file\n\
# jiofsck clean=1 dir=/tmp/journal file\n\
\n");
}

int main(int argc, char **argv)
{
	int i, do_cleanup;
	unsigned int flags;
	char *file, *jdir;
	struct jfsck_result res;
	enum jfsck_return rv;



	file = jdir = NULL;
	do_cleanup = 0;

	if (argc < 2) {
		usage();
		return 1;
	}

	for (i = 1; i < argc; i++) {
		if (strcmp("clean=1", argv[i]) == 0) {
			do_cleanup = 1;
		} else if (strncmp("dir=", argv[i], 4) == 0) {
			jdir = argv[i] + 4;
		} else {
			file = argv[i];
		}
	}

	memset(&res, 0, sizeof(res));

	flags = 0;
	if (do_cleanup)
		flags |= J_CLEANUP;

	printf("Checking journal: ");
	fflush(stdout);
	rv = jfsck(file, jdir, &res, flags);

	switch (rv) {
	case J_ESUCCESS:
		printf("done\n");
		break;
	case J_ENOENT:
		printf("No such file or directory\n");
		return 1;
	case J_ENOJOURNAL:
		printf("No journal associated to the file, "
				"or journal empty\n");
		return 1;
	case J_ENOMEM:
		printf("Not enough memory\n");
		return 1;
	case J_ECLEANUP:
		printf("Error cleaning up the journal directory\n");
		return 1;
	case J_EIO:
		printf("I/O error\n");
		perror("  additional information");
		return 1;
	default:
		printf("Unknown result, please report as a bug\n");
		return 1;
	}

	printf("Journal checking results\n");
	printf("------------------------\n\n");

	printf("Total:\t\t %d\n", res.total);
	printf("Invalid:\t %d\n", res.invalid);
	printf("In progress:\t %d\n", res.in_progress);
	printf("Broken:\t\t %d\n", res.broken);
	printf("Corrupt:\t %d\n", res.corrupt);
	printf("Reapplied:\t %d\n", res.reapplied);
	printf("\n");

	if (do_cleanup) {
		printf("The journal has been cleaned up.\n");
	}

	return 0;
}


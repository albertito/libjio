
/* 
 * jiofsck - A journal checker and recovery tool for libjio
 * Alberto Bertogli (albertogli@telpin.com.ar)
 */

#include <stdio.h>
#include <string.h>
#include "libjio.h"


void usage()
{
	printf("Use: jiofsck [clean] FILE\n\n");
	printf("Where \"FILE\" is the name of the file "
			"which you want to check the journal from,\n"
			"and the optional parameter \"clean\" makes "
			"jiofsck to clean up the journal after\n"
			"recovery.\n");
}

int main(int argc, char **argv)
{
	int rv, do_cleanup;
	char *file;
	struct jfsck_result res;
	
	if (argc != 2 && argc != 3) {
		usage();
		return 1;
	}

	if (argc == 3) {
		if (strcmp("clean", argv[1]) != 0 ) {
			usage();
			return 1;
		}
		file = argv[2];
		do_cleanup = 1;
	} else {
		file = argv[1];
		do_cleanup = 0;
	}

	memset(&res, 0, sizeof(res));
	
	printf("Checking journal: ");
	rv = jfsck(file, &res);

	if (rv == J_ENOENT) {
		printf("No such file or directory\n");
		return 1;
	} else if (rv == J_ENOJOURNAL) {
		printf("No journal associated to the file, "
				"or journal empty\n");
		return 1;
	}

	printf("done\n");

	if (do_cleanup) {
		printf("Cleaning journal: ");
		if (!jfsck_cleanup(file)) {
			printf("Error cleaning journal\n");
			return 1;
		}

		printf("done\n");
	}

	printf("Journal checking results\n");
	printf("------------------------\n\n");

	printf("Total:\t\t %d\n", res.total);
	printf("Invalid:\t %d\n", res.invalid);
	printf("In progress:\t %d\n", res.in_progress);
	printf("Broken head:\t %d\n", res.broken_head);
	printf("Broken body:\t %d\n", res.broken_body);
	printf("Load error:\t %d\n", res.load_error);
	printf("Apply error:\t %d\n", res.apply_error);
	printf("Reapplied:\t %d\n", res.reapplied);
	printf("\n");
	
	if (!do_cleanup) {
		printf("You can now safely remove the journal directory "
				"completely\nto start a new journal.\n");
	} else {
		printf("The journal has been checked and cleaned up.\n");
	}

	return 0;
}


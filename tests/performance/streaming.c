
/*
 * streaming.c - A program to test speed of a streaming write using libjio.
 * Alberto Bertogli (albertogli@telpin.com.ar)
 */


#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <libjio.h>

#define FILENAME "test_file-streaming"


void help(void)
{
	printf("Use: streaming MBs_to_write blocksize\n");
	exit(1);
}


int main(int argc, char **argv)
{
	int towrite, blocksize, rv, mb;
	long secs, usecs;
	double seconds, mb_per_sec;
	void *buf;
	struct jfs fs;
	struct timeval tv1, tv2;

	if (argc != 3)
		help();

	mb = atoi(argv[1]);
	towrite = mb * 1024 * 1024;
	blocksize = atoi(argv[2]);

	rv = jopen(&fs, FILENAME, O_RDWR | O_CREAT | O_SYNC | O_TRUNC, 
			0600, 0);
	if (rv < 0) {
		perror("jopen():");
		exit(1);
	}

	buf = malloc(blocksize);

	gettimeofday(&tv1, NULL);

	while (towrite > 0) {
		rv = jwrite(&fs, buf, blocksize);
		if (rv != blocksize) {
			perror("jwrite:");
			break;
		}

		towrite -= blocksize;
	}

	gettimeofday(&tv2, NULL);

	secs = tv2.tv_sec - tv1.tv_sec;
	usecs = tv2.tv_usec - tv1.tv_usec;

	if (usecs < 0) {
		secs -= 1;
		usecs = 1000000 + usecs;
	}
	
	seconds = secs + (usecs / 1000000.0);
	mb_per_sec = mb / seconds;
	
	printf("%d %d %f %f\n", mb, blocksize, seconds, mb_per_sec);

	jclose(&fs);
	return 0;
}


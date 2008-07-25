
/*
 * streaming.c - A program to test speed of paralell writes using libjio.
 * Alberto Bertogli (albertito@blitiri.com.ar)
 */

/*
 * It creates a big file, extend it using truncate and fork N threads, which
 * write the file in chunks (ie. if we have three threads, the first one
 * writes the first 1/3rd of the file, and so on).
 */

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <libjio.h>

#define FILENAME "test_file-paralell"

/* Declare here what's shared among threads
 * It's not the cleanest design ever, but let's face it, it's a simple
 * benchmarking program, who cares? */
struct jfs fs;
int blocksize, towrite, mb;


void help(void)
{
	printf("Use: paralell MBs_to_write_per_thread blocksize nthreads\n");
	exit(1);
}

void *worker(void *tno)
{
	void *buf;
	int tid, work_done, rv;
	off_t localoffset;
	long secs, usecs;
	double seconds, mb_per_sec;
	struct timeval tv1, tv2;

	tid = (int) tno;

	localoffset = tid * towrite;
	
	buf = malloc(blocksize);
	work_done = 0;

	gettimeofday(&tv1, NULL);

	while (work_done < towrite) {
		rv = jpwrite(&fs, buf, blocksize, localoffset + work_done );
		if (rv != blocksize) {
			perror("jpwrite:");
			break;
		}

		work_done += blocksize;
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
	
	printf("%d %d %d %f %f\n", tid, mb, blocksize, seconds, mb_per_sec);

	return NULL;

}

int main(int argc, char **argv)
{
	int rv, nthreads, i;
	pthread_t *threads;

	if (argc != 4)
		help();

	mb = atoi(argv[1]);
	blocksize = atoi(argv[2]);
	nthreads = atoi(argv[3]);
	towrite = mb * 1024 * 1024;

	threads = malloc(sizeof(pthread_t) * nthreads);
	

	rv = jopen(&fs, FILENAME, O_RDWR | O_CREAT | O_SYNC | O_TRUNC, 
			0600, 0);
	if (rv < 0) {
		perror("jopen():");
		exit(1);
	}

	/* extend the file */
	jtruncate(&fs, towrite * nthreads);
	
	/* start the threads */
	for (i = 0; i < nthreads; i++) {
		pthread_create(threads + i, NULL, &worker, (void *) i);
	}

	for (i = 0; i < nthreads; i++) {
		pthread_join(*(threads + i), NULL);
	}

	jclose(&fs);
	return 0;
}


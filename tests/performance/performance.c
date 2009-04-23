
/*
 * performance.c - A program to test speed of parallel writes using libjio.
 * Alberto Bertogli (albertito@blitiri.com.ar)
 *
 * It creates a big file, extends it using truncate, and forks N threads which
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
#include <string.h>
#include <libjio.h>

#define FILENAME "test_file"

/* These are shared among threads, to make the code simpler */
static jfs_t *fs;
static unsigned long mb;
static ssize_t blocksize, towrite;


static void help(void)
{
	printf("Use: performance towrite blocksize nthreads\n");
	printf("\n");
	printf(" - towrite: how many MB to write per thread\n");
	printf(" - blocksize: size of blocks written, in KB\n");
	printf(" - nthreads: number of threads to use\n");
}

static void *worker(void *tno)
{
	void *buf;
	unsigned long tid;
	ssize_t work_done, rv;
	off_t localoffset;
	long secs, usecs;
	double seconds, mb_per_sec;
	struct timeval tv1, tv2;

	tid = (unsigned long) tno;

	localoffset = tid * towrite;

	buf = malloc(blocksize);
	if (buf == NULL) {
		perror("malloc()");
		return NULL;
	}
	memset(buf, 5, blocksize);

	work_done = 0;

	gettimeofday(&tv1, NULL);

	while (work_done < towrite) {
		rv = jpwrite(fs, buf, blocksize, localoffset + work_done);
		if (rv != blocksize) {
			perror("jpwrite()");
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

	printf("%lu %zd %zd %f %f\n", tid, mb, blocksize, seconds, mb_per_sec);

	free(buf);

	return NULL;
}

int main(int argc, char **argv)
{
	int nthreads;
	unsigned long i;
	pthread_t *threads;
	struct jfsck_result ckres;

	if (argc != 4) {
		help();
		return 1;
	}

	mb = atoi(argv[1]);
	blocksize = atoi(argv[2]) * 1024;
	nthreads = atoi(argv[3]);
	towrite = mb * 1024 * 1024;

	threads = malloc(sizeof(pthread_t) * nthreads);
	if (threads == NULL) {
		perror("malloc()");
		return 1;
	}

	fs = jopen(FILENAME, O_RDWR | O_CREAT | O_TRUNC, 0600, 0);
	if (fs == NULL) {
		perror("jopen()");
		return 1;
	}

	jtruncate(fs, towrite * nthreads);

	for (i = 0; i < nthreads; i++) {
		pthread_create(threads + i, NULL, &worker, (void *) i);
	}

	for (i = 0; i < nthreads; i++) {
		pthread_join(*(threads + i), NULL);
	}

	jclose(fs);
	jfsck(FILENAME, NULL, &ckres, 0);
	if (ckres.total != 0) {
		fprintf(stderr, "There were %d errors during the test\n",
				ckres.total);
		fprintf(stderr, "jfsck() was used to fix them, but that ");
		fprintf(stderr, "shouldn't happen.\n");
		return 1;
	}

	return 0;
}


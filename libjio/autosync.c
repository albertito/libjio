
#include <pthread.h>	/* pthread_* */
#include <errno.h>	/* ETIMEDOUT */
#include <signal.h>	/* sig_atomic_t */
#include <stdlib.h>	/* malloc() and friends */
#include <time.h>	/* clock_gettime() */

#include "common.h"
#include "libjio.h"

struct autosync_cfg {
	struct jfs *fs;
	pthread_t tid;

	time_t max_sec;
	size_t max_bytes;

	/* When the thread must die, we set this to 1 */
	sig_atomic_t must_die;

	/* Condition variable to wake up the thread */
	pthread_cond_t cond;
	pthread_mutex_t mutex;
};

/* Thread that performes the automatic syncing */
static void *autosync_thread(void *arg)
{
	int rv;
	void *had_errors;
	struct timespec ts;
	struct autosync_cfg *cfg;

	cfg = (struct autosync_cfg *) arg;

	/* had_errors is a void * just to avoid weird casts, since we want to
	 * return it, but it's used as a boolean */
	had_errors = (void *) 0;

	pthread_mutex_lock(&cfg->mutex);
	for (;;) {
		clock_gettime(CLOCK_REALTIME, &ts);
		ts.tv_sec += cfg->max_sec;

		rv = pthread_cond_timedwait(&cfg->cond, &cfg->mutex, &ts);
		if (rv != 0 && rv != ETIMEDOUT)
			break;

		if (cfg->must_die)
			break;

		/* cover from spurious wakeups */
		if (rv != ETIMEDOUT && cfg->fs->ltrans_len < cfg->max_bytes)
			continue;

		rv = jsync(cfg->fs);
		if (rv != 0)
			had_errors = (void *) 1;

	}
	pthread_mutex_unlock(&cfg->mutex);

	pthread_exit(had_errors);
	return NULL;
}

/* Starts the autosync thread, which will perform a jsync() every max_sec
 * seconds, or every max_bytes written using lingering transactions. */
int jfs_autosync_start(struct jfs *fs, time_t max_sec, size_t max_bytes)
{
	struct autosync_cfg *cfg;

	if (fs->as_cfg != NULL)
		return -1;

	cfg = malloc(sizeof(struct autosync_cfg));
	if (cfg == NULL)
		return -1;

	cfg->fs = fs;
	cfg->max_sec = max_sec;
	cfg->max_bytes = max_bytes;
	cfg->must_die = 0;
	pthread_cond_init(&cfg->cond, NULL);
	pthread_mutex_init(&cfg->mutex, NULL);

	fs->as_cfg = cfg;

	return pthread_create(&cfg->tid, NULL, &autosync_thread, cfg);
}

/* Stops the autosync thread started by jfs_autosync_start(). It's
 * automatically called in jclose(). */
int jfs_autosync_stop(struct jfs *fs)
{
	int rv = 0;
	void *had_errors;

	if (fs->as_cfg == NULL)
		return 0;

	fs->as_cfg->must_die = 1;
	pthread_cond_signal(&fs->as_cfg->cond);
	pthread_join(fs->as_cfg->tid, &had_errors);

	if (had_errors)
		rv = -1;

	pthread_cond_destroy(&fs->as_cfg->cond);
	pthread_mutex_destroy(&fs->as_cfg->mutex);
	free(fs->as_cfg);
	fs->as_cfg = NULL;

	return rv;
}

/* Used internally to notify the autosync thread that the number of bytes has
 * been exceeded. Must be called with fs' ltlock held. */
void autosync_check(struct jfs *fs)
{
	if (fs->ltrans_len > fs->as_cfg->max_bytes)
		pthread_cond_signal(&fs->as_cfg->cond);
}


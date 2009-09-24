
/*
 * Recovery functions
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <errno.h>
#include <sys/mman.h>

#include "libjio.h"
#include "common.h"
#include "journal.h"
#include "trans.h"


/** Remove the journal directory (if it's clean).
 *
 * @param name path to the file
 * @param jdir path to the journal directory
 * @returns 0 on success, < 0 on error
 */
static int jfsck_cleanup(const char *name, const char *jdir)
{
	char tfile[PATH_MAX*3];
	DIR *dir;
	struct dirent *dent;

	dir = opendir(jdir);
	if (dir == NULL && errno == ENOENT)
		/* it doesn't exist, so it's clean */
		return 0;
	else if (dir == NULL)
		return -1;

	for (errno = 0, dent = readdir(dir); dent != NULL;
			errno = 0, dent = readdir(dir)) {
		/* We only care about files we know, and ignore everything
		 * else. Note that transactions should have been removed by
		 * jfsck(), we will not do it to prevent accidental misuse */
		if (strcmp(dent->d_name, "lock"))
			continue;

		/* build the full path to the transaction file */
		memset(tfile, 0, PATH_MAX * 3);
		strcat(tfile, jdir);
		strcat(tfile, "/");
		strcat(tfile, dent->d_name);

		if (strlen(tfile) > PATH_MAX) {
			closedir(dir);
			return -1;
		}

		if (unlink(tfile) != 0) {
			closedir(dir);
			return -1;
		}
	}

	if (errno) {
		closedir(dir);
		return -1;
	}

	if (closedir(dir) != 0)
		return -1;

	if (rmdir(jdir) != 0)
		return -1;

	return 0;
}

/* Check the journal and fix the incomplete transactions */
enum jfsck_return jfsck(const char *name, const char *jdir,
		struct jfsck_result *res, unsigned int flags)
{
	int tfd, rv, i, ret;
	unsigned int maxtid;
	char jlockfile[PATH_MAX], tname[PATH_MAX], brokenname[PATH_MAX];
	struct stat sinfo;
	struct jfs fs;
	struct jtrans *curts;
	struct operation *tmpop;
	DIR *dir;
	struct dirent *dent;
	unsigned char *map;
	off_t filelen, lr;

	tfd = -1;
	filelen = 0;
	dir = NULL;
	fs.fd = -1;
	fs.jfd = -1;
	fs.jdir = NULL;
	fs.jdirfd = -1;
	fs.jmap = MAP_FAILED;
	map = NULL;
	ret = 0;

	res->total = 0;
	res->invalid = 0;
	res->in_progress = 0;
	res->broken = 0;
	res->corrupt = 0;
	res->reapplied = 0;

	fs.fd = open(name, O_RDWR | O_SYNC);
	if (fs.fd < 0) {
		ret = J_EIO;
		if (errno == ENOENT)
			ret = J_ENOENT;
		goto exit;
	}

	fs.name = (char *) name;

	/* Locking the whole file protect us from concurrent runs, but it's
	 * not to be trusted nor assumed (lingering transactions break it): it
	 * just helps prevent some accidents. */
	lr = plockf(fs.fd, F_LOCKW, 0, 0);
	if (lr == -1) {
		/* In the future, we may want to differentiate this case from
		 * a normal I/O error. */
		ret = J_EIO;
		goto exit;
	}

	if (jdir == NULL) {
		fs.jdir = (char *) malloc(PATH_MAX);
		if (fs.jdir == NULL) {
			ret = J_ENOMEM;
			goto exit;
		}

		if (!get_jdir(name, fs.jdir)) {
			ret = J_ENOMEM;
			goto exit;
		}
	} else {
		fs.jdir = strdup(jdir);
		if (fs.jdir == NULL) {
			ret = J_ENOMEM;
			goto exit;
		}
	}

	rv = lstat(fs.jdir, &sinfo);
	if (rv < 0) {
		ret = J_EIO;
		if (errno == ENOENT)
			ret = J_ENOJOURNAL;
		goto exit;
	}
	if (!S_ISDIR(sinfo.st_mode)) {
		ret = J_ENOJOURNAL;
		goto exit;
	}

	fs.jdirfd = open(fs.jdir, O_RDONLY);
	if (fs.jdirfd < 0) {
		ret = J_EIO;
		if (errno == ENOENT)
			ret = J_ENOJOURNAL;
		goto exit;
	}

	/* open the lock file, which is only used to complete the jfs
	 * structure */
	snprintf(jlockfile, PATH_MAX, "%s/%s", fs.jdir, "lock");
	rv = open(jlockfile, O_RDWR | O_CREAT, 0600);
	if (rv < 0) {
		ret = J_EIO;
		if (errno == ENOENT)
			ret = J_ENOJOURNAL;
		goto exit;
	}
	fs.jfd = rv;

	fs.jmap = (unsigned int *) mmap(NULL, sizeof(unsigned int),
			PROT_READ | PROT_WRITE, MAP_SHARED, fs.jfd, 0);
	if (fs.jmap == MAP_FAILED) {
		ret = J_EIO;
		goto exit;
	}

	dir = opendir(fs.jdir);
	if (dir == NULL) {
		ret = J_EIO;
		if (errno == ENOENT)
			ret = J_ENOJOURNAL;
		goto exit;
	}

	/* find the greatest transaction number by looking into the journal
	 * directory */
	maxtid = 0;
	for (errno = 0, dent = readdir(dir); dent != NULL;
			errno = 0, dent = readdir(dir)) {
		/* see if the file is named like a transaction, ignore
		 * otherwise; as transactions are named as numbers > 0, a
		 * simple atoi() is enough testing */
		rv = atoi(dent->d_name);
		if (rv <= 0)
			continue;
		if (rv > maxtid)
			maxtid = rv;
	}
	if (errno) {
		ret = J_EIO;
		goto exit;
	}

	/* rewrite the lockfile, writing the new maxtid on it, so that when we
	 * rollback a transaction it doesn't step over existing ones */
	rv = spwrite(fs.jfd, &maxtid, sizeof(maxtid), 0);
	if (rv != sizeof(maxtid)) {
		ret = J_ENOMEM;
		goto exit;
	}

	/* remove the broken mark so we can call jtrans_commit() */
	snprintf(brokenname, PATH_MAX, "%s/broken", fs.jdir);
	rv = access(brokenname, F_OK);
	if (rv == 0) {
		if (unlink(brokenname) != 0) {
			ret = J_EIO;
			goto exit;
		}
	} else if (errno != ENOENT) {
		ret = J_EIO;
		goto exit;
	}

	/* verify (and possibly fix) all the transactions */
	for (i = 1; i <= maxtid; i++) {
		curts = jtrans_new(&fs, 0);
		if (curts == NULL) {
			ret = J_ENOMEM;
			goto exit;
		}

		curts->id = i;

		/* open the transaction file, using i as its name, so we are
		 * really looping in order (recovering transaction in a
		 * different order as they were applied would result in
		 * corruption) */
		get_jtfile(&fs, i, tname);
		tfd = open(tname, O_RDWR | O_SYNC, 0600);
		if (tfd < 0) {
			if (errno == ENOENT) {
				res->invalid++;
				goto nounlink_loop;
			} else {
				ret = J_EIO;
				goto exit;
			}
		}

		/* try to lock the transaction file, if it's locked then it is
		 * currently being used so we skip it */
		lr = plockf(tfd, F_TLOCKW, 0, 0);
		if (lr == -1) {
			res->in_progress++;
			goto loop;
		}

		filelen = lseek(tfd, 0, SEEK_END);
		if (filelen == 0) {
			res->broken++;
			goto loop;
		} else if (filelen < 0) {
			ret = J_EIO;
			goto exit;
		}

		/* no overflow problems because we know the transaction size
		 * is limited to SSIZE_MAX */
		map = mmap((void *) 0, filelen, PROT_READ, MAP_SHARED, tfd, 0);
		if (map == MAP_FAILED) {
			map = NULL;
			ret = J_EIO;
			goto exit;
		}

		rv = fill_trans(map, filelen, curts);
		if (rv == -1) {
			res->broken++;
			goto loop;
		} else if (rv == -2) {
			res->corrupt++;
			goto loop;
		}

		/* remove flags from the transaction, so we don't have issues
		 * re-committing */
		curts->flags = 0;

		rv = jtrans_commit(curts);

		if (rv < 0) {
			ret = J_EIO;
			goto exit;
		}
		res->reapplied++;

loop:
		if (unlink(tname) != 0) {
			ret = J_EIO;
			goto exit;
		}

nounlink_loop:
		if (tfd >= 0) {
			close(tfd);
			tfd = -1;
		}
		if (map != NULL)
			munmap(map, filelen);

		while (curts->op != NULL) {
			tmpop = curts->op->next;
			if (curts->op->pdata)
				free(curts->op->pdata);
			free(curts->op);
			curts->op = tmpop;
		}
		pthread_mutex_destroy(&(curts->lock));
		free(curts);

		res->total++;
	}

	if (flags & J_CLEANUP) {
		if (jfsck_cleanup(name, fs.jdir) < 0) {
			ret = J_ECLEANUP;
		}
	}

exit:
	if (fs.fd >= 0)
		close(fs.fd);
	if (fs.jfd >= 0)
		close(fs.jfd);
	if (fs.jdirfd >= 0)
		close(fs.jdirfd);
	if (fs.jdir)
		free(fs.jdir);
	if (dir != NULL)
		closedir(dir);
	if (fs.jmap != MAP_FAILED)
		munmap(fs.jmap, sizeof(unsigned int));

	return ret;
}


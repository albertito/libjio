
/*
 * libjio - A library for Journaled I/O
 * Alberto Bertogli (albertogli@telpin.com.ar)
 *
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


/* fill a transaction structure from a mmapped transaction file */
static int fill_trans(unsigned char *map, off_t len, struct jtrans *ts)
{
	int i;
	unsigned char *p;
	struct joper *op, *tmp;

	if (len < J_DISKHEADSIZE)
		return 0;

	p = map;

	ts->id = *( (uint32_t *) p);
	p += 4;

	ts->flags = *( (uint32_t *) p);
	p += 4;

	ts->numops = *( (uint32_t *) p);
	p += 4;

	for (i = 0; i < ts->numops; i++) {
		if (len < (p - map) + J_DISKOPHEADSIZE)
			goto error;

		op = malloc(sizeof(struct joper));
		if (op == NULL)
			goto error;

		op->len = *( (uint32_t *) p);
		p += 4;

		op->plen = *( (uint32_t *) p);
		p += 4;

		op->offset = *( (uint64_t *) p);
		p += 8;

		if (len < (p - map) + op->len)
			goto error;

		op->buf = (void *) p;
		p += op->len;

		op->pdata = NULL;

		if (ts->op == NULL) {
			ts->op = op;
			op->prev = NULL;
			op->next = NULL;
		} else {
			for(tmp = ts->op; tmp->next != NULL; tmp = tmp->next)
				;
			tmp->next = op;
			op->prev = tmp;
			op->next = NULL;
		}
	}

	return 1;

error:
	while (ts->op != NULL) {
		tmp = ts->op->next;
		free(ts->op);
		ts->op = tmp;
	}
	return 0;
}

/* check the journal and rollback incomplete transactions */
int jfsck(const char *name, struct jfsck_result *res)
{
	int tfd, rv, i, ret;
	unsigned int maxtid;
	uint32_t csum1, csum2;
	char jdir[PATH_MAX], jlockfile[PATH_MAX], tname[PATH_MAX];
	struct stat sinfo;
	struct jfs fs;
	struct jtrans *curts;
	struct joper *tmpop;
	DIR *dir;
	struct dirent *dent;
	unsigned char *map;
	off_t filelen;

	tfd = -1;
	filelen = 0;
	dir = NULL;
	fs.fd = -1;
	fs.jfd = -1;
	fs.jdirfd = -1;
	fs.jmap = MAP_FAILED;
	map = NULL;
	ret = 0;

	res->total = 0;
	res->invalid = 0;
	res->in_progress = 0;
	res->broken = 0;
	res->corrupt = 0;
	res->apply_error = 0;
	res->reapplied = 0;

	fs.fd = open(name, O_RDWR | O_SYNC | O_LARGEFILE);
	if (fs.fd < 0) {
		ret = J_ENOENT;
		goto exit;
	}

	fs.name = (char *) name;

	if (!get_jdir(name, jdir)) {
		ret = J_ENOMEM;
		goto exit;
	}
	rv = lstat(jdir, &sinfo);
	if (rv < 0 || !S_ISDIR(sinfo.st_mode)) {
		ret = J_ENOJOURNAL;
		goto exit;
	}

	fs.jdirfd = open(jdir, O_RDONLY);
	if (fs.jdirfd < 0) {
		ret = J_ENOJOURNAL;
		goto exit;
	}

	/* open the lock file, which is only used to complete the jfs
	 * structure */
	snprintf(jlockfile, PATH_MAX, "%s/%s", jdir, "lock");
	rv = open(jlockfile, O_RDWR | O_CREAT, 0600);
	if (rv < 0) {
		ret = J_ENOJOURNAL;
		goto exit;
	}
	fs.jfd = rv;

	fs.jmap = (unsigned int *) mmap(NULL, sizeof(unsigned int),
			PROT_READ | PROT_WRITE, MAP_SHARED, fs.jfd, 0);
	if (fs.jmap == MAP_FAILED) {
		ret = J_ENOJOURNAL;
		goto exit;
	}

	dir = opendir(jdir);
	if (dir == NULL) {
		ret = J_ENOJOURNAL;
		goto exit;
	}

	/* loop for each file in the journal directory to find out the greater
	 * transaction number */
	maxtid = 0;
	for (dent = readdir(dir); dent != NULL; dent = readdir(dir)) {
		/* see if the file is named like a transaction, ignore
		 * otherwise; as transactions are named as numbers > 0, a
		 * simple atoi() is enough testing */
		rv = atoi(dent->d_name);
		if (rv <= 0)
			continue;
		if (rv > maxtid)
			maxtid = rv;
	}

	/* rewrite the lockfile, writing the new maxtid on it, so that when we
	 * rollback a transaction it doesn't step over existing ones */
	rv = spwrite(fs.jfd, &maxtid, sizeof(maxtid), 0);
	if (rv != sizeof(maxtid)) {
		ret = J_ENOMEM;
		goto exit;
	}

	/* we loop all the way up to the max transaction id */
	for (i = 1; i <= maxtid; i++) {
		curts = malloc(sizeof(struct jtrans));
		if (curts == NULL) {
			ret = J_ENOMEM;
			goto exit;
		}

		jtrans_init(&fs, curts);
		curts->id = i;

		/* open the transaction file, using i as its name, so we are
		 * really looping in order (recovering transaction in a
		 * different order as they were applied means instant
		 * corruption) */
		if (!get_jtfile(name, i, tname)) {
			ret = J_ENOMEM;
			goto exit;
		}
		tfd = open(tname, O_RDWR | O_SYNC | O_LARGEFILE, 0600);
		if (tfd < 0) {
			res->invalid++;
			goto loop;
		}

		/* try to lock the transaction file, if it's locked then it is
		 * currently being used so we skip it */
		rv = plockf(tfd, F_TLOCKW, 0, 0);
		if (rv == -1) {
			res->in_progress++;
			goto loop;
		}

		filelen = lseek(tfd, 0, SEEK_END);
		map = mmap(0, filelen, PROT_READ, MAP_SHARED, tfd, 0);
		if (map == MAP_FAILED) {
			res->broken++;
			map = NULL;
			goto loop;
		}
		rv = fill_trans(map, filelen, curts);
		if (rv != 1) {
			res->broken++;
			goto loop;
		}

		/* verify the checksum */
		csum1 = checksum_map(map, filelen - (sizeof(uint32_t)));
		csum2 = * (uint32_t *) (map + filelen - (sizeof(uint32_t)));
		if (csum1 != csum2) {
			res->corrupt++;
			goto loop;
		}

		/* remove flags from the transaction */
		curts->flags = 0;

		rv = jtrans_commit(curts);

		if (rv < 0) {
			res->apply_error++;
			goto loop;
		}
		res->reapplied++;

loop:
		if (tfd >= 0) {
			close(tfd);
			tfd = -1;
		}
		if (map != NULL)
			munmap(map, filelen);

		if (curts->name)
			free(curts->name);
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

exit:
	if (fs.fd >= 0)
		close(fs.fd);
	if (fs.jfd >= 0)
		close(fs.jfd);
	if (fs.jdirfd >= 0)
		close(fs.jdirfd);
	if (dir != NULL)
		closedir(dir);
	if (fs.jmap != MAP_FAILED)
		munmap(fs.jmap, sizeof(unsigned int));

	return ret;

}

/* remove all the files in the journal directory (if any) */
int jfsck_cleanup(const char *name)
{
	char jdir[PATH_MAX], tfile[PATH_MAX*3];
	DIR *dir;
	struct dirent *dent;

	if (!get_jdir(name, jdir))
		return 0;

	dir = opendir(jdir);
	if (dir == NULL && errno == ENOENT)
		/* it doesn't exist, so it's clean */
		return 1;
	else if (dir == NULL)
		return 0;

	for (dent = readdir(dir); dent != NULL; dent = readdir(dir)) {
		/* we only care about transactions (named as numbers > 0) and
		 * the lockfile (named "lock"); ignore everything else */
		if (strcmp(dent->d_name, "lock") && atoi(dent->d_name) <= 0)
			continue;

		/* build the full path to the transaction file */
		memset(tfile, 0, PATH_MAX * 3);
		strcat(tfile, jdir);
		strcat(tfile, "/");
		strcat(tfile, dent->d_name);

		/* the full filename is too large */
		if (strlen(tfile) > PATH_MAX) {
			closedir(dir);
			return 0;
		}

		/* and remove it */
		unlink(tfile);
	}
	closedir(dir);

	rmdir(jdir);

	return 1;
}


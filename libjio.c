
/*
 * libjio - A library for Journaled I/O
 * Alberto Bertogli (albertogli@telpin.com.ar)
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <libgen.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/uio.h>
#include <errno.h>

#include "libjio.h"


/*
 * small util functions
 */

/* like lockf, but lock always from the beginning of the file */
static off_t plockf(int fd, int cmd, off_t offset, off_t len)
{
	struct flock fl;
	int op;

	if (cmd == F_LOCK) {
		fl.l_type = F_WRLCK;
		op = F_SETLKW;
	} else if (cmd == F_ULOCK) {
		fl.l_type = F_UNLCK;
		op = F_SETLKW;
	} else if (cmd == F_TLOCK) {
		fl.l_type = F_WRLCK;
		op = F_SETLK;
	} else
		return 0;
	
	fl.l_whence = SEEK_SET;
	fl.l_start = offset;
	fl.l_len = len;
	
	return fcntl(fd, op, &fl);
}

/* like pread but either fails, or return a complete read; if we return less
 * than count is because EOF was reached */
static ssize_t spread(int fd, void *buf, size_t count, off_t offset)
{
	int rv, c;

	c = 0;

	while (c < count) {
		rv = pread(fd, (char *) buf + c, count - c, offset + c);
		
		if (rv == count)
			/* we're done */
			return count;
		else if (rv < 0)
			/* error */
			return rv;
		else if (rv == 0)
			/* got EOF */
			return c;
		
		/* incomplete read, keep on reading */
		c += rv;
	}
	
	return count;
}

/* like spread() but for pwrite() */
static ssize_t spwrite(int fd, const void *buf, size_t count, off_t offset)
{
	int rv, c;

	c = 0;

	while (c < count) {
		rv = pwrite(fd, (char *) buf + c, count - c, offset + c);
		
		if (rv == count)
			/* we're done */
			return count;
		else if (rv <= 0)
			/* error/nothing was written */
			return rv;
		
		/* incomplete write, keep on writing */
		c += rv;
	}
	
	return count;
}

/* build the journal directory name out of the filename */
static int get_jdir(const char *filename, char *jdir)
{
	char *base, *baset;
	char *dir, *dirt;

	baset = strdup(filename);
	if (baset == NULL)
		return 0;
	base = basename(baset);

	dirt = strdup(filename);
	if (dirt == NULL)
		return 0;
	dir = dirname(dirt);

	snprintf(jdir, PATH_MAX, "%s/.%s.jio", dir, base);

	free(baset);
	free(dirt);

	return 1;
}

/* build the filename of a given transaction */
static int get_jtfile(const char *filename, int tid, char *jtfile)
{
	char *base, *baset;
	char *dir, *dirt;

	baset = strdup(filename);
	if (baset == NULL)
		return 0;
	base = basename(baset);

	dirt = strdup(filename);
	if (dirt == NULL)
		return 0;
	dir = dirname(dirt);

	snprintf(jtfile, PATH_MAX, "%s/.%s.jio/%d", dir, base, tid);

	free(baset);
	free(dirt);

	return 1;
}

/* gets a new transaction id */
static unsigned int get_tid(struct jfs *fs)
{
	unsigned int curid;
	int r, rv;
	
	/* lock the whole file */
	plockf(fs->jfd, F_LOCK, 0, 0);

	/* read the current max. curid */
	r = spread(fs->jfd, &curid, sizeof(curid), 0);
	if (r != sizeof(curid)) {
		rv = 0;
		goto exit;
	}
	
	/* increment it and handle overflows */
	rv = curid + 1;
	if (rv == 0)
		rv = 1;
	
	/* write to the file descriptor */
	r = spwrite(fs->jfd, &rv, sizeof(rv), 0);
	if (r != sizeof(curid)) {
		rv = 0;
		goto exit;
	}
	
exit:
	plockf(fs->jfd, F_ULOCK, 0, 0);
	return rv;
}

/* frees a transaction id */
static void free_tid(struct jfs *fs, unsigned int tid)
{
	unsigned int curid, i;
	int r;
	char name[PATH_MAX];
	
	/* lock the whole file */
	plockf(fs->jfd, F_LOCK, 0, 0);

	/* read the current max. curid */
	r = spread(fs->jfd, &curid, sizeof(curid), 0);
	if (r != sizeof(curid)) {
		goto exit;
	}

	if (tid < curid) {
		/* we're not freeing the max. curid, so we just return */
		goto exit;
	} else {
		/* look up the new max. */
		for (i = curid - 1; i > 0; i--) {
			/* this can fail if we're low on mem, but we don't
			 * care checking here because the problem will come
			 * out later and we can fail more properly */
			get_jtfile(fs->name, i, name);
			if (access(name, R_OK | W_OK) == 0) {
				curid = i;
				break;
			}
		}

		/* and save it */
		r = spwrite(fs->jfd, &i, sizeof(i), 0);
		if (r != sizeof(curid)) {
			goto exit;
		}
	}	

exit:
	plockf(fs->jfd, F_ULOCK, 0, 0);
	return;
}


/*
 * transaction functions
 */

/* initialize a transaction structure */
void jtrans_init(struct jfs *fs, struct jtrans *ts)
{
	ts->fs = fs;
	ts->name = NULL;
	ts->id = 0;
	ts->flags = 0;
	ts->buf = NULL;
	ts->len = 0;
	ts->offset = 0;
	ts->udata = NULL;
	ts->ulen = 0;
	ts->pdata = NULL;
	ts->plen = 0;
}

/* free the contents of a transaction structure */
void jtrans_free(struct jtrans *ts)
{
	/* NOTE: we only really free the name and previous data, which are the
	 * things _we_ allocate; the user data is caller stuff */
	ts->fs = NULL;
	if (ts->name)
		free(ts->name);
	if (ts->pdata)
		free(ts->pdata);

	/* don't free ts itself, it's very common to allocate it in the stack,
	 * so let the caller take care of it; and, after all, he was the one
	 * doing the alloc in the first place */
}

/* commit a transaction */
int jtrans_commit(struct jtrans *ts)
{
	int id, fd, rv, t;
	char *name;
	unsigned char *buf_init, *bufp;
	
	name = (char *) malloc(PATH_MAX);
	if (name == NULL)
		return -1;
	
	id = get_tid(ts->fs);
	if (id == 0)
		return -1;
	
	/* open the transaction file */
	if (!get_jtfile(ts->fs->name, id, name))
		return -1;
	fd = open(name, O_RDWR | O_CREAT | O_TRUNC | O_LARGEFILE, 0600);
	if (fd < 0)
		return -1;
	
	/* and lock it */
	plockf(fd, F_LOCK, 0, 0);
	
	ts->id = id;
	ts->name = name;
	
	/* lock the file region to work on */
	if (!(ts->fs->flags & J_NOLOCK))
		plockf(ts->fs->fd, F_LOCK, ts->offset, ts->len);
	
	/* read the current content and fill in the transaction structure */
	ts->pdata = malloc(ts->len);
	if (ts->pdata == NULL)
		goto exit;
	
	ts->plen = ts->len;

	rv = spread(ts->fs->fd, ts->pdata, ts->len, ts->offset);
	if (rv < 0)
		goto exit;
	if (rv < ts->len) {
		/* we are extending the file! use ftruncate() to do it */
		ftruncate(ts->fs->fd, ts->offset + ts->len);
		ts->plen = rv;
	}

	/* now save the transaction to the file, static data first */
	
	buf_init = malloc(J_DISKTFIXSIZE);
	if (buf_init == NULL)
		return -1;
	
	bufp = buf_init;
	
	memcpy(bufp, (void *) &(ts->id), sizeof(ts->id));
	bufp += 4;
	
	memcpy(bufp, (void *) &(ts->flags), sizeof(ts->flags));
	bufp += 4;
	
	memcpy(bufp, (void *) &(ts->len), sizeof(ts->len));
	bufp += 4;
	
	memcpy(bufp, (void *) &(ts->plen), sizeof(ts->plen));
	bufp += 4;
	
	memcpy(bufp, (void *) &(ts->ulen), sizeof(ts->ulen));
	bufp += 4;
	
	memcpy(bufp, (void *) &(ts->offset), sizeof(ts->offset));
	bufp += 8;
	
	rv = spwrite(fd, buf_init, J_DISKTFIXSIZE, 0);
	if (rv != J_DISKTFIXSIZE)
		goto exit;
	
	free(buf_init);
	
	
	/* and now the variable data */

	if (ts->udata) {
		rv = spwrite(fd, ts->udata, ts->ulen, J_DISKTFIXSIZE);
		if (rv != ts->ulen)
			goto exit;
	}
	
	t = J_DISKTFIXSIZE + ts->ulen;
	rv = spwrite(fd, ts->pdata, ts->plen, t);
	if (rv != ts->plen)
		goto exit;
	
	/* this is a simple but efficient optimization: instead of doing
	 * everything O_SYNC, we sync at this point only, this way we avoid
	 * doing a lot of very small writes; in case of a crash the
	 * transaction file is only useful if it's complete (ie. after this
	 * point) so we only flush here */
	fsync(fd);
	
	/* now that we have a safe transaction file, let's apply it */
	rv = spwrite(ts->fs->fd, ts->buf, ts->len, ts->offset);
	if (rv != ts->len)
		goto exit;
	
	/* the transaction has been applied, so we cleanup and remove it from
	 * the disk */
	free_tid(ts->fs, ts->id);
	unlink(name);

	/* mark the transaction as commited, _after_ it was removed */
	ts->flags = ts->flags | J_COMMITED;

	
exit:
	close(fd);
	
	if (!(ts->fs->flags & J_NOLOCK))
		plockf(ts->fs->fd, F_ULOCK, ts->offset, ts->len);
	
	/* return the lenght only if it was properly commited */
	if (ts->flags & J_COMMITED)
		return ts->len;
	else
		return -1;

}

/* rollback a transaction */
int jtrans_rollback(struct jtrans *ts)
{
	int rv;
	struct jtrans newts;

	/* copy the old transaction to the new one */
	jtrans_init(ts->fs, &newts);

	newts.flags = ts->flags;
	newts.offset = ts->offset;

	newts.buf = ts->pdata;
	newts.len = ts->plen;
	
	if (ts->plen < ts->len) {
		/* we extended the data in the previous transaction, so we
		 * should truncate it back */
		/* DANGEROUS: this is one of the main reasons why rollbacking
		 * is dangerous and should only be done with extreme caution:
		 * if for some reason, after the previous transacton, we have
		 * extended the file further, this will cut it back to what it
		 * was; read the docs for more detail */
		ftruncate(ts->fs->fd, ts->offset + ts->plen);
		
	}
	
	newts.pdata = ts->pdata;
	newts.plen = ts->plen;

	newts.udata = ts->udata;
	newts.ulen = ts->ulen;
	
	rv = jtrans_commit(&newts);
	return rv;
}


/*
 * basic operations
 */

/* open a file */
int jopen(struct jfs *fs, const char *name, int flags, int mode, int jflags)
{
	int fd, jfd, rv;
	unsigned int t;
	char jdir[PATH_MAX], jlockfile[PATH_MAX];
	struct stat sinfo;
	
	fd = open(name, flags, mode);
	if (fd < 0)
		return -1;

	fs->fd = fd;
	fs->name = strdup(name);
	fs->flags = jflags;
	
	/* Note on fs->lock usage: this lock is used only inside the wrappers,
	 * and exclusively to protect the file pointer. This means that it
	 * must only be held while performing operations that depend or alter
	 * the file pointer (jread, jreadv, jwrite, jwritev), but the others
	 * (jpread, jpwrite) are left unprotected because they can be
	 * performed in paralell as long as they don't affect the same portion
	 * of the file (this is protected by lockf). The lock doesn't slow
	 * things down tho: any threaded app MUST implement this kind of
	 * locking anyways if it wants to prevent data corruption, we only
	 * make it easier for them by taking care of it here. If performance
	 * is essential, the jpread/jpwrite functions should be used, just as
	 * real life. */
	
	pthread_mutex_init( &(fs->lock), NULL);

	if (!get_jdir(name, jdir))
		return -1;
	rv = mkdir(jdir, 0750);
	rv = lstat(jdir, &sinfo);
	if (rv < 0 || !S_ISDIR(sinfo.st_mode))
		return -1;
	
	snprintf(jlockfile, PATH_MAX, "%s/%s", jdir, "lock");
	jfd = open(jlockfile, O_RDWR | O_CREAT, 0600);
	if (jfd < 0)
		return -1;
	
	/* initialize the lock file by writing the first tid to it, but only
	 * if its empty, otherwise there is a race if two processes call
	 * jopen() simultaneously and both initialize the file */
	plockf(jfd, F_LOCK, 0, 0);
	lstat(jlockfile, &sinfo);
	if (sinfo.st_size == 0) {
		t = 1;
		rv = write(jfd, &t, sizeof(t));
		if (rv != sizeof(t)) {
			plockf(jfd, F_ULOCK, 0, 0);
			return -1;
		}
	}
	plockf(jfd, F_ULOCK, 0, 0);

	fs->jfd = jfd;

	return fd;
}


/* read() family wrappers */

/* read wrapper */
ssize_t jread(struct jfs *fs, void *buf, size_t count)
{
	int rv;
	off_t pos;

	pthread_mutex_lock(&(fs->lock));
	pos = lseek(fs->fd, 0, SEEK_CUR);
	plockf(fs->fd, F_LOCK, pos, count);
	rv = read(fs->fd, buf, count);
	plockf(fs->fd, F_ULOCK, pos, count);
	pthread_mutex_unlock(&(fs->lock));

	return rv;
}

/* pread wrapper */
ssize_t jpread(struct jfs *fs, void *buf, size_t count, off_t offset)
{
	int rv;

	plockf(fs->fd, F_LOCK, offset, count);
	rv = pread(fs->fd, buf, count, offset);
	plockf(fs->fd, F_ULOCK, offset, count);
	
	return rv;
}

/* readv wrapper */
ssize_t jreadv(struct jfs *fs, struct iovec *vector, int count)
{
	int rv, i;
	size_t sum;
	off_t pos;
	
	sum = 0;
	for (i = 0; i < count; i++)
		sum += vector[i].iov_len;
	
	pthread_mutex_lock(&(fs->lock));
	pos = lseek(fs->fd, 0, SEEK_CUR);
	plockf(fs->fd, F_LOCK, pos, count);
	rv = readv(fs->fd, vector, count);
	plockf(fs->fd, F_ULOCK, pos, count);
	pthread_mutex_unlock(&(fs->lock));

	return rv;
}

/* write wrapper */
ssize_t jwrite(struct jfs *fs, const void *buf, size_t count)
{
	int rv;
	off_t pos;
	struct jtrans ts;
	
	pthread_mutex_lock(&(fs->lock));
	
	jtrans_init(fs, &ts);
	pos = lseek(fs->fd, 0, SEEK_CUR);
	ts.offset = pos;
	
	ts.buf = buf;
	ts.len = count;
	
	rv = jtrans_commit(&ts);

	if (rv >= 0) {
		/* if success, advance the file pointer */
		lseek(fs->fd, count, SEEK_CUR);
	}
	
	pthread_mutex_unlock(&(fs->lock));

	jtrans_free(&ts);

	return rv;
}

/* write family wrappers */

/* pwrite wrapper */
ssize_t jpwrite(struct jfs *fs, const void *buf, size_t count, off_t offset)
{
	int rv;
	struct jtrans ts;
	
	jtrans_init(fs, &ts);
	ts.offset = offset;
	
	ts.buf = buf;
	ts.len = count;
	
	rv = jtrans_commit(&ts);

	jtrans_free(&ts);

	return rv;
}

/* writev wrapper */
ssize_t jwritev(struct jfs *fs, const struct iovec *vector, int count)
{
	int rv, i, bufp;
	ssize_t sum;
	char *buf;
	off_t pos;
	struct jtrans ts;
	
	sum = 0;
	for (i = 0; i < count; i++)
		sum += vector[i].iov_len;

	/* unify the buffers into one big chunk to commit */
	/* FIXME: can't we do this more efficient? It ruins the whole purpose
	 * of using writev() :\
	 * maybe we should do one transaction per vector */
	buf = malloc(sum);
	if (buf == NULL)
		return -1;
	bufp = 0;

	for (i = 0; i < count; i++) {
		memcpy(buf + bufp, vector[i].iov_base, vector[i].iov_len);
		bufp += vector[i].iov_len;
	}
	
	pthread_mutex_lock(&(fs->lock));
	
	jtrans_init(fs, &ts);
	pos = lseek(fs->fd, 0, SEEK_CUR);
	ts.offset = pos;
	
	ts.buf = buf;
	ts.len = sum;
	
	rv = jtrans_commit(&ts);

	if (rv >= 0) {
		/* if success, advance the file pointer */
		lseek(fs->fd, count, SEEK_CUR);
	}
	
	pthread_mutex_unlock(&(fs->lock));

	jtrans_free(&ts);

	return rv;

}

/* truncate a file - be careful with this */
int jtruncate(struct jfs *fs, off_t lenght)
{
	int rv;
	
	/* lock from lenght to the end of file */
	plockf(fs->fd, F_LOCK, lenght, 0);
	rv = ftruncate(fs->fd, lenght);
	plockf(fs->fd, F_ULOCK, lenght, 0);
	
	return rv;
}

/* close a file */
int jclose(struct jfs *fs)
{
	if (close(fs->fd))
		return -1;
	if (close(fs->jfd))
		return -1;
	if (fs->name)
		/* allocated by strdup() in jopen() */
		free(fs->name);
	return 0;
}


/*
 * journal recovery
 */

/* check the journal and replay the incomplete transactions */
int jfsck(const char *name, struct jfsck_result *res)
{
	int fd, tfd, rv, i, maxtid;
	char jdir[PATH_MAX], jlockfile[PATH_MAX], tname[PATH_MAX];
	unsigned char *buf = NULL;
	struct stat sinfo;
	struct jfs fs;
	struct jtrans *curts;
	DIR *dir;
	struct dirent *dent;
	off_t offset;
	
	fd = open(name, O_RDWR | O_SYNC | O_LARGEFILE);
	if (fd < 0)
		return J_ENOENT;

	fs.fd = fd;
	fs.name = (char *) name;

	if (!get_jdir(name, jdir))
		return J_ENOMEM;
	rv = lstat(jdir, &sinfo);
	if (rv < 0 || !S_ISDIR(sinfo.st_mode))
		return J_ENOJOURNAL;
	
	/* open the lock file, which is only used to complete the jfs
	 * structure */
	snprintf(jlockfile, PATH_MAX, "%s/%s", jdir, "lock");
	rv = open(jlockfile, O_RDWR | O_CREAT, 0600);
	if (rv < 0)
		return J_ENOJOURNAL;
	fs.jfd = rv;
	
	dir = opendir(jdir);
	if (dir == NULL)
		return J_ENOJOURNAL;

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
	closedir(dir);

	/* rewrite the lockfile, writing the new maxtid on it, so that when we
	 * rollback a transaction it doesn't step over existing ones */
	rv = spwrite(fs.jfd, &maxtid, sizeof(maxtid), 0);
	if (rv != sizeof(maxtid)) {
		return J_ENOMEM;
	}

	/* we loop all the way up to the max transaction id */
	for (i = 1; i <= maxtid; i++) {
		curts = malloc(sizeof(struct jtrans));
		if (curts == NULL)
			return J_ENOMEM;
		
		jtrans_init(&fs, curts);
		curts->id = i;
		
		/* open the transaction file, using i as its name, so we are
		 * really looping in order (recovering transaction in a
		 * different order as they were applied means instant
		 * corruption) */
		if (!get_jtfile(name, i, tname))
			return J_ENOMEM;
		tfd = open(tname, O_RDWR | O_SYNC | O_LARGEFILE, 0600);
		if (tfd < 0) {
			res->invalid++;
			goto loop;
		}

		/* try to lock the transaction file, if it's locked then it is
		 * currently being used so we skip it */
		rv = plockf(tfd, F_TLOCK, 0, 0);
		if (rv == -1) {
			res->in_progress++;
			goto loop;
		}
		
		/* load from disk, header first */
		buf = (unsigned char *) malloc(J_DISKTFIXSIZE);
		if (buf == NULL) {
			res->load_error++;
			goto loop;
		}
		
		rv = read(tfd, buf, J_DISKTFIXSIZE);
		if (rv != J_DISKTFIXSIZE) {
			res->broken_head++;
			free(buf);
			goto loop;
		}

		curts->flags = *( (uint32_t *) (buf + 4));
		curts->len = *( (uint32_t *) (buf + 8));
		curts->plen = *( (uint32_t *) (buf + 12));
		curts->ulen = *( (uint32_t *) (buf + 16));
		curts->offset = *( (uint64_t *) (buf + 20));

		free(buf);

		/* if we got here, the transaction was not applied, so we
		 * check if the transaction file is complete (we only need to
		 * rollback it) or not (so we can't do anything but ignore it)
		 */

		lstat(tname, &sinfo);
		rv = J_DISKTFIXSIZE + curts->ulen + curts->plen;
		if (sinfo.st_size != rv) {
			/* the transaction file is incomplete, some of the
			 * body is missing */
			res->broken_body++;
			goto loop;
		}

		/* we have a complete transaction file which commit was not
		 * successful, so we read it to complete the transaction
		 * structure and rollback it */
		curts->pdata = malloc(curts->plen);
		if (curts->pdata == NULL) {
			res->load_error++;
			goto loop;
		}
		
		curts->udata = malloc(curts->ulen);
		if (curts->udata == NULL) {
			res->load_error++;
			goto loop;
		}

		/* user data */
		offset = J_DISKTFIXSIZE;
		rv = spread(tfd, curts->udata, curts->ulen, offset);
		if (rv != curts->ulen) {
			res->load_error++;
			goto loop;
		}

		/* previous data */
		offset = J_DISKTFIXSIZE + curts->ulen;
		rv = spread(tfd, curts->pdata, curts->plen, offset);
		if (rv != curts->plen) {
			res->load_error++;
			goto loop;
		}

		/* rollback */
		rv = jtrans_rollback(curts);
		if (rv < 0) {
			res->apply_error++;
			goto loop;
		}
		res->rollbacked++;

		/* free the data we just allocated */
		if (curts->plen) {
			free(curts->pdata);
			curts->pdata = NULL;
		}
		if (curts->ulen) {
			free(curts->udata);
			curts->udata = NULL;
		}
		if (curts->name) {
			free(curts->name);
			curts->name = NULL;
		}

loop:
		if (tfd > 0)
			close(tfd);

		free(curts);

		res->total++;
	}

	close(fs.fd);
	close(fs.jfd);

	return 0;

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
		if (strlen(tfile) > PATH_MAX)
			return 0;

		/* and remove it */
		unlink(tfile);
	}
	closedir(dir);

	return 1;
}


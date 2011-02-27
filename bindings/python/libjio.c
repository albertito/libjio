
/*
 * Python (2 and 3) bindings for libjio
 * Alberto Bertogli (albertito@blitiri.com.ar)
 */

#define PY_SSIZE_T_CLEAN 1

#include <Python.h>

#include <libjio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

/*
 * This module provides two classes and some functions.
 *
 * The classes are jfile (created with open()) and jtrans (created with
 * jfile.new_trans()).
 *
 * The first one represents a journaled file where you operate using read(),
 * write() and so on; to close it, just call del(). This is similar to the
 * UNIX file.
 *
 * The second one represents a single transaction, which is composed of
 * several operations that get added by its add() method. It gets committed
 * with commit(), and rolled back with rollback().
 *
 * There rest of the module's functions are related to file checking, at the
 * moment only jfsck(), which is just a wrapper to the real C functions.
 */

/*
 * Type definitions
 */

/* jfile */
typedef struct {
	PyObject_HEAD
	jfs_t *fs;
} jfile_object;

static PyTypeObject jfile_type;


/* jtrans */
typedef struct {
	PyObject_HEAD
	jtrans_t *ts;
	jfile_object *jfile;

	/* add_r() allocates views which must be freed by the destructor */
	Py_buffer **views;
	size_t nviews;
} jtrans_object;

static PyTypeObject jtrans_type;


/*
 * The jfile object
 */

/* delete */
static void jf_dealloc(jfile_object *fp)
{
	if (fp->fs) {
		jclose(fp->fs);
	}
	PyObject_Del(fp);
}


/* In order to support older Python versions, we use this function to convert
 * from ssize_t to a Python long. */
PyObject *Our_PyLong_FromSsize_t(ssize_t v)
{
	/* Use PyLong_FromSsize_t() if available (Python 3 and >= 2.6),
	 * otherwise just fall back to the function that fits the size. */
#if PY_MAJOR_VERSION >= 3 || (PY_MAJOR_VERSION == 2 && PY_MINOR_VERSION >= 6)
	return PyLong_FromSsize_t(v);
#else
	switch (sizeof(ssize_t)) {
		case sizeof(long long):
			return PyLong_FromLongLong(v);
		default:
			return PyLong_FromLong(v);
	}
#endif
}


/* fileno */
PyDoc_STRVAR(jf_fileno__doc,
"fileno()\n\
\n\
Return the file descriptor number for the file.\n");

static PyObject *jf_fileno(jfile_object *fp, PyObject *args)
{
	if (!PyArg_ParseTuple(args, ":fileno"))
		return NULL;

	return PyLong_FromLong(jfileno(fp->fs));
}

/* read */
PyDoc_STRVAR(jf_read__doc,
"read(size)\n\
\n\
Read at most size bytes from the file, returns the string with\n\
the contents.\n\
It's a wrapper to jread().\n");

static PyObject *jf_read(jfile_object *fp, PyObject *args)
{
	ssize_t rv, len;
	unsigned char *buf;
	PyObject *r;

	if (!PyArg_ParseTuple(args, "n:read", &len))
		return NULL;

	if (len < 0) {
		PyErr_SetString(PyExc_TypeError, "len must be >= 0");
		return NULL;
	}

	buf = malloc(len);
	if (buf == NULL)
		return PyErr_NoMemory();

	Py_BEGIN_ALLOW_THREADS
	rv = jread(fp->fs, buf, len);
	Py_END_ALLOW_THREADS

	if (rv < 0) {
		r = PyErr_SetFromErrno(PyExc_IOError);
	} else {
#ifdef PYTHON3
		r = PyBytes_FromStringAndSize((char *) buf, rv);
#elif PYTHON2
		r = PyString_FromStringAndSize((char *) buf, rv);
#endif
	}

	free(buf);
	return r;
}

/* pread */
PyDoc_STRVAR(jf_pread__doc,
"pread(size, offset)\n\
\n\
Read size bytes from the file at the given offset, return a string with the\n\
contents.\n\
It's a wrapper to jpread().\n");

static PyObject *jf_pread(jfile_object *fp, PyObject *args)
{
	ssize_t rv, len;
	long long offset;
	unsigned char *buf;
	PyObject *r;

	if (!PyArg_ParseTuple(args, "nL:pread", &len, &offset))
		return NULL;

	if (len < 0) {
		PyErr_SetString(PyExc_TypeError, "len must be >= 0");
		return NULL;
	}

	if (offset < 0) {
		PyErr_SetString(PyExc_TypeError, "offset must be >= 0");
		return NULL;
	}

	buf = malloc(len);
	if (buf == NULL)
		return PyErr_NoMemory();

	Py_BEGIN_ALLOW_THREADS
	rv = jpread(fp->fs, buf, len, offset);
	Py_END_ALLOW_THREADS

	if (rv < 0) {
		r = PyErr_SetFromErrno(PyExc_IOError);
	} else {
#ifdef PYTHON3
		r = PyBytes_FromStringAndSize((char *) buf, rv);
#elif PYTHON2
		r = PyString_FromStringAndSize((char *) buf, rv);
#endif
	}

	free(buf);
	return r;
}

/* readv */
PyDoc_STRVAR(jf_readv__doc,
"readv([buf1, buf2, ...])\n\
\n\
Reads the data from the file into the different buffers; returns the\n\
number of bytes written.\n\
The buffers must be objects that support slice assignment, like bytearray\n\
(but *not* str).\n\
Only available in Python >= 2.6.\n\
It's a wrapper to jreadv().\n");

/* readv requires the new Py_buffer interface, which is only available in
 * Python >= 2.6 */
#if PY_MAJOR_VERSION >= 3 || (PY_MAJOR_VERSION == 2 && PY_MINOR_VERSION >= 6)
static PyObject *jf_readv(jfile_object *fp, PyObject *args)
{
	ssize_t rv;
	PyObject *buffers, *buf;
	Py_buffer *views = NULL;
	ssize_t len, pos = 0;
	struct iovec *iov = NULL;

	if (!PyArg_ParseTuple(args, "O:readv", &buffers))
		return NULL;

	len = PySequence_Length(buffers);
	if (len < 0) {
		PyErr_SetString(PyExc_TypeError, "iterable expected");
		return NULL;
	}

	iov = malloc(sizeof(struct iovec) * len);
	if (iov == NULL)
		return PyErr_NoMemory();

	views = malloc(sizeof(Py_buffer) * len);
	if (views == NULL) {
		free(iov);
		return PyErr_NoMemory();
	}

	for (pos = 0; pos < len; pos++) {
		buf = PySequence_GetItem(buffers, pos);
		if (buf == NULL)
			goto error;

		if (!PyObject_CheckBuffer(buf)) {
			PyErr_SetString(PyExc_TypeError,
				"object must support the buffer interface");
			goto error;
		}

		if (PyObject_GetBuffer(buf, &(views[pos]), PyBUF_CONTIG))
			goto error;

		iov[pos].iov_base = views[pos].buf;
		iov[pos].iov_len = views[pos].len;
	}

	Py_BEGIN_ALLOW_THREADS
	rv = jreadv(fp->fs, iov, len);
	Py_END_ALLOW_THREADS

	for (pos = 0; pos < len; pos++) {
		PyBuffer_Release(&(views[pos]));
	}

	free(iov);
	free(views);

	if (rv < 0)
		return PyErr_SetFromErrno(PyExc_IOError);

	return Our_PyLong_FromSsize_t(rv);

error:
	/* We might get here with pos between 0 and len, so we must release
	 * only what we have already taken */
	pos--;
	while (pos >= 0) {
		PyBuffer_Release(&(views[pos]));
		pos--;
	}

	free(iov);
	free(views);
	return NULL;
}

#else

static PyObject *jf_readv(jfile_object *fp, PyObject *args)
{
	PyErr_SetString(PyExc_NotImplementedError,
			"only supported in Python >= 2.6");
	return NULL;
}

#endif /* python version >= 2.6 */

/* write */
PyDoc_STRVAR(jf_write__doc,
"write(buf)\n\
\n\
Write the contents of the given buffer (a string) to the file, returns the\n\
number of bytes written.\n\
It's a wrapper to jwrite().\n");

static PyObject *jf_write(jfile_object *fp, PyObject *args)
{
	ssize_t rv;
	unsigned char *buf;
	ssize_t len;

	if (!PyArg_ParseTuple(args, "s#:write", &buf, &len))
		return NULL;

	Py_BEGIN_ALLOW_THREADS
	rv = jwrite(fp->fs, buf, len);
	Py_END_ALLOW_THREADS

	if (rv < 0)
		return PyErr_SetFromErrno(PyExc_IOError);

	return Our_PyLong_FromSsize_t(rv);
}

/* pwrite */
PyDoc_STRVAR(jf_pwrite__doc,
"pwrite(buf, offset)\n\
\n\
Write the contents of the given buffer (a string) to the file at the given\n\
offset, returns the number of bytes written.\n\
It's a wrapper to jpwrite().\n");

static PyObject *jf_pwrite(jfile_object *fp, PyObject *args)
{
	ssize_t rv;
	unsigned char *buf;
	long long offset;
	ssize_t len;

	if (!PyArg_ParseTuple(args, "s#L:pwrite", &buf, &len, &offset))
		return NULL;

	if (offset < 0) {
		PyErr_SetString(PyExc_TypeError, "offset must be >= 0");
		return NULL;
	}

	Py_BEGIN_ALLOW_THREADS
	rv = jpwrite(fp->fs, buf, len, offset);
	Py_END_ALLOW_THREADS

	if (rv < 0)
		return PyErr_SetFromErrno(PyExc_IOError);

	return Our_PyLong_FromSsize_t(rv);
}

/* writev */
PyDoc_STRVAR(jf_writev__doc,
"writev([buf1, buf2, ...])\n\
\n\
Writes the data contained in the different buffers to the file, returns the\n\
number of bytes written.\n\
The buffers must be strings or string-alike objects, like str or bytes.\n\
It's a wrapper to jwritev().\n");

static PyObject *jf_writev(jfile_object *fp, PyObject *args)
{
	ssize_t rv;
	PyObject *buffers, *buf;
	ssize_t len, pos;
	struct iovec *iov;

	if (!PyArg_ParseTuple(args, "O:writev", &buffers))
		return NULL;

	len = PySequence_Length(buffers);
	if (len < 0) {
		PyErr_SetString(PyExc_TypeError, "iterable expected");
		return NULL;
	}

	iov = malloc(sizeof(struct iovec) * len);
	if (iov == NULL)
		return PyErr_NoMemory();

	for (pos = 0; pos < len; pos++) {
		buf = PySequence_GetItem(buffers, pos);
		if (buf == NULL)
			return NULL;

		iov[pos].iov_len = 0;
		if (!PyArg_Parse(buf, "s#:writev", &(iov[pos].iov_base),
				&(iov[pos].iov_len))) {
			free(iov);
			return NULL;
		}
	}

	Py_BEGIN_ALLOW_THREADS
	rv = jwritev(fp->fs, iov, len);
	Py_END_ALLOW_THREADS

	free(iov);

	if (rv < 0)
		return PyErr_SetFromErrno(PyExc_IOError);

	return Our_PyLong_FromSsize_t(rv);
}

/* truncate */
PyDoc_STRVAR(jf_truncate__doc,
"truncate(length)\n\
\n\
Truncate the file to the given size.\n\
It's a wrapper to jtruncate().\n");

static PyObject *jf_truncate(jfile_object *fp, PyObject *args)
{
	int rv;
	long long length;

	if (!PyArg_ParseTuple(args, "L:truncate", &length))
		return NULL;

	Py_BEGIN_ALLOW_THREADS
	rv = jtruncate(fp->fs, length);
	Py_END_ALLOW_THREADS

	if (rv != 0)
		return PyErr_SetFromErrno(PyExc_IOError);

	return PyLong_FromLongLong(rv);
}

/* lseek */
PyDoc_STRVAR(jf_lseek__doc,
"lseek(offset, whence)\n\
\n\
Reposition the file pointer to the given offset, according to the directive\n\
whence as follows:\n\
SEEK_SET    The offset is set relative to the beginning of the file.\n\
SEEK_CUR    The offset is set relative to the current position.\n\
SEEK_END    The offset is set relative to the end of the file.\n\
\n\
These constants are defined in the module. See lseek's manpage for more\n\
information.\n\
It's a wrapper to jlseek().\n");

static PyObject *jf_lseek(jfile_object *fp, PyObject *args)
{
	long long rv;
	int whence;
	long long offset;

	if (!PyArg_ParseTuple(args, "Li:lseek", &offset, &whence))
		return NULL;

	Py_BEGIN_ALLOW_THREADS
	rv = jlseek(fp->fs, offset, whence);
	Py_END_ALLOW_THREADS

	if (rv == -1)
		return PyErr_SetFromErrno(PyExc_IOError);

	return PyLong_FromLongLong(rv);
}

/* jsync */
PyDoc_STRVAR(jf_jsync__doc,
"jsync()\n\
\n\
Used with lingering transactions, see the library documentation for more\n\
detailed information.\n\
It's a wrapper to jsync().\n");

static PyObject *jf_jsync(jfile_object *fp, PyObject *args)
{
	int rv;

	if (!PyArg_ParseTuple(args, ":jsync"))
		return NULL;

	Py_BEGIN_ALLOW_THREADS
	rv = jsync(fp->fs);
	Py_END_ALLOW_THREADS

	if (rv < 0)
		return PyErr_SetFromErrno(PyExc_IOError);

	return PyLong_FromLong(rv);
}

/* jmove_journal */
PyDoc_STRVAR(jf_jmove_journal__doc,
"jmove_journal(newpath)\n\
\n\
Moves the journal directory to the new path; note that there MUST NOT BE\n\
anything else operating on the file.\n\
It's a wrapper to jmove_journal().\n");

static PyObject *jf_jmove_journal(jfile_object *fp, PyObject *args)
{
	int rv;
	char *newpath;

	if (!PyArg_ParseTuple(args, "s:jmove_journal", &newpath))
		return NULL;

	Py_BEGIN_ALLOW_THREADS
	rv = jmove_journal(fp->fs, newpath);
	Py_END_ALLOW_THREADS

	if (rv != 0)
		return PyErr_SetFromErrno(PyExc_IOError);

	return PyLong_FromLong(rv);
}

/* jfs_autosync_start() */
PyDoc_STRVAR(jf_autosync_start__doc,
"autosync_start(max_sec, max_bytes)\n\
\n\
Starts the automatic sync thread (only useful when using lingering\n\
transactions).\n");

static PyObject *jf_autosync_start(jfile_object *fp, PyObject *args)
{
	int rv;
	unsigned int max_sec, max_bytes;

	if (!PyArg_ParseTuple(args, "II:autosync_start", &max_sec,
				&max_bytes))
		return NULL;

	Py_BEGIN_ALLOW_THREADS
	rv = jfs_autosync_start(fp->fs, max_sec, max_bytes);
	Py_END_ALLOW_THREADS

	if (rv != 0)
		return PyErr_SetFromErrno(PyExc_IOError);

	return PyLong_FromLong(rv);
}

/* jfs_autosync_stop() */
PyDoc_STRVAR(jf_autosync_stop__doc,
"autosync_stop()\n\
\n\
Stops the automatic sync thread started by autosync_start()\n");

static PyObject *jf_autosync_stop(jfile_object *fp, PyObject *args)
{
	int rv;

	if (!PyArg_ParseTuple(args, ":autosync_stop"))
		return NULL;

	Py_BEGIN_ALLOW_THREADS
	rv = jfs_autosync_stop(fp->fs);
	Py_END_ALLOW_THREADS

	if (rv != 0)
		return PyErr_SetFromErrno(PyExc_IOError);

	return PyLong_FromLong(rv);
}

/* new_trans */
PyDoc_STRVAR(jf_new_trans__doc,
"new_trans()\n\
\n\
Returns an object representing a new empty transaction.\n\
It's a wrapper to jtrans_new().\n");

static PyObject *jf_new_trans(jfile_object *fp, PyObject *args)
{
	jtrans_object *tp = NULL;
	unsigned int flags = 0;

	if (!PyArg_ParseTuple(args, "|I:new_trans", &flags))
		return NULL;

#ifdef PYTHON3
	tp = (jtrans_object *) jtrans_type.tp_alloc(&jtrans_type, 0);
#elif PYTHON2
	tp = PyObject_New(jtrans_object, &jtrans_type);
#endif
	if (tp == NULL)
		return NULL;

	tp->ts = jtrans_new(fp->fs, flags);
	if(tp->ts == NULL) {
		return PyErr_NoMemory();
	}

	/* increment the reference count, it's decremented on deletion */
	tp->jfile = fp;
	Py_INCREF(fp);

	tp->views = NULL;
	tp->nviews = 0;

	return (PyObject *) tp;
}

/* method table */
static PyMethodDef jfile_methods[] = {
	{ "fileno", (PyCFunction) jf_fileno, METH_VARARGS, jf_fileno__doc },
	{ "read", (PyCFunction) jf_read, METH_VARARGS, jf_read__doc },
	{ "pread", (PyCFunction) jf_pread, METH_VARARGS, jf_pread__doc },
	{ "readv", (PyCFunction) jf_readv, METH_VARARGS, jf_readv__doc },
	{ "write", (PyCFunction) jf_write, METH_VARARGS, jf_write__doc },
	{ "pwrite", (PyCFunction) jf_pwrite, METH_VARARGS, jf_pwrite__doc },
	{ "writev", (PyCFunction) jf_writev, METH_VARARGS, jf_writev__doc },
	{ "truncate", (PyCFunction) jf_truncate, METH_VARARGS,
		jf_truncate__doc },
	{ "lseek", (PyCFunction) jf_lseek, METH_VARARGS, jf_lseek__doc },
	{ "jsync", (PyCFunction) jf_jsync, METH_VARARGS, jf_jsync__doc },
	{ "jmove_journal", (PyCFunction) jf_jmove_journal, METH_VARARGS,
		jf_jmove_journal__doc },
	{ "autosync_start", (PyCFunction) jf_autosync_start, METH_VARARGS,
		jf_autosync_start__doc },
	{ "autosync_stop", (PyCFunction) jf_autosync_stop, METH_VARARGS,
		jf_autosync_stop__doc },
	{ "new_trans", (PyCFunction) jf_new_trans, METH_VARARGS,
		jf_new_trans__doc },
	{ NULL }
};

#ifdef PYTHON3
static PyTypeObject jfile_type = {
	PyObject_HEAD_INIT(NULL)
	.tp_name = "libjio.jfile",
	.tp_itemsize = sizeof(jfile_object),
	.tp_dealloc = (destructor) jf_dealloc,
	.tp_methods = jfile_methods,
};

#elif PYTHON2
static PyObject *jf_getattr(jfile_object *fp, char *name)
{
	return Py_FindMethod(jfile_methods, (PyObject *)fp, name);
}

static PyTypeObject jfile_type = {
	PyObject_HEAD_INIT(NULL)
	0,
	"libjio.jfile",
	sizeof(jfile_object),
	0,
	(destructor)jf_dealloc,
	0,
	(getattrfunc)jf_getattr,
};

#endif


/*
 * The jtrans object
 */

/* delete */
static void jt_dealloc(jtrans_object *tp)
{
	if (tp->ts != NULL) {
		jtrans_free(tp->ts);
	}
	Py_DECREF(tp->jfile);

	/* release views allocated by add_r */
	while (tp->nviews) {
		PyBuffer_Release(tp->views[tp->nviews - 1]);
		free(tp->views[tp->nviews - 1]);
		tp->nviews--;
	}
	free(tp->views);

	PyObject_Del(tp);
}

/* add_w */
PyDoc_STRVAR(jt_add_w__doc,
"add_w(buf, offset)\n\
\n\
Add an operation to write the given buffer at the given offset to the\n\
transaction.\n\
It's a wrapper to jtrans_add_w().\n");

static PyObject *jt_add_w(jtrans_object *tp, PyObject *args)
{
	int rv;
	ssize_t len;
	long long offset;
	unsigned char *buf;

	if (!PyArg_ParseTuple(args, "s#L:add_w", &buf, &len, &offset))
		return NULL;

	if (offset < 0) {
		PyErr_SetString(PyExc_TypeError, "offset must be >= 0");
		return NULL;
	}

	rv = jtrans_add_w(tp->ts, buf, len, offset);
	if (rv < 0)
		return PyErr_SetFromErrno(PyExc_IOError);

	return PyLong_FromLong(rv);
}

/* add_r */
PyDoc_STRVAR(jt_add_r__doc,
"add_r(buf, offset)\n\
\n\
Add an operation to read into the given buffer at the given offset to the\n\
transaction.\n\
It's a wrapper to jtrans_add_r().\n\
\n\
The buffer must be objects that support slice assignment, like bytearray\n\
(but *not* str).\n\
Only available in Python >= 2.6.\n");

/* add_r requires the new Py_buffer interface, which is only available in
 * Python >= 2.6 */
#if PY_MAJOR_VERSION >= 3 || (PY_MAJOR_VERSION == 2 && PY_MINOR_VERSION >= 6)
static PyObject *jt_add_r(jtrans_object *tp, PyObject *args)
{
	int rv;
	PyObject *py_buf;
	long long offset;
	Py_buffer *view = NULL, **new_views;

	if (!PyArg_ParseTuple(args, "OL:add_r", &py_buf, &offset))
		return NULL;

	if (!PyObject_CheckBuffer(py_buf)) {
		PyErr_SetString(PyExc_TypeError,
			"object must support the buffer interface");
		return NULL;
	}

	if (offset < 0) {
		PyErr_SetString(PyExc_TypeError, "offset must be >= 0");
		return NULL;
	}

	view = malloc(sizeof(Py_buffer));
	if (view == NULL)
		return PyErr_NoMemory();

	if (PyObject_GetBuffer(py_buf, view, PyBUF_CONTIG)) {
		free(view);
		return NULL;
	}

	Py_BEGIN_ALLOW_THREADS
	rv = jtrans_add_r(tp->ts, view->buf, view->len, offset);
	Py_END_ALLOW_THREADS

	if (rv < 0) {
		PyBuffer_Release(view);
		free(view);
		return PyErr_SetFromErrno(PyExc_IOError);
	}

	new_views = realloc(tp->views, sizeof(Py_buffer *) * (tp->nviews + 1));
	if (new_views == NULL) {
		PyBuffer_Release(view);
		free(view);
		return PyErr_NoMemory();
	}

	tp->nviews++;
	tp->views = new_views;
	tp->views[tp->nviews - 1] = view;

	return PyLong_FromLong(rv);
}

#else

static PyObject *jt_add_r(jtrans_object *tp, PyObject *args)
{
	PyErr_SetString(PyExc_NotImplementedError,
			"only supported in Python >= 2.6");
	return NULL;
}

#endif /* python version >= 2.6 */


/* commit */
PyDoc_STRVAR(jt_commit__doc,
"commit()\n\
\n\
Commits a transaction.\n\
It's a wrapper to jtrans_commit().\n");

static PyObject *jt_commit(jtrans_object *tp, PyObject *args)
{
	ssize_t rv;

	if (!PyArg_ParseTuple(args, ":commit"))
		return NULL;

	Py_BEGIN_ALLOW_THREADS
	rv = jtrans_commit(tp->ts);
	Py_END_ALLOW_THREADS

	if (rv < 0)
		return PyErr_SetFromErrno(PyExc_IOError);

	return Our_PyLong_FromSsize_t(rv);
}

/* rollback */
PyDoc_STRVAR(jt_rollback__doc,
"rollback()\n\
\n\
Rollbacks a transaction.\n\
It's a wrapper to jtrans_rollback().\n");

static PyObject *jt_rollback(jtrans_object *tp, PyObject *args)
{
	ssize_t rv;

	if (!PyArg_ParseTuple(args, ":rollback"))
		return NULL;

	Py_BEGIN_ALLOW_THREADS
	rv = jtrans_rollback(tp->ts);
	Py_END_ALLOW_THREADS

	if (rv < 0)
		return PyErr_SetFromErrno(PyExc_IOError);

	return Our_PyLong_FromSsize_t(rv);
}

/* method table */
static PyMethodDef jtrans_methods[] = {
	{ "add_r", (PyCFunction) jt_add_r, METH_VARARGS, jt_add_r__doc },
	{ "add_w", (PyCFunction) jt_add_w, METH_VARARGS, jt_add_w__doc },
	{ "commit", (PyCFunction) jt_commit, METH_VARARGS, jt_commit__doc },
	{ "rollback", (PyCFunction) jt_rollback, METH_VARARGS, jt_rollback__doc },
	{ NULL }
};

#ifdef PYTHON3
static PyTypeObject jtrans_type = {
	PyObject_HEAD_INIT(NULL)
	.tp_name = "libjio.jtrans",
	.tp_itemsize = sizeof(jtrans_object),
	.tp_dealloc = (destructor) jt_dealloc,
	.tp_methods = jtrans_methods,
};

#elif PYTHON2
static PyObject *jt_getattr(jtrans_object *tp, char *name)
{
	return Py_FindMethod(jtrans_methods, (PyObject *)tp, name);
}

static PyTypeObject jtrans_type = {
	PyObject_HEAD_INIT(NULL)
	0,
	"libjio.jtrans",
	sizeof(jtrans_object),
	0,
	(destructor)jt_dealloc,
	0,
	(getattrfunc)jt_getattr,
};

#endif


/*
 * The module
 */

/* open */
PyDoc_STRVAR(jf_open__doc,
"open(name[, flags[, mode[, jflags]]])\n\
\n\
Opens a file, returns a file object.\n\
The arguments flags, mode and jflags are the same as jopen(); the constants\n\
needed are defined in the module.\n\
It's a wrapper to jopen().\n");

static PyObject *jf_open(PyObject *self, PyObject *args)
{
	char *file;
	int flags = O_RDONLY;
	int mode = 0600;
	unsigned int jflags = 0;
	jfile_object *fp = NULL;

	flags = O_RDWR;
	mode = 0600;
	jflags = 0;

	if (!PyArg_ParseTuple(args, "s|iiI:open", &file, &flags, &mode,
				&jflags))
		return NULL;

#ifdef PYTHON3
	fp = (jfile_object *) jfile_type.tp_alloc(&jfile_type, 0);
#elif PYTHON2
	fp = PyObject_New(jfile_object, &jfile_type);
#endif

	if (fp == NULL)
		return NULL;

	fp->fs = jopen(file, flags, mode, jflags);
	if (fp->fs == NULL) {
		return PyErr_SetFromErrno(PyExc_IOError);
	}

	if (PyErr_Occurred()) {
		jclose(fp->fs);
		return NULL;
	}

	return (PyObject *) fp;
}

/* jfsck */
PyDoc_STRVAR(jf_jfsck__doc,
"jfsck(name[, jdir] [, flags])\n\
\n\
Checks the integrity of the file with the given name, using (optionally) jdir\n\
as the journal directory and the given flags; returns a dictionary with all\n\
the different values of the check (equivalent to the 'struct jfsck_result').\n\
If the path is incorrect, or there is no journal associated with it, an\n\
IOError will be raised.\n\
It's a wrapper to jfsck().\n");

static PyObject *jf_jfsck(PyObject *self, PyObject *args, PyObject *kw)
{
	int rv;
	unsigned int flags = 0;
	char *name, *jdir = NULL;
	struct jfsck_result res;
	PyObject *dict;
	char *keywords[] = { "name", "jdir", "flags", NULL };

	if (!PyArg_ParseTupleAndKeywords(args, kw, "s|sI:jfsck",
				keywords, &name, &jdir, &flags))
		return NULL;

	dict = PyDict_New();
	if (dict == NULL)
		return PyErr_NoMemory();

	Py_BEGIN_ALLOW_THREADS
	rv = jfsck(name, jdir, &res, flags);
	Py_END_ALLOW_THREADS

	if (rv == J_ENOMEM) {
		Py_XDECREF(dict);
		return PyErr_NoMemory();
	} else if (rv != 0) {
		Py_XDECREF(dict);
		PyErr_SetObject(PyExc_IOError, PyLong_FromLong(rv));
		return NULL;
	}

	PyDict_SetItemString(dict, "total", PyLong_FromLong(res.total));
	PyDict_SetItemString(dict, "invalid", PyLong_FromLong(res.invalid));
	PyDict_SetItemString(dict, "in_progress", PyLong_FromLong(res.in_progress));
	PyDict_SetItemString(dict, "broken", PyLong_FromLong(res.broken));
	PyDict_SetItemString(dict, "corrupt", PyLong_FromLong(res.corrupt));
	PyDict_SetItemString(dict, "reapplied", PyLong_FromLong(res.reapplied));

	return dict;
}

static PyMethodDef module_methods[] = {
	{ "open", jf_open, METH_VARARGS, jf_open__doc },
	{ "jfsck", (PyCFunction) jf_jfsck, METH_VARARGS | METH_KEYWORDS,
		jf_jfsck__doc },
	{ NULL, NULL, 0, NULL },
};

#define module_doc "libjio is a library to do transactional, journaled I/O\n" \
	"You can find it at http://blitiri.com.ar/p/libjio/\n" \
	"\n" \
	"Use the open() method to create a file object, and then operate " \
	"on it.\n" \
	"Please read the documentation for more information.\n"


/* fills the module with the objects and constants */
static void populate_module(PyObject *m)
{
	Py_INCREF(&jfile_type);
	PyModule_AddObject(m, "jfile", (PyObject *) &jfile_type);

	Py_INCREF(&jtrans_type);
	PyModule_AddObject(m, "jtrans", (PyObject *) &jtrans_type);

	/* libjio's constants */
	PyModule_AddIntConstant(m, "J_NOLOCK", J_NOLOCK);
	PyModule_AddIntConstant(m, "J_NOROLLBACK", J_NOROLLBACK);
	PyModule_AddIntConstant(m, "J_LINGER", J_LINGER);
	PyModule_AddIntConstant(m, "J_COMMITTED", J_COMMITTED);
	PyModule_AddIntConstant(m, "J_ROLLBACKED", J_ROLLBACKED);
	PyModule_AddIntConstant(m, "J_ROLLBACKING", J_ROLLBACKING);
	PyModule_AddIntConstant(m, "J_RDONLY", J_RDONLY);

	/* enum jfsck_return */
	PyModule_AddIntConstant(m, "J_ESUCCESS", J_ESUCCESS);
	PyModule_AddIntConstant(m, "J_ENOENT", J_ENOENT);
	PyModule_AddIntConstant(m, "J_ENOJOURNAL", J_ENOJOURNAL);
	PyModule_AddIntConstant(m, "J_ENOMEM", J_ENOMEM);
	PyModule_AddIntConstant(m, "J_ECLEANUP", J_ECLEANUP);
	PyModule_AddIntConstant(m, "J_EIO", J_EIO);

	/* jfsck() flags */
	PyModule_AddIntConstant(m, "J_CLEANUP", J_CLEANUP);

	/* open constants (at least the POSIX ones) */
	PyModule_AddIntConstant(m, "O_RDONLY", O_RDONLY);
	PyModule_AddIntConstant(m, "O_WRONLY", O_WRONLY);
	PyModule_AddIntConstant(m, "O_RDWR", O_RDWR);
	PyModule_AddIntConstant(m, "O_CREAT", O_CREAT);
	PyModule_AddIntConstant(m, "O_EXCL", O_EXCL);
	PyModule_AddIntConstant(m, "O_TRUNC", O_TRUNC);
	PyModule_AddIntConstant(m, "O_APPEND", O_APPEND);
	PyModule_AddIntConstant(m, "O_NONBLOCK", O_NONBLOCK);
	PyModule_AddIntConstant(m, "O_NDELAY", O_NDELAY);
	PyModule_AddIntConstant(m, "O_SYNC", O_SYNC);
	PyModule_AddIntConstant(m, "O_ASYNC", O_ASYNC);

	/* lseek constants */
	PyModule_AddIntConstant(m, "SEEK_SET", SEEK_SET);
	PyModule_AddIntConstant(m, "SEEK_CUR", SEEK_CUR);
	PyModule_AddIntConstant(m, "SEEK_END", SEEK_END);
}


#ifdef PYTHON3
static PyModuleDef libjio_module = {
	PyModuleDef_HEAD_INIT,
	.m_name = "libjio",
	.m_doc = module_doc,
	.m_size = -1,
	.m_methods = module_methods,
};

PyMODINIT_FUNC PyInit_libjio(void)
{
	PyObject *m;

	if (PyType_Ready(&jfile_type) < 0 ||
			PyType_Ready(&jtrans_type) < 0)
		return NULL;

	m = PyModule_Create(&libjio_module);

	populate_module(m);

	return m;
}

#elif PYTHON2
PyMODINIT_FUNC initlibjio(void)
{
	PyObject* m;

	jfile_type.ob_type = &PyType_Type;
	jtrans_type.ob_type = &PyType_Type;

	m = Py_InitModule3("libjio", module_methods, module_doc);

	populate_module(m);
}

#endif



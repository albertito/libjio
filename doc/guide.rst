
libjio Programmer's Guide
=========================

Introduction
------------

This small document attempts to serve as a guide to the programmer who wants
to use the library. It's not a replacement for the man page or reading the
code, but is a good starting point for everyone who wants to get involved with
it.

The library is not complex to use at all, and the interfaces were designed to
be as intuitive as possible, so the text is structured as a guide to present
the reader all the common structures and functions the way they're normally
used.


Definitions
-----------

This is a library which provides a transaction-oriented I/O API.

We say this is a transaction-oriented API because we make transactions the
center of our operations, and journaled because we use a journal (which takes
the form of a directory with files on it) to guarantee coherency even after a
crash at any point.

In this document, we think of a transaction as a list of *(buffer, length,
offset)* to be written to a file. That triplet is called an *operation*, so we
can say that a transaction represents an ordered group of operations on the
same file.

The act of *committing* a transaction means writing all the elements of that
list; and *rolling back* means to undo a previous commit, and leave the data
just as it was before doing the commit.

The library provides several guarantees, the most relevant and useful being
that at any point of time, even if the machine crash horribly, a transaction
will be either fully applied or not applied at all.

To achieve this, the library uses what is called a journal, a very vague and
fashionable term we use to describe a set of auxiliary files that get created
to store temporary data at several stages. The proper definition and how we
use them is outside the scope of this document, and you as a programmer
shouldn't need to deal with it. In case you're curious, it's described in a
bit more detail in another text which talks about how the library works
internally.


The data types
--------------

libjio has two basic opaque types which have a very strong relationship, and
represent the essential objects it deals with. Note that you don't manipulate
them directly, but use them through the API.

The first is *jfs_t*, usually called the file structure, and it represents an
open file, just like a regular file descriptor or a *FILE **.

Then second is *jtrans_t*, usually called the transaction structure, which
represents a single transaction.


Basic operation
---------------

First of all, as with regular I/O, you need to open your files. This is done
with *jopen()*, which looks a lot like *open()* but returns a file structure
instead of a file descriptor (this will be very common among all the
functions), and adds a new parameter *jflags* that can be used to modify some
library behaviour we'll see later, and is normally not used.

Now that you have opened a file, the next thing to do would be to create a
transaction. This is what *jtrans_new()* is for: it takes a file structure and
returns a new transaction structure.

To add a write operation to the transaction, use *jtrans_add_w()*. You can add
as many operations as you want. Operations within a transaction may overlap,
and will be applied in order.

Finally, to apply our transaction to the file, use *jtrans_commit()*.

When you're done using the file, call *jclose()*.

Let's put it all together and code a nice "hello world" program (return values
are ignored for simplicity)::

  char buf[] = "Hello world!";
  jfs_t *file;
  jtrans_t *trans;

  file = jopen("filename", O_RDWR | O_CREAT, 0600, 0);

  trans = jtrans_new(file, 0);
  jtrans_add_w(trans, buf, strlen(buf), 0);
  jtrans_commit(trans);
  jtrans_free(trans);

  jclose(file);

As we've seen, you open the file and initialize the structure with *jopen()*
(with the parameter *jflags* being the last 0), create a new transaction with
*jtrans_new()*, then add an operation with *jtrans_add_w()* (the last 0 is the
offset, in this case the beginning of the file), commit the transaction with
*jtrans_commit()*, free it with *jtrans_free()*, and finally close the file
with *jclose()*.

Reading is much easier: the library provides three functions, *jread()*,
*jpread()* and *jreadv()*, that behave exactly like *read()*, *pread()* and
*readv()*, except that they play safe with libjio's writing code. You should
use these to read from files when using libjio.

You can also add read operations to a transaction using *jtrans_add_r()*, and
the data will be read atomically at commit time.


Integrity checking and recovery
-------------------------------

An essential part of the library is taking care of recovering from crashes and
be able to assure a file is consistent. When you're working with the file,
this is taking care of; but what about when you first open it? To answer that
question, the library provides you with a function named *jfsck()*, which
checks the integrity of a file and makes sure that everything is consistent.

It must be called "offline", that is when you are not actively committing and
rollbacking; it is normally done before calling *jopen()* and is **very, very
important**.

You can also do this manually with an utility named jiofsck, which can be used
from the shell to perform the checking.


Rollback
--------

There is a very nice and important feature in transactions, that allows them
to be "undone", which means that you can undo a transaction and leave the file
just as it was the moment before applying it. The action of undoing it is
called *rollback*, and the function is called *jtrans_rollback()*, which takes
the transaction as the only parameter.

Be aware that rollbacking a transaction can be dangerous if you're not careful
and cause you a lot of troubles. For instance, consider you have two
transactions (let's call them 1 and 2, and assume they were applied in that
order) that modify the same offset, and you rollback transaction 1; then 2
would be lost. It is not an dangerous operation itself, but its use requires
care and thought.


UNIX-alike API
--------------

There is a set of functions that emulate the UNIX API (*read()*, *write()*,
and so on) which make each operation a transaction. This can be useful if you
don't need to have the full power of the transactions but only to provide
guarantees between the different functions. They are a lot like the normal
UNIX functions, but instead of getting a file descriptor as their first
parameter they get a file structure. You can check out the manual page to see
the details, but they work just like their UNIX version, only that they
preserve atomicity and thread-safety within each call.

In particular, the group of functions related to reading (which was described
above in `Basic operation`_) are extremely useful because they take care of
the locking needed for the library proper behaviour. You should use them
instead of the regular calls.

The full function list is available on the man page and I won't reproduce it
here; however the naming is quite simple: just prepend a 'j' to all the names:
*jread()*, *jwrite()*, etc.


Processes, threads and locking
------------------------------

The library is completely safe to use in multi-process and/or multi-thread
applications, as long as you abide by the following rules:

 - Within a process, a file must not be held open at the same time more than
   once, due to *fcntl()* locking limitations. Opening, closing and then
   opening again is safe.
 - *jclose()* must only be called when there are no other I/O operations in
   progress.
 - *jfsck()* must only be called when the file is known **not** to be open by
   any process.
 - *jmove_journal()* must only be called when the file is known **not** to be
   open by any other processes.

All other operations (committing a transaction, rolling it back, adding
operations, etc.) and all the wrappers are safe and don't require any special
considerations.


Lingering transactions
----------------------

If you need to increase performance, you can use lingering transactions. In
this mode, transactions take up more disk space but allows you to do the
synchronous write only once, making commits much faster. To use them, just add
*J_LINGER* to the *jflags* parameter in *jopen()*. You should call *jsync()*
frequently to avoid using up too much space, or start an asynchronous thread
that calls *jsync()* automatically using *jfs_autosync_start()*. Note that
files opened with this mode must not be opened by more than one process at the
same time.


Disk layout
-----------

The library creates a single directory for each file opened, named after it.
So if we open a file *output*, a directory named *.output.jio* will be
created. We call it the journal directory, and it's used internally by the
library to save temporary data; **you shouldn't modify any of the files that
are inside it, nor move it while it's in use**.

It doesn't grow much (it only uses space for transactions that are in the
process of committing) and gets automatically cleaned while working with it so
you can (and should) ignore it. Besides that, the file you work with has no
special modification and is just like any other file, all the internal stuff
is kept isolated on the journal directory.


ANSI C alike API
----------------

Besides the UNIX-alike API you can find an ANSI C alike API, which emulates
the traditional *fread()*, *fwrite()*, etc. It's still in development and has
not been tested carefully, so I won't spend time documenting them. Let me know
if you need them.


Compiling and linking
---------------------

If you have *pkg-config* in your build environment, then you can get the build
flags you need to use when building and linking against the library by
running::

  pkg-config --cflags --libs libjio

If *pkg-config* is not available, you have to make sure your application uses
the Large File Support (*"LFS"* from now on), to be able to handle large files
properly. This means that you will have to pass some special standard flags to
the compiler, so your C library uses the same data types as the library. For
instance, on 32-bit platforms (like x86), when using LFS, offsets are usually
64 bits, as opposed to the usual 32.

The library is always built with LFS; however, linking it against an
application without LFS support could lead to serious problems because this
kind of size differences and ABI compatibility.

The Single Unix Specification standard proposes a simple and practical way to
get the flags you need to pass your C compiler to tell you want to compile
your application with LFS: use a program called "getconf" which should be
called like "getconf LFS_CFLAGS", and it outputs the appropiate parameters.

In the end, the command line would be something like::

  gcc `getconf LFS_CFLAGS` app.c -ljio -o app

If you want more detailed information or examples, you can check out how the
library and sample applications get built.


Where to go from here
---------------------

If you're still interested in learning more, you can find some small and clean
samples are in the *samples* directory (*full.c* is a simple and complete
one), other more advanced examples can be found in the web page, as well as
modifications to well known software to make use of the library. For more
information about the inner workings of the library, you can read the "libjio"
document, the internal API reference, and the source code.


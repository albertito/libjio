
libjio Programmer's Guide
=========================

Introduction
------------

This small document attempts serve as a guide to the programmer who wants to
make use of the library. It's not a replacement for the man page or reading
the code; but it's a good starting point for everyone who wants to get
involved with it.

The library is not complex to use at all, and the interfaces were designed to
be as intuitive as possible, so the text is structured as a guide to present
the reader all the common structures and functions the way they're normally
used.

Definitions
-----------

This is a library which provides a journaled, transaction-oriented I/O API.
You've probably read this a hundred times already in the documents, and if you
haven't wondered yet what on earth does, this mean you should be reading
something else!

We say this is a transaction-oriented API because we make transactions the
center of our operations, and journaled because we use a journal (which takes
the form of a directory with files on it) to guarantee coherency even after a
crash at any point.

In this document, we think of a transaction as a list of *(buffer, length,
offset)* to be applied to a file. That triplet is called an *operation*, so we
can say that a transaction represent an ordered group of operations on the
same file.

The act of *committing* a transaction means writing all the elements of the
list; and rollbacking means to undo a previous commit, and leave the data just
as it was before doing the commit. While all this definitions may seem obvious
to some people, it requires special attention because there are a lot of
different definitions, and it's not that common to see "transaction" applied
to file I/O, because it's a term used mostly on database stuff.

The library provides several guarantees, the most relevant and useful being
that at any point of time, even if the machine crash horribly, a transaction
will be either fully applied or not applied at all.

To achieve this, the library uses what is called a journal, a very vague (and
fashionable) term we use to describe a set of auxiliary files that get created
to store temporary data at several stages. The proper definition and how we
use them is outside the scope of this document, and you as a programmer
shouldn't need to deal with it. In case you're curious, it's described in a
bit more detail in another text which talks about how the library works
internally. Now let's get real.


The data types
--------------

To understand any library, it's essential to be confident in the knowledge of
their data structures and how they relate each other. libjio has two basic
structures which have a very strong relationship, and represent the essential
objects it deals with. Note that you normally don't manipulate them directly,
because they have their own initializer functions, but they are the building
blocks for the rest of the text, which, once this is understood, should be
obvious and self-evident.

The first structure we face is *struct jfs*, usually called the file
structure, and it represents an open file, just like a regular file descriptor
or a FILE \*.

Then you find *struct jtrans*, usually called the transaction structure, which
represents a single transaction.


Basic operation
---------------

Now that we've described our data types, let's see how we can operate with the
library.

First of all, as with regular I/O, you need to open your files. This is done
with *jopen()*, which looks a lot like *open()* but takes a file structure
instead of a file descriptor (this will be very common among all the
functions), and adds a new parameter *jflags* that can be used to modify some
subtle library behaviour we'll see later, and is normally not used.

We have a happy file structure open now, and the next thing to do would be to
create a transaction. This is what *jtrans_init()* is for: it takes a file
structure and a transaction structure and initializes the latter, leaving it
ready to use.

Now that we have our transaction, let's add a write operation to it; to do
this we use *jtrans_add()*. We could keep on adding operations to the
transaction by keep on calling jtrans_add() as many times as we want.
Operations within a transaction may overlap, and will be applied in order.

Finally, we decide to apply our transaction to the file, that is, write all
the operations we've added. And this is the easiest part: we call
*jtrans_commit()*, and that's it!

When we're done using the file, we call *jclose()*, just like we would call
*close()*.

Let's put it all together and code a nice "hello world"
program (return values are ignored for simplicity)::

  char buf[] = "Hello world!";
  struct jfs file;
  struct jtrans trans;

  jopen(&file, "filename", O_RDWR | O_CREAT, 0600, 0);

  jtrans_init(&file, &trans);
  jtrans_add(&trans, buf, strlen(buf), 0);
  jtrans_commit(&trans);

  jclose(&file);

As we've seen, we open the file and initialize the structure with *jopen()*
(with the parameter *jflags* being the last 0), create a new transaction with
*jtrans_init()*, then add an operation with *jtrans_add()* (the last 0 is the
offset, in this case the beginning of the file), commit the transaction with
*jtrans_commit()*, and finally close the file with *jclose()*.

Reading is much easier: the library provides three functions, *jread()*,
*jpread()* and *jreadv()*, that behave exactly like *read()*, *pread()* and
*readv()*, except that they play safe with libjio's writing code. You should
use these to read from files you're writing with libjio.


Integrity checking and recovery
-------------------------------

An essential part of the library is taking care of recovering from crashes and
be able to assure a file is consistent. When you're working with the file,
this is taking care of; but what when you first open it? To answer that
question, the library provides you with a function named *jfsck()*, which
checks the integrity of a file and makes sure that everything is consistent.

It must be called "offline", that is when you are not actively committing and
rollbacking; it is normally done before calling *jopen()* and is **very, very
important**. Another good practise is to call *jfsck_cleanup()* after calling
*jfsck()*, to make sure we're starting up with a fresh clean journal. After
both calls, it is safe to assume that the file is and ready to use.

You can also do this manually with an utility named jiofsck, which can be used
from the shell to perform the checking and cleanup.


Rollback
--------

There is a very nice and important feature in transactions, that allow them to
be "undone", which means that you can undo a transaction and leave the file
just as it was the moment before applying it. The action of undoing it is
called to rollback, and the function is called jtrans_rollback(), which takes
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
parameter, they get a file structure. You can check out the manual page to see
the details, but they work just like their UNIX version, only that they
preserve atomicity and thread-safety within each call.

In particular, the group of functions related to reading (which was described
above in `Basic operation`_) are extremely useful because they take care of
the locking needed for the library proper behaviour. You should use them
instead of the regular calls.

The full function list is available on the man page and I won't reproduce it
here; however the naming is quite simple: just prepend a 'j' to all the names:
*jread()*, *jwrite()*, etc.


Threads and locking
-------------------

The library is completely safe to use in multithreaded applications; however,
there are some very basic and intuitive locking rules you have to bear in
mind.

Most is fully threadsafe so you don't need to worry about concurrency; in
fact, a lot of effort has been put in making parallel operation safe and fast.

You need to care only when opening, closing and checking for integrity. In
practise, that means that you shouldn't call *jopen()*, *jclose()* in parallel
with the same jfs structure, or in the middle of an I/O operation, just like
you do when using the normal UNIX calls. In the case of *jfsck()*, you
shouldn't invoke it for the same file more than once at the time; while it
will cope with that situation, it's not recommended.

All other operations (commiting a transaction, rollbacking it, adding
operations, etc.) and all the wrappers are safe and don't require any special
considerations.


Lingering transactions
----------------------

If you need to increase performance, you can use lingering transactions. In
this mode, transactions take up more disk space but allows you to do the
synchronous write only once, making commits much faster. To use them, just add
*J_LINGER* to the *jflags* parameter in *jopen()*. You should call *jsync()*
frequently to avoid using up too much space.


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

When you want to use your library, besides including the "libjio.h" header,
you have to make sure your application uses the Large File Support ("LFS" from
now on), to be able to handle large files properly. This means that you will
have to pass some special standard flags to the compiler, so your C library
uses the same data types as the library. For instance, on 32-bit platforms
(like x86), when using LFS, offsets are usually 64 bits, as opposed to the
usual 32.

The library is always built with LFS; however, link it against an application
without LFS support could lead to serious problems because this kind of size
differences and ABI compatibility.

The Single Unix Specification standard proposes a simple and practical way to
get the flags you need to pass your C compiler to tell you want to compile
your application with LFS: use a program called "getconf" which should be
called like "getconf LFS_CFLAGS", and it outputs the appropiate parameters.
Sadly, not all platforms implement it, so it's also wise to pass
"-D_FILE_OFFSET_BITS=64" just in case.

In the end, the command line would be something like::

  gcc `getconf LFS_CFLAGS` -D_FILE_OFFSET_BITS=64 app.c -ljio -o app

If you want more detailed information or examples, you can check out how the
library and sample applications get built.


Where to go from here
---------------------

If you're still interested in learning more, you can find some small and clean
samples are in the "samples" directory (full.c is a simple and complete one),
other more advanced examples can be found in the web page, as well as
modifications to well known software to make use of the library. For more
information about the inner workings of the library, you can read the "libjio"
document, and the source code.


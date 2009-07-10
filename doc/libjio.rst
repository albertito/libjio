
libjio - A library for journaled I/O
======================================

Introduction
------------

libjio is a library for doing journaled, transaction-oriented I/O, providing
atomicity warantees and a simple to use but powerful API.

This document explains the design of the library, how it works internally and
why it works that way. You should read it even if you don't plan to do use the
library in strange ways, it provides (or at least tries to) an insight view on
how the library performs its job, which can be very valuable knowledge when
working with it. It assumes that there is some basic knowledge about how the
library is used, which can be found in the manpage or in the programmer's
guide.

To the user, libjio provides two groups of functions, one UNIX-alike that
implements the journaled versions of the classic functions (open(), read(),
write() and friends); and a lower-level one that center on transactions and
allows the user to manipulate them directly by providing means of commiting
and rollbacking. The former, as expected, are based on the latter and interact
safely with them. Besides, it's designed in a way that allows efficient and
safe interaction with I/O performed from outside the library in case you want
to.

The following sections describe different concepts and procedures that the
library bases its work on. It's not intended to be a replace to reading the
source code: please do so if you have any doubts, it's not big at all (less
than 1500 lines, including comments) and I hope it's readable enough. If you
think that's not the case, please let me know and I'll try to give you a hand.


General on-disk data organization
---------------------------------

On the disk, the file you are working on will look exactly as you expect and
hasn't got a single bit different that what you would get using the regular
UNIX API. But, besides the working file, you will find a directory named after
it where the journaling information lives.

Inside, there are two kind of files: the lock file and transaction files. The
first one is used as a general lock and holds the next transaction ID to
assign, and there is only one; the second one holds one transaction, which is
composed by a header of fixed size and a variable-size payload, and can be as
many as in-flight transactions.

This impose some restrictions to the kind of operations you can perform over a
file while it's currently being used: you can't move it (because the journal
directory name depends on the filename) and you can't unlink it (for similar
reasons).

This warnings are no different from a normal simultaneous use under classic
UNIX environments, but they are here to remind you that even tho the library
warranties a lot and eases many things from its user, you should still be
careful when doing strange things with files while working on them.

The transaction file
~~~~~~~~~~~~~~~~~~~~

The transaction file is composed of three main parts: the header, the
operations, and the trailer.

The header holds basic information about the transaction itself, including the
version, the transaction ID, and its flags.

Then the operation part has all the operations one after the other, prepending
the operation data with a per-operation header that includes the length of the
data and the offset of the file where it should be applied, and then the data
itself.

Finally, the trailer contains the number of operations included in it and a
checksum of the whole file. Both fields are used to detect broken or corrupted
transactions.

The commit procedure
--------------------

We call *commit* to the action of safely and atomically write some given data
to the disk.

The former, "safely", means that after a commit has been done we can assume
the data will not get lost and can be retrieved, unless of course some major
event happens (like a physical hard disk crash). For us, this means that the
data was effectively written to the disk and if a crash occurs after the
commit operation has returned, the operation will be complete and data will be
available from the file.

The latter, "atomically", guarantees that the operation is either completely
done, or not done at all. This is a really common word, specially if you have
worked with multiprocessing, and should be quite familiar. We implement
atomicity by combining fine-grained locks and journaling, which can assure us
both to be able to recover from crashes, and to have exclusive access to a
portion of the file without having any other transaction overlap it.

Well, so much for talking, now let's get real; libjio applies commits in a
very simple and straightforward way, inside jtrans_commit():

 - Lock the file offsets where the commit takes place
 - Open the transaction file
 - Write the header
 - Read all the previous data from the file
 - Write the previous data in the transaction
 - Write the data to the file
 - Mark the transaction as committed by setting a flag in the header
 - Unlink the transaction file
 - Unlock the offsets where the commit takes place

This may seem like a lot of steps, but they're not as much as it looks like
inside the code, and allows a recovery from interruptions in every step of the
way, and even in the middle of a step.


The rollback procedure
----------------------

First of all, rollbacking is like "undo" a commit: returns the data to the
state it had exactly before a given commit was applied. Due to the way we
handle commits, doing this operation becomes quite simple and straightforward.

In the previous section we said that each transaction held the data that was
on it before commiting. That data saved is precisely the one we need to be
able to rollback.

So, to rollback a transaction all that has to be done is recover the
previous data from the transaction we want to rollback, and save it to the
disk. In the end, this ends up being a new transaction with the previous data
as the new one, and that's how it's done: create a new transaction structure,
fill in the data from the transaction we want to rollback, and commit it. All
this is performed by jtrans_rollback().

By doing this we can provide the same warranties a commit has, it's really
fast, eases the recovery, and the code is simple and clean. What a deal.

But be aware that rollbacking is dangerous. And I really mean it: you should
only do it if you're really sure it's ok. Consider, for instance, that you
commit transaction A, then B, and then you rollback A. If A and B happen to
touch the same portion of the file, the rollback will, of course, not return
the state previous to B, but previous to A.

If it's not done safely, this can lead to major corruption. Now, if you add to
this transactions that extend the file (and thus rollbacking truncates it
back), it gets even worse. So, again, be aware, I can't stress this enough,
rollback only if you really really know what you are doing.


The recovery procedure
----------------------

Recovering from crashes is done by the jfsck() call (or the program *jiofsck*
which is just a simple invocation to that function), which opens the file and
goes through all transactions in the journal (remember that transactions are
removed from the journal directory after they're applied), loading and
rollbacking them if necessary. There are several steps where it can fail:
there could be no journal, a given transaction file might be corrupted,
incomplete, and so on; but in the end, there are two cases regarding each
transaction: either it's complete and can be rollbacked, or not.

In the case the transaction file was not completely written, there is no
possibility that it has been partially applied to the disk: remember that,
from the commit procedure, we only apply the transaction after saving it in
the journal, so there is really nothing left to be done. So if the transaction
is complete, we only need to rollback.


UNIX-alike API
--------------

We call UNIX-alike API to the functions provided by the library that emulate
the good old UNIX file manipulation calls. Most of them are just wrappers
around commits, and implement proper locking when operating in order to allow
simultaneous operations (either across threads or processes). They are
described in detail in the manual pages, we'll only list them here for
completion:

 - jopen()
 - jread(), jpread(), jreadv()
 - jwrite(), jpwrite(), jwritev()
 - jtruncate()
 - jclose()


ACID warranties
---------------

Database people like ACID (well, that's not news for anybody), which they say
mean "Atomicity, Consistency, Isolation, Durability".

So, even when libjio is not a purely database thing, its transactions provide
those properties. Let's take a look one by one:

Atomicity
  In a transaction involving two or more discrete pieces of information,
  either all of the pieces are committed or none are. This has been talked
  before and we've seen how the library achieves this point, mostly based on
  locks and relying on a commit procedure.

Consistency
  A transaction either creates a new and valid state of data, or, if any
  failure occurs, returns all data to its state before the transaction was
  started. This, like atomicity, has been discussed before, specially in the
  recovery section, when we saw how in case of a crash we end up with a fully
  applied transaction, or no transaction applied at all.

Isolation
  A transaction in process and not yet committed must remain isolated from any
  other transaction. This comes as a side effect of doing proper locking on
  the sections each transaction affect, and guarantees that there can't be two
  transactions working on the same section at the same time.

Durability
  Committed data is saved by the system such that, even in the event of a
  failure, the data is available in a correct state. To provide this, libjio
  relies on the disk as a method of permanent storage, and expects that when
  it does syncronous I/O, data is safely written and can be recovered after a
  crash.


Working from outside
--------------------

If you want, and are careful enough, you can safely use the library and still
do I/O using the regular UNIX calls.

This section provides some general guidelines that you need to follow in order
to prevent corruption. Of course you can bend or break them according to your
use, this is just a general overview on how to interact from outside.

 - Lock the sections you want to use: the library, as we have already exposed,
   relies on fcntl() locking; so, if you intend to operate on parts on the
   file while using it, you should lock them.
 - Don't truncate, unlink or rename: these operations have serious
   implications when they're done while using the library, because the library
   itself assumes that names don't change, and files don't disappear from
   underneath it. It could potentially lead to corruption, although most of
   the time you would just get errors from every call.



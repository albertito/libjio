Transaction ID assignment procedure
===================================

This brief document describes how libjio assigns an unique number to each
transaction that identifies it univocally during its lifetime.

It is a very delicate issue, because the rest of the library depends on the
uniqueness of the ID. An ID has to be coherent across threads and procesess,
and choosing one it can't take long: it serializes transaction creation (and
it's the only contention point for independent non-overlapping transactions).


Description
-----------

We have two functions: *get_tid()* and *free_tid()*, which respectively return
a new transaction ID, and mark a given transaction ID as no longer in use.

The main piece of the mechanism is the lockfile: a file named *lock* which
holds the maximum transaction ID in use. This file gets opened and mmap()'ed
for faster use inside *jopen()*. That way, we can treat it directly as an
integer holding the max tid.

To avoid parallel modifications, we will always lock the file with *fcntl()*
before accessing it.

Let's begin by describing how *get_tid()* works, because it's quite simple: it
locks the lockfile, gets the max tid, adds 1 to it, unlock the file and return
that value. That way, the new tid is always the new max, and with the locking
we can be sure it's impossible to assign the same tid to two different
transactions.

After a tid has been assigned, the commit process will create a file named
after it inside the journal directory. Then, it will operate on that file all
it wants, and when the moment comes, the transaction is no longer needed and
has to be freed.

The first thing we do is to unlink that transaction file. And then, we call
*free_tid()*, which will update the lockfile to represent the new max tid, in
case it has changed.

*free_tid()* begins by checking that if the transaction we're freeing is the
greatest, and if not, just returns.

But if it is, we need to find out the new max tid. We do it by "walking" the
journal directory looking for the file with the greatest number, and that's
our new max tid. If there are no files, we use 0.


Things to notice
----------------

The following is a list of small things to notice about the mechanism. They're
useful because races tend to be subtle, and I *will* forget about them. The
descriptions are not really detailed, just enough to give a general idea.

 - It is possible that we get in *free_tid()* and the transaction we want to
   free is greater than the max tid. In that case, we do nothing: it's a valid
   situation. How to get there: two threads about to free two tids. The first
   one calls *unlink()* and just after its return (before it gets a chance to
   call *free_tid()*), another thread, the holder of the current max, steps in
   and performs both the *unlink()* and *free_tid()*, which would force a
   lookup to find a new tid, and as in the first thread we have removed the
   file, the max tid could be lower (in particular, it could be 0). This is
   why we only test for equalty.
 - Unlink after *free_tid()* is not desirable: in that case, it'd be normal
   for the tid to increment even if we have only one thread writing. It
   overflows quite easily.
 - The fact that new tids are always bigger than the current max is not only
   because the code is cleaner and faster: that way when recovering we know
   the order to apply transactions. A nice catch: this doesn't matter if we're
   working with non-overlapping transactions, but if they overlap, we know
   that it's impossible that transaction A and B (B gets committed after A)
   get applied in the wrong order, because B will only begin to commit *after*
   A has been worked on.


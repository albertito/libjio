
#ifndef _TRANS_H
#define _TRANS_H

struct operation;

/** A transaction */
struct jtrans {
	/** Journal file structure to operate on */
	struct jfs *fs;

	/** Transaction id */
	int id;

	/** Transaction flags */
	uint32_t flags;

	/** Number of operations in the list */
	unsigned int numops;

	/** Transaction's length */
	size_t len;

	/** Lock that protects the list of operations */
	pthread_mutex_t lock;

	/** List of operations */
	struct operation *op;
};

/* a single operation */
struct operation {
	int locked;		/* is the region is locked? */
	off_t offset;		/* operation's offset */
	size_t len;		/* data length */
	void *buf;		/* data */
	size_t plen;		/* previous data length */
	void *pdata;		/* previous data */
	struct operation *prev;
	struct operation *next;
};

/* lingered transaction */
struct journal_op;
struct jlinger {
	struct journal_op *jop;
	struct jlinger *next;
};


#endif


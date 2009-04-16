
#ifndef _TRANS_H
#define _TRANS_H


struct joper;

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
	struct joper *op;
};

/* a single operation */
struct joper {
	int locked;		/* is the region is locked? */
	off_t offset;		/* operation's offset */
	size_t len;		/* data length */
	void *buf;		/* data */
	size_t plen;		/* previous data length */
	void *pdata;		/* previous data */
	struct joper *prev;
	struct joper *next;
};

/* lingered transaction */
struct journal_op;
struct jlinger {
	struct journal_op *jop;
	struct jlinger *next;
};


/* on-disk structures */

/* header (fixed length, defined below) */
struct disk_header {
	uint32_t id;		/* id */
	uint32_t flags;		/* flags about this transaction */
	uint32_t numops;	/* number of operations */
};

/* operation */
struct disk_operation {
	uint32_t len;		/* data length */
	uint32_t plen;		/* previous data length */
	uint64_t offset;	/* offset relative to the BOF */
	char *prevdata;		/* previous data for rollback */
};

/* disk constants */
#define J_DISKHEADSIZE	 12	/* length of disk_header */
#define J_DISKOPHEADSIZE 16	/* length of disk_operation header */


#endif


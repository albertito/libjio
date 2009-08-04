
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

	/** Number of read operations in the list */
	unsigned int numops_r;

	/** Number of write operations in the list */
	unsigned int numops_w;

	/** Sum of the lengths of the write operations */
	size_t len_w;

	/** Lock that protects the list of operations */
	pthread_mutex_t lock;

	/** List of operations */
	struct operation *op;
};

/** Possible operation directions */
enum op_direction {
	D_READ = 1,
	D_WRITE = 2,
};

/** A single operation */
struct operation {
	/** Is the region locked? */
	int locked;

	/** Operation's offset */
	off_t offset;

	/** Data length, in bytes */
	size_t len;

	/** Data buffer */
	void *buf;

	/** Direction */
	enum op_direction direction;

	/** Previous data length (only if direction == D_WRITE) */
	size_t plen;

	/** Previous data (only if direction == D_WRITE) */
	void *pdata;

	/** Previous operation */
	struct operation *prev;

	/** Next operation */
	struct operation *next;
};

/* lingered transaction */
struct journal_op;
struct jlinger {
	struct journal_op *jop;
	struct jlinger *next;
};


#endif


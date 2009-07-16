
#ifndef _JOURNAL_H
#define _JOURNAL_H

#include <stdint.h>
#include "libjio.h"


struct journal_op {
	int id;
	int fd;
	int numops;
	char *name;
	uint32_t csum;
	struct jfs *fs;
};

typedef struct journal_op jop_t;

struct journal_op *journal_new(struct jfs *fs, unsigned int flags);
int journal_add_op(struct journal_op *jop, unsigned char *buf, size_t len,
		off_t offset);
void journal_pre_commit(struct journal_op *jop);
int journal_commit(struct journal_op *jop);
int journal_free(struct journal_op *jop, int do_unlink);

int fill_trans(unsigned char *map, off_t len, struct jtrans *ts);

#endif


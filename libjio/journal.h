
#ifndef _JOURNAL_H
#define _JOURNAL_H

#include <stdint.h>
#include "libjio.h"


struct journal_op {
	int id;
	int fd;
	char *name;
	off_t curpos;
	uint32_t csum;
	struct jtrans *ts;
	struct jfs *fs;
};

typedef struct journal_op jop_t;

struct journal_op *journal_new(struct jtrans *ts);
int journal_save(struct journal_op *jop);
int journal_free(struct journal_op *jop);

int fill_trans(unsigned char *map, off_t len, struct jtrans *ts);

#endif


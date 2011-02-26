
CFLAGS := -Wall -std=c99 -pedantic -O3 -D_XOPEN_SOURCE=500 \
	$(shell getconf LFS_CFLAGS 2>/dev/null)
LIBS = -ljio

BINS = full jio1 jio2 jio3

default: all

all: $(BINS)

$(BINS):
	$(CC) $(CFLAGS) $(LIBS) $@.c -o $@

clean:
	rm -f $(BINS)
	rm -f *.bb *.bbg *.da *.gcov gmon.out
	rm -f test1 test2 test3
	rm -rf .test1.jio .test2.jio .test3.jio

.PHONY: default all clean


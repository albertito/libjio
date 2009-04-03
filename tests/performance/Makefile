
CFLAGS := -Wall -O3 -D_XOPEN_SOURCE=500 \
	$(shell getconf LFS_CFLAGS 2>/dev/null)
LIBS = -ljio

default: all

all: performance random

performance: performance.o
	$(CC) $(LIBS) performance.o -o performance

random: random.o
	$(CC) $(LIBS) random.o -o random

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f performance.o performance
	rm -f random.o random
	rm -f *.bb *.bbg *.da *.gcov gmon.out
	rm -f test_file
	rm -rf .test_file.jio


.PHONY: default all clean



LIBS = -ljio -lpthread
CFLAGS = -Wall -O6

default: all

all: parallel streaming

parallell: parallel.o
	$(CC) $(LIBS) parallel.o -o parallel

streaming: streaming.o
	$(CC) $(LIBS) streaming.o -o streaming

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f streaming.o streaming parallel.o parallel
	rm -f *.bb *.bbg *.da *.gcov gmon.out


.PHONY: default all clean


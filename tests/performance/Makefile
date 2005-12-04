
LIBS = -ljio -lpthread
CFLAGS = -Wall -O6

default: all

all: paralell streaming

paralell: paralell.o
	$(CC) $(LIBS) paralell.o -o paralell

streaming: streaming.o
	$(CC) $(LIBS) streaming.o -o streaming

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f streaming.o streaming paralell.o paralell
	rm -f *.bb *.bbg *.da *.gcov gmon.out


.PHONY: default all clean



include Make.conf


# objects to build
OBJS = libjio.o

# rules
default: all

all: shared static jiofsck

shared: libjio.o
	$(CC) -shared libjio.o -o libjio.so

static: libjio.o
	$(AR) cr libjio.a libjio.o

jiofsck: jiofsck.o static
	$(CC) jiofsck.o libjio.a -lpthread -o jiofsck

install: all
	install -g root -o root -m 0755 libjio.so $(PREFIX)/lib
	install -g root -o root -m 0644 libjio.a $(PREFIX)/lib
	install -g root -o root -m 0644 libjio.h $(PREFIX)/include
	install -g root -o root -m 0775 jiofsck $(PREFIX)/bin
	install -g root -o root -m 0644 -d $(PREFIX)/man/man3
	install -g root -o root -m 0644 doc/libjio.3 $(PREFIX)/man/man3/
	@echo
	@echo "Please run ldconfig to update your library cache"
	@echo

.c.o:
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@


clean:
	rm -f libjio.o libjio.a libjio.so jiofsck.o jiofsck
	rm -f *.bb *.bbg *.da *.gcov gmon.out


.PHONY: default all shared static install clean


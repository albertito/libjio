
include Make.conf


# objects to build
OBJS = checksum.o common.o trans.o check.o unix.o ansi.o

# rules
default: all

all: shared static jiofsck

shared: $(OBJS)
	$(CC) -shared $(OBJS) -o libjio.so

static: $(OBJS)
	$(AR) cr libjio.a $(OBJS)

jiofsck: jiofsck.o static
	$(CC) jiofsck.o libjio.a -lpthread -o jiofsck

install: all
	install -g root -o root -d $(PREFIX)/lib
	install -g root -o root -m 0755 libjio.so $(PREFIX)/lib
	install -g root -o root -m 0644 libjio.a $(PREFIX)/lib
	install -g root -o root -d $(PREFIX)/include
	install -g root -o root -m 0644 libjio.h $(PREFIX)/include
	install -g root -o root -d $(PREFIX)/bin
	install -g root -o root -m 0775 jiofsck $(PREFIX)/bin
	install -g root -o root -d $(PREFIX)/man/man3
	install -g root -o root -m 0644 doc/libjio.3 $(PREFIX)/man/man3/
	@echo
	@echo "Please run ldconfig to update your library cache"
	@echo

.c.o:
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@


clean:
	rm -f $(OBJS) libjio.a libjio.so jiofsck.o jiofsck
	rm -f *.bb *.bbg *.da *.gcov gmon.out


.PHONY: default all shared static install clean


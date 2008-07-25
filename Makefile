
VERSION="0.23"

CC = gcc
CFLAGS += -Wall -O3 -fPIC \
	-D_LARGEFILE_SOURCE=1 -D_LARGEFILE64_SOURCE=1 \
	-D_LFS_LARGEFILE=1 -D_LFS64_LARGEFILE=1 \
	-D_FILE_OFFSET_BITS=64 `getconf LFS_CFLAGS 2>/dev/null` \
	-D_XOPEN_SOURCE=500

ifdef DEBUG
CFLAGS += -g -pg -fprofile-arcs -ftest-coverage
endif

ifdef STRICT
CFLAGS += -ansi -pedantic
endif

# prefix for installing the binaries
PREFIX=/usr/local


# objects to build
OBJS = checksum.o common.o trans.o check.o unix.o ansi.o

# rules
default: all

all: libjio.so libjio.a jiofsck

libjio.so: $(OBJS)
	$(CC) -shared -fPIC $(OBJS) -o libjio.so

libjio.a: $(OBJS)
	$(AR) cr libjio.a $(OBJS)

jiofsck: jiofsck.o libjio.a
	$(CC) jiofsck.o libjio.a -lpthread -o jiofsck

install: all
	install -d $(PREFIX)/lib
	install -m 0755 libjio.so $(PREFIX)/lib
	install -m 0644 libjio.a $(PREFIX)/lib
	install -d $(PREFIX)/include
	install -m 0644 libjio.h $(PREFIX)/include
	install -d $(PREFIX)/bin
	install -m 0775 jiofsck $(PREFIX)/bin
	install -d $(PREFIX)/man/man3
	install -m 0644 doc/libjio.3 $(PREFIX)/man/man3/
	@echo
	@echo "Please run ldconfig to update your library cache"
	@echo

.c.o:
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@


python: all
	cd bindings/python && python setup.py build

python_install: python
	cd bindings/python && python setup.py install


preload: all
	install -d bindings/preload/build/
	$(CC) $(INCLUDES) -Wall -O3 -shared -fPIC \
		-D_XOPEN_SOURCE=500 \
		-ldl -lpthread -L. -ljio -I. \
		bindings/preload/libjio_preload.c \
		-o bindings/preload/build/libjio_preload.so

preload_install: preload
	install -d $(PREFIX)/lib
	install -m 0755 bindings/preload/build/libjio_preload.so $(PREFIX)/lib


clean:
	rm -f $(OBJS) libjio.a libjio.so jiofsck.o jiofsck
	rm -f *.bb *.bbg *.da *.gcov gmon.out
	rm -rf bindings/python/build/
	rm -rf bindings/preload/build/


.PHONY: default all install python python_install preload preload_install clean


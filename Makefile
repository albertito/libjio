
VERSION="0.23"

CFLAGS = -std=c99 -pedantic -Wall -O3

MANDATORY_CFLAGS := \
	-D_LARGEFILE_SOURCE=1 -D_LARGEFILE64_SOURCE=1 \
	-D_LFS_LARGEFILE=1 -D_LFS64_LARGEFILE=1 \
	-D_FILE_OFFSET_BITS=64 $(shell getconf LFS_CFLAGS 2>/dev/null) \
	-D_XOPEN_SOURCE=500

ALL_CFLAGS += $(CFLAGS) $(MANDATORY_CFLAGS) -fPIC

ifdef DEBUG
ALL_CFLAGS += -g
endif

ifdef PROFILE
ALL_CFLAGS += -g -pg -fprofile-arcs -ftest-coverage
endif


# prefix for installing the binaries
PREFIX=/usr/local


ifneq ($(V), 1)
        NICE_CC = @echo "  CC  $@"; $(CC)
        NICE_AR = @echo "  AR  $@"; $(AR)
else
        NICE_CC = $(CC)
        NICE_AR = $(AR)
endif


# objects to build
OBJS = checksum.o common.o trans.o check.o unix.o ansi.o

# rules
default: all

all: libjio.so libjio.a libjio.pc jiofsck

libjio.so: $(OBJS)
	$(NICE_CC) -shared $(ALL_CFLAGS) $(OBJS) -o libjio.so

libjio.a: $(OBJS)
	$(NICE_AR) cr libjio.a $(OBJS)

libjio.pc: libjio.skel.pc
	@echo "generating libjio.pc"
	@cat libjio.skel.pc | \
		sed 's@++PREFIX++@$(PREFIX)@g' | \
		sed 's@++CFLAGS++@$(MANDATORY_CFLAGS)@g' \
		> libjio.pc

jiofsck: jiofsck.o libjio.a
	$(NICE_CC) $(ALL_CFLAGS) jiofsck.o libjio.a -lpthread -o jiofsck

install: all
	install -d $(PREFIX)/lib
	install -m 0755 libjio.so $(PREFIX)/lib
	install -m 0644 libjio.a $(PREFIX)/lib
	install -d $(PREFIX)/include
	install -m 0644 libjio.h $(PREFIX)/include
	install -d $(PREFIX)/lib/pkgconfig
	install -m 644 libjio.pc $(PREFIX)/lib/pkgconfig
	install -d $(PREFIX)/bin
	install -m 0775 jiofsck $(PREFIX)/bin
	install -d $(PREFIX)/man/man3
	install -m 0644 doc/libjio.3 $(PREFIX)/man/man3/
	@echo
	@echo "Please run ldconfig to update your library cache"
	@echo

.c.o:
	$(NICE_CC) $(ALL_CFLAGS) $(INCLUDES) -c $< -o $@


python: all
	cd bindings/python && python setup.py build

python_install: python
	cd bindings/python && python setup.py install


preload: all
	install -d bindings/preload/build/
	$(NICE_CC) $(INCLUDES) -Wall -O3 -shared -fPIC \
		-D_XOPEN_SOURCE=500 \
		-ldl -lpthread -L. -ljio -I. \
		bindings/preload/libjio_preload.c \
		-o bindings/preload/build/libjio_preload.so

preload_install: preload
	install -d $(PREFIX)/lib
	install -m 0755 bindings/preload/build/libjio_preload.so $(PREFIX)/lib


clean:
	rm -f $(OBJS) libjio.a libjio.so libjio.pc jiofsck.o jiofsck
	rm -f *.bb *.bbg *.da *.gcov *.gcno *.gcda gmon.out
	rm -rf bindings/python/build/
	rm -rf bindings/preload/build/


.PHONY: default all install python python_install preload preload_install clean


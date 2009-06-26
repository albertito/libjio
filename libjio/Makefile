
CFLAGS = -std=c99 -pedantic -Wall -O3

MANDATORY_CFLAGS := \
	-D_LARGEFILE_SOURCE=1 $(shell getconf LFS_CFLAGS 2>/dev/null) \
	-D_XOPEN_SOURCE=600

MANDATORY_LDFLAGS := $(shell getconf LFS_LIBS 2>/dev/null)

ALL_CFLAGS += $(CFLAGS) $(MANDATORY_CFLAGS) -fPIC
ALL_LDFLAGS += $(LDFLAGS) $(MANDATORY_LDFLAGS) -fPIC

# some platforms do not have librt, we only use it if available
NEED_LIBRT := $(shell ld -o rtcheck.so -shared -lrt 2>/dev/null && echo -lrt; \
	rm -f rtcheck.so)
LIBS = -lpthread $(NEED_LIBRT)

ifdef DEBUG
ALL_CFLAGS += -g
endif

ifdef PROFILE
ALL_CFLAGS += -g -pg -fprofile-arcs -ftest-coverage
endif

ifdef FI
ALL_CFLAGS += -DFIU_ENABLE=1
LIBS += -lfiu
endif

# prefix for installing the binaries
PREFIX = /usr/local


ifneq ($(V), 1)
        NICE_CC = @echo "  CC  $@"; $(CC)
        NICE_AR = @echo "  AR  $@"; $(AR)
else
        NICE_CC = $(CC)
        NICE_AR = $(AR)
endif


# objects to build
OBJS = autosync.o checksum.o common.o compat.o trans.o check.o journal.o \
       unix.o ansi.o

# rules
default: all

all: libjio.so libjio.a libjio.pc jiofsck

libjio.so: $(OBJS)
	$(NICE_CC) -shared $(ALL_LDFLAGS) $(LIBS) $(OBJS) -o libjio.so

libjio.a: $(OBJS)
	$(NICE_AR) cr libjio.a $(OBJS)

libjio.pc: libjio.skel.pc
	@echo "generating libjio.pc"
	@cat libjio.skel.pc | \
		sed 's@++PREFIX++@$(PREFIX)@g' | \
		sed 's@++CFLAGS++@$(MANDATORY_CFLAGS)@g' \
		> libjio.pc

jiofsck: jiofsck.o libjio.a
	$(NICE_CC) $(ALL_LDFLAGS) jiofsck.o libjio.a $(LIBS) -o jiofsck

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
	install -m 0644 libjio.3 $(PREFIX)/man/man3/
	@echo
	@echo "Please run ldconfig to update your library cache"
	@echo

.c.o:
	$(NICE_CC) $(ALL_CFLAGS) -c $< -o $@

doxygen:
	$(MAKE) -C doxygen

clean:
	rm -f $(OBJS) libjio.a libjio.so libjio.pc jiofsck.o jiofsck
	rm -f *.bb *.bbg *.da *.gcov *.gcno *.gcda gmon.out
	$(MAKE) -C doxygen $@


.PHONY: default all install clean doxygen


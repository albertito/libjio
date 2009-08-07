
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
ALL_LDFLAGS += -g
endif

ifdef PROFILE
ALL_CFLAGS += -g -pg -fprofile-arcs -ftest-coverage
ALL_LDFLAGS += -g -pg -fprofile-arcs -ftest-coverage -lgcov
endif

ifdef FI
ALL_CFLAGS += -DFIU_ENABLE=1
LIBS += -lfiu
endif

# prefix for installing the binaries
PREFIX = /usr/local

# prefix for eventual location of binaries
DESTDIR=$(PREFIX)

# install utility, we assume it's GNU/BSD compatible
INSTALL=install

ifneq ($(V), 1)
        NICE_CC = @echo "  CC  $@"; $(CC)
        NICE_AR = @echo "  AR  $@"; $(AR)
else
        NICE_CC = $(CC)
        NICE_AR = $(AR)
endif


LIB_VER=0.90
LIB_SO_VER=0


# objects to build
OBJS = autosync.o checksum.o common.o compat.o trans.o check.o journal.o \
       unix.o ansi.o

# rules
default: all

all: libjio.so libjio.a libjio.pc jiofsck

libjio.so: build-flags $(OBJS)
	$(NICE_CC) -shared $(ALL_LDFLAGS) \
		-Wl,-soname,libjio.so.$(LIB_SO_VER) \
		$(LIBS) $(OBJS) -o libjio.so.$(LIB_VER)
	ln -fs libjio.so.$(LIB_VER) libjio.so

libjio.a: build-flags $(OBJS)
	$(NICE_AR) cr libjio.a $(OBJS)

libjio.pc: build-flags libjio.pc.in
	@echo "generating libjio.pc"
	@cat libjio.pc.in | \
		sed 's@++PREFIX++@$(DESTDIR)@g' | \
		sed 's@++VERSION++@$(LIB_VER)@g' | \
		sed 's@++CFLAGS++@$(MANDATORY_CFLAGS)@g' \
		> libjio.pc

jiofsck: build-flags jiofsck.o libjio.a
	$(NICE_CC) $(ALL_LDFLAGS) jiofsck.o libjio.a $(LIBS) -o jiofsck

install: all
	$(INSTALL) -d $(PREFIX)/lib
	$(INSTALL) -m 0755 libjio.so.$(LIB_VER) $(PREFIX)/lib
	ln -fs libjio.so.$(LIB_VER) $(PREFIX)/lib/libjio.so
	ln -fs libjio.so.$(LIB_VER) $(PREFIX)/lib/libjio.so.$(LIB_SO_VER)
	$(INSTALL) -m 0644 libjio.a $(PREFIX)/lib
	$(INSTALL) -d $(PREFIX)/include
	$(INSTALL) -m 0644 libjio.h $(PREFIX)/include
	$(INSTALL) -d $(PREFIX)/lib/pkgconfig
	$(INSTALL) -m 644 libjio.pc $(PREFIX)/lib/pkgconfig
	$(INSTALL) -d $(PREFIX)/bin
	$(INSTALL) -m 0775 jiofsck $(PREFIX)/bin
	$(INSTALL) -d $(PREFIX)/share/man/man3
	$(INSTALL) -m 0644 libjio.3 $(PREFIX)/share/man/man3/
	@echo
	@echo "Please run ldconfig to update your library cache"
	@echo

BF = $(ALL_CFLAGS) ~ $(PREFIX)
build-flags: .force-build-flags
	@if [ x"$(BF)" != x"`cat build-flags 2>/dev/null`" ]; then \
		if [ -f build-flags ]; then \
			echo "build flags changed, rebuilding"; \
		fi; \
		echo "$(BF)" > build-flags; \
	fi

$(OBJS): build-flags

.c.o:
	$(NICE_CC) $(ALL_CFLAGS) -c $< -o $@

doxygen:
	$(MAKE) LIB_VER=$(LIB_VER) -C doxygen

clean:
	rm -f libjio.a libjio.so libjio.so.$(LIB_VER) libjio.pc
	rm -f $(OBJS) jiofsck.o jiofsck
	rm -f *.bb *.bbg *.da *.gcov *.gcno *.gcda gmon.out build-flags
	$(MAKE) -C doxygen $@


.PHONY: default all install clean doxygen .force-build-flags


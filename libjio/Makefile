
# output directory, will be created if it doesn't exist
O = build

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


# shorthands for common build variants

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


# nicer output
ifneq ($(V), 1)
        N_CC = @echo "  CC  $(subst $O/,,$@)"; $(CC)
        N_AR = @echo "  AR  $(subst $O/,,$@)"; $(AR)
else
        N_CC = $(CC)
        N_AR = $(AR)
endif


# library version, used for soname and generated documentation
LIB_VER=1.02
LIB_SO_VER=1


OBJS = $(addprefix $O/,autosync.o checksum.o common.o compat.o trans.o \
               check.o journal.o unix.o ansi.o)


# targets

default: all

all: $O/libjio.so $O/libjio.a $O/libjio.pc $O/jiofsck

# used to rebuild everything when the build flags have changed
BF = $(CC) ~ $(ALL_CFLAGS) ~ $(PREFIX)
$O/build-flags: .force-build-flags
	@mkdir -p $O
	@if [ x"$(BF)" != x"`cat $O/build-flags 2>/dev/null`" ]; then \
		if [ -f $O/build-flags ]; then \
			echo "build flags changed, rebuilding"; \
		fi; \
		echo "$(BF)" > $O/build-flags; \
	fi

$(OBJS): $O/build-flags

$O/%.o: %.c
	@mkdir -p $O
	$(N_CC) $(ALL_CFLAGS) -MMD -MF $@.mak -MP -MT $@ -c $< -o $@

sinclude $(OBJS:.o=.o.mak)

$O/libjio.so: $O/build-flags $(OBJS)
	$(N_CC) -shared $(ALL_LDFLAGS) \
		-Wl,-soname,libjio.so.$(LIB_SO_VER) \
		$(LIBS) $(OBJS) -o $O/libjio.so.$(LIB_VER)
	@echo "  LN  libjio.so.$(LIB_VER)"
	@ln -fs libjio.so.$(LIB_VER) $O/libjio.so
	@ln -fs libjio.so.$(LIB_VER) $O/libjio.so.$(LIB_SO_VER)

$O/libjio.a: $O/build-flags $(OBJS)
	$(N_AR) cr $@ $(OBJS)

$O/libjio.pc: $O/build-flags libjio.pc.in
	@echo "  GEN libjio.pc"
	@cat libjio.pc.in | \
		sed 's@++PREFIX++@$(DESTDIR)@g' | \
		sed 's@++VERSION++@$(LIB_VER)@g' | \
		sed 's@++CFLAGS++@$(MANDATORY_CFLAGS)@g' \
		> $O/libjio.pc

$O/jiofsck: $O/build-flags $O/jiofsck.o $O/libjio.a
	$(N_CC) $(ALL_LDFLAGS) $O/jiofsck.o $O/libjio.a $(LIBS) -o $@

install: all
	$(INSTALL) -d $(PREFIX)/lib
	$(INSTALL) -m 0755 $O/libjio.so.$(LIB_VER) $(PREFIX)/lib
	ln -fs libjio.so.$(LIB_VER) $(PREFIX)/lib/libjio.so
	ln -fs libjio.so.$(LIB_VER) $(PREFIX)/lib/libjio.so.$(LIB_SO_VER)
	$(INSTALL) -m 0644 $O/libjio.a $(PREFIX)/lib
	$(INSTALL) -d $(PREFIX)/include
	$(INSTALL) -m 0644 libjio.h $(PREFIX)/include
	$(INSTALL) -d $(PREFIX)/lib/pkgconfig
	$(INSTALL) -m 644 $O/libjio.pc $(PREFIX)/lib/pkgconfig
	$(INSTALL) -d $(PREFIX)/bin
	$(INSTALL) -m 0775 $O/jiofsck $(PREFIX)/bin
	$(INSTALL) -d $(PREFIX)/share/man/man3
	$(INSTALL) -m 0644 libjio.3 $(PREFIX)/share/man/man3/
	@echo
	@echo "Please run ldconfig to update your library cache"
	@echo

doxygen:
	$(MAKE) LIB_VER=$(LIB_VER) -C doxygen

clean:
	rm -f $O/libjio.a $O/libjio.so $O/libjio.so.$(LIB_VER) $O/libjio.pc
	rm -f $O/libjio.so.$(LIB_SO_VER)
	rm -f $(OBJS) $O/jiofsck.o $O/jiofsck
	rm -f $O/*.bb $O/*.bbg $O/*.da $O/*.gcov $O/*.gcno $O/*.gcda $O/gmon.out
	rm -f $O/build-flags $O/*.o.mak

	$(MAKE) -C doxygen $@


.PHONY: default all install clean doxygen .force-build-flags


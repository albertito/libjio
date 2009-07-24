
CFLAGS = -Wall -O3

MANDATORY_CFLAGS := \
	-D_LARGEFILE_SOURCE=1 $(shell getconf LFS_CFLAGS 2>/dev/null) \
	-D_XOPEN_SOURCE=500

ALL_CFLAGS = $(CFLAGS) $(MANDATORY_CFLAGS) -fPIC


PREFIX = /usr/local
INSTALL = install

ifneq ($(V), 1)
        NICE_CC = @echo "  CC  $@"; $(CC)
else
        NICE_CC = $(CC)
endif


default: all

all: preload

preload: libjio_preload.o
	$(NICE_CC) $(ALL_CFLAGS) -ljio -shared -fPIC \
		libjio_preload.o -o libjio_preload.so

.c.o:
	$(NICE_CC) $(ALL_CFLAGS) -c $< -o $@

install: preload
	$(INSTALL) -d $(PREFIX)/lib
	$(INSTALL) -m 0755 libjio_preload.so $(PREFIX)/lib

clean:
	rm -f libjio_preload.o libjio_preload.so

.PHONY: default all install clean


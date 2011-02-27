
default: all

all: libjio


libjio:
	$(MAKE) -C libjio/

install:
	$(MAKE) -C libjio/ install


PY_DEBUG=
ifdef DEBUG
	PY_DEBUG=--debug
endif

python2: libjio
	cd bindings/python && python setup.py build $(PY_DEBUG)

python2_install: python2
	cd bindings/python && python setup.py install

python3: libjio
	cd bindings/python && python3 setup.py build $(PY_DEBUG)

python3_install: python3
	cd bindings/python && python3 setup.py install


preload:
	$(MAKE) -C bindings/preload/

preload_install: preload
	$(MAKE) -C bindings/preload/ install

tests: all python2 python3
	tests/util/quick-test-run normal

tests-fi: all python2 python3
	@if [ "$(FI)" != "1" ]; then \
		echo "Error: $@ has to be run using:  make FI=1 $@"; \
		exit 1; \
	fi
	tests/util/quick-test-run fiu

clean:
	$(MAKE) -C libjio/ clean
	$(MAKE) -C bindings/preload clean
	rm -rf bindings/python/build/


.PHONY: default all libjio install \
	python2 python2_install python3 python3_install \
	preload preload_install \
	tests tests-fi clean


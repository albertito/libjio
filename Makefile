
default: all

all: libjio


libjio:
	$(MAKE) -C libjio/

install:
	$(MAKE) -C libjio/ install


python2:
	cd bindings/python2 && python setup.py build

python2_install: python2
	cd bindings/python2 && python setup.py install

python3:
	cd bindings/python3 && python3 setup.py build

python3_install: python3
	cd bindings/python3 && python3 setup.py install


preload:
	$(MAKE) -C bindings/preload/

preload_install: preload
	$(MAKE) -C bindings/preload/ install


clean:
	$(MAKE) -C libjio/ clean
	$(MAKE) -C bindings/preload clean
	rm -rf bindings/python2/build/
	rm -rf bindings/python3/build/


.PHONY: default all libjio install \
	python2 python2_install python3 python3_install \
	preload preload_install \
	clean


#!/usr/bin/env python

# Corruption tests using libfiu. libjio must have been built with libfiu
# enabled (using something like make FI=1) for them to work.

from tf import *

try:
	import fiu
except ImportError:
	print
	print "Error: unable to load fiu module. Corruption tests need"
	print "libfiu support. Please install libfiu and recompile libjio"
	print "with FI=1. You can still run the other tests."
	print
	raise


def test_c01():
	"checksum (1 bit change)"
	c = gencontent()

	def f1(f, jf):
		fiu.enable("jio/commit/tf_sync")
		jf.write(c)

	n = run_with_tmp(f1)
	assert content(n) == ''
	tc = open(transpath(n, 1)).read()
	# flip just one bit of the first byte
	tc = chr((ord(tc[0]) & 0xFE) | (~ ord(tc[0]) & 0x1) & 0xFF) + tc[1:]
	open(transpath(n, 1), 'w').write(tc)
	fsck_verify(n, corrupt = 1)
	assert content(n) == ''
	cleanup(n)

def test_c02():
	"truncate trans"
	c = gencontent()

	def f1(f, jf):
		fiu.enable("jio/commit/tf_sync")
		jf.write(c)

	n = run_with_tmp(f1)
	assert content(n) == ''
	tp = transpath(n, 1)
	open(tp, 'r+').truncate(len(content(tp)) - 2)
	fsck_verify(n, broken = 1)
	assert content(n) == ''
	cleanup(n)

def test_c03():
	"op len too big"
	c = gencontent(10)

	def f1(f, jf):
		fiu.enable("jio/commit/tf_sync")
		jf.write(c)

	n = run_with_tmp(f1)
	assert content(n) == ''

	tf = TransFile(transpath(n, 1))
	tf.ops[0].tlen = 99
	tf.save()
	fsck_verify(n, broken = 1)
	assert content(n) == ''
	cleanup(n)

def test_c04():
	"op len too small"
	c = gencontent(100)

	def f1(f, jf):
		fiu.enable("jio/commit/tf_sync")
		jf.write(c)

	n = run_with_tmp(f1)
	assert content(n) == ''

	tf = TransFile(transpath(n, 1))
	tf.ops[0].tlen = 10
	tf.save()
	fsck_verify(n, broken = 1)
	assert content(n) == ''
	cleanup(n)



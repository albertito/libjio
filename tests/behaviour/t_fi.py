#!/usr/bin/env python

# General tests using libfiu. libjio must have been built with libfiu enabled
# (using something like make FI=1) for them to work.

import struct
from tf import *
import libjio

try:
	import fiu
except ImportError:
	print
	print "Error: unable to load fiu module. Fault injection tests need"
	print "libfiu support. Please install libfiu and recompile libjio"
	print "with FI=1. You can still run the other tests."
	print
	raise


def test_f01():
	"fail jio/get_tid/overflow"
	c = gencontent()

	def f1(f, jf):
		jf.write(c)
		fiu.enable("jio/get_tid/overflow")
		try:
			jf.write(c)
		except IOError:
			pass

	n = run_with_tmp(f1)
	assert content(n) == c
	assert struct.unpack("I", content(jiodir(n) + '/lock'))[0] == 0
	fsck_verify(n)
	assert content(n) == c
	cleanup(n)

def test_f02():
	"fail jio/commit/created_tf"
	c = gencontent()

	def f1(f, jf):
		fiu.enable("jio/commit/created_tf")
		jf.write(c)

	n = run_with_tmp(f1)
	fsck_verify(n, broken = 1)
	assert content(n) == ''
	cleanup(n)

def test_f03():
	"fail jio/commit/tf_header"
	c = gencontent()

	def f1(f, jf):
		fiu.enable("jio/commit/tf_header")
		jf.write(c)

	n = run_with_tmp(f1)
	assert content(n) == ''
	fsck_verify(n, broken = 1)
	assert content(n) == ''
	cleanup(n)

def test_f04():
	"fail jio/commit/tf_pre_addop"
	c = gencontent()

	def f1(f, jf):
		fiu.enable_external("jio/commit/tf_pre_addop",
				gen_ret_seq((0, 1)))
		t = jf.new_trans()
		t.add_w(c, 0)
		t.add_w(c, len(c) + 200)
		t.commit()

	n = run_with_tmp(f1)

	assert len(content(transpath(n, 1))) == DHS + DOHS + len(c)
	assert content(n) == ''
	fsck_verify(n, broken = 1)
	assert content(n) == ''
	cleanup(n)

def test_f05():
	"fail jio/commit/tf_opdata"
	c = gencontent()

	def f1(f, jf):
		fiu.enable_external("jio/commit/tf_opdata",
				gen_ret_seq((0, 1)))
		t = jf.new_trans()
		t.add_w(c, 0)
		t.add_w(c, len(c) + 200)
		t.commit()

	n = run_with_tmp(f1)

	assert len(content(transpath(n, 1))) == DHS + (DOHS + len(c)) * 2
	assert content(n) == ''
	fsck_verify(n, broken = 1)
	assert content(n) == ''
	cleanup(n)

def test_f06():
	"fail jio/commit/tf_data"
	c = gencontent()

	def f1(f, jf):
		fiu.enable("jio/commit/tf_data")
		t = jf.new_trans()
		t.add_w(c, 0)
		t.add_w(c, len(c) + 200)
		t.commit()

	n = run_with_tmp(f1)

	assert len(content(transpath(n, 1))) == DHS + (DOHS + len(c)) * 2
	assert content(n) == ''
	fsck_verify(n, broken = 1)
	assert content(n) == ''
	cleanup(n)

def test_f07():
	"fail jio/commit/tf_sync"
	c = gencontent()

	def f1(f, jf):
		fiu.enable("jio/commit/tf_sync")
		jf.write(c)

	n = run_with_tmp(f1)
	assert content(n) == ''
	fsck_verify(n, reapplied = 1)
	assert content(n) == c
	cleanup(n)

def test_f08():
	"fail jio/commit/wrote_op"
	c = gencontent()

	def f1(f, jf):
		fiu.enable("jio/commit/wrote_op")
		t = jf.new_trans()
		t.add_w(c, 0)
		t.add_w(c, len(c) + 200)
		t.commit()

	n = run_with_tmp(f1)

	assert content(n) == c
	fsck_verify(n, reapplied = 1)
	assert content(n) == c + '\0' * 200 + c
	cleanup(n)

def test_f09():
	"fail jio/commit/wrote_all_ops"
	c = gencontent()

	def f1(f, jf):
		fiu.enable("jio/commit/wrote_all_ops")
		jf.write(c)

	n = run_with_tmp(f1)
	assert content(n) == c
	fsck_verify(n, reapplied = 1)
	assert content(n) == c
	cleanup(n)

def test_f10():
	"fail jio/commit/pre_ok_free_tid"
	c = gencontent()

	def f1(f, jf):
		fiu.enable("jio/commit/pre_ok_free_tid")
		jf.write(c)

	n = run_with_tmp(f1)
	assert content(n) == c
	assert struct.unpack("I", content(jiodir(n) + '/lock'))[0] == 1
	fsck_verify(n)
	assert content(n) == c
	assert not os.path.exists(jiodir(n))
	cleanup(n)

def test_f11():
	"fail jio/commit/tf_sync in rollback"
	c = gencontent()

	def f1(f, jf):
		jf.write('x' * (80 + len(c)))
		t = jf.new_trans()
		t.add_w(c, 80)
		t.commit()
		assert content(f.name) == 'x' * 80 + c
		fiu.enable("jio/commit/tf_sync")
		t.rollback()

	n = run_with_tmp(f1)

	assert content(n) == 'x' * 80 + c
	fsck_verify(n, reapplied = 1)
	assert content(n) == 'x' * (80 + len(c))
	cleanup(n)

def test_f12():
	"fail jio/jsync/pre_unlink"
	c = gencontent()

	def f1(f, jf):
		fiu.enable("jio/jsync/pre_unlink")
		t = jf.new_trans()
		t.add_w(c, 0)
		t.commit()
		jf.jsync()

	n = run_with_tmp(f1, libjio.J_LINGER)

	assert content(n) == c
	fsck_verify(n, reapplied = 1)
	assert content(n) == c
	cleanup(n)



#!/usr/bin/env python

# Normal tests.

import libjio
from tf import *


def test_n01():
	"open + close"
	def f1(f, jf):
		pass

	n = run_with_tmp(f1)
	assert content(n) == ''
	fsck_verify(n)
	assert content(n) == ''
	cleanup(n)

def test_n02():
	"write + seek + read"
	c = gencontent()

	def f1(f, jf):
		jf.write(c)
		jf.lseek(0, 0)
		assert jf.read(len(c) * 2) == c

	n = run_with_tmp(f1)
	assert content(n) == c
	fsck_verify(n)
	cleanup(n)

def test_n03():
	"pwrite"
	c = gencontent()

	def f1(f, jf):
		jf.pwrite(c, 80)

	n = run_with_tmp(f1)
	assert content(n) == '\0' * 80 + c
	fsck_verify(n)
	cleanup(n)

def test_n04():
	"truncate"
	def f1(f, jf):
		jf.truncate(826)

	n = run_with_tmp(f1)
	assert content(n) == '\0' * 826
	fsck_verify(n)
	cleanup(n)

def test_n05():
	"commit"
	c = gencontent()

	def f1(f, jf):
		t = jf.new_trans()
		t.add(c, 80)
		t.commit()

	n = run_with_tmp(f1)
	assert content(n) == '\0' * 80 + c
	fsck_verify(n)
	cleanup(n)

def test_n06():
	"empty, then rollback"
	c = gencontent()

	def f1(f, jf):
		t = jf.new_trans()
		t.add(c, 80)
		t.commit()
		t.rollback()

	n = run_with_tmp(f1)

	# XXX: This is weird, because the file was empty at the beginning.
	# However, making it go back to 0 is delicate and the current code
	# doesn't implement it. It probably should.
	assert content(n) == '\0' * 80
	fsck_verify(n)
	cleanup(n)

def test_n07():
	"extending, then rollback"
	c1 = gencontent()
	c2 = gencontent()

	def f1(f, jf):
		jf.write(c1)
		t = jf.new_trans()
		t.add(c2, len(c1) - 973)
		t.commit()
		t.rollback()

	n = run_with_tmp(f1)

	assert content(n) == c1
	fsck_verify(n)
	cleanup(n)

def test_n08():
	"multiple overlapping ops"
	c1 = gencontent(9345)
	c2 = gencontent(len(c1))
	c3 = gencontent(len(c1))
	c4 = gencontent(len(c1))
	c5 = gencontent(len(c1))

	def f1(f, jf):
		jf.write(c1)
		t = jf.new_trans()
		t.add(c2, len(c1) - 973)
		t.add(c3, len(c1) - 1041)
		t.add(c4, len(c1) - 666)
		t.add(c5, len(c1) - 3000)
		t.commit()

	n = run_with_tmp(f1)
	assert content(n) == c1[:-3000] + c5 + c4[- (- 666 + 3000):]
	fsck_verify(n)
	cleanup(n)

def test_n09():
	"rollback multiple overlapping ops"
	c1 = gencontent(9345)
	c2 = gencontent(len(c1))
	c3 = gencontent(len(c1))
	c4 = gencontent(len(c1))
	c5 = gencontent(len(c1))

	def f1(f, jf):
		jf.write(c1)
		t = jf.new_trans()
		t.add(c2, len(c1) - 973)
		t.add(c3, len(c1) - 1041)
		t.add(c4, len(c1) - 666)
		t.add(c5, len(c1) - 3000)
		t.commit()
		t.rollback()

	n = run_with_tmp(f1)

	assert content(n) == c1
	fsck_verify(n)
	cleanup(n)

def test_n10():
	"lingering transactions"
	c = gencontent()

	def f1(f, jf):
		t = jf.new_trans()
		t.add(c, 0)
		t.commit()
		del t
		assert content(f.name) == c
		assert os.path.exists(transpath(f.name, 1))
		jf.jsync()
		assert not os.path.exists(transpath(f.name, 1))

	n = run_with_tmp(f1, libjio.J_LINGER)

	assert content(n) == c
	fsck_verify(n)
	cleanup(n)



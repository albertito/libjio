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
		jf.write(c[:len(c) / 2])
		jf.write(c[len(c) / 2:])
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
		t.add_w(c, 80)
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
		t.add_w(c, 80)
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
		t.add_w(c2, len(c1) - 973)
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
		t.add_w(c2, len(c1) - 973)
		t.add_w(c3, len(c1) - 1041)
		t.add_w(c4, len(c1) - 666)
		t.add_w(c5, len(c1) - 3000)
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
		t.add_w(c2, len(c1) - 973)
		t.add_w(c3, len(c1) - 1041)
		t.add_w(c4, len(c1) - 666)
		t.add_w(c5, len(c1) - 3000)
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
		t.add_w(c, 0)
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

def test_n11():
	"jfsck a nonexisting file"
	try:
		libjio.jfsck('this file does not exist')
	except IOError:
		return
	raise

def test_n12():
	"jfsck with a nonexisting dir"
	f, jf = bitmp()
	try:
		libjio.jfsck(f.name, 'this directory does not exist')
	except IOError:
		cleanup(f.name)
		return
	raise

def test_n13():
	"move journal to a nonexisting dir"
	import os

	f, jf = bitmp()
	n = f.name
	p = tmppath()

	jf.write('x')
	jf.jmove_journal(p)
	jf.write('y')
	del jf

	assert libjio.jfsck(n, p)['total'] == 0
	os.unlink(n)

def test_n14():
	"autosync"
	f, jf = bitmp(jflags = libjio.J_LINGER)
	n = f.name

	jf.autosync_start(1, 10)
	jf.write('x' * 200)
	jf.write('x' * 200)
	jf.autosync_stop()
	del jf

	fsck_verify(n)
	cleanup(n)

def test_n15():
	"jpread/jpwrite"
	c = gencontent()

	f, jf = bitmp(jflags = libjio.J_LINGER)
	n = f.name

	jf.pwrite(c, 2000)
	assert content(n) == '\0' * 2000 + c
	assert jf.pread(len(c), 2000) == c
	del jf

	fsck_verify(n)
	cleanup(n)

def test_n16():
	"jopen r/o + jtrans_add_w + jtrans_commit"
	c = gencontent()

	# create the file before opening, read-only mode does not create it
	n = tmppath()
	open(n, 'w+')
	f, jf = biopen(n, mode = 'r')

	t = jf.new_trans()

	try:
		t.add_w(c, 80)
	except IOError:
		pass
	else:
		raise AssertionError

	try:
		# note this fails because there are no ops to commit
		t.commit()
	except IOError:
		pass
	else:
		raise AssertionError

	cleanup(n)

def test_n17():
	"move journal to an existing dir"
	import os

	f, jf = bitmp()
	n = f.name
	p = tmppath()
	os.mkdir(p)
	open(p + '/x', 'w')

	jf.write('x')
	jf.jmove_journal(p)
	jf.write('y')
	del jf
	os.unlink(p + '/x')

	assert libjio.jfsck(n, p)['total'] == 0
	os.unlink(n)

def test_n18():
	"jtrans_rollback with norollback"
	c = gencontent()
	f, jf = bitmp(jflags = libjio.J_NOROLLBACK)
	n = f.name

	t = jf.new_trans()
	t.add_w(c, 80)
	t.commit()
	try:
		t.rollback()
	except IOError:
		pass
	else:
		raise AssertionError

	assert content(n) == '\0' * 80 + c
	fsck_verify(n)
	cleanup(n)

def test_n19():
	"jwrite in files opened with O_APPEND"
	c1 = gencontent()
	c2 = gencontent()
	f, jf = bitmp(mode = 'a')
	n = f.name

	jf.write(c1)
	jf.write(c2)

	assert content(n) == c1 + c2
	fsck_verify(n)
	cleanup(n)

def test_n20():
	"jtrans_add_w of 0 length"
	f, jf = bitmp()
	n = f.name

	t = jf.new_trans()

	try:
		t.add_w('', 80)
	except IOError:
		pass
	else:
		raise AssertionError

	del t
	del jf
	fsck_verify(n)
	cleanup(n)

def test_n21():
	"jwritev and jreadv"
	f, jf = bitmp()
	n = f.name

	jf.writev(["hello ", "world"])
	l = [bytearray("......"), bytearray(".....")]
	jf.lseek(0, 0)
	jf.readv(l)

	assert content(n) == "hello world"
	assert l[0] == "hello " and l[1] == "world"
	fsck_verify(n)
	cleanup(n)

def test_n22():
	"jpread/jpwrite ~2mb"
	c = gencontent(2 * 1024 * 1024 + 1465)

	f, jf = bitmp(jflags = libjio.J_LINGER)
	n = f.name

	jf.pwrite(c, 2000)
	assert content(n) == '\0' * 2000 + c
	assert jf.pread(len(c), 2000) == c
	del jf

	fsck_verify(n)
	cleanup(n)

def test_n23():
	"jtrans_add_w + jtrans_add_r"
	f, jf = bitmp()
	n = f.name

	c1 = gencontent(1000)
	c2 = gencontent(2000)
	c3 = gencontent(3000)

	buf1 = bytearray(0 for i in range(30))
	buf2 = bytearray(0 for i in range(100))

	t = jf.new_trans()
	t.add_w(c1, 0)
	t.add_r(buf1, 0)
	t.add_w(c2, len(c1))
	t.add_r(buf2, len(c1) - len(buf2) / 2)
	t.add_w(c3, len(c1) + len(c2))
	t.commit()

	assert content(n) == c1 + c2 + c3
	assert buf1 == c1[:len(buf1)]
	assert buf2 == c1[-(len(buf2) / 2):] + c2[:len(buf2) / 2]

	del t
	del jf
	fsck_verify(n)
	cleanup(n)


def test_n24():
	"many jtrans_add_w + many jtrans_add_r"
	f, jf = bitmp()
	n = f.name

	# just randomly chosen numbers
	len_c1 = 1293
	len_c2 = 529
	len_c3 = 1621
	block_len = len_c1 + len_c2 + len_c3

	t = jf.new_trans()
	bufs = []
	for i in range(50):
		offset = i * block_len
		buf1 = bytearray(0 for _ in range(30))
		buf2 = bytearray(0 for _ in range(100))
		bufs.append((buf1, buf2))

		# We can't use the bytearray directly because add_w requires
		# an immutable object, so we convert it to a string
		c1 = str(bytearray(i for _ in range(len_c1)))
		c2 = str(bytearray(i for _ in range(len_c2)))
		c3 = str(bytearray(i for _ in range(len_c3)))

		t.add_w(c1, offset)
		t.add_r(buf1, offset)
		t.add_w(c2, offset + len(c1))
		t.add_r(buf2, offset + len(c1) - len(buf2) / 2)
		t.add_w(c3, offset + len(c1) + len(c2))

	t.commit()

	cont = content(n)
	for i in range(50):
		offset = i * block_len
		buf1, buf2 = bufs[i]
		c1 = str(bytearray(i for _ in range(len_c1)))
		c2 = str(bytearray(i for _ in range(len_c2)))
		c3 = str(bytearray(i for _ in range(len_c3)))

		assert cont[offset : offset + block_len] == c1 + c2 + c3
		assert buf1 == c1[:len(buf1)]
		assert buf2 == c1[-(len(buf2) / 2):] + c2[:len(buf2) / 2]

	del t
	del jf
	fsck_verify(n)
	cleanup(n)


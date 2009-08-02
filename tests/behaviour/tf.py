
"""
Our customized testing framework.

While not as sophisticated as the unittest module, it's targeted to our
particular kind of tests.

To that end, it has several simple but useful functions aimed to make tests
more easier to read and write.
"""

import sys
import os
import time
import random
import struct
import libjio


# Useful constants, must match journal.h
DHS = 8		# disk header size
DOHS = 12	# disk op header size
DTS = 8		# disk trailer size


def tmppath():
	"""Returns a temporary path. We could use os.tmpnam() if it didn't
	print a warning, or os.tmpfile() if it allowed us to get its name.
	Since we just need a stupid name, we got our own function. Truly a
	shame. Yes, it's not safe; I know and I don't care."""
	tmpdir = os.environ.get('TMPDIR', '/tmp')
	now = time.time()
	now_s = str(int(now))
	now_f = str((now - int(now)) * 10000)
	now_str = "%s.%s" % (now_s[-5:], now_f[:now_f.find('.')])
	return tmpdir + '/jiotest.%s.%s' % (now_str, os.getpid())


def run_forked(f, *args, **kwargs):
	"""Runs the function in a different process."""
	sys.stdout.flush()
	pid = os.fork()
	if pid == 0:
		# child
		f(*args, **kwargs)
		sys.exit(0)
	else:
		# parent
		id, status = os.waitpid(pid, 0)
		if not os.WIFEXITED(status):
			raise RuntimeError, (id, status)

def forked(f):
	"Decorator that makes the function run in a different process."
	def newf(*args, **kwargs):
		run_forked(f, *args, **kwargs)
	return newf


def gencontent(size = 9377):
	"Generates random content."
	s = ''
	a = "%.20f" % random.random()
	while len(s) < size:
		s += a
	s = s[:size]
	return s

def content(path):
	"Returns the content of the given path."
	f = open(path)
	return f.read()


def biopen(path, mode = 'w+', jflags = 0):
	"Returns (open(path), libjio.open(path))."
	if 'r' in mode:
		flags = os.O_RDONLY
		if '+' in mode:
			flags = os.O_RDWR
	elif 'w' in mode:
		flags = os.O_RDWR
		if '+' in mode:
			flags = flags | os.O_CREAT | os.O_TRUNC
	elif 'a' in mode:
		flags = os.O_RDWR | os.O_APPEND
	else:
		raise RuntimeError

	return open(path, mode), libjio.open(path, flags, 0400, jflags)

def bitmp(mode = 'w+', jflags = 0):
	"Opens a temporary file with biopen()."
	path = tmppath()
	return biopen(path, mode, jflags)


def run_with_tmp(func, jflags = 0):
	"""Runs the given function, that takes a file and a jfile as
	parameters, using a temporary file. Returns the path of the temporary
	file. The function runs in a new process that exits afterwards."""
	f, jf = bitmp(jflags = jflags)
	run_forked(func, f, jf)
	return f.name


def jiodir(path):
	return os.path.dirname(path) + '/.' + os.path.basename(path) + '.jio'

def transpath(path, ntrans):
	jpath = jiodir(path)
	return jpath + '/' + str(ntrans)

def fsck(path, flags = 0):
	"Calls libjio's jfsck()."
	res = libjio.jfsck(path, flags = flags)
	return res

def fsck_verify(n, **kwargs):
	"""Runs fsck(n), and verifies that the fsck result matches the given
	values. The default is to match all elements except total to 0 (total
	is calculated automatically from the sum of the others). Raises an
	AssertionError if the given results were not the ones expected."""
	expected = {
		'invalid': 0,
		'broken': 0,
		'reapplied': 0,
		'corrupt': 0,
		'in_progress': 0,
	}
	expected.update(kwargs)
	expected['total'] = sum(expected.values())
	res = fsck(n, flags = libjio.J_CLEANUP)

	for k in expected:
		if k not in res:
			raise AssertionError, k + ' not in res'
		if res[k] != expected[k]:
			raise AssertionError, k + ' does not match: ' + \
					str(res)

def cleanup(path):
	"""Unlinks the path and its temporary libjio directory. The libjio
	directory must only have the 'lock' file in it."""
	os.unlink(path)
	jpath = jiodir(path)
	if os.path.isdir(jpath):
		assert 'lock' in os.listdir(jpath)
		os.unlink(jpath + '/lock')
		os.rmdir(jpath)


class attrdict (dict):
	def __getattr__(self, name):
		return self[name]

	def __setattr__(self, name, value):
		self[name] = value

	def __delattr__(self, name):
		del self[name]

class TransFile (object):
	def __init__(self, path = ''):
		self.ver = 1
		self.id = -1
		self.flags = 0
		self.numops = -1
		self.checksum = -1
		self.ops = []
		self.path = path
		if path:
			self.load()

	def load(self):
		fd = open(self.path)

		# header
		hdrfmt = "!HHI"
		self.ver, self.flags, self.id = struct.unpack(hdrfmt,
				fd.read(struct.calcsize(hdrfmt)))

		# operations (header only)
		opfmt = "!IQ"
		self.ops = []
		while True:
			tlen, offset = struct.unpack(opfmt,
					fd.read(struct.calcsize(opfmt)))
			if tlen == offset == 0:
				break
			payload = fd.read(tlen)
			assert len(payload) == tlen
			self.ops.append(attrdict(tlen = tlen, offset = offset,
				payload = payload))

		# trailer
		trailerfmt = "!II"
		self.numops, self.checksum = struct.unpack(trailerfmt,
				fd.read(struct.calcsize(trailerfmt)))

	def save(self):
		# the lack of integrity checking in this function is
		# intentional, so we can write broken transactions and see how
		# jfsck() copes with them
		fd = open(self.path, 'w')
		fd.write(struct.pack("!HHI", self.ver, self.flags, self.id))
		for o in self.ops:
			fd.write(struct.pack("!IQ", o.tlen, o.offset,))
			fd.write(o.payload)
		fd.write(struct.pack("!IQ", 0, 0))
		fd.write(struct.pack("!II", self.numops, self.checksum))

	def __repr__(self):
		return '<TransFile %s: id:%d f:%s n:%d ops:%s>' % \
			(self.path, self.id, hex(self.flags), self.numops,
					self.ops)


def gen_ret_seq(seq):
	"""Returns a function that each time it is called returns a value of
	the given sequence, in order. When the sequence is exhausted, returns
	the last value."""
	it = iter(seq)
	last = [0]
	def newf(*args, **kwargs):
		try:
			r = it.next()
			last[0] = r
			return r
		except StopIteration:
			return last[0]
	return newf

def autorun(module, specific_test = None):
	"Runs all the functions in the given module that begin with 'test'."
	for name in sorted(dir(module)):
		if not name.startswith('test'):
			continue

		obj = getattr(module, name)
		if '__call__' in dir(obj):
			name = name[len('test'):]
			if name.startswith('_'):
				name = name[1:]

			if specific_test and name != specific_test:
				continue

			desc = ''
			if obj.__doc__:
				desc = obj.__doc__
			print "%-10s %-60.60s" % (name, desc)
			obj()



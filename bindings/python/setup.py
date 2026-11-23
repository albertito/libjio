
try:
	from setuptools import setup, Extension
except ImportError:
	from distutils.core import setup, Extension

libjio = Extension("libjio",
		libraries = ['jio'],
		sources = ['libjio.c'],

		# these two allow us to build without having libjio installed,
		# assuming we're in the libjio source tree
		include_dirs = ['../../libjio/'],
		library_dirs=['../../libjio/build/']
	)

setup(
	name = 'libjio',
	version = '1.02',
	description = "A library for journaled, transactional I/O",
	author = "Alberto Bertogli",
	author_email = "albertito@blitiri.com.ar",
	url = "http://blitiri.com.ar/p/libjio",
	ext_modules = [libjio],
	classifiers = [
		"License :: Public Domain",
		"Operating System :: POSIX",
		"Programming Language :: C",
		"Programming Language :: Python",
		"Programming Language :: Python :: 3",
		"Topic :: Software Development",
		"Topic :: Software Development :: Libraries",
	],
)



import sys
from distutils.core import setup, Extension

if sys.version_info[0] == 2:
	ver_define = ('PYTHON2', '1')
elif sys.version_info[0] == 3:
	ver_define = ('PYTHON3', '1')

libjio = Extension("libjio",
		libraries = ['jio'],
		sources = ['libjio.c'],
		define_macros = [ver_define],

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
		"Programming Language :: Python :: 2",
		"Programming Language :: Python :: 3",
		"Topic :: Software Development",
		"Topic :: Software Development :: Libraries",
	],
)


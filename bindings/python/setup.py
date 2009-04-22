
import sys
from distutils.core import setup, Extension

if sys.version_info[0] == 2:
	ver_define = ('PYTHON2', '1')
elif sys.version_info[0] == 3:
	ver_define = ('PYTHON3', '1')

libjio = Extension("libjio",
		libraries = ['jio'],
		sources = ['libjio.c'],
		define_macros = [ver_define] )

setup(
	name = 'libjio',
	description = "A library for journaled I/O",
	author="Alberto Bertogli",
	author_email="albertito@blitiri.com.ar",
	url="http://blitiri.com.ar/p/libjio",
	ext_modules = [libjio]
)


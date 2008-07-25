
from distutils.core import setup, Extension

libjio = Extension("libjio",
		libraries = ['jio'],
		sources = ['libjio.c'])

setup(
	name = 'libjio',
	description = "A library for journaled I/O",
	author="Alberto Bertogli",
	author_email="albertito@blitiri.com.ar",
	url="http://blitiri.com.ar/p/libjio",
	ext_modules = [libjio]
)


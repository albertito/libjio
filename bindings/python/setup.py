
from distutils.core import setup, Extension

libjio = Extension("libjio",
		libraries = ['jio'],
		sources = ['libjio.c'])

setup(
	name = 'libjio',
	description = "A library for journaled I/O",
	author="Alberto Bertogli",
	author_email="albertogli@telpin.com.ar",
	url="http://users.auriga.wearlab.de/~alb/libjio",
	ext_modules = [libjio]
)


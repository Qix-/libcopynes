AM_CPPFLAGS = \
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" 

AM_CFLAGS =\
	 -Wall\
	 -g

lib_LTLIBRARIES = \
	libcopynes.la

libcopynes_la_SOURCES = \
	copynes.c

include_HEADERS = \
	copynes.h

libcopynes_la_LDFLAGS = -version-info 0:0:1 $(all_libraries)

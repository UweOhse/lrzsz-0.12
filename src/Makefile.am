bin_PROGRAMS=lrz lsz
lrz_SOURCES=lrz.c timing.c zperr.c zreadline.c crctab.c rbsb.c zm.c protname.c
lsz_SOURCES=lsz.c timing.c zperr.c zreadline.c crctab.c rbsb.c zm.c protname.c
noinst_HEADERS = timing.h zglobal.h zmodem.h
datadir = $(prefix)/@DATADIRNAME@
localedir = $(datadir)/locale

LDADD = ../lib/libzmodem.a @INTLLIBS@
AUTOMAKE_OPTIONS=ansi2knr
EXTRA_DIST = ansi2knr.1 ansi2knr.c
INCLUDES = -I.. -I$(srcdir) -I$(top_srcdir)/src -I../intl -I$(top_srcdir)/lib
DEFS = -DLOCALEDIR=\"$(localedir)\" -DOS=\"@host_os@\" -DCPU=\"@host_cpu@\" \
	-DNFGVMIN @DEFS@

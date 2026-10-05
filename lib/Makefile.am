noinst_LIBRARIES=zmodem
zmodem_LIBADD=@LIBOBJS@ @ALLOCA@
AUTOMAKE_OPTIONS=ansi2knr
CFLAGS=@CFLAGS@

## Process this file with automake to produce Makefile.in
noinst_LIBRARIES = zmodem

EXTRA_DIST = alloca.c ansi2knr.1 ansi2knr.c \
	getopt.c getopt1.c mkdir.c mktime.c \
	strdup.c strerror.c strstr.c strpbrk.c strtol.c strtoul.c stpcpy.c

zmodem_SOURCES = long-options.c xstrtol.c xstrtoul.c error.c

zmodem_LIBADD = @LIBOBJS@ @ALLOCA@

noinst_HEADERS = getopt.h long-options.h xstrtol.h xstrtoul.h error.h


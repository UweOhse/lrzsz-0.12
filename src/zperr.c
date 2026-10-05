#include "zglobal.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#if __STDC__
#  include <stdarg.h>
#  define VA_START(args, lastarg) va_start(args, lastarg)
#else
#  include <varargs.h>
#  define VA_START(args, lastarg) va_start(args)
#endif

void
zperr(const char *fmt, ...)
{
    va_list ap;

	if (Verbose<=0)
		return;
	fprintf(stderr,_("Retry %d: "),errors);
    VA_START(ap, fmt);
    vfprintf(stderr,fmt, ap);
    va_end(ap);
    putc('\n',stderr);
}

void
zpfatal(const char *fmt, ...)
{
    va_list ap;
    int err=errno;

	if (Verbose<=0)
		return;
	fprintf(stderr,"%s: ",program_name);
    VA_START(ap, fmt);
    vfprintf(stderr,fmt, ap);
    va_end(ap);
	fprintf(stderr,": %s\n",strerror(err));
}

void 
vfile(const char *format, ...)
{
    va_list ap;

	if (Verbose < 3)
		return;
    VA_START(ap, format);
    vfprintf(stderr,format, ap);
    va_end(ap);
    putc('\n',stderr);
}

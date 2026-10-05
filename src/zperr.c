#include "zglobal.h"
#include <stdio.h>
#include <stdlib.h>

void
zperr(const char *fmt, ...)
{
    va_list ap;

	if (Verbose<=0)
		return;
	fprintf(stderr,_("Retry %d: "),errors);
    va_start(ap, fmt);
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
    va_start(ap, fmt);
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
    va_start(ap, format);
    vfprintf(stderr,format, ap);
    va_end(ap);
    putc('\n',stderr);
}

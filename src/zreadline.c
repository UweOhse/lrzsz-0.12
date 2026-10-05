/* lrz.c cosmetic modifications by Matt Porter
 * from rz.c By Chuck Forsberg
 * 
 *  A program for Linux to receive files and commands from computers running
 *  zmodem, ymodem, or xmodem protocols.
 *  lrz uses Unix buffered input to reduce wasted CPU time.
 *
 */

#include "zglobal.h"

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>

#include "error.h"


/* Ward Christensen / CP/M parameters - Don't change these! */
#define TIMEOUT (-2)

static int readline_readnum;
static int readline_fd;
static char *readline_buffer;
int readline_left=0;
char *readline_ptr;

static RETSIGTYPE
zreadline_alarm_handler(int dummy)
{
	/* doesn't need to do anything */
}

/*
 * This version of readline is reasonably well suited for
 * reading many characters.
 *
 * timeout is in tenths of seconds
 */
int 
readline(int timeout)
{
	register n;
#ifndef READLINE_PF
	static char *readline_ptr;	/* pointer for removing chars from linbuf */
#endif
	if (--readline_left >= 0) {
		if (Verbose > 8) {
			fprintf(stderr, "%02x ", *readline_ptr&0377);
		}
		return (*readline_ptr++ & 0377);
	}

	if (!no_timeout)
	{
		n = timeout/10;
		if (n < 2 && timeout!=1)
			n = 3;
		else if (n==0)
			n=1;
		if (Verbose > 5)
			fprintf(stderr, "Calling read: alarm=%d  Readnum=%d ",
			  n, readline_readnum);
		signal(SIGALRM, zreadline_alarm_handler); alarm(n);
	}
	else if (Verbose > 5)
		fprintf(stderr, "Calling read: Readnum=%d ",
		  readline_readnum);

	readline_ptr=readline_buffer;
	readline_left=read(readline_fd, readline_ptr, readline_readnum);
	if (!no_timeout)
		alarm(0);
	if (readline_left>0 && bytes_per_error) {
		static long ct=0;
		static int mod=1;
		ct+=readline_left;
		while (ct>bytes_per_error) {
			readline_ptr[ct % bytes_per_error]^=mod;
			ct-=bytes_per_error;
			mod++;
			if (mod==256)
				mod=1;
		}
	}
	if (Verbose > 5) {
		fprintf(stderr, "Read returned %d bytes\n", readline_left);
		if (readline_left==-1)
			fprintf(stderr, "errno=%d:%s\n", errno,strerror(errno));
		if (Verbose > 9 && readline_left>0) {
			int i,j;
			j=readline_left > 48 ? 48 : readline_left;
			fprintf(stderr,"    ");
			for (i=0;i<j;i++) {
				if (i==24)
					fprintf(stderr,"\n    ");
				fprintf(stderr, "%02x ", readline_ptr[i] & 0377);
			}
			fprintf(stderr,"\n");
		}
	}
	if (readline_left < 1)
		return TIMEOUT;
	--readline_left;
	if (Verbose > 8) {
		fprintf(stderr, "%02x ", *readline_ptr&0377);
	}
	return (*readline_ptr++ & 0377);
}



void
readline_setup(int fd, int readnum, int bufsize)
{
	readline_fd=fd;
	readline_readnum=readnum;
	readline_buffer=malloc(bufsize > readnum ? bufsize : readnum);
	if (!readline_buffer)
		error(1,0,_("out of memory"));
}

void
readline_purge(void)
{
	readline_left=0;
	return;
}


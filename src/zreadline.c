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
		if (n < 2)
			n = 3;
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
	if (Verbose > 5) {
		fprintf(stderr, "Read returned %d bytes\n", readline_left);
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


/*
 *   Z M . C
 *    ZMODEM protocol primitives
 *    05-09-88  Chuck Forsberg Omen Technology Inc
 *
 * Entry point Functions:
 *	zsbhdr(type, hdr) send binary header
 *	zshhdr(type, hdr) send hex header
 *	zgethdr(hdr, eflag) receive header - binary or hex
 *	zsdata(buf, len, frameend) send data
 *	zrdata(buf, len) receive data
 *	stohdr(pos) store position data in Txhdr
 *	long rclhdr(hdr) recover position offset from header
 */


#include "zglobal.h"

#include <stdio.h>
#include <unistd.h>

int Rxtimeout = 100;		/* Tenths of seconds to wait for something */

/* Globals used by ZMODEM functions */
int Rxframeind;		/* ZBIN ZBIN32, or ZHEX type of frame received */
int Rxtype;		/* Type of header received */
int Rxcount;		/* Count of data bytes received */
char Rxhdr[4];		/* Received header */
char Txhdr[4];		/* Transmitted header */
long Rxpos;		/* Received file position */
long Txpos;		/* Transmitted file position */
int Txfcs32;		/* TRUE means send binary frames with 32 bit FCS */
int Crc32t;		/* Display flag indicating 32 bit CRC being sent */
int Crc32;		/* Display flag indicating 32 bit CRC being received */
int Znulls;		/* Number of nulls to send at beginning of ZDATA hdr */
char Attn[ZATTNLEN+1];	/* Attention string rx sends to tx on err */

static lastsent;	/* Last char we sent */
static Not8bit;		/* Seven bits seen on header */
int turbo_escape;
int bytes_per_error=0;

static const char *frametypes[] = {
	"Carrier Lost",		/* -3 */
	"TIMEOUT",		/* -2 */
	"ERROR",		/* -1 */
#define FTOFFSET 3
	"ZRQINIT",
	"ZRINIT",
	"ZSINIT",
	"ZACK",
	"ZFILE",
	"ZSKIP",
	"ZNAK",
	"ZABORT",
	"ZFIN",
	"ZRPOS",
	"ZDATA",
	"ZEOF",
	"ZFERR",
	"ZCRC",
	"ZCHALLENGE",
	"ZCOMPL",
	"ZCAN",
	"ZFREECNT",
	"ZCOMMAND",
	"ZSTDERR",
	"xxxxx"
#define FRTYPES 22	/* Total number of frame types in this array */
			/*  not including psuedo negative entries */
};

#define badcrc _("Bad CRC")
/* static char *badcrc = "Bad CRC"; */
static inline int noxrd7(void);
static int inline zdlread(void);
static int zdlread2(int);
static inline int zgeth1(void);
static void zputhex(int c, char *pos);
static int zgethex(void);
static int zrbhdr(char *hdr);
static int zrbhdr32(char *hdr);
static int zrhhdr(char *hdr);
static void zsendline_init(char *);
static int zrdat32(char *buf, int length);
static void zsda32(const char *buf, int length, int frameend);
static void zsbh32(char *hdr, int type);

extern int zmodem_requested;

#define sendline(c) putchar((c) & 0377)
#define xsendline(c) putchar(c)

/*
 * Read a byte, checking for ZMODEM escape encoding
 *  including CAN*5 which represents a quick abort
 */
static inline int
zdlread(void)
{
	int c;
	/* Quick check for non control characters */
	if ((c = READLINE_PF(Rxtimeout)) & 0140)
		return c;
	return zdlread2(c);
}
/* no, i don't like gotos. -- uwe */
static int
zdlread2(int c)
{
	goto jump_over;

again:
	/* Quick check for non control characters */
	if ((c = READLINE_PF(Rxtimeout)) & 0140)
		return c;
jump_over:
	switch (c) {
	case ZDLE:
		break;
	case XON:
	case (XON|0200):
	case XOFF:
	case (XOFF|0200):
		goto again;
	default:
		if (Zctlesc && !(c & 0140)) {
			goto again;
		}
		return c;
	}
again2:
	if ((c = READLINE_PF(Rxtimeout)) < 0)
		return c;
	if (c == CAN && (c = READLINE_PF(Rxtimeout)) < 0)
		return c;
	if (c == CAN && (c = READLINE_PF(Rxtimeout)) < 0)
		return c;
	if (c == CAN && (c = READLINE_PF(Rxtimeout)) < 0)
		return c;
	switch (c) {
	case CAN:
		return GOTCAN;
	case ZCRCE:
	case ZCRCG:
	case ZCRCQ:
	case ZCRCW:
		return (c | GOTOR);
	case ZRUB0:
		return 0177;
	case ZRUB1:
		return 0377;
	case XON:
	case (XON|0200):
	case XOFF:
	case (XOFF|0200):
		goto again2;
	default:
		if (Zctlesc && ! (c & 0140)) {
			goto again2;
		}
		if ((c & 0140) ==  0100)
			return (c ^ 0100);
		break;
	}
	if (Verbose>1)
		zperr(_("Bad escape sequence %x"), c);
	return ERROR;
}


/*
 * Read a character from the modem line with timeout.
 *  Eat parity, XON and XOFF characters.
 */
static inline int
noxrd7(void)
{
	register int c;

	for (;;) {
		if ((c = readline(Rxtimeout)) < 0)
			return c;
		switch (c &= 0177) {
		case XON:
		case XOFF:
			continue;
		default:
			if (Zctlesc && !(c & 0140))
				continue;
		case '\r':
		case '\n':
		case ZDLE:
			return c;
		}
	}
}

/*
 * Send character c with ZMODEM escape sequence encoding.
 *  Escape XON, XOFF. Escape CR following @ (Telenet net escape)
 */
void inline
zsendline(int c)
{
	static int last_esc=-2;
	static char tab[256];
	if (Zctlesc!=last_esc) {
		zsendline_init(tab);
		last_esc=Zctlesc;
	}

	switch(tab[(unsigned) (c&=0377)])
	{
	case 0: 
		xsendline(lastsent = c); 
		break;
	case 1:
		xsendline(ZDLE);
		c ^= 0100;
		xsendline(lastsent = c);
		break;
	case 2:
		if ((lastsent & 0177) != '@') {
			xsendline(lastsent = c);
		} else {
			xsendline(ZDLE);
			c ^= 0100;
			xsendline(lastsent = c);
		}
		break;
	}
}

/* Send ZMODEM binary header hdr of type type */
void 
zsbhdr(int type, char *hdr)
{
	register int n;
	register unsigned short crc;

	vfile("zsbhdr: %s %lx", frametypes[type+FTOFFSET], rclhdr(hdr));
	if (type == ZDATA)
		for (n = Znulls; --n >=0; )
			xsendline(0);

	xsendline(ZPAD); xsendline(ZDLE);

	Crc32t=Txfcs32;
	if (Crc32t)
		zsbh32(hdr, type);
	else {
		xsendline(ZBIN); zsendline(type); crc = updcrc(type, 0);

		for (n=4; --n >= 0; ++hdr) {
			zsendline(*hdr);
			crc = updcrc((0377& *hdr), crc);
		}
		crc = updcrc(0,updcrc(0,crc));
		zsendline(crc>>8);
		zsendline(crc);
	}
	if (type != ZDATA)
		flushmo();
}


/* Send ZMODEM binary header hdr of type type */
void
zsbh32(char *hdr, int type)
{
	register int n;
	register unsigned long crc;

	xsendline(ZBIN32);  zsendline(type);
	crc = 0xFFFFFFFFL; crc = UPDC32(type, crc);

	for (n=4; --n >= 0; ++hdr) {
		crc = UPDC32((0377 & *hdr), crc);
		zsendline(*hdr);
	}
	crc = ~crc;
	for (n=4; --n >= 0;) {
		zsendline((int)crc);
		crc >>= 8;
	}
}

/* Send ZMODEM HEX header hdr of type type */
void 
zshhdr(int type, char *hdr)
{
	register int n;
	register unsigned short crc;
	char s[30];
	size_t len;

	vfile("zshhdr: %s %lx", frametypes[(type & 0x7f)+FTOFFSET], rclhdr(hdr));
	s[0]=ZPAD;
	s[1]=ZPAD;
	s[2]=ZDLE;
	s[3]=ZHEX;
	zputhex(type & 0x7f ,s+4);
	len=6;
	Crc32t = 0;

	crc = updcrc((type & 0x7f), 0);
	for (n=4; --n >= 0; ++hdr) {
		zputhex(*hdr,s+len); 
		len += 2;
		crc = updcrc((0377 & *hdr), crc);
	}
	crc = updcrc(0,updcrc(0,crc));
	zputhex(crc>>8,s+len); 
	zputhex(crc,s+len+2);
	len+=4;

	/* Make it printable on remote machine */
	s[len++]=015;
	s[len++]=0212;
	/*
	 * Uncork the remote in case a fake XOFF has stopped data flow
	 */
	if (type != ZFIN && type != ZACK)
	{
		s[len++]=021;
	}
	flushmo();
	write(1,s,len);
}

/*
 * Send binary array buf of length length, with ending ZDLE sequence frameend
 */
static const char *Zendnames[] = { "ZCRCE", "ZCRCG", "ZCRCQ", "ZCRCW"};
void 
zsdata(const char *buf, int length, int frameend)
{
	register unsigned short crc;

	vfile("zsdata: %d %s", length, Zendnames[(frameend-ZCRCE)&3]);
	if (Crc32t)
		zsda32(buf, length, frameend);
	else {
		crc = 0;
		for (;--length >= 0; ++buf) {
			zsendline(*buf); crc = updcrc((0377 & *buf), crc);
		}
		xsendline(ZDLE); xsendline(frameend);
		crc = updcrc(frameend, crc);

		crc = updcrc(0,updcrc(0,crc));
		zsendline(crc>>8); zsendline(crc);
	}
	if (frameend == ZCRCW) {
		xsendline(XON);  flushmo();
	}
}

void
zsda32(const char *buf, int length, int frameend)
{
	register int c;
	register unsigned long crc;

	crc = 0xFFFFFFFFL;
	for (;--length >= 0; ++buf) {
		c = *buf & 0377;
		if (c & 0140)
			xsendline(lastsent = c);
		else
			zsendline(c);
		crc = UPDC32(c, crc);
	}
	xsendline(ZDLE); xsendline(frameend);
	crc = UPDC32(frameend, crc);

	crc = ~crc;
	for (length=4; --length >= 0;) {
		c=(int) crc;
		if (c & 0140)
			xsendline(lastsent = c);
		else
			zsendline(c);
		crc >>= 8;
	}
}

#if __GNUC__ < 2 || (__GNUC__ == 2 && __GNUC_MINOR__ <= 4)
#  undef DEBUG_BLOCKSIZE
#endif

#ifdef DEBUG_BLOCKSIZE
struct debug_blocksize {
	int size;
	long count;
};
struct debug_blocksize blocksizes[]={
	{32,0},
	{64,0},
	{128,0},
	{256,0},
	{512,0},
	{1024,0},
	{2048,0},
	{4096,0},
	{8192,0},
	{0,0}
};
static inline void
count_blk(int size)
{
	int i;
	for (i=0;blocksizes[i].size;i++) {
		if (blocksizes[i].size==size) {
			blocksizes[i].count++;
			return;
		}
	}
	blocksizes[i].count++;
}

static void printout_blocksizes(void) __attribute__((__destructor__));
#include <syslog.h>
static void 
printout_blocksizes(void) 
{
	int i;
	for (i=0;blocksizes[i].size;i++) {
		if (blocksizes[i].count) {
			syslog(LOG_DEBUG,"%4d byte: %ld blocks\n",
				   blocksizes[i].size,blocksizes[i].count);
		}
	}
	if (blocksizes[i].count) {
		syslog(LOG_DEBUG,"unk. byte: %ld blocks",
			   blocksizes[i].count);
	}
}
#define COUNT_BLK(x) count_blk(x)
#else
#define COUNT_BLK(x)
#endif

/*
 * Receive array buf of max length with ending ZDLE sequence
 *  and CRC.  Returns the ending character or error code.
 *  NB: On errors may store length+1 bytes!
 */
int
zrdata(char *buf, int length)
{
	register int c;
	register unsigned short crc;
	register char *end;
	register int d;

	if (Rxframeind == ZBIN32)
		return zrdat32(buf, length);

	crc = Rxcount = 0;  end = buf + length;
	while (buf <= end) {
		if ((c = zdlread()) & ~0377) {
crcfoo:
			switch (c) {
			case GOTCRCE:
			case GOTCRCG:
			case GOTCRCQ:
			case GOTCRCW:
				{ 
					d = c;
					c &= 0377;
					crc = updcrc(c, crc);
					if ((c = zdlread()) & ~0377)
						goto crcfoo;
					crc = updcrc(c, crc);
					if ((c = zdlread()) & ~0377)
						goto crcfoo;
					crc = updcrc(c, crc);
					if (crc & 0xFFFF) {
						zperr(badcrc);
						return ERROR;
					}
					Rxcount = length - (end - buf);
					COUNT_BLK(Rxcount);
					vfile("zrdata: %d  %s", Rxcount, Zendnames[(d-GOTCRCE)&3]);
					return d;
				}
			case GOTCAN:
				zperr(_("Sender Canceled"));
				return ZCAN;
			case TIMEOUT:
				zperr(_("TIMEOUT"));
				return c;
			default:
				zperr(_("Bad data subpacket"));
				return c;
			}
		}
		*buf++ = c;
		crc = updcrc(c, crc);
	}
	zperr(_("Data subpacket too long"));
	return ERROR;
}

int
zrdat32(char *buf, int length)
{
	register int c;
	register unsigned long crc;
	register char *end;
	register int d;


	crc = 0xFFFFFFFFL;  Rxcount = 0;  end = buf + length;
	while (buf <= end) {
		if ((c = zdlread()) & ~0377) {
crcfoo:
			switch (c) {
			case GOTCRCE:
			case GOTCRCG:
			case GOTCRCQ:
			case GOTCRCW:
				d = c;
				c &= 0377;
				crc = UPDC32(c, crc);
				if ((c = zdlread()) & ~0377)
					goto crcfoo;
				crc = UPDC32(c, crc);
				if ((c = zdlread()) & ~0377)
					goto crcfoo;
				crc = UPDC32(c, crc);
				if ((c = zdlread()) & ~0377)
					goto crcfoo;
				crc = UPDC32(c, crc);
				if ((c = zdlread()) & ~0377)
					goto crcfoo;
				crc = UPDC32(c, crc);
				if (crc != 0xDEBB20E3) {
					zperr(badcrc);
					return ERROR;
				}
				Rxcount = length - (end - buf);
				COUNT_BLK(Rxcount);
				vfile("zrdat32: %d %s", Rxcount, Zendnames[(d-GOTCRCE)&3]);
				return d;
			case GOTCAN:
				zperr(_("Sender Canceled"));
				return ZCAN;
			case TIMEOUT:
				zperr(_("TIMEOUT"));
				return c;
			default:
				zperr(_("Bad data subpacket"));
				return c;
			}
		}
		*buf++ = c;
		crc = UPDC32(c, crc);
	}
	zperr(_("Data subpacket too long"));
	return ERROR;
}

/* Local screen character display function */
static void 
bttyout(int c)
{
	if (Verbose)
		putc(c, stderr);
}


/*
 * Read a ZMODEM header to hdr, either binary or hex.
 *  eflag controls local display of non zmodem characters:
 *	0:  no display
 *	1:  display printing characters only
 *	2:  display all non ZMODEM characters
 *  On success, set Zmodem to 1, set Rxpos and return type of header.
 *   Otherwise return negative on error.
 *   Return ERROR instantly if ZCRCW sequence, for fast error recovery.
 */
int
zgethdr(char *hdr, int eflag)
{
	register int c, n, cancount;

	n = Zrwindow + Baudrate;	/* Max bytes before start of frame */
	Rxframeind = Rxtype = 0;

startover:
	cancount = 5;
again:
	/* Return immediate ERROR if ZCRCW sequence seen */
	switch (c = readline(Rxtimeout)) {
	case RCDO:
	case TIMEOUT:
		goto fifi;
	case CAN:
gotcan:
		if (--cancount <= 0) {
			c = ZCAN; goto fifi;
		}
		switch (c = readline(1)) {
		case TIMEOUT:
			goto again;
		case ZCRCW:
			c = ERROR;
		/* **** FALL THRU TO **** */
		case RCDO:
			goto fifi;
		default:
			break;
		case CAN:
			if (--cancount <= 0) {
				c = ZCAN; goto fifi;
			}
			goto again;
		}
	/* **** FALL THRU TO **** */
	default:
agn2:
		if ( --n == 0) {
			zperr(_("Garbage count exceeded"));
			return(ERROR);
		}
		if (eflag && ((c &= 0177) & 0140))
			bttyout(c);
		else if (eflag > 1)
			bttyout(c);
#ifdef UNIX
		fflush(stderr);
#endif
		goto startover;
	case ZPAD|0200:		/* This is what we want. */
		Not8bit = c;
	case ZPAD:		/* This is what we want. */
		break;
	}
	cancount = 5;
splat:
	switch (c = noxrd7()) {
	case ZPAD:
		goto splat;
	case RCDO:
	case TIMEOUT:
		goto fifi;
	default:
		goto agn2;
	case ZDLE:		/* This is what we want. */
		break;
	}

	switch (c = noxrd7()) {
	case RCDO:
	case TIMEOUT:
		goto fifi;
	case ZBIN:
		Rxframeind = ZBIN;  Crc32 = FALSE;
		c =  zrbhdr(hdr);
		break;
	case ZBIN32:
		Crc32 = Rxframeind = ZBIN32;
		c =  zrbhdr32(hdr);
		break;
	case ZHEX:
		Rxframeind = ZHEX;  Crc32 = FALSE;
		c =  zrhhdr(hdr);
		break;
	case CAN:
		goto gotcan;
	default:
		goto agn2;
	}
	Rxpos = hdr[ZP3] & 0377;
	Rxpos = (Rxpos<<8) + (hdr[ZP2] & 0377);
	Rxpos = (Rxpos<<8) + (hdr[ZP1] & 0377);
	Rxpos = (Rxpos<<8) + (hdr[ZP0] & 0377);
fifi:
	switch (c) {
	case GOTCAN:
		c = ZCAN;
	/* **** FALL THRU TO **** */
	case ZNAK:
	case ZCAN:
	case ERROR:
	case TIMEOUT:
	case RCDO:
		zperr(_("Got %s"), frametypes[c+FTOFFSET]);
	/* **** FALL THRU TO **** */
	default:
		if (c >= -3 && c <= FRTYPES)
			vfile("zgethdr: %s %lx", frametypes[c+FTOFFSET], Rxpos);
		else
			vfile("zgethdr: %d %lx", c, Rxpos);
	}
	return c;
}

/* Receive a binary style header (type and position) */
int 
zrbhdr(char *hdr)
{
	register int c, n;
	register unsigned short crc;

	if ((c = zdlread()) & ~0377)
		return c;
	Rxtype = c;
	crc = updcrc(c, 0);

	for (n=4; --n >= 0; ++hdr) {
		if ((c = zdlread()) & ~0377)
			return c;
		crc = updcrc(c, crc);
		*hdr = c;
	}
	if ((c = zdlread()) & ~0377)
		return c;
	crc = updcrc(c, crc);
	if ((c = zdlread()) & ~0377)
		return c;
	crc = updcrc(c, crc);
	if (crc & 0xFFFF) {
		zperr(badcrc); 
		return ERROR;
	}
	protocol = ZM_ZMODEM;
	zmodem_requested=TRUE;
	return Rxtype;
}

/* Receive a binary style header (type and position) with 32 bit FCS */
int
zrbhdr32(char *hdr)
{
	register int c, n;
	register unsigned long crc;

	if ((c = zdlread()) & ~0377)
		return c;
	Rxtype = c;
	crc = 0xFFFFFFFFL; crc = UPDC32(c, crc);
#ifdef DEBUGZ
	vfile("zrbhdr32 c=%X  crc=%lX", c, crc);
#endif

	for (n=4; --n >= 0; ++hdr) {
		if ((c = zdlread()) & ~0377)
			return c;
		crc = UPDC32(c, crc);
		*hdr = c;
#ifdef DEBUGZ
		vfile("zrbhdr32 c=%X  crc=%lX", c, crc);
#endif
	}
	for (n=4; --n >= 0;) {
		if ((c = zdlread()) & ~0377)
			return c;
		crc = UPDC32(c, crc);
#ifdef DEBUGZ
		vfile("zrbhdr32 c=%X  crc=%lX", c, crc);
#endif
	}
	if (crc != 0xDEBB20E3) {
		zperr(badcrc);
		return ERROR;
	}
	protocol = ZM_ZMODEM;
	zmodem_requested=TRUE;
	return Rxtype;
}


/* Receive a hex style header (type and position) */
int 
zrhhdr(char *hdr)
{
	register int c;
	register unsigned short crc;
	register int n;

	if ((c = zgethex()) < 0)
		return c;
	Rxtype = c;
	crc = updcrc(c, 0);

	for (n=4; --n >= 0; ++hdr) {
		if ((c = zgethex()) < 0)
			return c;
		crc = updcrc(c, crc);
		*hdr = c;
	}
	if ((c = zgethex()) < 0)
		return c;
	crc = updcrc(c, crc);
	if ((c = zgethex()) < 0)
		return c;
	crc = updcrc(c, crc);
	if (crc & 0xFFFF) {
		zperr(badcrc); return ERROR;
	}
	switch ( c = readline(1)) {
	case 0215:
		Not8bit = c;
		/* **** FALL THRU TO **** */
	case 015:
	 	/* Throw away possible cr/lf */
		switch (c = readline(1)) {
		case 012:
			Not8bit |= c;
		}
	}
	protocol = ZM_ZMODEM;
	zmodem_requested=TRUE;
	return Rxtype;
}

/* Write a byte as two hex digits */
void 
zputhex(int c, char *pos)
{
	static char	digits[]	= "0123456789abcdef";

	if (Verbose>8)
		vfile("zputhex: %02X", c);
	pos[0]=digits[(c&0xF0)>>4];
	pos[1]=digits[c&0x0F];
}

static void
zsendline_init(char *tab)
{
	int i;
	for (i=0;i<256;i++) {	
		if (i & 0140)
			tab[i]=0;
		else {
			switch(i)
			{
			case ZDLE:
			case XOFF: /* ^Q */
			case XON: /* ^S */
			case (XOFF | 0200):
			case (XON | 0200):
				tab[i]=1;
				break;
			case 020: /* ^P */
			case 0220:
				if (turbo_escape)
					tab[i]=0;
				else
					tab[i]=1;
				break;
			case 015:
			case 0215:
				if (Zctlesc)
					tab[i]=1;
				else if (!turbo_escape)
					tab[i]=2;
				else 
					tab[i]=0;
				break;
			default:
				if (Zctlesc)
					tab[i]=1;
				else
					tab[i]=0;
			}
		}
	}
}

int inline
zgeth1(void)
{
	register int c, n;

	if ((c = noxrd7()) < 0)
		return c;
	n = c - '0';
	if (n > 9)
		n -= ('a' - ':');
	if (n & ~0xF)
		return ERROR;
	if ((c = noxrd7()) < 0)
		return c;
	c -= '0';
	if (c > 9)
		c -= ('a' - ':');
	if (c & ~0xF)
		return ERROR;
	c += (n<<4);
	return c;
}

/* Decode two lower case hex digits into an 8 bit byte value */
int
zgethex(void)
{
	register int c;

	c = zgeth1();
	if (Verbose>8)
		vfile("zgethex: %02X", c);
	return c;
}


/* Store long integer pos in Txhdr */
void 
stohdr(long pos)
{
	Txhdr[ZP0] = pos;
	Txhdr[ZP1] = pos>>8;
	Txhdr[ZP2] = pos>>16;
	Txhdr[ZP3] = pos>>24;
}

/* Recover a long integer from a header */
long
rclhdr(char *hdr)
{
	register long l;

	l = (hdr[ZP3] & 0377);
	l = (l << 8) | (hdr[ZP2] & 0377);
	l = (l << 8) | (hdr[ZP1] & 0377);
	l = (l << 8) | (hdr[ZP0] & 0377);
	return l;
}

/* End of zm.c */

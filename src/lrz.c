/* lrz.c cosmetic modifications by Matt Porter
 * many changes by Uwe Ohse
 * from rz.c By Chuck Forsberg
 * 
 *  A program for Linux to receive files and commands from computers running
 *  zmodem, ymodem, or xmodem protocols.
 *  lrz uses Unix buffered input to reduce wasted CPU time.
 *
 */

#include "zglobal.h"

#define SS_NORMAL 0
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <getopt.h>

#ifdef HAVE_UTIME_H
#include <utime.h>
#endif

#include "timing.h"
#include "long-options.h"
#include "xstrtoul.h"
#include "error.h"

#ifndef STRICT_PROTOTYPES
extern time_t time();
extern char *strerror();
extern char *strstr();
#endif

extern int errno;

#define MAX_BLOCK 8192

/*
 * Max value for HOWMANY is 255 if NFGVMIN is not defined.
 *   A larger value reduces system overhead but may evoke kernel bugs.
 *   133 corresponds to an XMODEM/CRC sector
 */
#ifndef HOWMANY
#ifdef NFGVMIN
#define HOWMANY MAX_BLOCK
#else
#define HOWMANY 255
#endif
#endif

unsigned Baudrate = 2400;

FILE *fout;


int Lastrx;
int Crcflg;
int Firstsec;
int errors;
int Restricted=1;	/* restricted; no /.. or ../ in filenames */
int Readnum = HOWMANY;	/* Number of bytes to ask for in read() from modem */

#define DEFBYTL 2000000000L	/* default rx file size */
char *Pathname;
const char *program_name;		/* the name by which we were called */

int Topipe=0;
int MakeLCPathname=TRUE;	/* make received pathname lower case */
int Verbose=0;
int Quiet=0;		/* overrides logic that would otherwise set verbose */
int Nflag = 0;		/* Don't really transfer files */
int Rxclob=FALSE;	/* Clobber existing file */
int Rxbinary=FALSE;	/* receive all files in bin mode */
int Rxascii=FALSE;	/* receive files in ascii (translate) mode */
int Thisbinary;		/* current file is to be received in bin mode */
int try_resume=FALSE;
int allow_remote_commands=FALSE;
int no_timeout=FALSE;
enum zm_type_enum protocol;
int	under_rsh=FALSE;
int zmodem_requested=FALSE;

#ifdef SEGMENTS
int chinseg = 0;	/* Number of characters received in this data seg */
char secbuf[1+(SEGMENTS+1)*MAX_BLOCK];
#else
char secbuf[MAX_BLOCK + 1];
#endif

#ifdef ENABLE_TIMESYNC
int timesync_flag=0;
int in_timesync=0;
#endif
static int rzfiles(struct zm_fileinfo *);
static int tryz(void);
static void checkpath(const char *name);
static void chkinvok(const char *s);
static void canit(void);
static void report(int sct);
static void uncaps(char *s);
static int IsAnyLower(const char *s);
static int putsec(struct zm_fileinfo *zi, char *buf, int n);
static int make_dirs(char *pathname);
static int procheader(char *name, struct zm_fileinfo *);
static int wcgetsec(int *Blklen, char *rxbuf, int maxtime);
static int wcrx(struct zm_fileinfo *);
static int wcrxpn(struct zm_fileinfo *, char *rpn);
static int wcreceive(int argc, char **argp);
static int rzfile(struct zm_fileinfo *);
static void usage(int exitcode, const char *what);
static void usage1(int exitcode);
static void exec2(const char *s);
static int closeit(struct zm_fileinfo *);
static void ackbibi(void);
static int sys2(const char *s);
static void zmputs(const char *s);
static long getfree(void);

static long buffersize=32768;
static long min_bps=0;
static long min_bps_time=120;

char Lzmanag;		/* Local file management request */
char zconv;		/* ZMODEM file conversion request */
char zmanag;		/* ZMODEM file management request */
char ztrans;		/* ZMODEM file transport request */
int Zctlesc;		/* Encode control characters */
int Zrwindow = 1400;	/* RX window size (controls garbage count) */

int tryzhdrtype=ZRINIT;	/* Header type to send corresponding to Last rx close */

#ifdef ENABLE_SYSLOG
#  if defined(ENABLE_SYSLOG_FORCE) || defined(ENABLE_SYSLOG_DEFAULT)
int enable_syslog=TRUE;
#  else
int enable_syslog=FALSE;
#  endif
#endif


/* called by signal interrupt or terminate to clean things up */
RETSIGTYPE
bibi(int n)
{
	if (zmodem_requested)
		zmputs(Attn);
	canit();
	io_mode(0,0);
	error(128+n,0,_("caught signal %d; exiting"), n);
}

static struct option const long_options[] =
{
	{"append", no_argument, NULL, '+'},
	{"ascii", no_argument, NULL, 'a'},
	{"binary", no_argument, NULL, 'b'},
	{"bufsize", required_argument, NULL, 'B'},
	{"allow-commands", no_argument, NULL, 'C'},
	{"allow-remote-commands", no_argument, NULL, 'C'},
	{"escape", no_argument, NULL, 'e'},
	{"rename", no_argument, NULL, 'E'},
	{"errors", required_argument, NULL, 3},
	{"disable-timeouts", no_argument, NULL, 'O'},
	{"disable-timeout", no_argument, NULL, 'O'}, /* i can't get it right */
	{"min-bps", required_argument, NULL, 'm'},
	{"min-bps-time", required_argument, NULL, 'M'},
	{"protect", no_argument, NULL, 'p'},
	{"resume", no_argument, NULL, 'r'},
	{"restricted", no_argument, NULL, 'R'},
	{"quiet", no_argument, NULL, 'q'},
	{"timesync", no_argument, NULL, 'S'},
	{"timeout", required_argument, NULL, 't'},
	{"keep-uppercase", no_argument, NULL, 'u'},
	{"unrestrict", no_argument, NULL, 'U'},
	{"verbose", no_argument, NULL, 'v'},
	{"windowsize", required_argument, NULL, 'w'},
	{"with-crc", required_argument, NULL, 'c'},
	{"xmodem", no_argument, NULL, 'X'},
	{"ymodem", no_argument, NULL, 1},
	{"zmodem", no_argument, NULL, 'Z'},
	{"overwrite", no_argument, NULL, 'y'},
	{"null", no_argument, NULL, 'D'},
	{"syslog", optional_argument, NULL , 2},
	{NULL,0,NULL,0}
};

int
main(int argc, char *argv[])
{
	register char *cp;
	register npats;
	char **patts=NULL; /* keep compiler quiet */
	int exitcode=0;
	int c;

	Rxtimeout = 100;
	setbuf(stderr, NULL);
	if ((cp=getenv("SHELL")) && (strstr(cp, "rsh") || strstr(cp, "rksh")
		|| strstr(cp,"rbash")))
		under_rsh=TRUE;
	if ((cp=getenv("ZMODEM_RESTRICTED"))!=NULL)
		Restricted=2;

	from_cu();
	chkinvok(argv[0]);	/* if called as [-]rzCOMMAND set flag */

#ifdef ENABLE_SYSLOG
	openlog(program_name,LOG_PID,ENABLE_SYSLOG);
#endif

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

    parse_long_options (argc, argv, program_name, PACKAGE_VERSION, usage1);

	while ((c = getopt_long (argc, argv, 
		"a+bB:cCDeEghm:M:OpqrRSt:w:uUvy",
		long_options, (int *) 0)) != EOF)
	{
		unsigned long int tmp;
		char *tmpptr;
		enum strtol_error s_err;

		switch (c)
		{
		case 0:
			break;
		case '+': Lzmanag = ZF1_ZMAPND; break;
		case 'a': Rxascii=TRUE;  break;
		case 'b': Rxbinary=TRUE; break;
		case 'B': 
			if (strcmp(optarg,"auto")==0) 
				buffersize=-1;
			else
				buffersize=strtol(optarg,NULL,10);
			break;
		case 'c': Crcflg=TRUE; break;
		case 'C': allow_remote_commands=TRUE; break;
		case 'D': Nflag = TRUE; break;
		case 'E': Lzmanag = ZF1_ZMCHNG;
		case 'e': Zctlesc = 1; break;
		case 'h': usage(0,NULL); break;
		case 'm':
			s_err = xstrtoul (optarg, &tmpptr, 0, &tmp, "km");
			min_bps = tmp;
			if (s_err != LONGINT_OK)
				STRTOL_FATAL_ERROR (optarg, _("min_bps"), s_err);
			if (min_bps<0)
				usage(2,_("min_bps must be >= 0"));
			break;
		case 'M':
			s_err = xstrtoul (optarg, NULL, 0, &tmp, NULL);
			min_bps_time = tmp;
			if (s_err != LONGINT_OK)
				STRTOL_FATAL_ERROR (optarg, _("min_bps_time"), s_err);
			if (min_bps_time<=1)
				usage(2,_("min_bps_time must be > 1"));
			break;
		case 'O': no_timeout=TRUE; break;
		case 'p': Lzmanag = ZF1_ZMPROT;  break;
		case 'q': Quiet=TRUE; Verbose=0; break;
		case 'r': try_resume=TRUE;  break;
		case 'R': Restricted++;  break;
		case 'S':
#ifdef ENABLE_TIMESYNC
			timesync_flag++;
			if (timesync_flag==2) {
#ifdef HAVE_SETTIMEOFDAY
				error(0,0,_("don't have settimeofday, will not set time\n"));
#endif
				if (getuid()!=0)
					error(0,0,
				_("not running as root (this is good!), can not set time\n"));
			}
#endif
			break;
		case 't':
			s_err = xstrtoul (optarg, NULL, 0, &tmp, NULL);
			Rxtimeout = tmp;
			if (s_err != LONGINT_OK)
				STRTOL_FATAL_ERROR (optarg, _("timeout"), s_err);
			if (Rxtimeout<10 || Rxtimeout>1000)
				usage(2,_("timeout out of range 10..1000"));
			break;
		case 'w':
			s_err = xstrtoul (optarg, NULL, 0, &tmp, NULL);
			Zrwindow = tmp;
			if (s_err != LONGINT_OK)
				STRTOL_FATAL_ERROR (optarg, _("window size"), s_err);
			break;
		case 'u':
			MakeLCPathname=FALSE; break;
		case 'U':
			if (!under_rsh)
				Restricted=0;
			else 
				error(1,0,
	_("security violation: can't do that under restricted shell\n"));
			break;
		case 'v':
			++Verbose; break;
		case 'X': protocol=ZM_XMODEM; break;
		case 1:   protocol=ZM_YMODEM; break;
		case 'Z': protocol=ZM_ZMODEM; break;
		case 'y':
			Rxclob=TRUE; break;
		case 2:
#ifdef ENABLE_SYSLOG
#  ifndef ENABLE_SYSLOG_FORCE
			if (optarg && (!strcmp(optarg,"off") || !strcmp(optarg,"no"))) {
				if (under_rsh)
					error(0,0, _("cannot turnoff syslog"));
				else
					enable_syslog=FALSE;
			}
			else
				enable_syslog=TRUE;
#  else
			error(0,0, _("cannot turnoff syslog"));
#  endif
#endif
		case 3:
			s_err = xstrtoul (optarg, NULL, 0, &tmp, "km");
			bytes_per_error = tmp;
			if (s_err != LONGINT_OK)
				STRTOL_FATAL_ERROR (optarg, _("bytes_per_error"), s_err);
			if (bytes_per_error<100)
				usage(2,_("error-per-byte should be >100"));
			break;
		default:
			usage(2,NULL);
		}

	}

	npats = argc - optind;
	patts=&argv[optind];

	if (npats > 1)
		usage(2,"garbage on commandline");
	if (protocol!=ZM_XMODEM && npats)
		usage(2, "garbage on commandline");
	if (Restricted && allow_remote_commands)
		allow_remote_commands=FALSE;
	if (Fromcu && !Quiet) {
		if (Verbose == 0)
			Verbose = 2;
	}
	vfile("%s %s for %s-%s\n", program_name, VERSION, CPU, OS);
	io_mode(0,1);
	readline_setup(0, HOWMANY, MAX_BLOCK*2);
#ifndef linux
	if (signal(SIGINT, bibi) == SIG_IGN) {
		signal(SIGINT, SIG_IGN); signal(SIGKILL, SIG_IGN);
	}
	else {
		signal(SIGINT, bibi); signal(SIGKILL, bibi);
	}
	signal(SIGTERM, bibi);
#endif
	if (wcreceive(npats, patts)==ERROR) {
		exitcode=0200;
		canit();
	}
	io_mode(0,0);
	if (exitcode && !zmodem_requested)	/* bellow again with all thy might. */
		canit();
	if (Verbose)
	{
		fputs("\r\n",stderr);
		if (exitcode)
			fputs(_("Transfer incomplete\n"),stderr);
		else
			fputs(_("Transfer complete\n"),stderr);
	}
	exit(exitcode);
}

static void
usage1(int exitcode)
{
	usage(exitcode,NULL);
}

static void
usage(int exitcode, const char *what)
{
	FILE *f=stdout;

	if (exitcode)
	{
		if (what)
			fprintf(stderr, "%s: %s\n",program_name,what);
		fprintf (stderr, _("Try `%s --help' for more information.\n"),
			program_name);
		exit(exitcode);
	}

	fprintf(f, _("%s version %s for %s-%s\n"), program_name,
		VERSION, CPU, OS);

	fprintf(f,_("Usage: %s [options] [filename.if.xmodem]\n"), program_name);
	fputs(_("Receive files with ZMODEM/YMODEM/XMODEM protocol\n"),f);
	fputs(_(
		"    (X) = Option applies to XMODEM only\n"
		"    (Y) = Option applies to YMODEM only\n"
		"    (Z) = Option applies to ZMODEM only\n"
		),f);
	fputs(_(
"  -+, --append                append to existing files\n"
"  -a, --ascii                 ASCII transfer (change CR/LF to LF)\n"
"  -b, --binary                binary transfer\n"
"  -B, --bufsize N             buffer N bytes (N==auto: buffer whole file)\n"
"  -c, --with-crc              Use 16 bit CRC (X)\n"
"  -C, --allow-remote-commands allow execution of remote commands (Z)\n"
"  -D, --null                  write all received data to /dev/null\n"
"  -e, --escape                Escape control characters (Z)\n"
"  -E, --rename                rename any files already existing\n"
"      --errors N              generate CRC error every N bytes (debugging)\n"
"  -h, --help                  Help, print this usage message\n"
"  -m, --min-bps N             stop transmission if BPS below N\n"
"  -M, --min-bps-time N          for at least N seconds (default: 120)\n"
"  -O, --disable-timeouts      disable timeout code, wait forever for data\n"
"  -p, --protect               protect existing files\n"
"  -q, --quiet                 quiet, no progress reports\n"
"  -r, --resume                try to resume interrupted file transfer (Z)\n"
"  -R, --restricted            restricted, more secure mode\n"
"  -S, --timesync              request remote time (twice: set local time)\n"
"      --syslog[=off]          turn syslog on or off, if possible\n"
"  -t, --timeout N             set timeout to N tenths of a second\n"
"  -u, --keep-uppercase        keep upper case filenames\n"
"  -U, --unrestrict            disable restricted mode (if allowed to)\n"
"  -v, --verbose               be verbose, provide debugging information\n"
"  -w, --windowsize N          Window is N bytes (Z)\n"
"  -X  --xmodem                use XMODEM protocol\n"
"  -y, --overwrite             Yes, clobber existing file if any\n"
"      --ymodem                use YMODEM protocol\n"
"  -Z, --zmodem                use ZMODEM protocol\n"
"\n"
"short options use the same arguments as the long ones\n"
	),f);
	exit(exitcode);
}

/*
 * Let's receive something already.
 */

static int 
wcreceive(int argc, char **argp)
{
	int c;
	struct zm_fileinfo zi;
#ifdef ENABLE_SYSLOG
	const char *shortname=NULL;;
#endif
	zi.fname=NULL;
	zi.modtime=0;
	zi.mode=0;
	zi.bytes_total=0;
	zi.bytes_sent=0;
	zi.bytes_received=0;
	zi.bytes_skipped=0;
	zi.eof_seen=0;

	if (protocol!=ZM_XMODEM || argc==0) {
		Crcflg=1;
		if ( !Quiet)
			fprintf(stderr, _("%s waiting to receive."), program_name);
		if ((c=tryz())!=0) {
			if (c == ZCOMPL)
				return OK;
			if (c == ERROR)
				goto fubar;
			c = rzfiles(&zi);

#ifdef ENABLE_SYSLOG
			shortname=NULL;
#endif
			if (c)
				goto fubar;
		} else {
			for (;;) {
				if (Verbose > 1
#ifdef ENABLE_SYSLOG
					|| enable_syslog
#endif
				)
					timing(1);
#ifdef ENABLE_SYSLOG
				shortname=NULL;
#endif
				if (wcrxpn(&zi,secbuf)== ERROR)
					goto fubar;
				if (secbuf[0]==0)
					return OK;
				if (procheader(secbuf, &zi) == ERROR)
					goto fubar;
#ifdef ENABLE_SYSLOG
				shortname=strrchr(zi.fname,'/');
				if (shortname)
					shortname++;
				else
					shortname=zi.fname;
#endif
				if (wcrx(&zi)==ERROR)
					goto fubar;

				if (Verbose > 1
#ifdef ENABLE_SYSLOG
					|| enable_syslog
#endif
				) {
					double d;
					long bps;
					d=timing(0);
					if (d==0)
						d=0.5; /* can happen if timing uses time() */
					bps=(zi.bytes_received-zi.bytes_skipped)/d;

					if (Verbose>1) {
						fprintf(stderr,
							_("\rBytes received: %7ld/%7ld   BPS:%-6ld                \r\n"),
							zi.bytes_received, zi.bytes_total, bps);
					}
#ifdef ENABLE_SYSLOG
					if (enable_syslog)
						syslog(LOG_INFO,"%s/%s: %ld Bytes, %ld BPS",
							shortname,protname(),zi.bytes_received, bps);
#endif
				}
			}
		}
	} else {
		char dummy[128]="";
		zi.bytes_total = DEFBYTL;

		if (Verbose > 1
#ifdef ENABLE_SYSLOG
			|| enable_syslog
#endif
			) 
			timing(1);
		procheader(dummy, &zi);

		if (Pathname)
			free(Pathname);
		errno=0;
		Pathname=malloc(PATH_MAX+1);
		if (!Pathname)
			error(1,0,_("out of memory"));

		strcpy(Pathname, *argp);
		checkpath(Pathname);
#ifdef ENABLE_SYSLOG
		shortname=strrchr(*argp,'/');
		if (shortname)
			shortname++;
		else
			shortname=*argp;
#endif
		putc('\n',stderr);
		fprintf(stderr, _("%s: ready to receive %s"), program_name, Pathname);
		fputs("\r\n",stderr);

		if ((fout=fopen(Pathname, "w")) == NULL) {
#ifdef ENABLE_SYSLOG
			if (enable_syslog)
				syslog(LOG_ERR,"%s/%s: cannot open: %m",
					shortname,protname());
#endif
			return ERROR;
		}
		if (wcrx(&zi)==ERROR) {
			goto fubar;
		}
		if (Verbose > 1
#ifdef ENABLE_SYSLOG
			|| enable_syslog
#endif
	 		) {
			double d;
			long bps;
			d=timing(0);
			if (d==0)
				d=0.5; /* can happen if timing uses time() */
			bps=(zi.bytes_received-zi.bytes_skipped)/d;
			if (Verbose) {
				fprintf(stderr,
					_("\rBytes received: %7ld   BPS:%-6ld                \r\n"),
					zi.bytes_received, bps);
			}
#ifdef ENABLE_SYSLOG
			if (enable_syslog)
				syslog(LOG_INFO,"%s/%s: %ld Bytes, %ld BPS",
					shortname,protname(),zi.bytes_received, bps);
#endif
		}
	}
	return OK;
fubar:
#ifdef ENABLE_SYSLOG
	if (enable_syslog)
		syslog(LOG_ERR,"%s/%s: got error", 
			shortname ? shortname : "no.name", protname());
#endif
	canit();
	if (Topipe && fout) {
		pclose(fout);  return ERROR;
	}
	if (fout)
		fclose(fout);

	if (Restricted && Pathname) {
		unlink(Pathname);
		fprintf(stderr, _("\r\n%s: %s removed.\r\n"), program_name, Pathname);
	}
	return ERROR;
}


/*
 * Fetch a pathname from the other end as a C ctyle ASCIZ string.
 * Length is indeterminate as long as less than Blklen
 * A null string represents no more files (YMODEM)
 */
static int 
wcrxpn(struct zm_fileinfo *zi, char *rpn)
{
	register c;
	int Blklen=0;		/* record length of received packets */

#ifdef NFGVMIN
	readline(1);
#else
	purgeline(0);
#endif

et_tu:
	Firstsec=TRUE;
	zi->eof_seen=FALSE;
	sendline(Crcflg?WANTCRC:NAK);
	flushmo();
	purgeline(0); /* Do read next time ... */
	while ((c = wcgetsec(&Blklen, rpn, 100)) != 0) {
		if (c == WCEOT) {
			zperr( _("Pathname fetch returned EOT"));
			sendline(ACK);
			flushmo();
			purgeline(0);	/* Do read next time ... */
			readline(1);
			goto et_tu;
		}
		return ERROR;
	}
	sendline(ACK);
	flushmo();
	return OK;
}

/*
 * Adapted from CMODEM13.C, written by
 * Jack M. Wierda and Roderick W. Hart
 */
static int 
wcrx(struct zm_fileinfo *zi)
{
	register int sectnum, sectcurr;
	register char sendchar;
	int Blklen;

	Firstsec=TRUE;sectnum=0; 
	zi->eof_seen=FALSE;
	sendchar=Crcflg?WANTCRC:NAK;

	for (;;) {
		sendline(sendchar);	/* send it now, we're ready! */
		flushmo();
		purgeline(0);	/* Do read next time ... */
		sectcurr=wcgetsec(&Blklen, secbuf, (sectnum&0177)?50:130);
		report(sectcurr);
		if (sectcurr==((sectnum+1) &0377)) {
			sectnum++;
			if (R_BYTESLEFT(zi) < Blklen)
				Blklen=R_BYTESLEFT(zi);
			zi->bytes_received+=Blklen;
			if (putsec(zi, secbuf, Blklen)==ERROR)
				return ERROR;
			sendchar=ACK;
		}
		else if (sectcurr==(sectnum&0377)) {
			zperr( _("Received dup Sector"));
			sendchar=ACK;
		}
		else if (sectcurr==WCEOT) {
			if (closeit(zi))
				return ERROR;
			sendline(ACK);
			flushmo();
			purgeline(0);	/* Do read next time ... */
			return OK;
		}
		else if (sectcurr==ERROR)
			return ERROR;
		else {
			zperr( _("Sync Error"));
			return ERROR;
		}
	}
}

/*
 * Wcgetsec fetches a Ward Christensen type sector.
 * Returns sector number encountered or ERROR if valid sector not received,
 * or CAN CAN received
 * or WCEOT if eot sector
 * time is timeout for first char, set to 4 seconds thereafter
 ***************** NO ACK IS SENT IF SECTOR IS RECEIVED OK **************
 *    (Caller must do that when he is good and ready to get next sector)
 */
static int
wcgetsec(int *Blklen, char *rxbuf, int maxtime)
{
	register checksum, wcj, firstch;
	register unsigned short oldcrc;
	register char *p;
	int sectcurr;

	for (Lastrx=errors=0; errors<RETRYMAX; errors++) {

		if ((firstch=readline(maxtime))==STX) {
			*Blklen=1024; goto get2;
		}
		if (firstch==SOH) {
			*Blklen=128;
get2:
			sectcurr=readline(1);
			if ((sectcurr+(oldcrc=readline(1)))==0377) {
				oldcrc=checksum=0;
				for (p=rxbuf,wcj=*Blklen; --wcj>=0; ) {
					if ((firstch=readline(1)) < 0)
						goto bilge;
					oldcrc=updcrc(firstch, oldcrc);
					checksum += (*p++ = firstch);
				}
				if ((firstch=readline(1)) < 0)
					goto bilge;
				if (Crcflg) {
					oldcrc=updcrc(firstch, oldcrc);
					if ((firstch=readline(1)) < 0)
						goto bilge;
					oldcrc=updcrc(firstch, oldcrc);
					if (oldcrc & 0xFFFF)
						zperr( _("CRC"));
					else {
						Firstsec=FALSE;
						return sectcurr;
					}
				}
				else if (((checksum-firstch)&0377)==0) {
					Firstsec=FALSE;
					return sectcurr;
				}
				else
					zperr( _("Checksum"));
			}
			else
				zperr(_("Sector number garbled"));
		}
		/* make sure eot really is eot and not just mixmash */
#ifdef NFGVMIN
		else if (firstch==EOT && readline(1)==TIMEOUT)
			return WCEOT;
#else
		else if (firstch==EOT && readline_left>0)
			return WCEOT;
#endif
		else if (firstch==CAN) {
			if (Lastrx==CAN) {
				zperr( _("Sender Cancelled"));
				return ERROR;
			} else {
				Lastrx=CAN;
				continue;
			}
		}
		else if (firstch==TIMEOUT) {
			if (Firstsec)
				goto humbug;
bilge:
			zperr( _("TIMEOUT"));
		}
		else
			zperr( _("Got 0%o sector header"), firstch);

humbug:
		Lastrx=0;
		while(readline(1)!=TIMEOUT)
			;
		if (Firstsec) {
			sendline(Crcflg?WANTCRC:NAK);
			flushmo();
			purgeline(0);	/* Do read next time ... */
		} else {
			maxtime=40;
			sendline(NAK);
			flushmo();
			purgeline(0);	/* Do read next time ... */
		}
	}
	/* try to stop the bubble machine. */
	canit();
	return ERROR;
}


/*
 * Process incoming file information header
 */
static int
procheader(char *name, struct zm_fileinfo *zi)
{
	const char *openmode;
	char *p;
	static char *name_static=NULL;

	if (name_static)
		free(name_static);
	name_static=malloc(strlen(name)+1);
	if (!name_static)
		error(1,0,_("out of memory"));
	strcpy(name_static,name);
	zi->fname=name_static;

	if (Verbose>2) {
		fprintf(stderr,_("zmanag=%d, Lzmanag=%d\n"),zmanag,Lzmanag);
		fprintf(stderr,_("zconv=%d\n"),zconv);
	}

	/* set default parameters and overrides */
	openmode = "w";
	Thisbinary = (!Rxascii) || Rxbinary;
	if (Lzmanag)
		zmanag = Lzmanag;

	/*
	 *  Process ZMODEM remote file management requests
	 */
	if (!Rxbinary && zconv == ZCNL)	/* Remote ASCII override */
		Thisbinary = 0;
	if (zconv == ZCBIN)	/* Remote Binary override */
		Thisbinary = TRUE;
	if (Thisbinary && zconv == ZCBIN && try_resume)
		zconv=ZCRESUM;
	if (zmanag == ZF1_ZMAPND && zconv!=ZCRESUM)
		openmode = "a";

#ifdef ENABLE_TIMESYNC
	in_timesync=0;
	if (timesync_flag && 0==strcmp(name,"$time$.t"))
		in_timesync=1;
#endif
	/* Check for existing file */
	if (zconv != ZCRESUM && !Rxclob && (zmanag&ZF1_ZMMASK) != ZF1_ZMCLOB 
		&& (zmanag&ZF1_ZMMASK) != ZF1_ZMAPND
#ifdef ENABLE_TIMESYNC
	    && !in_timesync
#endif
		&& (fout=fopen(name, "r"))) {
		struct stat sta;
		char *tmpname;
		char *ptr;
		int i;
		fclose(fout);
		if ((zmanag & ZF1_ZMMASK)!=ZF1_ZMCHNG) {
			if (Verbose>2)
				fprintf(stderr,_("file exists, skipped\n"));
			return ERROR;
		}
		/* try to rename */
		tmpname=alloca(strlen(name+5));
		ptr=stpcpy(tmpname,name);
		*ptr++='.';
		i=0;
		do {
			sprintf(ptr,"%d",i++);
		} while (i<1000 && stat(tmpname,&sta)==0);
		if (i==1000)
			return ERROR;
		free(name_static);
		name_static=malloc(strlen(tmpname)+1);
		if (!name_static)
			error(1,0,_("out of memory"));
		strcpy(name_static,tmpname);
		zi->fname=name_static;
	}

	zi->bytes_total = DEFBYTL;
	zi->mode = 0; 
	zi->eof_seen = 0; 
	zi->modtime = 0;

	p = name + 1 + strlen(name);
	if (*p) {	/* file coming from Unix or DOS system */
		long modtime;
		long bytes_total;
		int mode;
		sscanf(p, "%ld%lo%o", &bytes_total, &modtime, &mode);
		zi->modtime=modtime;
		zi->bytes_total=bytes_total;
		zi->mode=mode;
		if (zi->mode & UNIXFILE)
			++Thisbinary;
	} else {		/* File coming from CP/M system */
		for (p=name_static; *p; ++p)		/* change / to _ */
			if ( *p == '/')
				*p = '_';

		if ( *--p == '.')		/* zap trailing period */
			*p = 0;
	}

#ifdef ENABLE_TIMESYNC
	if (in_timesync)
	{
		long t=time(0);
		long d=t-zi->modtime;
		if (d<0)
			d=0;
		if ((Verbose && d>60) || Verbose > 1)
			fprintf(stderr,  
	_("TIMESYNC: here %ld, remote %ld, diff %d seconds\n"),
			(long) t, (long) zi->modtime, (long) d);
#ifdef HAVE_SETTIMEOFDAY
		if (timesync_flag > 1 && d > 10)
		{
			struct timeval tv;
			tv.tv_sec=zi->modtime;
			tv.tv_usec=0;
			if (settimeofday(&tv,NULL))
				fprintf(stderr,
					_("TIMESYNC: cannot set time: %s\n"),
				strerror(errno));
		}
#endif
		return ERROR; /* skips file */
	}
#endif /* ENABLE_TIMESYNC */

	if (!zmodem_requested && MakeLCPathname && !IsAnyLower(name_static)
	  && !(zi->mode&UNIXFILE))
		uncaps(name_static);
	if (Topipe > 0) {
		if (Pathname)
			free(Pathname);
		Pathname=malloc((PATH_MAX)*2);
		if (!Pathname)
			error(1,0,_("out of memory"));
		sprintf(Pathname, "%s %s", program_name+2, name_static);
		if (Verbose) {
			fprintf(stderr,  "%s: %s %s\n",
				_("Topipe"),
				Pathname, Thisbinary?"BIN":"ASCII");
		}
		if ((fout=popen(Pathname, "w")) == NULL)
			return ERROR;
	} else {
		if (protocol==ZM_XMODEM)
			/* we don't have the filename yet */
			return OK; /* dummy */
		if (Pathname)
			free(Pathname);
		Pathname=malloc((PATH_MAX)*2);
		if (!Pathname)
			error(1,0,_("out of memory"));
		strcpy(Pathname, name_static);
		if (Verbose) {
			putc('\r',stderr);
			fprintf(stderr, _("Receiving: %s\n"), name_static);
		}
		checkpath(name_static);
		if (Nflag)
		{
			/* cast because we might not have a prototyp for strdup :-/ */
			free(name_static);
			name_static=(char *) strdup("/dev/null");
			if (!name_static)
			{
				fprintf(stderr,"%s: %s\n", program_name, _("out of memory"));
				exit(1);
			}
		}
#ifdef OMEN
		if (name_static[0] == '!' || name_static[0] == '|') {
			if ( !(fout = popen(name_static+1, "w"))) {
				return ERROR;
			}
			Topipe = -1;  return(OK);
		}
#endif
		if (Thisbinary && zconv==ZCRESUM) {
			struct stat st;
			fout = fopen(name_static, "r+");
			if (fout && 0==fstat(fileno(fout),&st))
			{
				/* retransfer whole blocks */
				zi->bytes_skipped = st.st_size & ~(1024);
				if (zi->bytes_skipped < zi->bytes_total) {
					if (fseek(fout, zi->bytes_skipped, SEEK_SET)) {
						fclose(fout);
						return ZFERR;
					}
				}
				else
					zi->bytes_skipped=0; /* resume impossible, file has changed */
				goto buffer_it;
			}
			zi->bytes_skipped=0;
			if (fout)
				fclose(fout);
		}
		fout = fopen(name_static, openmode);
#ifdef ENABLE_MKDIR
		if ( !fout && Restricted < 2) {
			if (make_dirs(name_static))
				fout = fopen(name_static, openmode);
		}
#endif
		if ( !fout)
		{
			zpfatal(_("cannot open %s"), name_static);
			return ERROR;
		}
	}
buffer_it:
	if (Topipe == 0) {
		static char *s=NULL;
		static size_t last_length=0;
		if (buffersize==-1 && s) {
			if (zi->bytes_total>last_length) {
				free(s);
				s=NULL;
				last_length=0;
			}
		}
		if (!s && buffersize) {
			last_length=32768;
			if (buffersize==-1) {
				if (zi->bytes_total>0)
					last_length=zi->bytes_total;
			} else 
				last_length=buffersize;
			/* buffer `4096' bytes pages */
			last_length=(last_length+4095)&0xfffff000;
			s=malloc(last_length);
			if (!s) {
				zpfatal(_("out of memory"));
				exit(1);
			}
		}
		if (s) {
#ifdef SETVBUF_REVERSED
			setvbuf(fout,_IOFBF,s,last_length);
#else
			setvbuf(fout,s,_IOFBF,last_length);
#endif
		}
	}
	zi->bytes_received=zi->bytes_skipped;

	return OK;
}

#ifdef ENABLE_MKDIR
/*
 *  Directory-creating routines from Public Domain TAR by John Gilmore
 */

/*
 * After a file/link/symlink/dir creation has failed, see if
 * it's because some required directory was not present, and if
 * so, create all required dirs.
 */
static int
make_dirs(char *pathname)
{
	register char *p;		/* Points into path */
	int madeone = 0;		/* Did we do anything yet? */
	int save_errno = errno;		/* Remember caller's errno */

	if (errno != ENOENT)
		return 0;		/* Not our problem */

	for (p = strchr(pathname, '/'); p != NULL; p = strchr(p+1, '/')) {
		/* Avoid mkdir of empty string, if leading or double '/' */
		if (p == pathname || p[-1] == '/')
			continue;
		/* Avoid mkdir where last part of path is '.' */
		if (p[-1] == '.' && (p == pathname+1 || p[-2] == '/'))
			continue;
		*p = 0;				/* Truncate the path there */
		if ( !mkdir(pathname, 0777)) {	/* Try to create it as a dir */
			vfile("Made directory %s\n", pathname);
			madeone++;		/* Remember if we made one */
			*p = '/';
			continue;
		}
		*p = '/';
		if (errno == EEXIST)		/* Directory already exists */
			continue;
		/*
		 * Some other error in the mkdir.  We return to the caller.
		 */
		break;
	}
	errno = save_errno;		/* Restore caller's errno */
	return madeone;			/* Tell them to retry if we made one */
}

#endif /* ENABLE_MKDIR */

/*
 * Putsec writes the n characters of buf to receive file fout.
 *  If not in binary mode, carriage returns, and all characters
 *  starting with CPMEOF are discarded.
 */
static int 
putsec(struct zm_fileinfo *zi, char *buf, int n)
{
	register char *p;

	if (n == 0)
		return OK;
	if (Thisbinary) {
		if (fwrite(buf,n,1,fout)!=1)
			return ERROR;
	}
	else {
		if (zi->eof_seen)
			return OK;
		for (p=buf; --n>=0; ++p ) {
			if ( *p == '\r')
				continue;
			if (*p == CPMEOF) {
				zi->eof_seen=TRUE;
				return OK;
			}
			putc(*p ,fout);
		}
	}
	return OK;
}

/* make string s lower case */
static void
uncaps(char *s)
{
	for ( ; *s; ++s)
		if (isupper(*s))
			*s = tolower(*s);
}
/*
 * IsAnyLower returns TRUE if string s has lower case letters.
 */
static int 
IsAnyLower(const char *s)
{
	for ( ; *s; ++s)
		if (islower(*s))
			return TRUE;
	return FALSE;
}

/* send cancel string to get the other end to shut up */
static void
canit(void)
{
	static char canistr[] = {
	 24,24,24,24,24,24,24,24,24,24,8,8,8,8,8,8,8,8,8,8,0
	};

	printf(canistr);
	flushmo();
	purgeline(0);	/* Do read next time ... */
}


static void
report(int sct)
{
	if (Verbose>1)
	{
		fprintf(stderr,_("Blocks received: %d"),sct);
		putc('\r',stderr);
	}
}

/*
 * If called as [-][dir/../]vrzCOMMAND set Verbose to 1
 * If called as [-][dir/../]rzCOMMAND set the pipe flag
 * If called as rb use YMODEM protocol
 */
static void
chkinvok(const char *s)
{
	const char *p;

	p = s;
	while (*p == '-')
		s = ++p;
	while (*p)
		if (*p++ == '/')
			s = p;
	if (*s == 'v') {
		Verbose=1; ++s;
	}
	program_name = s;
	if (*s == 'l') 
		s++; /* lrz -> rz */
	protocol=ZM_ZMODEM;
	if (s[0]=='r' && s[1]=='x')
		protocol=ZM_XMODEM;
	if (s[0]=='r' && (s[1]=='b' || s[1]=='y'))
		protocol=ZM_YMODEM;
	if (s[2] && protocol!=ZM_XMODEM)
		Topipe = 1;
}

/*
 * Totalitarian Communist pathname processing
 */
static void 
checkpath(const char *name)
{
	if (Restricted) {
		const char *p;
		p=strrchr(name,'/');
		if (p)
			p++;
		else
			p=name;
		/* don't overwrite any file in very restricted mode.
		 * don't overwrite hidden files in restricted mode */
		if ((Restricted==2 || *name=='.') && fopen(name, "r") != NULL) {
			canit();
			fputs("\r\n",stderr);
			fprintf(stderr, _("%s: %s exists\n"), 
				program_name, name);
			bibi(-1);
		}
		/* restrict pathnames to current tree or uucppublic */
		if ( strstr(name, "../")
#ifdef PUBDIR
		 || (name[0]== '/' && strncmp(name, PUBDIR, 
		 	strlen(PUBDIR)))
#endif
		) {
			canit();
			fputs("\r\n",stderr);
			fprintf(stderr,_("%s:\tSecurity Violation"),program_name);
			fputs("\r\n",stderr);
			bibi(-1);
		}
		if (Restricted > 1) {
			if (name[0]=='.' || strstr(name,"/.")) {
				canit();
				fputs("\r\n",stderr);
				fprintf(stderr,_("%s:\tSecurity Violation"),program_name);
				fputs("\r\n",stderr);
				bibi(-1);
			}
		}
	}
}

/*
 * Initialize for Zmodem receive attempt, try to activate Zmodem sender
 *  Handles ZSINIT frame
 *  Return ZFILE if Zmodem filename received, -1 on error,
 *   ZCOMPL if transaction finished,  else 0
 */
static int
tryz(void)
{
	register c, n;
	register cmdzack1flg;

	if (protocol!=ZM_ZMODEM)		/* Check for "rb" program name */
		return 0;

	for (n=zmodem_requested?15:5; --n>=0; ) {
		/* Set buffer length (0) and capability flags */
#ifdef SEGMENTS
		stohdr(SEGMENTS*MAX_BLOCK);
#else
		stohdr(0L);
#endif
#ifdef CANBREAK
		Txhdr[ZF0] = CANFC32|CANFDX|CANOVIO|CANBRK;
#else
		Txhdr[ZF0] = CANFC32|CANFDX|CANOVIO;
#endif
#ifdef ENABLE_TIMESYNC
		if (timesync_flag)
			Txhdr[ZF1] |= ZF1_TIMESYNC;
#endif
		if (Zctlesc)
			Txhdr[ZF0] |= TESCCTL; /* TESCCTL == ESCCTL */
		zshhdr(tryzhdrtype, Txhdr);
		if (tryzhdrtype == ZSKIP)	/* Don't skip too far */
			tryzhdrtype = ZRINIT;	/* CAF 8-21-87 */
again:
		switch (zgethdr(Rxhdr, 0)) {
		case ZRQINIT:
			continue;
		case ZEOF:
			continue;
		case TIMEOUT:
			continue;
		case ZFILE:
			zconv = Rxhdr[ZF0];
			if (!zconv)
				/* resume with sz -r is impossible (at least with unix sz)
				 * if this is not set */
				zconv=ZCBIN;
			zmanag = Rxhdr[ZF1];
			ztrans = Rxhdr[ZF2];
			tryzhdrtype = ZRINIT;
			c = zrdata(secbuf, MAX_BLOCK);
			io_mode(0,3);
			if (c == GOTCRCW)
				return ZFILE;
			zshhdr(ZNAK, Txhdr);
			goto again;
		case ZSINIT:
			Zctlesc = TESCCTL & Rxhdr[ZF0];
			if (zrdata(Attn, ZATTNLEN) == GOTCRCW) {
				stohdr(1L);
				zshhdr(ZACK, Txhdr);
				goto again;
			}
			zshhdr(ZNAK, Txhdr);
			goto again;
		case ZFREECNT:
			stohdr(getfree());
			zshhdr(ZACK, Txhdr);
			goto again;
		case ZCOMMAND:
			cmdzack1flg = Rxhdr[ZF0];
			if (zrdata(secbuf, MAX_BLOCK) == GOTCRCW) {
				if (Verbose)
				{
					fprintf(stderr,"%s: %s\n", program_name,
						_("remote command execution requested"));
					fprintf(stderr,"%s: %s\n", program_name, secbuf);
				}
				if (!allow_remote_commands) 
				{
					if (Verbose)
						fprintf(stderr,"%s: %s\n", program_name, 
							_("not executed"));
					zshhdr(ZCOMPL, Txhdr);
					return ZCOMPL;
				}
				if (cmdzack1flg & ZCACK1)
					stohdr(0L);
				else
					stohdr((long)sys2(secbuf));
				purgeline(0);	/* dump impatient questions */
				do {
					zshhdr(ZCOMPL, Txhdr);
				}
				while (++errors<20 && zgethdr(Rxhdr,1) != ZFIN);
				ackbibi();
				if (cmdzack1flg & ZCACK1)
					exec2(secbuf);
				return ZCOMPL;
			}
			zshhdr(ZNAK, Txhdr);
			goto again;
		case ZCOMPL:
			goto again;
		default:
			continue;
		case ZFIN:
			ackbibi();
			return ZCOMPL;
		case ZRINIT:
			if (Verbose)
				fprintf(stderr,_("got ZRINIT"));
			return ERROR;
		case ZCAN:
			if (Verbose)
				fprintf(stderr,_("got ZCAN"));
			return ERROR;
		}
	}
	return 0;
}

#ifdef ENABLE_SYSLOG
#define DO_SYSLOG(message) do { \
	if (enable_syslog) { \
		const char *shortname; \
		if (!zi->fname) \
			shortname="no.name"; \
		else { \
			shortname=strrchr(zi->fname,'/'); \
			if (!shortname) \
				shortname=zi->fname; \
			else \
				shortname++; \
		} \
        syslog message ; \
	} \
} while(0)
#else
#define DO_SYSLOG(message) do { } while(0)
#endif

/*
 * Receive 1 or more files with ZMODEM protocol
 */
static int
rzfiles(struct zm_fileinfo *zi)
{
	register c;

	for (;;) {
		timing(1);
		c = rzfile(zi);
		switch (c) {
		case ZEOF:
			if (Verbose > 1
#ifdef ENABLE_SYSLOG
				|| enable_syslog
#endif
	 		) {
				double d;
				long bps;
				d=timing(0);
				if (d==0)
					d=0.5; /* can happen if timing uses time() */
				bps=(zi->bytes_received-zi->bytes_skipped)/d;
				if (Verbose > 1) {
					fprintf(stderr,
						_("\rBytes received: %7ld/%7ld   BPS:%-6ld                \r\n"),
						zi->bytes_received, zi->bytes_total, bps);
				}
				DO_SYSLOG((LOG_INFO, "%s/%s: %ld Bytes, %ld BPS",shortname,
						   protname(), (long) zi->bytes_total,bps));
			}
		case ZSKIP:
			if (c==ZSKIP)
			{
				if (Verbose) 
					fprintf(stderr,_("Skipped"));
				DO_SYSLOG((LOG_INFO, "%s/%s: skipped",shortname,protname()));
			}
			switch (tryz()) {
			case ZCOMPL:
				return OK;
			default:
				return ERROR;
			case ZFILE:
				break;
			}
			continue;
		default:
			return c;
		case ERROR:
			DO_SYSLOG((LOG_INFO, "%s/%s: error",shortname,protname()));
			return ERROR;
		}
	}
}

/* "OOSB" means Out Of Sync Block. I once thought that if sz sents
 * blocks a,b,c,d, of which a is ok, b fails, we might want to save 
 * c and d. But, alas, i never saw c and d.
 */
#define SAVE_OOSB
#ifdef SAVE_OOSB
typedef struct oosb_t {
	size_t pos;
	long len;
	char *data;
	struct oosb_t *next;
} oosb_t;
struct oosb_t *anker=NULL;
#endif

/*
 * Receive a file with ZMODEM protocol
 *  Assumes file name frame is in secbuf
 */
static int
rzfile(struct zm_fileinfo *zi)
{
	register c, n;
	long last_rxbytes=0;
	long last_bps=0;
	long not_printed=0;
	time_t low_bps=0;

	zi->eof_seen=FALSE;

	n = 20;

	if (procheader(secbuf,zi) == ERROR) {
		DO_SYSLOG((LOG_INFO, "%s/%s: procheader error",
				   shortname,protname()));
		return (tryzhdrtype = ZSKIP);
	}

	for (;;) {
#ifdef SEGMENTS
		chinseg = 0;
#endif
		stohdr(zi->bytes_received);
		zshhdr(ZRPOS, Txhdr);
		goto skip_oosb;
nxthdr:
#ifdef SAVE_OOSB
		if (anker) {
			oosb_t *akt,*last,*next;
			for (akt=anker,last=NULL;akt;last= akt ? akt : last ,akt=next) {
				if (akt->pos==zi->bytes_received) {
					putsec(zi, akt->data, akt->len);
					zi->bytes_received += akt->len;
					vfile("using saved out-of-sync-paket %lx, len %ld",
						  akt->pos,akt->len);
					goto nxthdr;
				}
				next=akt->next;
				if (akt->pos<zi->bytes_received) {
					vfile("removing unneeded saved out-of-sync-paket %lx, len %ld",
						  akt->pos,akt->len);
					if (last)
						last->next=akt->next;
					else
						anker=akt->next;
					free(akt->data);
					free(akt);
					akt=NULL;
				}
			}
		}
#endif
	skip_oosb:
		c = zgethdr(Rxhdr, 0);
		switch (c) {
		default:
			DO_SYSLOG((LOG_INFO, "%s/%s: error: zgethdr returned %d",shortname,
					   protname(),c));
			vfile("rzfile: zgethdr returned %d", c);
			return ERROR;
		case ZNAK:
		case TIMEOUT:
#ifdef SEGMENTS
			putsec(secbuf, chinseg);
			chinseg = 0;
#endif
			if ( --n < 0) {
				DO_SYSLOG((LOG_INFO, "%s/%s: error: zgethdr returned %s",shortname,
					   protname(),c == ZNAK ? "ZNAK" : "TIMEOUT"));
				vfile("rzfile: zgethdr returned %d", c);
				return ERROR;
			}
		case ZFILE:
			zrdata(secbuf, MAX_BLOCK);
			continue;
		case ZEOF:
#ifdef SEGMENTS
			putsec(secbuf, chinseg);
			chinseg = 0;
#endif
			if (rclhdr(Rxhdr) != zi->bytes_received) {
				/*
				 * Ignore eof if it's at wrong place - force
				 *  a timeout because the eof might have gone
				 *  out before we sent our zrpos.
				 */
				errors = 0;  goto nxthdr;
			}
			if (closeit(zi)) {
				tryzhdrtype = ZFERR;
				DO_SYSLOG((LOG_INFO, "%s/%s: error: closeit return <>0",
						   shortname, protname()));
				vfile("rzfile: closeit returned <> 0");
				return ERROR;
			}
			vfile("rzfile: normal EOF");
			return c;
		case ERROR:	/* Too much garbage in header search error */
#ifdef SEGMENTS
			putsec(secbuf, chinseg);
			chinseg = 0;
#endif
			if ( --n < 0) {
				DO_SYSLOG((LOG_INFO, "%s/%s: error: zgethdr returned %d",
						   shortname, protname(),c));
				vfile("rzfile: zgethdr returned %d", c);
				return ERROR;
			}
			zmputs(Attn);
			continue;
		case ZSKIP:
#ifdef SEGMENTS
			putsec(secbuf, chinseg);
			chinseg = 0;
#endif
			closeit(zi);
			DO_SYSLOG((LOG_INFO, "%s/%s: error: sender skipped",
					   shortname, protname()));
			vfile("rzfile: Sender SKIPPED file");
			return c;
		case ZDATA:
			if (rclhdr(Rxhdr) != zi->bytes_received) {
#if defined(SAVE_OOSB)
				oosb_t *neu;
				long pos=rclhdr(Rxhdr);
#endif
				if ( --n < 0) {
					vfile("rzfile: out of sync");
					DO_SYSLOG((LOG_INFO, "%s/%s: error: out of sync",
					   shortname, protname()));
					return ERROR;
				}
#if defined(SAVE_OOSB)
				switch (c = zrdata(secbuf, MAX_BLOCK))
				{
				case GOTCRCW:
				case GOTCRCG:
				case GOTCRCE:
				case GOTCRCQ:
					if (pos>zi->bytes_received) {
						neu=malloc(sizeof(oosb_t));
						if (neu)
							neu->data=malloc(Rxcount);
						if (neu && neu->data) {
#ifdef ENABLE_SYSLOG
/* call syslog to tell me if this happens */
							syslog(LOG_ERR, 
								   "saving out-of-sync-block %lx, len %ld",
								   pos, (long) Rxcount);
#endif
							vfile("saving out-of-sync-block %lx, len %ld",pos,
								  (long) Rxcount);
							memcpy(neu->data,secbuf,Rxcount);
							neu->pos=pos;
							neu->len=Rxcount;
							neu->next=anker;
							anker=neu;
						}
						else if (neu)
							free(neu);
					}
				}
#endif
#ifdef SEGMENTS
				putsec(secbuf, chinseg);
				chinseg = 0;
#endif
				zmputs(Attn);  continue;
			}
moredata:
			if ((Verbose>1 || min_bps)
				&& (not_printed > (min_bps ? 3 : 7) 
					|| zi->bytes_received > last_bps / 2 + last_rxbytes)) {
				int minleft =  0;
				int secleft =  0;
				double d;
				d=timing(0);
				if (d==0)
					d=0.5; /* timing() might use time() */
				last_bps=zi->bytes_received/d;
				if (last_bps > 0) {
					minleft =  (R_BYTESLEFT(zi))/last_bps/60;
					secleft =  ((R_BYTESLEFT(zi))/last_bps)%60;
				}
				if (min_bps) {
					if (low_bps) {
						if (last_bps<min_bps) {
							if (time(NULL)-low_bps>=min_bps_time) {
								/* too bad */
								vfile(_("rzfile: bps rate %ld below min %ld"), 
									  last_bps, min_bps);
								DO_SYSLOG((LOG_INFO, "%s/%s: bps rate low: %ld < %ld",
										   shortname, protname(), last_bps, min_bps));
								return ERROR;
							}
						}
						else
							low_bps=0;
					} else if (last_bps<min_bps) {
						low_bps=time(NULL);
					}
				}
				if (Verbose > 1) {
					fprintf(stderr,
							_("\rBytes received: %7ld/%7ld   BPS:%-6ld ETA %02d:%02d  "),
							zi->bytes_received, zi->bytes_total, last_bps, minleft, secleft);
					last_rxbytes=zi->bytes_received;
					not_printed=0;
				}
			} else if (Verbose)
				not_printed++;
#ifdef SEGMENTS
			if (chinseg >= (MAX_BLOCK * SEGMENTS)) {
				putsec(secbuf, chinseg);
				chinseg = 0;
			}
			switch (c = zrdata(secbuf+chinseg, MAX_BLOCK))
#else
			switch (c = zrdata(secbuf, MAX_BLOCK))
#endif
			{
			case ZCAN:
#ifdef SEGMENTS
				putsec(secbuf, chinseg);
				chinseg = 0;
#endif
				vfile("rzfile: zrdata returned %d", c);
				DO_SYSLOG((LOG_INFO, "%s/%s: zrdata returned ZCAN",
						   shortname, protname()));
				return ERROR;
			case ERROR:	/* CRC error */
#ifdef SEGMENTS
				putsec(secbuf, chinseg);
				chinseg = 0;
#endif
				if ( --n < 0) {
					vfile("rzfile: zgethdr returned %d", c);
					DO_SYSLOG((LOG_INFO, "%s/%s: zrdata returned ERROR",
							   shortname, protname()));
					return ERROR;
				}
				zmputs(Attn);
				continue;
			case TIMEOUT:
#ifdef SEGMENTS
				putsec(secbuf, chinseg);
				chinseg = 0;
#endif
				if ( --n < 0) {
					DO_SYSLOG((LOG_INFO, "%s/%s: zrdata returned TIMEOUT",
							   shortname, protname()));
					vfile("rzfile: zgethdr returned %d", c);
					return ERROR;
				}
				continue;
			case GOTCRCW:
				n = 20;
#ifdef SEGMENTS
				chinseg += Rxcount;
				putsec(zi, secbuf, chinseg);
				chinseg = 0;
#else
				putsec(zi, secbuf, Rxcount);
#endif
				zi->bytes_received += Rxcount;
				stohdr(zi->bytes_received);
				zshhdr(ZACK | 0x80, Txhdr);
				goto nxthdr;
			case GOTCRCQ:
				n = 20;
#ifdef SEGMENTS
				chinseg += Rxcount;
#else
				putsec(zi, secbuf, Rxcount);
#endif
				zi->bytes_received += Rxcount;
				stohdr(zi->bytes_received);
				zshhdr(ZACK, Txhdr);
				goto moredata;
			case GOTCRCG:
				n = 20;
#ifdef SEGMENTS
				chinseg += Rxcount;
#else
				putsec(zi, secbuf, Rxcount);
#endif
				zi->bytes_received += Rxcount;
				goto moredata;
			case GOTCRCE:
				n = 20;
#ifdef SEGMENTS
				chinseg += Rxcount;
#else
				putsec(zi, secbuf, Rxcount);
#endif
				zi->bytes_received += Rxcount;
				goto nxthdr;
			}
		}
	}
}

/*
 * Send a string to the modem, processing for \336 (sleep 1 sec)
 *   and \335 (break signal)
 */
static void
zmputs(const char *s)
{
	char *p;

	while (s && *s)
	{
		p=strpbrk(s,"\335\336");
		if (!p)
		{
			write(1,s,strlen(s));
			return;
		}
		if (p!=s)
		{
			write(1,s,p-s);
			s=p;
		}
		if (*p=='\336')
			sleep(1);
		else
			sendbrk(0);
		p++;
	}
}

/*
 * Close the receive dataset, return OK or ERROR
 */
static int
closeit(struct zm_fileinfo *zi)
{
	int ret;
	if (Topipe) {
		if (pclose(fout)) {
			return ERROR;
		}
		return OK;
	}
	ret=fclose(fout);
	if (ret) {
		zpfatal(_("file close error"));
		/* this may be any sort of error, including random data corruption */

		unlink(Pathname);
		return ERROR;
	}
	if (zi->modtime) {
#ifdef HAVE_STRUCT_UTIMBUF
		struct utimbuf timep;
		timep.actime = time(NULL);
		timep.modtime = zi->modtime;
		utime(Pathname, &timep);
#else
		time_t timep[2];
		timep[0] = time(NULL);
		timep[1] = zi->modtime;
		utime(Pathname, timep);
#endif
	}
	if ((zi->mode&S_IFMT) == S_IFREG) {
		/* we must not make this program executable if running 
		 * under rsh, because the user might have uploaded an
		 * unrestricted shell.
		 */
		if (under_rsh)
			chmod(Pathname, (00666 & zi->mode));
		else
			chmod(Pathname, (07777 & zi->mode));
	}
	return OK;
}

/*
 * Ack a ZFIN packet, let byegones be byegones
 */
static void
ackbibi(void)
{
	register n;

	vfile("ackbibi:");
	Readnum = 1;
	stohdr(0L);
	for (n=3; --n>=0; ) {
		purgeline(0);
		zshhdr(ZFIN, Txhdr);
		switch (readline(100)) {
		case 'O':
			readline(1);	/* Discard 2nd 'O' */
			vfile("ackbibi complete");
			return;
		case RCDO:
			return;
		case TIMEOUT:
		default:
			break;
		}
	}
}

/*
 * Strip leading ! if present, do shell escape. 
 */
static int
sys2(const char *s)
{
	if (*s == '!')
		++s;
	return system(s);
}

/*
 * Strip leading ! if present, do exec.
 */
static void 
exec2(const char *s)
{
	if (*s == '!')
		++s;
	io_mode(0,0);
	execl("/bin/sh", "sh", "-c", s);
	zpfatal("execl");
	exit(1);
}

/*
 * Routine to calculate the free bytes on the current file system
 *  ~0 means many free bytes (unknown)
 */
static long 
getfree(void)
{
	return(~0L);	/* many free bytes ... */
}

/* End of lrz.c */

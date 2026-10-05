/*  lsz.c cosmetic modifications by Matt Porter *  from the Public Domain version of sz.c by Chuck Forsberg,
 *  Omen Technology INC
 *
 *  A program for Linux to send files and commands to computers running
 *  zmodem, ymodem, or xmodem protocols.
 *
 */
#include "zglobal.h"

#define NEW_ERROR

/* char *getenv(); */

#define SS_NORMAL 0
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <setjmp.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>

#ifndef R_OK
#  define R_OK 4
#endif

#if defined(HAVE_SYS_MMAN_H) && defined(HAVE_MMAP)
#  include <sys/mman.h>
size_t mm_size;
void *mm_addr=NULL;
#else
#  undef HAVE_MMAP
#endif
#include "timing.h"
#include "long-options.h"
#include "xstrtoul.h"
#include "error.h"

#ifndef STRICT_PROTOTYPES
extern time_t time();
extern char *strerror();
extern char *strstr();
extern char *stpcpy();
#endif

extern int errno;

unsigned Baudrate=2400;	/* Default, should be set by first mode() call */
unsigned Txwindow;	/* Control the size of the transmitted window */
unsigned Txwspac;	/* Spacing between zcrcq requests */
unsigned Txwcnt;	/* Counter used to space ack requests */
long Lrxpos;		/* Receiver's last reported offset */
int errors;
enum zm_type_enum protocol;
int under_rsh=FALSE;
extern int turbo_escape;

int Canseek=1; /* 1: can; 0: only rewind, -1: neither */

static int zsendfile(struct zm_fileinfo *zi, const char *buf, int blen);
static int getnak(void);
static int wctxpn(struct zm_fileinfo *);
static int wcs(const char *oname);
static int zfilbuf(struct zm_fileinfo *zi);
static int filbuf(char *buf, int count);
static int getzrxinit(void);
#ifdef NEW_ERROR
static int calc_blklen(long total_sent);
#endif
static int sendzsinit(void);
static int wctx(struct zm_fileinfo *);
static int zsendfdata(struct zm_fileinfo *);
static void canit(void);
static int getinsync(struct zm_fileinfo *, int flag);
static void countem(int argc, char **argv);
static void chkinvok(const char *s);
static void usage(int exitcode, const char *what);
static int zsendcmd(const char *buf, int blen);
static void saybibi(void);
static int wcsend(int argc, char *argp[]);
static int wcputsec(char *buf, int sectnum, int cseclen);
static void usage1(int exitcode);

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

int Filesleft;
long Totalleft;
size_t buffersize=16384;
#ifdef HAVE_MMAP
int use_mmap=1;
#endif

/*
 * Attention string to be executed by receiver to interrupt streaming data
 *  when an error is detected.  A pause (0336) may be needed before the
 *  ^C (03) or after it.
 */
#ifdef READCHECK
char Myattn[] = { 0 };
#else
char Myattn[] = { 03, 0336, 0 };
#endif

FILE *input_f;

#define MAX_BLOCK 8192
char txbuf[MAX_BLOCK];

long vpos = 0;			/* Number of bytes read from file */

char Lastrx;
char Crcflg;
int Verbose=0;
int Restricted=0;	/* restricted; no /.. or ../ in filenames */
int Quiet=0;		/* overrides logic that would otherwise set verbose */
int Ascii=0;		/* Add CR's for brain damaged programs */
int Fullname=0;		/* transmit full pathname */
int Unlinkafter=0;	/* Unlink file after it is sent */
int Dottoslash=0;	/* Change foo.bar.baz to foo/bar/baz */
int firstsec;
int errcnt=0;		/* number of files unreadable */
int blklen=128;		/* length of transmitted records */
int Optiong;		/* Let it rip no wait for sector ACK's */
int Totsecs;		/* total number of sectors this file */
int Filcnt=0;		/* count of number of files opened */
int Lfseen=0;
unsigned Rxbuflen = 16384;	/* Receiver's max buffer length */
int Tframlen = 0;	/* Override for tx frame length */
int blkopt=0;		/* Override value for zmodem blklen */
int Rxflags = 0;
int Rxflags2 = 0;
long bytcnt;
int Wantfcs32 = TRUE;	/* want to send 32 bit FCS */
char Lzconv;	/* Local ZMODEM file conversion request */
char Lzmanag;	/* Local ZMODEM file management request */
int Lskipnocor;
char Lztrans;
char zconv;		/* ZMODEM file conversion request */
char zmanag;		/* ZMODEM file management request */
char ztrans;		/* ZMODEM file transport request */
int command_mode;		/* Send a command, then exit. */
int Cmdtries = 11;
int Cmdack1;		/* Rx ACKs command, then do it */
int Exitcode;
int enable_timesync=0;
long Lastsync;		/* Last offset to which we got a ZRPOS */
int Beenhereb4;		/* How many times we've been ZRPOS'd same place */

int no_timeout=FALSE;
int max_blklen=1024;
int start_blklen=0;
int zmodem_requested;
time_t stop_time=0;

#ifdef NEW_ERROR
int error_count;
#define OVERHEAD 18
#define OVER_ERR 20
#endif
#define MK_STRING(x) #x

#ifdef ENABLE_SYSLOG
#  if defined(ENABLE_SYSLOG_FORCE) || defined(ENABLE_SYSLOG_DEFAULT)
int enable_syslog=TRUE;
#  else
int enable_syslog=FALSE;
#  endif
#endif

jmp_buf intrjmp;	/* For the interrupt on RX CAN */

static long min_bps;
static long min_bps_time;

static int io_mode_fd=0;
static int zrqinits_sent=0;
static int play_with_sigint=0;

/* called by signal interrupt or terminate to clean things up */
RETSIGTYPE
bibi (int n)
{
	canit ();
	fflush (stdout);
	io_mode (io_mode_fd,0);
	if (n == 99)
		error (0, 0, _ ("io_mode(,2) in rbsb.c not implemented\n"));
	else
		error (0, 0, _ ("caught signal %d; exiting"), n);
	if (n == SIGQUIT)
		abort ();
	exit (128 + n);
}

/* Called when ZMODEM gets an interrupt (^C) */
static RETSIGTYPE
onintr(int n)
{
	signal(SIGINT, SIG_IGN);
	longjmp(intrjmp, -1);
}

int Zctlesc;	/* Encode control characters */
const char *program_name = "sz";
int Zrwindow = 1400;	/* RX window size (controls garbage count) */

static struct option const long_options[] =
{
  {"append", no_argument, NULL, '+'},
  {"twostop", no_argument, NULL, '2'},
  {"try-8k", no_argument, NULL, '8'},
  {"start-8k", no_argument, NULL, '9'},
  {"ascii", no_argument, NULL, 'a'},
  {"binary", no_argument, NULL, 'b'},
  {"bufsize", required_argument, NULL, 'B'},
  {"cmdtries", required_argument, NULL, 'C'},
  {"command", required_argument, NULL, 'c'},
  {"immediate-command", required_argument, NULL, 'c'},
  {"dot-to-slash", no_argument, NULL, 'd'},
  {"full-path", no_argument, NULL, 'f'},
  {"escape", no_argument, NULL, 'e'},
  {"rename", no_argument, NULL, 'E'},
  {"1024", no_argument, NULL, 'k'},
  {"1k", no_argument, NULL, 'k'},
  {"packetlen", required_argument, NULL, 'L'},
  {"framelen", required_argument, NULL, 'l'},
  {"min-bps", required_argument, NULL, 'm'},
  {"min-bps-time", required_argument, NULL, 'M'},
  {"newer", no_argument, NULL, 'n'},
  {"newer-or-longer", no_argument, NULL, 'N'},
  {"16-bit-crc", no_argument, NULL, 'o'},
  {"disable-timeouts", no_argument, NULL, 'O'},
  {"disable-timeout", no_argument, NULL, 'O'}, /* i can't get it right */
  {"protect", no_argument, NULL, 'p'},
  {"resume", no_argument, NULL, 'r'},
  {"restricted", no_argument, NULL, 'R'},
  {"quiet", no_argument, NULL, 'q'},
  {"stop-at", required_argument, NULL, 's'},
  {"syslog", optional_argument, NULL , 2},
  {"timesync", no_argument, NULL, 'S'},
  {"timeout", required_argument, NULL, 't'},
  {"turbo", no_argument, NULL, 'T'},
  {"unlink", no_argument, NULL, 'u'},
  {"unrestrict", no_argument, NULL, 'U'},
  {"verbose", no_argument, NULL, 'v'},
  {"windowsize", required_argument, NULL, 'w'},
  {"xmodem", no_argument, NULL, 'X'},
  {"ymodem", no_argument, NULL, 1},
  {"zmodem", no_argument, NULL, 'Z'},
  {"overwrite", no_argument, NULL, 'y'},
  {"overwrite-or-skip", no_argument, NULL, 'Y'},

  {"delay-startup", required_argument, NULL, 4},
  {NULL, 0, NULL, 0}
};

int 
main(int argc, char **argv)
{
	register char *cp;
	register npats;
	int dm;
	int i;
	int stdin_files;
	char **patts;
	int c;
	const char *Cmdstr=NULL;		/* Pointer to the command string */
	int startup_delay=0;

	if ((cp = getenv("ZNULLS")) && *cp)
		Znulls = atoi(cp);
	if ((cp=getenv("SHELL")) && (strstr(cp, "rsh") || strstr(cp, "rksh")
		|| strstr(cp, "rbash") || strstr(cp,"rshell")))
	{
		under_rsh=TRUE;
		Restricted=1;
	}
	if ((cp=getenv("ZMODEM_RESTRICTED"))!=NULL)
		Restricted=1;
	from_cu();
	chkinvok(argv[0]);

#ifdef ENABLE_SYSLOG
	openlog(program_name,LOG_PID,ENABLE_SYSLOG);
#endif

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	parse_long_options (argc, argv, program_name, PACKAGE_VERSION, usage1);

	Rxtimeout = 600;

	while ((c = getopt_long (argc, argv, 
		"2+8abB:C:c:dfeEghi:kL:l:m:M:NnOopRrqsSt:TUuvw:XYy",
		long_options, (int *) 0))!=EOF)
	{
		unsigned long int tmp;
		char *tmpptr;
		enum strtol_error s_err;

		switch (c)
		{
		case 0:
			break;
		case '+': Lzmanag = ZF1_ZMAPND; break;
		case '2': Twostop = TRUE; break;
		case '8':
			if (max_blklen==8192)
				start_blklen=8192;
			else
				max_blklen=8192;
			break;
		case '9': /* this is a longopt .. */
			start_blklen=8192;
			max_blklen=8192;
			break;
		case 'a': Lzconv = ZCNL; Ascii = TRUE; break;
		case 'b': Lzconv = ZCBIN; break;
		case 'B':
			if (0==strcmp(optarg,"auto"))
				buffersize=-1;
			else
				buffersize=strtol(optarg,NULL,10);
#ifdef HAVE_MMAP
			use_mmap=0;
#endif
			break;
		case 'C': 
			s_err = xstrtoul (optarg, NULL, 0, &tmp, NULL);
			Cmdtries = tmp;
			if (s_err != LONGINT_OK)
				STRTOL_FATAL_ERROR (optarg, _("command tries"), s_err);
			break;
		case 'i':
			Cmdack1 = ZCACK1;
			/* **** FALL THROUGH TO **** */
		case 'c':
			command_mode = TRUE;
			Cmdstr = optarg;
			break;
		case 'd':
			++Dottoslash;
			/* **** FALL THROUGH TO **** */
		case 'f': Fullname=TRUE; break;
		case 'e': Zctlesc = 1; break;
		case 'E': Lzmanag = ZF1_ZMCHNG; break;
		case 'h': usage(0,NULL); break;
		case 'k': start_blklen=1024; break;
		case 'L':
			s_err = xstrtoul (optarg, NULL, 0, &tmp, "ck");
			blkopt = tmp;
			if (s_err != LONGINT_OK)
				STRTOL_FATAL_ERROR (optarg, _("packetlength"), s_err);
			if (blkopt<24 || blkopt>MAX_BLOCK)
			{
				char meld[256];
				sprintf(meld,
					_("packetlength out of range 24..%ld"),
					(long) MAX_BLOCK);
				usage(2,meld);
			}
			break;
		case 'l':
			s_err = xstrtoul (optarg, NULL, 0, &tmp, "ck");
			Tframlen = tmp;
			if (s_err != LONGINT_OK)
				STRTOL_FATAL_ERROR (optarg, _("packetlength"), s_err);
			if (Tframlen<32 || Tframlen>MAX_BLOCK)
			{
				char meld[256];
				sprintf(meld,
					_("framelength out of range 32..%ld"),
					(long) MAX_BLOCK);
				usage(2,meld);
			}
			break;
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
		case 'N': Lzmanag = ZF1_ZMNEWL;  break;
		case 'n': Lzmanag = ZF1_ZMNEW;  break;
		case 'o': Wantfcs32 = FALSE; break;
		case 'O': no_timeout = TRUE; break;
		case 'p': Lzmanag = ZF1_ZMPROT;  break;
		case 'r': Lzconv = ZCRESUM; break;
		case 'R': Restricted = TRUE; break;
		case 'q': Quiet=TRUE; Verbose=0; break;
		case 's':
			if (isdigit(*optarg)) {
				struct tm *tm;
				time_t t;
				int hh,mm;
				char *nex;

				hh = strtoul (optarg, &nex, 10);
				if (hh>23)
					usage(2,_("hour to large (0..23)"));
				if (*nex!=':')
					usage(2, _("unparsable stop time\n"));
				nex++;
				mm = strtoul (optarg, &nex, 10);
				if (mm>59)
					usage(2,_("minute to large (0..59)"));

				t=time(NULL);
				tm=localtime(&t);
				tm->tm_hour=hh;
				tm->tm_min=hh;
				stop_time=mktime(tm);
				if (stop_time<t)
					stop_time+=86400L; /* one day more */
				if (stop_time - t <10) 
					usage(2,_("stop time to small"));
			} else {
				s_err = xstrtoul (optarg, NULL, 0, &tmp, NULL);
				stop_time = tmp + time(0);
				if (s_err != LONGINT_OK)
					STRTOL_FATAL_ERROR (optarg, _("stop-at"), s_err);
				if (tmp<10)
					usage(2,_("stop time to small"));
			}
			break;
		case 'S': enable_timesync=1; break;
		case 'T': turbo_escape=1; break;
		case 't':
			s_err = xstrtoul (optarg, NULL, 0, &tmp, NULL);
			Rxtimeout = tmp;
			if (s_err != LONGINT_OK)
				STRTOL_FATAL_ERROR (optarg, _("timeout"), s_err);
			if (Rxtimeout<10 || Rxtimeout>1000)
				usage(2,_("timeout out of range 10..1000"));
			break;
		case 'u': ++Unlinkafter; break;
		case 'U':
			if (!under_rsh)
				Restricted=0;
			else
				error(1,0,
		_("security violation: can't do that under restricted shell\n"));
			break;
		case 'v': ++Verbose; break;
		case 'w':
			s_err = xstrtoul (optarg, NULL, 0, &tmp, NULL);
			Txwindow = tmp;
			if (s_err != LONGINT_OK)
				STRTOL_FATAL_ERROR (optarg, _("window size"), s_err);
			if (Txwindow < 256)
				Txwindow = 256;
			Txwindow = (Txwindow/64) * 64;
			Txwspac = Txwindow/4;
			if (blkopt > Txwspac
			 || (!blkopt && Txwspac < MAX_BLOCK))
				blkopt = Txwspac;
			break;
		case 'X': protocol=ZM_XMODEM; break;
		case 1:   protocol=ZM_YMODEM; break;
		case 'Z': protocol=ZM_ZMODEM; break;
		case 'Y':
			Lskipnocor = TRUE;
			/* **** FALLL THROUGH TO **** */
		case 'y':
			Lzmanag = ZF1_ZMCLOB; break;
		case 2:
#ifdef ENABLE_SYSLOG
#  ifndef ENABLE_SYSLOG_FORCE
			if (optarg && (!strcmp(optarg,"off") || !strcmp(optarg,"no")))
			{
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
			break;
		case 4:
			s_err = xstrtoul (optarg, NULL, 0, &tmp, NULL);
			startup_delay = tmp;
			if (s_err != LONGINT_OK)
				STRTOL_FATAL_ERROR (optarg, _("startup delay"), s_err);
			break;
		default:
			usage (2,NULL);
			break;
		}
	}

	if (start_blklen==0) {
		if (protocol == ZM_ZMODEM)
			start_blklen=1024;
		else
			start_blklen=128;
	}

	if (argc<2)
		usage(2,_("need at least one file to send"));

	if (startup_delay)
		sleep(startup_delay);

#ifdef HAVE_SIGINTERRUPT
	/* we want interrupted system calls to fail and not to be restarted. */
	siginterrupt(SIGALRM,1);
#endif

	npats = argc - optind;
	patts=&argv[optind];

	if (npats < 1 && !command_mode) 
		usage(2,_("need at least one file to send"));
	if (command_mode && Restricted) {
		printf(_("Can't send command in restricted mode\n"));
		exit(1);
	}

	if (Fromcu && !Quiet) {
		if (Verbose == 0)
			Verbose = 2;
	}
	vfile("%s %s for %s-%s\n", program_name, VERSION, CPU, OS);

	{
		/* we write max_blocklen (data) + 18 (ZModem protocol overhead)
		 * + escape overhead (about 4 %), so buffer has to be
		 * somewhat larger than max_blklen 
		 */
		char *s=malloc(max_blklen+1024);
		if (!s)
		{
			zperr(_("out of memory"));
			exit(1);
		}
#ifdef SETVBUF_REVERSED
		setvbuf(stdout,_IOFBF,s,max_blklen+1024);
#else
		setvbuf(stdout,s,_IOFBF,max_blklen+1024);
#endif
	}
	blklen=start_blklen;

	for (i=optind,stdin_files=0;i<argc;i++) {
		if (0==strcmp(argv[i],"-"))
			stdin_files++;
	}

	if (stdin_files>1) {
		usage(1,_("can read only one file from stdin"));
	} else if (stdin_files==1) {
		io_mode_fd=1;
	}
	io_mode(io_mode_fd,1);
	readline_setup(io_mode_fd, 128, 256);

	if (signal(SIGINT, bibi) == SIG_IGN)
		signal(SIGINT, SIG_IGN);
	else {
		signal(SIGINT, bibi); 
		play_with_sigint=1;
	}
	signal(SIGTERM, bibi);

	if ( protocol!=ZM_XMODEM) {
		if (protocol==ZM_ZMODEM) {
			printf("rz\r");
			fflush(stdout);
		}
		countem(npats, patts);
		if (protocol == ZM_ZMODEM) {
			/* throw away any input already received. This doesn't harm
			 * as we invite the receiver to send it's data again, and
			 * might be useful if the receiver has already died or
			 * if there is dirt left if the line 
			 */
#ifdef HAVE_SELECT
			struct timeval t;
			unsigned char throwaway;
			fd_set f;
#endif

			purgeline(io_mode_fd);
				
#ifdef HAVE_SELECT
			t.tv_sec = 0;
			t.tv_usec = 0;
				
			FD_ZERO(&f);
			FD_SET(io_mode_fd,&f);
				
			while (select(1,&f,NULL,NULL,&t)) {
				if (0==read(io_mode_fd,&throwaway,1)) /* EOF ... */
					break;
			}
#endif

			purgeline(io_mode_fd);
			stohdr(0L);
			if (command_mode)
				Txhdr[ZF0] = ZCOMMAND;
			zshhdr(ZRQINIT, Txhdr);
			zrqinits_sent++;
#if defined(ENABLE_TIMESYNC)
			if (Rxflags2 != ZF1_TIMESYNC)
				/* disable timesync if there are any flags we don't know.
				 * dsz/gsz seems to use some other flags! */
				enable_timesync=FALSE;
			if (Rxflags2 & ZF1_TIMESYNC && enable_timesync) {
				Totalleft+=6; /* TIMESYNC never needs more */
				Filesleft++;
			}
#endif
		}
	}
	fflush(stdout);

	if (Cmdstr) {
		if (getzrxinit()) {
			Exitcode=0200; canit();
		}
		else if (zsendcmd(Cmdstr, 1+strlen(Cmdstr))) {
			Exitcode=0200; canit();
		}
	} else if (wcsend(npats, patts)==ERROR) {
		Exitcode=0200;
		canit();
	}
	fflush(stdout);
	io_mode(io_mode_fd,0);
	if (Exitcode)
		dm=Exitcode;
	else if (errcnt)
		dm=1;
	else
		dm=0;
	if (Verbose)
	{
		fputs("\r\n",stderr);
		if (dm)
			fputs(_("Transfer incomplete\n"),stderr);
		else
			fputs(_("Transfer complete\n"),stderr);
	}
	exit(dm);
	/*NOTREACHED*/
}

static int
wcsend (int argc, char *argp[])
{
	register n;

	Crcflg = FALSE;
	firstsec = TRUE;
	bytcnt = -1;
	for (n = 0; n < argc; ++n) {
		Totsecs = 0;
		if (wcs (argp[n]) == ERROR)
			return ERROR;
	}
#if defined(ENABLE_TIMESYNC)
	if (Rxflags2 & ZF1_TIMESYNC && enable_timesync) {
		/* implement Peter Mandrellas extension */
		/* yes, this *has* a minor race condition */
		char *tmp;
		const char *p;
		FILE *f;
		if (Verbose) {
			fputs ("\r\n", stderr);
			fprintf (stderr, _ ("Answering TIMESYNC"));
		}
		p = getenv ("TMPDIR");
		if (!p)
			p = getenv ("TMP");
		if (!p)
			p = "/tmp";
		tmp=malloc(PATH_MAX+1);
		if (!tmp)
			error(1,0,_("out of memory"));
		
		strcpy(stpcpy(tmp,p),"/$time$.t");

		f = fopen (tmp, "w");
		if (f) {
			char buf[30];
			time_t t = time (NULL);
			struct tm *tm = localtime (&t);		/* sets timezone */
			strftime (buf, sizeof (buf) - 1, "%H:%M:%S", tm);
			if (Verbose)
				fprintf (stderr, " %s %s", _ ("at"), buf);
#if defined(HAVE_TIMEZONE_VAR)
			fprintf (f, "%ld\r\n", timezone / 60);
			if (Verbose)
				fprintf (stderr, " (%s %ld)\r\n", _ ("timezone"), timezone / 60);
#else
			if (Verbose)
				fprintf (stderr, " (%s %s)\r\n", _ ("timezone unknown"));
#endif
			fclose (f);
			if (wcs (tmp) == ERROR)
				if (Verbose)
					fprintf (stderr, _ ("TIMESYNC: failed\n"));
				else {
					if (Verbose)
						fprintf (stderr, _ ("TIMESYNC: ok\n"));
					Filcnt--;
				}
			unlink (tmp);
		} else {
			if (!Verbose) {
				fputs ("\r\n", stderr);
				fprintf (stderr, _ ("Answering TIMESYNC"));
			}
			fprintf (stderr, "\n");
			fprintf (stderr, _ ("  cannot open tmpfile %s: %s"),
					 tmp, strerror (errno));
			fputs ("\r\n", stderr);
		}
		free(tmp);
	}
#endif
	Totsecs = 0;
	if (Filcnt == 0) {			/* bitch if we couldn't open ANY files */
#if 0
	/* i *really* do not like this */
		if (protocol != ZM_XMODEM) {
			const char *Cmdstr;		/* Pointer to the command string */
			command_mode = TRUE;
			Cmdstr = "echo \"lsz: Can't open any requested files\"";
			if (getnak ()) {
				Exitcode = 0200;
				canit ();
			}
			if (!zmodem_requested)
				canit ();
			else if (zsendcmd (Cmdstr, 1 + strlen (Cmdstr))) {
				Exitcode = 0200;
				canit ();
			}
			Exitcode = 1;
			return OK;
		}
#endif
		canit ();
		fputs ("\r\n", stderr);
		fprintf (stderr, _ ("Can't open any requested files."));
		fputs ("\r\n", stderr);
		return ERROR;
	}
	if (zmodem_requested)
		saybibi ();
	else if (protocol != ZM_XMODEM) {
		struct zm_fileinfo zi;
		char *pa;
		pa=alloca(PATH_MAX+1);
		*pa='\0';
		zi.fname = pa;
		zi.modtime = 0;
		zi.mode = 0;
		zi.bytes_total = 0;
		zi.bytes_sent = 0;
		zi.bytes_received = 0;
		zi.bytes_skipped = 0;
		wctxpn (&zi);
	}
	return OK;
}

static int
wcs(const char *oname)
{
	register c;
	struct stat f;
	char *name;
	struct zm_fileinfo zi;
#ifdef HAVE_MMAP
	int dont_mmap_this=0;
#endif
#ifdef ENABLE_SYSLOG
	const char *shortname;
	shortname=strrchr(oname,'/');
	if (shortname)
		shortname++;
	else
		shortname=oname;
#endif


	if (Restricted) {
		/* restrict pathnames to current tree or uucppublic */
		if ( strstr(oname, "../")
#ifdef PUBDIR
		 || (oname[0]== '/' && strncmp(oname, MK_STRING(PUBDIR),
		 	strlen(MK_STRING(PUBDIR))))
#endif
		) {
			canit();
			putc('\r',stderr);
			error(1,0,
				_("security violation: not allowed to upload from %s"),oname);
		}
	}
	
	if (0==strcmp(oname,"-")) {
		char *p=getenv("ONAME");
		name=alloca(PATH_MAX+1);
		if (p) {
			strcpy(name, p);
		} else {
			sprintf(name, "s%lu.lsz", (unsigned long) getpid());
		}
		input_f=stdin;
#ifdef HAVE_MMAP
		dont_mmap_this=1;
#endif
	} else if ((input_f=fopen(oname, "r"))==NULL) {
		int e=errno;
		error(0,e, _("cannot open %s"),oname);
		++errcnt;
		return OK;	/* pass over it, there may be others */
	} else {
		name=alloca(PATH_MAX+1);
		strcpy(name, oname);
	}
#ifdef HAVE_MMAP
	if (!use_mmap || dont_mmap_this)
#endif
	{
		static char *s=NULL;
		static size_t last_length=0;
		struct stat st;
		if (fstat(fileno(input_f),&st)==-1)
			st.st_size=1024*1024;
		if (buffersize==-1 && s) {
			if (st.st_size > last_length) {
				free(s);
				s=NULL;
				last_length=0;
			}
		}
		if (!s && buffersize) {
			last_length=16384;
			if (buffersize==-1) {
				if (st.st_size>0)
					last_length=st.st_size;
			} else
				last_length=buffersize;
			/* buffer whole pages */
			last_length=(last_length+4095)&0xfffff000;
			s=malloc(last_length);
			if (!s) {
				zpfatal(_("out of memory"));
				exit(1);
			}
		}
		if (s) {
#ifdef SETVBUF_REVERSED
			setvbuf(input_f,_IOFBF,s,last_length);
#else
			setvbuf(input_f,s,_IOFBF,last_length);
#endif
		}
	}
	vpos = 0;
	/* Check for directory or block special files */
	fstat(fileno(input_f), &f);
	c = f.st_mode & S_IFMT;
	if (c == S_IFDIR || c == S_IFBLK) {
		error(0,0, _("is not a file: %s"),name);
		fclose(input_f);
		return OK;
	}

	zi.fname=name;
	zi.modtime=f.st_mtime;
	zi.mode=f.st_mode;
	zi.bytes_total= c == S_IFIFO ? DEFBYTL : f.st_size;
	zi.bytes_sent=0;
	zi.bytes_received=0;
	zi.bytes_skipped=0;
	zi.eof_seen=0;
	timing(1,NULL);

	++Filcnt;
	switch (wctxpn(&zi)) {
	case ERROR:
#ifdef ENABLE_SYSLOG
		if (enable_syslog)
			syslog(LOG_INFO, _("%s/%s: error occured"),protname(),shortname);
#endif
		return ERROR;
	case ZSKIP:
		error(0,0, _("skipped: %s"),name);
#ifdef ENABLE_SYSLOG
		if (enable_syslog)
			syslog(LOG_INFO, _("%s/%s: skipped"),protname(),shortname);
#endif
		return OK;
	}
	if (!zmodem_requested && wctx(&zi)==ERROR)
	{
#ifdef ENABLE_SYSLOG
		if (enable_syslog)
			syslog(LOG_INFO, _("%s/%s: error occured"),protname(),shortname);
#endif
		return ERROR;
	}
	if (Unlinkafter)
		unlink(oname);

	if (Verbose > 1
#ifdef ENABLE_SYSLOG
		|| enable_syslog
#endif
		) {
		long bps;
		double d=timing(0,NULL);
		if (d==0) /* can happen if timing() uses time() */
			d=0.5;
		bps=zi.bytes_sent/d;
		putc('\r',stderr);
		if (Verbose > 1) 
			fprintf(stderr,
				_("Bytes Sent:%7ld   BPS:%-8ld                        \n"),
				zi.bytes_sent,bps);
#ifdef ENABLE_SYSLOG
		if (enable_syslog)
			syslog(LOG_INFO, "%s/%s: %ld Bytes, %ld BPS",shortname,
				protname(), (long) zi.bytes_sent,bps);
#endif
	}
	return 0;
}

/*
 * generate and transmit pathname block consisting of
 *  pathname (null terminated),
 *  file length, mode time and file mode in octal
 *  as provided by the Unix fstat call.
 *  N.B.: modifies the passed name, may extend it!
 */
static int
wctxpn(struct zm_fileinfo *zi)
{
	register char *p, *q;
	char *name2;
	struct stat f;

	name2=alloca(PATH_MAX+1);

	if (protocol==ZM_XMODEM) {
		if (Verbose && *zi->fname && fstat(fileno(input_f), &f)!= -1) {
			fprintf(stderr, _("Sending %s, %ld blocks: "),
			  zi->fname, (long) (f.st_size>>7));
		}
		fprintf(stderr, _("Give your local XMODEM receive command now."));
		fputs("\r\n",stderr);
		return OK;
	}
	if (!zmodem_requested)
		if (getnak()) {
			vfile("getnak failed");
			DO_SYSLOG((LOG_INFO, "%s/%s: getnak failed",
					   shortname,protname()));
			return ERROR;
		}

	q = (char *) 0;
	if (Dottoslash) {		/* change . to . */
		for (p=zi->fname; *p; ++p) {
			if (*p == '/')
				q = p;
			else if (*p == '.')
				*(q=p) = '/';
		}
		if (q && strlen(++q) > 8) {	/* If name>8 chars */
			q += 8;			/*   make it .ext */
			strcpy(name2, q);	/* save excess of name */
			*q = '.';
			strcpy(++q, name2);	/* add it back */
		}
	}

	for (p=zi->fname, q=txbuf ; *p; )
		if ((*q++ = *p++) == '/' && !Fullname)
			q = txbuf;
	*q++ = 0;
	p=q;
	while (q < (txbuf + MAX_BLOCK))
		*q++ = 0;
	if (!Ascii && (input_f!=stdin) && *zi->fname && fstat(fileno(input_f), &f)!= -1)
		sprintf(p, "%lu %lo %o 0 %d %ld", (long) f.st_size, f.st_mtime,
		  f.st_mode, Filesleft, Totalleft);
	if (Verbose)
		fprintf(stderr, _("Sending: %s\n"),txbuf);
	Totalleft -= f.st_size;
	if (--Filesleft <= 0)
		Totalleft = 0;
	if (Totalleft < 0)
		Totalleft = 0;

	/* force 1k blocks if name won't fit in 128 byte block */
	if (txbuf[125])
		blklen=1024;
	else {		/* A little goodie for IMP/KMD */
		txbuf[127] = (f.st_size + 127) >>7;
		txbuf[126] = (f.st_size + 127) >>15;
	}
	if (zmodem_requested)
		return zsendfile(zi,txbuf, 1+strlen(p)+(p-txbuf));
	if (wcputsec(txbuf, 0, 128)==ERROR) {
		vfile("wcputsec failed");
		DO_SYSLOG((LOG_INFO, "%s/%s: wcputsec failed",
				   shortname,protname()));
		return ERROR;
	}
	return OK;
}

static int 
getnak(void)
{
	register firstch;
	int tries=0;

	Lastrx = 0;
	for (;;) {
		tries++;
		switch (firstch = readline(100)) {
		case ZPAD:
			if (getzrxinit())
				return ERROR;
			Ascii = 0;	/* Receiver does the conversion */
			return FALSE;
		case TIMEOUT:
			/* 60 seconds are enough */
			if (tries==6) {
				zperr(_("Timeout on pathname"));
				return TRUE;
			}
			if (zrqinits_sent>0 && zrqinits_sent<4) {
				/* if we already sent a ZRQINIT we are using zmodem
				 * protocol and may send further ZRQINITs 
				 */
				stohdr(0L);
				zshhdr(ZRQINIT, Txhdr);
				zrqinits_sent++;
			}
			continue;
		case WANTG:
			io_mode(io_mode_fd,2);	/* Set cbreak, XON/XOFF, etc. */
			Optiong = TRUE;
			blklen=1024;
		case WANTCRC:
			Crcflg = TRUE;
		case NAK:
			return FALSE;
		case CAN:
			if ((firstch = readline(20)) == CAN && Lastrx == CAN)
				return TRUE;
		default:
			break;
		}
		Lastrx = firstch;
	}
}


static int 
wctx(struct zm_fileinfo *zi)
{
	register int thisblklen;
	register int sectnum, attempts, firstch;

	firstsec=TRUE;  thisblklen = blklen;
	vfile("wctx:file length=%ld", (long) zi->bytes_total);

	while ((firstch=readline(Rxtimeout))!=NAK && firstch != WANTCRC
	  && firstch != WANTG && firstch!=TIMEOUT && firstch!=CAN)
		;
	if (firstch==CAN) {
		zperr(_("Receiver Cancelled"));
		return ERROR;
	}
	if (firstch==WANTCRC)
		Crcflg=TRUE;
	if (firstch==WANTG)
		Crcflg=TRUE;
	sectnum=0;
	for (;;) {
		if (zi->bytes_total <= (zi->bytes_sent + 896L))
			thisblklen = 128;
		if ( !filbuf(txbuf, thisblklen))
			break;
		if (wcputsec(txbuf, ++sectnum, thisblklen)==ERROR)
			return ERROR;
		zi->bytes_sent += thisblklen;
	}
	fclose(input_f);
	attempts=0;
	do {
		purgeline(io_mode_fd);
		sendline(EOT);
		flushmo();
		++attempts;
	} while ((firstch=(readline(Rxtimeout)) != ACK) && attempts < RETRYMAX);
	if (attempts == RETRYMAX) {
		zperr(_("No ACK on EOT"));
		return ERROR;
	}
	else
		return OK;
}

static int 
wcputsec(char *buf, int sectnum, int cseclen)
{
	register checksum, wcj;
	register char *cp;
	unsigned oldcrc;
	int firstch;
	int attempts;

	firstch=0;	/* part of logic to detect CAN CAN */

	if (Verbose>1) {
		putc('\r',stderr);
		fprintf(stderr, 
			_("Ymodem sectors/kbytes sent: %3d/%2dk"), Totsecs, Totsecs/8 );
	}
	for (attempts=0; attempts <= RETRYMAX; attempts++) {
		Lastrx= firstch;
		sendline(cseclen==1024?STX:SOH);
		sendline(sectnum);
		sendline(-sectnum -1);
		oldcrc=checksum=0;
		for (wcj=cseclen,cp=buf; --wcj>=0; ) {
			sendline(*cp);
			oldcrc=updcrc((0377& *cp), oldcrc);
			checksum += *cp++;
		}
		if (Crcflg) {
			oldcrc=updcrc(0,updcrc(0,oldcrc));
			sendline((int)oldcrc>>8);
			sendline((int)oldcrc);
		}
		else
			sendline(checksum);

		flushmo();
		if (Optiong) {
			firstsec = FALSE; return OK;
		}
		firstch = readline(Rxtimeout);
gotnak:
		switch (firstch) {
		case CAN:
			if(Lastrx == CAN) {
cancan:
				zperr(_("Cancelled"));  return ERROR;
			}
			break;
		case TIMEOUT:
			zperr(_("Timeout on sector ACK")); continue;
		case WANTCRC:
			if (firstsec)
				Crcflg = TRUE;
		case NAK:
			zperr(_("NAK on sector")); continue;
		case ACK: 
			firstsec=FALSE;
			Totsecs += (cseclen>>7);
			return OK;
		case ERROR:
			zperr(_("Got burst for sector ACK")); break;
		default:
			zperr(_("Got %02x for sector ACK"), firstch); break;
		}
		for (;;) {
			Lastrx = firstch;
			if ((firstch = readline(Rxtimeout)) == TIMEOUT)
				break;
			if (firstch == NAK || firstch == WANTCRC)
				goto gotnak;
			if (firstch == CAN && Lastrx == CAN)
				goto cancan;
		}
	}
	zperr(_("Retry Count Exceeded"));
	return ERROR;
}

/* fill buf with count chars padding with ^Z for CPM */
static int 
filbuf(char *buf, int count)
{
	register c, m;

	if ( !Ascii) {
		m = read(fileno(input_f), buf, count);
		if (m <= 0)
			return 0;
		while (m < count)
			buf[m++] = 032;
		return count;
	}
	m=count;
	if (Lfseen) {
		*buf++ = 012; --m; Lfseen = 0;
	}
	while ((c=getc(input_f))!=EOF) {
		if (c == 012) {
			*buf++ = 015;
			if (--m == 0) {
				Lfseen = TRUE; break;
			}
		}
		*buf++ =c;
		if (--m == 0)
			break;
	}
	if (m==count)
		return 0;
	else
		while (--m>=0)
			*buf++ = CPMEOF;
	return count;
}

/* Fill buffer with blklen chars */
static int
zfilbuf (struct zm_fileinfo *zi)
{
	int n;

	n = fread (txbuf, 1, blklen, input_f);
	if (n < blklen)
		zi->eof_seen = 1;
	return n;
}

/* send cancel string to get the other end to shut up */
static void
canit (void)
{
	static char canistr[] =
	{
		24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 0
	};

	printf (canistr);
	flushmo ();
	purgeline(io_mode_fd);
}

static void
usage1 (int exitcode)
{
	usage (exitcode, NULL);
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

	fprintf(f,_("Usage: %s [options] file ...\n"),
		program_name);
	fprintf(f,_("   or: %s [options] -{c|i} COMMAND\n"),program_name);
	fputs(_("Send file(s) with ZMODEM/YMODEM/XMODEM protocol\n"),f);
	fputs(_(
		"    (X) = Option applies to XMODEM only\n"
		"    (Y) = Option applies to YMODEM only\n"
		"    (Z) = Option applies to ZMODEM only\n"
		),f);
	fputs(_(
"  -+, --append                append to existing destination file (Z)\n"
"  -2, --twostop               use 2 stop bits\n"
"  -8, --try-8k                go up to 8K blocksize\n"
"      --start-8k              start with 8K blocksize\n"
"  -a, --ascii                 ASCII transfer (change CR/LF to LF)\n"
"  -b, --binary                binary transfer\n"
"  -B, --bufsize N             buffer N bytes (N==auto: buffer whole file)\n"
"  -c, --command COMMAND       execute remote command COMMAND (Z)\n"
"  -C, --command-tries N       try N times to execute a command (Z)\n"
"  -d, --dot-to-slash          change '.' to '/' in pathnames (Y/Z)\n"
"      --delay-startup N       sleep N seconds before doing anything\n"
"  -e, --escape                escape all control characters (Z)\n"
"  -E, --rename                force receiver to rename files it already has\n"
"  -f, --full-path             send full pathname (Y/Z)\n"
"  -i, --immediate-command CMD send remote CMD, return immediately (Z)\n"
"  -h, --help                  print this usage message\n"
"  -k, --1k                    send 1024 byte packets (X)\n"
"  -L, --packetlen N           limit subpacket length to N bytes (Z)\n"
"  -l, --framelen N            limit frame length to N bytes (l>=L) (Z)\n"
"  -m, --min-bps N             stop transmission if BPS below N\n"
"  -M, --min-bps-time N          for at least N seconds (default: 120)\n"
"  -n, --newer                 send file if source newer (Z)\n"
"  -N, --newer-or-longer       send file if source newer or longer (Z)\n"
"  -o, --16-bit-crc            use 16 bit CRC instead of 32 bit CRC (Z)\n"
"  -O, --disable-timeouts      disable timeout code, wait forever\n"
"  -p, --protect               protect existing destination file (Z)\n"
"  -r, --resume                resume interrupted file transfer (Z)\n"
"  -R, --restricted            restricted, more secure mode\n"
"  -q, --quiet                 quiet (no progress reports)\n"
"  -s, --stop-at {HH:MM|+N}    stop transmission at HH:MM or in N seconds\n"
"  -u, --unlink                unlink file after transmission\n"
"  -U, --unrestrict            turn off restricted mode (if allowed to)\n"
"  -v, --verbose               be verbose, provide debugging information\n"
"  -w, --windowsize N          Window is N bytes (Z)\n"
"  -X, --xmodem                use XMODEM protocol\n"
"  -y, --overwrite             overwrite existing files\n"
"  -Y, --overwrite-or-skip     overwrite existing files, else skip\n"
"      --ymodem                use YMODEM protocol\n"
"  -Z, --zmodem                use ZMODEM protocol\n"
"\n"
"short options use the same arguments as the long ones\n"
	),f);
	exit(exitcode);
}

/*
 * Get the receiver's init parameters
 */
static int 
getzrxinit(void)
{
	int old_timeout=Rxtimeout;
	register n;
	struct stat f;

	Rxtimeout=100; /* 10 seconds */

	for (n=10; --n>=0; ) {
		/* we might need to send another zrqinit in case the first is 
		 * lost. But *not* if getting here for the first time - in
		 * this case we might just right a ZRINIT for our first ZRQINIT 
		 */
		if (zrqinits_sent<10 && n!=10 && zrqinits_sent < 4) {
			zrqinits_sent++;
			stohdr(0L);
			zshhdr(ZRQINIT, Txhdr);
		}
		
		switch (zgethdr(Rxhdr, 1)) {
		case ZCHALLENGE:	/* Echo receiver's challenge numbr */
			stohdr(Rxpos);
			zshhdr(ZACK, Txhdr);
			continue;
		case ZCOMMAND:		/* They didn't see out ZRQINIT */
			/* ??? Since when does a receiver send ZCOMMAND?  -- uwe */
			continue;
		case ZRINIT:
			Rxflags = 0377 & Rxhdr[ZF0];
			Rxflags2 = 0377 & Rxhdr[ZF1];
			Txfcs32 = (Wantfcs32 && (Rxflags & CANFC32));
			Zctlesc |= Rxflags & TESCCTL;
			Rxbuflen = (0377 & Rxhdr[ZP0])+((0377 & Rxhdr[ZP1])<<8);
			if ( !(Rxflags & CANFDX))
				Txwindow = 0;
			vfile("Rxbuflen=%d Tframlen=%d", Rxbuflen, Tframlen);
			if ( play_with_sigint)
				signal(SIGINT, SIG_IGN);
			io_mode(io_mode_fd,2);	/* Set cbreak, XON/XOFF, etc. */
#ifndef READCHECK
			/* Use MAX_BLOCK byte frames if no sample/interrupt */
			if (Rxbuflen < 32 || Rxbuflen > MAX_BLOCK) {
				Rxbuflen = MAX_BLOCK;
				vfile("Rxbuflen=%d", Rxbuflen);
			}
#endif
			/* Override to force shorter frame length */
			if (Rxbuflen && (Rxbuflen>Tframlen) && (Tframlen>=32))
				Rxbuflen = Tframlen;
			if ( !Rxbuflen && (Tframlen>=32) && (Tframlen<=MAX_BLOCK))
				Rxbuflen = Tframlen;
			vfile("Rxbuflen=%d", Rxbuflen);

			/* If using a pipe for testing set lower buf len */
			fstat(0, &f);
			if ((f.st_mode & S_IFMT) != S_IFCHR) {
				Rxbuflen = MAX_BLOCK;
			}
			/*
			 * If input is not a regular file, force ACK's to
			 *  prevent running beyond the buffer limits
			 */
			if ( !command_mode) {
				fstat(fileno(input_f), &f);
				if ((f.st_mode & S_IFMT) != S_IFREG) {
					Canseek = -1;
					/* return ERROR; */
				}
			}
			/* Set initial subpacket length */
			if (blklen < 1024) {	/* Command line override? */
				if (Baudrate > 300)
					blklen = 256;
				if (Baudrate > 1200)
					blklen = 512;
				if (Baudrate > 2400)
					blklen = 1024;
			}
			if (Rxbuflen && blklen>Rxbuflen)
				blklen = Rxbuflen;
			if (blkopt && blklen > blkopt)
				blklen = blkopt;
			vfile("Rxbuflen=%d blklen=%d", Rxbuflen, blklen);
			vfile("Txwindow = %u Txwspac = %d", Txwindow, Txwspac);
			Rxtimeout=old_timeout;
			return (sendzsinit());
		case ZCAN:
		case TIMEOUT:
			return ERROR;
		case ZRQINIT:
			if (Rxhdr[ZF0] == ZCOMMAND)
				continue;
		default:
			zshhdr(ZNAK, Txhdr);
			continue;
		}
	}
	return ERROR;
}

/* Send send-init information */
static int 
sendzsinit(void)
{
	register c;

	if (Myattn[0] == '\0' && (!Zctlesc || (Rxflags & TESCCTL)))
		return OK;
	errors = 0;
	for (;;) {
		stohdr(0L);
		if (Zctlesc) {
			Txhdr[ZF0] |= TESCCTL; zshhdr(ZSINIT, Txhdr);
		}
		else
			zsbhdr(ZSINIT, Txhdr);
		zsdata(Myattn, 1+strlen(Myattn), ZCRCW);
		c = zgethdr(Rxhdr, 1);
		switch (c) {
		case ZCAN:
			return ERROR;
		case ZACK:
			return OK;
		default:
			if (++errors > 19)
				return ERROR;
			continue;
		}
	}
}

/* Send file name and related info */
static int 
zsendfile(struct zm_fileinfo *zi, const char *buf, int blen)
{
	register c;
	register unsigned long crc;

	/* we are going to send a ZFILE. There cannot be much useful
	 * stuff in the line right now (*except* ZCAN?). 
	 */
#if 0
	purgeline(io_mode_fd); /* might possibly fix stefan glasers problems */
#endif

	for (;;) {
		Txhdr[ZF0] = Lzconv;	/* file conversion request */
		Txhdr[ZF1] = Lzmanag;	/* file management request */
		if (Lskipnocor)
			Txhdr[ZF1] |= ZF1_ZMSKNOLOC;
		Txhdr[ZF2] = Lztrans;	/* file transport request */
		Txhdr[ZF3] = 0;
		zsbhdr(ZFILE, Txhdr);
		zsdata(buf, blen, ZCRCW);
again:
		c = zgethdr(Rxhdr, 1);
		switch (c) {
		case ZRINIT:
			while ((c = readline(50)) > 0)
				if (c == ZPAD) {
					goto again;
				}
			/* **** FALL THRU TO **** */
		default:
			continue;
		case ZRQINIT:  /* remote site is sender! */
			if (Verbose)
				fprintf(stderr,_("got ZRQINIT"));
			DO_SYSLOG((LOG_INFO, "%s/%s: got ZRQINIT - sz talks to sz",
					   shortname,protname()));
			return ERROR;
		case ZCAN:
			if (Verbose)
				fprintf(stderr,_("got ZCAN"));
			DO_SYSLOG((LOG_INFO, "%s/%s: got ZCAN - receiver canceled",
					   shortname,protname()));
			return ERROR;
		case TIMEOUT:
			DO_SYSLOG((LOG_INFO, "%s/%s: got TIMEOUT",
					   shortname,protname()));
			return ERROR;
		case ZABORT:
			DO_SYSLOG((LOG_INFO, "%s/%s: got ZABORT",
					   shortname,protname()));
			return ERROR;
		case ZFIN:
			DO_SYSLOG((LOG_INFO, "%s/%s: got ZFIN",
					   shortname,protname()));
			return ERROR;
		case ZCRC:
			crc = 0xFFFFFFFFL;
#ifdef HAVE_MMAP
			if (mm_addr) {
				size_t i;
				char *p=mm_addr;
				for (i=0;i<Rxpos && i<mm_size;i++,p++) {
					crc = UPDC32(*p, crc);
				}
				crc = ~crc;
			} else
#endif
			if (Canseek >= 0) {
				while (((c = getc(input_f)) != EOF) && --Rxpos)
					crc = UPDC32(c, crc);
				crc = ~crc;
				clearerr(input_f);	/* Clear EOF */
				fseek(input_f, 0L, 0);
			}
			stohdr(crc);
			zsbhdr(ZCRC, Txhdr);
			goto again;
		case ZSKIP:
			if (input_f)
				fclose(input_f);
			vfile("receiver skipped");
			DO_SYSLOG((LOG_INFO, "%s/%s: receiver skipped",
					   shortname, protname()));
			return c;
		case ZRPOS:
			/*
			 * Suppress zcrcw request otherwise triggered by
			 * lastyunc==bytcnt
			 */
#ifdef HAVE_MMAP
			if (!mm_addr)
#endif
			if (Rxpos && fseek(input_f, Rxpos, 0)) {
				int er=errno;
				vfile("fseek failed: %s", strerror(er));
				DO_SYSLOG((LOG_INFO, "%s/%s: fseek failed: %s",
						   shortname, protname(), strerror(er)));
				return ERROR;
			}
			if (Rxpos)
				zi->bytes_skipped=Rxpos;
			bytcnt = zi->bytes_sent = Rxpos;
			Lastsync = Rxpos -1;
	 		return zsendfdata(zi);
		}
	}
}

/* Send the data in the file */
static int
zsendfdata (struct zm_fileinfo *zi)
{
	static int c;
	register newcnt;
	static int junkcount;				/* Counts garbage chars received by TX */
	static long last_txpos = 0;
	static long last_bps = 0;
	static long not_printed = 0;
	static long total_sent = 0;
	static time_t low_bps=0;

#ifdef HAVE_MMAP
	if (use_mmap)
	{
		struct stat st;
		if (fstat (fileno (input_f), &st) == 0) {
			mm_size = st.st_size;
			mm_addr = mmap (0, mm_size, PROT_READ,
							MAP_SHARED, fileno (input_f), 0);
			if ((caddr_t) mm_addr == (caddr_t) - 1)
				mm_addr = NULL;
			else {
				fclose (input_f);
				input_f = NULL;
			}
		}
	}
#endif

	if (play_with_sigint)
		signal (SIGINT, onintr);

	Lrxpos = 0;
	junkcount = 0;
	Beenhereb4 = 0;
  somemore:
	if (setjmp (intrjmp)) {
	  if (play_with_sigint)
		  signal (SIGINT, onintr);
	  waitack:
		junkcount = 0;
		c = getinsync (zi, 0);
	  gotack:
		switch (c) {
		default:
			if (input_f)
				fclose (input_f);
			DO_SYSLOG((LOG_INFO, "%s/%s: got %d",
					   shortname, protname(), c));
			return ERROR;
		case ZCAN:
			if (input_f)
				fclose (input_f);
			DO_SYSLOG((LOG_INFO, "%s/%s: got ZCAN",
					   shortname, protname(), c));
			return ERROR;
		case ZSKIP:
			if (input_f)
				fclose (input_f);
			DO_SYSLOG((LOG_INFO, "%s/%s: got ZSKIP",
					   shortname, protname(), c));
			return c;
		case ZACK:
		case ZRPOS:
			break;
		case ZRINIT:
			return OK;
		}
#ifdef READCHECK
		/*
		 * If the reverse channel can be tested for data,
		 *  this logic may be used to detect error packets
		 *  sent by the receiver, in place of setjmp/longjmp
		 *  rdchk(fdes) returns non 0 if a character is available
		 */
		while (rdchk (io_mode_fd)) {
#ifdef READCHECK_READS
			switch (checked)
#else
			switch (readline (1))
#endif
			{
			case CAN:
			case ZPAD:
				c = getinsync (zi, 1);
				goto gotack;
			case XOFF:			/* Wait a while for an XON */
			case XOFF | 0200:
				readline (100);
			}
		}
#endif
	}

	newcnt = Rxbuflen;
	Txwcnt = 0;
	stohdr (zi->bytes_sent);
	zsbhdr (ZDATA, Txhdr);

	do {
		int n;
		int e;
#ifdef NEW_ERROR
		int old = blklen;
		blklen = calc_blklen (total_sent);
		total_sent += blklen + OVERHEAD;
		if (Verbose > 2 && blklen != old)
			fprintf (stderr, "blklen now %d\n", blklen);
#endif
#ifdef HAVE_MMAP
		if (mm_addr) {
			if (zi->bytes_sent + blklen < mm_size)
				n = blklen;
			else {
				n = mm_size - zi->bytes_sent;
				zi->eof_seen = 1;
			}
		} else
#endif
			n = zfilbuf (zi);
		if (zi->eof_seen)
			e = ZCRCE;
		else if (junkcount > 3)
			e = ZCRCW;
		else if (bytcnt == Lastsync)
			e = ZCRCW;
		else if (Rxbuflen && (newcnt -= n) <= 0)
			e = ZCRCW;
		else if (Txwindow && (Txwcnt += n) >= Txwspac) {
			Txwcnt = 0;
			e = ZCRCQ;
		} else
			e = ZCRCG;
		if ((Verbose > 1 || min_bps || stop_time)
			&& (not_printed > (min_bps ? 3 : 7) 
				|| zi->bytes_sent > last_bps / 2 + last_txpos)) {
			int minleft = 0;
			int secleft = 0;
			time_t now;
			last_bps = (zi->bytes_sent / timing (0,&now));
			if (last_bps > 0) {
				minleft = (zi->bytes_total - zi->bytes_sent) / last_bps / 60;
				secleft = ((zi->bytes_total - zi->bytes_sent) / last_bps) % 60;
			}
			if (min_bps) {
				if (low_bps) {
					if (last_bps<min_bps) {
						if (now-low_bps>=min_bps_time) {
							/* too bad */
							vfile(_("zsendfdata: bps rate %ld below min %ld"),
								  last_bps, min_bps);
							DO_SYSLOG((LOG_INFO, "%s/%s: bps rate low: %ld <%ld",
									   shortname, protname(), last_bps, min_bps));
							return ERROR;
						}
					} else
						low_bps=0;
				} else if (last_bps < min_bps) {
					low_bps=now;
				}
			}
			if (stop_time && now>=stop_time) {
				/* too bad */
				vfile(_("zsendfdata: reached stop time"));
				DO_SYSLOG((LOG_INFO, "%s/%s: reached stop time",
						   shortname, protname()));
				return ERROR;
			}

			if (Verbose > 1) {
				putc ('\r', stderr);
				fprintf (stderr,
						 _ ("Bytes Sent:%7ld/%7ld   BPS:%-8ld ETA %02d:%02d  "),
						 zi->bytes_sent, zi->bytes_total, last_bps, minleft, secleft);
			}
			last_txpos = zi->bytes_sent;
		} else if (Verbose)
			not_printed++;
#ifdef HAVE_MMAP
		if (mm_addr)
			zsdata (((char *) mm_addr) + zi->bytes_sent, n, e);
		else
#endif
			zsdata (txbuf, n, e);
		bytcnt = zi->bytes_sent += n;
		if (e == ZCRCW)
			goto waitack;
#ifdef READCHECK
		/*
		 * If the reverse channel can be tested for data,
		 *  this logic may be used to detect error packets
		 *  sent by the receiver, in place of setjmp/longjmp
		 *  rdchk(fdes) returns non 0 if a character is available
		 */
		fflush (stdout);
		while (rdchk (io_mode_fd)) {
#ifdef READCHECK_READS
			switch (checked)
#else
			switch (readline (1))
#endif
			{
			case CAN:
			case ZPAD:
				c = getinsync (zi, 1);
				if (c == ZACK)
					break;
				/* zcrce - dinna wanna starta ping-pong game */
				zsdata (txbuf, 0, ZCRCE);
				goto gotack;
			case XOFF:			/* Wait a while for an XON */
			case XOFF | 0200:
				readline (100);
			default:
				++junkcount;
			}
		}
#endif							/* READCHECK */
		if (Txwindow) {
			long tcount = 0;
			while ((tcount = zi->bytes_sent - Lrxpos) >= Txwindow) {
				vfile ("%ld window >= %u", tcount, Txwindow);
				if (e != ZCRCQ)
					zsdata (txbuf, 0, e = ZCRCQ);
				c = getinsync (zi, 1);
				if (c != ZACK) {
					zsdata (txbuf, 0, ZCRCE);
					goto gotack;
				}
			}
			vfile ("window = %ld", tcount);
		}
	} while (!zi->eof_seen);


	if (play_with_sigint)
		signal (SIGINT, SIG_IGN);

	for (;;) {
		stohdr (zi->bytes_sent);
		zsbhdr (ZEOF, Txhdr);
		switch (getinsync (zi, 0)) {
		case ZACK:
			continue;
		case ZRPOS:
			goto somemore;
		case ZRINIT:
			return OK;
		case ZSKIP:
			if (input_f)
				fclose (input_f);
			DO_SYSLOG((LOG_INFO, "%s/%s: got ZSKIP",
					   shortname, protname()));
			return c;
		default:
			if (input_f)
				fclose (input_f);
			DO_SYSLOG((LOG_INFO, "%s/%s: got %d",
					   shortname, protname(), c));
			return ERROR;
		}
	}
}
#ifdef NEW_ERROR
static int
calc_blklen(long total_sent)
{
	static long total_bytes=0;
	static int calcs_done=0;
	static long last_error_count=0;
	static int last_blklen=0;
	static long last_bytes_per_error=0;
	unsigned long best_bytes=0;
	long best_size=0;
	long this_bytes_per_error;
	long d;
	int i;
	if (total_bytes==0)
	{
		/* called from countem */
		total_bytes=total_sent;
		return 0;
	}

	/* it's not good to calc blklen too early */
	if (calcs_done++ < 5) {
		if (error_count && start_blklen >1024)
			return last_blklen=1024;
		else 
			last_blklen/=2;
		return last_blklen=start_blklen;
	}

	if (!error_count) {
		/* that's fine */
		if (start_blklen==max_blklen)
			return start_blklen;
		this_bytes_per_error=LONG_MAX;
		goto calcit;
	}

	if (error_count!=last_error_count) {
		/* the last block was bad. shorten blocks until one block is
		 * ok. this is because very often many errors come in an
		 * short period */
		if (error_count & 2)
		{
			last_blklen/=2;
			if (last_blklen < 32)
				last_blklen = 32;
			else if (last_blklen > 512)
				last_blklen=512;
			if (Verbose > 3)
				fprintf(stderr,"calc_blklen: reduced to %d due to error\n",
					last_blklen);
		}
		last_error_count=error_count;
		last_bytes_per_error=0; /* force recalc */
		return last_blklen;
	}

	this_bytes_per_error=total_sent / error_count;
		/* we do not get told about every error, because
		 * there may be more than one error per failed block.
		 * but one the other hand some errors are reported more
		 * than once: If a modem buffers more than one block we
		 * get at least two ZRPOS for the same position in case
		 * *one* block has to be resent.
		 * so don't do this:
		 * this_bytes_per_error/=2;
		 */
	/* there has to be a margin */
	if (this_bytes_per_error<100)
		this_bytes_per_error=100;

	/* be nice to the poor machine and do the complicated things not
	 * too often
	 */
	if (last_bytes_per_error>this_bytes_per_error)
		d=last_bytes_per_error-this_bytes_per_error;
	else
		d=this_bytes_per_error-last_bytes_per_error;
	if (d<4)
	{
		if (Verbose > 3)
		{
			fprintf(stderr,"calc_blklen: returned old value %d due to low bpe diff\n",
				last_blklen);
			fprintf(stderr,"calc_blklen: old %ld, new %ld, d %ld\n",
				last_bytes_per_error,this_bytes_per_error,d );
		}
		return last_blklen;
	}
	last_bytes_per_error=this_bytes_per_error;

calcit:
	if (Verbose > 3)
		fprintf(stderr,"calc_blklen: calc total_bytes=%ld, bpe=%ld, ec=%ld\n",
			total_bytes,this_bytes_per_error,(long) error_count);
	for (i=32;i<=max_blklen;i*=2) {
		long ok; /* some many ok blocks do we need */
		long failed; /* and that's the number of blocks not transmitted ok */
		unsigned long transmitted;
		ok=total_bytes / i + 1;
		failed=((long) i + OVERHEAD) * ok / this_bytes_per_error;
		transmitted=total_bytes + ok * OVERHEAD  
			+ failed * ((long) i+OVERHEAD+OVER_ERR);
		if (Verbose > 4)
			fprintf(stderr,"calc_blklen: blklen %d, ok %ld, failed %ld -> %lu\n",
				i,ok,failed,transmitted);
		if (transmitted < best_bytes || !best_bytes)
		{
			best_bytes=transmitted;
			best_size=i;
		}
	}
	if (best_size > 2*last_blklen)
		best_size=2*last_blklen;
	last_blklen=best_size;
	if (Verbose > 3)
		fprintf(stderr,"calc_blklen: returned %d as best\n",
			last_blklen);
	return last_blklen;
}
#endif

/*
 * Respond to receiver's complaint, get back in sync with receiver
 */
static int 
getinsync(struct zm_fileinfo *zi, int flag)
{
	register c;

	for (;;) {
		c = zgethdr(Rxhdr, 0);
		switch (c) {
		case ZCAN:
		case ZABORT:
		case ZFIN:
		case TIMEOUT:
			return ERROR;
		case ZRPOS:
			/* ************************************* */
			/*  If sending to a buffered modem, you  */
			/*   might send a break at this point to */
			/*   dump the modem's buffer.		 */
			if (input_f)
				clearerr(input_f);	/* In case file EOF seen */
#ifdef HAVE_MMAP
			if (!mm_addr)
#endif
			if (fseek(input_f, Rxpos, 0))
				return ERROR;
			zi->eof_seen = 0;
			bytcnt = Lrxpos = zi->bytes_sent = Rxpos;
			if (Lastsync == Rxpos) {
#ifndef NEW_ERROR
				if (++Beenhereb4 > 4)
					if (blklen > 32)
					{
						blklen /= 2;
						if (Verbose > 1) {
							putc('\r',stderr);
							fprintf(stderr,_("Falldown to %ld blklen"),
								blklen);
							puts("\r\n",stderr);
						}
					}
#else
				error_count++;
#endif
			}
			Lastsync = Rxpos;
			return c;
		case ZACK:
			Lrxpos = Rxpos;
			if (flag || zi->bytes_sent == Rxpos)
				return ZACK;
			continue;
		case ZRINIT:
		case ZSKIP:
			if (input_f)
				fclose(input_f);
			return c;
		case ERROR:
		default:
#ifdef NEW_ERROR
				error_count++;
#endif
			zsbhdr(ZNAK, Txhdr);
			continue;
		}
	}
}


/* Say "bibi" to the receiver, try to do it cleanly */
static void
saybibi(void)
{
	for (;;) {
		stohdr(0L);		/* CAF Was zsbhdr - minor change */
		zshhdr(ZFIN, Txhdr);	/*  to make debugging easier */
		switch (zgethdr(Rxhdr, 0)) {
		case ZFIN:
			sendline('O');
			sendline('O');
			flushmo();
		case ZCAN:
		case TIMEOUT:
			return;
		}
	}
}

/* Send command and related info */
static int 
zsendcmd(const char *buf, int blen)
{
	register c;
	long cmdnum;

	cmdnum = getpid();
	errors = 0;
	for (;;) {
		stohdr(cmdnum);
		Txhdr[ZF0] = Cmdack1;
		zsbhdr(ZCOMMAND, Txhdr);
		zsdata(buf, blen, ZCRCW);
listen:
		Rxtimeout = 100;		/* Ten second wait for resp. */
		c = zgethdr(Rxhdr, 1);

		switch (c) {
		case ZRINIT:
			goto listen;	/* CAF 8-21-87 */
		case ERROR:
		case TIMEOUT:
			if (++errors > Cmdtries)
				return ERROR;
			continue;
		case ZCAN:
		case ZABORT:
		case ZFIN:
		case ZSKIP:
		case ZRPOS:
			return ERROR;
		default:
			if (++errors > 20)
				return ERROR;
			continue;
		case ZCOMPL:
			Exitcode = Rxpos;
			saybibi();
			return OK;
		case ZRQINIT:
			vfile("******** RZ *******");
			system("rz");
			vfile("******** SZ *******");
			goto listen;
		}
	}
}

/*
 * If called as lsb use YMODEM protocol
 */
static void
chkinvok (const char *s)
{
	const char *p;

	p = s;
	while (*p == '-')
		s = ++p;
	while (*p)
		if (*p++ == '/')
			s = p;
	if (*s == 'v') {
		Verbose = 1;
		++s;
	}
	program_name = s;
	if (*s == 'l')
		s++;					/* lsz -> sz */
	protocol = ZM_ZMODEM;
	if (s[0] == 's' && s[1] == 'x')
		protocol = ZM_XMODEM;
	if (s[0] == 's' && (s[1] == 'b' || s[1] == 'y')) {
		protocol = ZM_YMODEM;
	}
}

static void
countem (int argc, char **argv)
{
	register c;
	struct stat f;

	for (Totalleft = 0, Filesleft = 0; --argc >= 0; ++argv) {
		f.st_size = -1;
		if (Verbose > 2) {
			fprintf (stderr, "\nCountem: %03d %s ", argc, *argv);
			fflush (stderr);
		}
		if (access (*argv, R_OK) >= 0 && stat (*argv, &f) >= 0) {
			c = f.st_mode & S_IFMT;
			if (c != S_IFDIR && c != S_IFBLK) {
				++Filesleft;
				Totalleft += f.st_size;
			}
		} else if (strcmp (*argv, "-") == 0) {
			++Filesleft;
			Totalleft += DEFBYTL;
		}
		if (Verbose > 2)
			fprintf (stderr, " %ld", (long) f.st_size);
	}
	if (Verbose > 2)
		fprintf (stderr, "\ncountem: Total %d %ld\n",
				 Filesleft, Totalleft);
#ifdef NEW_ERROR
	calc_blklen (Totalleft);
#endif
}

/* End of lsz.c */



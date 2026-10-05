#ifndef ZMODEM_GLOBAL_H
#define ZMODEM_GLOBAL_H

#include "config.h"
#include <sys/types.h>

#ifdef __GNUC__
# define alloca __builtin_alloca
# define HAVE_ALLOCA 1
#else
# if defined HAVE_ALLOCA_H || defined _LIBC
#  include <alloca.h>
# else
#  ifdef _AIX
#pragma alloca
#  else
#   ifndef alloca
 char *alloca ();
#   endif
#  endif
# endif
#endif

#include <stdarg.h>

#ifdef TIME_WITH_SYS_TIME
#  include <sys/time.h>
#  include <time.h>
#else
#  ifdef HAVE_SYS_TIME_H
#    include <sys/time.h>
#  else
#    include <time.h>
#  endif
#endif

#if STDC_HEADERS
# include <string.h>
#else
# ifdef HAVE_STRINGS_H
#   include <strings.h>
# endif
# ifndef HAVE_STRCHR
#  define strchr index
#  define strrchr rindex
# endif
char *strchr (), *strrchr ();
# ifndef HAVE_MEMCPY
#  define memcpy(d, s, n) bcopy ((s), (d), (n))
#  define memmove(d, s, n) bcopy ((s), (d), (n))
# endif
#endif

#include <sys/stat.h>

/* we need to decide whether readcheck is possible */
#ifdef HAVE_FCNTL_H
#  include <fcntl.h>
#endif
#ifdef HAVE_SYS_IOCTL_H
#  include <sys/ioctl.h>
#endif
#ifdef HAVE_RDCHK
#  define READCHECK
#else
#  ifdef FIONREAD
#    define READCHECK_FIONREAD
#    define READCHECK
#  else
#    ifdef F_GETFL
#      define READCHECK
#      define READCHECK_READS
#      define READCHECK_GETFL
#    endif
#  endif
#endif

#ifdef HAVE_TERMIO_H
#  include <termios.h>
#  define USE_TERMIOS
#elif defined(HAVE_SYS_TERMIOS_H)
#  include <sys/termios.h>
#  define USE_TERMIOS
#elif defined(HAVE_TERMIO_H)
#  include <termio.h>
#  define USE_TERMIO
#elif defined(HAVE_SYS_TERMIO_H)
#  include <sys/termio.h>
#  define USE_TERMIO
#elif defined(HAVE_SGTTY_H)
#  include <sgtty.h>
#  define USE_SGTTY
#  ifdef LLITOUT
long Locmode;		/* Saved "local mode" for 4.x BSD "new driver" */
long Locbit = LLITOUT;	/* Bit SUPPOSED to disable output translations */
#  endif
#else
#  error neither termio.h nor sgtty.h found. Cannot continue.
#endif

#ifdef USE_SGTTY
#  ifdef TIOCSBRK
#    define CANBREAK
#  endif
#endif
#ifdef USE_TERMIO
#  define CANBREAK
#endif



/* Take care of NLS matters.  */
#if HAVE_LOCALE_H
# include <locale.h>
#endif
#if !HAVE_SETLOCALE
# define setlocale(Category, Locale) /* empty */
#endif

#if ENABLE_NLS
# include <libintl.h>
# define _(Text) gettext (Text)
#else
# define bindtextdomain(Domain, Directory) /* empty */
# define textdomain(Domain) /* empty */
# define _(Text) Text
#endif

#ifndef ENABLE_SYSLOG
#  undef HAVE_SYSLOG
#else
#  ifdef HAVE_SYSLOG_H
#    include <syslog.h>
#  elif defined(HAVE_SYS_SYSLOG_H)
#    include <sys/syslog.h>
#  else
#    undef HAVE_SYSLOG
#  endif
#endif
#ifndef ENABLE_SYSLOG
#  define openlog(name,pid,facility) /* void it */
#  define setlogmask(x) /* void it */
#  define syslog(x,y) /* void it */
#else
extern int enable_syslog;
#endif

#if defined HAVE_UNISTD_H
# include <unistd.h>
#endif
/* The following is from pathmax.h.  */
/* Non-POSIX BSD systems might have gcc's limits.h, which doesn't define
   PATH_MAX but might cause redefinition warnings when sys/param.h is
   later included (as on MORE/BSD 4.3).  */
#if defined(_POSIX_VERSION) || (defined(HAVE_LIMITS_H) && !defined(__GNUC__))
# include <limits.h>
#endif

#ifndef _POSIX_PATH_MAX
# define _POSIX_PATH_MAX 255
#endif

#if !defined(PATH_MAX) && defined(_PC_PATH_MAX)
# define PATH_MAX (pathconf ("/", _PC_PATH_MAX) < 1 ? 1024 : pathconf ("/", _PC_PATH_MAX))
#endif

/* Don't include sys/param.h if it already has been.  */
#if defined(HAVE_SYS_PARAM_H) && !defined(PATH_MAX) && !defined(MAXPATHLEN)
# include <sys/param.h>
#endif

#if !defined(PATH_MAX) && defined(MAXPATHLEN)
# define PATH_MAX MAXPATHLEN
#endif

#ifndef PATH_MAX
# define PATH_MAX _POSIX_PATH_MAX
#endif


#define OK 0
#define FALSE 0
#define TRUE 1
#define ERROR (-1)

/* Ward Christensen / CP/M parameters - Don't change these! */
#define ENQ 005
#define CAN ('X'&037)
#define XOFF ('s'&037)
#define XON ('q'&037)
#define SOH 1
#define STX 2
#define EOT 4
#define ACK 6
#define NAK 025
#define CPMEOF 032
#define WANTCRC 0103    /* send C not NAK to get crc not checksum */
#define WANTG 0107  /* Send G not NAK to get nonstop batch xmsn */
#define TIMEOUT (-2)
#define RCDO (-3)
#define WCEOT (-10)

#define RETRYMAX 10

#define UNIXFILE 0xF000  /* The S_IFMT file mask bit for stat */

enum zm_type_enum {
	ZM_XMODEM,
	ZM_YMODEM,
	ZM_ZMODEM
};

struct zm_fileinfo {
	char *fname;
	time_t modtime;
	mode_t mode;
	size_t bytes_total;
	size_t bytes_sent;
	size_t bytes_received;
	size_t bytes_skipped; /* crash recovery */
	int    eof_seen;
};

#define R_BYTESLEFT(x) ((x)->bytes_total-(x)->bytes_received)

extern enum zm_type_enum protocol;

extern const char *program_name;        /* the name by which we were called */
extern int Verbose;
extern int errors;
extern int no_timeout;
extern int Zctlesc;    /* Encode control characters */
extern int under_rsh;

RETSIGTYPE bibi(int n);

#define sendline(c) putchar((c) & 0377)
#define xsendline(c) putchar(c)

/* zreadline.c */
extern char *readline_ptr; /* pointer for removing chars from linbuf */
extern int readline_left; /* number of buffered chars left to read */
extern int readline_readnum; /* number of chars to read with one read() */
#define READLINE_PF(timeout) \
    (--readline_left >= 0? (*readline_ptr++ & 0377) : readline(timeout))

int readline(int timeout);
void readline_purge(void);
void readline_setup(int fd, int readnum, int buffer_size);


/* rbsb.c */
extern int Fromcu;
extern int Twostop;
#ifdef READCHECK_READS
extern char checked;
#endif
extern int iofd;
extern unsigned Baudrate;

void zperr(const char *fmt, ...);
void zpfatal(const char *fmt, ...);
void vfile(const char *format, ...);

/* rbsb.c */
int from_cu(void);
void cucheck(void);
int rdchk(int fd);
int io_mode(int fd, int n);
void sendbrk(int fd);
#define flushmo() fflush(stdout)
void purgeline(int fd);


/* crctab.c */
extern unsigned short crctab[256];
#define updcrc(cp, crc) ( crctab[((crc >> 8) & 255)] ^ (crc << 8) ^ cp)
extern long cr3tab[];
#define UPDC32(b, c) (cr3tab[((int)c ^ b) & 0xff] ^ ((c >> 8) & 0x00FFFFFF))

/* zm.c */
#include "zmodem.h"
extern int Rxtimeout;        /* Tenths of seconds to wait for something */
extern int bytes_per_error;  /* generate one error around every x bytes */

/* Globals used by ZMODEM functions */
extern int Rxframeind;     /* ZBIN ZBIN32, or ZHEX type of frame received */
extern int Rxtype;     /* Type of header received */
extern int Rxcount;        /* Count of data bytes received */
extern char Rxhdr[4];      /* Received header */
extern char Txhdr[4];      /* Transmitted header */
extern long Rxpos;     /* Received file position */
extern long Txpos;     /* Transmitted file position */
extern int Txfcs32;        /* TURE means send binary frames with 32 bit FCS */
extern int Crc32t;     /* Display flag indicating 32 bit CRC being sent */
extern int Crc32;      /* Display flag indicating 32 bit CRC being received */
extern int Znulls;     /* Number of nulls to send at beginning of ZDATA hdr */
extern char Attn[ZATTNLEN+1];  /* Attention string rx sends to tx on err */

extern void zsendline(int c);
void zsbhdr(int type, char *hdr);
void zshhdr(int type, char *hdr);
void zsdata(const char *buf, int length, int frameend);
int zrdata(char *buf, int length);
int zgethdr(char *hdr, int eflag);
void stohdr(long pos);
long rclhdr(char *hdr);

const char * protname(void);

#endif

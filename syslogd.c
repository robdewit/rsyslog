/**
 * TODO:
 * - check template lines for extra characters and provide 
 *   a warning, if they exists
 * - check that no exit() is used!
 * - implement the escape-cc property replacer option
 * - it looks liek the time stamp is missing on internally-generated
 *   messages - but maybe we need to keep this for compatibility
 *   reasons.
 *
 * \brief This is what will become the rsyslogd daemon.
 *
 * Please note that as of now, a lot of the code in this file stems
 * from the sysklogd project. To learn more over this project, please
 * visit
 *
 * http://www.infodrom.org/projects/sysklogd/
 *
 * I would like to express my thanks to the developers of the sysklogd
 * package - without it, I would have had a much harder start...
 *
 * Please note that I made quite some changes to the orignal package.
 * I expect to do even more changes - up
 * to a full rewrite - to meat my design goals, which among others
 * contain a (at least) dual-thread design with a memory buffer for
 * storing received bursts of data. This is also the reason why I 
 * kind of "forked" a completely new branch of the package. My intension
 * is to do many changes and only this initial release will look
 * similar to sysklogd (well, one never knows...).
 *
 * As I have made a lot of modifications, please assume that all bugs
 * in this package are mine and not those of the sysklogd team.
 * 
 * I have decided to put my code under the GPL. The sysklog package
 * is distributed under the BSD license. As such, this package here
 * currently comes with two licenses. Both are given below. As it is
 * probably hard for you to see what was part of the sysklogd package
 * and what is part of my code, I recommend that you visit the 
 * sysklogd site on the URL above if you would like to base your
 * development on a version that is not under the GPL.
 *
 * This Project was intiated and is maintened by
 * Rainer Gerhards <rgerhards@hq.adiscon.com>. See
 * AUTHORS to learn who helped make it become a reality.
 *
 * If you have questions about rsyslogd in general, please email
 * info@adiscon.com. To learn more about rsyslogd, please visit
 * http://www.rsyslog.com.
 *
 * \author Rainer Gerhards <rgerhards@adiscon.com>
 * \date 2003-10-17
 *       Some initial modifications on the sysklogd package to support
 *       liblogging. These have actually not yet been merged to the
 *       source you see currently (but they hopefully will)
 *
 * \date 2004-10-28
 *       Restarted the modifications of sysklogd. This time, we
 *       focus on a simpler approach first. The initial goal is to
 *       provide MySQL database support (so that syslogd can log
 *       to the database).
 *
 * This license applies to the new code not in sysklogd:
 *
 * rsyslog - An Enhanced syslogd Replacement.
 * Copyright 2003-2005 Rainer Gerhards and Adiscon GmbH.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * A copy of the GPL can be found in the file "COPYING" in this distribution.
 *
 * The following copyright and license applies to the original
 * sysklogd package that was used as a basis for this release of
 * rsyslogd. Obviously, it applies to those parts stemming directly
 * back to the original sysklogd package.
 *
 * Copyright (c) 1983, 1988 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by the University of California, Berkeley.  The name of the
 * University may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#if !defined(lint) && !defined(NO_SCCS)
char copyright2[] =
"@(#) Copyright (c) 1983, 1988 Regents of the University of California.\n\
 All rights reserved.\n";
#endif /* not lint */

#if !defined(lint) && !defined(NO_SCCS)
static char sccsid[] = "@(#)rsyslogd.c	0.8 (Adiscon) 18/03/2005";
#endif /* not lint */

#ifdef __FreeBSD__
#define	BSD
#endif

#define	MAXLINE		1024		/* maximum line length */
#define DEFUPRI		(LOG_USER|LOG_NOTICE)
#define DEFSPRI		(LOG_KERN|LOG_CRIT)
#define TIMERINTVL	30		/* interval for checking flush, mark */

#define CONT_LINE	1		/* Allow continuation lines */

#ifdef MTRACE
#include <mcheck.h>
#endif
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#ifdef SYSV
#include <sys/types.h>
#endif
#include <utmp.h>
#include <ctype.h>
#include <string.h>
#include <setjmp.h>
#include <stdarg.h>
#include <time.h>

#define SYSLOG_NAMES
#include <sys/syslog.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/file.h>
#ifdef SYSV
#include <fcntl.h>
#else
#include <sys/msgbuf.h>
#endif
#include <sys/uio.h>
#include <sys/un.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <signal.h>

#include <netinet/in.h>
#include <netdb.h>
#ifndef BSD
#include <syscall.h>
#endif
#include <arpa/nameser.h>
#include <arpa/inet.h>
#include <resolv.h>
#ifndef TESTING
#include "pidfile.h"
#endif
#include "version.h"

#include <assert.h>

#ifdef	WITH_DB
#include "mysql/mysql.h" 
#include "mysql/errmsg.h"
#endif

#if defined(__linux__)
#include <paths.h>
#endif

#include "template.h"
#include "outchannel.h"
#include "syslogd.h"

/* from liblogging */
#include "liblogging-stub.h"
#include "stringbuf.h"
/* end liblogging */

#ifdef	WITH_DB
#define	_DB_MAXDBLEN	128	/* maximum number of db */
#define _DB_MAXUNAMELEN	128	/* maximum number of user name */
#define	_DB_MAXPWDLEN	128 	/* maximum number of user's pass */
#define _DB_DELAYTIMEONERROR	20	/* If an error occur we stop logging until
					   a delayed time is over */
#endif

#ifndef UTMP_FILE
#ifdef UTMP_FILENAME
#define UTMP_FILE UTMP_FILENAME
#else
#ifdef _PATH_UTMP
#define UTMP_FILE _PATH_UTMP
#else
#define UTMP_FILE "/etc/utmp"
#endif
#endif
#endif

#ifndef _PATH_LOGCONF 
#define _PATH_LOGCONF	"/etc/rsyslog.conf"
#endif

#if defined(SYSLOGD_PIDNAME)
#undef _PATH_LOGPID
#if defined(FSSTND)
#ifdef BSD
#define _PATH_VARRUN "/var/run/"
#endif
#define _PATH_LOGPID _PATH_VARRUN SYSLOGD_PIDNAME
#else
#define _PATH_LOGPID "/etc/" SYSLOGD_PIDNAME
#endif
#else
#ifndef _PATH_LOGPID
#if defined(FSSTND)
#define _PATH_LOGPID _PATH_VARRUN "rsyslogd.pid"
#else
#define _PATH_LOGPID "/etc/rsyslogd.pid"
#endif
#endif
#endif

#ifndef _PATH_DEV
#define _PATH_DEV	"/dev/"
#endif

#ifndef _PATH_CONSOLE
#define _PATH_CONSOLE	"/dev/console"
#endif

#ifndef _PATH_TTY
#define _PATH_TTY	"/dev/tty"
#endif

#ifndef _PATH_LOG
#ifdef BSD
#define _PATH_LOG	"/var/run/log"
#else
#define _PATH_LOG	"/dev/log"
#endif
#endif

char	*ConfFile = _PATH_LOGCONF;
char	*PidFile = _PATH_LOGPID;
char	ctty[] = _PATH_CONSOLE;

char	**parts;

int inetm = 0;
static int debugging_on = 0;
static int nlogs = -1;
static int restart = 0;

#define MAXFUNIX	20

int nfunix = 1;
char *funixn[MAXFUNIX] = { _PATH_LOG };
int funix[MAXFUNIX] = { -1, };

#ifdef UT_NAMESIZE
# define UNAMESZ	UT_NAMESIZE	/* length of a login name */
#else
# define UNAMESZ	8	/* length of a login name */
#endif
#define MAXUNAMES	20	/* maximum number of user names */
#define MAXFNAME	200	/* max file pathname length */

#define INTERNAL_NOPRI	0x10	/* the "no priority" priority */
#define TABLE_NOPRI	0	/* Value to indicate no priority in f_pmask */
#define TABLE_ALLPRI    0xFF    /* Value to indicate all priorities in f_pmask */
#define	LOG_MARK	LOG_MAKEPRI(LOG_NFACILITIES, 0)	/* mark "facility" */

/*
 * Flags to logmsg().
 */

#define IGN_CONS	0x001	/* don't print on console */
#define SYNC_FILE	0x002	/* do fsync on file after printing */
#define ADDDATE		0x004	/* add a date to the message */
#define MARK		0x008	/* this message is a mark */

/*
 * This table contains plain text for h_errno errors used by the
 * net subsystem.
 */
const char *sys_h_errlist[] = {
    "No problem",						/* NETDB_SUCCESS */
    "Authoritative answer: host not found",			/* HOST_NOT_FOUND */
    "Non-authoritative answer: host not found, or serverfail",	/* TRY_AGAIN */
    "Non recoverable errors",					/* NO_RECOVERY */
    "Valid name, no data record of requested type",		/* NO_DATA */
    "no address, look for MX record"				/* NO_ADDRESS */
 };

/*
 * This table lists the directive lines:
 */
const char *directive_name_list[] = {
	"template",
	"outchannel"
};
/* ... and their definitions: */
enum eDirective { DIR_TEMPLATE = 0, DIR_OUTCHANNEL = 1};

/* rgerhards 2004-11-11: the following structure represents
 * a time as it is used in syslog.
 */
struct syslogTime {
	int timeType;	/* 0 - unitinialized , 1 - RFC 3164, 2 - syslog-protocol */
	int year;
	int month;
	int day;
	int hour; /* 24 hour clock */
	int minute;
	int second;
	int secfrac;	/* fractional seconds (must be 32 bit!) */
	int secfracPrecision;
	char OffsetMode;	/* UTC offset + or - */
	char OffsetHour;	/* UTC offset in hours */
	int OffsetMinute;	/* UTC offset in minutes */
	/* full UTC offset minutes = OffsetHours*60 + OffsetMinute. Then use
	 * OffsetMode to know the direction.
	 */
};

/* rgerhards 2004-11-08: The following structure represents a
 * syslog message. 
 *
 * Important Note:
 * The message object is used for multiple purposes (once it
 * has been created). Once created, it actully is a read-only
 * object (though we do not specifically express this). In order
 * to avoid multiple copies of the same object, we use a
 * reference counter. This counter is set to 1 by the constructer
 * and increased by 1 with a call to MsgAddRef(). The destructor
 * checks the reference count. If it is more than 1, only the counter
 * will be decremented. If it is 1, however, the object is actually
 * destroyed. To make this work, it is vital that MsgAddRef() is
 * called each time a "copy" is stored somewhere.
 */
struct msg {
	int	iRefCount;	/* reference counter (0 = unused) */
	short	iSyslogVers;	/* version of syslog protocol
				 * 0 - RFC 3164
				 * 1 - RFC draft-protocol-08 */
	short	iMsgSource;	/* where did the msg originate from? */
#define SOURCE_INTERNAL 0
#define SOURCE_STDIN 1
#define SOURCE_UNIXAF 2
#define SOURCE_INET 3
	short	iSeverity;	/* the severity 0..7 */
	char *pszSeverity;	/* severity as string... */
	int iLenSeverity;	/* ... and its length. */
	int	iFacility;	/* Facility code (up to 2^32-1) */
	char *pszFacility;	/* Facility as string... */
	int iLenFacility;	/* ... and its length. */
	char *pszPRI;		/* the PRI as a string */
	int iLenPRI;		/* and its length */
	char	*pszRawMsg;	/* message as it was received on the
				 * wire. This is important in case we
				 * need to preserve cryptographic verifiers.
				 */
	int	iLenRawMsg;	/* length of raw message */
	char	*pszMSG;	/* the MSG part itself */
	int	iLenMSG;	/* Length of the MSG part */
	char	*pszUxTradMsg;	/* the traditional UNIX message */
	int	iLenUxTradMsg;/* Length of the traditional UNIX message */
	char	*pszTAG;	/* pointer to tag value */
	int	iLenTAG;	/* Length of the TAG part */
	char	*pszHOSTNAME;	/* HOSTNAME from syslog message */
	int	iLenHOSTNAME;	/* Length of HOSTNAME */
	char	*pszRcvFrom;	/* System message was received from */
	int	iLenRcvFrom;	/* Length of pszRcvFrom */
	struct syslogTime tRcvdAt;/* time the message entered this program */
	char *pszRcvdAt3164;	/* time as RFC3164 formatted string (always 15 chracters) */
	char *pszRcvdAt3339;	/* time as RFC3164 formatted string (32 chracters at most) */
	char *pszRcvdAt_MySQL;	/* rcvdAt as MySQL formatted string (always 14 chracters) */
	struct syslogTime tTIMESTAMP;/* (parsed) value of the timestamp */
	char *pszTIMESTAMP3164;	/* TIMESTAMP as RFC3164 formatted string (always 15 chracters) */
	char *pszTIMESTAMP3339;	/* TIMESTAMP as RFC3339 formatted string (32 chracters at most) */
	char *pszTIMESTAMP_MySQL;/* TIMESTAMP as MySQL formatted string (always 14 chracters) */
};

#ifdef WITH_DB
	
#endif

/*
 * This structure represents the files that will have log
 * copies printed.
 * RGerhards 2004-11-08: Each instance of the filed structure 
 * describes what I call an "output channel". This is important
 * to mention as we now allow database connections to be
 * present in the filed structure. If helps immensely, if we
 * think of it as the abstraction of an output channel.
 */

struct filed {
	struct	filed *f_next;		/* next in linked list */
	short	f_type;			/* entry type, see below */
	short	f_file;			/* file descriptor */
	off_t	f_sizeLimit;		/* file size limit, 0 = no limit */
	char	*f_sizeLimitCmd;	/* command to carry out when size limit is reached */
#ifdef	WITH_DB
	MYSQL	f_hmysql;		/* handle to MySQL */
	/* TODO: optimize memory layout / consumption; rgerhards 2004-11-19 */
	char	f_dbsrv[MAXHOSTNAMELEN+1];	/* IP or hostname of DB server*/ 
	char	f_dbname[_DB_MAXDBLEN+1];	/* DB name */
	char	f_dbuid[_DB_MAXUNAMELEN+1];	/* DB user */
	char	f_dbpwd[_DB_MAXPWDLEN+1];	/* DB user's password */
	time_t	f_timeResumeOnError;		/* 0 if no error is present,	
						   otherwise is it set to the
						   time a retrail should be attampt */
	int	f_iLastDBErrNo;			/* Last db error number. 0 = no error */
#endif
	time_t	f_time;			/* time this was last written */
	u_char	f_pmask[LOG_NFACILITIES+1];	/* priority mask */
	union {
		char	f_uname[MAXUNAMES][UNAMESZ+1];
		struct {
			char	f_hname[MAXHOSTNAMELEN+1];
			struct sockaddr_in	f_addr;
		} f_forw;		/* forwarding address */
		char	f_fname[MAXFNAME];
	} f_un;
	char	f_lasttime[16];			/* time of last occurrence */
	char	f_prevhost[MAXHOSTNAMELEN+1];	/* host from which recd. */
	int	f_prevpri;			/* pri of f_prevline */
	int	f_prevlen;			/* length of f_prevline */
	int	f_prevcount;			/* repetition cnt of prevline */
	int	f_repeatcount;			/* number of "repeated" msgs */
	int	f_flags;			/* store some additional flags */
	struct template *f_pTpl;		/* pointer to template to use */
	struct iovec *f_iov;			/* dyn allocated depinding on template */
	unsigned short *f_bMustBeFreed;		/* indicator, if iov_base must be freed to destruct */
	int	f_iIovUsed;			/* nbr of elements used in IOV */
	char	*f_psziov;			/* iov as string */
	int	f_iLenpsziov;			/* length of iov as string */
	struct msg* f_pMsg;			/* pointer to the message (this wil
					         * replace the other vars with msg
						 * content later). This is preserved after
						 * the message has been processed - it is
						 * also used to detect duplicates. */
};

/*
 * Intervals at which we flush out "message repeated" messages,
 * in seconds after previous message is logged.  After each flush,
 * we move to the next interval until we reach the largest.
 */
int	repeatinterval[] = { 30, 60 };	/* # of secs before flush */
#define	MAXREPEAT ((sizeof(repeatinterval) / sizeof(repeatinterval[0])) - 1)
#define	REPEATTIME(f)	((f)->f_time + repeatinterval[(f)->f_repeatcount])
#define	BACKOFF(f)	{ if (++(f)->f_repeatcount > MAXREPEAT) \
				 (f)->f_repeatcount = MAXREPEAT; \
			}
#ifdef SYSLOG_INET
#define INET_SUSPEND_TIME 180		/* equal to 3 minutes */
#define INET_RETRY_MAX 10		/* maximum of retries for gethostbyname() */
#endif

#define LIST_DELIMITER	':'		/* delimiter between two hosts */

/* values for f_type */
#define F_UNUSED	0		/* unused entry */
#define F_FILE		1		/* regular file */
#define F_TTY		2		/* terminal */
#define F_CONSOLE	3		/* console terminal */
#define F_FORW		4		/* remote machine */
#define F_USERS		5		/* list of users */
#define F_WALL		6		/* everyone logged on */
#define F_FORW_SUSP	7		/* suspended host forwarding */
#define F_FORW_UNKN	8		/* unknown host forwarding */
#define F_PIPE		9		/* named pipe */
#define F_MYSQL		10		/* MySQL database */
char	*TypeNames[] = {
	"UNUSED",	"FILE",		"TTY",		"CONSOLE",
	"FORW",		"USERS",	"WALL",		"FORW(SUSPENDED)",
	"FORW(UNKNOWN)", "PIPE", 	"MYSQL"
};

struct	filed *Files = NULL;
struct	filed consfile;
struct 	filed emergfile; /* this is only used for emergency logging when
			  * no actual config has been loaded.
			  */

struct code {
	char	*c_name;
	int	c_val;
};

struct code	PriNames[] = {
	{"alert",	LOG_ALERT},
	{"crit",	LOG_CRIT},
	{"debug",	LOG_DEBUG},
	{"emerg",	LOG_EMERG},
	{"err",		LOG_ERR},
	{"error",	LOG_ERR},		/* DEPRECATED */
	{"info",	LOG_INFO},
	{"none",	INTERNAL_NOPRI},	/* INTERNAL */
	{"notice",	LOG_NOTICE},
	{"panic",	LOG_EMERG},		/* DEPRECATED */
	{"warn",	LOG_WARNING},		/* DEPRECATED */
	{"warning",	LOG_WARNING},
	{"*",		TABLE_ALLPRI},
	{NULL,		-1}
};

struct code	FacNames[] = {
	{"auth",         LOG_AUTH},
	{"authpriv",     LOG_AUTHPRIV},
	{"cron",         LOG_CRON},
	{"daemon",       LOG_DAEMON},
	{"kern",         LOG_KERN},
	{"lpr",          LOG_LPR},
	{"mail",         LOG_MAIL},
	{"mark",         LOG_MARK},		/* INTERNAL */
	{"news",         LOG_NEWS},
	{"security",     LOG_AUTH},		/* DEPRECATED */
	{"syslog",       LOG_SYSLOG},
	{"user",         LOG_USER},
	{"uucp",         LOG_UUCP},
#if defined(LOG_FTP)
	{"ftp",          LOG_FTP},
#endif
	{"local0",       LOG_LOCAL0},
	{"local1",       LOG_LOCAL1},
	{"local2",       LOG_LOCAL2},
	{"local3",       LOG_LOCAL3},
	{"local4",       LOG_LOCAL4},
	{"local5",       LOG_LOCAL5},
	{"local6",       LOG_LOCAL6},
	{"local7",       LOG_LOCAL7},
	{NULL,           -1},
};

int	Debug;			/* debug flag */
char	LocalHostName[MAXHOSTNAMELEN+1];	/* our hostname */
char	*LocalDomain;		/* our local domain name */
int	InetInuse = 0;		/* non-zero if INET sockets are being used */
int	finet = -1;		/* Internet datagram socket */
int	LogPort;		/* port number for INET connections */
int	MarkInterval = 20 * 60;	/* interval between marks in seconds */
int	MarkSeq = 0;		/* mark sequence number */
int	NoFork = 0; 		/* don't fork - don't run in daemon mode */
int	AcceptRemote = 0;	/* receive messages that come via UDP */
char	**StripDomains = NULL;	/* these domains may be stripped before writing logs */
char	**LocalHosts = NULL;	/* these hosts are logged with their hostname */
int	NoHops = 1;		/* Can we bounce syslog messages through an
				   intermediate host. */
int     Initialized = 0;        /* set when we have initialized ourselves
                                 * rgerhards 2004-11-09: and by initialized, we mean that
                                 * the configuration file could be properly read AND the
                                 * syslog/udp port could be obtained (the later is debatable).
                                 * It is mainly a setting used for emergency logging: if
                                 * something really goes wild, we can not do as indicated in
                                 * the log file, but we still log messages to the system
                                 * console. This is probably the best that can be done in
                                 * such a case.
                                 */


extern	int errno;

/* hardcoded standard templates (used for defaults) */
static char template_TraditionalFormat[] = "\"%timegenerated% %HOSTNAME% %syslogtag%%msg:::drop-last-lf%\n\"";
static char template_WallFmt[] = "\"\r\n\7Message from syslogd@%HOSTNAME% at %timegenerated% ...\r\n %syslogtag%%msg%\n\r\"";
static char template_StdFwdFmt[] = "\"<%PRI%>%TIMESTAMP% %HOSTNAME% %syslogtag%%msg%\"";
static char template_StdUsrMsgFmt[] = "\" %syslogtag%%msg%\n\r\"";
static char template_StdDBFmt[] = "\"insert into SystemEvents (Message, Facility, FromHost, Priority, DeviceReportedTime, ReceivedAt, InfoUnitID, SysLogTag) values ('%msg%', %syslogfacility%, '%HOSTNAME%', %syslogpriority%, '%timereported:::date-mysql%', '%timegenerated:::date-mysql%', %iut%, '%syslogtag%')\",SQL";
/* end template */

/* Function prototypes. */
int main(int argc, char **argv);
char **crunch_list(char *list);
int usage(void);
void untty(void);
void printchopped(char *hname, char *msg, int len, int fd, int iSourceType);
void printline(char *hname, char *msg, int iSource);
void printsys(char *msg);
void logmsg(int pri, struct msg*, int flags);
void fprintlog(register struct filed *f, int flags);
void endtty();
void wallmsg(register struct filed *f);
void reapchild();
const char *cvthname(struct sockaddr_in *f);
void domark();
void debug_switch();
void logerror(char *type);
void die(int sig);
#ifndef TESTING
void doexit(int sig);
#endif
void init();
void cfline(char *line, register struct filed *f);
int decode(char *name, struct code *codetab);
void sighup_handler();
#ifdef WITH_DB
void initMySQL(register struct filed *f);
void writeMySQL(register struct filed *f);
void closeMySQL(register struct filed *f);
void reInitMySQL(register struct filed *f);
int checkDBErrorState(register struct filed *f);
void DBErrorHandler(register struct filed *f);
#endif

int getSubString(char **pSrc, char *pDst, size_t DstSize, char cSep);
void getCurrTime(struct syslogTime *t);
void cflineSetTemplateAndIOV(struct filed *f, char *pTemplateName);

#ifdef SYSLOG_UNIXAF
static int create_inet_socket();
#endif


/*******************************************************************
 * BEGIN CODE-LIBLOGGING                                           *
 *******************************************************************
 * Code in this section is borrowed from liblogging. This is an
 * interim solution. Once liblogging is fully integrated, this is
 * to be removed (see http://www.monitorware.com/liblogging for
 * more details. 2004-11-16 rgerhards
 *
 * Please note that the orginal liblogging code is modified so that
 * it fits into the context of the current version of syslogd.c.
 *
 * DO NOT PUT ANY OTHER CODE IN THIS BEGIN ... END BLOCK!!!!
 */

#define FALSE 0
#define TRUE 1
/**
 * Parse a 32 bit integer number from a string.
 *
 * \param ppsz Pointer to the Pointer to the string being parsed. It
 *             must be positioned at the first digit. Will be updated 
 *             so that on return it points to the first character AFTER
 *             the integer parsed.
 * \retval The number parsed.
 */

int srSLMGParseInt32(unsigned char** ppsz)
{
	int i;

	i = 0;
	while(isdigit(**ppsz))
	{
		i = i * 10 + **ppsz - '0';
		++(*ppsz);
	}

	return i;
}
/**
 * Parse a TIMESTAMP-3164.
 * Returns TRUE on parse OK, FALSE on parse error.
 */
static int srSLMGParseTIMESTAMP3164(struct syslogTime *pTime, unsigned char* pszTS)
{
	assert(pTime != NULL);
	assert(pszTS != NULL);

	getCurrTime(pTime);	/* obtain the current year and UTC offsets! */

	/* If we look at the month (Jan, Feb, Mar, Apr, May, Jun, Jul, Aug, Sep, Oct, Nov, Dec),
	 * we may see the following character sequences occur:
	 *
	 * J(an/u(n/l)), Feb, Ma(r/y), A(pr/ug), Sep, Oct, Nov, Dec
	 *
	 * We will use this for parsing, as it probably is the
	 * fastest way to parse it.
	 */
	switch(*pszTS++)
	{
	case 'J':
		if(*pszTS++ == 'a')
			if(*pszTS++ == 'n')
				pTime->month = 1;
			else
				return FALSE;
		else if(*pszTS++ == 'u')
			if(*pszTS++ == 'n')
				pTime->month = 6;
			else if(*pszTS++ == 'l')
				pTime->month = 7;
			else
				return FALSE;
		else
			return FALSE;
		break;
	case 'F':
		if(*pszTS++ == 'e')
			if(*pszTS++ == 'b')
				pTime->month = 2;
			else
				return FALSE;
		else
			return FALSE;
		break;
	case 'M':
		if(*pszTS++ == 'a')
			if(*pszTS++ == 'r')
				pTime->month = 3;
			else if(*pszTS++ == 'y')
				pTime->month = 5;
			else
				return FALSE;
		else
			return FALSE;
		break;
	case 'A':
		if(*pszTS++ == 'p')
			if(*pszTS++ == 'r')
				pTime->month = 4;
			else
				return FALSE;
		else if(*pszTS++ == 'u')
			if(*pszTS++ == 'g')
				pTime->month = 8;
			else
				return FALSE;
		else
			return FALSE;
		break;
	case 'S':
		if(*pszTS++ == 'e')
			if(*pszTS++ == 'p')
				pTime->month = 9;
			else
				return FALSE;
		else
			return FALSE;
		break;
	case 'O':
		if(*pszTS++ == 'c')
			if(*pszTS++ == 't')
				pTime->month = 10;
			else
				return FALSE;
		else
			return FALSE;
		break;
	case 'N':
		if(*pszTS++ == 'o')
			if(*pszTS++ == 'v')
				pTime->month = 11;
			else
				return FALSE;
		else
			return FALSE;
		break;
	case 'D':
		if(*pszTS++ == 'e')
			if(*pszTS++ == 'c')
				pTime->month = 12;
			else
				return FALSE;
		else
			return FALSE;
		break;
	default:
		return FALSE;
	}

	/* done month */

	if(*pszTS++ != ' ')
		return FALSE;

	/* we accept a slightly malformed timestamp when receiving. This is
	 * we accept one-digit days
	 */
	if(*pszTS == ' ')
		++pszTS;

	pTime->day = srSLMGParseInt32(&pszTS);
	if(pTime->day < 1 || pTime->day > 31)
		return FALSE;

	if(*pszTS++ != ' ')
		return FALSE;
	pTime->hour = srSLMGParseInt32(&pszTS);
	if(pTime->hour < 0 || pTime->hour > 23)
		return FALSE;

	if(*pszTS++ != ':')
		return FALSE;
	pTime->minute = srSLMGParseInt32(&pszTS);
	if(pTime->minute < 0 || pTime->minute > 59)
		return FALSE;

	if(*pszTS++ != ':')
		return FALSE;
	pTime->second = srSLMGParseInt32(&pszTS);
	if(pTime->second < 0 || pTime->second > 60)
		return FALSE;

	/* OK, we actually have a 3164 timestamp, so let's indicate this
	 * and fill the rest of the properties. */
	pTime->timeType = 1;
 	pTime->secfracPrecision = 0;
	pTime->secfrac = 0;
	return TRUE;
}

/*******************************************************************
 * END CODE-LIBLOGGING                                             *
 *******************************************************************/

/**
 * Format a syslogTimestamp into format required by MySQL.
 * We are using the 14 digits format. For example 20041111122600 
 * is interpreted as '2004-11-11 12:26:00'. 
 * The caller must provide the timestamp as well as a character
 * buffer that will receive the resulting string. The function
 * returns the size of the timestamp written in bytes (without
 * the string terminator). If 0 is returend, an error occured.
 */
int formatTimestampToMySQL(struct syslogTime *ts, char* pDst, size_t iLenDst)
{
	/* TODO: currently we do not consider localtime/utc */
	assert(ts != NULL);
	assert(pDst != NULL);

	if (iLenDst < 15) /* we need at least 14 bytes
			     14 digits for timestamp + '\n' */
		return(0); 

	return(snprintf(pDst, iLenDst, "%4.4d%2.2d%2.2d%2.2d%2.2d%2.2d", 
		ts->year, ts->month, ts->day, ts->hour, ts->minute, ts->second));

}

/**
 * Format a syslogTimestamp to a RFC3339 timestamp string (as
 * specified in syslog-protocol).
 * The caller must provide the timestamp as well as a character
 * buffer that will receive the resulting string. The function
 * returns the size of the timestamp written in bytes (without
 * the string terminator). If 0 is returend, an error occured.
 */
int formatTimestamp3339(struct syslogTime *ts, char* pBuf, size_t iLenBuf)
{
	int iRet;

	assert(ts != NULL);
	assert(pBuf != NULL);
	
	if(iLenBuf < 20)
		return(0); /* we NEED at least 20 bytes */

	if(ts->secfracPrecision > 0)
	{	/* we now need to include fractional seconds. While doing so, we must look at
		 * the precision specified. For example, if we have millisec precision (3 digits), a
		 * secFrac value of 12 is not equivalent to ".12" but ".012". Obviously, this
		 * is a huge difference ;). To avoid this, we first create a format string with
		 * the specific precision and *then* use that format string to do the actual
		 * formating (mmmmhhh... kind of self-modifying code... ;)).
		 */
		char szFmtStr[64];
		/* be careful: there is ONE actual %d in the format string below ;) */
		snprintf(szFmtStr, sizeof(szFmtStr),
		         "%%04d-%%02d-%%02dT%%02d:%%02d:%%02d.%%0%dd%%c%%02d:%%02d ",
			ts->secfracPrecision);
		iRet = snprintf(pBuf, iLenBuf, szFmtStr, ts->year, ts->month, ts->day,
			        ts->hour, ts->minute, ts->second, ts->secfrac,
				ts->OffsetMode, ts->OffsetHour, ts->OffsetMinute);
	}
	else
		iRet = snprintf(pBuf, iLenBuf,
		 		"%4.4d-%2.2d-%2.2dT%2.2d:%2.2d:%2.2d%c%2.2d:%2.2d ",
				ts->year, ts->month, ts->day,
			        ts->hour, ts->minute, ts->second,
				ts->OffsetMode, ts->OffsetHour, ts->OffsetMinute);
	return(iRet);
}

/**
 * Format a syslogTimestamp to a RFC3164 timestamp sring.
 * The caller must provide the timestamp as well as a character
 * buffer that will receive the resulting string. The function
 * returns the size of the timestamp written in bytes (without
 * the string termnator). If 0 is returend, an error occured.
 */
int formatTimestamp3164(struct syslogTime *ts, char* pBuf, size_t iLenBuf)
{
	static char* monthNames[13] = {"ERR", "Jan", "Feb", "Mar",
	                               "Apr", "May", "Jun", "Jul",
				       "Aug", "Sep", "Oct", "Nov", "Dec"};
	assert(ts != NULL);
	assert(pBuf != NULL);
	
	if(iLenBuf < 16)
		return(0); /* we NEED 16 bytes */
	return(snprintf(pBuf, iLenBuf, "%s %2d %2.2d:%2.2d:%2.2d",
		monthNames[ts->month], ts->day, ts->hour,
		ts->minute, ts->second
		));
}

/**
 * Format a syslogTimestamp to a text format.
 * The caller must provide the timestamp as well as a character
 * buffer that will receive the resulting string. The function
 * returns the size of the timestamp written in bytes (without
 * the string termnator). If 0 is returend, an error occured.
 */
int formatTimestamp(struct syslogTime *ts, char* pBuf, size_t iLenBuf)
{
	assert(ts != NULL);
	assert(pBuf != NULL);
	
	if(ts->timeType == 1) {
		return(formatTimestamp3164(ts, pBuf, iLenBuf));
	}

	if(ts->timeType == 2) {
		return(formatTimestamp3339(ts, pBuf, iLenBuf));
	}

	return(0);
}


/**
 * Get the current date/time in the best resolution the operating
 * system has to offer (well, actually at most down to the milli-
 * second level.
 *
 * The date and time is returned in separate fields as this is
 * most portable and removes the need for additional structures
 * (but I have to admit it is somewhat "bulky";)).
 *
 * Obviously, all caller-provided pointers must not be NULL...
 */
void getCurrTime(struct syslogTime *t)
{
	struct timeval tp;
	struct tm *tm;
	long lBias;

	assert(t != NULL);
	gettimeofday(&tp, NULL);
	tm = localtime((time_t*) &(tp.tv_sec));

	t->year = tm->tm_year + 1900;
	t->month = tm->tm_mon + 1;
	t->day = tm->tm_mday;
	t->hour = tm->tm_hour;
	t->minute = tm->tm_min;
	t->second = tm->tm_sec;
	t->secfrac = tp.tv_usec;
	t->secfracPrecision = 6;

	lBias = tm->tm_gmtoff;
	if(lBias < 0)
	{
		t->OffsetMode = '-';
		lBias *= -1;
	}
	else
		t->OffsetMode = '+';
	t->OffsetHour = lBias / 3600;
	t->OffsetMinute = lBias % 3600;
}

/* rgerhards 2004-11-09: the following function is used to 
 * log emergency message when syslogd has no way of using
 * regular meas of processing. It is supposed to be 
 * primarily be called when there is memory shortage. As
 * we now rely on dynamic memory allocation for the messages,
 * we can no longer act correctly when we do not receive
 * memory.
 */
void syslogdPanic(char* ErrMsg)
{
	/* TODO: provide a meaningful implementation! */
	dprintf("rsyslogdPanic: '%s'\n", ErrMsg);
}

/* rgerhards 2004-11-09: helper routines for handling the
 * message object. We do only the most important things. It
 * is our firm hope that this will sooner or later be
 * obsoleted by liblogging.
 */


/* "Constructor" for a msg "object". Returns a pointer to
 * the new object or NULL if no such object could be allocated.
 * An object constructed via this function should only be destroyed
 * via "MsgDestruct()".
 */
struct msg* MsgConstruct()
{
	struct msg *pM;

	if((pM = calloc(1, sizeof(struct msg))) != NULL)
	{ /* initialize members that are non-zero */
		pM->iRefCount = 1;
		pM->iSyslogVers = -1;
		pM->iSeverity = -1;
		pM->iFacility = -1;
		getCurrTime(&(pM->tRcvdAt));
	}

	/* DEV debugging only! dprintf("MsgConstruct\t0x%x\n", (int)pM);*/

	return(pM);
}


/* Destructor for a msg "object". Must be called to dispose
 * of a msg object.
 */
void MsgDestruct(struct msg * pM)
{
	assert(pM != NULL);
	/* DEV Debugging only ! dprintf("MsgDestruct\t0x%x, Ref now: %d\n", (int)pM, pM->iRefCount - 1); */
	if(--pM->iRefCount == 0)
	{
		/* DEV Debugging Only! dprintf("MsgDestruct\t0x%x, RefCount now 0, doing DESTROY\n", (int)pM); */
		if(pM->pszUxTradMsg != NULL)
			free(pM->pszUxTradMsg);
		if(pM->pszRawMsg != NULL)
			free(pM->pszRawMsg);
		if(pM->pszTAG != NULL)
			free(pM->pszTAG);
		if(pM->pszHOSTNAME != NULL)
			free(pM->pszHOSTNAME);
		if(pM->pszRcvFrom != NULL)
			free(pM->pszRcvFrom);
		if(pM->pszMSG != NULL)
			free(pM->pszMSG);
		if(pM->pszFacility != NULL)
			free(pM->pszFacility);
		if(pM->pszSeverity != NULL)
			free(pM->pszSeverity);
		if(pM->pszRcvdAt3164 != NULL)
			free(pM->pszRcvdAt3164);
		if(pM->pszRcvdAt3339 != NULL)
			free(pM->pszRcvdAt3339);
		if(pM->pszRcvdAt_MySQL != NULL)
			free(pM->pszRcvdAt_MySQL);
		if(pM->pszTIMESTAMP3164 != NULL)
			free(pM->pszTIMESTAMP3164);
		if(pM->pszTIMESTAMP3339 != NULL)
			free(pM->pszTIMESTAMP3339);
		if(pM->pszTIMESTAMP_MySQL != NULL)
			free(pM->pszTIMESTAMP_MySQL);
		if(pM->pszPRI != NULL)
			free(pM->pszPRI);
		free(pM);
	}
}

/* Increment reference count - see description of the "msg"
 * structure for details. As a convenience to developers,
 * this method returns the msg pointer that is passed to it.
 * It is recommended that it is called as follows:
 *
 * pSecondMsgPointer = MsgAddRef(pOrgMsgPointer);
 */
struct msg *MsgAddRef(struct msg *pM)
{
	assert(pM != NULL);
	pM->iRefCount++;
	/* DEV debugging only! dprintf("MsgAddRef\t0x%x done, Ref now: %d\n", (int)pM, pM->iRefCount);*/
	return(pM);
}

/* Access methods - dumb & easy, not a comment for each ;)
 */
int getMSGLen(struct msg *pM)
{
	return((pM == NULL) ? 0 : pM->iLenMSG);
}


char *getRawMsg(struct msg *pM)
{
	if(pM == NULL)
		return "";
	else
		if(pM->pszRawMsg == NULL)
			return "";
		else
			return pM->pszRawMsg;
}

char *getUxTradMsg(struct msg *pM)
{
	if(pM == NULL)
		return "";
	else
		if(pM->pszUxTradMsg == NULL)
			return "";
		else
			return pM->pszUxTradMsg;
}

char *getMSG(struct msg *pM)
{
	if(pM == NULL)
		return "";
	else
		if(pM->pszMSG == NULL)
			return "";
		else
			return pM->pszMSG;
}


char *getPRI(struct msg *pM)
{
	if(pM == NULL)
		return "";

	if(pM->pszPRI == NULL) {
		/* OK, we need to construct it... 
		 * we use a 5 byte buffer - as of 
		 * RFC 3164, it can't be longer. Should it
		 * still be, snprintf will truncate...
		 */
		if((pM->pszPRI = malloc(5)) == NULL) return "";
		pM->iLenPRI = snprintf(pM->pszPRI, 5, "%d",
			LOG_MAKEPRI(pM->iFacility, pM->iSeverity));
	}

	return pM->pszPRI;
}


char *getTimeReported(struct msg *pM, enum tplFormatTypes eFmt)
{
	if(pM == NULL)
		return "";

	switch(eFmt) {
	case tplFmtDefault:
		if(pM->pszTIMESTAMP3164 == NULL) {
			if((pM->pszTIMESTAMP3164 = malloc(16)) == NULL) return "";
			formatTimestamp3164(&pM->tTIMESTAMP, pM->pszTIMESTAMP3164, 16);
		}
		return(pM->pszTIMESTAMP3164);
	case tplFmtMySQLDate:
		if(pM->pszTIMESTAMP_MySQL == NULL) {
			if((pM->pszTIMESTAMP_MySQL = malloc(15)) == NULL) return "";
			formatTimestampToMySQL(&pM->tTIMESTAMP, pM->pszTIMESTAMP_MySQL, 15);
		}
		return(pM->pszTIMESTAMP_MySQL);
	case tplFmtRFC3164Date:
		if(pM->pszTIMESTAMP3164 == NULL) {
			if((pM->pszTIMESTAMP3164 = malloc(16)) == NULL) return "";
			formatTimestamp3164(&pM->tTIMESTAMP, pM->pszTIMESTAMP3164, 16);
		}
		return(pM->pszTIMESTAMP3164);
	case tplFmtRFC3339Date:
		if(pM->pszTIMESTAMP3339 == NULL) {
			if((pM->pszTIMESTAMP3339 = malloc(33)) == NULL) return "";
			formatTimestamp3339(&pM->tTIMESTAMP, pM->pszTIMESTAMP3339, 33);
		}
		return(pM->pszTIMESTAMP3339);
	}
	return "INVALID eFmt OPTION!";
}

char *getTimeGenerated(struct msg *pM, enum tplFormatTypes eFmt)
{
	if(pM == NULL)
		return "";

	switch(eFmt) {
	case tplFmtDefault:
		if(pM->pszRcvdAt3164 == NULL) {
			if((pM->pszRcvdAt3164 = malloc(16)) == NULL) return "";
			formatTimestamp3164(&pM->tRcvdAt, pM->pszRcvdAt3164, 16);
		}
		return(pM->pszRcvdAt3164);
	case tplFmtMySQLDate:
		if(pM->pszRcvdAt_MySQL == NULL) {
			if((pM->pszRcvdAt_MySQL = malloc(15)) == NULL) return "";
			formatTimestampToMySQL(&pM->tRcvdAt, pM->pszRcvdAt_MySQL, 15);
		}
		return(pM->pszRcvdAt_MySQL);
	case tplFmtRFC3164Date:
		if((pM->pszRcvdAt3164 = malloc(16)) == NULL) return "";
		formatTimestamp3164(&pM->tRcvdAt, pM->pszRcvdAt3164, 16);
		return(pM->pszRcvdAt3164);
	case tplFmtRFC3339Date:
		if(pM->pszRcvdAt3339 == NULL) {
			if((pM->pszRcvdAt3339 = malloc(33)) == NULL) return "";
			formatTimestamp3339(&pM->tRcvdAt, pM->pszRcvdAt3339, 33);
		}
		return(pM->pszRcvdAt3339);
	}
	return "INVALID eFmt OPTION!";
}


char *getSeverity(struct msg *pM)
{
	if(pM == NULL)
		return "";

	if(pM->pszSeverity == NULL) {
		/* we use a 2 byte buffer - can only be one digit */
		if((pM->pszSeverity = malloc(2)) == NULL) return "";
		pM->iLenSeverity =
		   snprintf(pM->pszSeverity, 2, "%d", pM->iSeverity);
	}
	return(pM->pszSeverity);
}

char *getFacility(struct msg *pM)
{
	if(pM == NULL)
		return "";

	if(pM->pszFacility == NULL) {
		/* we use a 12 byte buffer - as of 
		 * syslog-protocol, facility can go
		 * up to 2^32 -1
		 */
		if((pM->pszFacility = malloc(12)) == NULL) return "";
		pM->iLenFacility =
		   snprintf(pM->pszFacility, 12, "%d", pM->iFacility);
	}
	return(pM->pszFacility);
}


char *getTAG(struct msg *pM)
{
	if(pM == NULL)
		return "";
	else
		if(pM->pszTAG == NULL)
			return "";
		else
			return pM->pszTAG;
}


char *getHOSTNAME(struct msg *pM)
{
	if(pM == NULL)
		return "";
	else
		if(pM->pszHOSTNAME == NULL)
			return "";
		else
			return pM->pszHOSTNAME;
}


char *getRcvFrom(struct msg *pM)
{
	if(pM == NULL)
		return "";
	else
		if(pM->pszRcvFrom == NULL)
			return "";
		else
			return pM->pszRcvFrom;
}


/* rgerhards 2004-11-16: set pszRcvFrom in msg object
 * returns 0 if OK, other value if not. In case of failure,
 * logs error message and destroys msg object.
 */
int MsgSetRcvFrom(struct msg *pMsg, char* pszRcvFrom)
{
	assert(pMsg != NULL);
	if(pMsg->pszRcvFrom != NULL)
		free(pMsg->pszRcvFrom);

	pMsg->iLenRcvFrom = strlen(pszRcvFrom);
	if((pMsg->pszRcvFrom = malloc(pMsg->iLenRcvFrom + 1)) == NULL) {
		syslogdPanic("Could not allocate memory for pszRcvFrom buffer.");
		MsgDestruct(pMsg);
		return(-1);
	}
	memcpy(pMsg->pszRcvFrom, pszRcvFrom, pMsg->iLenRcvFrom + 1);

	return(0);
}


/* Set the HOSTNAME to a caller-provided string. This is thought
 * to be a heap buffer that the caller will no longer use. This
 * function is a performance optimization over MsgSetHOSTNAME().
 * rgerhards 2004-11-19
 */
void MsgAssignHOSTNAME(struct msg *pMsg, char *pBuf)
{
	assert(pMsg != NULL);
	assert(pBuf != NULL);
	pMsg->iLenHOSTNAME = strlen(pBuf);
	pMsg->pszHOSTNAME = pBuf;
}


/* rgerhards 2004-11-09: set HOSTNAME in msg object
 * returns 0 if OK, other value if not. In case of failure,
 * logs error message and destroys msg object.
 */
int MsgSetHOSTNAME(struct msg *pMsg, char* pszHOSTNAME)
{
	assert(pMsg != NULL);
	if(pMsg->pszHOSTNAME != NULL)
		free(pMsg->pszHOSTNAME);

	pMsg->iLenHOSTNAME = strlen(pszHOSTNAME);
	if((pMsg->pszHOSTNAME = malloc(pMsg->iLenHOSTNAME + 1)) == NULL) {
		syslogdPanic("Could not allocate memory for pszHOSTNAME buffer.");
		MsgDestruct(pMsg);
		return(-1);
	}
	memcpy(pMsg->pszHOSTNAME, pszHOSTNAME, pMsg->iLenHOSTNAME + 1);

	return(0);
}


/* Set the TAG to a caller-provided string. This is thought
 * to be a heap buffer that the caller will no longer use. This
 * function is a performance optimization over MsgSetTAG().
 * rgerhards 2004-11-19
 */
void MsgAssignTAG(struct msg *pMsg, char *pBuf)
{
	assert(pMsg != NULL);
	assert(pBuf != NULL);
	pMsg->iLenTAG = strlen(pBuf);
	pMsg->pszTAG = pBuf;
}


/* rgerhards 2004-11-16: set TAG in msg object
 * returns 0 if OK, other value if not. In case of failure,
 * logs error message and destroys msg object.
 */
int MsgSetTAG(struct msg *pMsg, char* pszTAG)
{
	assert(pMsg != NULL);
	pMsg->iLenTAG = strlen(pszTAG);
	if((pMsg->pszTAG = malloc(pMsg->iLenTAG + 1)) == NULL) {
		syslogdPanic("Could not allocate memory for pszTAG buffer.");
		MsgDestruct(pMsg);
		return(-1);
	}
	memcpy(pMsg->pszTAG, pszTAG, pMsg->iLenTAG + 1);

	return(0);
}


/* Set the UxTradMsg to a caller-provided string. This is thought
 * to be a heap buffer that the caller will no longer use. This
 * function is a performance optimization over MsgSetUxTradMsg().
 * rgerhards 2004-11-19
 */
void MsgAssignUxTradMsg(struct msg *pMsg, char *pBuf)
{
	assert(pMsg != NULL);
	assert(pBuf != NULL);
	pMsg->iLenUxTradMsg = strlen(pBuf);
	pMsg->pszUxTradMsg = pBuf;
}


/* rgerhards 2004-11-17: set the traditional Unix message in msg object
 * returns 0 if OK, other value if not. In case of failure,
 * logs error message and destroys msg object.
 */
int MsgSetUxTradMsg(struct msg *pMsg, char* pszUxTradMsg)
{
	assert(pMsg != NULL);
	assert(pszUxTradMsg != NULL);
	pMsg->iLenUxTradMsg = strlen(pszUxTradMsg);
	if(pMsg->pszUxTradMsg != NULL)
		free(pMsg->pszUxTradMsg);
	if((pMsg->pszUxTradMsg = malloc(pMsg->iLenUxTradMsg + 1)) == NULL) {
		syslogdPanic("Could not allocate memory for pszUxTradMsg buffer.");
		MsgDestruct(pMsg);
		return(-1);
	}
	memcpy(pMsg->pszUxTradMsg, pszUxTradMsg, pMsg->iLenUxTradMsg + 1);

	return(0);
}


/* rgerhards 2004-11-09: set MSG in msg object
 * returns 0 if OK, other value if not. In case of failure,
 * logs error message and destroys msg object.
 */
int MsgSetMSG(struct msg *pMsg, char* pszMSG)
{
	assert(pMsg != NULL);
	assert(pszMSG != NULL);

	if(pMsg->pszMSG != NULL) {
		free(pMsg->pszMSG);
	}
	pMsg->iLenMSG = strlen(pszMSG);
	if((pMsg->pszMSG = malloc(pMsg->iLenMSG + 1)) == NULL) {
		syslogdPanic("Could not allocate memory for pszMSG buffer.");
		MsgDestruct(pMsg);
		return(-1);
	}
	memcpy(pMsg->pszMSG, pszMSG, pMsg->iLenMSG + 1);

	return(0);
}

/* rgerhards 2004-11-11: set RawMsg in msg object
 * returns 0 if OK, other value if not. In case of failure,
 * logs error message and destroys msg object.
 */
int MsgSetRawMsg(struct msg *pMsg, char* pszRawMsg)
{
	assert(pMsg != NULL);
	if(pMsg->pszRawMsg != NULL) {
		free(pMsg->pszRawMsg);
	}
	pMsg->iLenRawMsg = strlen(pszRawMsg);
	if((pMsg->pszRawMsg = malloc(pMsg->iLenRawMsg + 1)) == NULL) {
		syslogdPanic("Could not allocate memory for pszRawMsg buffer.");
		MsgDestruct(pMsg);
		return(-1);
	}
	memcpy(pMsg->pszRawMsg, pszRawMsg, pMsg->iLenRawMsg + 1);

	return(0);
}


/* This function returns a string-representation of the 
 * requested message property. This is a generic function used
 * to abstract properties so that these can be easier
 * queried. Returns NULL if property could not be found.
 * Actually, this function is a big if..elseif. What it does
 * is simply to map property names (from MonitorWare) to the
 * message object data fields.
 *
 * In case we need string forms of propertis we do not
 * yet have in string form, we do a memory allocation that
 * is sufficiently large (in all cases). Once the string
 * form has been obtained, it is saved until the Msg object
 * is finally destroyed. This is so that we save the processing
 * time in the (likely) case that this property is requested
 * again. It also saves us a lot of dynamic memory management
 * issues in the upper layers, because we so can guarantee that
 * the buffer will remain static AND available during the lifetime
 * of the object. Please note that both the max size allocation as
 * well as keeping things in memory might like look like a 
 * waste of memory (some might say it actually is...) - we
 * deliberately accept this because performance is more important
 * to us ;)
 * rgerhards 2004-11-18
 * Parameter "bMustBeFreed" is set by this function. It tells the
 * caller whether or not the string returned must be freed by the
 * caller itself. It is is 0, the caller MUST NOT free it. If it is
 * 1, the caller MUST free 1. Handling this wrongly leads to either
 * a memory leak of a program abort (do to double-frees or frees on
 * the constant memory pool). So be careful to do it right.
 * rgerhards 2004-11-23
 */
char *MsgGetProp(struct msg *pMsg, struct templateEntry *pTpe, unsigned short *pbMustBeFreed)
{
	char *pName;
	char *pRes; /* result pointer */

	assert(pMsg != NULL);
	assert(pTpe != NULL);
	assert(pbMustBeFreed != NULL);

	pName = pTpe->data.field.pPropRepl;
	*pbMustBeFreed = 0;

	/* sometimes there are aliases to the original MonitoWare
	 * property names. These come after || in the ifs below. */
	if(!strcmp(pName, "msg")) {
		pRes = getMSG(pMsg);
	} else if(!strcmp(pName, "rawmsg")) {
		pRes = getRawMsg(pMsg);
	} else if(!strcmp(pName, "UxTradMsg")) {
		pRes = getUxTradMsg(pMsg);
	} else if(!strcmp(pName, "source")
		  || !strcmp(pName, "HOSTNAME")) {
		pRes = getHOSTNAME(pMsg);
	} else if(!strcmp(pName, "syslogtag")) {
		pRes = getTAG(pMsg);
	} else if(!strcmp(pName, "PRI")) {
		pRes = getPRI(pMsg);
	} else if(!strcmp(pName, "iut")) {
		pRes = "1"; /* always 1 for syslog messages (a MonitorWare thing;)) */
	} else if(!strcmp(pName, "syslogfacility")) {
		pRes = getFacility(pMsg);
	} else if(!strcmp(pName, "syslogpriority")) {
		pRes = getSeverity(pMsg);
	} else if(!strcmp(pName, "timegenerated")) {
		pRes = getTimeGenerated(pMsg, pTpe->data.field.eDateFormat);
	} else if(!strcmp(pName, "timereported")
		  || !strcmp(pName, "TIMESTAMP")) {
		pRes = getTimeReported(pMsg, pTpe->data.field.eDateFormat);
	} else {
		pRes = "**INVALID PROPERTY NAME**";
	}
	
	/* Now check if we need to make "temporary" transformations (these
	 * are transformations that do not go back into the message -
	 * memory must be allocated for them!).
	 */
	
	/* substring extraction */
	if(pTpe->data.field.iFromPos != 0 || pTpe->data.field.iToPos != 0) {
		/* we need to obtain a private copy */
		int iLen;
		int iFrom, iTo;
		char *pBufStart;
		char *pBuf;
		iFrom = pTpe->data.field.iFromPos;
		iTo = pTpe->data.field.iToPos;
		/* need to zero-base to and from (they are 1-based!) */
		if(iFrom > 0)
			--iFrom;
		if(iTo > 0)
			--iTo;
		iLen = iTo - iFrom + 1; /* the +1 is for an actual char, NOT \0! */
		pBufStart = pBuf = malloc((iLen + 1) * sizeof(char));
		if(pBuf == NULL) {
			if(*pbMustBeFreed == 1)
				free(pRes);
			*pbMustBeFreed = 0;
			return "**OUT OF MEMORY**";
		}
		if(iFrom) {
		/* skip to the start of the substring (can't do pointer arithmetic
		 * because the whole string might be smaller!!)
		 */
		//	++iFrom; /* nbr of chars to skip! */
			while(*pRes && iFrom) {
				--iFrom;
				++pRes;
			}
		}
		/* OK, we are at the begin - now let's copy... */
		while(*pRes && iLen) {
			*pBuf++ = *pRes;
			++pRes;
			--iLen;
		}
		*pBuf = '\0';
		if(*pbMustBeFreed == 1)
			free(pRes);
		pRes = pBufStart;
		*pbMustBeFreed = 1;
	}

	/* case conversations (should go after substring, because so we are able to
	 * work on the smallest possible buffer).
	 */
	if(pTpe->data.field.eCaseConv != tplCaseConvNo) {
		/* we need to obtain a private copy */
		int iLen = strlen(pRes);
		char *pBufStart;
		char *pBuf;
		pBufStart = pBuf = malloc((iLen + 1) * sizeof(char));
		if(pBuf == NULL) {
			if(*pbMustBeFreed == 1)
				free(pRes);
			*pbMustBeFreed = 0;
			return "**OUT OF MEMORY**";
		}
		while(*pRes) {
			*pBuf++ = (pTpe->data.field.eCaseConv == tplCaseConvUpper) ?
			          toupper(*pRes) : tolower(*pRes);
				  /* currently only these two exist */
			++pRes;
		}
		*pBuf = '\0';
		if(*pbMustBeFreed == 1)
			free(pRes);
		pRes = pBufStart;
		*pbMustBeFreed = 1;
	}

	/* Now drop last LF if present (pls note that this must not be done
	 * if bEscapeCC was set! - once that is implemented ;)).
	 */
	if(pTpe->data.field.options.bDropLastLF) {
		int iLen = strlen(pRes);
		char *pBuf;
		if(*(pRes + iLen - 1) == '\n') {
			/* we have a LF! */
			/* check if we need to obtain a private copy */
			if(pbMustBeFreed == 0) {
				/* ok, original copy, need a private one */
				pBuf = malloc((iLen + 1) * sizeof(char));
				if(pBuf == NULL) {
					if(*pbMustBeFreed == 1)
						free(pRes);
					*pbMustBeFreed = 0;
					return "**OUT OF MEMORY**";
				}
				memcpy(pBuf, pRes, iLen - 1);
				pRes = pBuf;
				*pbMustBeFreed = 1;
			}
			*(pRes + iLen - 1) = '\0'; /* drop LF ;) */
		}
	}

	/*dprintf("MsgGetProp(\"%s\"): \"%s\"\n", pName, pRes); only for verbose debug logging */
	return(pRes);
}

/* rgerhards 2004-11-09: end of helper routines. On to the 
 * "real" code ;)
 */


int main(argc, argv)
	int argc;
	char **argv;
{	register int i;
	register char *p;
#if !defined(__GLIBC__)
	int len, num_fds;
#else /* __GLIBC__ */
#ifndef TESTING
	size_t len;
#endif
	int num_fds;
#endif /* __GLIBC__ */
	/*
	 * It took me quite some time to figure out how this is
	 * supposed to work so I guess I should better write it down.
	 * unixm is a list of file descriptors from which one can
	 * read().  This is in contrary to readfds which is a list of
	 * file descriptors where activity is monitored by select()
	 * and from which one cannot read().  -Joey
	 *
	 * Changed: unixm is gone, since we now use datagram unix sockets.
	 * Hence we recv() from unix sockets directly (rather than
	 * first accept()ing connections on them), so there's no need
	 * for separate book-keeping.  --okir
	 */
	fd_set readfds;

#ifdef	MTRACE
	mtrace(); /* this is a debug aid for leak detection - either remove
	           * or put in conditional compilation. 2005-01-18 RGerhards */
#endif

#ifndef TESTING
	int	fd;
#ifdef  SYSLOG_INET
	struct sockaddr_in frominet;
	char *from;
#endif
	pid_t ppid = getpid();
#endif
	int ch;
	struct hostent *hent;

	char line[MAXLINE +1];
	extern int optind;
	extern char *optarg;
	int maxfds;
	char *pTmp;

#ifndef TESTING
	chdir ("/");
#endif
	for (i = 1; i < MAXFUNIX; i++) {
		funixn[i] = "";
		funix[i]  = -1;
	}

	while ((ch = getopt(argc, argv, "a:dhf:l:m:np:rs:v")) != EOF)
		switch((char)ch) {
		case 'a':
			if (nfunix < MAXFUNIX)
				funixn[nfunix++] = optarg;
			else
				fprintf(stderr, "Out of descriptors, ignoring %s\n", optarg);
			break;
		case 'd':		/* debug */
			Debug = 1;
			break;
		case 'f':		/* configuration file */
			ConfFile = optarg;
			break;
		case 'h':
			NoHops = 0;
			break;
		case 'l':
			if (LocalHosts) {
				fprintf (stderr, "Only one -l argument allowed," \
					"the first one is taken.\n");
				break;
			}
			LocalHosts = crunch_list(optarg);
			break;
		case 'm':		/* mark interval */
			MarkInterval = atoi(optarg) * 60;
			break;
		case 'n':		/* don't fork */
			NoFork = 1;
			break;
		case 'p':		/* path to regular log socket */
			funixn[0] = optarg;
			break;
		case 'r':		/* accept remote messages */
			AcceptRemote = 1;
			break;
		case 's':
			if (StripDomains) {
				fprintf (stderr, "Only one -s argument allowed," \
					"the first one is taken.\n");
				break;
			}
			StripDomains = crunch_list(optarg);
			break;
		case 'v':
			printf("syslogd %s.%s\n", VERSION, PATCHLEVEL);
			exit (0);
		case '?':
		default:
			usage();
		}
	if ((argc -= optind))
		usage();

#ifndef TESTING
	if ( !(Debug || NoFork) )
	{
		dprintf("Checking pidfile.\n");
		if (!check_pid(PidFile))
		{
			if (fork()) {
				/*
				 * Parent process
				 */
				signal (SIGTERM, doexit);
				sleep(300);
				/*
				 * Not reached unless something major went wrong.  5
				 * minutes should be a fair amount of time to wait.
				 * Please note that this procedure is important since
				 * the father must not exit before syslogd isn't
				 * initialized or the klogd won't be able to flush its
				 * logs.  -Joey
				 */
				exit(1);
			}
			num_fds = getdtablesize();
			for (i= 0; i < num_fds; i++)
				(void) close(i);
			untty();
		}
		else
		{
			fputs("rsyslogd: Already running.\n", stderr);
			exit(1);
		}
	}
	else
#endif
		debugging_on = 1;
#ifndef SYSV
	else
		setlinebuf(stdout);
#endif

#ifndef TESTING
	/* tuck my process id away */
	if ( !Debug )
	{
		dprintf("Writing pidfile.\n");
		if (!check_pid(PidFile))
		{
			if (!write_pid(PidFile))
			{
				dprintf("Can't write pid.\n");
				exit(1);
			}
		}
		else
		{
			dprintf("Pidfile (and pid) already exist.\n");
			exit(1);
		}
	} /* if ( !Debug ) */
#endif

	/* initialize the default templates
	 * we use template names with a SP in front - these 
	 * can NOT be generated via the configuration file
	 */
	pTmp = template_TraditionalFormat;
	tplAddLine(" TradFmt", &pTmp);
	pTmp = template_WallFmt;
	tplAddLine(" WallFmt", &pTmp);
	pTmp = template_StdFwdFmt;
	tplAddLine(" StdFwdFmt", &pTmp);
	pTmp = template_StdUsrMsgFmt;
	tplAddLine(" StdUsrMsgFmt", &pTmp);
	pTmp = template_StdDBFmt;
	tplAddLine(" StdDBFmt", &pTmp);

	/* prepare emergency logging system */

	consfile.f_type = F_CONSOLE;
	(void) strcpy(consfile.f_un.f_fname, ctty);
	cflineSetTemplateAndIOV(&consfile, " TradFmt");
	(void) gethostname(LocalHostName, sizeof(LocalHostName));
	if ( (p = strchr(LocalHostName, '.')) ) {
		*p++ = '\0';
		LocalDomain = p;
	}
	else
	{
		LocalDomain = "";

		/*
		 * It's not clearly defined whether gethostname()
		 * should return the simple hostname or the fqdn. A
		 * good piece of software should be aware of both and
		 * we want to distribute good software.  Joey
		 *
		 * Good software also always checks its return values...
		 * If syslogd starts up before DNS is up & /etc/hosts
		 * doesn't have LocalHostName listed, gethostbyname will
		 * return NULL. 
		 */
		hent = gethostbyname(LocalHostName);
		if ( hent )
			snprintf(LocalHostName, sizeof(LocalHostName), "%s", hent->h_name);
			
		if ( (p = strchr(LocalHostName, '.')) )
		{
			*p++ = '\0';
			LocalDomain = p;
		}
	}

	/*
	 * Convert to lower case to recognize the correct domain laterly
	 */
	for (p = (char *)LocalDomain; *p ; p++)
		if (isupper(*p))
			*p = tolower(*p);

	(void) signal(SIGTERM, die);
	(void) signal(SIGINT, Debug ? die : SIG_IGN);
	(void) signal(SIGQUIT, Debug ? die : SIG_IGN);
	(void) signal(SIGCHLD, reapchild);
	(void) signal(SIGALRM, domark);
	(void) signal(SIGUSR1, Debug ? debug_switch : SIG_IGN);
	(void) alarm(TIMERINTVL);

	/* Create a partial message table for all file descriptors. */
	num_fds = getdtablesize();
	dprintf("Allocated parts table for %d file descriptors.\n", num_fds);
	if ( (parts = (char **) malloc(num_fds * sizeof(char *))) == \
	    (char **) 0 )
	{
		logerror("Cannot allocate memory for message parts table.");
		die(0);
	}
	for(i= 0; i < num_fds; ++i)
	    parts[i] = (char *) 0;

	dprintf("Starting.\n");
	init();
#ifndef TESTING
	if ( Debug )
	{
		dprintf("Debugging disabled, SIGUSR1 to turn on debugging.\n");
		debugging_on = 0;
	}
	/*
	 * Send a signal to the parent to it can terminate.
	 */
	if (getpid() != ppid)
		kill (ppid, SIGTERM);
#endif

	/* Main loop begins here. */
	for (;;) {
		int nfds;
		errno = 0;
		FD_ZERO(&readfds);
		maxfds = 0;
#ifdef SYSLOG_UNIXAF
#ifndef TESTING
		/*
		 * Add the Unix Domain Sockets to the list of read
		 * descriptors.
		 */
		/* Copy master connections */
		for (i = 0; i < nfunix; i++) {
			if (funix[i] != -1) {
				FD_SET(funix[i], &readfds);
				if (funix[i]>maxfds) maxfds=funix[i];
			}
		}
#endif
#endif
#ifdef SYSLOG_INET
#ifndef TESTING
		/*
		 * Add the Internet Domain Socket to the list of read
		 * descriptors.
		 */
		if ( InetInuse && AcceptRemote ) {
			FD_SET(inetm, &readfds);
			if (inetm>maxfds) maxfds=inetm;
			dprintf("Listening on syslog UDP port.\n");
		}
#endif
#endif
#ifdef TESTING
		FD_SET(fileno(stdin), &readfds);
		if (fileno(stdin) > maxfds) maxfds = fileno(stdin);

		dprintf("Listening on stdin.  Press Ctrl-C to interrupt.\n");
#endif

		if ( debugging_on )
		{
			dprintf("----------------------------------------\nCalling select, active file descriptors (max %d): ", maxfds);
			for (nfds= 0; nfds <= maxfds; ++nfds)
				if ( FD_ISSET(nfds, &readfds) )
					dprintf("%d ", nfds);
			dprintf("\n");
		}
		nfds = select(maxfds+1, (fd_set *) &readfds, (fd_set *) NULL,
				  (fd_set *) NULL, (struct timeval *) NULL);
		if ( restart )
		{
			dprintf("\nReceived SIGHUP, reloading rsyslogd.\n");
			init();
			restart = 0;
			continue;
		}
		if (nfds == 0) {
			dprintf("No select activity.\n");
			continue;
		}
		if (nfds < 0) {
			if (errno != EINTR)
				logerror("select");
			dprintf("Select interrupted.\n");
			continue;
		}

		if ( debugging_on )
		{
			dprintf("\nSuccessful select, descriptor count = %d, " \
				"Activity on: ", nfds);
			for (nfds= 0; nfds <= maxfds; ++nfds)
				if ( FD_ISSET(nfds, &readfds) )
					dprintf("%d ", nfds);
			dprintf(("\n"));
		}

#ifndef TESTING
#ifdef SYSLOG_UNIXAF
		for (i = 0; i < nfunix; i++) {
		    if ((fd = funix[i]) != -1 && FD_ISSET(fd, &readfds)) {
			memset(line, '\0', sizeof(line));
			i = recv(fd, line, MAXLINE - 2, 0);
			dprintf("Message from UNIX socket: #%d\n", fd);
			if (i > 0) {
				line[i] = line[i+1] = '\0';
				printchopped(LocalHostName, line, i + 2,  fd, SOURCE_UNIXAF);
			} else if (i < 0 && errno != EINTR) {
				dprintf("UNIX socket error: %d = %s.\n", \
					errno, strerror(errno));
				logerror("recvfrom UNIX");
	      	}
				}
			}
#endif

#ifdef SYSLOG_INET
		if (InetInuse && AcceptRemote && FD_ISSET(inetm, &readfds)) {
			len = sizeof(frominet);
			memset(line, '\0', sizeof(line));
			i = recvfrom(finet, line, MAXLINE - 2, 0, \
				     (struct sockaddr *) &frominet, &len);
			dprintf("Message from inetd socket: #%d, host: %s\n",
				inetm, inet_ntoa(frominet.sin_addr));
			if (i > 0) {
				line[i] = line[i+1] = '\0';
				from = (char *)cvthname(&frominet);
				/*
				 * Here we could check if the host is permitted
				 * to send us syslog messages. We just have to
				 * catch the result of cvthname, look for a dot
				 * and if that doesn't exist, replace the first
				 * '\0' with '.' and we have the fqdn in lowercase
				 * letters so we could match them against whatever.
				 *  -Joey
				 */
				printchopped(from, line, i + 2,  finet, SOURCE_INET);
			} else if (i < 0 && errno != EINTR) {
				dprintf("INET socket error: %d = %s.\n", \
					errno, strerror(errno));
				logerror("recvfrom inet");
				/* should be harmless now that we set
				 * BSDCOMPAT on the socket */
				sleep(10);
			}
		}
#endif
#else
		if ( FD_ISSET(fileno(stdin), &readfds) ) {
			dprintf("Message from stdin.\n");
			memset(line, '\0', sizeof(line));
			line[0] = '.';
			parts[fileno(stdin)] = (char *) 0;
			i = read(fileno(stdin), line, MAXLINE);
			if (i > 0) {
				printchopped(LocalHostName, line, i+1,
					     fileno(stdin), SOURCE_STDIN);
		  	} else if (i < 0) {
		    		if (errno != EINTR) {
		      			logerror("stdin");
				}
		  	}
			FD_CLR(fileno(stdin), &readfds);
		  }

#endif
	}
}

int usage()
{
	fprintf(stderr, "usage: syslogd [-drvh] [-l hostlist] [-m markinterval] [-n] [-p path]\n" \
		" [-s domainlist] [-f conffile]\n");
	exit(1);
}

#ifdef SYSLOG_UNIXAF
static int create_unix_socket(const char *path)
{
	struct sockaddr_un sunx;
	int fd;
	char line[MAXLINE +1];

	if (path[0] == '\0')
		return -1;

	(void) unlink(path);

	memset(&sunx, 0, sizeof(sunx));
	sunx.sun_family = AF_UNIX;
	(void) strncpy(sunx.sun_path, path, sizeof(sunx.sun_path));
	fd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (fd < 0 || bind(fd, (struct sockaddr *) &sunx,
			   SUN_LEN(&sunx)) < 0 ||
	    chmod(path, 0666) < 0) {
		(void) snprintf(line, sizeof(line), "cannot create %s", path);
		logerror(line);
		dprintf("cannot create %s (%d).\n", path, errno);
		close(fd);
#ifndef SYSV
		die(0);
#endif
		return -1;
	}
	return fd;
}
#endif

#ifdef SYSLOG_INET
static int create_inet_socket()
{
	int fd, on = 1;
	struct sockaddr_in sin;

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		logerror("syslog: Unknown protocol, suspending inet service.");
		return fd;
	}

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = LogPort;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, \
		       (char *) &on, sizeof(on)) < 0 ) {
		logerror("setsockopt(REUSEADDR), suspending inet");
		close(fd);
		return -1;
	}
	/* We need to enable BSD compatibility. Otherwise an attacker
	 * could flood our log files by sending us tons of ICMP errors.
	 */
#ifndef BSD	
	if (setsockopt(fd, SOL_SOCKET, SO_BSDCOMPAT, \
			(char *) &on, sizeof(on)) < 0) {
		logerror("setsockopt(BSDCOMPAT), suspending inet");
		close(fd);
		return -1;
	}
#endif
	if (bind(fd, (struct sockaddr *) &sin, sizeof(sin)) < 0) {
		logerror("bind, suspending inet");
		close(fd);
		return -1;
	}
	return fd;
}
#endif

char **
crunch_list(list)
	char *list;
{
	int count, i;
	char *p, *q;
	char **result = NULL;

	p = list;
	
	/* strip off trailing delimiters */
	while (p[strlen(p)-1] == LIST_DELIMITER) {
		count--;
		p[strlen(p)-1] = '\0';
	}
	/* cut off leading delimiters */
	while (p[0] == LIST_DELIMITER) {
		count--;
		p++; 
	}
	
	/* count delimiters to calculate elements */
	for (count=i=0; p[i]; i++)
		if (p[i] == LIST_DELIMITER) count++;
	
	if ((result = (char **)malloc(sizeof(char *) * count+2)) == NULL) {
		printf ("Sorry, can't get enough memory, exiting.\n");
		exit(0);
	}
	
	/*
	 * We now can assume that the first and last
	 * characters are different from any delimiters,
	 * so we don't have to care about this.
	 */
	count = 0;
	while ((q=strchr(p, LIST_DELIMITER))) {
		result[count] = (char *) malloc((q - p + 1) * sizeof(char));
		if (result[count] == NULL) {
			printf ("Sorry, can't get enough memory, exiting.\n");
			exit(0);
		}
		strncpy(result[count], p, q - p);
		result[count][q - p] = '\0';
		p = q; p++;
		count++;
	}
	if ((result[count] = \
	     (char *)malloc(sizeof(char) * strlen(p) + 1)) == NULL) {
		printf ("Sorry, can't get enough memory, exiting.\n");
		exit(0);
	}
	strcpy(result[count],p);
	result[++count] = NULL;

#if 0
	count=0;
	while (result[count])
		dprintf ("#%d: %s\n", count, StripDomains[count++]);
#endif
	return result;
}


void untty()
#ifdef SYSV
{
	if ( !Debug ) {
		setsid();
	}
	return;
}

#else
{
	int i;

	if ( !Debug ) {
		i = open(_PATH_TTY, O_RDWR);
		if (i >= 0) {
			(void) ioctl(i, (int) TIOCNOTTY, (char *)0);
			(void) close(i);
		}
	}
}
#endif


/*
 * Parse the line to make sure that the msg is not a composite of more
 * than one message.
 * This function is called from ALL receivers, no matter how the message
 * was received. rgerhards
 * Partial messages are reconstruced via the parts[] table. This table is
 * dynamically allocated on startup based on getdtablesize(). This design has
 * its advantages, however it typically allocates way too many table
 * entries. If we've nothing better to do, we might want to look into this.
 * rgerhards 2004-11-08
 * I added the "iSource" parameter. This is needed to distinguish between
 * messages that have a hostname in them (received from the internet) and
 * those that do not have (most prominently /dev/log).  rgerhards 2004-11-16
 */

void printchopped(hname, msg, len, fd, iSource)
	char *hname;
	char *msg;
	int len;
	int fd;
	int iSource;
{
	auto int ptlngth;

	auto char *start = msg,
		  *p,
	          *end,
		  tmpline[MAXLINE + 1];

	dprintf("Message length: %d, File descriptor: %d.\n", len, fd);
	tmpline[0] = '\0';
	if ( parts[fd] != (char *) 0 )
	{
		dprintf("Including part from messages.\n");
		strcpy(tmpline, parts[fd]);
		free(parts[fd]);
		parts[fd] = (char *) 0;
		if ( (strlen(msg) + strlen(tmpline)) > MAXLINE )
		{
			logerror("Cannot glue message parts together");
			printline(hname, tmpline, iSource);
			start = msg;
		}
		else
		{
			dprintf("Previous: %s\n", tmpline);
			dprintf("Next: %s\n", msg);
			strcat(tmpline, msg);	/* length checked above */
			printline(hname, tmpline, iSource);
			if ( (strlen(msg) + 1) == len )
				return;
			else
				start = strchr(msg, '\0') + 1;
		}
	}

	if ( msg[len-1] != '\0' )
	{
		msg[len] = '\0';
		for(p= msg+len-1; *p != '\0' && p > msg; )
			--p;
		if(*p == '\0') p++;
		ptlngth = strlen(p);
		if ( (parts[fd] = malloc(ptlngth + 1)) == (char *) 0 )
			logerror("Cannot allocate memory for message part.");
		else
		{
			strcpy(parts[fd], p);
			dprintf("Saving partial msg: %s\n", parts[fd]);
			memset(p, '\0', ptlngth);
		}
	}

	do {
		end = strchr(start + 1, '\0');
		printline(hname, start, iSource);
		start = end + 1;
	} while ( *start != '\0' );

	return;
}

/* Take a raw input line, decode the message, and print the message
 * on the appropriate log files.
 * rgerhards 2004-11-08: Please note
 * that this function does only a partial decoding. At best, it splits 
 * the PRI part. No further decode happens. The rest is done in 
 * logmsg(). Please note that printsys() calls logmsg() directly, so
 * this is something we need to restructure once we are moving the
 * real decoder in here. I now (2004-11-09) found that printsys() seems
 * not to be called from anywhere. So we might as well decode the full
 * message here.
 * Added the iSource parameter so that we know if we have to parse
 * HOSTNAME or not. rgerhards 2004-11-16.
 */
void printline(hname, msg, iSource)
	char *hname;
	char *msg;
	int iSource;
{
	register char *p;
	int pri;
	struct msg *pMsg;

	/* Now it is time to create the message object (rgerhards)
	*/
	if((pMsg = MsgConstruct()) == NULL){
		/* rgerhards 2004-11-09: calling panic might not be the
		 * brightest idea - however, it is the best I currently have
		 * (TODO: think a bit more about this).
		 */
		syslogdPanic("Could not construct Msg object.");
		return;
	}
	if(MsgSetRawMsg(pMsg, msg) != 0) return;
	
	pMsg->iMsgSource = iSource;
	/* test for special codes */
	pri = DEFUPRI;
	p = msg;
	if (*p == '<') {
		pri = 0;
		while (isdigit(*++p))
		{
		   pri = 10 * pri + (*p - '0');
		}
		if (*p == '>')
			++p;
	}
	if (pri &~ (LOG_FACMASK|LOG_PRIMASK))
		pri = DEFUPRI;
	pMsg->iFacility = LOG_FAC(pri);
	pMsg->iSeverity = LOG_PRI(pri);

	/* got the buffer, now copy over the message. We use the "old" code
	 * here, it doesn't make sense to optimize as that code will soon
	 * be replaced.
	 */
#if 0	 /* TODO: REMOVE THIS LATER */
	/* we soon need to support UTF-8, so we will soon need to remove
	 * this. As a side-note, the current code destroys MBCS messages
	 * (like Japanese).
	 */
	q = pMsg->pszMSG;
	pEnd = pMsg->pszMSG + pMsg->iLenMSG;	 /* was -4 */
	while ((c = *p++) && q < pEnd) {
		if (c == '\n')
			*q++ = ' ';
/* not yet!		else if (c < 040) {
			*q++ = '^';
			*q++ = c ^ 0100;
		} else if (c == 0177 || (c & 0177) < 040) {
			*q++ = '\\';
			*q++ = '0' + ((c & 0300) >> 6);
			*q++ = '0' + ((c & 0070) >> 3);
			*q++ = '0' + (c & 0007);
		}*/ else
			*q++ = c;
	}
	*q = '\0';
#endif

	/* Now we look at the HOSTNAME. That is a bit complicated...
	 * If we have a locally received message, it does NOT
	 * contain any hostname information in the message itself.
	 * As such, the HOSTNAME is the same as the system that
	 * the message was received from (that, for obvious reasons,
	 * being the local host).  rgerhards 2004-11-16
	 */
	if(iSource != SOURCE_INET)
		if(MsgSetHOSTNAME(pMsg, hname) != 0) return;
	if(MsgSetRcvFrom(pMsg, hname) != 0) return;

	/* rgerhards 2004-11-19: well, well... we've now seen that we
	 * have the "hostname problem" also with the traditional Unix
	 * message. As we like to emulate it, we need to add the hostname
	 * to it.
	 */
	if(MsgSetUxTradMsg(pMsg, p) != 0) return;

	logmsg(pri, pMsg, SYNC_FILE);

	/* rgerhards 2004-11-11:
	 * we are done with the message object. If it still is
	 * stored somewhere, we can call discard anyhow. This
	 * is handled via the reference count - see description
	 * for struct msg for details.
	 */
	MsgDestruct(pMsg);
	return;
}

/* Decode a priority into textual information like auth.emerg.
 * rgerhards: This needs to be changed for syslog-protocol - severities
 * are then supported up to 2^32-1.
 */
char *textpri(pri)
	int pri;
{
	static char res[20];
	CODE *c_pri, *c_fac;

	for (c_fac = facilitynames; c_fac->c_name && !(c_fac->c_val == LOG_FAC(pri)<<3); c_fac++);
	for (c_pri = prioritynames; c_pri->c_name && !(c_pri->c_val == LOG_PRI(pri)); c_pri++);

	snprintf (res, sizeof(res), "%s.%s<%d>", c_fac->c_name, c_pri->c_name, pri);

	return res;
}

time_t	now;

/* rgerhards 2004-11-09: the following is a function that can be used
 * to log a message orginating from the syslogd itself. In sysklogd code,
 * this is done by simply calling logmsg(). However, logmsg() is changed in
 * rsyslog so that it takes a msg "object". So it can no longer be called
 * directly. This method here solves the need. It provides an interface that
 * allows to construct a locally-generated message. Please note that this
 * function here probably is only an interim solution and that we need to
 * think on the best way to do this.
 */
void logmsgInternal(pri, msg, from, flags)
	int pri;
	char *msg;
	char *from;
	int flags;
{
	struct msg *pMsg;

	if((pMsg = MsgConstruct()) == NULL){
		/* rgerhards 2004-11-09: calling panic might not be the
		 * brightest idea - however, it is the best I currently have
		 * (TODO: think a bit more about this).
		 */
		syslogdPanic("Could not construct Msg object.");
		return;
	}

	if(MsgSetUxTradMsg(pMsg, msg) != 0) return;
	if(MsgSetRawMsg(pMsg, msg) != 0) return;
	if(MsgSetHOSTNAME(pMsg, LocalHostName) != 0) return;
	pMsg->iFacility = LOG_FAC(pri);
	pMsg->iSeverity = LOG_PRI(pri);
	pMsg->iMsgSource = SOURCE_INTERNAL;
	getCurrTime(&(pMsg->tTIMESTAMP)); /* use the current time! */

	logmsg(pri, pMsg, flags);
	MsgDestruct(pMsg);
}

/*
 * Log a message to the appropriate log files, users, etc. based on
 * the priority.
 * rgerhards 2004-11-08: actually, this also decodes all but the PRI part.
 * rgerhards 2004-11-09: ... but only, if syslogd could properly be initialized
 *			 if not, we use emergency logging to the console and in
 *                       this case, no further decoding happens.
 * changed to no longer receive a plain message but a msg object instead.
 * rgerhards-2994-11-16: OK, we are now up to another change... This method
 * actually needs to PARSE the message. How exactly this needs to happen depends on
 * a number of things. Most importantly, it depends on the source. For example,
 * locally received messages (SOURCE_UNIXAF) do NOT have a hostname in them. So
 * we need to treat them differntly form network-received messages which have.
 * Well, actually not all network-received message really have a hostname. We
 * can just hope they do, but we can not be sure. So this method tries to find
 * whatever can be found in the message and uses that... Obviously, there is some
 * potential for misinterpretation, which we simply can not solve under the
 * circumstances given.
 */

void logmsg(pri, pMsg, flags)
	int pri;
	struct msg *pMsg;
	int flags;
{
	register struct filed *f;

	/* allocate next entry and add it */
	int fac, prilev;
	int msglen;
	char *msg;
	char *from;
	char *p2parse;
	char *pBuf;
	char *pWork;
	sbStrBObj *pStrB;
	int iCnt;
	int bContParse = 1;

	assert(pMsg != NULL);
	assert(pMsg->pszUxTradMsg != NULL);
	msg = pMsg->pszUxTradMsg;
	from = pMsg->pszHOSTNAME;
	dprintf("logmsg: %s, flags %x, from %s, msg %s\n", textpri(pri), flags, from, msg);

#ifndef SYSV
	omask = sigblock(sigmask(SIGHUP)|sigmask(SIGALRM));
#endif

	/* extract facility and priority level */
	if (flags & MARK)
		fac = LOG_NFACILITIES;
	else
		fac = LOG_FAC(pri);
	prilev = LOG_PRI(pri);

	p2parse = msg;	/* our "message" begins here */
	/*
	 * Check to see if msg contains a timestamp
	 */
	msglen = pMsg->iLenMSG;
	if(srSLMGParseTIMESTAMP3164(&(pMsg->tTIMESTAMP), msg) == TRUE)
		p2parse += 16;
	else {
		bContParse = 0;
		flags |= ADDDATE;
	}

	/* here we need to check if the timestamp is valid. If it is not,
	 * we can not continue to parse but must treat the rest as the 
	 * MSG part of the message (as of RFC 3164).
	 * rgerhards 2004-12-03
	 */
	(void) time(&now);
	if (flags & ADDDATE) {
		getCurrTime(&(pMsg->tTIMESTAMP)); /* use the current time! */
		/* but modify it to be an RFC 3164 time... */
		pMsg->tTIMESTAMP.timeType = 1;
		pMsg->tTIMESTAMP.secfracPrecision = 0;
		pMsg->tTIMESTAMP.secfrac = 0;
	}

	/* parse HOSTNAME - but only if this is network-received! */
	if(pMsg->iMsgSource == SOURCE_INET) {
		if(bContParse) {
			/* TODO: quick and dirty memory allocation */
			if((pBuf = malloc(sizeof(char)* strlen(p2parse) +1)) == NULL)
				return;
			pWork = pBuf;
			while(*p2parse && *p2parse != ' ')
				*pWork++ = *p2parse++;
			if(*p2parse == ' ')
				++p2parse;
			*pWork = '\0';
			MsgAssignHOSTNAME(pMsg, pBuf);
		} else {
			/* we can not parse, so we get the system we
			 * received the data from.
			 */
			MsgSetHOSTNAME(pMsg, getRcvFrom(pMsg));
		}
	}

	/* now parse TAG - that should be present in message from
	 * all sources.
	 * This code is somewhat not compliant with RFC 3164. As of 3164,
	 * the TAG field is ended by any non-alphanumeric character. In
	 * practice, however, the TAG often contains dashes and other things,
	 * which would end the TAG. So it is not desirable. As such, we only
	 * accept colon and SP to be terminators. Even there is a slight difference:
	 * a colon is PART of the TAG, while a SP is NOT part of the tag
	 * (it is CONTENT). Finally, we allow only up to 32 characters for
	 * TAG, as it is specified in RFC 3164.
	 */
	/* The following code in general is quick & dirty - I need to get
	 * it going for a test, TODO: redo later. rgerhards 2004-11-16 */
	/* TODO: quick and dirty memory allocation */
	/* lol.. we tried to solve it, just to remind ourselfs that 32 octets
	 * is the max size ;) we need to shuffle the code again... Just for 
	 * the records: the code is currently clean, but we could optimize it! */
	if(bContParse) {
		if((pStrB = sbStrBConstruct()) == NULL) 
			return;
		sbStrBSetAllocIncrement(pStrB, 33);
		pWork = pBuf;
		iCnt = 0;
		while(*p2parse && *p2parse != ':' && *p2parse != ' ' && iCnt < 32) {
			sbStrBAppendChar(pStrB, *p2parse++);
			++iCnt;
		}
		if(*p2parse == ':') {
			++p2parse; 
			sbStrBAppendChar(pStrB, ':');
		}
		MsgAssignTAG(pMsg, sbStrBFinish(pStrB));
	} else {
		/* we have no TAG, so we ... */
		/*DO NOTHING*/;
	}

	/* The rest is the actual MSG */
	if(MsgSetMSG(pMsg, p2parse) != 0) return;

	/* ---------------------- END PARSING ---------------- */

	/* log the message to the particular outputs */
	if (!Initialized) {
		/* If we reach this point, the daemon initialization FAILED. That is,
		 * syslogd is NOT actually running. So what we do here is just
		 * initialize a pointer to the system console and then output
		 * the message to the it. So at least we have a little
		 * chance that messages show up somewhere.
		 * rgerhards 2004-11-09
		 */
		f = &consfile;
		f->f_file = open(ctty, O_WRONLY|O_NOCTTY);

		if (f->f_file >= 0) {
			untty();
			f->f_pMsg = MsgAddRef(pMsg); /* is expected here... */
			fprintlog(f, flags);
			MsgDestruct(pMsg);
			(void) close(f->f_file);
			f->f_file = -1;
		}

		/* now log to a second emergency log... 2005-06-21 rgerhards */
		/* TODO: make this configurable, eventually via the command line */
		if(ttyname(0) != NULL) {
			memset(&emergfile, 0, sizeof(emergfile));
			f = &emergfile;
			emergfile.f_type = F_TTY;
			(void) strcpy(emergfile.f_un.f_fname, ttyname(0));
			cflineSetTemplateAndIOV(&emergfile, " TradFmt");
			f->f_file = open(ttyname(0), O_WRONLY|O_NOCTTY);

			if (f->f_file >= 0) {
				untty();
				f->f_pMsg = MsgAddRef(pMsg); /* is expected here... */
				fprintlog(f, flags);
				MsgDestruct(pMsg);
				(void) close(f->f_file);
				f->f_file = -1;
			}
		}
#ifndef SYSV
		(void) sigsetmask(omask);
#endif
		return; /* we are done with emergency loging */
	}

	for (f = Files; f; f = f->f_next) {

		/* skip messages that are incorrect priority */
		if ( (f->f_pmask[fac] == TABLE_NOPRI) || \
		    ((f->f_pmask[fac] & (1<<prilev)) == 0) )
		  	continue;

		if (f->f_type == F_CONSOLE && (flags & IGN_CONS))
			continue;

		/* don't output marks to recently written files */
		if ((flags & MARK) && (now - f->f_time) < MarkInterval / 2)
			continue;

		/*
		 * suppress duplicate lines to this file
		 */
		if ((flags & MARK) == 0 && msglen == getMSGLen(f->f_pMsg) &&
		    !strcmp(msg, getMSG(f->f_pMsg)) &&
		    !strcmp(from, getHOSTNAME(f->f_pMsg))) {
			f->f_prevcount++;
			dprintf("msg repeated %d times, %ld sec of %d.\n",
			    f->f_prevcount, now - f->f_time,
			    repeatinterval[f->f_repeatcount]);
			/*
			 * If domark would have logged this by now,
			 * flush it now (so we don't hold isolated messages),
			 * but back off so we'll flush less often
			 * in the future.
			 */
			if (now > REPEATTIME(f)) {
				fprintlog(f, flags);
				BACKOFF(f);
			}
		} else {
			/* new line, save it */
			/* first check if we have a previous message stored
			 * if so, discard that first
			 */
			if(f->f_pMsg != NULL)
				MsgDestruct(f->f_pMsg);
			f->f_pMsg = MsgAddRef(pMsg);
			/* call the output driver */
			fprintlog(f, flags);
		}
	}
#ifndef SYSV
	(void) sigsetmask(omask);
#endif
}


/* Helper to doSQLEscape. This is called if doSQLEscape
 * runs out of memory allocating the escaped string.
 * Then we are in trouble. We can
 * NOT simply return the unmodified string because this
 * may cause SQL injection. But we also can not simply
 * abort the run, this would be a DoS. I think an appropriate
 * measure is to remove the dangerous \' characters. We
 * replace them by \", which will break the message and
 * signatures eventually present - but this is the
 * best thing we can do now (or does anybody 
 * have a better idea?). rgerhards 2004-11-23
 */
void doSQLEmergencyEscape(register char *p)
{
	while(*p) {
		if(*p == '\'')
			*p = '"';
		++p;
	}
}


/* SQL-Escape a string. Single quotes are found and
 * replaced by two of them. A new buffer is allocated
 * for the provided string and the provided buffer is
 * freed. The length is updated. Parameter pbMustBeFreed
 * is set to 1 if a new buffer is allocated. Otherwise,
 * it is left untouched.
 */
void doSQLEscape(char **pp, size_t *pLen, unsigned short *pbMustBeFreed)
{
	char *p;
	int iLen;
	sbStrBObj *pStrB;
	char *pszGenerated;

	assert(pp != NULL);
	assert(*pp != NULL);
	assert(pLen != NULL);
	assert(pbMustBeFreed != NULL);

	/* first check if we need to do anything at all... */
	for(p = *pp ; *p && *p != '\'' ; ++p)
		;
	/* when we get out of the loop, we are either at the
	 * string terminator or the first \'. */
	if(*p == '\0')
		return; /* nothing to do in this case! */

	p = *pp;
	iLen = *pLen;
	if((pStrB = sbStrBConstruct()) == NULL) {
		/* oops - no mem ... Do emergency... */
		doSQLEmergencyEscape(p);
		return;
	}
	
	while(*p) {
		if(*p == '\'') {
			if(sbStrBAppendChar(pStrB, '\'') != SR_RET_OK) {
				doSQLEmergencyEscape(*pp);
				if((pszGenerated = sbStrBFinish(pStrB))
					!= NULL)
					free(pszGenerated);
					return;
			iLen++;	/* reflect the extra character */
			}
		}
		if(sbStrBAppendChar(pStrB, *p) != SR_RET_OK) {
			doSQLEmergencyEscape(*pp);
			if((pszGenerated = sbStrBFinish(pStrB))
				!= NULL)
				free(pszGenerated);
				return;
		}
		++p;
	}
	if((pszGenerated = sbStrBFinish(pStrB)) == NULL) {
		doSQLEmergencyEscape(*pp);
		return;
	}

	if(*pbMustBeFreed)
		free(*pp); /* discard previous value */

	*pp = pszGenerated;
	*pLen = iLen;
	*pbMustBeFreed = 1;
}


/* create a string from the provided iovec. This can
 * be called by all functions who need the template
 * text in a single string. The function takes an
 * entry of the filed structure. It uses all data
 * from there. It returns a pointer to the generated
 * string if it succeeded, or NULL otherwise.
 * rgerhards 2004-11-22 
 */
char *iovAsString(struct filed *f)
{
	struct iovec *v;
	int i;
	sbStrBObj *pStrB;

	assert(f != NULL);

	if(f->f_psziov != NULL) {
		/* for now, we always free a previous buffer.
		 * The idea, however, is to keep a copy of the
		 * buffer until we know we no longer can re-use it.
		 */
		free(f->f_psziov);
	}

	if((pStrB = sbStrBConstruct()) == NULL) {
		/* oops - no mem, let's try to set the message we have
		 * most probably, this will fail, too. But at least we
		 * can try... */
		return NULL;
	}

	i = 0;
	f->f_iLenpsziov = 0;
	v = f->f_iov;
	while(i++ < f->f_iIovUsed) {
		if(v->iov_len > 0) {
			sbStrBAppendStr(pStrB, v->iov_base);
			f->f_iLenpsziov += v->iov_len;
		}
		++v;
	}

	f->f_psziov = sbStrBFinish(pStrB);
	return f->f_psziov;
}


/* rgerhards 2004-11-24: free the to-be-freed string in
 * iovec. Some strings point to read-only constants in the
 * msg object, these must not be taken care of. But some
 * are specifically created for this instance and those
 * must be freed before the next is created. This is done
 * here. After this method has been called, the iovec
 * string array is invalid and must either be totally
 * discarded or e-initialized!
 */
void iovDeleteFreeableStrings(struct filed *f)
{
	register int i;

	assert(f != NULL);

	for(i = 0 ; i < f->f_iIovUsed ; ++i) {
		/* free to-be-freed strings in iovec */
		if(*(f->f_bMustBeFreed + i)) {
			free((f->f_iov + i)->iov_base);
			*(f->f_bMustBeFreed) = 0;
		}
	}
}


/* rgerhards 2004-11-19: create the iovec for
 * a given message. This is called by all methods
 * using iovec's for their output. Returns the number
 * of iovecs used (might be different from max if the
 * template contains an invalid entry).
 */
void  iovCreate(struct filed *f)
{
	register struct iovec *v;
	int iIOVused;
	struct template *pTpl;
	struct templateEntry *pTpe;
	struct msg *pMsg;

	assert(f != NULL);

	/* discard previous memory buffers */
	iovDeleteFreeableStrings(f);
	
	pMsg = f->f_pMsg;
	pTpl = f->f_pTpl;
	v = f->f_iov;
	
	iIOVused = 0;
	pTpe = pTpl->pEntryRoot;
	while(pTpe != NULL) {
		if(pTpe->eEntryType == CONSTANT) {
			v->iov_base = pTpe->data.constant.pConstant;
			v->iov_len = pTpe->data.constant.iLenConstant;
			++v;
			++iIOVused;
		} else 	if(pTpe->eEntryType == FIELD) {
			v->iov_base = MsgGetProp(pMsg, pTpe, f->f_bMustBeFreed + iIOVused);
			v->iov_len = strlen(v->iov_base);
			/* TODO: performance optimize - can we obtain the length? */
			/* we now need to check if we should use SQL option. In this case,
			 * we must go over the generated string and escape '\'' characters.
			 */
			if(f->f_pTpl->optFormatForSQL)
				doSQLEscape((char**)&v->iov_base, &v->iov_len,
					    f->f_bMustBeFreed + iIOVused);
			++v;
			++iIOVused;
		}
		pTpe = pTpe->pNext;
	}
	
	f->f_iIovUsed = iIOVused;

#if 0 /* debug aid */
{
	int i;
	v = f->f_iov;
	for(i = 0 ; i < iIOVused ; ++i, ++v) {
		printf("iovCreate(%d), string '%s', mustbeFreed %d\n", i,
		        v->iov_base, *(f->f_bMustBeFreed + i));
	}
}
#endif /* debug aid */
#if 0
	/* almost done - just let's check how we need to terminate
	 * the message.
	 */
	dprintf(" %s\n", f->f_un.f_fname);
	if (f->f_type == F_TTY || f->f_type == F_CONSOLE) {
		v->iov_base = "\r\n";
		v->iov_len = 2;
	} else {
		v->iov_base = "\n";
		v->iov_len = 1;
	}
	
#endif
	return;
}

/* rgerhards 2005-06-21: Try to resolve a size limit
 * situation. This first runs the command, and then
 * checks if we are still above the treshold.
 * returns 0 if ok, 1 otherwise
 * TODO: consider moving the initial check in here, too
 */
int resolveFileSizeLimit(struct filed *f)
{
	off_t actualFileSize;
	assert(f != NULL);

	if(f->f_sizeLimitCmd == NULL)
		return 1; /* nothing we can do in this case... */
	
	/* TODO: this is a really quick hack. We need something more
	 * solid when it goes into production. This was just to see if
	 * the overall idea works (I hope it won't survive...).
	 * rgerhards 2005-06-21
	 */
	system(f->f_sizeLimitCmd);

	f->f_file = open(f->f_un.f_fname, O_WRONLY|O_APPEND|O_CREAT|O_NOCTTY,
			 0644);

	actualFileSize = lseek(f->f_file, 0, SEEK_END);
	if(actualFileSize >= f->f_sizeLimit) {
		/* OK, it didn't work out... */
		return 1;
		}

	return 0;
}

/* rgerhards 2004-11-11: write to a file output. This
 * will be called for all outputs using file semantics,
 * for example also for pipes.
 */
void writeFile(struct filed *f)
{
	off_t actualFileSize;
	assert(f != NULL);

	/* Now generate the message. This can eventually be moved to
	 * a generic subroutine (need to think about this....).
	 * for now, this is a quick and dirty dummy. We need to have the
	 * ability to specify the message format before we can actually 
	 * code this part of the function. rgerhards 2004-11-11
	 */
	iovCreate(f);
again:
	/* first check if we have a file size limit and, if so,
	 * obey to it.
	 */
	if(f->f_sizeLimit != 0) {
		actualFileSize = lseek(f->f_file, 0, SEEK_END);
		if(actualFileSize >= f->f_sizeLimit) {
			char errMsg[256];
			/* for now, we simply disable a file once it is
			 * beyond the maximum size. This is better than having
			 * us aborted by the OS... rgerhards 2005-06-21
			 */
			(void) close(f->f_file);
			/* try to resolve the situation */
			if(resolveFileSizeLimit(f) != 0) {
				/* didn't work out, so disable... */
				f->f_type = F_UNUSED;
				snprintf(errMsg, sizeof(errMsg),
					 "no longer writing to file %s; grown beyond configured file size of %llu bytes, actual size %llu - configured command did not resolve situation\n",
					 f->f_un.f_fname, f->f_sizeLimit, actualFileSize);
				errno = 0;
				logerror(errMsg);
				return;
			} else {
				snprintf(errMsg, sizeof(errMsg),
					 "file %s had grown beyond configured file size of %llu bytes, actual size was %llu - configured command resolved situation\n",
					 f->f_un.f_fname, f->f_sizeLimit, actualFileSize);
				errno = 0;
				logerror(errMsg);
			}
		}
	}

	if (writev(f->f_file, f->f_iov, f->f_iIovUsed) < 0) {
		int e = errno;

		/* If a named pipe is full, just ignore it for now
		   - mrn 24 May 96 */
		if (f->f_type == F_PIPE && e == EAGAIN)
			return;

		(void) close(f->f_file);
		/*
		 * Check for EBADF on TTY's due to vhangup()
		 * Linux uses EIO instead (mrn 12 May 96)
		 */
		if ((f->f_type == F_TTY || f->f_type == F_CONSOLE)
#ifdef linux
			&& e == EIO) {
#else
			&& e == EBADF) {
#endif
			f->f_file = open(f->f_un.f_fname, O_WRONLY|O_APPEND|O_NOCTTY);
			if (f->f_file < 0) {
				f->f_type = F_UNUSED;
				logerror(f->f_un.f_fname);
			} else {
				untty();
				goto again;
			}
		} else {
			f->f_type = F_UNUSED;
			errno = e;
			logerror(f->f_un.f_fname);
		}
	} else if (f->f_flags & SYNC_FILE)
		(void) fsync(f->f_file);
}

/* rgerhards 2004-11-09: fprintlog() is the actual driver for
 * the output channel. It receives the channel description (f) as
 * well as the message and outputs them according to the channel
 * semantics. The message is typically already contained in the
 * channel save buffer (f->f_prevline). This is not only the case
 * when a message was already repeated but also when a new message
 * arrived. Parameter "msg", which sounds like the message content,
 * actually contains the message only in those few cases where it
 * was too large to fit into the channel save buffer.
 *
 * This whole function is probably about to change once we have the
 * message abstraction.
 */
void fprintlog(f, flags)
	register struct filed *f;
	int flags;
{
	char *msg;
#ifdef SYSLOG_INET
	register int l;
	time_t fwd_suspend;
	struct hostent *hp;
#endif

	msg = f->f_pMsg->pszMSG;
	dprintf("Called fprintlog, ");

#if 0
	/* finally commented code out, because it is not working at all ;)
	 * TODO: handle the case of message repeation. Currently, there is still
	 * some code to do it, but that code is defunct due to our changes!
	 */
	if (msg) {
		/*v->iov_base = msg;
		v->iov_len = strlen(msg);
		*/v->iov_base = f->f_pMsg->pszRawMsg;
		v->iov_len = f->f_pMsg->iLenRawMsg;
	} else if (f->f_prevcount > 1) {
		(void) snprintf(repbuf, sizeof(repbuf), "last message repeated %d times",
		    f->f_prevcount);
		v->iov_base = repbuf;
		v->iov_len = strlen(repbuf);
	} else {
		v->iov_base = f->f_pMsg->pszMSG;
		v->iov_len = f->f_pMsg->iLenMSG;
	}
#endif

	dprintf("logging to %s", TypeNames[f->f_type]);

	switch (f->f_type) {
	case F_UNUSED:
		f->f_time = now;
		dprintf("\n");
		break;

#ifdef SYSLOG_INET
	case F_FORW_SUSP:
		fwd_suspend = time((time_t *) 0) - f->f_time;
		if ( fwd_suspend >= INET_SUSPEND_TIME ) {
			dprintf("\nForwarding suspension over, " \
				"retrying FORW ");
			f->f_type = F_FORW;
			goto f_forw;
		}
		else {
			dprintf(" %s\n", f->f_un.f_forw.f_hname);
			dprintf("Forwarding suspension not over, time " \
				"left: %d.\n", INET_SUSPEND_TIME - \
				fwd_suspend);
		}
		break;
		
	/*
	 * The trick is to wait some time, then retry to get the
	 * address. If that fails retry x times and then give up.
	 *
	 * You'll run into this problem mostly if the name server you
	 * need for resolving the address is on the same machine, but
	 * is started after syslogd. 
	 */
	case F_FORW_UNKN:
		dprintf(" %s\n", f->f_un.f_forw.f_hname);
		fwd_suspend = time((time_t *) 0) - f->f_time;
		if ( fwd_suspend >= INET_SUSPEND_TIME ) {
			dprintf("Forwarding suspension to unknown over, retrying\n");
			if ( (hp = gethostbyname(f->f_un.f_forw.f_hname)) == NULL ) {
				dprintf("Failure: %s\n", sys_h_errlist[h_errno]);
				dprintf("Retries: %d\n", f->f_prevcount);
				if ( --f->f_prevcount < 0 ) {
					dprintf("Giving up.\n");
					f->f_type = F_UNUSED;
				}
				else
					dprintf("Left retries: %d\n", f->f_prevcount);
			}
			else {
			        dprintf("%s found, resuming.\n", f->f_un.f_forw.f_hname);
				memcpy((char *) &f->f_un.f_forw.f_addr.sin_addr, hp->h_addr, hp->h_length);
				f->f_prevcount = 0;
				f->f_type = F_FORW;
				goto f_forw;
			}
		}
		else
			dprintf("Forwarding suspension not over, time " \
				"left: %d\n", INET_SUSPEND_TIME - fwd_suspend);
		break;

	case F_FORW:
		/* 
		 * Don't send any message to a remote host if it
		 * already comes from one. (we don't care 'bout who
		 * sent the message, we don't send it anyway)  -Joey
		 */
	f_forw:
		dprintf(" %s\n", f->f_un.f_forw.f_hname);
		iovCreate(f);
		if ( strcmp(f->f_pMsg->pszHOSTNAME, LocalHostName) && NoHops )
			dprintf("Not sending message to remote.\n");
		else {
			char *psz;
			f->f_time = now;
			psz = iovAsString(f);
			l = f->f_iLenpsziov;
			if (l > MAXLINE)
				l = MAXLINE;
			if (sendto(finet, psz, l, 0, \
				   (struct sockaddr *) &f->f_un.f_forw.f_addr,
				   sizeof(f->f_un.f_forw.f_addr)) != l) {
				int e = errno;
				dprintf("INET sendto error: %d = %s.\n", 
					e, strerror(e));
				f->f_type = F_FORW_SUSP;
				errno = e;
				logerror("sendto");
			}
		}
		break;
#endif

	case F_CONSOLE:
		f->f_time = now;
#ifdef UNIXPC
		if (1) {
#else
		if (flags & IGN_CONS) {	
#endif
			dprintf(" (ignored).\n");
			break;
		}
		/* FALLTHROUGH */

	case F_TTY:
	case F_FILE:
	case F_PIPE:
		dprintf("\n");
		/* TODO: check if we need f->f_time = now;*/
		/* f->f_file == -1 is an indicator that the we couldn't
		   open the file at startup. */
		if (f->f_file != -1)
			writeFile(f);
		break;

	case F_USERS:
	case F_WALL:
		f->f_time = now;
		dprintf("\n");
		wallmsg(f);
		break;

#ifdef	WITH_DB
	case F_MYSQL:
		f->f_time = now;
		dprintf("\n");
		writeMySQL(f);
		break;
#endif
	} /* switch */
	if (f->f_type != F_FORW_UNKN)
		f->f_prevcount = 0;
	return;		
}

jmp_buf ttybuf;

void endtty()
{
	longjmp(ttybuf, 1);
}

/**
 * BSD setutent/getutent() replacement routines
 * The following routines emulate setutent() and getutent() under
 * BSD because they are not available there. We only emulate what we actually
 * need! rgerhards 2005-03-18
 */
#ifdef BSD
static FILE *BSD_uf = NULL;
void setutent(void)
{
	assert(BSD_uf == NULL);
	if ((BSD_uf = fopen(_PATH_UTMP, "r")) == NULL) {
		logerror(_PATH_UTMP);
		return;
	}
}

struct utmp* getutent(void)
{
	static struct utmp st_utmp;

	if(fread((char *)&st_utmp, sizeof(st_utmp), 1, BSD_uf) != 1)
		return NULL;

	return(&st_utmp);
}

void endutent(void)
{
	fclose(BSD_uf);
	BSD_uf = NULL;
}
#endif


/*
 *  WALLMSG -- Write a message to the world at large
 *
 *	Write the specified message to either the entire
 *	world, or a list of approved users.
 */

void wallmsg(f)
	register struct filed *f;
{
	char p[6 + UNAMESZ];
	register int i;
	int ttyf;
	static int reenter = 0;
	struct utmp ut;
	struct utmp *uptr;

	assert(f != NULL);

	if (reenter++)
		return;

	iovCreate(f);	/* init the iovec */

	/* open the user login file */
	setutent();

	/*
	 * Might as well fork instead of using nonblocking I/O
	 * and doing notty().
	 */
	if (fork() == 0) {
		(void) signal(SIGTERM, SIG_DFL);
		(void) alarm(0);
		(void) signal(SIGALRM, endtty);
#ifndef SYSV
		(void) signal(SIGTTOU, SIG_IGN);
		(void) sigsetmask(0);
#endif
	/* TODO: find a way to limit the max size of the message. hint: this
	 * should go into the template!
	 */

		/* scan the user login file */
		while ((uptr = getutent())) {
			memcpy(&ut, uptr, sizeof(ut));
			/* is this slot used? */
			if (ut.ut_name[0] == '\0')
				continue;
#ifndef BSD
			if (ut.ut_type == LOGIN_PROCESS)
			        continue;
#endif
			if (!(strncmp (ut.ut_name,"LOGIN", 6))) /* paranoia */
			        continue;

			/* should we send the message to this user? */
			if (f->f_type == F_USERS) {
				for (i = 0; i < MAXUNAMES; i++) {
					if (!f->f_un.f_uname[i][0]) {
						i = MAXUNAMES;
						break;
					}
					if (strncmp(f->f_un.f_uname[i],
					    ut.ut_name, UNAMESZ) == 0)
						break;
				}
				if (i >= MAXUNAMES)
					continue;
			}

			/* compute the device name */
			strcpy(p, _PATH_DEV);
			strncat(p, ut.ut_line, UNAMESZ);

			if (setjmp(ttybuf) == 0) {
				(void) alarm(15);
				/* open the terminal */
				ttyf = open(p, O_WRONLY|O_NOCTTY);
				if (ttyf >= 0) {
					struct stat statb;

					if (fstat(ttyf, &statb) == 0 &&
					    (statb.st_mode & S_IWRITE))
						(void) writev(ttyf, f->f_iov, f->f_iIovUsed);
					close(ttyf);
					ttyf = -1;
				}
			}
			(void) alarm(0);
		}
		exit(0);
	}
	/* close the user login file */
	endutent();
	reenter = 0;
}

void reapchild()
{
	int saved_errno = errno;
#if defined(SYSV) && !defined(linux)
	(void) signal(SIGCHLD, reapchild);	/* reset signal handler -ASP */
	wait ((int *)0);
#else
	union wait status;

	while (wait3(&status, WNOHANG, (struct rusage *) NULL) > 0)
		;
#endif
#ifdef linux
	(void) signal(SIGCHLD, reapchild);	/* reset signal handler -ASP */
#endif
	errno = saved_errno;
}

/*
 * Return a printable representation of a host address.
 */
const char *cvthname(f)
	struct sockaddr_in *f;
{
	struct hostent *hp;
	register char *p;
	int count;

	if (f->sin_family != AF_INET) {
		dprintf("Malformed from address.\n");
		return ("???");
	}
	hp = gethostbyaddr((char *) &f->sin_addr, sizeof(struct in_addr), \
			   f->sin_family);
	if (hp == NULL) {
		dprintf("Host name for your address (%s) unknown.\n",
			inet_ntoa(f->sin_addr));
		return (inet_ntoa(f->sin_addr));
	}
	/*
	 * Convert to lower case, just like LocalDomain above
	 */
	for (p = (char *)hp->h_name; *p ; p++)
		if (isupper(*p))
			*p = tolower(*p);

	/*
	 * Notice that the string still contains the fqdn, but your
	 * hostname and domain are separated by a '\0'.
	 */
	if ((p = strchr(hp->h_name, '.'))) {
		if (strcmp(p + 1, LocalDomain) == 0) {
			*p = '\0';
			return (hp->h_name);
		} else {
			if (StripDomains) {
				count=0;
				while (StripDomains[count]) {
					if (strcmp(p + 1, StripDomains[count]) == 0) {
						*p = '\0';
						return (hp->h_name);
					}
					count++;
				}
			}
			if (LocalHosts) {
				count=0;
				while (LocalHosts[count]) {
					if (!strcmp(hp->h_name, LocalHosts[count])) {
						*p = '\0';
						return (hp->h_name);
					}
					count++;
				}
			}
		}
	}

	return (hp->h_name);
}

void domark()
{
	register struct filed *f;
	if (MarkInterval > 0) {
	now = time(0);
	MarkSeq += TIMERINTVL;
	if (MarkSeq >= MarkInterval) {
		logmsgInternal(LOG_INFO, "-- MARK --", LocalHostName, ADDDATE|MARK);
		MarkSeq = 0;
	}

	for (f = Files; f; f = f->f_next) {
		if (f->f_prevcount && now >= REPEATTIME(f)) {
			dprintf("flush %s: repeated %d times, %d sec.\n",
			    TypeNames[f->f_type], f->f_prevcount,
			    repeatinterval[f->f_repeatcount]);
			/* TODO: re-implement fprintlog(f, LocalHostName, 0, (char *)NULL); */
			BACKOFF(f);
		}
	}
	}
	(void) signal(SIGALRM, domark);
	(void) alarm(TIMERINTVL);
}

void debug_switch()

{
	dprintf("Switching debugging_on to %s\n", (debugging_on == 0) ? "true" : "false");
	debugging_on = (debugging_on == 0) ? 1 : 0;
	signal(SIGUSR1, debug_switch);
}


/*
 * Print syslogd errors some place.
 */
void logerror(type)
	char *type;
{
	char buf[256];

	dprintf("Called logerr, msg: %s\n", type);

	if (errno == 0)
		(void) snprintf(buf, sizeof(buf), "rsyslogd: %s", type);
	else
		(void) snprintf(buf, sizeof(buf), "rsyslogd: %s: %s", type, strerror(errno));
	errno = 0;
	logmsgInternal(LOG_SYSLOG|LOG_ERR, buf, LocalHostName, ADDDATE);
	return;
}

void die(sig)

	int sig;
	
{
	register struct filed *f;
	char buf[100];
	int lognum;
	int i;
	int was_initialized = Initialized;

	Initialized = 0;	/* Don't log SIGCHLDs in case we
				   receive one during exiting */

	for (lognum = 0; lognum <= nlogs; lognum++) {
		f = &Files[lognum];
		/* flush any pending output */
		if (f->f_prevcount)
			/* rgerhards: 2004-11-09: I am now changing it, but
			 * I am not sure it is correct as done.
			 * TODO: verify later!
			 */
			fprintlog(f, 0);
	}

	Initialized = was_initialized; /* we restore this so that the logmsgInternal() 
	                                * below can work ... and keep in mind we need the
					* filed structure still intact (initialized) for the below! */
	if (sig) {
		dprintf("syslogd: exiting on signal %d\n", sig);
		(void) snprintf(buf, sizeof(buf), "syslogd: exiting on signal %d", sig);
		errno = 0;
		logmsgInternal(LOG_SYSLOG|LOG_INFO, buf, LocalHostName, ADDDATE);
	}

	/* Close the MySQL connection */
	for (lognum = 0; lognum <= nlogs; lognum++) {
		f = &Files[lognum];
		/* free iovec if it was allocated */
		if(f->f_iov != NULL) {
			if(f->f_bMustBeFreed != NULL) {
				iovDeleteFreeableStrings(f);
				free(f->f_bMustBeFreed);
			}
			free(f->f_iov);
		}
		/* Now delete cached messages */
		MsgDestruct(f->f_pMsg);
#ifdef WITH_DB
		if (f->f_type == F_MYSQL)
			closeMySQL(f);
#endif
	}
	
	/* now clean up the listener part */

	/* Close the UNIX sockets. */
        for (i = 0; i < nfunix; i++)
		if (funix[i] != -1)
			close(funix[i]);
	/* Close the inet socket. */
	if (InetInuse) close(inetm);

	/* Clean-up files. */
        for (i = 0; i < nfunix; i++)
		if (funixn[i] && funix[i] != -1)
			(void)unlink(funixn[i]);

	/* rger 2005-02-22
	 * now clean up the in-memory structures. OK, the OS
	 * would also take care of that, but if we do it
	 * ourselfs, this makes finding memory leaks a lot
	 * easier.
	 */
	tplDeleteAll();
	free(parts);
	free(Files);
	if(consfile.f_iov != NULL)
		free(consfile.f_iov);
	if(consfile.f_bMustBeFreed != NULL)
		free(consfile.f_bMustBeFreed);

#ifndef TESTING
	(void) remove_pid(PidFile);
#endif
	exit(0);
}

/*
 * Signal handler to terminate the parent process.
 */
#ifndef TESTING
void doexit(sig)
	int sig;
{
	exit (0);
}
#endif

/* parse and interpret a $-config line that starts with
 * a name (this is common code). It is parsed to the name
 * and then the proper sub-function is called to handle
 * the actual directive.
 * rgerhards 2004-11-17
 * rgerhards 2005-06-21: previously only for templates, now 
 *    generalized.
 */
void doNameLine(char **pp, enum eDirective eDir)
{
	char *p = *pp;
	char szName[128];

	assert(pp != NULL);
	assert(p != NULL);
	assert((eDir == DIR_TEMPLATE) || (eDir == DIR_OUTCHANNEL));

	if(getSubString(&p, szName, sizeof(szName) / sizeof(char), ',')  != 0) {
		char errMsg[128];
		snprintf(errMsg, sizeof(errMsg)/sizeof(char),
		         "Invalid $%s line: could not extract name - line ignored",
			 directive_name_list[eDir]);
		logerror(errMsg);
		return;
	}
	if(*p == ',')
		++p; /* comma was eaten */
	
	/* we got the name - now we pass name & the rest of the string
	 * to the subfunction. It makes no sense to do further
	 * parsing here, as this is in close interaction with the
	 * respective subsystem. rgerhards 2004-11-17
	 */
	
	if(eDir == DIR_TEMPLATE)
		tplAddLine(szName, &p);
	else
		ochAddLine(szName, &p);

	*pp = p;
	return;
}


/* Parse and interpret a system-directive in the config line
 * A system directive is one that starts with a "$" sign. It offers
 * extended configuration parameters.
 * 2004-11-17 rgerhards
 */
void cfsysline(char *p)
{
	char szCmd[32];

	assert(p != NULL);
	errno = 0;
	dprintf("cfsysline --> %s", p);
	if(getSubString(&p, szCmd, sizeof(szCmd) / sizeof(char), ' ')  != 0) {
		logerror("Invalid $-configline - could not extract command - line ignored\n");
		return;
	}

	/* check the command and carry out processing */
	if(!strcmp(szCmd, "template")) { 
		doNameLine(&p, DIR_TEMPLATE);
	} else if(!strcmp(szCmd, "outchannel")) { 
		doNameLine(&p, DIR_OUTCHANNEL);
	} else { /* invalid command! */
		char err[100];
		snprintf(err, sizeof(err)/sizeof(char),
		         "Invalid command in $-configline: '%s' - line ignored\n", szCmd);
		logerror(err);
		return;
	}
}


/*
 *  INIT -- Initialize syslogd from configuration table
 */
void init()
{
	register int i;
	register FILE *cf;
	register struct filed *f;
	register struct filed *nextp;
	register char *p;
	register unsigned int Forwarding = 0;
#ifdef CONT_LINE
	char cbuf[BUFSIZ];
	char *cline;
#else
	char cline[BUFSIZ];
#endif
	struct servent *sp;

	nextp = NULL;
	sp = getservbyname("syslog", "udp");
	if (sp == NULL) {
		errno = 0;
		logerror("Could not find syslog/udp port in /etc/services.");
		logerror("Now using default of 514.");
		LogPort = 514;
	}
	else
		LogPort = sp->s_port;

	/*
	 *  Close all open log files and free log descriptor array.
	 */
	dprintf("Called init.\n");
	Initialized = 0;
	if ( nlogs > -1 )
	{
		dprintf("Initializing log structures.\n");

		f = Files;
		while (f != NULL) {
			/* flush any pending output */
			if (f->f_prevcount)
				/* rgerhards: 2004-11-09: I am now changing it, but
				 * I am not sure it is correct as done.
				 * TODO: verify later!
				 */
				fprintlog(f, 0);

			/* free iovec if it was allocated */
			if(f->f_iov != NULL) {
				if(f->f_bMustBeFreed != NULL) {
					iovDeleteFreeableStrings(f);
					free(f->f_bMustBeFreed);
				}
				free(f->f_iov);
			}

			switch (f->f_type) {
				case F_FILE:
				case F_PIPE:
				case F_TTY:
				case F_CONSOLE:
					(void) close(f->f_file);
				break;
#ifdef	WITH_DB
				case F_MYSQL:
					closeMySQL(f);
				break;
#endif
			}
			/* done with this entry, we now need to delete itself */
			f = f->f_next;
			free(f);
		}

		/* Reflect the deletion of the Files linked list. */
		nlogs = -1;
		Files = NULL;
	}
	
	f = NULL;
	nextp = NULL;
	/* open the configuration file */
	if ((cf = fopen(ConfFile, "r")) == NULL) {
		/* rgerhards: this code is executed to set defaults when the
		 * config file could not be opened. We might think about
		 * abandoning the run in this case - but this, too, is not
		 * very clever...
		 */
		dprintf("cannot open %s (%s).\n", ConfFile, strerror(errno));
		nextp = (struct filed *)calloc(1, sizeof(struct filed));
		Files = nextp; /* set the root! */
		cfline("*.ERR\t" _PATH_CONSOLE, nextp);
		nextp->f_next = (struct filed *)calloc(1, sizeof(struct filed));
		cfline("*.PANIC\t*", nextp->f_next);
		nextp->f_next = (struct filed *)calloc(1, sizeof(struct filed));
		snprintf(cbuf,sizeof(cbuf), "*.*\t%s", ttyname(0));
		cfline(cbuf, nextp->f_next);
		Initialized = 1;
	}
	else { /* we should consider moving this into a separate function - TODO */
		/*
		 *  Foreach line in the conf table, open that file.
		 */
	#if CONT_LINE
		cline = cbuf;
		while (fgets(cline, sizeof(cbuf) - (cline - cbuf), cf) != NULL) {
	#else
		while (fgets(cline, sizeof(cline), cf) != NULL) {
	#endif
			/*
			 * check for end-of-section, comments, strip off trailing
			 * spaces and newline character.
			 */
			for (p = cline; isspace(*p); ++p) /*SKIP SPACES*/;
			if (*p == '\0' || *p == '#')
				continue;

			if(*p == '$') {
				cfsysline(++p);
				continue;
			}
	#if CONT_LINE
			strcpy(cline, p);
	#endif
			for (p = strchr(cline, '\0'); isspace(*--p););
	#if CONT_LINE
			if (*p == '\\') {
				if ((p - cbuf) > BUFSIZ - 30) {
					/* Oops the buffer is full - what now? */
					cline = cbuf;
				} else {
					*p = 0;
					cline = p;
					continue;
				}
			}  else
				cline = cbuf;
	#endif
			*++p = '\0';

			/* allocate next entry and add it */
			f = (struct filed *)calloc(1, sizeof(struct filed));
			/* TODO: check for NULL pointer (this is a general issue in this code...)! */
			if(nextp == NULL) {
				Files = f;
			}
			else {
				nextp->f_next = f;
			}
			nextp = f;

			/* be careful: the default below must be set BEFORE calling cfline()! */
			f->f_sizeLimit = 0; /* default value, use outchannels to configure! */
	#if CONT_LINE
			cfline(cbuf, f);
	#else
			cfline(cline, f);
	#endif
			if (f->f_type == F_FORW || f->f_type == F_FORW_SUSP || f->f_type == F_FORW_UNKN) {
				Forwarding++;
			}
		}

		/* close the configuration file */
		(void) fclose(cf);
	}


#ifdef SYSLOG_UNIXAF
	for (i = 0; i < nfunix; i++) {
		if (funix[i] != -1)
			/* Don't close the socket, preserve it instead
			close(funix[i]);
			*/
			continue;
		if ((funix[i] = create_unix_socket(funixn[i])) != -1)
			dprintf("Opened UNIX socket `%s'.\n", funixn[i]);
	}
#endif

#ifdef SYSLOG_INET
	if (Forwarding || AcceptRemote) {
		if (finet < 0) {
			finet = create_inet_socket();
			if (finet >= 0) {
				InetInuse = 1;
				dprintf("Opened syslog UDP port.\n");
			}
		}
	}
	else {
		if (finet >= 0)
			close(finet);
		finet = -1;
		InetInuse = 0;
	}
	inetm = finet;
#endif

	Initialized = 1;

	if ( Debug ) {
		for (f = Files; f; f = f->f_next) {
			if (f->f_type != F_UNUSED) {
				for (i = 0; i <= LOG_NFACILITIES; i++)
					if (f->f_pmask[i] == TABLE_NOPRI)
						printf(" X ");
					else
						printf("%2X ", f->f_pmask[i]);
				printf("%s: ", TypeNames[f->f_type]);
				switch (f->f_type) {
				case F_FILE:
				case F_PIPE:
				case F_TTY:
				case F_CONSOLE:
					printf("%s", f->f_un.f_fname);
					if (f->f_file == -1)
						printf(" (unused)");
					break;

				case F_FORW:
				case F_FORW_SUSP:
				case F_FORW_UNKN:
					printf("%s", f->f_un.f_forw.f_hname);
					break;

				case F_USERS:
					for (i = 0; i < MAXUNAMES && *f->f_un.f_uname[i]; i++)
						printf("%s, ", f->f_un.f_uname[i]);
					break;
				}
				printf("\n");
			}
		}
		tplPrintList();
		ochPrintList();
	}

	if ( AcceptRemote )
#ifdef DEBRELEASE
		logmsgInternal(LOG_SYSLOG|LOG_INFO, "rsyslogd " VERSION "." PATCHLEVEL "#" DEBRELEASE \
		       ": restart (remote reception)." , LocalHostName, \
		       	ADDDATE);
#else
		logmsgInternal(LOG_SYSLOG|LOG_INFO, "rsyslogd " VERSION "." PATCHLEVEL \
		       ": restart (remote reception)." , LocalHostName, \
		       	ADDDATE);
#endif
	else
#ifdef DEBRELEASE
		logmsgInternal(LOG_SYSLOG|LOG_INFO, "rsyslogd " VERSION "." PATCHLEVEL "#" DEBRELEASE \
		       ": restart." , LocalHostName, ADDDATE);
#else
		logmsgInternal(LOG_SYSLOG|LOG_INFO, "rsyslogd " VERSION "." PATCHLEVEL \
		       ": restart." , LocalHostName, ADDDATE);
#endif
	(void) signal(SIGHUP, sighup_handler);
	dprintf("syslogd: restarted.\n");
}

/* helper to cfline() and its helpers. Assign the right template
 * to a filed entry and allocates memory for its iovec.
 * rgerhards 2004-11-19
 */
void cflineSetTemplateAndIOV(struct filed *f, char *pTemplateName)
{
	char errMsg[512];

	assert(f != NULL);
	assert(pTemplateName != NULL);

	/* Ok, we got everything, so it now is time to look up the
	 * template (Hint: templates MUST be defined before they are
	 * used!) and initialize the pointer to it PLUS the iov 
	 * pointer. We do the later because the template tells us
	 * how many elements iov must have - and this can never change.
	 */
	if((f->f_pTpl = tplFind(pTemplateName, strlen(pTemplateName))) == NULL) {
		snprintf(errMsg, sizeof(errMsg) / sizeof(char),
			 "rsyslogd: Could not find template '%s'\n", pTemplateName);
		logmsgInternal(LOG_SYSLOG|LOG_ERR, errMsg, LocalHostName, ADDDATE);
		dprintf(errMsg);
		f->f_type = F_UNUSED;
	} else {
		if((f->f_iov = calloc(tplGetEntryCount(f->f_pTpl),
		    sizeof(struct iovec))) == NULL) {
			/* TODO: provide better message! */
			dprintf("Could not allocate iovec memory\n");
			f->f_type = F_UNUSED;
		}
		if((f->f_bMustBeFreed = calloc(tplGetEntryCount(f->f_pTpl),
		    sizeof(unsigned short))) == NULL) {
			/* TODO: provide better message! */
			dprintf("Could not allocate bMustBeFreed memory\n");
			f->f_type = F_UNUSED;
		}
	}
}
	
/* Helper to cfline() and its helpers. Parses a template name
 * from an "action" line. Must be called with the Line pointer
 * pointing to the first character after the semicolon.
 * Everything is stored in the filed struct. If there is no
 * template name (it is empty), than it is ensured that the
 * returned string is "\0". So you can count on the first character
 * to be \0 in this case.
 * rgerhards 2004-11-19
 */
void cflineParseTemplateName(struct filed *f, char** pp,
			     register char* pTemplateName, int iLenTemplate)
{
	register char *p;
	int i;

	assert(f != NULL);
	assert(pp != NULL);
	assert(*pp != NULL);

	p =*pp;

	/* Just as a general precaution, we skip whitespace.  */
	while(*p && isspace(*p))
		++p;

	i = 1; /* we start at 1 so that we resever space for the '\0'! */
	while(*p && i < iLenTemplate) {
		*pTemplateName++ = *p++;
		++i;
	}
	*pTemplateName = '\0';

	*pp = p;
}

/* Helper to cfline(). Parses a file name up until the first
 * comma and then looks for the template specifier. Tries
 * to find that template. Everything is stored in the
 * filed struct.
 * rgerhards 2004-11-18
 */
void cflineParseFileName(struct filed *f, char* p)
{
	register char *pName;
	int i;
	char szTemplateName[128];	/* should be more than sufficient */

	if(*p == '|') {
		f->f_type = F_PIPE;
		++p;
	} else {
		f->f_type = F_FILE;
	}

	pName = f->f_un.f_fname;
	i = 1; /* we start at 1 so that we resever space for the '\0'! */
	while(*p && *p != ';' && i < MAXFNAME) {
		*pName++ = *p++;
		++i;
	}
	*pName = '\0';

	/* got the file name - now let's look for the template to use
	 * Just as a general precaution, we skip whitespace.
	 */
	while(*p && isspace(*p))
		++p;
	if(*p == ';')
		++p; /* eat it */

	cflineParseTemplateName(f, &p, szTemplateName,
	                        sizeof(szTemplateName) / sizeof(char));

	if(szTemplateName[0] == '\0')	/* no template? */
		strcpy(szTemplateName, " TradFmt"); /* use default! */

	cflineSetTemplateAndIOV(f, szTemplateName);
	
	dprintf("filename: '%s', template: '%s'\n", f->f_un.f_fname, szTemplateName);
}


/* Helper to cfline(). Parses a output channel name up until the first
 * comma and then looks for the template specifier. Tries
 * to find that template. Maps the output channel to the 
 * proper filed structure settings. Everything is stored in the
 * filed struct. Over time, the dependency on filed might be
 * removed.
 * rgerhards 2005-06-21
 */
void cflineParseOutchannel(struct filed *f, char* p)
{
	int i;
	struct outchannel *pOch;
	char szBuf[128];	/* should be more than sufficient */

	/* this must always be a file, because we can not set a size limit
	 * on a pipe...
	 * rgerhards 2005-06-21: later, this will be a separate type, but let's
	 * emulate things for the time being. When everything runs, we can
	 * extend it...
	 */
	f->f_type = F_FILE;

	++p; /* skip '$' */
	i = 0;
	/* get outchannel name */
	while(*p && *p != ';' && *p != ' ' &&
	      i < sizeof(szBuf) / sizeof(char)) {
	      szBuf[i++] = *p++;
	}
	szBuf[i] = '\0';

	/* got the name, now look up the channel... */
	pOch = ochFind(szBuf, i);

	if(pOch == NULL) {
		char errMsg[128];
		errno = 0;
		snprintf(errMsg, sizeof(errMsg)/sizeof(char),
			 "outchannel '%s' not found - ignoring action line",
			 szBuf);
		logerror(errMsg);
		f->f_type = F_UNUSED;
		return;
	}

	/* check if there is a file name in the outchannel... */
	if(pOch->pszFileTemplate == NULL) {
		char errMsg[128];
		errno = 0;
		snprintf(errMsg, sizeof(errMsg)/sizeof(char),
			 "outchannel '%s' has no file name template - ignoring action line",
			 szBuf);
		logerror(errMsg);
		f->f_type = F_UNUSED;
		return;
	}

	/* OK, we finally got a correct template. So let's use it... */
	strncpy(f->f_un.f_fname, pOch->pszFileTemplate, MAXFNAME);
	f->f_sizeLimit = pOch->uSizeLimit;
	/* WARNING: It is dangerous "just" to pass the pointer. As wer
	 * never rebuild the output channel description, this is acceptable here.
	 */
	f->f_sizeLimitCmd = pOch->cmdOnSizeLimit;

	/* back to the input string - now let's look for the template to use
	 * Just as a general precaution, we skip whitespace.
	 */
	while(*p && isspace(*p))
		++p;
	if(*p == ';')
		++p; /* eat it */

	cflineParseTemplateName(f, &p, szBuf,
	                        sizeof(szBuf) / sizeof(char));

	if(szBuf[0] == '\0')	/* no template? */
		strcpy(szBuf, " TradFmt"); /* use default! */

	cflineSetTemplateAndIOV(f, szBuf);
	
	dprintf("[outchannel]filename: '%s', template: '%s', size: %lu\n", f->f_un.f_fname, szBuf,
		f->f_sizeLimit);
}


/*
 * Crack a configuration file line
 * rgerhards 2004-11-17: well, I somewhat changed this function. It now does NOT
 * handle config lines in general, but only lines that reflect actual filter
 * pairs (the original syslog message line format). Extended lines (those starting
 * with "$" have been filtered out by the caller and are passed to another function (cfsysline()).
 * Please note, however, that I needed to make changes in the line syntax to support
 * assignment of format definitions to a file. So it is not (yet) 100% transparent.
 * Eventually, we can overcome this limitation by prefixing the actual action indicator
 * (e.g. "/file..") by something (e.g. "$/file..") - but for now, we just modify it... 
 */
void cfline(line, f)
	char *line;
	register struct filed *f;
{
	char *p;
	register char *q;
	register int i, i2;
	char *bp;
	int pri;
	int singlpri = 0;
	int ignorepri = 0;
	int syncfile;
#ifdef SYSLOG_INET
	struct hostent *hp;
#endif
	char buf[MAXLINE];
	char szTemplateName[128];
	char xbuf[200];
#ifdef WITH_DB
	int iMySQLPropErr = 0;
#endif

	dprintf("cfline(%s)\n", line);

	errno = 0;	/* keep strerror() stuff out of logerror messages */

	/* Note: file structure is pre-initialized to zero because it was
	 * created with calloc()!
	 */
	for (i = 0; i <= LOG_NFACILITIES; i++) {
		f->f_pmask[i] = TABLE_NOPRI;
		f->f_flags = 0;
	}

	/* scan through the list of selectors */
	for (p = line; *p && *p != '\t' && *p != ' ';) {

		/* find the end of this facility name list */
		for (q = p; *q && *q != '\t' && *q++ != '.'; )
			continue;

		/* collect priority name */
		for (bp = buf; *q && !strchr("\t ,;", *q); )
			*bp++ = *q++;
		*bp = '\0';

		/* skip cruft */
		while (strchr(",;", *q))
			q++;

		/* decode priority name */
		if ( *buf == '!' ) {
			ignorepri = 1;
			for (bp=buf; *(bp+1); bp++)
				*bp=*(bp+1);
			*bp='\0';
		}
		else {
			ignorepri = 0;
		}
		if ( *buf == '=' )
		{
			singlpri = 1;
			pri = decode(&buf[1], PriNames);
		}
		else {
		        singlpri = 0;
			pri = decode(buf, PriNames);
		}

		if (pri < 0) {
			(void) snprintf(xbuf, sizeof(xbuf), "unknown priority name \"%s\"", buf);
			logerror(xbuf);
			return;
		}

		/* scan facilities */
		while (*p && !strchr("\t .;", *p)) {
			for (bp = buf; *p && !strchr("\t ,;.", *p); )
				*bp++ = *p++;
			*bp = '\0';
			if (*buf == '*') {
				for (i = 0; i <= LOG_NFACILITIES; i++) {
					if ( pri == INTERNAL_NOPRI ) {
						if ( ignorepri )
							f->f_pmask[i] = TABLE_ALLPRI;
						else
							f->f_pmask[i] = TABLE_NOPRI;
					}
					else if ( singlpri ) {
						if ( ignorepri )
				  			f->f_pmask[i] &= ~(1<<pri);
						else
				  			f->f_pmask[i] |= (1<<pri);
					}
					else
					{
						if ( pri == TABLE_ALLPRI ) {
							if ( ignorepri )
								f->f_pmask[i] = TABLE_NOPRI;
							else
								f->f_pmask[i] = TABLE_ALLPRI;
						}
						else
						{
							if ( ignorepri )
								for (i2= 0; i2 <= pri; ++i2)
									f->f_pmask[i] &= ~(1<<i2);
							else
								for (i2= 0; i2 <= pri; ++i2)
									f->f_pmask[i] |= (1<<i2);
						}
					}
				}
			} else {
				i = decode(buf, FacNames);
				if (i < 0) {

					(void) snprintf(xbuf, sizeof(xbuf), "unknown facility name \"%s\"", buf);
					logerror(xbuf);
					return;
				}

				if ( pri == INTERNAL_NOPRI ) {
					if ( ignorepri )
						f->f_pmask[i >> 3] = TABLE_ALLPRI;
					else
						f->f_pmask[i >> 3] = TABLE_NOPRI;
				} else if ( singlpri ) {
					if ( ignorepri )
						f->f_pmask[i >> 3] &= ~(1<<pri);
					else
						f->f_pmask[i >> 3] |= (1<<pri);
				} else {
					if ( pri == TABLE_ALLPRI ) {
						if ( ignorepri )
							f->f_pmask[i >> 3] = TABLE_NOPRI;
						else
							f->f_pmask[i >> 3] = TABLE_ALLPRI;
					} else {
						if ( ignorepri )
							for (i2= 0; i2 <= pri; ++i2)
								f->f_pmask[i >> 3] &= ~(1<<i2);
						else
							for (i2= 0; i2 <= pri; ++i2)
								f->f_pmask[i >> 3] |= (1<<i2);
					}
				}
			}
			while (*p == ',' || *p == ' ')
				p++;
		}

		p = q;
	}

	/* skip to action part */
	while (*p == '\t' || *p == ' ')
		p++;

	if (*p == '-')
	{
		syncfile = 0;
		p++;
	} else
		syncfile = 1;

	dprintf("leading char in action: %c\n", *p);
	switch (*p)
	{
	case '@':
#ifdef SYSLOG_INET
		++p; /* eat '@' */
		/* extract the host first (we do a trick - we 
		 * replace the ';' with a '\0') */
		for(q = p ; *p && *p != ';' ; ++p)
		 	/* JUST SKIP */;
		if(*p == ';') {
			*p = '\0'; /* trick! */
			++p;
			 /* Now look for the template! */
			cflineParseTemplateName(f, &p, szTemplateName,
						sizeof(szTemplateName) / sizeof(char));
		} else
			szTemplateName[0] = '\0';
		if(szTemplateName[0] == '\0') {
			/* we do not have a template, so let's use the default */
			strcpy(szTemplateName, " StdFwdFmt");
		}

		/* first set the f->f_type */
		if ( (hp = gethostbyname(q)) == NULL ) {
			f->f_type = F_FORW_UNKN;
			f->f_prevcount = INET_RETRY_MAX;
			f->f_time = time((time_t *) NULL);
		} else {
			f->f_type = F_FORW;
		}

		/* then try to find the template and re-set f_type to UNUSED
		 * if it can not be found. */
		cflineSetTemplateAndIOV(f, szTemplateName);

		(void) strcpy(f->f_un.f_forw.f_hname, q);
		dprintf("forwarding host: '%s' template '%s'\n",
		         q, szTemplateName);	/*ASP*/
		memset((char *) &f->f_un.f_forw.f_addr, 0,
			 sizeof(f->f_un.f_forw.f_addr));
		f->f_un.f_forw.f_addr.sin_family = AF_INET;
		f->f_un.f_forw.f_addr.sin_port = LogPort;
		if ( f->f_type == F_FORW )
			memcpy((char *) &f->f_un.f_forw.f_addr.sin_addr, hp->h_addr, hp->h_length);
		/*
		 * Otherwise the host might be unknown due to an
		 * inaccessible nameserver (perhaps on the same
		 * host). We try to get the ip number later, like
		 * FORW_SUSP.
		 */
#endif
		break;

        case '$':
		/* rgerhards 2005-06-21: this is a special setting for output-channel
		 * defintions. In the long term, this setting will probably replace
		 * anything else, but for the time being we must co-exist with the
		 * traditional mode lines.
		 */
		cflineParseOutchannel(f, p);
		f->f_file = open(f->f_un.f_fname, O_WRONLY|O_APPEND|O_CREAT|O_NOCTTY,
				 0644);
		break;

        case '|':
	case '/':
		/* rgerhards 2004-11-17: from now, we need to have different
		 * processing, because after the first comma, the template name
		 * to use is specified. So we need to scan for the first coma first
		 * and then look at the rest of the line.
		 */
		cflineParseFileName(f, p);
		if (syncfile)
			f->f_flags |= SYNC_FILE;
		if (f->f_type == F_PIPE) {
			f->f_file = open(f->f_un.f_fname, O_RDWR|O_NONBLOCK);
	        } else {
			f->f_file = open(f->f_un.f_fname, O_WRONLY|O_APPEND|O_CREAT|O_NOCTTY,
					 0644);
		}
		        
	  	if ( f->f_file < 0 ){
			f->f_file = -1;
			dprintf("Error opening log file: %s\n", f->f_un.f_fname);
			logerror(f->f_un.f_fname);
			break;
		}
		if (isatty(f->f_file)) {
			f->f_type = F_TTY;
			untty();
		}
		if (strcmp(p, ctty) == 0)
			f->f_type = F_CONSOLE;
		break;

	case '*':
		dprintf ("write-all");
		if(*(p+1) == ';') {
			/* we have a template specifier! */
			p += 2; /* eat "*;" */
			cflineParseTemplateName(f, &p, szTemplateName,
						sizeof(szTemplateName) / sizeof(char));
		}
		else	/* assign default format if none given! */
			szTemplateName[0] = '\0';
		if(szTemplateName[0] == '\0')
			strcpy(szTemplateName, " WallFmt");
		cflineSetTemplateAndIOV(f, szTemplateName);
		dprintf(" template '%s'\n", szTemplateName);
		f->f_type = F_WALL;
		break;

#ifdef	WITH_DB
	case '>':	/* rger 2004-10-28: added support for MySQL
			 * >server,dbname,userid,password
			 */
		dprintf ("in init() - WITH_DB case \n");
		f->f_type = F_MYSQL;
		p++;
		
		/* Now we read the MySQL connection properties 
		 * and verify that the properties are valid.
		 */
		if(getSubString(&p, f->f_dbsrv, MAXHOSTNAMELEN+1, ','))
			iMySQLPropErr++;
		if(*f->f_dbsrv == '\0')
			iMySQLPropErr++;
		if(getSubString(&p, f->f_dbname, _DB_MAXDBLEN+1, ','))
			iMySQLPropErr++;
		if(*f->f_dbname == '\0')
			iMySQLPropErr++;
		if(getSubString(&p, f->f_dbuid, _DB_MAXUNAMELEN+1, ','))
			iMySQLPropErr++;
		if(*f->f_dbuid == '\0')
			iMySQLPropErr++;
		if(getSubString(&p, f->f_dbpwd, _DB_MAXPWDLEN+1, ';'))
			iMySQLPropErr++;
		if(*p != '\n') { 
			/* we have a template specifier! */
			cflineParseTemplateName(f, &p, szTemplateName,
						sizeof(szTemplateName) / sizeof(char));
		}
		else	/* assign default format if none given! */
			szTemplateName[0] = '\0';

		if(szTemplateName[0] == '\0')
			strcpy(szTemplateName, " StdDBFmt");

		cflineSetTemplateAndIOV(f, szTemplateName);
		dprintf(" template '%s'\n", szTemplateName);
		
		/* If db used, the template have to use the SQL option.
		   This is for your own protection (prevent sql injection). */
		if (f->f_pTpl->optFormatForSQL != 1)
		{
			f->f_type = F_UNUSED;
			dprintf("DB logging disabled. You have to use"
				" the SQL option in your template!\n");

		}
		
		/* If we dedect invalid properties, we disable logging, 
		 * because right properties are vital at this place.  
		 * Retrials make no sens. 
		 */
		if (iMySQLPropErr) { 
			f->f_type = F_UNUSED;
			dprintf("Trouble with MySQL conncetion properties.\n"
				"MySQL logging disabled.\n");
		}
		else {
			initMySQL(f);
		}
		break;
#endif	/* #ifdef WITH_DB */

	default:
		dprintf ("users: %s\n", p);	/* ASP */
		f->f_type = F_USERS;
		for (i = 0; i < MAXUNAMES && *p && *p != ';'; i++) {
			for (q = p; *q && *q != ',' && *q != ';'; )
				q++;
			(void) strncpy(f->f_un.f_uname[i], p, UNAMESZ);
			if ((q - p) > UNAMESZ)
				f->f_un.f_uname[i][UNAMESZ] = '\0';
			else
				f->f_un.f_uname[i][q - p] = '\0';
			while (*q == ',' || *q == ' ')
				q++;
			p = q;
		}
		/* done, now check if we have a template name
		 * TODO: we need to handle the case where i >= MAXUNAME!
		 */
		szTemplateName[0] = '\0';
		if(*p == ';') {
			/* we have a template specifier! */
			++p; /* eat ";" */
			cflineParseTemplateName(f, &p, szTemplateName,
						sizeof(szTemplateName) / sizeof(char));
		}
		if(szTemplateName[0] == '\0')
			strcpy(szTemplateName, " StdUsrMsgFmt");
		cflineSetTemplateAndIOV(f, szTemplateName);
		break;
	}
	return;
}


/*
 *  Decode a symbolic name to a numeric value
 */

int decode(name, codetab)
	char *name;
	struct code *codetab;
{
	register struct code *c;
	register char *p;
	char buf[80];

	dprintf ("symbolic name: %s", name);
	if (isdigit(*name))
	{
		dprintf ("\n");
		return (atoi(name));
	}
	(void) strncpy(buf, name, 79);
	for (p = buf; *p; p++)
		if (isupper(*p))
			*p = tolower(*p);
	for (c = codetab; c->c_name; c++)
		if (!strcmp(buf, c->c_name))
		{
			dprintf (" ==> %d\n", c->c_val);
			return (c->c_val);
		}
	return (-1);
}

void dprintf(char *fmt, ...)

{
	va_list ap;

	if ( !(Debug && debugging_on) )
		return;
	
	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	va_end(ap);

	fflush(stdout);
	return;
}


/*
 * The following function is resposible for handling a SIGHUP signal.  Since
 * we are now doing mallocs/free as part of init we had better not being
 * doing this during a signal handler.  Instead this function simply sets
 * a flag variable which will tell the main loop to go through a restart.
 */
void sighup_handler()

{
	restart = 1;
	signal(SIGHUP, sighup_handler);
	return;
}

#ifdef WITH_DB
/*
 * The following function is responsible for initiatlizing a
 * MySQL connection.
 * Initially added 2004-10-28 mmeckelein
 */
void initMySQL(register struct filed *f)
{
	int iCounter = 0;
	assert(f != NULL);

	if (checkDBErrorState(f))
		return;
	
	mysql_init(&f->f_hmysql);
	do {
		iCounter++;
		/* Connect to database */
		if (!mysql_real_connect(&f->f_hmysql, f->f_dbsrv, f->f_dbuid, f->f_dbpwd, f->f_dbname, 0, NULL, 0)) {
			/* if also the second attempt failed
			   we call the error handler */
			if(iCounter)
				DBErrorHandler(f);
		}
		else
		{
			f->f_timeResumeOnError = 0; /* We have a working db connection */
			dprintf("connected successfully to db\n");
		}
	} while (mysql_errno(&f->f_hmysql) && iCounter<2);
}

/*
 * The following function is responsible for closing a
 * MySQL connection.
 * Initially added 2004-10-28
 */
void closeMySQL(register struct filed *f)
{
	assert(f != NULL);
	dprintf("in closeMySQL\n");
	mysql_close(&f->f_hmysql);	
}

/*
 * Reconnect a MySQL connection.
 * Initially added 2004-12-02
 */
void reInitMySQL(register struct filed *f)
{
	assert(f != NULL);

	dprintf("reInitMySQL\n");
	/* close the current handle */
	closeMySQL(f);
	/* new connection */   
	initMySQL(f);
}

/*
 * The following function writes the current log entry
 * to an established MySQL session.
 * Initially added 2004-10-28
 */
void writeMySQL(register struct filed *f)
{
	char *psz;
	int iCounter=0;
	assert(f != NULL);
	/* dprintf("in writeMySQL()\n"); */
	iovCreate(f);
	psz = iovAsString(f);
	
	if (checkDBErrorState(f))
		return;

	/* 
	 * Now we are trying to insert the data. 
	 *
	 * If the first attampt failes we simply try a second one. If also 
	 * the second attampt failed we discard this message and enable  
	 * the "delay" error hanlding.
	 */
	do {
		iCounter++;
		/* query */
		if(mysql_query(&f->f_hmysql, psz)) {

			/* if also the second attempt failed
			   we call the error handler */
			if(iCounter)
				DBErrorHandler(f);	
		}
		else {
			/* dprintf("db insert sucessfully\n"); */
		}
	} while (mysql_errno(&f->f_hmysql) && iCounter<2);
}

/**
 * DBErrorHandler
 *
 * Call this function if an db error apears. It will initiate 
 * the "delay" handling which stopped the db logging for some 
 * time.  
 */
void DBErrorHandler(register struct filed *f)
{
	/* TODO:
	 * NO DB connection -> Can not log to DB
	 * -------------------- 
	 * Case 1: db server unavailable
	 * We can check after a specified time interval if the server is up.
	 * Also a reason can be a down DNS service.
	 * Case 2: uid, pwd or dbname are incorrect
	 * If this is a fault in the syslog.conf we have no chance to recover. But
	 * if it is a problem of the DB we can make a retry after some time. Possible
	 * are that the admin has not already set up the database table. Or he has not
	 * created the database user yet. 
	 * Case 3: unkown error
	 * If we get an unkowon error here, we should in any case try to recover after
	 * a specified time interval.
	 *
	 * Insert failed -> Can not log to DB
	 * -------------------- 
	 * If the insert fails it is never a good idea to give up. Only an
	 * invalid sql sturcture (wrong template) force us to disable db
	 * logging. 
	 *
	 * Think about diffrent "delay" for diffrent errors!
	 */
	dprintf("db error no: %d\n", mysql_errno(&f->f_hmysql));
	dprintf("db error: %s\n", mysql_error(&f->f_hmysql));
	/* Enable "delay" */
	f->f_timeResumeOnError = time(&f->f_timeResumeOnError) + _DB_DELAYTIMEONERROR ;
	f->f_iLastDBErrNo = mysql_errno(&f->f_hmysql);

}

/**
 * checkDBErrorState
 *
 * Check if we can go on with database logging or if we should wait
 * a little bit longer. It also check if the DB hanlde is still valid. 
 * If it is necessary, it takes action to reinitiate the db connection.
 *
 * \ret int		Returns 0 if successful (no error)
 */ 
int checkDBErrorState(register struct filed *f)
{
	assert(f != NULL);
	/* dprintf("in checkDBErrorState, timeResumeOnError: %d\n", f->f_timeResumeOnError); */

	/* If timeResumeOnError == 0 no error occured, 
	   we can return with 0 (no error) */
	if (f->f_timeResumeOnError == 0)
		return 0;
	
	(void) time(&now);
	/* Now we know an error occured. We check timeResumeOnError
	   if we can process. If we have not reach the resume time
	   yet, we return an error status. */  
	if (f->f_timeResumeOnError > now)
	{
		/* dprintf("Wait time is not over yet.\n"); */
		return 1;
	}
	 	
	/* Ok, we can try to resume the database logging. First
	   we have to reset the status (timeResumeOnError) and
	   the last error no. */
	/* TODO:
	 * To improve this code it would be better to check 
	   if we really need to reInit the db connection. If 
	   only the insert failed and the db conncetcion is
	   still valid, we need no reInit. 
	   Of course, if an unkown error appeared, we should
	   reInit. */
	 /* rgerhards 2004-12-08: I think it is pretty unlikely
	  * that we can re-use a connection after the error. So I guess
	  * the connection must be closed and re-opened in all cases
	  * (as it is done currently). When we come back to optimize
	  * this code, we should anyhow see if there are cases where
	  * we could keep it open. I just doubt this won't be the case.
	  * I added this comment (and did not remove Michaels) just so
	  * that we all know what we are looking for.
	  */
	f->f_timeResumeOnError = 0;
	f->f_iLastDBErrNo = 0; 
	reInitMySQL(f);
	return 0;

}

#endif	/* #ifdef WITH_DB */

/**
 * getSubString
 *
 * Copy a string byte by byte until the occurrence  
 * of a given separator.
 *
 * \param ppSrc		Pointer to a pointer of the source array of characters. If a
			separator detected the Pointer points to the next char after the
			separator. Except if the end of the string is dedected ('\n'). 
			Then it points to the terminator char. 
 * \param pDst		Pointer to the destination array of characters. Here the substing
			will be stored.
 * \param DstSize	Maximum numbers of characters to store.
 * \param cSep		Separator char.
 * \ret int		Returns 0 if no error occured.
 */
int getSubString(char **ppSrc,  char *pDst, size_t DstSize, char cSep)
{
	char *pSrc = *ppSrc;
	int iErr = 0; /* 0 = no error, >0 = error */
	while(*pSrc != cSep && *pSrc != '\0' && DstSize>1) {
		*pDst++ = *(pSrc)++;
		DstSize--;
	}
	/* check if the Dst buffer was to small */
	if (*pSrc != cSep && *pSrc != '\0')
	{ 
		dprintf("in getSubString, error Src buffer > Dst buffer\n");
		iErr = 1;
	}	
	if (*pSrc != '\0')
		*ppSrc=pSrc+1;
	*pDst = '\0';
	return iErr;
}

/*
 * Local variables:
 *  c-indent-level: 8
 *  c-basic-offset: 8
 *  tab-width: 8
 * End:
 * vi:set ai:
 */

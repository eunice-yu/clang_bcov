/* grep.c - main driver file for grep.
   Copyright (C) 1992 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

   Written July 1992 by Mike Haertel.  */

/* I added a series define from makefile here in order to run cfe */
#define GREP 1
#define STDC_HEADERS 1
#define HAVE_STRING_H 1
#define HAVE_SYS_PARAM_H 1
#define HAVE_UNISTD_H 1
#define HAVE_ALLOCA_H 1
#define HAVE_GETPAGESIZE 1
#define HAVE_MEMCHR 1
#define HAVE_STRERROR 1
#define HAVE_VALLOC 1
#define HAVE_WORKING_MMAP 1
#define LINUX 1
/*** end of cfe purpose **/

#include <errno.h>
#include <stdio.h>
#include "br.h"
/*******IMPORTANT CHANGE for let stderr redirect to a file *******/
#undef stderr
#define stderr stdout
/******* FINISH IMPORTANT CHANGE *********/

#ifndef errno
extern int errno;
#endif

#ifdef STDC_HEADERS
#include <stdlib.h>
#define flag_stdlib 1
#else
#include <sys/types.h>
#define flag_systypes 1
extern char *malloc(), *realloc();
extern void free();
#endif

#if defined(STDC_HEADERS) || defined(HAVE_STRING_H)
#include <string.h>
#define flag_string 1
#ifdef NEED_MEMORY_H
#include <memory.h>
#define flag_memory 1
#endif
#else
#include <strings.h>
#define flag_strings 1
#ifdef __STDC__
extern void *memchr();
#else
extern char *memchr();
#endif
#define strrchr rindex
#endif

#ifdef HAVE_UNISTD_H
#if flag_systypes==0
#include <sys/types.h>
#define flag_systypes 1
#endif
#include <fcntl.h>
#include <unistd.h>
#else
#define O_RDONLY 0
extern int open(), read(), close();
#endif

#include "getpagesize.h"
#include "grep.h"

#undef MAX
#define MAX(A,B) ((A) > (B) ? (A) : (B))

/* Provide missing ANSI features if necessary. */

#ifndef HAVE_STRERROR
extern int sys_nerr;
extern char *sys_errlist[];
#define strerror(E) ((E) < sys_nerr ? sys_errlist[(E)] : "bogus error number")
#endif

#ifndef HAVE_MEMCHR
#ifdef __STDC__
#define VOID void
#else
#define VOID char
#endif
VOID *
memchr(vp, c, n)
     VOID *vp;
     int c;
     size_t n;
{
  unsigned char *p;

  for (p = (unsigned char *) vp; n--; ++p)
    if (*p == c)
      return (VOID *) p;
  return 0;
}
#endif
    
/* Define flags declared in grep.h. */
char *matcher;
int match_icase;
int match_words;
int match_lines;

/* Functions we'll use to search. */
static void (*compile)();
static char *(*execute)();

/* For error messages. */
static char *prog;
static char *filename;
static int errseen;

/* Print a message and possibly an error string.  Remember
   that something awful happened. */
static void
error(mesg, errnum)
#ifdef __STDC__
     const
#endif
     char *mesg;
     int errnum;
{
  if (_br(errnum,0x46859a8,147,7))
    fprintf(stderr, "%s: %s: %s\n", prog, mesg, strerror(errnum));
  else
    fprintf(stderr, "%s: %s\n", prog, mesg);
  errseen = 1;
}

/* Like error(), but die horribly after printing. */
void
fatal(mesg, errnum)
#ifdef __STDC__
     const
#endif
     char *mesg;
     int errnum;
{
  error(mesg, errnum);
  exit(2);
}

/* Interface to handle errors and fix library lossage. */
char *
xmalloc(size)
     size_t size;
{
  char *result;

  result = malloc(size);
  if (_br(size && !result,0x46877e8,175,7))
    fatal("memory exhausted", 0);
  return result;
}

/* Interface to handle errors and fix some library lossage. */
char *
xrealloc(ptr, size)
     char *ptr;
     size_t size;
{
  char *result;

  if (_br(ptr,0x4687c58,188,7))
    result = realloc(ptr, size);
  else
    result = malloc(size);
  if (_br(size && !result,0x4688048,192,7))
    fatal("memory exhausted", 0);
  return result;
}

#if !defined(HAVE_VALLOC)
#define valloc malloc
#else
#ifdef __STDC__
extern void *valloc(size_t);
#else
extern char *valloc();
#endif
#endif

/* Hairy buffering mechanism for grep.  The intent is to keep
   all reads aligned on a page boundary and multiples of the
   page size. */

static char *buffer;		/* Base of buffer. */
static size_t bufsalloc;	/* Allocated size of buffer save region. */
static size_t bufalloc;		/* Total buffer size. */
static int bufdesc;		/* File descriptor. */
static char *bufbeg;		/* Beginning of user-visible stuff. */
static char *buflim;		/* Limit of user-visible stuff. */

#if defined(HAVE_WORKING_MMAP)
#if flag_systypes==0
#include <sys/types.h>
#define flag_systypes 1
#endif
#include <sys/stat.h>
#include <sys/mman.h>

static int bufmapped;		/* True for ordinary files. */
static struct stat bufstat;	/* From fstat(). */
static off_t bufoffset;		/* What read() normally remembers. */
#endif

/* Reset the buffer for a new file.  Initialize
   on the first time through. */
void
reset(fd)
     int fd;
{
  static int initialized;

  if (_br(!initialized,0x45b3040,239,7))
    {
      initialized = 1;
#ifndef BUFSALLOC
      bufsalloc = MAX(8192, getpagesize());
#else
      bufsalloc = BUFSALLOC;
#endif
      bufalloc = 5 * bufsalloc;
      /* The 1 byte of overflow is a kludge for dfaexec(), which
	 inserts a sentinel newline at the end of the buffer
	 being searched.  There's gotta be a better way... */
      buffer = valloc(bufalloc + 1);
      if (_br(!buffer,0x45b3640,252,11))
	fatal("memory exhausted", 0);
      bufbeg = buffer;
      buflim = buffer;
    }
  bufdesc = fd;
#if defined(HAVE_WORKING_MMAP)
  if (fstat(fd, &bufstat) < 0 || !S_ISREG(bufstat.st_mode))
    bufmapped = 0;
  else
    {
      bufmapped = 1;
      bufoffset = lseek(fd, 0, 1);
    }
#endif
}

/* Read new stuff into the buffer, saving the specified
   amount of old stuff.  When we're done, 'bufbeg' points
   to the beginning of the buffer contents, and 'buflim'
   points just after the end.  Return count of new stuff. */
static int
fillbuf(save)
     size_t save;
{
  char *nbuffer, *dp, *sp;
  int cc;
#if defined(HAVE_WORKING_MMAP)
  caddr_t maddr;
#endif
  static int pagesize;

  if (_br(pagesize == 0 && (pagesize = getpagesize()) == 0,0x45b4870,284,7))
    abort();

  if (_br(save > bufsalloc,0x45b49e0,287,7))
    {
      while (_br(save > bufsalloc,0x45b4a88,289,14))
	bufsalloc *= 2;
      bufalloc = 5 * bufsalloc;
      nbuffer = valloc(bufalloc + 1);
      if (_br(!nbuffer,0x45b4e28,293,11))
	fatal("memory exhausted", 0);
    }
  else
    nbuffer = buffer;

  sp = buflim - save;
  dp = nbuffer + bufsalloc - save;
  bufbeg = dp;
  while (_br(save--,0x45b5390,302,10))
    *dp++ = *sp++;

  /* We may have allocated a new, larger buffer.  Since
     there is no portable vfree(), we just have to forget
     about the old one.  Sorry. */
  buffer = nbuffer;

#if defined(HAVE_WORKING_MMAP)
  if (_br(bufmapped && bufoffset % pagesize == 0
      && bufstat.st_size - bufoffset >= bufalloc - bufsalloc,0x45b58c0,311,7))
    {
      maddr = buffer + bufsalloc;
      maddr = mmap(maddr, bufalloc - bufsalloc, PROT_READ | PROT_WRITE,
		   MAP_PRIVATE | MAP_FIXED, bufdesc, bufoffset);
      if (_br(maddr == (caddr_t) -1,0x45b5e70,317,11))
	{
	  fprintf(stderr, "%s: warning: %s: %s\n", filename,
		  strerror(errno));
	  goto tryread;
	}
#if 0
      /* You might thing this (or MADV_WILLNEED) would help,
	 but it doesn't, at least not on a Sun running 4.1.
	 In fact, it actually slows us down about 30%! */
      madvise(maddr, bufalloc - bufsalloc, MADV_SEQUENTIAL);
#endif
      cc = bufalloc - bufsalloc;
      bufoffset += cc;
    }
  else
    {
    tryread:
      /* We come here when we're not going to use mmap() any more.
	 Note that we need to synchronize the file offset the
	 first time through. */
      if (_br(bufmapped,0x45b64b0,338,11))
	{
	  bufmapped = 0;
	  lseek(bufdesc, bufoffset, 0);
	}
      cc = read(bufdesc, buffer + bufsalloc, bufalloc - bufsalloc);
    }
#else
  cc = read(bufdesc, buffer + bufsalloc, bufalloc - bufsalloc);
#endif
  if (_br(cc > 0,0x45b6a80,348,7))
    buflim = buffer + bufsalloc + cc;
  else
    buflim = buffer + bufsalloc;
  return cc;
}

/* Flags controlling the style of output. */
static int out_quiet;		/* Suppress all normal output. */
static int out_invert;		/* Print nonmatching stuff. */
static int out_file;		/* Print filenames. */
static int out_line;		/* Print line numbers. */
static int out_byte;		/* Print byte offsets. */
static int out_before;		/* Lines of leading context. */
static int out_after;		/* Lines of trailing context. */

/* Internal variables to keep track of byte count, context, etc. */
static size_t totalcc;		/* Total character count before bufbeg. */
static char *lastnl;		/* Pointer after last newline counted. */
static char *lastout;		/* Pointer after last character output;
				   NULL if no character has been output
				   or if it's conceptually before bufbeg. */
static size_t totalnl;		/* Total newline count before lastnl. */
static int pending;		/* Pending lines of output. */

static void
nlscan(lim)
     char *lim;
{
  char *beg;

  for (beg = lastnl; _br(beg < lim,0x46b6f90,379,22); ++beg)
    if (_br(*beg == '\n',0x46b70a8,380,9))
      ++totalnl;
  lastnl = beg;
}

static void
prline(beg, lim, sep)
     char *beg;
     char *lim;
     char sep;
{
  if (_br(out_file,0x46b8538,391,7))
    printf("%s%c", filename, sep);
  if (_br(out_line,0x46b8798,393,7))
    {
      nlscan(beg);
      printf("%d%c", ++totalnl, sep);
      lastnl = lim;
    }
  if (_br(out_byte,0x46b8e90,399,7))
    printf("%lu%c", totalcc + (beg - bufbeg), sep);
  fwrite(beg, 1, lim - beg, stdout);
  if (_br(ferror(stdout),0x46b94e0,402,7))
    error("writing output", errno);
  lastout = lim;
}

/* Print pending lines of trailing context prior to LIM. */
static void
prpending(lim)
     char *lim;
{
  char *nl;

  if (_br(!lastout,0x46b99e8,414,7))
    lastout = bufbeg;
  while (_br(pending > 0 && lastout < lim,0x46b9c00,416,10))
    {
      --pending;
      if (_br((nl = memchr(lastout, '\n', lim - lastout)) != 0,0x46b9f20,419,11))
	++nl;
      else
	nl = lim;
      prline(lastout, nl, '-');
    }
}

/* Print the lines between BEG and LIM.  Deal with context crap.
   If NLINESP is non-null, store a count of lines between BEG and LIM. */
static void
prtext(beg, lim, nlinesp)
     char *beg;
     char *lim;
     int *nlinesp;
{
  static int used;		/* avoid printing "--" before any output */
  char *bp, *p, *nl;
  int i, n;

  if (_br(!out_quiet && pending > 0,0x46ba8e8,439,7))
    prpending(beg);

  p = beg;

  if (_br(!out_quiet,0x46baac8,444,7))
    {
      /* Deal with leading context crap. */

      bp = lastout ? lastout : bufbeg;
      for (i = 0; _br(i < out_before,0x46bad18,449,19); ++i)
	if (_br(p > bp,0x46bae08,450,6))
	  do
	    --p;
	  while (p > bp && p[-1] != '\n');

      /* We only print the "--" separator if our output is
	 discontiguous from the last output in the file. */
      if (_br((out_before || out_after) && used && p != lastout,0x46bb2c8,457,11))
	puts("--");

      while (_br(p < beg,0x46bb6b8,460,14))
	{
	  nl = memchr(p, '\n', beg - p);
	  prline(p, nl + 1, '-');
	  p = nl + 1;
	}
    }

  if (_br(nlinesp,0x46bbc30,468,7))
    {
      /* Caller wants a line count. */
      for (n = 0; _br(p < lim,0x46bbd38,471,19); ++n)
	{
	  if (_br((nl = memchr(p, '\n', lim - p)) != 0,0x46bc030,473,8))
	    ++nl;
	  else
	    nl = lim;
	  if (_br(!out_quiet,0x46bc1a8,477,8))
	    prline(p, nl, ':');
	  p = nl;
	}
      *nlinesp = n;
    }
  else
    if (_br(!out_quiet,0x46bc540,484,9))
      prline(beg, lim, ':');

  pending = out_after;
  used = 1;
}

/* Scan the specified portion of the buffer, matching lines (or
   between matching lines if OUT_INVERT is true).  Return a count of
   lines printed. */
static int
grepbuf(beg, lim)
     char *beg;
     char *lim;
{
  int nlines, n;
  register char *p, *b;
  char *endp;

  nlines = 0;
  p = beg;
  while (_br((b = (*execute)(p, lim - p, &endp)) != 0,0x46bd110,505,10))
    {
      /* Avoid matching the empty line at the end of the buffer. */
      if (_br(b == lim && ((b > beg && b[-1] == '\n') || b == beg),0x46bd4d8,508,11))
	break;
      if (_br(!out_invert,0x46bd580,510,11))
	{
	  prtext(b, endp, (int *) 0);
	  nlines += 1;
	}
      else if (_br(p < b,0x46be210,515,16))
	{
	  prtext(p, b, &n);
	  nlines += n;
	}
      p = endp;
    }
  if (_br(out_invert && p < lim,0x46be680,522,7))
    {
      prtext(p, lim, &n);
      nlines += n;
    }
  return nlines;
}

/* Search a given file.  Return a count of lines printed. */
static int
grep(fd)
     int fd;
{
  int nlines, i;
  size_t residue, save;
  char *beg, *lim;

  reset(fd);

  totalcc = 0;
  lastout = 0;
  totalnl = 0;
  pending = 0;

  nlines = 0;
  residue = 0;
  save = 0;

  for (;;)
    {
      if (_br(fillbuf(save) < 0,0x46bf938,552,11))
	{
	  error(filename, errno);
	  return nlines;
	}
      lastnl = bufbeg;
      if (_br(lastout,0x46bfc60,558,11))
	lastout = bufbeg;
      if (_br(buflim - bufbeg == save,0x46bfe40,560,11))
	break;
      beg = bufbeg + save - residue;
      for (lim = buflim; _br(lim > beg && lim[-1] != '\n',0x46c0258,563,26); --lim)
	;
      residue = buflim - lim;
      if (_br(beg < lim,0x46c04a0,566,11))
	{
	  nlines += grepbuf(beg, lim);
	  if (_br(pending,0x46c0670,569,8))
	    prpending(lim);
	}
      i = 0;
      beg = lim;
      while (_br(i < out_before && beg > bufbeg && beg != lastout,0x46c0af0,574,14))
	{
	  ++i;
	  do
	    --beg;
	  while (beg > bufbeg && beg[-1] != '\n');
	}
      if (_br(beg != lastout,0x46c0e78,581,11))
	lastout = 0;
      save = residue + lim - beg;
      totalcc += buflim - bufbeg - save;
      if (_br(out_line,0x46c1288,585,11))
	nlscan(beg);
    }
  if (_br(residue,0x46c1478,588,7))
    {
      nlines += grepbuf(bufbeg + save - residue, buflim);
      if (_br(pending,0x46c1700,591,11))
	prpending(buflim);
    }
  return nlines;
}

static char version[] = "GNU grep version 2.0";

#define USAGE \
  "usage: %s [-[[AB] ]<num>] [-[CEFGVchilnqsvwx]] [-[ef]] <expr> [<files...>]\n"

static void
usage()
{
  fprintf(stderr, USAGE, prog);
  exit(2);
}

/* Go through the matchers vector and look for the specified matcher.
   If we find it, install it in compile and execute, and return 1.  */
int
setmatcher(name)
     char *name;
{
  int i;

  for (i = 0; _br(matchers[i].name,0x46c20f8,617,15); ++i)
    if (_br(strcmp(name, matchers[i].name) == 0,0x46c23b8,618,9))
      {
	compile = matchers[i].compile;
	execute = matchers[i].execute;
	return 1;
      }
  return 0;
}  

int
main(argc, argv)
     int argc;
     char *argv[];
{
  char *keys;
  size_t keycc, oldcc, keyalloc;
  int keyfound, count_matches, no_filenames, list_files, suppress_errors;
  int opt, cc, desc, count, status;
  FILE *fp;
  extern char *optarg;
  extern int optind;
  /* add this line for diff between *.c and *.int.c */
  argv[0] = "target";

  prog = argv[0];
  if (_br(prog && strrchr(prog, '/'),0x46c52d0,643,7))
    prog = strrchr(prog, '/') + 1;

  keys = NULL;
  keycc = 0;
  keyfound = 0;
  count_matches = 0;
  no_filenames = 0;
  list_files = 0;
  suppress_errors = 0;
  matcher = NULL;

  while ((opt = getopt(argc, argv, "0123456789A:B:CEFGVX:bce:f:hiLlnqsvwxy"))
	 != EOF)
    switch (opt)
      {
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
	out_before = 10 * out_before + opt - '0';
	out_after = 10 * out_after + opt - '0';
	break;
      case 'A':
	out_after = atoi(optarg);
	if (_br(out_after < 0,0x46c64d0,674,6))
	  usage();
	break;
      case 'B':
	out_before = atoi(optarg);
	if (_br(out_before < 0,0x46c6768,679,6))
	  usage();
	break;
      case 'C':
	out_before = out_after = 2;
	break;
      case 'E':
	if (_br(matcher && strcmp(matcher, "egrep") != 0,0x46c6b88,686,6))
	  fatal("you may specify only one of -E, -F, or -G", 0);
	matcher = "posix-egrep";
	break;
      case 'F':
	if (_br(matcher && strcmp(matcher, "fgrep") != 0,0x46c7028,691,6))
	  fatal("you may specify only one of -E, -F, or -G", 0);;
	matcher = "fgrep";
	break;
      case 'G':
	if (_br(matcher && strcmp(matcher, "grep") != 0,0x46c74a0,696,6))
	  fatal("you may specify only one of -E, -F, or -G", 0);
	matcher = "grep";
	break;
      case 'V':
	fprintf(stderr, "%s\n", version);
	break;
      case 'X':
	if (_br(matcher,0x46c78f0,704,6))
	  fatal("matcher already specified", 0);
	matcher = optarg;
	break;
      case 'b':
	out_byte = 1;
	break;
      case 'c':
	out_quiet = 1;
	count_matches = 1;
	break;
      case 'e':
	cc = strlen(optarg);
	keys = xrealloc(keys, keycc + cc + 1);
	if (_br(keyfound,0x46c8160,718,6))
	  keys[keycc++] = '\n';
	strcpy(&keys[keycc], optarg);
	keycc += cc;
	keyfound = 1;
	break;
      case 'f':
	fp = strcmp(optarg, "-") != 0 ? fopen(optarg, "r") : stdin;
	if (_br(!fp,0x46c8a80,726,6))
	  fatal(optarg, errno);
	for (keyalloc = 1; _br(keyalloc <= keycc,0x46c8d70,728,21); keyalloc *= 2)
	  ;
	keys = xrealloc(keys, keyalloc);
	oldcc = keycc;
	if (_br(keyfound,0x46c9078,732,6))
	  keys[keycc++] = '\n';
	while (_br(!feof(fp)
	       && (cc = fread(keys + keycc, 1, keyalloc - keycc, fp)) > 0,0x46c9658,734,9))
	  {
	    keycc += cc;
	    if (_br(keycc == keyalloc,0x46c97b8,738,10))
	      keys = xrealloc(keys, keyalloc *= 2);
	  }
	if (fp != stdin)
	  fclose(fp);
	/* Nuke the final newline to avoid matching a null string. */
	if (_br(keycc - oldcc > 0 && keys[keycc - 1] == '\n',0x46c9e10,744,6))
	  --keycc;
	keyfound = 1;
	break;
      case 'h':
	no_filenames = 1;
	break;
      case 'i':
      case 'y':			/* For old-timers . . . */
	match_icase = 1;
	break;
      case 'L':
	/* Like -l, except list files that don't contain matches.
	   Inspired by the same option in Hume's gre. */
	out_quiet = 1;
	list_files = -1;
	break;
      case 'l':
	out_quiet = 1;
	list_files = 1;
	break;
      case 'n':
	out_line = 1;
	break;
      case 'q':
	out_quiet = 1;
	break;
      case 's':
	suppress_errors = 1;
	break;
      case 'v':
	out_invert = 1;
	break;
      case 'w':
	match_words = 1;
	break;
      case 'x':
	match_lines = 1;
	break;
      default:
	usage();
	break;
      }

  if (_br(!keyfound,0x46cabc0,788,7))
    if (_br(optind < argc,0x46cac60,789,9))
      {
	keys = argv[optind++];
	keycc = strlen(keys);
      }
    else
      usage();

  if (_br(!matcher,0x46cb010,797,7))
    matcher = prog;

  if (_br(!setmatcher(matcher) && !setmatcher("default"),0x46cb2c0,800,7))
    abort();

  (*compile)(keys, keycc);

  if (_br(argc - optind > 1 && !no_filenames,0x46cb628,805,7))
    out_file = 1;

  status = 1;

  if (_br(optind < argc,0x46cb7e8,810,7))
    while (_br(optind < argc,0x46cb890,811,12))
      {
	desc = strcmp(argv[optind], "-") ? open(argv[optind], O_RDONLY) : 0;
	if (_br(desc < 0,0x46cbd30,814,6))
	  {
	    if (_br(!suppress_errors,0x46cbd98,816,10))
	      error(argv[optind], errno);
	  }
	else
	  {
	    filename = desc == 0 ? "(standard input)" : argv[optind];
	    count = grep(desc);
	    if (_br(count_matches,0x46cc380,823,10))
	      {
		if (_br(out_file,0x46cc3c0,825,7))
		  printf("%s:", filename);
		printf("%d\n", count);
	      }
	    if (_br(count,0x46cc6c8,829,10))
	      {
		status = 0;
		if (_br(list_files == 1,0x46cc7b0,832,7))
		  printf("%s\n", filename);
	      }
	    else if (_br(list_files == -1,0x46cc9d0,835,15))
	      printf("%s\n", filename);
	  }
	if (_br(desc != 0,0x46ccc50,838,6))
	  close(desc);
	++optind;
      }
  else
    {
      filename = "(standard input)";
      count = grep(0);
      if (_br(count_matches,0x46ccfd8,846,11))
	printf("%d\n", count);
      if (_br(count,0x46cd348,848,11))
	{
	  status = 0;
	  if (_br(list_files == 1,0x46cd430,851,8))
	    printf("(standard input)\n");
	}
      else if (_br(list_files == -1,0x46cd650,854,16))
	printf("(standard input)\n");
    }

  exit(errseen ? 2 : status);
}
/* Getopt for GNU.
   NOTE: getopt is now part of the C library, so if you don't know what
   "Keep this file name-space clean" means, talk to roland@gnu.ai.mit.edu
   before changing it!

   Copyright (C) 1987, 88, 89, 90, 91, 92, 1993
   	Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 2, or (at your option) any
   later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */

/* NOTE!!!  AIX requires this to be the first thing in the file.
   Do not put ANYTHING before it!  */
#if !defined (__GNUC__) && defined (_AIX)
 #pragma alloca
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#define flag_config 1
#endif

#if (defined( __GNUC__) && (!defined(LINUX)))
#define alloca __builtin_alloca
#else /* not __GNUC__ */
#if defined (HAVE_ALLOCA_H) || (defined(sparc) && (defined(sun) || (!defined(USG) && !defined(SVR4) && !defined(__svr4__))))
#include <alloca.h>
#define flag_alloca 1
#else
#ifndef _AIX
char *alloca ();
#endif
#endif /* alloca.h */
#endif /* not __GNUC__ */

#if !__STDC__ && !defined(const) && IN_GCC
#define const
#endif

/* This tells Alpha OSF/1 not to define a getopt prototype in <stdio.h>.  */
#ifndef _NO_PROTO
#define _NO_PROTO
#endif

/*#include <stdio.h> */

/* Comment out all this code if we are using the GNU C Library, and are not
   actually compiling the library itself.  This code is part of the GNU C
   Library, but also included in many other GNU distributions.  Compiling
   and linking in this code is a waste when using the GNU C library
   (especially if it is a shared library).  Rather than having every GNU
   program understand `configure --with-gnu-libc' and omit the object files,
   it is simpler to just do this in the source for each such file.  */

#if defined (_LIBC) || !defined (__GNU_LIBRARY__)


/* This needs to come after some library #include
   to get __GNU_LIBRARY__ defined.  */
#ifdef	__GNU_LIBRARY__
#undef	alloca
/* Don't include stdlib.h for non-GNU C libraries because some of them
   contain conflicting prototypes for getopt.  */
#if flag_stdlib==0   
#include <stdlib.h>
#define flag_stdlib 1
#endif
#else	/* Not GNU C library.  */
#define	__alloca	alloca
#endif	/* GNU C library.  */

/* If GETOPT_COMPAT is defined, `+' as well as `--' can introduce a
   long-named option.  Because this is not POSIX.2 compliant, it is
   being phased out.  */
/* #define GETOPT_COMPAT */

/* This version of `getopt' appears to the caller like standard Unix `getopt'
   but it behaves differently for the user, since it allows the user
   to intersperse the options with the other arguments.

   As `getopt' works, it permutes the elements of ARGV so that,
   when it is done, all the options precede everything else.  Thus
   all application programs are extended to handle flexible argument order.

   Setting the environment variable POSIXLY_CORRECT disables permutation.
   Then the behavior is completely standard.

   GNU application programs can use a third alternative mode in which
   they can distinguish the relative order of options and other arguments.  */

#include "getopt.h"

/* For communication from `getopt' to the caller.
   When `getopt' finds an option that takes an argument,
   the argument value is returned here.
   Also, when `ordering' is RETURN_IN_ORDER,
   each non-option ARGV-element is returned here.  */

char *optarg = 0;

/* Index in ARGV of the next element to be scanned.
   This is used for communication to and from the caller
   and for communication between successive calls to `getopt'.

   On entry to `getopt', zero means this is the first call; initialize.

   When `getopt' returns EOF, this is the index of the first of the
   non-option elements that the caller should itself scan.

   Otherwise, `optind' communicates from one call to the next
   how much of ARGV has been scanned so far.  */

/* XXX 1003.2 says this must be 1 before any call.  */
int optind = 0;

/* The next char to be scanned in the option-element
   in which the last option character we returned was found.
   This allows us to pick up the scan where we left off.

   If this is zero, or a null string, it means resume the scan
   by advancing to the next ARGV-element.  */

static char *nextchar;

/* Callers store zero here to inhibit the error message
   for unrecognized options.  */

int opterr = 1;

/* Set to an option character which was unrecognized.
   This must be initialized on some systems to avoid linking in the
   system's own getopt implementation.  */

int optopt = '?';

/* Describe how to deal with options that follow non-option ARGV-elements.

   If the caller did not specify anything,
   the default is REQUIRE_ORDER if the environment variable
   POSIXLY_CORRECT is defined, PERMUTE otherwise.

   REQUIRE_ORDER means don't recognize them as options;
   stop option processing when the first non-option is seen.
   This is what Unix does.
   This mode of operation is selected by either setting the environment
   variable POSIXLY_CORRECT, or using `+' as the first character
   of the list of option characters.

   PERMUTE is the default.  We permute the contents of ARGV as we scan,
   so that eventually all the non-options are at the end.  This allows options
   to be given in any order, even with programs that were not written to
   expect this.

   RETURN_IN_ORDER is an option available to programs that were written
   to expect options and other ARGV-elements in any order and that care about
   the ordering of the two.  We describe each non-option ARGV-element
   as if it were the argument of an option with character code 1.
   Using `-' as the first character of the list of option characters
   selects this mode of operation.

   The special argument `--' forces an end of option-scanning regardless
   of the value of `ordering'.  In the case of RETURN_IN_ORDER, only
   `--' can cause `getopt' to return EOF with `optind' != ARGC.  */

static enum
{
  REQUIRE_ORDER, PERMUTE, RETURN_IN_ORDER
} ordering;

#ifdef	__GNU_LIBRARY__
/* We want to avoid inclusion of string.h with non-GNU libraries
   because there are many ways it can cause trouble.
   On some systems, it contains special magic macros that don't work
   in GCC.  */
#if flag_string==0   
#include <string.h>
#define flag_string 1
#endif
#define	my_index	strchr
#define	my_bcopy(src, dst, n)	memcpy ((dst), (src), (n))
#else

/* Avoid depending on library functions or files
   whose names are inconsistent.  */

char *getenv ();

static char *
my_index (str, chr)
     const char *str;
     int chr;
{
  while (*str)
    {
      if (*str == chr)
	return (char *) str;
      str++;
    }
  return 0;
}

static void
my_bcopy (from, to, size)
     const char *from;
     char *to;
     int size;
{
  int i;
  for (i = 0; i < size; i++)
    to[i] = from[i];
}
#endif				/* GNU C library.  */

/* Handle permutation of arguments.  */

/* Describe the part of ARGV that contains non-options that have
   been skipped.  `first_nonopt' is the index in ARGV of the first of them;
   `last_nonopt' is the index after the last of them.  */

static int first_nonopt;
static int last_nonopt;

/* Exchange two adjacent subsequences of ARGV.
   One subsequence is elements [first_nonopt,last_nonopt)
   which contains all the non-options that have been skipped so far.
   The other is elements [last_nonopt,optind), which contains all
   the options processed since those non-options were skipped.

   `first_nonopt' and `last_nonopt' are relocated so that they describe
   the new indices of the non-options in ARGV after they are moved.  */

static void
exchange (argv)
     char **argv;
{
  int nonopts_size = (last_nonopt - first_nonopt) * sizeof (char *);
  char **temp = (char **) __alloca (nonopts_size);

  /* Interchange the two blocks of data in ARGV.  */

  my_bcopy ((char *) &argv[first_nonopt], (char *) temp, nonopts_size);
  my_bcopy ((char *) &argv[last_nonopt], (char *) &argv[first_nonopt],
	    (optind - last_nonopt) * sizeof (char *));
  my_bcopy ((char *) temp,
	    (char *) &argv[first_nonopt + optind - last_nonopt],
	    nonopts_size);

  /* Update records for the slots the non-options now occupy.  */

  first_nonopt += (optind - last_nonopt);
  last_nonopt = optind;
}

/* Scan elements of ARGV (whose length is ARGC) for option characters
   given in OPTSTRING.

   If an element of ARGV starts with '-', and is not exactly "-" or "--",
   then it is an option element.  The characters of this element
   (aside from the initial '-') are option characters.  If `getopt'
   is called repeatedly, it returns successively each of the option characters
   from each of the option elements.

   If `getopt' finds another option character, it returns that character,
   updating `optind' and `nextchar' so that the next call to `getopt' can
   resume the scan with the following option character or ARGV-element.

   If there are no more option characters, `getopt' returns `EOF'.
   Then `optind' is the index in ARGV of the first ARGV-element
   that is not an option.  (The ARGV-elements have been permuted
   so that those that are not options now come last.)

   OPTSTRING is a string containing the legitimate option characters.
   If an option character is seen that is not listed in OPTSTRING,
   return '?' after printing an error message.  If you set `opterr' to
   zero, the error message is suppressed but we still return '?'.

   If a char in OPTSTRING is followed by a colon, that means it wants an arg,
   so the following text in the same ARGV-element, or the text of the following
   ARGV-element, is returned in `optarg'.  Two colons mean an option that
   wants an optional arg; if there is text in the current ARGV-element,
   it is returned in `optarg', otherwise `optarg' is set to zero.

   If OPTSTRING starts with `-' or `+', it requests different methods of
   handling the non-option ARGV-elements.
   See the comments about RETURN_IN_ORDER and REQUIRE_ORDER, above.

   Long-named options begin with `--' instead of `-'.
   Their names may be abbreviated as long as the abbreviation is unique
   or is an exact match for some defined option.  If they have an
   argument, it follows the option name in the same ARGV-element, separated
   from the option name by a `=', or else the in next ARGV-element.
   When `getopt' finds a long-named option, it returns 0 if that option's
   `flag' field is nonzero, the value of the option's `val' field
   if the `flag' field is zero.

   The elements of ARGV aren't really const, because we permute them.
   But we pretend they're const in the prototype to be compatible
   with other systems.

   LONGOPTS is a vector of `struct option' terminated by an
   element containing a name which is zero.

   LONGIND returns the index in LONGOPT of the long-named option found.
   It is only valid when a long-named option has been found by the most
   recent call.

   If LONG_ONLY is nonzero, '-' as well as '--' can introduce
   long-named options.  */

int
_getopt_internal (argc, argv, optstring, longopts, longind, long_only)
     int argc;
     char *const *argv;
     const char *optstring;
     const struct option *longopts;
     int *longind;
     int long_only;
{
  int option_index;

  optarg = 0;

  /* Initialize the internal data when the first call is made.
     Start processing options with ARGV-element 1 (since ARGV-element 0
     is the program name); the sequence of previously skipped
     non-option ARGV-elements is empty.  */

  if (optind == 0)
    {
      first_nonopt = last_nonopt = optind = 1;

      nextchar = NULL;

      /* Determine how to handle the ordering of options and nonoptions.  */

      if (optstring[0] == '-')
	{
	  ordering = RETURN_IN_ORDER;
	  ++optstring;
	}
      else if (optstring[0] == '+')
	{
	  ordering = REQUIRE_ORDER;
	  ++optstring;
	}
      else if (getenv ("POSIXLY_CORRECT") != NULL)
	ordering = REQUIRE_ORDER;
      else
	ordering = PERMUTE;
    }

  if (nextchar == NULL || *nextchar == '\0')
    {
      if (ordering == PERMUTE)
	{
	  /* If we have just processed some options following some non-options,
	     exchange them so that the options come first.  */

	  if (first_nonopt != last_nonopt && last_nonopt != optind)
	    exchange ((char **) argv);
	  else if (last_nonopt != optind)
	    first_nonopt = optind;

	  /* Now skip any additional non-options
	     and extend the range of non-options previously skipped.  */

	  while (optind < argc
		 && (argv[optind][0] != '-' || argv[optind][1] == '\0')
#ifdef GETOPT_COMPAT
		 && (longopts == NULL
		     || argv[optind][0] != '+' || argv[optind][1] == '\0')
#endif				/* GETOPT_COMPAT */
		 )
	    optind++;
	  last_nonopt = optind;
	}

      /* Special ARGV-element `--' means premature end of options.
	 Skip it like a null option,
	 then exchange with previous non-options as if it were an option,
	 then skip everything else like a non-option.  */

      if (optind != argc && !strcmp (argv[optind], "--"))
	{
	  optind++;

	  if (first_nonopt != last_nonopt && last_nonopt != optind)
	    exchange ((char **) argv);
	  else if (first_nonopt == last_nonopt)
	    first_nonopt = optind;
	  last_nonopt = argc;

	  optind = argc;
	}

      /* If we have done all the ARGV-elements, stop the scan
	 and back over any non-options that we skipped and permuted.  */

      if (optind == argc)
	{
	  /* Set the next-arg-index to point at the non-options
	     that we previously skipped, so the caller will digest them.  */
	  if (first_nonopt != last_nonopt)
	    optind = first_nonopt;
	  return EOF;
	}

      /* If we have come to a non-option and did not permute it,
	 either stop the scan or describe it to the caller and pass it by.  */

      if ((argv[optind][0] != '-' || argv[optind][1] == '\0')
#ifdef GETOPT_COMPAT
	  && (longopts == NULL
	      || argv[optind][0] != '+' || argv[optind][1] == '\0')
#endif				/* GETOPT_COMPAT */
	  )
	{
	  if (ordering == REQUIRE_ORDER)
	    return EOF;
	  optarg = argv[optind++];
	  return 1;
	}

      /* We have found another option-ARGV-element.
	 Start decoding its characters.  */

      nextchar = (argv[optind] + 1
		  + (longopts != NULL && argv[optind][1] == '-'));
    }

  if (longopts != NULL
      && ((argv[optind][0] == '-'
	   && (argv[optind][1] == '-' || long_only))
#ifdef GETOPT_COMPAT
	  || argv[optind][0] == '+'
#endif				/* GETOPT_COMPAT */
	  ))
    {
      const struct option *p;
      char *s = nextchar;
      int exact = 0;
      int ambig = 0;
      const struct option *pfound = NULL;
      int indfound;

      while (*s && *s != '=')
	s++;

      /* Test all options for either exact match or abbreviated matches.  */
      for (p = longopts, option_index = 0; p->name;
	   p++, option_index++)
	if (!strncmp (p->name, nextchar, s - nextchar))
	  {
	    if (s - nextchar == strlen (p->name))
	      {
		/* Exact match found.  */
		pfound = p;
		indfound = option_index;
		exact = 1;
		break;
	      }
	    else if (pfound == NULL)
	      {
		/* First nonexact match found.  */
		pfound = p;
		indfound = option_index;
	      }
	    else
	      /* Second nonexact match found.  */
	      ambig = 1;
	  }

      if (ambig && !exact)
	{
	  if (opterr)
	    fprintf (stderr, "%s: option `%s' is ambiguous\n",
		     argv[0], argv[optind]);
	  nextchar += strlen (nextchar);
	  optind++;
	  return '?';
	}

      if (pfound != NULL)
	{
	  option_index = indfound;
	  optind++;
	  if (*s)
	    {
	      /* Don't test has_arg with >, because some C compilers don't
		 allow it to be used on enums.  */
	      if (pfound->has_arg)
		optarg = s + 1;
	      else
		{
		  if (opterr)
		    {
		      if (argv[optind - 1][1] == '-')
			/* --option */
			fprintf (stderr,
				 "%s: option `--%s' doesn't allow an argument\n",
				 argv[0], pfound->name);
		      else
			/* +option or -option */
			fprintf (stderr,
			     "%s: option `%c%s' doesn't allow an argument\n",
			     argv[0], argv[optind - 1][0], pfound->name);
		    }
		  nextchar += strlen (nextchar);
		  return '?';
		}
	    }
	  else if (pfound->has_arg == 1)
	    {
	      if (optind < argc)
		optarg = argv[optind++];
	      else
		{
		  if (opterr)
		    fprintf (stderr, "%s: option `%s' requires an argument\n",
			     argv[0], argv[optind - 1]);
		  nextchar += strlen (nextchar);
		  return optstring[0] == ':' ? ':' : '?';
		}
	    }
	  nextchar += strlen (nextchar);
	  if (longind != NULL)
	    *longind = option_index;
	  if (pfound->flag)
	    {
	      *(pfound->flag) = pfound->val;
	      return 0;
	    }
	  return pfound->val;
	}
      /* Can't find it as a long option.  If this is not getopt_long_only,
	 or the option starts with '--' or is not a valid short
	 option, then it's an error.
	 Otherwise interpret it as a short option.  */
      if (!long_only || argv[optind][1] == '-'
#ifdef GETOPT_COMPAT
	  || argv[optind][0] == '+'
#endif				/* GETOPT_COMPAT */
	  || my_index (optstring, *nextchar) == NULL)
	{
	  if (opterr)
	    {
	      if (argv[optind][1] == '-')
		/* --option */
		fprintf (stderr, "%s: unrecognized option `--%s'\n",
			 argv[0], nextchar);
	      else
		/* +option or -option */
		fprintf (stderr, "%s: unrecognized option `%c%s'\n",
			 argv[0], argv[optind][0], nextchar);
	    }
	  nextchar = (char *) "";
	  optind++;
	  return '?';
	}
    }

  /* Look at and handle the next option-character.  */

  {
    char c = *nextchar++;
    char *temp = my_index (optstring, c);

    /* Increment `optind' when we start to process its last character.  */
    if (*nextchar == '\0')
      ++optind;

    if (temp == NULL || c == ':')
      {
	if (opterr)
	  {
#if 0
	    if (c < 040 || c >= 0177)
	      fprintf (stderr, "%s: unrecognized option, character code 0%o\n",
		       argv[0], c);
	    else
	      fprintf (stderr, "%s: unrecognized option `-%c'\n", argv[0], c);
#else
	    /* 1003.2 specifies the format of this message.  */
	    fprintf (stderr, "%s: illegal option -- %c\n", argv[0], c);
#endif
	  }
	optopt = c;
	return '?';
      }
    if (temp[1] == ':')
      {
	if (temp[2] == ':')
	  {
	    /* This is an option that accepts an argument optionally.  */
	    if (*nextchar != '\0')
	      {
		optarg = nextchar;
		optind++;
	      }
	    else
	      optarg = 0;
	    nextchar = NULL;
	  }
	else
	  {
	    /* This is an option that requires an argument.  */
	    if (*nextchar != '\0')
	      {
		optarg = nextchar;
		/* If we end this ARGV-element by taking the rest as an arg,
		   we must advance to the next element now.  */
		optind++;
	      }
	    else if (optind == argc)
	      {
		if (opterr)
		  {
#if 0
		    fprintf (stderr, "%s: option `-%c' requires an argument\n",
			     argv[0], c);
#else
		    /* 1003.2 specifies the format of this message.  */
		    fprintf (stderr, "%s: option requires an argument -- %c\n",
			     argv[0], c);
#endif
		  }
		optopt = c;
		if (optstring[0] == ':')
		  c = ':';
		else
		  c = '?';
	      }
	    else
	      /* We already incremented `optind' once;
		 increment it again when taking next ARGV-elt as argument.  */
	      optarg = argv[optind++];
	    nextchar = NULL;
	  }
      }
    return c;
  }
}

int
getopt (argc, argv, optstring)
     int argc;
     char *const *argv;
     const char *optstring;
{
  return _getopt_internal (argc, argv, optstring,
			   (const struct option *) 0,
			   (int *) 0,
			   0);
}

#endif	/* _LIBC or not __GNU_LIBRARY__.  */

#ifdef TEST

/* Compile with -DTEST to make an executable for use in testing
   the above definition of `getopt'.  */

int
main (argc, argv)
     int argc;
     char **argv;
{
  int c;
  int digit_optind = 0;

  while (1)
    {
      int this_option_optind = optind ? optind : 1;

      c = getopt (argc, argv, "abc:d:0123456789");
      if (c == EOF)
	break;

      switch (c)
	{
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
	  if (digit_optind != 0 && digit_optind != this_option_optind)
	    printf ("digits occur in two different argv-elements.\n");
	  digit_optind = this_option_optind;
	  printf ("option %c\n", c);
	  break;

	case 'a':
	  printf ("option a\n");
	  break;

	case 'b':
	  printf ("option b\n");
	  break;

	case 'c':
	  printf ("option c with value `%s'\n", optarg);
	  break;

	case '?':
	  break;

	default:
	  printf ("?? getopt returned character code 0%o ??\n", c);
	}
    }

  if (optind < argc)
    {
      printf ("non-option ARGV-elements: ");
      while (optind < argc)
	printf ("%s ", argv[optind++]);
      printf ("\n");
    }

  exit (0);
}

#endif /* TEST */
/* Extended regular expression matching and search library,
   version 0.12.
   (Implements POSIX draft P10003.2/D11.2, except for
   internationalization features.)

   Copyright (C) 1993 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

/* AIX requires this to be the first thing in the file. */
#if defined (_AIX) && !defined (REGEX_MALLOC)
  #pragma alloca
#endif

#define _GNU_SOURCE

/* We need this for `regex.h', and perhaps for the Emacs include files.  */
#if flag_systypes==0
#include <sys/types.h>
#define flag_systypes 1
#endif

#ifdef HAVE_CONFIG_H
#if flag_config==0
#include "config.h"
#define flag_config 1
#endif
#endif

/* The `emacs' switch turns on certain matching commands
   that make sense only in Emacs. */
#ifdef emacs

#include "lisp.h"
#include "buffer.h"
#include "syntax.h"

/* Emacs uses `NULL' as a predicate.  */
#undef NULL

#else  /* not emacs */

/* We used to test for `BSTRING' here, but only GCC and Emacs define
   `BSTRING', as far as I know, and neither of them use this code.  */
#if HAVE_STRING_H || STDC_HEADERS
#if flag_string==0
#include <string.h>
#define flag_string 1
#endif
#ifndef bcmp
#define bcmp(s1, s2, n)	memcmp ((s1), (s2), (n))
#endif
#ifndef bcopy
#define bcopy(s, d, n)	memcpy ((d), (s), (n))
#endif
#ifndef bzero
#define bzero(s, n)	memset ((s), 0, (n))
#endif
#else
#if flag_strings==0
#include <strings.h>
#define flag_strings 1
#endif
#endif

#ifdef STDC_HEADERS
#if flag_stdlib==0 
#include <stdlib.h>
#define flag_stdlib 1
#endif
#else
char *malloc ();
char *realloc ();
#endif


/* Define the syntax stuff for \<, \>, etc.  */

/* This must be nonzero for the wordchar and notwordchar pattern
   commands in re_match_2.  */
#ifndef Sword 
#define Sword 1
#endif

#ifdef SYNTAX_TABLE

extern char *re_syntax_table;

#else /* not SYNTAX_TABLE */

/* How many characters in the character set.  */
#define CHAR_SET_SIZE 256

static char re_syntax_table[CHAR_SET_SIZE];

static void
init_syntax_once ()
{
   register int c;
   static int done = 0;

   if (_br(done,0x46cdd68,1712,8))
     return;

   bzero (re_syntax_table, sizeof re_syntax_table);

   for (c = 'a'; _br(c <= 'z',0x46ce050,1717,18); c++)
     re_syntax_table[c] = Sword;

   for (c = 'A'; _br(c <= 'Z',0x46ce2c0,1720,18); c++)
     re_syntax_table[c] = Sword;

   for (c = '0'; _br(c <= '9',0x46d0ba0,1723,18); c++)
     re_syntax_table[c] = Sword;

   re_syntax_table['_'] = Sword;

   done = 1;
}

#endif /* not SYNTAX_TABLE */

#define SYNTAX(c) re_syntax_table[c]

#endif /* not emacs */

/* Get the interface, including the syntax bits.  */
#include "regex.h"

/* isalpha etc. are used for the character classes.  */
#include <ctype.h>

/* Jim Meyering writes:

   "... Some ctype macros are valid only for character codes that
   isascii says are ASCII (SGI's IRIX-4.0.5 is one such system --when
   using /bin/cc or gcc but without giving an ansi option).  So, all
   ctype uses should be through macros like ISPRINT...  If
   STDC_HEADERS is defined, then autoconf has verified that the ctype
   macros don't need to be guarded with references to isascii. ...
   Defining isascii to 1 should let any compiler worth its salt
   eliminate the && through constant folding."  */
#if ! defined (isascii) || defined (STDC_HEADERS)
#undef isascii
#define isascii(c) 1
#endif

#ifdef isblank
#define ISBLANK(c) (isascii (c) && isblank (c))
#else
#define ISBLANK(c) ((c) == ' ' || (c) == '\t')
#endif
#ifdef isgraph
#define ISGRAPH(c) (isascii (c) && isgraph (c))
#else
#define ISGRAPH(c) (isascii (c) && isprint (c) && !isspace (c))
#endif

#define ISPRINT(c) (isascii (c) && isprint (c))
#define ISDIGIT(c) (isascii (c) && isdigit (c))
#define ISALNUM(c) (isascii (c) && isalnum (c))
#define ISALPHA(c) (isascii (c) && isalpha (c))
#define ISCNTRL(c) (isascii (c) && iscntrl (c))
#define ISLOWER(c) (isascii (c) && islower (c))
#define ISPUNCT(c) (isascii (c) && ispunct (c))
#define ISSPACE(c) (isascii (c) && isspace (c))
#define ISUPPER(c) (isascii (c) && isupper (c))
#define ISXDIGIT(c) (isascii (c) && isxdigit (c))

#ifndef NULL
#define NULL 0
#endif

/* We remove any previous definition of `SIGN_EXTEND_CHAR',
   since ours (we hope) works properly with all combinations of
   machines, compilers, `char' and `unsigned char' argument types.
   (Per Bothner suggested the basic approach.)  */
#undef SIGN_EXTEND_CHAR
#if __STDC__
#define SIGN_EXTEND_CHAR(c) ((signed char) (c))
#else  /* not __STDC__ */
/* As in Harbison and Steele.  */
#define SIGN_EXTEND_CHAR(c) ((((unsigned char) (c)) ^ 128) - 128)
#endif

/* Should we use malloc or alloca?  If REGEX_MALLOC is not defined, we
   use `alloca' instead of `malloc'.  This is because using malloc in
   re_search* or re_match* could cause memory leaks when C-g is used in
   Emacs; also, malloc is slower and causes storage fragmentation.  On
   the other hand, malloc is more portable, and easier to debug.  
   
   Because we sometimes use alloca, some routines have to be macros,
   not functions -- `alloca'-allocated space disappears at the end of the
   function it is called in.  */

#ifdef REGEX_MALLOC

#define REGEX_ALLOCATE malloc
#define REGEX_REALLOCATE(source, osize, nsize) realloc (source, nsize)

#else /* not REGEX_MALLOC  */

/* Emacs already defines alloca, sometimes.  */
#ifndef alloca

/* Make alloca work the best possible way.  */
#ifdef __GNUC__
#define alloca __builtin_alloca
#else /* not __GNUC__ */
#if HAVE_ALLOCA_H
#if flag_alloca==0
#include <alloca.h>
#define flag_alloca 1
#endif 
#else /* not __GNUC__ or HAVE_ALLOCA_H */
#ifndef _AIX /* Already did AIX, up at the top.  */
char *alloca ();
#endif /* not _AIX */
#endif /* not HAVE_ALLOCA_H */ 
#endif /* not __GNUC__ */

#endif /* not alloca */

#define REGEX_ALLOCATE alloca

/* Assumes a `char *destination' variable.  */
#define REGEX_REALLOCATE(source, osize, nsize)				\
  (destination = (char *) alloca (nsize),				\
   bcopy (source, destination, osize),					\
   destination)

#endif /* not REGEX_MALLOC */


/* True if `size1' is non-NULL and PTR is pointing anywhere inside
   `string1' or just past its end.  This works if PTR is NULL, which is
   a good thing.  */
#define FIRST_STRING_P(ptr) 					\
  (size1 && string1 <= (ptr) && (ptr) <= string1 + size1)

/* (Re)Allocate N items of type T using malloc, or fail.  */
#define TALLOC(n, t) ((t *) malloc ((n) * sizeof (t)))
#define RETALLOC(addr, n, t) ((addr) = (t *) realloc (addr, (n) * sizeof (t)))
#define REGEX_TALLOC(n, t) ((t *) REGEX_ALLOCATE ((n) * sizeof (t)))

#define BYTEWIDTH 8 /* In bits.  */

#define STREQ(s1, s2) ((strcmp (s1, s2) == 0))

#undef MAX               /****** added later *******/
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

typedef char boolean;
#define false 0
#define true 1

/* These are the command codes that appear in compiled regular
   expressions.  Some opcodes are followed by argument bytes.  A
   command code can specify any interpretation whatsoever for its
   arguments.  Zero bytes may appear in the compiled regular expression.

   The value of `exactn' is needed in search.c (search_buffer) in Emacs.
   So regex.h defines a symbol `RE_EXACTN_VALUE' to be 1; the value of
   `exactn' we use here must also be 1.  */

typedef enum
{
  no_op = 0,

        /* Followed by one byte giving n, then by n literal bytes.  */
  exactn = 1,

        /* Matches any (more or less) character.  */
  anychar,

        /* Matches any one char belonging to specified set.  First
           following byte is number of bitmap bytes.  Then come bytes
           for a bitmap saying which chars are in.  Bits in each byte
           are ordered low-bit-first.  A character is in the set if its
           bit is 1.  A character too large to have a bit in the map is
           automatically not in the set.  */
  charset,

        /* Same parameters as charset, but match any character that is
           not one of those specified.  */
  charset_not,

        /* Start remembering the text that is matched, for storing in a
           register.  Followed by one byte with the register number, in
           the range 0 to one less than the pattern buffer's re_nsub
           field.  Then followed by one byte with the number of groups
           inner to this one.  (This last has to be part of the
           start_memory only because we need it in the on_failure_jump
           of re_match_2.)  */
  start_memory,

        /* Stop remembering the text that is matched and store it in a
           memory register.  Followed by one byte with the register
           number, in the range 0 to one less than `re_nsub' in the
           pattern buffer, and one byte with the number of inner groups,
           just like `start_memory'.  (We need the number of inner
           groups here because we don't have any easy way of finding the
           corresponding start_memory when we're at a stop_memory.)  */
  stop_memory,

        /* Match a duplicate of something remembered. Followed by one
           byte containing the register number.  */
  duplicate,

        /* Fail unless at beginning of line.  */
  begline,

        /* Fail unless at end of line.  */
  endline,

        /* Succeeds if at beginning of buffer (if emacs) or at beginning
           of string to be matched (if not).  */
  begbuf,

        /* Analogously, for end of buffer/string.  */
  endbuf,
 
        /* Followed by two byte relative address to which to jump.  */
  jump, 

	/* Same as jump, but marks the end of an alternative.  */
  jump_past_alt,

        /* Followed by two-byte relative address of place to resume at
           in case of failure.  */
  on_failure_jump,
	
        /* Like on_failure_jump, but pushes a placeholder instead of the
           current string position when executed.  */
  on_failure_keep_string_jump,
  
        /* Throw away latest failure point and then jump to following
           two-byte relative address.  */
  pop_failure_jump,

        /* Change to pop_failure_jump if know won't have to backtrack to
           match; otherwise change to jump.  This is used to jump
           back to the beginning of a repeat.  If what follows this jump
           clearly won't match what the repeat does, such that we can be
           sure that there is no use backtracking out of repetitions
           already matched, then we change it to a pop_failure_jump.
           Followed by two-byte address.  */
  maybe_pop_jump,

        /* Jump to following two-byte address, and push a dummy failure
           point. This failure point will be thrown away if an attempt
           is made to use it for a failure.  A `+' construct makes this
           before the first repeat.  Also used as an intermediary kind
           of jump when compiling an alternative.  */
  dummy_failure_jump,

	/* Push a dummy failure point and continue.  Used at the end of
	   alternatives.  */
  push_dummy_failure,

        /* Followed by two-byte relative address and two-byte number n.
           After matching N times, jump to the address upon failure.  */
  succeed_n,

        /* Followed by two-byte relative address, and two-byte number n.
           Jump to the address N times, then fail.  */
  jump_n,

        /* Set the following two-byte relative address to the
           subsequent two-byte number.  The address *includes* the two
           bytes of number.  */
  set_number_at,

  wordchar,	/* Matches any word-constituent character.  */
  notwordchar,	/* Matches any char that is not a word-constituent.  */

  wordbeg,	/* Succeeds if at word beginning.  */
  wordend,	/* Succeeds if at word end.  */

  wordbound,	/* Succeeds if at a word boundary.  */
  notwordbound	/* Succeeds if not at a word boundary.  */

#ifdef emacs
  ,before_dot,	/* Succeeds if before point.  */
  at_dot,	/* Succeeds if at point.  */
  after_dot,	/* Succeeds if after point.  */

	/* Matches any character whose syntax is specified.  Followed by
           a byte which contains a syntax code, e.g., Sword.  */
  syntaxspec,

	/* Matches any character whose syntax is not that specified.  */
  notsyntaxspec
#endif /* emacs */
} re_opcode_t;

/* Common operations on the compiled pattern.  */

/* Store NUMBER in two contiguous bytes starting at DESTINATION.  */

#define STORE_NUMBER(destination, number)				\
  do {									\
    (destination)[0] = (number) & 0377;					\
    (destination)[1] = (number) >> 8;					\
  } while (0)

/* Same as STORE_NUMBER, except increment DESTINATION to
   the byte after where the number is stored.  Therefore, DESTINATION
   must be an lvalue.  */

#define STORE_NUMBER_AND_INCR(destination, number)			\
  do {									\
    STORE_NUMBER (destination, number);					\
    (destination) += 2;							\
  } while (0)

/* Put into DESTINATION a number stored in two contiguous bytes starting
   at SOURCE.  */

#define EXTRACT_NUMBER(destination, source)				\
  do {									\
    (destination) = *(source) & 0377;					\
    (destination) += SIGN_EXTEND_CHAR (*((source) + 1)) << 8;		\
  } while (0)

#ifdef DEBUG
static void
extract_number (dest, source)
    int *dest;
    unsigned char *source;
{
  int temp = SIGN_EXTEND_CHAR (*(source + 1)); 
  *dest = *source & 0377;
  *dest += temp << 8;
}

#ifndef EXTRACT_MACROS /* To debug the macros.  */
#undef EXTRACT_NUMBER
#define EXTRACT_NUMBER(dest, src) extract_number (&dest, src)
#endif /* not EXTRACT_MACROS */

#endif /* DEBUG */

/* Same as EXTRACT_NUMBER, except increment SOURCE to after the number.
   SOURCE must be an lvalue.  */

#define EXTRACT_NUMBER_AND_INCR(destination, source)			\
  do {									\
    EXTRACT_NUMBER (destination, source);				\
    (source) += 2; 							\
  } while (0)

#ifdef DEBUG
static void
extract_number_and_incr (destination, source)
    int *destination;
    unsigned char **source;
{ 
  extract_number (destination, *source);
  *source += 2;
}

#ifndef EXTRACT_MACROS
#undef EXTRACT_NUMBER_AND_INCR
#define EXTRACT_NUMBER_AND_INCR(dest, src) \
  extract_number_and_incr (&dest, &src)
#endif /* not EXTRACT_MACROS */

#endif /* DEBUG */

/* If DEBUG is defined, Regex prints many voluminous messages about what
   it is doing (if the variable `debug' is nonzero).  If linked with the
   main program in `iregex.c', you can enter patterns and strings
   interactively.  And if linked with the main program in `main.c' and
   the other test files, you can run the already-written tests.  */

#ifdef DEBUG

/* We use standard I/O for debugging.  */
/*#include <stdio.h>*/

/* It is useful to test things that ``must'' be true when debugging.  */
#include <assert.h>

static int debug = 0;

#define DEBUG_STATEMENT(e) e
#define DEBUG_PRINT1(x) if (debug) printf (x)
#define DEBUG_PRINT2(x1, x2) if (debug) printf (x1, x2)
#define DEBUG_PRINT3(x1, x2, x3) if (debug) printf (x1, x2, x3)
#define DEBUG_PRINT4(x1, x2, x3, x4) if (debug) printf (x1, x2, x3, x4)
#define DEBUG_PRINT_COMPILED_PATTERN(p, s, e) 				\
  if (debug) print_partial_compiled_pattern (s, e)
#define DEBUG_PRINT_DOUBLE_STRING(w, s1, sz1, s2, sz2)			\
  if (debug) print_double_string (w, s1, sz1, s2, sz2)


extern void printchar ();

/* Print the fastmap in human-readable form.  */

void
print_fastmap (fastmap)
    char *fastmap;
{
  unsigned was_a_range = 0;
  unsigned i = 0;  
  
  while (i < (1 << BYTEWIDTH))
    {
      if (fastmap[i++])
	{
	  was_a_range = 0;
          printchar (i - 1);
          while (i < (1 << BYTEWIDTH)  &&  fastmap[i])
            {
              was_a_range = 1;
              i++;
            }
	  if (was_a_range)
            {
              printf ("-");
              printchar (i - 1);
            }
        }
    }
  putchar ('\n'); 
}


/* Print a compiled pattern string in human-readable form, starting at
   the START pointer into it and ending just before the pointer END.  */

void
print_partial_compiled_pattern (start, end)
    unsigned char *start;
    unsigned char *end;
{
  int mcnt, mcnt2;
  unsigned char *p = start;
  unsigned char *pend = end;

  if (start == NULL)
    {
      printf ("(null)\n");
      return;
    }
    
  /* Loop over pattern commands.  */
  while (p < pend)
    {
      printf ("%d:\t", p - start);

      switch ((re_opcode_t) *p++)
	{
        case no_op:
          printf ("/no_op");
          break;

	case exactn:
	  mcnt = *p++;
          printf ("/exactn/%d", mcnt);
          do
	    {
              putchar ('/');
	      printchar (*p++);
            }
          while (--mcnt);
          break;

	case start_memory:
          mcnt = *p++;
          printf ("/start_memory/%d/%d", mcnt, *p++);
          break;

	case stop_memory:
          mcnt = *p++;
	  printf ("/stop_memory/%d/%d", mcnt, *p++);
          break;

	case duplicate:
	  printf ("/duplicate/%d", *p++);
	  break;

	case anychar:
	  printf ("/anychar");
	  break;

	case charset:
        case charset_not:
          {
            register int c, last = -100;
	    register int in_range = 0;

	    printf ("/charset [%s",
	            (re_opcode_t) *(p - 1) == charset_not ? "^" : "");
            
            assert (p + *p < pend);

            for (c = 0; c < 256; c++)
	      if (c / 8 < *p
		  && (p[1 + (c/8)] & (1 << (c % 8))))
		{
		  /* Are we starting a range?  */
		  if (last + 1 == c && ! in_range)
		    {
		      putchar ('-');
		      in_range = 1;
		    }
		  /* Have we broken a range?  */
		  else if (last + 1 != c && in_range)
              {
		      printchar (last);
		      in_range = 0;
		    }
                
		  if (! in_range)
		    printchar (c);

		  last = c;
              }

	    if (in_range)
	      printchar (last);

	    putchar (']');

	    p += 1 + *p;
	  }
	  break;

	case begline:
	  printf ("/begline");
          break;

	case endline:
          printf ("/endline");
          break;

	case on_failure_jump:
          extract_number_and_incr (&mcnt, &p);
  	  printf ("/on_failure_jump to %d", p + mcnt - start);
          break;

	case on_failure_keep_string_jump:
          extract_number_and_incr (&mcnt, &p);
  	  printf ("/on_failure_keep_string_jump to %d", p + mcnt - start);
          break;

	case dummy_failure_jump:
          extract_number_and_incr (&mcnt, &p);
  	  printf ("/dummy_failure_jump to %d", p + mcnt - start);
          break;

	case push_dummy_failure:
          printf ("/push_dummy_failure");
          break;
          
        case maybe_pop_jump:
          extract_number_and_incr (&mcnt, &p);
  	  printf ("/maybe_pop_jump to %d", p + mcnt - start);
	  break;

        case pop_failure_jump:
	  extract_number_and_incr (&mcnt, &p);
  	  printf ("/pop_failure_jump to %d", p + mcnt - start);
	  break;          
          
        case jump_past_alt:
	  extract_number_and_incr (&mcnt, &p);
  	  printf ("/jump_past_alt to %d", p + mcnt - start);
	  break;          
          
        case jump:
	  extract_number_and_incr (&mcnt, &p);
  	  printf ("/jump to %d", p + mcnt - start);
	  break;

        case succeed_n: 
          extract_number_and_incr (&mcnt, &p);
          extract_number_and_incr (&mcnt2, &p);
	  printf ("/succeed_n to %d, %d times", p + mcnt - start, mcnt2);
          break;
        
        case jump_n: 
          extract_number_and_incr (&mcnt, &p);
          extract_number_and_incr (&mcnt2, &p);
	  printf ("/jump_n to %d, %d times", p + mcnt - start, mcnt2);
          break;
        
        case set_number_at: 
          extract_number_and_incr (&mcnt, &p);
          extract_number_and_incr (&mcnt2, &p);
	  printf ("/set_number_at location %d to %d", p + mcnt - start, mcnt2);
          break;
        
        case wordbound:
	  printf ("/wordbound");
	  break;

	case notwordbound:
	  printf ("/notwordbound");
          break;

	case wordbeg:
	  printf ("/wordbeg");
	  break;
          
	case wordend:
	  printf ("/wordend");
          
#ifdef emacs
	case before_dot:
	  printf ("/before_dot");
          break;

	case at_dot:
	  printf ("/at_dot");
          break;

	case after_dot:
	  printf ("/after_dot");
          break;

	case syntaxspec:
          printf ("/syntaxspec");
	  mcnt = *p++;
	  printf ("/%d", mcnt);
          break;
	  
	case notsyntaxspec:
          printf ("/notsyntaxspec");
	  mcnt = *p++;
	  printf ("/%d", mcnt);
	  break;
#endif /* emacs */

	case wordchar:
	  printf ("/wordchar");
          break;
	  
	case notwordchar:
	  printf ("/notwordchar");
          break;

	case begbuf:
	  printf ("/begbuf");
          break;

	case endbuf:
	  printf ("/endbuf");
          break;

        default:
          printf ("?%d", *(p-1));
	}

      putchar ('\n');
    }

  printf ("%d:\tend of pattern.\n", p - start);
}


void
print_compiled_pattern (bufp)
    struct re_pattern_buffer *bufp;
{
  unsigned char *buffer = bufp->buffer;

  print_partial_compiled_pattern (buffer, buffer + bufp->used);
  printf ("%d bytes used/%d bytes allocated.\n", bufp->used, bufp->allocated);

  if (bufp->fastmap_accurate && bufp->fastmap)
    {
      printf ("fastmap: ");
      print_fastmap (bufp->fastmap);
    }

  printf ("re_nsub: %d\t", bufp->re_nsub);
  printf ("regs_alloc: %d\t", bufp->regs_allocated);
  printf ("can_be_null: %d\t", bufp->can_be_null);
  printf ("newline_anchor: %d\n", bufp->newline_anchor);
  printf ("no_sub: %d\t", bufp->no_sub);
  printf ("not_bol: %d\t", bufp->not_bol);
  printf ("not_eol: %d\t", bufp->not_eol);
  printf ("syntax: %d\n", bufp->syntax);
  /* Perhaps we should print the translate table?  */
}


void
print_double_string (where, string1, size1, string2, size2)
    const char *where;
    const char *string1;
    const char *string2;
    int size1;
    int size2;
{
  unsigned this_char;
  
  if (where == NULL)
    printf ("(null)");
  else
    {
      if (FIRST_STRING_P (where))
        {
          for (this_char = where - string1; this_char < size1; this_char++)
            printchar (string1[this_char]);

          where = string2;    
        }

      for (this_char = where - string2; this_char < size2; this_char++)
        printchar (string2[this_char]);
    }
}

#else /* not DEBUG */

#undef assert
#define assert(e)

#define DEBUG_STATEMENT(e)
#define DEBUG_PRINT1(x)
#define DEBUG_PRINT2(x1, x2)
#define DEBUG_PRINT3(x1, x2, x3)
#define DEBUG_PRINT4(x1, x2, x3, x4)
#define DEBUG_PRINT_COMPILED_PATTERN(p, s, e)
#define DEBUG_PRINT_DOUBLE_STRING(w, s1, sz1, s2, sz2)

#endif /* not DEBUG */

/* Set by `re_set_syntax' to the current regexp syntax to recognize.  Can
   also be assigned to arbitrarily: each pattern buffer stores its own
   syntax, so it can be changed between regex compilations.  */
reg_syntax_t re_syntax_options = RE_SYNTAX_EMACS;


/* Specify the precise syntax of regexps for compilation.  This provides
   for compatibility for various utilities which historically have
   different, incompatible syntaxes.

   The argument SYNTAX is a bit mask comprised of the various bits
   defined in regex.h.  We return the old syntax.  */

reg_syntax_t
re_set_syntax (syntax)
    reg_syntax_t syntax;
{
  reg_syntax_t ret = re_syntax_options;
  
  re_syntax_options = syntax;
  return ret;
}

/* This table gives an error message for each of the error codes listed
   in regex.h.  Obviously the order here has to be same as there.  */

static const char *re_error_msg[] =
  { NULL,					/* REG_NOERROR */
    "No match",					/* REG_NOMATCH */
    "Invalid regular expression",		/* REG_BADPAT */
    "Invalid collation character",		/* REG_ECOLLATE */
    "Invalid character class name",		/* REG_ECTYPE */
    "Trailing backslash",			/* REG_EESCAPE */
    "Invalid back reference",			/* REG_ESUBREG */
    "Unmatched [ or [^",			/* REG_EBRACK */
    "Unmatched ( or \\(",			/* REG_EPAREN */
    "Unmatched \\{",				/* REG_EBRACE */
    "Invalid content of \\{\\}",		/* REG_BADBR */
    "Invalid range end",			/* REG_ERANGE */
    "Memory exhausted",				/* REG_ESPACE */
    "Invalid preceding regular expression",	/* REG_BADRPT */
    "Premature end of regular expression",	/* REG_EEND */
    "Regular expression too big",		/* REG_ESIZE */
    "Unmatched ) or \\)",			/* REG_ERPAREN */
  };

/* Subroutine declarations and macros for regex_compile.  */

static void store_op1 (), store_op2 ();
static void insert_op1 (), insert_op2 ();
static boolean at_begline_loc_p (), at_endline_loc_p ();
static boolean group_in_compile_stack ();
static reg_errcode_t compile_range ();

/* Fetch the next character in the uncompiled pattern---translating it 
   if necessary.  Also cast from a signed character in the constant
   string passed to us by the user to an unsigned char that we can use
   as an array index (in, e.g., `translate').  */
#define PATFETCH(c)							\
  do {if (p == pend) return REG_EEND;					\
    c = (unsigned char) *p++;						\
    if (translate) c = translate[c]; 					\
  } while (0)

/* Fetch the next character in the uncompiled pattern, with no
   translation.  */
#define PATFETCH_RAW(c)							\
  do {if (p == pend) return REG_EEND;					\
    c = (unsigned char) *p++; 						\
  } while (0)

/* Go backwards one character in the pattern.  */
#define PATUNFETCH p--


/* If `translate' is non-null, return translate[D], else just D.  We
   cast the subscript to translate because some data is declared as
   `char *', to avoid warnings when a string constant is passed.  But
   when we use a character as a subscript we must make it unsigned.  */
#define TRANSLATE(d) (translate ? translate[(unsigned char) (d)] : (d))


/* Macros for outputting the compiled pattern into `buffer'.  */

/* If the buffer isn't allocated when it comes in, use this.  */
#define INIT_BUF_SIZE  32

/* Make sure we have at least N more bytes of space in buffer.  */
#define GET_BUFFER_SPACE(n)						\
    while (b - bufp->buffer + (n) > bufp->allocated)			\
      EXTEND_BUFFER ()

/* Make sure we have one more byte of buffer space and then add C to it.  */
#define BUF_PUSH(c)							\
  do {									\
    GET_BUFFER_SPACE (1);						\
    *b++ = (unsigned char) (c);						\
  } while (0)


/* Ensure we have two more bytes of buffer space and then append C1 and C2.  */
#define BUF_PUSH_2(c1, c2)						\
  do {									\
    GET_BUFFER_SPACE (2);						\
    *b++ = (unsigned char) (c1);					\
    *b++ = (unsigned char) (c2);					\
  } while (0)


/* As with BUF_PUSH_2, except for three bytes.  */
#define BUF_PUSH_3(c1, c2, c3)						\
  do {									\
    GET_BUFFER_SPACE (3);						\
    *b++ = (unsigned char) (c1);					\
    *b++ = (unsigned char) (c2);					\
    *b++ = (unsigned char) (c3);					\
  } while (0)


/* Store a jump with opcode OP at LOC to location TO.  We store a
   relative address offset by the three bytes the jump itself occupies.  */
#define STORE_JUMP(op, loc, to) \
  store_op1 (op, loc, (to) - (loc) - 3)

/* Likewise, for a two-argument jump.  */
#define STORE_JUMP2(op, loc, to, arg) \
  store_op2 (op, loc, (to) - (loc) - 3, arg)

/* Like `STORE_JUMP', but for inserting.  Assume `b' is the buffer end.  */
#define INSERT_JUMP(op, loc, to) \
  insert_op1 (op, loc, (to) - (loc) - 3, b)

/* Like `STORE_JUMP2', but for inserting.  Assume `b' is the buffer end.  */
#define INSERT_JUMP2(op, loc, to, arg) \
  insert_op2 (op, loc, (to) - (loc) - 3, arg, b)


/* This is not an arbitrary limit: the arguments which represent offsets
   into the pattern are two bytes long.  So if 2^16 bytes turns out to
   be too small, many things would have to change.  */
#define MAX_BUF_SIZE (1L << 16)


/* Extend the buffer by twice its current size via realloc and
   reset the pointers that pointed into the old block to point to the
   correct places in the new one.  If extending the buffer results in it
   being larger than MAX_BUF_SIZE, then flag memory exhausted.  */
#define EXTEND_BUFFER()							\
  do { 									\
    unsigned char *old_buffer = bufp->buffer;				\
    if (bufp->allocated == MAX_BUF_SIZE) 				\
      return REG_ESIZE;							\
    bufp->allocated <<= 1;						\
    if (bufp->allocated > MAX_BUF_SIZE)					\
      bufp->allocated = MAX_BUF_SIZE; 					\
    bufp->buffer = (unsigned char *) realloc (bufp->buffer, bufp->allocated);\
    if (bufp->buffer == NULL)						\
      return REG_ESPACE;						\
    /* If the buffer moved, move all the pointers into it.  */		\
    if (old_buffer != bufp->buffer)					\
      {									\
        b = (b - old_buffer) + bufp->buffer;				\
        begalt = (begalt - old_buffer) + bufp->buffer;			\
        if (fixup_alt_jump)						\
          fixup_alt_jump = (fixup_alt_jump - old_buffer) + bufp->buffer;\
        if (laststart)							\
          laststart = (laststart - old_buffer) + bufp->buffer;		\
        if (pending_exact)						\
          pending_exact = (pending_exact - old_buffer) + bufp->buffer;	\
      }									\
  } while (0)


/* Since we have one byte reserved for the register number argument to
   {start,stop}_memory, the maximum number of groups we can report
   things about is what fits in that byte.  */
#define MAX_REGNUM 255

/* But patterns can have more than `MAX_REGNUM' registers.  We just
   ignore the excess.  */
typedef unsigned regnum_t;


/* Macros for the compile stack.  */

/* Since offsets can go either forwards or backwards, this type needs to
   be able to hold values from -(MAX_BUF_SIZE - 1) to MAX_BUF_SIZE - 1.  */
typedef int pattern_offset_t;

typedef struct
{
  pattern_offset_t begalt_offset;
  pattern_offset_t fixup_alt_jump;
  pattern_offset_t inner_group_offset;
  pattern_offset_t laststart_offset;  
  regnum_t regnum;
} compile_stack_elt_t;


typedef struct
{
  compile_stack_elt_t *stack;
  unsigned size;
  unsigned avail;			/* Offset of next open position.  */
} compile_stack_type;


#define INIT_COMPILE_STACK_SIZE 32

#define COMPILE_STACK_EMPTY  (compile_stack.avail == 0)
#define COMPILE_STACK_FULL  (compile_stack.avail == compile_stack.size)

/* The next available element.  */
#define COMPILE_STACK_TOP (compile_stack.stack[compile_stack.avail])


/* Set the bit for character C in a list.  */
#define SET_LIST_BIT(c)                               \
  (b[((unsigned char) (c)) / BYTEWIDTH]               \
   |= 1 << (((unsigned char) c) % BYTEWIDTH))


/* Get the next unsigned number in the uncompiled pattern.  */
#define GET_UNSIGNED_NUMBER(num) 					\
  { if (p != pend)							\
     {									\
       PATFETCH (c); 							\
       while (ISDIGIT (c)) 						\
         { 								\
           if (num < 0)							\
              num = 0;							\
           num = num * 10 + c - '0'; 					\
           if (p == pend) 						\
              break; 							\
           PATFETCH (c);						\
         } 								\
       } 								\
    }		

#define CHAR_CLASS_MAX_LENGTH  6 /* Namely, `xdigit'.  */

#define IS_CHAR_CLASS(string)						\
   (STREQ (string, "alpha") || STREQ (string, "upper")			\
    || STREQ (string, "lower") || STREQ (string, "digit")		\
    || STREQ (string, "alnum") || STREQ (string, "xdigit")		\
    || STREQ (string, "space") || STREQ (string, "print")		\
    || STREQ (string, "punct") || STREQ (string, "graph")		\
    || STREQ (string, "cntrl") || STREQ (string, "blank"))

/* `regex_compile' compiles PATTERN (of length SIZE) according to SYNTAX.
   Returns one of error codes defined in `regex.h', or zero for success.

   Assumes the `allocated' (and perhaps `buffer') and `translate'
   fields are set in BUFP on entry.

   If it succeeds, results are put in BUFP (if it returns an error, the
   contents of BUFP are undefined):
     `buffer' is the compiled pattern;
     `syntax' is set to SYNTAX;
     `used' is set to the length of the compiled pattern;
     `fastmap_accurate' is zero;
     `re_nsub' is the number of subexpressions in PATTERN;
     `not_bol' and `not_eol' are zero;
   
   The `fastmap' and `newline_anchor' fields are neither
   examined nor set.  */

static reg_errcode_t
regex_compile (pattern, size, syntax, bufp)
     const char *pattern;
     int size;
     reg_syntax_t syntax;
     struct re_pattern_buffer *bufp;
{
  /* We fetch characters from PATTERN here.  Even though PATTERN is
     `char *' (i.e., signed), we declare these variables as unsigned, so
     they can be reliably used as array indices.  */
  register unsigned char c, c1;
  
  /* A random tempory spot in PATTERN.  */
  const char *p1;

  /* Points to the end of the buffer, where we should append.  */
  register unsigned char *b;
  
  /* Keeps track of unclosed groups.  */
  compile_stack_type compile_stack;

  /* Points to the current (ending) position in the pattern.  */
  const char *p = pattern;
  const char *pend = pattern + size;
  
  /* How to translate the characters in the pattern.  */
  char *translate = bufp->translate;

  /* Address of the count-byte of the most recently inserted `exactn'
     command.  This makes it possible to tell if a new exact-match
     character can be added to that command or if the character requires
     a new `exactn' command.  */
  unsigned char *pending_exact = 0;

  /* Address of start of the most recently finished expression.
     This tells, e.g., postfix * where to find the start of its
     operand.  Reset at the beginning of groups and alternatives.  */
  unsigned char *laststart = 0;

  /* Address of beginning of regexp, or inside of last group.  */
  unsigned char *begalt;

  /* Place in the uncompiled pattern (i.e., the {) to
     which to go back if the interval is invalid.  */
  const char *beg_interval;
                
  /* Address of the place where a forward jump should go to the end of
     the containing expression.  Each alternative of an `or' -- except the
     last -- ends with a forward jump of this sort.  */
  unsigned char *fixup_alt_jump = 0;

  /* Counts open-groups as they are encountered.  Remembered for the
     matching close-group on the compile stack, so the same register
     number is put in the stop_memory as the start_memory.  */
  regnum_t regnum = 0;

#ifdef DEBUG
  DEBUG_PRINT1 ("\nCompiling pattern: ");
  if (debug)
    {
      unsigned debug_count;
      
      for (debug_count = 0; debug_count < size; debug_count++)
        printchar (pattern[debug_count]);
      putchar ('\n');
    }
#endif /* DEBUG */

  /* Initialize the compile stack.  */
  compile_stack.stack = TALLOC (INIT_COMPILE_STACK_SIZE, compile_stack_elt_t);
  if (compile_stack.stack == NULL)
    return REG_ESPACE;

  compile_stack.size = INIT_COMPILE_STACK_SIZE;
  compile_stack.avail = 0;

  /* Initialize the pattern buffer.  */
  bufp->syntax = syntax;
  bufp->fastmap_accurate = 0;
  bufp->not_bol = bufp->not_eol = 0;

  /* Set `used' to zero, so that if we return an error, the pattern
     printer (for debugging) will think there's no pattern.  We reset it
     at the end.  */
  bufp->used = 0;
  
  /* Always count groups, whether or not bufp->no_sub is set.  */
  bufp->re_nsub = 0;				

#if !defined (emacs) && !defined (SYNTAX_TABLE)
  /* Initialize the syntax table.  */
   init_syntax_once ();
#endif

  if (_br(bufp->allocated == 0,0x47080a0,2806,7))
    {
      if (_br(bufp->buffer,0x4708140,2808,11))
	{ /* If zero allocated, but buffer is non-null, try to realloc
             enough space.  This loses if buffer's address is bogus, but
             that is the user's responsibility.  */
          RETALLOC (bufp->buffer, INIT_BUF_SIZE, unsigned char);
        }
      else
        { /* Caller did not allocate a buffer.  Do it for them.  */
          bufp->buffer = TALLOC (INIT_BUF_SIZE, unsigned char);
        }
      if (_br(!bufp->buffer,0x4708798,2818,11)) return REG_ESPACE;

      bufp->allocated = INIT_BUF_SIZE;
    }

  begalt = b = bufp->buffer;

  /* Loop through the uncompiled pattern until we're at the end.  */
  while (_br(p != pend,0x4708b38,2826,10))
    {
      PATFETCH (c);

      switch (c)
        {
        case '^':
          {
            if (   /* If at start of pattern, it's an operator.  */
                   _br(p == pattern + 1
                   /* If context independent, it's an operator.  */
                || syntax & RE_CONTEXT_INDEP_ANCHORS
                   /* Otherwise, depends on what's come before.  */
                || at_begline_loc_p (pattern, p, syntax),0x4709980,2835,20))
              BUF_PUSH (begline);
            else
              goto normal_char;
          }
          break;


        case '$':
          {
            if (   /* If at end of pattern, it's an operator.  */
                   _br(p == pend 
                   /* If context independent, it's an operator.  */
                || syntax & RE_CONTEXT_INDEP_ANCHORS
                   /* Otherwise, depends on what's next.  */
                || at_endline_loc_p (p, pend, syntax),0x470cb40,2850,20))
               BUF_PUSH (endline);
             else
               goto normal_char;
           }
           break;


	case '+':
        case '?':
          if (_br((syntax & RE_BK_PLUS_QM)
              || (syntax & RE_LIMITED_OPS),0x470eeb8,2864,15))
            goto normal_char;
        handle_plus:
        case '*':
          /* If there is no previous pattern... */
          if (_br(!laststart,0x470efc0,2870,15))
            {
              if (syntax & RE_CONTEXT_INVALID_OPS)
                return REG_BADRPT;
              else if (_br(!(syntax & RE_CONTEXT_INDEP_OPS),0x470f580,2874,24))
                goto normal_char;
            }

          {
            /* Are we optimizing this jump?  */
            boolean keep_string_p = false;
            
            /* 1 means zero (many) matches is allowed.  */
            char zero_times_ok = 0, many_times_ok = 0;

            /* If there is a sequence of repetition chars, collapse it
               down to just one (the right one).  We can't combine
               interval operators with these because of, e.g., `a{2}*',
               which should only match an even number of `a's.  */

            for (;;)
              {
                zero_times_ok |= c != '+';
                many_times_ok |= c != '?';

                if (_br(p == pend,0x470fb90,2895,21))
                  break;

                PATFETCH (c);

                if (_br(c == '*'
                    || (!(syntax & RE_BK_PLUS_QM) && (c == '+' || c == '?')),0x4710410,2900,21))
                  ;

                else if (_br(syntax & RE_BK_PLUS_QM  &&  c == '\\',0x47109a8,2904,26))
                  {
                    if (_br(p == pend,0x4710a50,2906,25)) return REG_EESCAPE;

                    PATFETCH (c1);
                    if (_br(!(c1 == '+' || c1 == '?'),0x47110d8,2909,25))
                      {
                        PATUNFETCH;
                        PATUNFETCH;
                        break;
                      }

                    c = c1;
                  }
                else
                  {
                    PATUNFETCH;
                    break;
                  }

                /* If we get here, we found another repeat character.  */
               }

            /* Star, etc. applied to an empty pattern is equivalent
               to an empty pattern.  */
            if (_br(!laststart,0x4711460,2929,17))  
              break;

            /* Now we know whether or not zero matches is allowed
               and also whether or not two or more matches is allowed.  */
            if (_br(many_times_ok,0x47114e8,2934,17))
              { /* More than one repetition is allowed, so put in at the
                   end a backward relative jump from `b' to before the next
                   jump we're going to put in below (which jumps from
                   laststart to after this jump).  

                   But if we are at the `*' in the exact sequence `.*\n',
                   insert an unconditional jump backwards to the .,
                   instead of the beginning of the loop.  This way we only
                   push a failure point once, instead of every time
                   through the loop.  */
                assert (p - 1 > pattern);

                /* Allocate the space for the jump.  */
                GET_BUFFER_SPACE (3);

                /* We know we are not at the first character of the pattern,
                   because laststart was nonzero.  And we've already
                   incremented `p', by the way, to be the character after
                   the `*'.  Do we have to do something analogous here
                   for null bytes, because of RE_DOT_NOT_NULL?  */
                if (TRANSLATE (*(p - 2)) == TRANSLATE ('.')
		    && zero_times_ok
                    && p < pend && TRANSLATE (*p) == TRANSLATE ('\n')
                    && !(syntax & RE_DOT_NEWLINE))
                  { /* We have .*\n.  */
                    STORE_JUMP (jump, b, laststart);
                    keep_string_p = true;
                  }
                else
                  /* Anything else.  */
                  STORE_JUMP (maybe_pop_jump, b, laststart - 3);

                /* We've added more stuff to the buffer.  */
                b += 3;
              }

            /* On failure, jump from laststart to b + 3, which will be the
               end of the buffer after this jump is inserted.  */
            GET_BUFFER_SPACE (3);
            INSERT_JUMP (keep_string_p ? on_failure_keep_string_jump
                                       : on_failure_jump,
                         laststart, b + 3);
            pending_exact = 0;
            b += 3;

            if (_br(!zero_times_ok,0x4716508,2980,17))
              {
                /* At least one repetition is required, so insert a
                   `dummy_failure_jump' before the initial
                   `on_failure_jump' instruction of the loop. This
                   effects a skip over that instruction the first time
                   we hit that loop.  */
                GET_BUFFER_SPACE (3);
                INSERT_JUMP (dummy_failure_jump, laststart, laststart + 6);
                b += 3;
              }
            }
	  break;


	case '.':
          laststart = b;
          BUF_PUSH (anychar);
          break;


        case '[':
          {
            boolean had_char_class = false;

            if (_br(p == pend,0x471a230,3005,17)) return REG_EBRACK;

            /* Ensure that we have enough space to push a charset: the
               opcode, the length count, and the bitset; 34 bytes in all.  */
	    GET_BUFFER_SPACE (34);

            laststart = b;

            /* We test `*p == '^' twice, instead of using an if
               statement, so we only need one BUF_PUSH.  */
            BUF_PUSH (*p == '^' ? charset_not : charset); 
            if (_br(*p == '^',0x471dbb0,3016,17))
              p++;

            /* Remember the first position in the bracket expression.  */
            p1 = p;

            /* Push the number of bytes in the bitmap.  */
            BUF_PUSH ((1 << BYTEWIDTH) / BYTEWIDTH);

            /* Clear the whole map.  */
            bzero (b, (1 << BYTEWIDTH) / BYTEWIDTH);

            /* charset_not matches newline according to a syntax bit.  */
            if (_br((re_opcode_t) b[-2] == charset_not
                && (syntax & RE_HAT_LISTS_NOT_NEWLINE),0x4720418,3029,17))
              SET_LIST_BIT ('\n');

            /* Read in characters and ranges, setting map bits.  */
            for (;;)
              {
                if (_br(p == pend,0x4720820,3036,21)) return REG_EBRACK;

                PATFETCH (c);

                /* \ might escape characters inside [...] and [^...].  */
                if (_br((syntax & RE_BACKSLASH_ESCAPE_IN_LISTS) && c == '\\',0x4720ed8,3041,21))
                  {
                    if (_br(p == pend,0x4720f80,3043,25)) return REG_EESCAPE;

                    PATFETCH (c1);
                    SET_LIST_BIT (c1);
                    continue;
                  }

                /* Could be the end of the bracket expression.  If it's
                   not (i.e., when the bracket expression is `[]' so
                   far), the ']' character bit gets set way below.  */
                if (_br(c == ']' && p != p1 + 1,0x4721a08,3053,21))
                  break;

                /* Look ahead to see if it's a range when the last thing
                   was a character class.  */
                if (_br(had_char_class && c == '-' && *p != ']',0x4721c58,3058,21))
                  return REG_ERANGE;

                /* Look ahead to see if it's a range when the last thing
                   was a character: if this is a hyphen not at the
                   beginning or the end of a list, then it's the range
                   operator.  */
                if (_br(c == '-' 
                    && !(p - 2 >= pattern && p[-2] == '[') 
                    && !(p - 3 >= pattern && p[-3] == '[' && p[-2] == '^')
                    && *p != ']',0x47224e8,3065,21))
                  {
                    reg_errcode_t ret
                      = compile_range (&p, pend, translate, syntax, b);
                    if (_br(ret != REG_NOERROR,0x4722890,3072,25)) return ret;
                  }

                else if (_br(p[0] == '-' && p[1] != ']',0x4722b60,3075,26))
                  { /* This handles ranges made up of characters only.  */
                    reg_errcode_t ret;

		    /* Move past the `-'.  */
                    PATFETCH (c1);
                    
                    ret = compile_range (&p, pend, translate, syntax, b);
                    if (_br(ret != REG_NOERROR,0x4723338,3083,25)) return ret;
                  }

                /* See if we're at the beginning of a possible character
                   class.  */

                else if (_br(syntax & RE_CHAR_CLASSES && c == '[' && *p == ':',0x4723748,3089,26))
                  { /* Leave room for the null.  */
                    char str[CHAR_CLASS_MAX_LENGTH + 1];

                    PATFETCH (c);
                    c1 = 0;

                    /* If pattern is `[[:'.  */
                    if (_br(p == pend,0x4723dd0,3097,25)) return REG_EBRACK;

                    for (;;)
                      {
                        PATFETCH (c);
                        if (c == ':' || c == ']' || p == pend
                            || c1 == CHAR_CLASS_MAX_LENGTH)
                          break;
                        str[c1++] = c;
                      }
                    str[c1] = '\0';

                    /* If isn't a word bracketed by `[:' and:`]':
                       undo the ending character, the letters, and leave 
                       the leading `:' and `[' (but set bits for them).  */
                    if (_br(c == ':' && *p == ']',0x4724a10,3112,25))
                      {
                        int ch;
                        boolean is_alnum = STREQ (str, "alnum");
                        boolean is_alpha = STREQ (str, "alpha");
                        boolean is_blank = STREQ (str, "blank");
                        boolean is_cntrl = STREQ (str, "cntrl");
                        boolean is_digit = STREQ (str, "digit");
                        boolean is_graph = STREQ (str, "graph");
                        boolean is_lower = STREQ (str, "lower");
                        boolean is_print = STREQ (str, "print");
                        boolean is_punct = STREQ (str, "punct");
                        boolean is_space = STREQ (str, "space");
                        boolean is_upper = STREQ (str, "upper");
                        boolean is_xdigit = STREQ (str, "xdigit");
                        
                        if (!IS_CHAR_CLASS (str)) return REG_ECTYPE;

                        /* Throw away the ] at the end of the character
                           class.  */
                        PATFETCH (c);					

                        if (_br(p == pend,0x4729608,3134,29)) return REG_EBRACK;

                        for (ch = 0; ch < 1 << BYTEWIDTH; ch++)
                          {
                            if (   _br((is_alnum  && ISALNUM (ch))
                                || (is_alpha  && ISALPHA (ch))
                                || (is_blank  && ISBLANK (ch))
                                || (is_cntrl  && ISCNTRL (ch))
                                || (is_digit  && ISDIGIT (ch))
                                || (is_graph  && ISGRAPH (ch))
                                || (is_lower  && ISLOWER (ch))
                                || (is_print  && ISPRINT (ch))
                                || (is_punct  && ISPUNCT (ch))
                                || (is_space  && ISSPACE (ch))
                                || (is_upper  && ISUPPER (ch))
                                || (is_xdigit && ISXDIGIT (ch)),0x472c858,3138,36))
                            SET_LIST_BIT (ch);
                          }
                        had_char_class = true;
                      }
                    else
                      {
                        c1++;
                        while (_br(c1--,0x472ce28,3157,32))    
                          PATUNFETCH;
                        SET_LIST_BIT ('[');
                        SET_LIST_BIT (':');
                        had_char_class = false;
                      }
                  }
                else
                  {
                    had_char_class = false;
                    SET_LIST_BIT (c);
                  }
              }

            /* Discard any (non)matching list bytes that are all 0 at the
               end of the map.  Decrease the map-length byte too.  */
            while (_br((int) b[-1] > 0 && b[b[-1] - 1] == 0,0x472df10,3173,20)) 
              b[-1]--; 
            b += b[-1];
          }
          break;


	case '(':
          if (syntax & RE_NO_BK_PARENS)
            goto handle_open;
          else
            goto normal_char;


        case ')':
          if (syntax & RE_NO_BK_PARENS)
            goto handle_close;
          else
            goto normal_char;


        case '\n':
          if (syntax & RE_NEWLINE_ALT)
            goto handle_alt;
          else
            goto normal_char;


	case '|':
          if (syntax & RE_NO_BK_VBAR)
            goto handle_alt;
          else
            goto normal_char;


        case '{':
           if (syntax & RE_INTERVALS && syntax & RE_NO_BK_BRACES)
             goto handle_interval;
           else
             goto normal_char;


        case '\\':
          if (_br(p == pend,0x47309d0,3216,15)) return REG_EESCAPE;

          /* Do not translate the character after the \, so that we can
             distinguish, e.g., \B from \b, even if we normally would
             translate, e.g., B to b.  */
          PATFETCH_RAW (c);

          switch (c)
            {
            case '(':
              if (syntax & RE_NO_BK_PARENS)
                goto normal_backslash;

            handle_open:
              bufp->re_nsub++;
              regnum++;

              if (COMPILE_STACK_FULL)
                { 
                  RETALLOC (compile_stack.stack, compile_stack.size << 1,
                            compile_stack_elt_t);
                  if (compile_stack.stack == NULL) return REG_ESPACE;

                  compile_stack.size <<= 1;
                }

              /* These are the values to restore when we hit end of this
                 group.  They are all relative offsets, so that if the
                 whole pattern moves because of realloc, they will still
                 be valid.  */
              COMPILE_STACK_TOP.begalt_offset = begalt - bufp->buffer;
              COMPILE_STACK_TOP.fixup_alt_jump 
                = fixup_alt_jump ? fixup_alt_jump - bufp->buffer + 1 : 0;
              COMPILE_STACK_TOP.laststart_offset = b - bufp->buffer;
              COMPILE_STACK_TOP.regnum = regnum;

              /* We will eventually replace the 0 with the number of
                 groups inner to this one.  But do not push a
                 start_memory for groups beyond the last one we can
                 represent in the compiled pattern.  */
              if (regnum <= MAX_REGNUM)
                {
                  COMPILE_STACK_TOP.inner_group_offset = b - bufp->buffer + 2;
                  BUF_PUSH_3 (start_memory, regnum, 0);
                }
                
              compile_stack.avail++;

              fixup_alt_jump = 0;
              laststart = 0;
              begalt = b;
	      /* If we've reached MAX_REGNUM groups, then this open
		 won't actually generate any code, so we'll have to
		 clear pending_exact explicitly.  */
	      pending_exact = 0;
              break;


            case ')':
              if (syntax & RE_NO_BK_PARENS) goto normal_backslash;

              if (COMPILE_STACK_EMPTY)
                if (syntax & RE_UNMATCHED_RIGHT_PAREN_ORD)
                  goto normal_backslash;
                else
                  return REG_ERPAREN;

            handle_close:
              if (_br(fixup_alt_jump,0x464ff78,3284,19))
                { /* Push a dummy failure point at the end of the
                     alternative for a possible future
                     `pop_failure_jump' to pop.  See comments at
                     `push_dummy_failure' in `re_match_2'.  */
                  BUF_PUSH (push_dummy_failure);
                  
                  /* We allocated space for this jump when we assigned
                     to `fixup_alt_jump', in the `handle_alt' case below.  */
                  STORE_JUMP (jump_past_alt, fixup_alt_jump, b - 1);
                }

              /* See similar code for backslashed left paren above.  */
              if (COMPILE_STACK_EMPTY)
                if (syntax & RE_UNMATCHED_RIGHT_PAREN_ORD)
                  goto normal_char;
                else
                  return REG_ERPAREN;

              /* Since we just checked for an empty stack above, this
                 ``can't happen''.  */
              assert (compile_stack.avail != 0);
              {
                /* We don't just want to restore into `regnum', because
                   later groups should continue to be numbered higher,
                   as in `(ab)c(de)' -- the second group is #2.  */
                regnum_t this_group_regnum;

                compile_stack.avail--;		
                begalt = bufp->buffer + COMPILE_STACK_TOP.begalt_offset;
                fixup_alt_jump
                  = COMPILE_STACK_TOP.fixup_alt_jump
                    ? bufp->buffer + COMPILE_STACK_TOP.fixup_alt_jump - 1 
                    : 0;
                laststart = bufp->buffer + COMPILE_STACK_TOP.laststart_offset;
                this_group_regnum = COMPILE_STACK_TOP.regnum;
		/* If we've reached MAX_REGNUM groups, then this open
		   won't actually generate any code, so we'll have to
		   clear pending_exact explicitly.  */
		pending_exact = 0;

                /* We're at the end of the group, so now we know how many
                   groups were inside this one.  */
                if (this_group_regnum <= MAX_REGNUM)
                  {
                    unsigned char *inner_group_loc
                      = bufp->buffer + COMPILE_STACK_TOP.inner_group_offset;
                    
                    *inner_group_loc = regnum - this_group_regnum;
                    BUF_PUSH_3 (stop_memory, this_group_regnum,
                                regnum - this_group_regnum);
                  }
              }
              break;


            case '|':					/* `\|'.  */
              if (syntax & RE_LIMITED_OPS || syntax & RE_NO_BK_VBAR)
                goto normal_backslash;
            handle_alt:
              if (syntax & RE_LIMITED_OPS)
                goto normal_char;

              /* Insert before the previous alternative a jump which
                 jumps to this alternative if the former fails.  */
              GET_BUFFER_SPACE (3);
              INSERT_JUMP (on_failure_jump, begalt, b + 6);
              pending_exact = 0;
              b += 3;

              /* The alternative before this one has a jump after it
                 which gets executed if it gets matched.  Adjust that
                 jump so it will jump to this alternative's analogous
                 jump (put in below, which in turn will jump to the next
                 (if any) alternative's such jump, etc.).  The last such
                 jump jumps to the correct final destination.  A picture:
                          _____ _____ 
                          |   | |   |   
                          |   v |   v 
                         a | b   | c   

                 If we are at `b', then fixup_alt_jump right now points to a
                 three-byte space after `a'.  We'll put in the jump, set
                 fixup_alt_jump to right after `b', and leave behind three
                 bytes which we'll fill in when we get to after `c'.  */

              if (_br(fixup_alt_jump,0x474c898,3370,19))
                STORE_JUMP (jump_past_alt, fixup_alt_jump, b);

              /* Mark and leave space for a jump after this alternative,
                 to be filled in later either by next alternative or
                 when know we're at the end of a series of alternatives.  */
              fixup_alt_jump = b;
              GET_BUFFER_SPACE (3);
              b += 3;

              laststart = 0;
              begalt = b;
              break;


            case '{': 
              /* If \{ is a literal.  */
              if (_br(!(syntax & RE_INTERVALS)
                     /* If we're at `\{' and it's not the open-interval 
                        operator.  */
                  || ((syntax & RE_INTERVALS) && (syntax & RE_NO_BK_BRACES))
                  || (p - 2 == pattern  &&  p == pend),0x474f930,3387,19))
                goto normal_backslash;

            handle_interval:
              {
                /* If got here, then the syntax allows intervals.  */

                /* At least (most) this many matches must be made.  */
                int lower_bound = -1, upper_bound = -1;

                beg_interval = p - 1;

                if (_br(p == pend,0x474fcd0,3403,21))
                  {
                    if (syntax & RE_NO_BK_BRACES)
                      goto unfetch_interval;
                    else
                      return REG_EBRACE;
                  }

                GET_UNSIGNED_NUMBER (lower_bound);

                if (_br(c == ',',0x4751578,3413,21))
                  {
                    GET_UNSIGNED_NUMBER (upper_bound);
                    if (_br(upper_bound < 0,0x4752748,3416,25)) upper_bound = RE_DUP_MAX;
                  }
                else
                  /* Interval such as `{1}' => match exactly once. */
                  upper_bound = lower_bound;

                if (_br(lower_bound < 0 || upper_bound > RE_DUP_MAX
                    || lower_bound > upper_bound,0x4752c90,3422,21))
                  {
                    if (syntax & RE_NO_BK_BRACES)
                      goto unfetch_interval;
                    else 
                      return REG_BADBR;
                  }

                if (_br(!(syntax & RE_NO_BK_BRACES),0x4753918,3431,21)) 
                  {
                    if (_br(c != '\\',0x47539a8,3433,25)) return REG_EBRACE;

                    PATFETCH (c);
                  }

                if (_br(c != '}',0x47549b0,3438,21))
                  {
                    if (syntax & RE_NO_BK_BRACES)
                      goto unfetch_interval;
                    else 
                      return REG_BADBR;
                  }

                /* We just parsed a valid interval.  */

                /* If it's invalid to have no preceding re.  */
                if (_br(!laststart,0x47550b8,3449,21))
                  {
                    if (syntax & RE_CONTEXT_INVALID_OPS)
                      return REG_BADRPT;
                    else if (syntax & RE_CONTEXT_INDEP_OPS)
                      laststart = b;
                    else
                      goto unfetch_interval;
                  }

                /* If the upper bound is zero, don't want to succeed at
                   all; jump from `laststart' to `b + 3', which will be
                   the end of the buffer after we insert the jump.  */
                 if (_br(upper_bound == 0,0x4755828,3462,22))
                   {
                     GET_BUFFER_SPACE (3);
                     INSERT_JUMP (jump, laststart, b + 3);
                     b += 3;
                   }

                 /* Otherwise, we have a nontrivial interval.  When
                    we're all done, the pattern will look like:
                      set_number_at <jump count> <upper bound>
                      set_number_at <succeed_n count> <lower bound>
                      succeed_n <after jump addr> <succed_n count>
                      <body of loop>
                      jump_n <succeed_n addr> <jump count>
                    (The upper bound and `jump_n' are omitted if
                    `upper_bound' is 1, though.)  */
                 else 
                   { /* If the upper bound is > 1, we need to insert
                        more at the end of the loop.  */
                     unsigned nbytes = 10 + (upper_bound > 1) * 10;

                     GET_BUFFER_SPACE (nbytes);

                     /* Initialize lower bound of the `succeed_n', even
                        though it will be set during matching by its
                        attendant `set_number_at' (inserted next),
                        because `re_compile_fastmap' needs to know.
                        Jump to the `jump_n' we might insert below.  */
                     INSERT_JUMP2 (succeed_n, laststart,
                                   b + 5 + (upper_bound > 1) * 5,
                                   lower_bound);
                     b += 5;

                     /* Code to initialize the lower bound.  Insert 
                        before the `succeed_n'.  The `5' is the last two
                        bytes of this `set_number_at', plus 3 bytes of
                        the following `succeed_n'.  */
                     insert_op2 (set_number_at, laststart, 5, lower_bound, b);
                     b += 5;

                     if (_br(upper_bound > 1,0x4759990,3502,26))
                       { /* More than one repetition is allowed, so
                            append a backward jump to the `succeed_n'
                            that starts this interval.
                            
                            When we've reached this during matching,
                            we'll have matched the interval once, so
                            jump back only `upper_bound - 1' times.  */
                         STORE_JUMP2 (jump_n, b, laststart + 5,
                                      upper_bound - 1);
                         b += 5;

                         /* The location we want to set is the second
                            parameter of the `jump_n'; that is `b-2' as
                            an absolute address.  `laststart' will be
                            the `set_number_at' we're about to insert;
                            `laststart+3' the number to set, the source
                            for the relative address.  But we are
                            inserting into the middle of the pattern --
                            so everything is getting moved up by 5.
                            Conclusion: (b - 2) - (laststart + 3) + 5,
                            i.e., b - laststart.
                            
                            We insert this at the beginning of the loop
                            so that if we fail during matching, we'll
                            reinitialize the bounds.  */
                         insert_op2 (set_number_at, laststart, b - laststart,
                                     upper_bound - 1, b);
                         b += 5;
                       }
                   }
                pending_exact = 0;
                beg_interval = NULL;
              }
              break;

            unfetch_interval:
              /* If an invalid interval, match the characters as literals.  */
               assert (beg_interval);
               p = beg_interval;
               beg_interval = NULL;

               /* normal_char and normal_backslash need `c'.  */
               PATFETCH (c);	

               if (_br(!(syntax & RE_NO_BK_BRACES),0x475af08,3547,20))
                 {
                   if (_br(p > pattern  &&  p[-1] == '\\',0x475b0e8,3549,24))
                     goto normal_backslash;
                 }
               goto normal_char;

#ifdef emacs
            /* There is no way to specify the before_dot and after_dot
               operators.  rms says this is ok.  --karl  */
            case '=':
              BUF_PUSH (at_dot);
              break;

            case 's':	
              laststart = b;
              PATFETCH (c);
              BUF_PUSH_2 (syntaxspec, syntax_spec_code[c]);
              break;

            case 'S':
              laststart = b;
              PATFETCH (c);
              BUF_PUSH_2 (notsyntaxspec, syntax_spec_code[c]);
              break;
#endif /* emacs */


            case 'w':
              laststart = b;
              BUF_PUSH (wordchar);
              break;


            case 'W':
              laststart = b;
              BUF_PUSH (notwordchar);
              break;


            case '<':
              BUF_PUSH (wordbeg);
              break;

            case '>':
              BUF_PUSH (wordend);
              break;

            case 'b':
              BUF_PUSH (wordbound);
              break;

            case 'B':
              BUF_PUSH (notwordbound);
              break;

            case '`':
              BUF_PUSH (begbuf);
              break;

            case '\'':
              BUF_PUSH (endbuf);
              break;

            case '1': case '2': case '3': case '4': case '5':
            case '6': case '7': case '8': case '9':
              if (syntax & RE_NO_BK_REFS)
                goto normal_char;

              c1 = c - '0';

              if (_br(c1 > regnum,0x476cf30,3618,19))
                return REG_ESUBREG;

              /* Can't back reference to a subexpression if inside of it.  */
              if (_br(group_in_compile_stack (compile_stack, c1),0x476d078,3622,19))
                goto normal_char;

              laststart = b;
              BUF_PUSH_2 (duplicate, c1);
              break;


            case '+':
            case '?':
              if (syntax & RE_BK_PLUS_QM)
                goto handle_plus;
              else
                goto normal_backslash;

            default:
            normal_backslash:
              /* You might think it would be useful for \ to mean
                 not to translate; but if we don't translate it
                 it will never match anything.  */
              c = TRANSLATE (c);
              goto normal_char;
            }
          break;


	default:
        /* Expects the character in `c'.  */
	normal_char:
	      /* If no exactn currently being built.  */
          if (_br(!pending_exact 

              /* If last exactn not at current position.  */
              || pending_exact + *pending_exact + 1 != b
              
              /* We have only one byte following the exactn for the count.  */
	      || *pending_exact == (1 << BYTEWIDTH) - 1

              /* If followed by a repetition operator.  */
              || *p == '*' || *p == '^'
	      || ((syntax & RE_BK_PLUS_QM)
		  ? *p == '\\' && (p[1] == '+' || p[1] == '?')
		  : (*p == '+' || *p == '?'))
	      || ((syntax & RE_INTERVALS)
                  && ((syntax & RE_NO_BK_BRACES)
		      ? *p == '{'
                      : (p[0] == '\\' && p[1] == '{'))),0x47719d8,3652,15))
	    {
	      /* Start building a new exactn.  */
              
              laststart = b;

	      BUF_PUSH_2 (exactn, 0);
	      pending_exact = b - 1;
            }
            
	  BUF_PUSH (c);
          (*pending_exact)++;
	  break;
        } /* switch (c) */
    } /* while p != pend */

  
  /* Through the pattern now.  */
  
  if (_br(fixup_alt_jump,0x4775b58,3687,7))
    STORE_JUMP (jump_past_alt, fixup_alt_jump, b);

  if (!COMPILE_STACK_EMPTY) 
    return REG_EPAREN;

  free (compile_stack.stack);

  /* We have succeeded; set the length of the buffer.  */
  bufp->used = b - bufp->buffer;

#ifdef DEBUG
  if (debug)
    {
      DEBUG_PRINT1 ("\nCompiled pattern: \n");
      print_compiled_pattern (bufp);
    }
#endif /* DEBUG */

  return REG_NOERROR;
} /* regex_compile */

/* Subroutines for `regex_compile'.  */

/* Store OP at LOC followed by two-byte integer parameter ARG.  */

static void
store_op1 (op, loc, arg)
    re_opcode_t op;
    unsigned char *loc;
    int arg;
{
  *loc = (unsigned char) op;
  STORE_NUMBER (loc + 1, arg);
}


/* Like `store_op1', but" for 'two two-byte parameters ARG1 and ARG2.  */

static void
store_op2 (op, loc, arg1, arg2)
    re_opcode_t op;
    unsigned char *loc;
    int arg1, arg2;
{
  *loc = (unsigned char) op;
  STORE_NUMBER (loc + 1, arg1);
  STORE_NUMBER (loc + 3, arg2);
}


/* Copy the bytes from LOC to END to open up three bytes of space at LOC
   for OP followed by two-byte integer parameter ARG.  */

static void
insert_op1 (op, loc, arg, end)
    re_opcode_t op;
    unsigned char *loc;
    int arg;
    unsigned char *end;    
{
  register unsigned char *pfrom = end;
  register unsigned char *pto = end + 3;

  while (_br(pfrom != loc,0x4777dd0,3751,10))
    *--pto = *--pfrom;
    
  store_op1 (op, loc, arg);
}


/* Like `insert_op1', but for two two-byte parameters ARG1 and ARG2.  */

static void
insert_op2 (op, loc, arg1, arg2, end)
    re_opcode_t op;
    unsigned char *loc;
    int arg1, arg2;
    unsigned char *end;    
{
  register unsigned char *pfrom = end;
  register unsigned char *pto = end + 5;

  while (_br(pfrom != loc,0x477a8d8,3770,10))
    *--pto = *--pfrom;
    
  store_op2 (op, loc, arg1, arg2);
}


/* P points to just after a ^ in PATTERN.  Return true if that ^ comes
   after an alternative or a begin-subexpression.  We assume there is at
   least one character before the ^.  */

static boolean
at_begline_loc_p (pattern, p, syntax)
    const char *pattern, *p;
    reg_syntax_t syntax;
{
  const char *prev = p - 2;
  boolean prev_prev_backslash = prev > pattern && prev[-1] == '\\';
  
  return
       /* After a subexpression?  */
       (*prev == '(' && (syntax & RE_NO_BK_PARENS || prev_prev_backslash))
       /* After an alternative?  */
    || (*prev == '|' && (syntax & RE_NO_BK_VBAR || prev_prev_backslash));
}


/* The dual of at_begline_loc_p.  This one is for $.  We assume there is
   at least one character after the $, i.e., `P < PEND'.  */

static boolean
at_endline_loc_p (p, pend, syntax)
    const char *p, *pend;
    int syntax;
{
  const char *next = p;
  boolean next_backslash = *next == '\\';
  const char *next_next = p + 1 < pend ? p + 1 : NULL;
  
  return
       /* Before a subexpression?  */
       (syntax & RE_NO_BK_PARENS ? *next == ')'
        : next_backslash && next_next && *next_next == ')')
       /* Before an alternative?  */
    || (syntax & RE_NO_BK_VBAR ? *next == '|'
        : next_backslash && next_next && *next_next == '|');
}


/* Returns true if REGNUM is in one of COMPILE_STACK's elements and 
   false if it's not.  */

static boolean
group_in_compile_stack (compile_stack, regnum)
    compile_stack_type compile_stack;
    regnum_t regnum;
{
  int this_element;

  for (this_element = compile_stack.avail - 1;  
       _br(this_element >= 0,0x477e2f8,3830,8); 
       this_element--)
    if (_br(compile_stack.stack[this_element].regnum == regnum,0x477e4d8,3832,9))
      return true;

  return false;
}


/* Read the ending character of a range (in a bracket expression) from the
   uncompiled pattern *P_PTR (which ends at PEND).  We assume the
   starting character is in `P[-2]'.  (`P[-1]' is the character `-'.)
   Then we set the translation of all bits between the starting and
   ending characters (inclusive) in the compiled pattern B.
   
   Return an error code.
   
   We use these short variable names so we can use the same macros as
   `regex_compile' itself.  */

static reg_errcode_t
compile_range (p_ptr, pend, translate, syntax, b)
    const char **p_ptr, *pend;
    char *translate;
    reg_syntax_t syntax;
    unsigned char *b;
{
  unsigned this_char;

  const char *p = *p_ptr;
  int range_start, range_end;
  
  if (_br(p == pend,0x477ed88,3862,7))
    return REG_ERANGE;

  /* Even though the pattern is a signed `char *', we need to fetch
     with unsigned char *'s; if the high bit of the pattern character
     is set, the range endpoints will be negative if we fetch using a
     signed char *.

     We also want to fetch the endpoints without translating them; the 
     appropriate translation is done in the bit-setting loop below.  */
  range_start = ((unsigned char *) p)[-2];
  range_end   = ((unsigned char *) p)[0];

  /* Have to increment the pointer into the pattern string, so the
     caller isn't still at the ending character.  */
  (*p_ptr)++;

  /* If the start is after the end, the range is empty.  */
  if (_br(range_start > range_end,0x477f250,3880,7))
    return syntax & RE_NO_EMPTY_RANGES ? REG_ERANGE : REG_NOERROR;

  /* Here we see why `this_char' has to be larger than an `unsigned
     char' -- the range is inclusive, so if `range_end' == 0xff
     (assuming 8-bit characters), we would otherwise go into an infinite
     loop, since all characters <= 0xff.  */
  for (this_char = range_start; _br(this_char <= range_end,0x477fbe0,3887,33); this_char++)
    {
      SET_LIST_BIT (TRANSLATE (this_char));
    }
  
  return REG_NOERROR;
}

/* Failure stack declarations and macros; both re_compile_fastmap and
   re_match_2 use a failure stack.  These have to be macros because of
   REGEX_ALLOCATE.  */
   

/* Number of failure points for which to initially allocate space
   when matching.  If this number is exceeded, we allocate more
   space, so it is not a hard limit.  */
#ifndef INIT_FAILURE_ALLOC
#define INIT_FAILURE_ALLOC 5
#endif

/* Roughly the maximum number of failure points on the stack.  Would be
   exactly that if always used MAX_FAILURE_SPACE each time we failed.
   This is a variable only so users of regex can assign to it; we never
   change it ourselves.  */
int re_max_failures = 2000;

typedef const unsigned char *fail_stack_elt_t;

typedef struct
{
  fail_stack_elt_t *stack;
  unsigned size;
  unsigned avail;			/* Offset of next open position.  */
} fail_stack_type;

#define FAIL_STACK_EMPTY()     (fail_stack.avail == 0)
#define FAIL_STACK_PTR_EMPTY() (fail_stack_ptr->avail == 0)
#define FAIL_STACK_FULL()      (fail_stack.avail == fail_stack.size)
#define FAIL_STACK_TOP()       (fail_stack.stack[fail_stack.avail])


/* Initialize `fail_stack'.  Do `return -2' if the alloc fails.  */

#define INIT_FAIL_STACK()						\
  do {									\
    fail_stack.stack = (fail_stack_elt_t *)				\
      REGEX_ALLOCATE (INIT_FAILURE_ALLOC * sizeof (fail_stack_elt_t));	\
									\
    if (fail_stack.stack == NULL)					\
      return -2;							\
									\
    fail_stack.size = INIT_FAILURE_ALLOC;				\
    fail_stack.avail = 0;						\
  } while (0)


/* Double the size of FAIL_STACK, up to approximately `re_max_failures' items.

   Return 1 if succeeds, and 0 if either ran out of memory
   allocating space for it or it was already too large.  
   
   REGEX_REALLOCATE requires `destination' be declared.   */

#define DOUBLE_FAIL_STACK(fail_stack)					\
  ((fail_stack).size > re_max_failures * MAX_FAILURE_ITEMS		\
   ? 0									\
   : ((fail_stack).stack = (fail_stack_elt_t *)				\
        REGEX_REALLOCATE ((fail_stack).stack, 				\
          (fail_stack).size * sizeof (fail_stack_elt_t),		\
          ((fail_stack).size << 1) * sizeof (fail_stack_elt_t)),	\
									\
      (fail_stack).stack == NULL					\
      ? 0								\
      : ((fail_stack).size <<= 1, 					\
         1)))


/* Push PATTERN_OP on FAIL_STACK. 

   Return 1 if was able to do so and 0 if ran out of memory allocating
   space to do so.  */
#define PUSH_PATTERN_OP(pattern_op, fail_stack)				\
  ((FAIL_STACK_FULL ()							\
    && !DOUBLE_FAIL_STACK (fail_stack))					\
    ? 0									\
    : ((fail_stack).stack[(fail_stack).avail++] = pattern_op,		\
       1))

/* This pushes an item onto the failure stack.  Must be a four-byte
   value.  Assumes the variable `fail_stack'.  Probably should only
   be called from within `PUSH_FAILURE_POINT'.  */
#define PUSH_FAILURE_ITEM(item)						\
  fail_stack.stack[fail_stack.avail++] = (fail_stack_elt_t) item

/* The complement operation.  Assumes `fail_stack' is nonempty.  */
#define POP_FAILURE_ITEM() fail_stack.stack[--fail_stack.avail]

/* Used to omit pushing failure point id's when we're not debugging.  */
#ifdef DEBUG
#define DEBUG_PUSH PUSH_FAILURE_ITEM
#define DEBUG_POP(item_addr) *(item_addr) = POP_FAILURE_ITEM ()
#else
#define DEBUG_PUSH(item)
#define DEBUG_POP(item_addr)
#endif


/* Push the information about the state we will need
   if we ever fail back to it.  
   
   Requires variables fail_stack, regstart, regend, reg_info, and
   num_regs be declared.  DOUBLE_FAIL_STACK requires `destination' be
   declared.
   
   Does `return FAILURE_CODE' if runs out of memory.  */

#define PUSH_FAILURE_POINT(pattern_place, string_place, failure_code)	\
  do {									\
    char *destination;							\
    /* Must be int, so when we don't save any registers, the arithmetic	\
       of 0 + -1 isn't done as unsigned.  */				\
    int this_reg;							\
    									\
    DEBUG_STATEMENT (failure_id++);					\
    DEBUG_STATEMENT (nfailure_points_pushed++);				\
    DEBUG_PRINT2 ("\nPUSH_FAILURE_POINT #%u:\n", failure_id);		\
    DEBUG_PRINT2 ("  Before push, next avail: %d\n", (fail_stack).avail);\
    DEBUG_PRINT2 ("                     size: %d\n", (fail_stack).size);\
									\
    DEBUG_PRINT2 ("  slots needed: %d\n", NUM_FAILURE_ITEMS);		\
    DEBUG_PRINT2 ("     available: %d\n", REMAINING_AVAIL_SLOTS);	\
									\
    /* Ensure we have enough space allocated for what we will push.  */	\
    while (REMAINING_AVAIL_SLOTS < NUM_FAILURE_ITEMS)			\
      {									\
        if (!DOUBLE_FAIL_STACK (fail_stack))			\
          return failure_code;						\
									\
        DEBUG_PRINT2 ("\n  Doubled stack; size now: %d\n",		\
		       (fail_stack).size);				\
        DEBUG_PRINT2 ("  slots available: %d\n", REMAINING_AVAIL_SLOTS);\
      }									\
									\
    /* Push the info, starting with the registers.  */			\
    DEBUG_PRINT1 ("\n");						\
									\
    for (this_reg = lowest_active_reg; this_reg <= highest_active_reg;	\
         this_reg++)							\
      {									\
	DEBUG_PRINT2 ("  Pushing reg: %d\n", this_reg);			\
        DEBUG_STATEMENT (num_regs_pushed++);				\
									\
	DEBUG_PRINT2 ("    start: 0x%x\n", regstart[this_reg]);		\
        PUSH_FAILURE_ITEM (regstart[this_reg]);				\
                                                                        \
	DEBUG_PRINT2 ("    end: 0x%x\n", regend[this_reg]);		\
        PUSH_FAILURE_ITEM (regend[this_reg]);				\
									\
	DEBUG_PRINT2 ("    info: 0x%x\n      ", reg_info[this_reg]);	\
        DEBUG_PRINT2 (" match_null=%d",					\
                      REG_MATCH_NULL_STRING_P (reg_info[this_reg]));	\
        DEBUG_PRINT2 (" active=%d", IS_ACTIVE (reg_info[this_reg]));	\
        DEBUG_PRINT2 (" matched_something=%d",				\
                      MATCHED_SOMETHING (reg_info[this_reg]));		\
        DEBUG_PRINT2 (" ever_matched=%d",				\
                      EVER_MATCHED_SOMETHING (reg_info[this_reg]));	\
	DEBUG_PRINT1 ("\n");						\
        PUSH_FAILURE_ITEM (reg_info[this_reg].word);			\
      }									\
									\
    DEBUG_PRINT2 ("  Pushing  low active reg: %d\n", lowest_active_reg);\
    PUSH_FAILURE_ITEM (lowest_active_reg);				\
									\
    DEBUG_PRINT2 ("  Pushing high active reg: %d\n", highest_active_reg);\
    PUSH_FAILURE_ITEM (highest_active_reg);				\
									\
    DEBUG_PRINT2 ("  Pushing pattern 0x%x: ", pattern_place);		\
    DEBUG_PRINT_COMPILED_PATTERN (bufp, pattern_place, pend);		\
    PUSH_FAILURE_ITEM (pattern_place);					\
									\
    DEBUG_PRINT2 ("  Pushing string 0x%x: `", string_place);		\
    DEBUG_PRINT_DOUBLE_STRING (string_place, string1, size1, string2,   \
				 size2);				\
    DEBUG_PRINT1 ("'\n");						\
    PUSH_FAILURE_ITEM (string_place);					\
									\
    DEBUG_PRINT2 ("  Pushing failure id: %u\n", failure_id);		\
    DEBUG_PUSH (failure_id);						\
  } while (0)

/* This is the number of items that are pushed and popped on the stack
   for each register.  */
#define NUM_REG_ITEMS  3

/* Individual items aside from the registers.  */
#ifdef DEBUG
#define NUM_NONREG_ITEMS 5 /* Includes failure point id.  */
#else
#define NUM_NONREG_ITEMS 4
#endif

/* We push at most this many items on the stack.  */
#define MAX_FAILURE_ITEMS ((num_regs - 1) * NUM_REG_ITEMS + NUM_NONREG_ITEMS)

/* We actually push this many items.  */
#define NUM_FAILURE_ITEMS						\
  ((highest_active_reg - lowest_active_reg + 1) * NUM_REG_ITEMS 	\
    + NUM_NONREG_ITEMS)

/* How many items can still be added to the stack without overflowing it.  */
#define REMAINING_AVAIL_SLOTS ((fail_stack).size - (fail_stack).avail)


/* Pops what PUSH_FAIL_STACK pushes.

   We restore into the parameters, all of which should be lvalues:
     STR -- the saved data position.
     PAT -- the saved pattern position.
     LOW_REG, HIGH_REG -- the highest and lowest active registers.
     REGSTART, REGEND -- arrays of string positions.
     REG_INFO -- array of information about each subexpression.
   
   Also assumes the variables `fail_stack' and (if debugging), `bufp',
   `pend', `string1', `size1', `string2', and `size2'.  */

#define POP_FAILURE_POINT(str, pat, low_reg, high_reg, regstart, regend, reg_info)\
{									\
  DEBUG_STATEMENT (fail_stack_elt_t failure_id;)			\
  int this_reg;								\
  const unsigned char *string_temp;					\
									\
  assert (!FAIL_STACK_EMPTY ());					\
									\
  /* Remove failure points and point to how many regs pushed.  */	\
  DEBUG_PRINT1 ("POP_FAILURE_POINT:\n");				\
  DEBUG_PRINT2 ("  Before pop, next avail: %d\n", fail_stack.avail);	\
  DEBUG_PRINT2 ("                    size: %d\n", fail_stack.size);	\
									\
  assert (fail_stack.avail >= NUM_NONREG_ITEMS);			\
									\
  DEBUG_POP (&failure_id);						\
  DEBUG_PRINT2 ("  Popping failure id: %u\n", failure_id);		\
									\
  /* If the saved string location is NULL, it came from an		\
     on_failure_keep_string_jump opcode, and we want to throw away the	\
     saved NULL, thus retaining our current position in the string.  */	\
  string_temp = POP_FAILURE_ITEM ();					\
  if (string_temp != NULL)						\
    str = (const char *) string_temp;					\
									\
  DEBUG_PRINT2 ("  Popping string 0x%x: `", str);			\
  DEBUG_PRINT_DOUBLE_STRING (str, string1, size1, string2, size2);	\
  DEBUG_PRINT1 ("'\n");							\
									\
  pat = (unsigned char *) POP_FAILURE_ITEM ();				\
  DEBUG_PRINT2 ("  Popping pattern 0x%x: ", pat);			\
  DEBUG_PRINT_COMPILED_PATTERN (bufp, pat, pend);			\
									\
  /* Restore register info.  */						\
  high_reg = (unsigned) POP_FAILURE_ITEM ();				\
  DEBUG_PRINT2 ("  Popping high active reg: %d\n", high_reg);		\
									\
  low_reg = (unsigned) POP_FAILURE_ITEM ();				\
  DEBUG_PRINT2 ("  Popping  low active reg: %d\n", low_reg);		\
									\
  for (this_reg = high_reg; this_reg >= low_reg; this_reg--)		\
    {									\
      DEBUG_PRINT2 ("    Popping reg: %d\n", this_reg);			\
									\
      reg_info[this_reg].word = POP_FAILURE_ITEM ();			\
      DEBUG_PRINT2 ("      info: 0x%x\n", reg_info[this_reg]);		\
									\
      regend[this_reg] = (const char *) POP_FAILURE_ITEM ();		\
      DEBUG_PRINT2 ("      end: 0x%x\n", regend[this_reg]);		\
									\
      regstart[this_reg] = (const char *) POP_FAILURE_ITEM ();		\
      DEBUG_PRINT2 ("      start: 0x%x\n", regstart[this_reg]);		\
    }									\
									\
  DEBUG_STATEMENT (nfailure_points_popped++);				\
} /* POP_FAILURE_POINT */

/* re_compile_fastmap computes a ``fastmap'' for the compiled pattern in
   BUFP.  A fastmap records which of the (1 << BYTEWIDTH) possible
   characters can start a string that matches the pattern.  This fastmap
   is used by re_search to skip quickly over impossible starting points.

   The caller must supply the address of a (1 << BYTEWIDTH)-byte data
   area as BUFP->fastmap.
   
   We set the `fastmap', `fastmap_accurate', and `can_be_null' fields in
   the pattern buffer.

   Returns 0 if we succeed, -2 if an internal error.   */

int
re_compile_fastmap (bufp)
     struct re_pattern_buffer *bufp;
{
  int j, k;
  fail_stack_type fail_stack;
#ifndef REGEX_MALLOC
  char *destination;
#endif
  /* We don't push any register information onto the failure stack.  */
  unsigned num_regs = 0;
  
  register char *fastmap = bufp->fastmap;
  unsigned char *pattern = bufp->buffer;
  unsigned long size = bufp->used;
  const unsigned char *p = pattern;
  register unsigned char *pend = pattern + size;

  /* Assume that each path through the pattern can be null until
     proven otherwise.  We set this false at the bottom of switch
     statement, to which we get only if a particular path doesn't
     match the empty string.  */
  boolean path_can_be_null = true;

  /* We aren't doing a `succeed_n' to begin with.  */
  boolean succeed_n_p = false;

  assert (fastmap != NULL && p != NULL);
  
  INIT_FAIL_STACK ();
  bzero (fastmap, 1 << BYTEWIDTH);  /* Assume nothing's valid.  */
  bufp->fastmap_accurate = 1;	    /* It will be when we're done.  */
  bufp->can_be_null = 0;
      
  while (p != pend || !FAIL_STACK_EMPTY ())
    {
      if (_br(p == pend,0x478b100,4218,11))
        {
          bufp->can_be_null |= path_can_be_null;
          
          /* Reset for next path.  */
          path_can_be_null = true;
          
          p = fail_stack.stack[--fail_stack.avail];
	}

      /* We should never be about to go beyond the end of the pattern.  */
      assert (p < pend);
      
#ifdef SWITCH_ENUM_BUG
      switch ((int) ((re_opcode_t) *p++))
#else
      switch ((re_opcode_t) *p++)
#endif
	{

        /* I guess the idea here is to simply not bother with a fastmap
           if a backreference is used, since it's too hard to figure out
           the fastmap for the corresponding group.  Setting
           `can_be_null' stops `re_search_2' from using the fastmap, so
           that is all we do.  */
	case duplicate:
	  bufp->can_be_null = 1;
          return 0;


      /* Following are the cases which match a character.  These end
         with `break'.  */

	case exactn:
          fastmap[p[1]] = 1;
	  break;


        case charset:
          for (j = *p++ * BYTEWIDTH - 1; _br(j >= 0,0x478bb30,4257,42); j--)
	    if (_br(p[j / BYTEWIDTH] & (1 << (j % BYTEWIDTH)),0x478bdd0,4258,10))
              fastmap[j] = 1;
	  break;


	case charset_not:
	  /* Chars beyond end of map must be allowed.  */
	  for (j = *p * BYTEWIDTH; _br(j < (1 << BYTEWIDTH),0x478c1c8,4265,29); j++)
            fastmap[j] = 1;

	  for (j = *p++ * BYTEWIDTH - 1; _br(j >= 0,0x478c550,4268,35); j--)
	    if (_br(!(p[j / BYTEWIDTH] & (1 << (j % BYTEWIDTH))),0x478c838,4269,10))
              fastmap[j] = 1;
          break;


	case wordchar:
	  for (j = 0; _br(j < (1 << BYTEWIDTH),0x478cb70,4275,16); j++)
	    if (SYNTAX (j) == Sword)
	      fastmap[j] = 1;
	  break;


	case notwordchar:
	  for (j = 0; _br(j < (1 << BYTEWIDTH),0x478d018,4282,16); j++)
	    if (SYNTAX (j) != Sword)
	      fastmap[j] = 1;
	  break;


        case anychar:
          /* `.' matches anything ...  */
	  for (j = 0; _br(j < (1 << BYTEWIDTH),0x478d4e0,4290,16); j++)
            fastmap[j] = 1;

          /* ... except perhaps newline.  */
          if (_br(!(bufp->syntax & RE_DOT_NEWLINE),0x478da30,4294,15))
            fastmap['\n'] = 0;

          /* Return if we have already set `can_be_null'; if we have,
             then the fastmap is irrelevant.  Something's wrong here.  */
	  else if (_br(bufp->can_be_null,0x478dba8,4299,13))
	    return 0;

          /* Otherwise, have to check alternative paths.  */
	  break;


#ifdef emacs
        case syntaxspec:
	  k = *p++;
	  for (j = 0; j < (1 << BYTEWIDTH); j++)
	    if (SYNTAX (j) == (enum syntaxcode) k)
	      fastmap[j] = 1;
	  break;


	case notsyntaxspec:
	  k = *p++;
	  for (j = 0; j < (1 << BYTEWIDTH); j++)
	    if (SYNTAX (j) != (enum syntaxcode) k)
	      fastmap[j] = 1;
	  break;


      /* All cases after this match the empty string.  These end with
         `continue'.  */


	case before_dot:
	case at_dot:
	case after_dot:
          continue;
#endif /* not emacs */


        case no_op:
        case begline:
        case endline:
	case begbuf:
	case endbuf:
	case wordbound:
	case notwordbound:
	case wordbeg:
	case wordend:
        case push_dummy_failure:
          continue;


	case jump_n:
        case pop_failure_jump:
	case maybe_pop_jump:
	case jump:
        case jump_past_alt:
	case dummy_failure_jump:
          EXTRACT_NUMBER_AND_INCR (j, p);
	  p += j;	
	  if (_br(j > 0,0x478e8e0,4355,8))
	    continue;
            
          /* Jump backward implies we just went through the body of a
             loop and matched nothing.  Opcode jumped to should be
             `on_failure_jump' or `succeed_n'.  Just treat it like an
             ordinary jump.  For a * loop, it has pushed its failure
             point already; if so, discard that as redundant.  */
          if (_br((re_opcode_t) *p != on_failure_jump
	      && (re_opcode_t) *p != succeed_n,0x478eba8,4363,15))
	    continue;

          p++;
          EXTRACT_NUMBER_AND_INCR (j, p);
          p += j;		
	  
          /* If what's on the stack is where we are now, pop it.  */
          if (_br(!FAIL_STACK_EMPTY () 
	      && fail_stack.stack[fail_stack.avail - 1] == p,0x478f590,4372,15))
            fail_stack.avail--;

          continue;


        case on_failure_jump:
        case on_failure_keep_string_jump:
	handle_on_failure_jump:
          EXTRACT_NUMBER_AND_INCR (j, p);

          /* For some patterns, e.g., `(a?)?', `p+j' here points to the
             end of the pattern.  We don't want to push such a point,
             since when we restore it above, entering the switch will
             increment `p' past the end of the pattern.  We don't need
             to push such a point since we obviously won't find any more
             fastmap entries beyond `pend'.  Such a pattern can match
             the null string, though.  */
          if (_br(p + j < pend,0x478fe00,4391,15))
            {
              if (!PUSH_PATTERN_OP (p + j, fail_stack))
                return -2;
            }
          else
            bufp->can_be_null = 1;

          if (_br(succeed_n_p,0x4792c88,4399,15))
            {
              EXTRACT_NUMBER_AND_INCR (k, p);	/* Skip the n.  */
              succeed_n_p = false;
	    }

          continue;


	case succeed_n:
          /* Get to the number of times to succeed.  */
          p += 2;		

          /* Increment p past the n for when k != 0.  */
          EXTRACT_NUMBER_AND_INCR (k, p);
          if (_br(k == 0,0x47939a0,4414,15))
	    {
              p -= 4;
  	      succeed_n_p = true;  /* Spaghetti code alert.  */
              goto handle_on_failure_jump;
            }
          continue;


	case set_number_at:
          p += 4;
          continue;


	case start_memory:
        case stop_memory:
	  p += 2;
	  continue;


	default:
          abort (); /* We have listed all the cases.  */
        } /* switch *p++ */

      /* Getting here means we have found the possible starting
         characters for one path of the pattern -- and that the empty
         string does not match.  We need not follow this path further.
         Instead, look at the next alternative (remembered on the
         stack), or quit if no more.  The test at the top of the loop
         does these things.  */
      path_can_be_null = false;
      p = pend;
    } /* while p */

  /* Set `can_be_null' for the last path (also the first path, if the
     pattern is empty).  */
  bufp->can_be_null |= path_can_be_null;
  return 0;
} /* re_compile_fastmap */

/* Set REGS to hold NUM_REGS registers, storing them in STARTS and
   ENDS.  Subsequent matches using PATTERN_BUFFER and REGS will use
   this memory for recording register information.  STARTS and ENDS
   must be allocated using the malloc library routine, and must each
   be at least NUM_REGS * sizeof (regoff_t) bytes long.

   If NUM_REGS == 0, then subsequent matches should allocate their own
   register data.

   Unless this function is called, the first search or match using
   PATTERN_BUFFER will allocate its own register data, without
   freeing the old data.  */

void
re_set_registers (bufp, regs, num_regs, starts, ends)
    struct re_pattern_buffer *bufp;
    struct re_registers *regs;
    unsigned num_regs;
    regoff_t *starts, *ends;
{
  if (_br(num_regs,0x4794930,4474,7))
    {
      bufp->regs_allocated = REGS_REALLOCATE;
      regs->num_regs = num_regs;
      regs->start = starts;
      regs->end = ends;
    }
  else
    {
      bufp->regs_allocated = REGS_UNALLOCATED;
      regs->num_regs = 0;
      regs->start = regs->end = (regoff_t) 0;
    }
}

/* Searching routines.  */

/* Like re_search_2, below, but only one string is specified, and
   doesn't let you say where to stop matching. */

int
re_search (bufp, string, size, startpos, range, regs)
     struct re_pattern_buffer *bufp;
     const char *string;
     int size, startpos, range;
     struct re_registers *regs;
{
  return re_search_2 (bufp, NULL, 0, string, size, startpos, range, 
		      regs, size);
}


/* Using the compiled pattern in BUFP->buffer, first tries to match the
   virtual concatenation of STRING1 and STRING2, starting first at index
   STARTPOS, then at STARTPOS + 1, and so on.
   
   STRING1 and STRING2 have length SIZE1 and SIZE2, respectively.
   
   RANGE is how far to scan while trying to match.  RANGE = 0 means try
   only at STARTPOS; in general, the last start tried is STARTPOS +
   RANGE.
   
   In REGS, return the indices of the virtual concatenation of STRING1
   and STRING2 that matched the entire BUFP->buffer and its contained
   subexpressions.
   
   Do not consider matching one past the index STOP in the virtual
   concatenation of STRING1 and STRING2.

   We return either the position in the strings at which the match was
   found, -1 if no match, or -2 if error (such as failure
   stack overflow).  */

int
re_search_2 (bufp, string1, size1, string2, size2, startpos, range, regs, stop)
     struct re_pattern_buffer *bufp;
     const char *string1, *string2;
     int size1, size2;
     int startpos;
     int range;
     struct re_registers *regs;
     int stop;
{
  int val;
  register char *fastmap = bufp->fastmap;
  register char *translate = bufp->translate;
  int total_size = size1 + size2;
  int endpos = startpos + range;

  /* Check for out-of-range STARTPOS.  */
  if (_br(startpos < 0 || startpos > total_size,0x47964e0,4544,7))
    return -1;
    
  /* Fix up RANGE if it might eventually take us outside
     the virtual concatenation of STRING1 and STRING2.  */
  if (_br(endpos < -1,0x4796618,4549,7))
    range = -1 - startpos;
  else if (_br(endpos > total_size,0x47967b8,4551,12))
    range = total_size - startpos;

  /* If the search isn't to be a backwards one, don't waste time in a
     search for a pattern that must be anchored.  */
  if (_br(bufp->used > 0 && (re_opcode_t) bufp->buffer[0] == begbuf && range > 0,0x4796ca0,4556,7))
    {
      if (_br(startpos > 0,0x4796d28,4558,11))
	return -1;
      else
	range = 1;
    }

  /* Update the fastmap now if not correct already.  */
  if (_br(fastmap && !bufp->fastmap_accurate,0x4796f98,4565,7))
    if (_br(re_compile_fastmap (bufp) == -2,0x47970b0,4566,9))
      return -2;
  
  /* Loop through the string, looking for a place to start matching.  */
  for (;;)
    { 
      /* If a fastmap is supplied, skip quickly over characters that
         cannot be the start of a match.  If the pattern can match the
         null string, however, we don't need to skip characters; we want
         the first null string.  */
      if (_br(fastmap && startpos < total_size && !bufp->can_be_null,0x4797360,4576,11))
	{
	  if (_br(range > 0,0x47973e8,4578,8))	/* Searching forwards.  */
	    {
	      register const char *d;
	      register int lim = 0;
	      int irange = range;

              if (_br(startpos < size1 && startpos + range >= size1,0x47977d8,4584,19))
                lim = range - (size1 - startpos);

	      d = (startpos >= size1 ? string2 - size1 : string1) + startpos;
   
              /* Written out as an if-else to avoid testing `translate'
                 inside the loop.  */
	      if (_br(translate,0x4797c78,4591,12))
                while (_br(range > lim
                       && !fastmap[(unsigned char)
				   translate[(unsigned char) *d++]],0x4797f58,4592,24))
                  range--;
	      else
                while (_br(range > lim && !fastmap[(unsigned char) *d++],0x47981f0,4597,24))
                  range--;

	      startpos += irange - range;
	    }
	  else				/* Searching backwards.  */
	    {
	      register char c = (size1 == 0 || startpos >= size1
                                 ? string2[startpos - size1] 
                                 : string1[startpos]);

	      if (_br(!fastmap[(unsigned char) TRANSLATE (c)],0x4798be8,4608,12))
		goto advance;
	    }
	}

      /* If can't match the null string, and that's all we have left, fail.  */
      if (_br(range >= 0 && startpos == total_size && fastmap
          && !bufp->can_be_null,0x4798fc8,4614,11))
	return -1;

      val = re_match_2 (bufp, string1, size1, string2, size2,
	                startpos, regs, stop);
      if (_br(val >= 0,0x4799430,4620,11))
	return startpos;
        
      if (_br(val == -2,0x4799568,4623,11))
	return -2;

    advance:
      if (_br(!range,0x4799660,4627,11)) 
        break;
      else if (_br(range > 0,0x47996e8,4629,16)) 
        {
          range--; 
          startpos++;
        }
      else
        {
          range++; 
          startpos--;
        }
    }
  return -1;
} /* re_search_2 */

/* Declarations and macros for re_match_2.  */

static int bcmp_translate ();
static boolean alt_match_null_string_p (),
               common_op_match_null_string_p (),
               group_match_null_string_p ();

/* Structure for per-register (a.k.a. per-group) information.
   This must not be longer than one word, because we push this value
   onto the failure stack.  Other register information, such as the
   starting and ending positions (which are addresses), and the list of
   inner groups (which is a bits list) are maintained in separate
   variables.  
   
   We are making a (strictly speaking) nonportable assumption here: that
   the compiler will pack our bit fields into something that fits into
   the type of `word', i.e., is something that fits into one item on the
   failure stack.  */
typedef union
{
  fail_stack_elt_t word;
  struct
  {
      /* This field is one if this group can match the empty string,
         zero if not.  If not yet determined,  `MATCH_NULL_UNSET_VALUE'.  */
#define MATCH_NULL_UNSET_VALUE 3
    unsigned match_null_string_p : 2;
    unsigned is_active : 1;
    unsigned matched_something : 1;
    unsigned ever_matched_something : 1;
  } bits;
} register_info_type;

#define REG_MATCH_NULL_STRING_P(R)  ((R).bits.match_null_string_p)
#define IS_ACTIVE(R)  ((R).bits.is_active)
#define MATCHED_SOMETHING(R)  ((R).bits.matched_something)
#define EVER_MATCHED_SOMETHING(R)  ((R).bits.ever_matched_something)


/* Call this when have matched a real character; it sets `matched' flags
   for the subexpressions which we are currently inside.  Also records
   that those subexprs have matched.  */
#define SET_REGS_MATCHED()						\
  do									\
    {									\
      unsigned r;							\
      for (r = lowest_active_reg; r <= highest_active_reg; r++)		\
        {								\
          MATCHED_SOMETHING (reg_info[r])				\
            = EVER_MATCHED_SOMETHING (reg_info[r])			\
            = 1;							\
        }								\
    }									\
  while (0)


/* This converts PTR, a pointer into one of the search strings `string1'
   and `string2' into an offset from the beginning of that string.  */
#define POINTER_TO_OFFSET(ptr)						\
  (FIRST_STRING_P (ptr) ? (ptr) - string1 : (ptr) - string2 + size1)

/* Registers are set to a sentinel when they haven't yet matched.  */
#define REG_UNSET_VALUE ((char *) -1)
#define REG_UNSET(e) ((e) == REG_UNSET_VALUE)


/* Macros for dealing with the split strings in re_match_2.  */

#define MATCHING_IN_FIRST_STRING  (dend == end_match_1)

/* Call before fetching a character with *d.  This switches over to
   string2 if necessary.  */
#define PREFETCH()							\
  while (d == dend)						    	\
    {									\
      /* End of string2 => fail.  */					\
      if (dend == end_match_2) 						\
        goto fail;							\
      /* End of string1 => advance to string2.  */ 			\
      d = string2;						        \
      dend = end_match_2;						\
    }


/* Test if at very beginning or at very end of the virtual concatenation
   of `string1' and `string2'.  If only one string, it's `string2'.  */
#define AT_STRINGS_BEG(d) ((d) == (size1 ? string1 : string2) || !size2)
#define AT_STRINGS_END(d) ((d) == end2)	


/* Test if D points to a character which is word-constituent.  We have
   two special cases to check for: if past the end of string1, look at
   the first character in string2; and if before the beginning of
   string2, look at the last character in string1.  */
#define WORDCHAR_P(d)							\
  (SYNTAX ((d) == end1 ? *string2					\
           : (d) == string2 - 1 ? *(end1 - 1) : *(d))			\
   == Sword)

/* Test if the character before D and the one at D differ with respect
   to being word-constituent.  */
#define AT_WORD_BOUNDARY(d)						\
  (AT_STRINGS_BEG (d) || AT_STRINGS_END (d)				\
   || WORDCHAR_P (d - 1) != WORDCHAR_P (d))


/* Free everything we malloc.  */
#ifdef REGEX_MALLOC
#define FREE_VAR(var) if (var) free (var); var = NULL
#define FREE_VARIABLES()						\
  do {									\
    FREE_VAR (fail_stack.stack);					\
    FREE_VAR (regstart);						\
    FREE_VAR (regend);							\
    FREE_VAR (old_regstart);						\
    FREE_VAR (old_regend);						\
    FREE_VAR (best_regstart);						\
    FREE_VAR (best_regend);						\
    FREE_VAR (reg_info);						\
    FREE_VAR (reg_dummy);						\
    FREE_VAR (reg_info_dummy);						\
  } while (0)
#else /* not REGEX_MALLOC */
/* Some MIPS systems (at least) want this to free alloca'd storage.  */
#define FREE_VARIABLES() alloca (0)
#endif /* not REGEX_MALLOC */


/* These values must meet several constraints.  They must not be valid
   register values; since we have a limit of 255 registers (because
   we use only one byte in the pattern for the register number), we can
   use numbers larger than 255.  They must differ by 1, because of
   NUM_FAILURE_ITEMS above.  And the value for the lowest register must
   be larger than the value for the highest register, so we do not try
   to actually save any registers when none are active.  */
#define NO_HIGHEST_ACTIVE_REG (1 << BYTEWIDTH)
#define NO_LOWEST_ACTIVE_REG (NO_HIGHEST_ACTIVE_REG + 1)

/* Matching routines.  */

#ifndef emacs   /* Emacs never uses this.  */
/* re_match is like re_match_2 except it takes only a single string.  */

int
re_match (bufp, string, size, pos, regs)
     struct re_pattern_buffer *bufp;
     const char *string;
     int size, pos;
     struct re_registers *regs;
 {
  return re_match_2 (bufp, NULL, 0, string, size, pos, regs, size); 
}
#endif /* not emacs */


/* re_match_2 matches the compiled pattern in BUFP against the
   the (virtual) concatenation of STRING1 and STRING2 (of length SIZE1
   and SIZE2, respectively).  We start matching at POS, and stop
   matching at STOP.
   
   If REGS is non-null and the `no_sub' field of BUFP is nonzero, we
   store offsets for the substring each group matched in REGS.  See the
   documentation for exactly how many groups we fill.

   We return -1 if no match, -2 if an internal error (such as the
   failure stack overflowing).  Otherwise, we return the length of the
   matched substring.  */

int
re_match_2 (bufp, string1, size1, string2, size2, pos, regs, stop)
     struct re_pattern_buffer *bufp;
     const char *string1, *string2;
     int size1, size2;
     int pos;
     struct re_registers *regs;
     int stop;
{
  /* General temporaries.  */
  int mcnt;
  unsigned char *p1;

  /* Just past the end of the corresponding string.  */
  const char *end1, *end2;

  /* Pointers into string1 and string2, just past the last characters in
     each to consider matching.  */
  const char *end_match_1, *end_match_2;

  /* Where we are in the data, and the end of the current string.  */
  const char *d, *dend;
  
  /* Where we are in the pattern, and the end of the pattern.  */
  unsigned char *p = bufp->buffer;
  register unsigned char *pend = p + bufp->used;

  /* We use this to map every character in the string.  */
  char *translate = bufp->translate;

  /* Failure point stack.  Each place that can handle a failure further
     down the line pushes a failure point on this stack.  It consists of
     restart, regend, and reg_info for all registers corresponding to
     the subexpressions we're currently inside, plus the number of such
     registers, and, finally, two char *'s.  The first char * is where
     to resume scanning the pattern; the second one is where to resume
     scanning the strings.  If the latter is zero, the failure point is
     a ``dummy''; if a failure happens and the failure point is a dummy,
     it gets discarded and the next next one is tried.  */
  fail_stack_type fail_stack;
#ifdef DEBUG
  static unsigned failure_id = 0;
  unsigned nfailure_points_pushed = 0, nfailure_points_popped = 0;
#endif

  /* We fill all the registers internally, independent of what we
     return, for use in backreferences.  The number here includes
     an element for register zero.  */
  unsigned num_regs = bufp->re_nsub + 1;
  
  /* The currently active registers.  */
  unsigned lowest_active_reg = NO_LOWEST_ACTIVE_REG;
  unsigned highest_active_reg = NO_HIGHEST_ACTIVE_REG;

  /* Information on the contents of registers. These are pointers into
     the input strings; they record just what was matched (on this
     attempt) by a subexpression part of the pattern, that is, the
     regnum-th regstart pointer points to where in the pattern we began
     matching and the regnum-th regend points to right after where we
     stopped matching the regnum-th subexpression.  (The zeroth register
     keeps track of what the whole pattern matches.)  */
  const char **regstart, **regend;

  /* If a group that's operated upon by a repetition operator fails to
     match anything, then the register for its start will need to be
     restored because it will have been set to wherever in the string we
     are when we last see its open-group operator.  Similarly for a
     register's end.  */
  const char **old_regstart, **old_regend;

  /* The is_active field of reg_info helps us keep track of which (possibly
     nested) subexpressions we are currently in. The matched_something
     field of reg_info[reg_num] helps us tell whether or not we have
     matched any of the pattern so far this time through the reg_num-th
     subexpression.  These two fields get reset each time through any
     loop their register is in.  */
  register_info_type *reg_info; 

  /* The following record the register info as found in the above
     variables when we find a match better than any we've seen before. 
     This happens as we backtrack through the failure points, which in
     turn happens only if we have not yet matched the entire string. */
  unsigned best_regs_set = false;
  const char **best_regstart, **best_regend;
  
  /* Logically, this is `best_regend[0]'.  But we don't want to have to
     allocate space for that if we're not allocating space for anything
     else (see below).  Also, we never need info about register 0 for
     any of the other register vectors, and it seems rather a kludge to
     treat `best_regend' differently than the rest.  So we keep track of
     the end of the best match so far in a separate variable.  We
     initialize this to NULL so that when we backtrack the first time
     and need to test it, it's not garbage.  */
  const char *match_end = NULL;

  /* Used when we pop values we don't care about.  */
  const char **reg_dummy;
  register_info_type *reg_info_dummy;

#ifdef DEBUG
  /* Counts the total number of registers pushed.  */
  unsigned num_regs_pushed = 0; 	
#endif

  DEBUG_PRINT1 ("\n\nEntering re_match_2.\n");
  
  INIT_FAIL_STACK ();
  
  /* Do not bother to initialize all the register variables if there are
     no groups in the pattern, as it takes a fair amount of time.  If
     there are groups, we include space for register 0 (the whole
     pattern), even though we never use it, since it simplifies the
     array indexing.  We should fix this.  */
  if (_br(bufp->re_nsub,0x47a2700,4924,7))
    {
      regstart = REGEX_TALLOC (num_regs, const char *);
      regend = REGEX_TALLOC (num_regs, const char *);
      old_regstart = REGEX_TALLOC (num_regs, const char *);
      old_regend = REGEX_TALLOC (num_regs, const char *);
      best_regstart = REGEX_TALLOC (num_regs, const char *);
      best_regend = REGEX_TALLOC (num_regs, const char *);
      reg_info = REGEX_TALLOC (num_regs, register_info_type);
      reg_dummy = REGEX_TALLOC (num_regs, const char *);
      reg_info_dummy = REGEX_TALLOC (num_regs, register_info_type);

      if (_br(!(regstart && regend && old_regstart && old_regend && reg_info 
            && best_regstart && best_regend && reg_dummy && reg_info_dummy),0x47a4078,4936,11)) 
        {
          FREE_VARIABLES ();
          return -2;
        }
    }
#ifdef REGEX_MALLOC
  else
    {
      /* We must initialize all our variables to NULL, so that
         `FREE_VARIABLES' doesn't try to free them.  */
      regstart = regend = old_regstart = old_regend = best_regstart
        = best_regend = reg_dummy = NULL;
      reg_info = reg_info_dummy = (register_info_type *) NULL;
    }
#endif /* REGEX_MALLOC */

  /* The starting position is bogus.  */
  if (_br(pos < 0 || pos > size1 + size2,0x47a4430,4955,7))
    {
      FREE_VARIABLES ();
      return -1;
    }
    
  /* Initialize subexpression text positions to -1 to mark ones that no
     start_memory/stop_memory has been seen for. Also initialize the
     register information struct.  */
  for (mcnt = 1; _br(mcnt < num_regs,0x47a46c0,4964,18); mcnt++)
    {
      regstart[mcnt] = regend[mcnt] 
        = old_regstart[mcnt] = old_regend[mcnt] = REG_UNSET_VALUE;
        
      REG_MATCH_NULL_STRING_P (reg_info[mcnt]) = MATCH_NULL_UNSET_VALUE;
      IS_ACTIVE (reg_info[mcnt]) = 0;
      MATCHED_SOMETHING (reg_info[mcnt]) = 0;
      EVER_MATCHED_SOMETHING (reg_info[mcnt]) = 0;
    }
  
  /* We move `string1' into `string2' if the latter's empty -- but not if
     `string1' is null.  */
  if (size2 == 0 && string1 != NULL)
    {
      string2 = string1;
      size2 = size1;
      string1 = 0;
      size1 = 0;
    }
  end1 = string1 + size1;
  end2 = string2 + size2;

  /* Compute where to stop matching, within the two strings.  */
  if (_br(stop <= size1,0x47a5d58,4988,7))
    {
      end_match_1 = string1 + stop;
      end_match_2 = string2;
    }
  else
    {
      end_match_1 = end1;
      end_match_2 = string2 + stop - size1;
    }

  /* `p' scans through the pattern as `d' scans through the data. 
     `dend' is the end of the input string that `d' points within.  `d'
     is advanced into the following input string whenever necessary, but
     this happens before fetching; therefore, at the beginning of the
     loop, `d' can be pointing at the end of a string, but it cannot
     equal `string2'.  */
  if (_br(size1 > 0 && pos <= size1,0x47a62b0,5005,7))
    {
      d = string1 + pos;
      dend = end_match_1;
    }
  else
    {
      d = string2 + pos - size1;
      dend = end_match_2;
    }

  DEBUG_PRINT1 ("The compiled pattern is: ");
  DEBUG_PRINT_COMPILED_PATTERN (bufp, p, pend);
  DEBUG_PRINT1 ("The string to match is: `");
  DEBUG_PRINT_DOUBLE_STRING (d, string1, size1, string2, size2);
  DEBUG_PRINT1 ("'\n");
  
  /* This loops over pattern commands.  It exits by returning from the
     function if the match is complete, or it drops through if the match
     fails at this starting point in the input data.  */
  for (;;)
    {
      DEBUG_PRINT2 ("\n0x%x: ", p);

      if (_br(p == pend,0x47a67b8,5029,11))
	{ /* End of pattern means we might have succeeded.  */
          DEBUG_PRINT1 ("end of pattern ... ");
          
	  /* If we haven't matched the entire string, and we want the
             longest match, try backtracking.  */
          if (_br(d != end_match_2,0x47a6870,5035,15))
	    {
              DEBUG_PRINT1 ("backtracking.\n");
              
              if (!FAIL_STACK_EMPTY ())
                { /* More failure points to try.  */
                  boolean same_str_p = (FIRST_STRING_P (match_end) 
	        	                == MATCHING_IN_FIRST_STRING);

                  /* If exceeds best match so far, save it.  */
                  if (_br(!best_regs_set
                      || (same_str_p && d > match_end)
                      || (!same_str_p && !MATCHING_IN_FIRST_STRING),0x47a7398,5045,23))
                    {
                      best_regs_set = true;
                      match_end = d;
                      
                      DEBUG_PRINT1 ("\nSAVING match as best so far.\n");
                      
                      for (mcnt = 1; _br(mcnt < num_regs,0x47a75f0,5054,38); mcnt++)
                        {
                          best_regstart[mcnt] = regstart[mcnt];
                          best_regend[mcnt] = regend[mcnt];
                        }
                    }
                  goto fail;	       
                }

              /* If no failure points, don't restore garbage.  */
              else if (_br(best_regs_set,0x47a7b08,5064,24))   
                {
  	        restore_best_regs:
                  /* Restore best match.  It may happen that `dend ==
                     end_match_1' while the restored d is in string2.
                     For example, the pattern `x.*y.*z' against the
                     strings `x-' and `y-z-', if the two strings are
                     not consecutive in memory.  */
                  DEBUG_PRINT1 ("Restoring best registers.\n");
                  
                  d = match_end;
                  dend = ((d >= string1 && d <= end1)
		           ? end_match_1 : end_match_2);

		  for (mcnt = 1; _br(mcnt < num_regs,0x47a7fe0,5078,20); mcnt++)
		    {
		      regstart[mcnt] = best_regstart[mcnt];
		      regend[mcnt] = best_regend[mcnt];
		    }
                }
            } /* d != end_match_2 */

          DEBUG_PRINT1 ("Accepting match.\n");

          /* If caller wants register contents data back, do it.  */
          if (_br(regs && !bufp->no_sub,0x47a85d8,5089,15))
	    {
              /* Have the register data arrays been allocated?  */
              if (bufp->regs_allocated == REGS_UNALLOCATED)
                { /* No.  So allocate them with malloc.  We need one
                     extra element beyond `num_regs' for the `-1' marker
                     GNU code uses.  */
                  regs->num_regs = MAX (RE_NREGS, num_regs + 1);
                  regs->start = TALLOC (regs->num_regs, regoff_t);
                  regs->end = TALLOC (regs->num_regs, regoff_t);
                  if (regs->start == NULL || regs->end == NULL)
                    return -2;
                  bufp->regs_allocated = REGS_REALLOCATE;
                }
              else if (bufp->regs_allocated == REGS_REALLOCATE)
                { /* Yes.  If we need more elements than were already
                     allocated, reallocate them.  If we need fewer, just
                     leave it alone.  */
                  if (_br(regs->num_regs < num_regs + 1,0x47a9600,5107,23))
                    {
                      regs->num_regs = num_regs + 1;
                      RETALLOC (regs->start, regs->num_regs, regoff_t);
                      RETALLOC (regs->end, regs->num_regs, regoff_t);
                      if (regs->start == NULL || regs->end == NULL)
                        return -2;
                    }
                }
              else
		{
		  /* These braces fend off a "empty body in an else-statement"
		     warning under GCC when assert expands to nothing.  */
		  assert (bufp->regs_allocated == REGS_FIXED);
		}

              /* Convert the pointer data in `regstart' and `regend' to
                 indices.  Register zero has to be set differently,
                 since we haven't kept track of any info for it.  */
              if (_br(regs->num_regs > 0,0x47aa398,5126,19))
                {
                  regs->start[0] = pos;
                  regs->end[0] = (MATCHING_IN_FIRST_STRING ? d - string1
			          : d - string2 + size1);
                }
              
              /* Go through the first `min (num_regs, regs->num_regs)'
                 registers, since that is all we initialized.  */
	      for (mcnt = 1; mcnt < MIN (num_regs, regs->num_regs); mcnt++)
		{
                  if (REG_UNSET (regstart[mcnt]) || REG_UNSET (regend[mcnt]))
                    regs->start[mcnt] = regs->end[mcnt] = -1;
                  else
                    {
		      regs->start[mcnt] = POINTER_TO_OFFSET (regstart[mcnt]);
                      regs->end[mcnt] = POINTER_TO_OFFSET (regend[mcnt]);
                    }
		}
              
              /* If the regs structure we return has more elements than
                 were in the pattern, set the extra elements to -1.  If
                 we (re)allocated the registers, this is the case,
                 because we always allocate enough to have at least one
                 -1 at the end.  */
              for (mcnt = num_regs; _br(mcnt < regs->num_regs,0x47ac688,5151,37); mcnt++)
                regs->start[mcnt] = regs->end[mcnt] = -1;
	    } /* regs && !bufp->no_sub */

          FREE_VARIABLES ();
          DEBUG_PRINT4 ("%u failure points pushed, %u popped (%u remain).\n",
                        nfailure_points_pushed, nfailure_points_popped,
                        nfailure_points_pushed - nfailure_points_popped);
          DEBUG_PRINT2 ("%u registers pushed.\n", num_regs_pushed);

          mcnt = d - pos - (MATCHING_IN_FIRST_STRING 
			    ? string1 
			    : string2 - size1);

          DEBUG_PRINT2 ("Returning %d from re_match_2.\n", mcnt);

          return mcnt;
        }

      /* Otherwise match next pattern command.  */
#ifdef SWITCH_ENUM_BUG
      switch ((int) ((re_opcode_t) *p++))
#else
      switch ((re_opcode_t) *p++)
#endif
	{
        /* Ignore these.  Used to ignore the n of succeed_n's which
           currently have n == 0.  */
        case no_op:
          DEBUG_PRINT1 ("EXECUTING no_op.\n");
          break;


        /* Match the next n pattern characters exactly.  The following
           byte in the pattern defines n, and the n bytes after that
           are the characters to match.  */
	case exactn:
	  mcnt = *p++;
          DEBUG_PRINT2 ("EXECUTING exactn %d.\n", mcnt);

          /* This is written out as an if-else so we don't waste time
             testing `translate' inside the loop.  */
          if (_br(translate,0x47ad220,5193,15))
	    {
	      do
		{
		  PREFETCH ();
		  if (_br(translate[(unsigned char) *d++] != (char) *p++,0x47ad798,5198,9))
                    goto fail;
		}
	      while (--mcnt);
	    }
	  else
	    {
	      do
		{
		  PREFETCH ();
		  if (_br(*d++ != (char) *p++,0x47add50,5208,9)) goto fail;
		}
	      while (--mcnt);
	    }
	  SET_REGS_MATCHED ();
          break;


        /* Match any character except possibly a newline or a null.  */
	case anychar:
          DEBUG_PRINT1 ("EXECUTING anychar.\n");

          PREFETCH ();

          if (_br((!(bufp->syntax & RE_DOT_NEWLINE) && TRANSLATE (*d) == '\n')
              || (bufp->syntax & RE_DOT_NOT_NULL && TRANSLATE (*d) == '\000'),0x47af700,5222,15))
	    goto fail;

          SET_REGS_MATCHED ();
          DEBUG_PRINT2 ("  Matched `%d'.\n", *d);
          d++;
	  break;


	case charset:
	case charset_not:
	  {
	    register unsigned char c;
	    boolean not = (re_opcode_t) *(p - 1) == charset_not;

            DEBUG_PRINT2 ("EXECUTING charset%s.\n", not ? "_not" : "");

	    PREFETCH ();
	    c = TRANSLATE (*d); /* The character to match.  */

            /* Cast to `unsigned' instead of `unsigned char' in case the
               bit list is a full 32 bytes long.  */
	    if (_br(c < (unsigned) (*p * BYTEWIDTH)
		&& p[1 + c / BYTEWIDTH] & (1 << (c % BYTEWIDTH)),0x47b0c98,5245,10))
	      not = !not;

	    p += 1 + *p;

	    if (_br(!not,0x47b0f38,5251,10)) goto fail;
            
	    SET_REGS_MATCHED ();
            d++;
	    break;
	  }


        /* The beginning of a group is represented by start_memory.
           The arguments are the register number in the next byte, and the
           number of groups inner to this one in the next.  The text
           matched within the group is recorded (in the internal
           registers data structure) under the register number.  */
        case start_memory:
	  DEBUG_PRINT3 ("EXECUTING start_memory %d (%d):\n", *p, p[1]);

          /* Find out if this group can match the empty string.  */
	  p1 = p;		/* To send to group_match_null_string_p.  */
          
          if (REG_MATCH_NULL_STRING_P (reg_info[*p]) == MATCH_NULL_UNSET_VALUE)
            REG_MATCH_NULL_STRING_P (reg_info[*p]) 
              = group_match_null_string_p (&p1, pend, reg_info);

          /* Save the position in the string where we were the last time
             we were at this open-group operator in case the group is
             operated upon by a repetition operator, e.g., with `(a*)*b'
             against `ab'; then we want to ignore where we are now in
             the string in case this attempt to match fails.  */
          old_regstart[*p] = REG_MATCH_NULL_STRING_P (reg_info[*p])
                             ? REG_UNSET (regstart[*p]) ? d : regstart[*p]
                             : regstart[*p];
	  DEBUG_PRINT2 ("  old_regstart: %d\n", 
			 POINTER_TO_OFFSET (old_regstart[*p]));

          regstart[*p] = d;
	  DEBUG_PRINT2 ("  regstart: %d\n", POINTER_TO_OFFSET (regstart[*p]));

          IS_ACTIVE (reg_info[*p]) = 1;
          MATCHED_SOMETHING (reg_info[*p]) = 0;
          
          /* This is the new highest active register.  */
          highest_active_reg = *p;
          
          /* If nothing was active before, this is the new lowest active
             register.  */
          if (lowest_active_reg == NO_LOWEST_ACTIVE_REG)
            lowest_active_reg = *p;

          /* Move past the register number and inner group count.  */
          p += 2;
          break;


        /* The stop_memory opcode represents the end of a group.  Its
           arguments are the same as start_memory's: the register
           number, and the number of inner groups.  */
	case stop_memory:
	  DEBUG_PRINT3 ("EXECUTING stop_memory %d (%d):\n", *p, p[1]);
             
          /* We need to save the string position the last time we were at
             this close-group operator in case the group is operated
             upon by a repetition operator, e.g., with `((a*)*(b*)*)*'
             against `aba'; then we want to ignore where we are now in
             the string in case this attempt to match fails.  */
          old_regend[*p] = REG_MATCH_NULL_STRING_P (reg_info[*p])
                           ? REG_UNSET (regend[*p]) ? d : regend[*p]
			   : regend[*p];
	  DEBUG_PRINT2 ("      old_regend: %d\n", 
			 POINTER_TO_OFFSET (old_regend[*p]));

          regend[*p] = d;
	  DEBUG_PRINT2 ("      regend: %d\n", POINTER_TO_OFFSET (regend[*p]));

          /* This register isn't active anymore.  */
          IS_ACTIVE (reg_info[*p]) = 0;
          
          /* If this was the only register active, nothing is active
             anymore.  */
          if (_br(lowest_active_reg == highest_active_reg,0x47b3988,5329,15))
            {
              lowest_active_reg = NO_LOWEST_ACTIVE_REG;
              highest_active_reg = NO_HIGHEST_ACTIVE_REG;
            }
          else
            { /* We must scan for the new highest active register, since
                 it isn't necessarily one less than now: consider
                 (a(b)c(d(e)f)g).  When group 3 ends, after the f), the
                 new highest active register is 1.  */
              unsigned char r = *p - 1;
              while (r > 0 && !IS_ACTIVE (reg_info[r]))
                r--;
              
              /* If we end up at register zero, that means that we saved
                 the registers as the result of an `on_failure_jump', not
                 a `start_memory', and we jumped to past the innermost
                 `stop_memory'.  For example, in ((.)*) we save
                 registers 1 and 2 as a result of the *, but when we pop
                 back to the second ), we are at the stop_memory 1.
                 Thus, nothing is active.  */
	      if (_br(r == 0,0x47b40d8,5350,12))
                {
                  lowest_active_reg = NO_LOWEST_ACTIVE_REG;
                  highest_active_reg = NO_HIGHEST_ACTIVE_REG;
                }
              else
                highest_active_reg = r;
            }
          
          /* If just failed to match something this time around with a
             group that's operated on by a repetition operator, try to
             force exit from the ``loop'', and restore the register
             information for this group that we had before trying this
             last match.  */
          if (_br((!MATCHED_SOMETHING (reg_info[*p])
               || (re_opcode_t) p[-3] == start_memory)
	      && (p + 2) < pend,0x47b4950,5364,15))              
            {
              boolean is_a_jump_n = false;
              
              p1 = p + 2;
              mcnt = 0;
              switch ((re_opcode_t) *p1++)
                {
                  case jump_n:
		    is_a_jump_n = true;
                  case pop_failure_jump:
		  case maybe_pop_jump:
		  case jump:
		  case dummy_failure_jump:
                    EXTRACT_NUMBER_AND_INCR (mcnt, p1);
		    if (_br(is_a_jump_n,0x47b5698,5381,11))
		      p1 += 2;
                    break;
                  
                  default:
                    /* do nothing */ ;
                }
	      p1 += mcnt;
        
              /* If the next operation is a jump backwards in the pattern
	         to an on_failure_jump right before the start_memory
                 corresponding to this stop_memory, exit from the loop
                 by forcing a failure after pushing on the stack the
                 on_failure_jump's jump in the pattern, and d.  */
              if (_br(mcnt < 0 && (re_opcode_t) *p1 == on_failure_jump
                  && (re_opcode_t) p1[3] == start_memory && p1[4] == *p,0x47b5dc8,5395,19))
		{
                  /* If this group ever matched anything, then restore
                     what its registers were before trying this last
                     failed match, e.g., with `(a*)*b' against `ab' for
                     regstart[1], and, e.g., with `((a*)*(b*)*)*'
                     against `aba' for regend[3].
                     
                     Also restore the registers for inner groups for,
                     e.g., `((a*)(b*))*' against `aba' (register 3 would
                     otherwise get trashed).  */
                     
                  if (EVER_MATCHED_SOMETHING (reg_info[*p]))
		    {
		      unsigned r; 
        
                      EVER_MATCHED_SOMETHING (reg_info[*p]) = 0;
                      
		      /* Restore this and inner groups' (if any) registers.  */
                      for (r = *p; _br(r < *p + *(p + 1),0x47b6500,5415,36); r++)
                        {
                          regstart[r] = old_regstart[r];

                          /* xx why this test?  */
                          if (_br((int) old_regend[r] >= (int) regstart[r],0x47b6900,5420,31))
                            regend[r] = old_regend[r];
                        }     
                    }
		  p1++;
                  EXTRACT_NUMBER_AND_INCR (mcnt, p1);
                  PUSH_FAILURE_POINT (p1 + mcnt, d, -2);

                  goto fail;
                }
            }
          
          /* Move past the register number and the inner group count.  */
          p += 2;
          break;


	/* \<digit> has been turned into a `duplicate' command which is
           followed by the numeric value of <digit> as the register number.  */
        case duplicate:
	  {
	    register const char *d2, *dend2;
	    int regno = *p++;   /* Get which register to match against.  */
	    DEBUG_PRINT2 ("EXECUTING duplicate %d.\n", regno);

	    /* Can't back reference a group which we've never matched.  */
            if (REG_UNSET (regstart[regno]) || REG_UNSET (regend[regno]))
              goto fail;
              
            /* Where in input to try to start matching.  */
            d2 = regstart[regno];
            
            /* Where to stop matching; if both the place to start and
               the place to stop matching are in the same string, then
               set to the place to stop, otherwise, for now have to use
               the end of the first string.  */

            dend2 = ((FIRST_STRING_P (regstart[regno]) 
		      == FIRST_STRING_P (regend[regno]))
		     ? regend[regno] : end_match_1);
	    for (;;)
	      {
		/* If necessary, advance to next segment in register
                   contents.  */
		while (_br(d2 == dend2,0x47befa8,5464,10))
		  {
		    if (_br(dend2 == end_match_2,0x47bf050,5466,11)) break;
		    if (_br(dend2 == regend[regno],0x47bf1b8,5467,11)) break;

                    /* End of string1 => advance to string2. */
                    d2 = string2;
                    dend2 = regend[regno];
		  }
		/* At end of register contents => success */
		if (_br(d2 == dend2,0x47bf498,5474,7)) break;

		/* If necessary, advance to next segment in data.  */
		PREFETCH ();

		/* How many characters left in this segment to match.  */
		mcnt = dend - d;
                
		/* Want how many consecutive characters we can match in
                   one shot, so, if necessary, adjust the count.  */
                if (_br(mcnt > dend2 - d2,0x47bfa50,5484,21))
		  mcnt = dend2 - d2;
                  
		/* Compare that many; failure if mismatch, else move
                   past them.  */
		if (translate 
                    ? bcmp_translate (d, d2, mcnt, translate) 
                    : bcmp (d, d2, mcnt))
		  goto fail;
		d += mcnt, d2 += mcnt;
	      }
	  }
	  break;


        /* begline matches the empty string at the beginning of the string
           (unless `not_bol' is set in `bufp'), and, if
           `newline_anchor' is set, after newlines.  */
	case begline:
          DEBUG_PRINT1 ("EXECUTING begline.\n");
          
          if (AT_STRINGS_BEG (d))
            {
              if (_br(!bufp->not_bol,0x47c05f8,5507,19)) break;
            }
          else if (_br(d[-1] == '\n' && bufp->newline_anchor,0x47c0838,5509,20))
            {
              break;
            }
          /* In all other cases, we fail.  */
          goto fail;


        /* endline is the dual of begline.  */
	case endline:
          DEBUG_PRINT1 ("EXECUTING endline.\n");

          if (AT_STRINGS_END (d))
            {
              if (_br(!bufp->not_eol,0x47c0af8,5523,19)) break;
            }
          
          /* We have to ``prefetch'' the next character.  */
          else if (_br((d == end1 ? *string2 : *d) == '\n'
                   && bufp->newline_anchor,0x47c0e78,5527,20))
            {
              break;
            }
          goto fail;


	/* Match at the very beginning of the data.  */
        case begbuf:
          DEBUG_PRINT1 ("EXECUTING begbuf.\n");
          if (AT_STRINGS_BEG (d))
            break;
          goto fail;


	/* Match at the very end of the data.  */
        case endbuf:
          DEBUG_PRINT1 ("EXECUTING endbuf.\n");
	  if (AT_STRINGS_END (d))
	    break;
          goto fail;


        /* on_failure_keep_string_jump is used to optimize `.*\n'.  It
           pushes NULL as the value for the string on the stack.  Then
           `pop_failure_point' will keep the current value for the
           string, instead of restoring it.  To see why, consider
           matching `foo\nbar' against `.*\n'.  The .* matches the foo;
           then the . fails against the \n.  But the next thing we want
           to do is match the \n against the \n; if we restored the
           string value, we would be back at the foo.
           
           Because this is used only in specific cases, we don't need to
           check all the things that `on_failure_jump' does, to make
           sure the right things get saved on the stack.  Hence we don't
           share its code.  The only reason to push anything on the
           stack at all is that otherwise we would have to change
           `anychar's code to do something besides goto fail in this
           case; that seems worse than this.  */
        case on_failure_keep_string_jump:
          DEBUG_PRINT1 ("EXECUTING on_failure_keep_string_jump");
          
          EXTRACT_NUMBER_AND_INCR (mcnt, p);
          DEBUG_PRINT3 (" %d (to 0x%x):\n", mcnt, p + mcnt);

          PUSH_FAILURE_POINT (p + mcnt, NULL, -2);
          break;


	/* Uses of on_failure_jump:
        
           Each alternative starts with an on_failure_jump that points
           to the beginning of the next alternative.  Each alternative
           except the last ends with a jump that in effect jumps past
           the rest of the alternatives.  (They really jump to the
           ending jump of the following alternative, because tensioning
           these jumps is a hassle.)

           Repeats start with an on_failure_jump that points past both
           the repetition text and either the following jump or
           pop_failure_jump back to this on_failure_jump.  */
	case on_failure_jump:
        on_failure:
          DEBUG_PRINT1 ("EXECUTING on_failure_jump");

          EXTRACT_NUMBER_AND_INCR (mcnt, p);
          DEBUG_PRINT3 (" %d (to 0x%x)", mcnt, p + mcnt);

          /* If this on_failure_jump comes right before a group (i.e.,
             the original * applied to a group), save the information
             for that group and all inner ones, so that if we fail back
             to this point, the group's information will be correct.
             For example, in \(a*\)*\1, we need the preceding group,
             and in \(\(a*\)b*\)\2, we need the inner group.  */

          /* We can't use `p' to check ahead because we push
             a failure point to `p + mcnt' after we do this.  */
          p1 = p;

          /* We need to skip no_op's before we look for the
             start_memory in case this on_failure_jump is happening as
             the result of a completed succeed_n, as in \(a\)\{1,3\}b\1
             against aba.  */
          while (_br(p1 < pend && (re_opcode_t) *p1 == no_op,0x47c4ad8,5611,18))
            p1++;

          if (_br(p1 < pend && (re_opcode_t) *p1 == start_memory,0x47c4d40,5614,15))
            {
              /* We have a new highest active register now.  This will
                 get reset at the start_memory we are about to get to,
                 but we will have saved all the registers relevant to
                 this repetition op, as described above.  */
              highest_active_reg = *(p1 + 1) + *(p1 + 2);
              if (lowest_active_reg == NO_LOWEST_ACTIVE_REG)
                lowest_active_reg = *(p1 + 1);
            }

          DEBUG_PRINT1 (":\n");
          PUSH_FAILURE_POINT (p + mcnt, d, -2);
          break;


        /* A smart repeat ends with `maybe_pop_jump'.
	   We change it to either `pop_failure_jump' or `jump'.  */
        case maybe_pop_jump:
          EXTRACT_NUMBER_AND_INCR (mcnt, p);
          DEBUG_PRINT2 ("EXECUTING maybe_pop_jump %d.\n", mcnt);
          {
	    register unsigned char *p2 = p;

            /* Compare the beginning of the repeat with what in the
               pattern follows its end. If we can establish that there
               is nothing that they would both match, i.e., that we
               would have to backtrack because of (as in, e.g., `a*a')
               then we can change to pop_failure_jump, because we'll
               never have to backtrack.
               
               This is not true in the case of alternatives: in
               `(a|ab)*' we do need to backtrack to the `ab' alternative
               (e.g., if the string was `ab').  But instead of trying to
               detect that here, the alternative has put on a dummy
               failure point which is what we will end up popping.  */

	    /* Skip over open/close-group commands.  */
	    while (_br(p2 + 2 < pend
		   && ((re_opcode_t) *p2 == stop_memory
		       || (re_opcode_t) *p2 == start_memory),0x47c8540,5652,13))
	      p2 += 3;			/* Skip over args, too.  */

            /* If we're at the end of the pattern, we can change.  */
            if (_br(p2 == pend,0x47c8688,5658,17))
	      {
		/* Consider what happens when matching ":\(.*\)"
		   against ":/".  I don't really understand this code
		   yet.  */
  	        p[-3] = (unsigned char) pop_failure_jump;
                DEBUG_PRINT1
                  ("  End of pattern: change to `pop_failure_jump'.\n");
              }

            else if (_br((re_opcode_t) *p2 == exactn
		     || (bufp->newline_anchor && (re_opcode_t) *p2 == endline),0x47c8b70,5668,22))
	      {
		register unsigned char c
                  = *p2 == (unsigned char) endline ? '\n' : p2[2];
		p1 = p + mcnt;

                /* p1[0] ... p1[2] are the `on_failure_jump' corresponding
                   to the `maybe_finalize_jump' of this case.  Examine what 
                   follows.  */
                if (_br((re_opcode_t) p1[3] == exactn && p1[5] != c,0x47c9200,5678,21))
                  {
  		    p[-3] = (unsigned char) pop_failure_jump;
                    DEBUG_PRINT3 ("  %c != %c => pop_failure_jump.\n",
                                  c, p1[5]);
                  }
                  
		else if (_br((re_opcode_t) p1[3] == charset
			 || (re_opcode_t) p1[3] == charset_not,0x47c9648,5685,12))
		  {
		    int not = (re_opcode_t) p1[3] == charset_not;
                    
		    if (_br(c < (unsigned char) (p1[4] * BYTEWIDTH)
			&& p1[5 + c / BYTEWIDTH] & (1 << (c % BYTEWIDTH)),0x47ca190,5690,11))
		      not = !not;

                    /* `not' is equal to 1 if c would match, which means
                        that we can't change to pop_failure_jump.  */
		    if (_br(!not,0x47ca2e0,5696,11))
                      {
  		        p[-3] = (unsigned char) pop_failure_jump;
                        DEBUG_PRINT1 ("  No match => pop_failure_jump.\n");
                      }
		  }
	      }
	  }
	  p -= 2;		/* Point at relative address again.  */
	  if (_br((re_opcode_t) p[-1] != pop_failure_jump,0x47ca7e8,5705,8))
	    {
	      p[-1] = (unsigned char) jump;
              DEBUG_PRINT1 ("  Match => jump.\n");
	      goto unconditional_jump;
	    }
        /* Note fall through.  */


	/* The end of a simple repeat has a pop_failure_jump back to
           its matching on_failure_jump, where the latter will push a
           failure point.  The pop_failure_jump takes off failure
           points put on by this pop_failure_jump's matching
           on_failure_jump; we got through the pattern to here from the
           matching on_failure_jump, so didn't fail.  */
        case pop_failure_jump:
          {
            /* We need to pass separate storage for the lowest and
               highest registers, even though we don't care about the
               actual values.  Otherwise, we will restore only one
               register from the stack, since lowest will == highest in
               `pop_failure_point'.  */
            unsigned dummy_low_reg, dummy_high_reg;
            unsigned char *pdummy;
            const char *sdummy;

            DEBUG_PRINT1 ("EXECUTING pop_failure_jump.\n");
            POP_FAILURE_POINT (sdummy, pdummy,
                               dummy_low_reg, dummy_high_reg,
                               reg_dummy, reg_dummy, reg_info_dummy);
          }
          /* Note fall through.  */

          
        /* Unconditionally jump (without popping any failure points).  */
        case jump:
	unconditional_jump:
	  EXTRACT_NUMBER_AND_INCR (mcnt, p);	/* Get the amount to jump.  */
          DEBUG_PRINT2 ("EXECUTING jump %d ", mcnt);
	  p += mcnt;				/* Do the jump.  */
          DEBUG_PRINT2 ("(to 0x%x).\n", p);
	  break;

	
        /* We need this opcode so we can detect where alternatives end
           in `group_match_null_string_p' et al.  */
        case jump_past_alt:
          DEBUG_PRINT1 ("EXECUTING jump_past_alt.\n");
          goto unconditional_jump;


        /* Normally, the on_failure_jump pushes a failure point, which
           then gets popped at pop_failure_jump.  We will end up at
           pop_failure_jump, also, and with a pattern of, say, `a+', we
           are skipping over the on_failure_jump, so we have to push
           something meaningless for pop_failure_jump to pop.  */
        case dummy_failure_jump:
          DEBUG_PRINT1 ("EXECUTING dummy_failure_jump.\n");
          /* It doesn't matter what we push for the string here.  What
             the code at `fail' tests is the value for the pattern.  */
          PUSH_FAILURE_POINT (0, 0, -2);
          goto unconditional_jump;


        /* At the end of an alternative, we need to push a dummy failure
           point in case we are followed by a `pop_failure_jump', because
           we don't want the failure point for the alternative to be
           popped.  For example, matching `(a|ab)*' against `aab'
           requires that we match the `ab' alternative.  */
        case push_dummy_failure:
          DEBUG_PRINT1 ("EXECUTING push_dummy_failure.\n");
          /* See comments just above at `dummy_failure_jump' about the
             two zeroes.  */
          PUSH_FAILURE_POINT (0, 0, -2);
          break;

        /* Have to succeed matching what follows at least n times.
           After that, handle like `on_failure_jump'.  */
        case succeed_n: 
          EXTRACT_NUMBER (mcnt, p + 2);
          DEBUG_PRINT2 ("EXECUTING succeed_n %d.\n", mcnt);

          assert (mcnt >= 0);
          /* Originally, this is how many times we HAVE to succeed.  */
          if (_br(mcnt > 0,0x47d2970,5789,15))
            {
               mcnt--;
	       p += 2;
               STORE_NUMBER_AND_INCR (p, mcnt);
               DEBUG_PRINT3 ("  Setting 0x%x to %d.\n", p, mcnt);
            }
	  else if (_br(mcnt == 0,0x47d2f98,5796,13))
            {
              DEBUG_PRINT2 ("  Setting two bytes from 0x%x to no_op.\n", p+2);
	      p[2] = (unsigned char) no_op;
              p[3] = (unsigned char) no_op;
              goto on_failure;
            }
          break;
        
        case jump_n: 
          EXTRACT_NUMBER (mcnt, p + 2);
          DEBUG_PRINT2 ("EXECUTING jump_n %d.\n", mcnt);

          /* Originally, this is how many times we CAN jump.  */
          if (_br(mcnt,0x47d3850,5810,15))
            {
               mcnt--;
               STORE_NUMBER (p + 2, mcnt);
	       goto unconditional_jump;	     
            }
          /* If don't have to jump any more, skip over the rest of command.  */
	  else      
	    p += 4;		     
          break;
        
	case set_number_at:
	  {
            DEBUG_PRINT1 ("EXECUTING set_number_at.\n");

            EXTRACT_NUMBER_AND_INCR (mcnt, p);
            p1 = p + mcnt;
            EXTRACT_NUMBER_AND_INCR (mcnt, p);
            DEBUG_PRINT3 ("  Setting 0x%x to %d.\n", p1, mcnt);
	    STORE_NUMBER (p1, mcnt);
            break;
          }

        case wordbound:
          DEBUG_PRINT1 ("EXECUTING wordbound.\n");
          if (AT_WORD_BOUNDARY (d))
	    break;
          goto fail;

	case notwordbound:
          DEBUG_PRINT1 ("EXECUTING notwordbound.\n");
	  if (AT_WORD_BOUNDARY (d))
	    goto fail;
          break;

	case wordbeg:
          DEBUG_PRINT1 ("EXECUTING wordbeg.\n");
	  if (WORDCHAR_P (d) && (AT_STRINGS_BEG (d) || !WORDCHAR_P (d - 1)))
	    break;
          goto fail;

	case wordend:
          DEBUG_PRINT1 ("EXECUTING wordend.\n");
	  if (_br(!AT_STRINGS_BEG (d) && WORDCHAR_P (d - 1)
              && (!WORDCHAR_P (d) || AT_STRINGS_END (d)),0x47d9550,5853,8))
	    break;
          goto fail;

#ifdef emacs
#ifdef emacs19
  	case before_dot:
          DEBUG_PRINT1 ("EXECUTING before_dot.\n");
 	  if (PTR_CHAR_POS ((unsigned char *) d) >= point)
  	    goto fail;
  	  break;
  
  	case at_dot:
          DEBUG_PRINT1 ("EXECUTING at_dot.\n");
 	  if (PTR_CHAR_POS ((unsigned char *) d) != point)
  	    goto fail;
  	  break;
  
  	case after_dot:
          DEBUG_PRINT1 ("EXECUTING after_dot.\n");
          if (PTR_CHAR_POS ((unsigned char *) d) <= point)
  	    goto fail;
  	  break;
#else /* not emacs19 */
	case at_dot:
          DEBUG_PRINT1 ("EXECUTING at_dot.\n");
	  if (PTR_CHAR_POS ((unsigned char *) d) + 1 != point)
	    goto fail;
	  break;
#endif /* not emacs19 */

	case syntaxspec:
          DEBUG_PRINT2 ("EXECUTING syntaxspec %d.\n", mcnt);
	  mcnt = *p++;
	  goto matchsyntax;

        case wordchar:
          DEBUG_PRINT1 ("EXECUTING Emacs wordchar.\n");
	  mcnt = (int) Sword;
        matchsyntax:
	  PREFETCH ();
	  if (SYNTAX (*d++) != (enum syntaxcode) mcnt)
            goto fail;
          SET_REGS_MATCHED ();
	  break;

	case notsyntaxspec:
          DEBUG_PRINT2 ("EXECUTING notsyntaxspec %d.\n", mcnt);
	  mcnt = *p++;
	  goto matchnotsyntax;

        case notwordchar:
          DEBUG_PRINT1 ("EXECUTING Emacs notwordchar.\n");
	  mcnt = (int) Sword;
        matchnotsyntax:
	  PREFETCH ();
	  if (SYNTAX (*d++) == (enum syntaxcode) mcnt)
            goto fail;
	  SET_REGS_MATCHED ();
          break;

#else /* not emacs */
	case wordchar:
          DEBUG_PRINT1 ("EXECUTING non-Emacs wordchar.\n");
	  PREFETCH ();
          if (!WORDCHAR_P (d))
            goto fail;
	  SET_REGS_MATCHED ();
          d++;
	  break;
	  
	case notwordchar:
          DEBUG_PRINT1 ("EXECUTING non-Emacs notwordchar.\n");
	  PREFETCH ();
	  if (WORDCHAR_P (d))
            goto fail;
          SET_REGS_MATCHED ();
          d++;
	  break;
#endif /* not emacs */
          
        default:
          abort ();
	}
      continue;  /* Successfully executed one pattern command; keep going.  */


    /* We goto here if a matching operation fails. */
    fail:
      if (!FAIL_STACK_EMPTY ())
	{ /* A restart point is known.  Restore to that state.  */
          DEBUG_PRINT1 ("\nFAIL:\n");
          POP_FAILURE_POINT (d, p,
                             lowest_active_reg, highest_active_reg,
                             regstart, regend, reg_info);

          /* If this failure point is a dummy, try the next one.  */
          if (_br(!p,0x47dd338,5951,15))
	    goto fail;

          /* If we failed to the end of the pattern, don't examine *p.  */
	  assert (p <= pend);
          if (_br(p < pend,0x47dd438,5956,15))
            {
              boolean is_a_jump_n = false;
              
              /* If failed to a backwards jump that's part of a repetition
                 loop, need to pop this failure point and use the next one.  */
              switch ((re_opcode_t) *p)
                {
                case jump_n:
                  is_a_jump_n = true;
                case maybe_pop_jump:
                case pop_failure_jump:
                case jump:
                  p1 = p + 1;
                  EXTRACT_NUMBER_AND_INCR (mcnt, p1);
                  p1 += mcnt;	

                  if (_br((is_a_jump_n && (re_opcode_t) *p1 == succeed_n)
                      || (!is_a_jump_n
                          && (re_opcode_t) *p1 == on_failure_jump),0x47de2a8,5973,23))
                    goto fail;
                  break;
                default:
                  /* do nothing */ ;
                }
            }

          if (_br(d >= string1 && d <= end1,0x47de5b8,5983,15))
	    dend = end_match_1;
        }
      else
        break;   /* Matching at this starting point really fails.  */
    } /* for (;;) */

  if (_br(best_regs_set,0x47de7f0,5990,7))
    goto restore_best_regs;

  FREE_VARIABLES ();

  return -1;         			/* Failure to match.  */
} /* re_match_2 */

/* Subroutine definitions for re_match_2.  */


/* We are passed P pointing to a register number after a start_memory.
   
   Return true if the pattern up to the corresponding stop_memory can
   match the empty string, and false otherwise.
   
   If we find the matching stop_memory, sets P to point to one past its number.
   Otherwise, sets P to an undefined byte less than or equal to END.

   We don't handle duplicates properly (yet).  */

static boolean
group_match_null_string_p (p, end, reg_info)
    unsigned char **p, *end;
    register_info_type *reg_info;
{
  int mcnt;
  /* Point to after the args to the start_memory.  */
  unsigned char *p1 = *p + 2;
  
  while (_br(p1 < end,0x47deff0,6020,10))
    {
      /* Skip over opcodes that can match nothing, and return true or
	 false, as appropriate, when we get to one that can't, or to the
         matching stop_memory.  */
      
      switch ((re_opcode_t) *p1)
        {
        /* Could be either a loop or a series of alternatives.  */
        case on_failure_jump:
          p1++;
          EXTRACT_NUMBER_AND_INCR (mcnt, p1);
          
          /* If the next operation is not a jump backwards in the
	     pattern.  */

	  if (_br(mcnt >= 0,0x47e2b98,6036,8))
	    {
              /* Go through the on_failure_jumps of the alternatives,
                 seeing if any of the alternatives cannot match nothing.
                 The last alternative starts with only a jump,
                 whereas the rest start with on_failure_jump and end
                 with a jump, e.g., here is the pattern for `a|b|c':

                 /on_failure_jump/0/6/exactn/1/a/jump_past_alt/0/6
                 /on_failure_jump/0/6/exactn/1/b/jump_past_alt/0/3
                 /exactn/1/c						

                 So, we have to first go through the first (n-1)
                 alternatives and then deal with the last one separately.  */


              /* Deal with the first (n-1) alternatives, which start
                 with an on_failure_jump (see above) that jumps to right
                 past a jump_past_alt.  */

              while (_br((re_opcode_t) p1[mcnt-3] == jump_past_alt,0x47e2d58,6056,22))
                {
                  /* `mcnt' holds how many bytes long the alternative
                     is, including the ending `jump_past_alt' and
                     its number.  */

                  if (_br(!alt_match_null_string_p (p1, p1 + mcnt - 3, 
				                      reg_info),0x47e2f70,6062,23))
                    return false;

                  /* Move to right after this alternative, including the
		     jump_past_alt.  */
                  p1 += mcnt;	

                  /* Break if it's the beginning of an n-th alternative
                     that doesn't begin with an on_failure_jump.  */
                  if (_br((re_opcode_t) *p1 != on_failure_jump,0x47e31c0,6072,23))
                    break;
		
		  /* Still have to check that it's not an n-th
		     alternative that starts with an on_failure_jump.  */
		  p1++;
                  EXTRACT_NUMBER_AND_INCR (mcnt, p1);
                  if (_br((re_opcode_t) p1[mcnt-3] != jump_past_alt,0x47e3970,6079,23))
                    {
		      /* Get to the beginning of the n-th alternative.  */
                      p1 -= 3;
                      break;
                    }
                }

              /* Deal with the last alternative: go back and get number
                 of the `jump_past_alt' just before it.  `mcnt' contains
                 the length of the alternative.  */
              EXTRACT_NUMBER (mcnt, p1 - 2);

              if (_br(!alt_match_null_string_p (p1, p1 + mcnt, reg_info),0x47e4180,6092,19))
                return false;

              p1 += mcnt;	/* Get past the n-th alternative.  */
            } /* if mcnt > 0 */
          break;

          
        case stop_memory:
	  assert (p1[1] == **p);
          *p = p1 + 2;
          return true;

        
        default: 
          if (_br(!common_op_match_null_string_p (&p1, end, reg_info),0x47e4670,6107,15))
            return false;
        }
    } /* while p1 < end */

  return false;
} /* group_match_null_string_p */


/* Similar to group_match_null_string_p, but doesn't deal with alternatives:
   It expects P to be the first byte of a single alternative and END one
   byte past the last. The alternative can contain groups.  */
   
static boolean
alt_match_null_string_p (p, end, reg_info)
    unsigned char *p, *end;
    register_info_type *reg_info;
{
  int mcnt;
  unsigned char *p1 = p;
  
  while (_br(p1 < end,0x47e4d50,6128,10))
    {
      /* Skip over opcodes that can match nothing, and break when we get 
         to one that can't.  */
      
      switch ((re_opcode_t) *p1)
        {
	/* It's a loop.  */
        case on_failure_jump:
          p1++;
          EXTRACT_NUMBER_AND_INCR (mcnt, p1);
          p1 += mcnt;
          break;
          
	default: 
          if (_br(!common_op_match_null_string_p (&p1, end, reg_info),0x47e5670,6143,15))
            return false;
        }
    }  /* while p1 < end */

  return true;
} /* alt_match_null_string_p */


/* Deals with the ops common to group_match_null_string_p and
   alt_match_null_string_p.  
   
   Sets P to one after the op and its arguments, if any.  */

static boolean
common_op_match_null_string_p (p, end, reg_info)
    unsigned char **p, *end;
    register_info_type *reg_info;
{
  int mcnt;
  boolean ret;
  int reg_no;
  unsigned char *p1 = *p;

  switch ((re_opcode_t) *p1++)
    {
    case no_op:
    case begline:
    case endline:
    case begbuf:
    case endbuf:
    case wordbeg:
    case wordend:
    case wordbound:
    case notwordbound:
#ifdef emacs
    case before_dot:
    case at_dot:
    case after_dot:
#endif
      break;

    case start_memory:
      reg_no = *p1;
      assert (reg_no > 0 && reg_no <= MAX_REGNUM);
      ret = group_match_null_string_p (&p1, end, reg_info);
      
      /* Have to set this here in case we're checking a group which
         contains a group and a back reference to it.  */

      if (REG_MATCH_NULL_STRING_P (reg_info[reg_no]) == MATCH_NULL_UNSET_VALUE)
        REG_MATCH_NULL_STRING_P (reg_info[reg_no]) = ret;

      if (_br(!ret,0x47e68e0,6196,11))
        return false;
      break;
          
    /* If this is an optimized succeed_n for zero times, make the jump.  */
    case jump:
      EXTRACT_NUMBER_AND_INCR (mcnt, p1);
      if (_br(mcnt >= 0,0x47e6fb8,6203,11))
        p1 += mcnt;
      else
        return false;
      break;

    case succeed_n:
      /* Get to the number of times to succeed.  */
      p1 += 2;		
      EXTRACT_NUMBER_AND_INCR (mcnt, p1);

      if (_br(mcnt == 0,0x47e77b8,6214,11))
        {
          p1 -= 4;
          EXTRACT_NUMBER_AND_INCR (mcnt, p1);
          p1 += mcnt;
        }
      else
        return false;
      break;

    case duplicate: 
      if (!REG_MATCH_NULL_STRING_P (reg_info[*p1]))
        return false;
      break;

    case set_number_at:
      p1 += 4;

    default:
      /* All other opcodes mean we cannot match the empty string.  */
      return false;
  }

  *p = p1;
  return true;
} /* common_op_match_null_string_p */


/* Return zero if TRANSLATE[S1] and TRANSLATE[S2] are identical for LEN
   bytes; nonzero otherwise.  */
   
static int
bcmp_translate (s1, s2, len, translate)
     unsigned char *s1, *s2;
     register int len;
     char *translate;
{
  register unsigned char *p1 = s1, *p2 = s2;
  while (_br(len,0x47e8ba8,6252,10))
    {
      if (_br(translate[*p1++] != translate[*p2++],0x47e8df0,6254,11)) return 1;
      len--;
    }
  return 0;
}

/* Entry points for GNU code.  */

/* re_compile_pattern is the GNU regular expression compiler: it
   compiles PATTERN (of length SIZE) and puts the result in BUFP.
   Returns 0 if the pattern was valid, otherwise an error string.
   
   Assumes the `allocated' (and perhaps `buffer') and `translate' fields
   are set in BUFP on entry.
   
   We call regex_compile to do the actual compilation.  */

const char *
re_compile_pattern (pattern, length, bufp)
     const char *pattern;
     int length;
     struct re_pattern_buffer *bufp;
{
  reg_errcode_t ret;
  
  /* GNU code is written to assume at least RE_NREGS registers will be set
     (and at least one extra will be -1).  */
  bufp->regs_allocated = REGS_UNALLOCATED;
  
  /* And GNU code determines whether or not to get register information
     by passing null for the REGS argument to re_match, etc., not by
     setting no_sub.  */
  bufp->no_sub = 0;
  
  /* Match anchors at newline.  */
  bufp->newline_anchor = 1;
  
  ret = regex_compile (pattern, length, re_syntax_options, bufp);

  return re_error_msg[(int) ret];
}     

/* Entry points compatible with 4.2 BSD regex library.  We don't define
   them if this is an Emacs or POSIX compilation.  */

#if !defined (emacs) && !defined (_POSIX_SOURCE)

/* BSD has one and only one pattern buffer.  */
static struct re_pattern_buffer re_comp_buf;

char *
re_comp (s)
    const char *s;
{
  reg_errcode_t ret;
  
  if (!s)
    {
      if (!re_comp_buf.buffer)
	return "No previous regular expression";
      return 0;
    }

  if (!re_comp_buf.buffer)
    {
      re_comp_buf.buffer = (unsigned char *) malloc (200);
      if (re_comp_buf.buffer == NULL)
        return "Memory exhausted";
      re_comp_buf.allocated = 200;

      re_comp_buf.fastmap = (char *) malloc (1 << BYTEWIDTH);
      if (re_comp_buf.fastmap == NULL)
	return "Memory exhausted";
    }

  /* Since `re_exec' always passes NULL for the `regs' argument, we
     don't need to initialize the pattern buffer fields which affect it.  */

  /* Match anchors at newlines.  */
  re_comp_buf.newline_anchor = 1;

  ret = regex_compile (s, strlen (s), re_syntax_options, &re_comp_buf);
  
  /* Yes, we're discarding `const' here.  */
  return (char *) re_error_msg[(int) ret];
}


int
re_exec (s)
    const char *s;
{
  const int len = strlen (s);
  return
    0 <= re_search (&re_comp_buf, s, len, 0, len, (struct re_registers *) 0);
}
#endif /* not emacs and not _POSIX_SOURCE */

/* POSIX.2 functions.  Don't define these for Emacs.  */

#ifndef emacs

/* regcomp takes a regular expression as a string and compiles it.

   PREG is a regex_t *.  We do not expect any fields to be initialized,
   since POSIX says we shouldn't.  Thus, we set

     `buffer' to the compiled pattern;
     `used' to the length of the compiled pattern;
     `syntax' to RE_SYNTAX_POSIX_EXTENDED if the
       REG_EXTENDED bit in CFLAGS is set; otherwise, to
       RE_SYNTAX_POSIX_BASIC;
     `newline_anchor' to REG_NEWLINE being set in CFLAGS;
     `fastmap' and `fastmap_accurate' to zero;
     `re_nsub' to the number of subexpressions in PATTERN.

   PATTERN is the address of the pattern string.

   CFLAGS is a series of bits which affect compilation.

     If REG_EXTENDED is set, we use POSIX extended syntax; otherwise, we
     use POSIX basic syntax.

     If REG_NEWLINE is set, then . and [^...] don't match newline.
     Also, regexec will try a match beginning after every newline.

     If REG_ICASE is set, then we considers upper- and lowercase
     versions of letters to be equivalent when matching.

     If REG_NOSUB is set, then when PREG is passed to regexec, that
     routine will report only success or failure, and nothing about the
     registers.

   It returns 0 if it succeeds, nonzero if it doesn't.  (See regex.h for
   the return codes and their meanings.)  */

int
regcomp (preg, pattern, cflags)
    regex_t *preg;
    const char *pattern; 
    int cflags;
{
  reg_errcode_t ret;
  unsigned syntax
    = (cflags & REG_EXTENDED) ?
      RE_SYNTAX_POSIX_EXTENDED : RE_SYNTAX_POSIX_BASIC;

  /* regex_compile will allocate the space for the compiled pattern.  */
  preg->buffer = 0;
  preg->allocated = 0;
  preg->used = 0;
  
  /* Don't bother to use a fastmap when searching.  This simplifies the
     REG_NEWLINE case: if we used a fastmap, we'd have to put all the
     characters after newlines into the fastmap.  This way, we just try
     every character.  */
  preg->fastmap = 0;
  
  if (cflags & REG_ICASE)
    {
      unsigned i;
      
      preg->translate = (char *) malloc (CHAR_SET_SIZE);
      if (preg->translate == NULL)
        return (int) REG_ESPACE;

      /* Map uppercase characters to corresponding lowercase ones.  */
      for (i = 0; i < CHAR_SET_SIZE; i++)
        preg->translate[i] = ISUPPER (i) ? tolower (i) : i;
    }
  else
    preg->translate = NULL;

  /* If REG_NEWLINE is set, newlines are treated differently.  */
  if (cflags & REG_NEWLINE)
    { /* REG_NEWLINE implies neither . nor [^...] match newline.  */
      syntax &= ~RE_DOT_NEWLINE;
      syntax |= RE_HAT_LISTS_NOT_NEWLINE;
      /* It also changes the matching behavior.  */
      preg->newline_anchor = 1;
    }
  else
    preg->newline_anchor = 0;

  preg->no_sub = !!(cflags & REG_NOSUB);

  /* POSIX says a null character in the pattern terminates it, so we 
     can use strlen here in compiling the pattern.  */
  ret = regex_compile (pattern, strlen (pattern), syntax, preg);
  
  /* POSIX doesn't distinguish between an unmatched open-group and an
     unmatched close-group: both are REG_EPAREN.  */
  if (_br(ret == REG_ERPAREN,0x4733780,6446,7)) ret = REG_EPAREN;
  
  return (int) ret;
}


/* regexec searches for a given pattern, specified by PREG, in the
   string STRING.
   
   If NMATCH is zero or REG_NOSUB was set in the cflags argument to
   `regcomp', we ignore PMATCH.  Otherwise, we assume PMATCH has at
   least NMATCH elements, and we set them to the offsets of the
   corresponding matched substrings.
   
   EFLAGS specifies `execution flags' which affect matching: if
   REG_NOTBOL is set, then ^ does not match at the beginning of the
   string; if REG_NOTEOL is set, then $ does not match at the end.
   
   We return 0 if we find a match and REG_NOMATCH if not.  */

int
regexec (preg, string, nmatch, pmatch, eflags)
    const regex_t *preg;
    const char *string; 
    size_t nmatch; 
    regmatch_t pmatch[]; 
    int eflags;
{
  int ret;
  struct re_registers regs;
  regex_t private_preg;
  int len = strlen (string);
  boolean want_reg_info = !preg->no_sub && nmatch > 0;

  private_preg = *preg;
  
  private_preg.not_bol = !!(eflags & REG_NOTBOL);
  private_preg.not_eol = !!(eflags & REG_NOTEOL);
  
  /* The user has told us exactly how many registers to return
     information about, via `nmatch'.  We have to pass that on to the
     matching routines.  */
  private_preg.regs_allocated = REGS_FIXED;
  
  if (_br(want_reg_info,0x4735820,6490,7))
    {
      regs.num_regs = nmatch;
      regs.start = TALLOC (nmatch, regoff_t);
      regs.end = TALLOC (nmatch, regoff_t);
      if (regs.start == NULL || regs.end == NULL)
        return (int) REG_NOMATCH;
    }

  /* Perform the searching operation.  */
  ret = re_search (&private_preg, string, len,
                   /* start: */ 0, /* range: */ len,
                   want_reg_info ? &regs : (struct re_registers *) 0);
  
  /* Copy the register information to the POSIX structure.  */
  if (_br(want_reg_info,0x47364b8,6505,7))
    {
      if (_br(ret >= 0,0x4736530,6507,11))
        {
          unsigned r;

          for (r = 0; _br(r < nmatch,0x4736708,6511,23); r++)
            {
              pmatch[r].rm_so = regs.start[r];
              pmatch[r].rm_eo = regs.end[r];
            }
        }

      /* If we needed the temporary register info, free the space now.  */
      free (regs.start);
      free (regs.end);
    }

  /* We want zero return to mean success, unlike `re_search'.  */
  return ret >= 0 ? (int) REG_NOERROR : (int) REG_NOMATCH;
}


/* Returns a message corresponding to an error code, ERRCODE, returned
   from either regcomp or regexec.   We don't use PREG here.  */

size_t
regerror (errcode, preg, errbuf, errbuf_size)
    int errcode;
    const regex_t *preg;
    char *errbuf;
    size_t errbuf_size;
{
  const char *msg;
  size_t msg_size;

  if (_br(errcode < 0
      || errcode >= (sizeof (re_error_msg) / sizeof (re_error_msg[0])),0x4737730,6541,7))
    /* Only error codes returned by the rest of the code should be passed 
       to this routine.  If we are given anything else, or if other regex
       code generates an invalid error code, then the program has a bug.
       Dump core so we can fix it.  */
    abort ();

  msg = re_error_msg[errcode];

  /* POSIX doesn't require that we do anything in this case, but why
     not be nice.  */
  if (_br(! msg,0x4737948,6553,7))
    msg = "Success";

  msg_size = strlen (msg) + 1; /* Includes the null.  */
  
  if (_br(errbuf_size != 0,0x4737c40,6558,7))
    {
      if (_br(msg_size > errbuf_size,0x4737ce8,6560,11))
        {
          strncpy (errbuf, msg, errbuf_size - 1);
          errbuf[errbuf_size - 1] = 0;
        }
      else
        strcpy (errbuf, msg);
    }

  return msg_size;
}


/* Free dynamically allocated space used by PREG.  */

void
regfree (preg)
    regex_t *preg;
{
  if (preg->buffer != NULL)
    free (preg->buffer);
  preg->buffer = NULL;
  
  preg->allocated = 0;
  preg->used = 0;

  if (preg->fastmap != NULL)
    free (preg->fastmap);
  preg->fastmap = NULL;
  preg->fastmap_accurate = 0;

  if (preg->translate != NULL)
    free (preg->translate);
  preg->translate = NULL;
}

#endif /* not emacs  */

/*
Local variables:
make-backup-files: t
version-control: t
trim-versions-without-asking: nil
End:
*/
/* dfa.c - deterministic extended regexp routines for GNU
   Copyright (C) 1988 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

/* Written June, 1988 by Mike Haertel
   Modified July, 1988 by Arthur David Olson to assist BMG speedups  */

/*#include <assert.h> */
/*#include <ctype.h> */
/*#include <stdio.h> */

#ifdef STDC_HEADERS
#if flag_stdlib==0
#include <stdlib.h>
#define flag_stdlib 1
#endif
#else
#if flag_systypes==0 
#include <sys/types.h>
#define flag_systypes 1
#endif
extern char *calloc(), *malloc(), *realloc();
extern void free();
#endif

#if defined(HAVE_STRING_H) || defined(STDC_HEADERS)
#if flag_string==0
#include <string.h>
#define flag_string 1
#endif
#undef index
#define index strchr
#else
#if flag_strings==0
#include <strings.h>
#define flag_strings 1
#endif
#endif

#ifndef isgraph
#define isgraph(C) (isprint(C) && !isspace(C))
#endif

#undef ISALPHA
#undef ISUPPER
#undef ISLOWER
#undef ISDIGIT
#undef ISXDIGIT
#undef ISSPACE
#undef ISPUNCT
#undef ISALNUM
#undef ISPRINT
#undef ISGRAPH
#undef ISCNTRL


#ifdef isascii
#define ISALPHA(C) (isascii(C) && isalpha(C))
#define ISUPPER(C) (isascii(C) && isupper(C))
#define ISLOWER(C) (isascii(C) && islower(C))
#define ISDIGIT(C) (isascii(C) && isdigit(C))
#define ISXDIGIT(C) (isascii(C) && isxdigit(C))
#define ISSPACE(C) (isascii(C) && isspace(C))
#define ISPUNCT(C) (isascii(C) && ispunct(C))
#define ISALNUM(C) (isascii(C) && isalnum(C))
#define ISPRINT(C) (isascii(C) && isprint(C))
#define ISGRAPH(C) (isascii(C) && isgraph(C))
#define ISCNTRL(C) (isascii(C) && iscntrl(C))
#else
#define ISALPHA(C) isalpha(C)
#define ISUPPER(C) isupper(C)
#define ISLOWER(C) islower(C)
#define ISDIGIT(C) isdigit(C)
#define ISXDIGIT(C) isxdigit(C)
#define ISSPACE(C) isspace(C)
#define ISPUNCT(C) ispunct(C)
#define ISALNUM(C) isalnum(C)
#define ISPRINT(C) isprint(C)
#define ISGRAPH(C) isgraph(C)
#define ISCNTRL(C) iscntrl(C)
#endif

#include "dfa.h"
/*#include "regex.h"*/

#if __STDC__
typedef void *ptr_t;
#else
typedef char *ptr_t;
#endif

static void	dfamust();

static ptr_t
xcalloc(n, s)
     int n;
     size_t s;
{
  ptr_t r = calloc(n, s);

  if (_br(!r,0x4745ab8,6718,7))
    dfaerror("Memory exhausted");
  return r;
}

static ptr_t
xmalloc_1(n)
     size_t n;
{
  ptr_t r = malloc(n);

  assert(n != 0);
  if (_br(!r,0x4745fa0,6730,7))
    dfaerror("Memory exhausted");
  return r;
}

static ptr_t
xrealloc_1(p, n)
     ptr_t p;
     size_t n;
{
  ptr_t r = realloc(p, n);

  assert(n != 0);
  if (_br(!r,0x4746548,6743,7))
    dfaerror("Memory exhausted");
  return r;
}

#define CALLOC(p, t, n) ((p) = (t *) xcalloc((n), sizeof (t)))
#define MALLOC(p, t, n) ((p) = (t *) xmalloc_1((n) * sizeof (t)))
#define REALLOC(p, t, n) ((p) = (t *) xrealloc_1((ptr_t) (p), (n) * sizeof (t)))

/* Reallocate an array of type t if nalloc is too small for index. */
#define REALLOC_IF_NECESSARY(p, t, nalloc, index) \
  if ((index) >= (nalloc))			  \
    {						  \
      while ((index) >= (nalloc))		  \
	(nalloc) *= 2;				  \
      REALLOC(p, t, nalloc);			  \
    }

#ifdef DEBUG

static void
prtok(t)
     token t;
{
  char *s;

  if (t < 0)
    fprintf(stderr, "END");
  else if (t < NOTCHAR)
    fprintf(stderr, "%c", t);
  else
    {
      switch (t)
	{
	case EMPTY: s = "EMPTY"; break;
	case BACKREF: s = "BACKREF"; break;
	case BEGLINE: s = "BEGLINE"; break;
	case ENDLINE: s = "ENDLINE"; break;
	case BEGWORD: s = "BEGWORD"; break;
	case ENDWORD: s = "ENDWORD"; break;
	case LIMWORD: s = "LIMWORD"; break;
	case NOTLIMWORD: s = "NOTLIMWORD"; break;
	case QMARK: s = "QMARK"; break;
	case STAR: s = "STAR"; break;
	case PLUS: s = "PLUS"; break;
	case CAT: s = "CAT"; break;
	case OR: s = "OR"; break;
	case ORTOP: s = "ORTOP"; break;
	case LPAREN: s = "LPAREN"; break;
	case RPAREN: s = "RPAREN"; break;
	default: s = "CSET"; break;
	}
      fprintf(stderr, "%s", s);
    }
}
#endif /* DEBUG */

/* Stuff pertaining to charclasses. */

static int
tstbit(b, c)
     int b;
     charclass c;
{
  return c[b / INTBITS] & 1 << b % INTBITS;
}

static void
setbit(b, c)
     int b;
     charclass c;
{
  c[b / INTBITS] |= 1 << b % INTBITS;
}

static void
clrbit(b, c)
     int b;
     charclass c;
{
  c[b / INTBITS] &= ~(1 << b % INTBITS);
}

static void
copyset(src, dst)
     charclass src;
     charclass dst;
{
  int i;

  for (i = 0; i < CHARCLASS_INTS; ++i)
    dst[i] = src[i];
}

static void
zeroset(s)
     charclass s;
{
  int i;

  for (i = 0; i < CHARCLASS_INTS; ++i)
    s[i] = 0;
}

static void
notset(s)
     charclass s;
{
  int i;

  for (i = 0; i < CHARCLASS_INTS; ++i)
    s[i] = ~s[i];
}

static int
equal(s1, s2)
     charclass s1;
     charclass s2;
{
  int i;

  for (i = 0; i < CHARCLASS_INTS; ++i)
    if (_br(s1[i] != s2[i],0x4820ab8,6865,9))
      return 0;
  return 1;
}

/* A pointer to the current dfa is kept here during parsing. */
static struct dfa *dfa;

/* Find the index of charclass s in dfa->charclasses, or allocate a new charclass. */
static int
charclass_index(s)
     charclass s;
{
  int i;

  for (i = 0; _br(i < dfa->cindex,0x4820fc8,6880,15); ++i)
    if (_br(equal(s, dfa->charclasses[i]),0x4821198,6881,9))
      return i;
  REALLOC_IF_NECESSARY(dfa->charclasses, charclass, dfa->calloc, dfa->cindex);
  ++dfa->cindex;
  copyset(s, dfa->charclasses[i]);
  return i;
}

/* Syntax bits controlling the behavior of the lexical analyzer. */
static int syntax_bits, syntax_bits_set;

/* Flag for case-folding letters into sets. */
static int case_fold;

/* Entry point to set syntax options. */
void
dfasyntax(bits, fold)
     int bits;
     int fold;
{
  syntax_bits_set = 1;
  syntax_bits = bits;
  case_fold = fold;
}

/* Lexical analyzer.  All the dross that deals with the obnoxious
   GNU Regex syntax bits is located here.  The poor, suffering
   reader is referred to the GNU Regex documentation for the
   meaning of the @#%!@#%^!@ syntax bits. */

static char *lexstart;		/* Pointer to beginning of input string. */
static char *lexptr;		/* Pointer to next input character. */
static lexleft;			/* Number of characters remaining. */
static token lasttok;		/* Previous token returned; initially END. */
static int laststart;		/* True if we're separated from beginning or (, |
				   only by zero-width characters. */
static int parens;		/* Count of outstanding left parens. */
static int minrep, maxrep;	/* Repeat counts for {m,n}. */

/* Note that characters become unsigned here. */
#define FETCH(c, eoferr)   	      \
  {			   	      \
    if (! lexleft)	   	      \
      if (eoferr != 0)	   	      \
	dfaerror(eoferr);  	      \
      else		   	      \
	return END;	   	      \
    (c) = (unsigned char) *lexptr++;  \
    --lexleft;		   	      \
  }

#define FUNC(F, P) static int F(c) int c; { return P(c); }

FUNC(is_alpha, ISALPHA)
FUNC(is_upper, ISUPPER)
FUNC(is_lower, ISLOWER)
FUNC(is_digit, ISDIGIT)
FUNC(is_xdigit, ISXDIGIT)
FUNC(is_space, ISSPACE)
FUNC(is_punct, ISPUNCT)
FUNC(is_alnum, ISALNUM)
FUNC(is_print, ISPRINT)
FUNC(is_graph, ISGRAPH)
FUNC(is_cntrl, ISCNTRL)

/* The following list maps the names of the Posix named character classes
   to predicate functions that determine whether a given character is in
   the class.  The leading [ has already been eaten by the lexical analyzer. */
static struct {
  char *name;
  int (*pred)();
} prednames[] = {
  ":alpha:]", is_alpha,
  ":upper:]", is_upper,
  ":lower:]", is_lower,
  ":digit:]", is_digit,
  ":xdigit:]", is_xdigit,
  ":space:]", is_space,
  ":punct:]", is_punct,
  ":alnum:]", is_alnum,
  ":print:]", is_print,
  ":graph:]", is_graph,
  ":cntrl:]", is_cntrl,
  0
};

static int
looking_at(s)
     char *s;
{
  int len;

  len = strlen(s);
  if (_br(lexleft < len,0x4827c90,6974,7))
    return 0;
  return strncmp(s, lexptr, len) == 0;
}

static token
lex()
{
  token c, c1, c2;
  int backslash = 0, invert;
  charclass ccl;
  int i;

  /* Basic plan: We fetch a character.  If it's a backslash,
     we set the backslash flag and go through the loop again.
     On the plus side, this avoids having a duplicate of the
     main switch inside the backslash case.  On the minus side,
     it means that just about every case begins with
     "if (backslash) ...".  */
  for (i = 0; _br(i < 2,0x4828580,6993,15); ++i)
    {
      FETCH(c, 0);
      switch (c)
	{
	case '\\':
	  if (_br(backslash,0x4828af8,6999,8))
	    goto normal_char;
	  if (_br(lexleft == 0,0x4828c08,7001,8))
	    dfaerror("Unfinished \\ escape");
	  backslash = 1;
	  break;

	case '^':
	  if (_br(backslash,0x4828e20,7007,8))
	    goto normal_char;
	  if (_br(syntax_bits & RE_CONTEXT_INDEP_ANCHORS
	      || lasttok == END
	      || lasttok == LPAREN
	      || lasttok == OR,0x48293f0,7009,8))
	    return lasttok = BEGLINE;
	  goto normal_char;

	case '$':
	  if (_br(backslash,0x4829588,7017,8))
	    goto normal_char;
	  if (_br(syntax_bits & RE_CONTEXT_INDEP_ANCHORS
	      || lexleft == 0
	      || (syntax_bits & RE_NO_BK_PARENS
		  ? lexleft > 0 && *lexptr == ')'
		  : lexleft > 1 && lexptr[0] == '\\' && lexptr[1] == ')')
	      || (syntax_bits & RE_NO_BK_VBAR
		  ? lexleft > 0 && *lexptr == '|'
		  : lexleft > 1 && lexptr[0] == '\\' && lexptr[1] == '|')
	      || ((syntax_bits & RE_NEWLINE_ALT)
	          && lexleft > 0 && *lexptr == '\n'),0x482b5c8,7019,8))
	    return lasttok = ENDLINE;
	  goto normal_char;

	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
	  if (_br(backslash && !(syntax_bits & RE_NO_BK_REFS),0x482c090,7041,8))
	    {
	      laststart = 0;
	      return lasttok = BACKREF;
	    }
	  goto normal_char;

	case '<':
	  if (_br(backslash,0x482c2c0,7049,8))
	    return lasttok = BEGWORD;
	  goto normal_char;

	case '>':
	  if (_br(backslash,0x482c448,7054,8))
	    return lasttok = ENDWORD;
	  goto normal_char;

	case 'b':
	  if (_br(backslash,0x482c5d0,7059,8))
	    return lasttok = LIMWORD;
	  goto normal_char;

	case 'B':
	  if (_br(backslash,0x482c758,7064,8))
	    return lasttok = NOTLIMWORD;
	  goto normal_char;

	case '?':
	  if (syntax_bits & RE_LIMITED_OPS)
	    goto normal_char;
	  if (_br(backslash != ((syntax_bits & RE_BK_PLUS_QM) != 0),0x482cf98,7071,8))
	    goto normal_char;
	  if (_br(!(syntax_bits & RE_CONTEXT_INDEP_OPS) && laststart,0x482d2f0,7073,8))
	    goto normal_char;
	  return lasttok = QMARK;

	case '*':
	  if (_br(backslash,0x482d488,7078,8))
	    goto normal_char;
	  if (_br(!(syntax_bits & RE_CONTEXT_INDEP_OPS) && laststart,0x482d7b8,7080,8))
	    goto normal_char;
	  return lasttok = STAR;

	case '+':
	  if (syntax_bits & RE_LIMITED_OPS)
	    goto normal_char;
	  if (_br(backslash != ((syntax_bits & RE_BK_PLUS_QM) != 0),0x482e008,7087,8))
	    goto normal_char;
	  if (_br(!(syntax_bits & RE_CONTEXT_INDEP_OPS) && laststart,0x482e348,7089,8))
	    goto normal_char;
	  return lasttok = PLUS;

	case '{':
	  if (_br(!(syntax_bits & RE_INTERVALS),0x482e928,7094,8))
	    goto normal_char;
	  if (_br(backslash != ((syntax_bits & RE_NO_BK_BRACES) == 0),0x482efe8,7096,8))
	    goto normal_char;
	  minrep = maxrep = 0;
	  /* Cases:
	     {M} - exact count
	     {M,} - minimum count, maximum is infinity
	     {,M} - 0 through M
	     {M,N} - M through N */
	  FETCH(c, "unfinished repeat count");
	  if (ISDIGIT(c))
	    {
	      minrep = c - '0';
	      for (;;)
		{
		  FETCH(c, "unfinished repeat count");
		  if (!ISDIGIT(c))
		    break;
		  minrep = 10 * minrep + c - '0';
		}
	    }
	  else if (_br(c != ',',0x4830498,7116,13))
	    dfaerror("malformed repeat count");
	  if (_br(c == ',',0x4830668,7118,8))
	    for (;;)
	      {
		FETCH(c, "unfinished repeat count");
		if (!ISDIGIT(c))
		  break;
		maxrep = 10 * maxrep + c - '0';
	      }
	  else
	    maxrep = minrep;
	  if (_br(!(syntax_bits & RE_NO_BK_BRACES),0x4831740,7128,8))
	    {
	      if (_br(c != '\\',0x48317d0,7130,12))
		dfaerror("malformed repeat count");
	      FETCH(c, "unfinished repeat count");
	    }
	  if (_br(c != '}',0x4831e48,7134,8))
	    dfaerror("malformed repeat count");
	  laststart = 0;
	  return lasttok = REPMN;

	case '|':
	  if (syntax_bits & RE_LIMITED_OPS)
	    goto normal_char;
	  if (_br(backslash != ((syntax_bits & RE_NO_BK_VBAR) == 0),0x4832d68,7142,8))
	    goto normal_char;
	  laststart = 1;
	  return lasttok = OR;

	case '\n':
	  if (_br(syntax_bits & RE_LIMITED_OPS
	      || backslash
	      || !(syntax_bits & RE_NEWLINE_ALT),0x48339e8,7148,8))
	    goto normal_char;
	  laststart = 1;
	  return lasttok = OR;

	case '(':
	  if (_br(backslash != ((syntax_bits & RE_NO_BK_PARENS) == 0),0x4834280,7156,8))
	    goto normal_char;
	  ++parens;
	  laststart = 1;
	  return lasttok = LPAREN;

	case ')':
	  if (_br(backslash != ((syntax_bits & RE_NO_BK_PARENS) == 0),0x4834b60,7163,8))
	    goto normal_char;
	  if (parens == 0 && syntax_bits & RE_UNMATCHED_RIGHT_PAREN_ORD)
	    goto normal_char;
	  --parens;
	  laststart = 0;
	  return lasttok = RPAREN;

	case '.':
	  if (_br(backslash,0x4835660,7172,8))
	    goto normal_char;
	  zeroset(ccl);
	  notset(ccl);
	  if (_br(!(syntax_bits & RE_DOT_NEWLINE),0x4835b60,7176,8))
	    clrbit('\n', ccl);
	  if (syntax_bits & RE_DOT_NOT_NULL)
	    clrbit('\0', ccl);
	  laststart = 0;
	  return lasttok = CSET + charclass_index(ccl);

	case 'w':
	case 'W':
	  if (_br(!backslash,0x48363e0,7185,8))
	    goto normal_char;
	  zeroset(ccl);
	  for (c2 = 0; c2 < NOTCHAR; ++c2)
	    if (ISALNUM(c2))
	      setbit(c2, ccl);
	  if (_br(c == 'W',0x4836c00,7191,8))
	    notset(ccl);
	  laststart = 0;
	  return lasttok = CSET + charclass_index(ccl);
	
	case '[':
	  if (_br(backslash,0x4836f78,7197,8))
	    goto normal_char;
	  zeroset(ccl);
	  FETCH(c, "Unbalanced [");
	  if (_br(c == '^',0x48379a0,7201,8))
	    {
	      FETCH(c, "Unbalanced [");
	      invert = 1;
	    }
	  else
	    invert = 0;
	  do
	    {
	      /* Nobody ever said this had to be fast. :-)
		 Note that if we're looking at some other [:...:]
		 construct, we just treat it as a bunch of ordinary
		 characters.  We can do this because we assume
		 regex has checked for syntax errors before
		 dfa is ever called. */
	      if (_br(c == '[' && (syntax_bits & RE_CHAR_CLASSES),0x48381a8,7216,12))
		for (c1 = 0; _br(prednames[c1].name,0x4838398,7217,16); ++c1)
		  if (_br(looking_at(prednames[c1].name),0x4838518,7218,9))
		    {
		      for (c2 = 0; c2 < NOTCHAR; ++c2)
			if (_br((*prednames[c1].pred)(c2),0x48388b0,7221,8))
			  setbit(c2, ccl);
		      lexptr += strlen(prednames[c1].name);
		      lexleft -= strlen(prednames[c1].name);
		      FETCH(c1, "Unbalanced [");
		      goto skip;
		    }
	      if (_br(c == '\\' && (syntax_bits & RE_BACKSLASH_ESCAPE_IN_LISTS),0x4839780,7228,12))
		FETCH(c, "Unbalanced [");
	      FETCH(c1, "Unbalanced [");
	      if (_br(c1 == '-',0x483a120,7231,12))
		{
		  FETCH(c2, "Unbalanced [");
		  if (_br(c2 == ']',0x483a620,7234,9))
		    {
		      /* In the case [x-], the - is an ordinary hyphen,
			 which is left in c1, the lookahead character. */
		      --lexptr;
		      ++lexleft;
		      c2 = c;
		    }
		  else
		    {
		      if (_br(c2 == '\\'
			  && (syntax_bits & RE_BACKSLASH_ESCAPE_IN_LISTS),0x483a8f8,7244,13))
			FETCH(c2, "Unbalanced [");
		      FETCH(c1, "Unbalanced [");
		    }
		}
	      else
		c2 = c;
	      while (_br(c <= c2,0x483b440,7252,15))
		{
		  setbit(c, ccl);
		  if (_br(case_fold,0x483b5a0,7255,9))
		    if (ISUPPER(c))
		      setbit(tolower(c), ccl);
		    else if (ISLOWER(c))
		      setbit(toupper(c), ccl);
		  ++c;
		}
	    skip:
	      ;
	    }
	  while ((c = c1) != ']');
	  if (_br(invert,0x483c258,7266,8))
	    {
	      notset(ccl);
	      if (syntax_bits & RE_HAT_LISTS_NOT_NEWLINE)
		clrbit('\n', ccl);
	    }
	  laststart = 0;
	  return lasttok = CSET + charclass_index(ccl);

	default:
	normal_char:
	  laststart = 0;
	  if (case_fold && ISALPHA(c))
	    {
	      zeroset(ccl);
	      setbit(c, ccl);
	      if (isupper(c))
		setbit(tolower(c), ccl);
	      else
		setbit(toupper(c), ccl);
	      return lasttok = CSET + charclass_index(ccl);
	    }
	  return c;
	}
    }

  /* The above loop should consume at most a backslash
     and some other character. */
  abort();
}

/* Recursive descent parser for regular expressions. */

static token tok;		/* Lookahead token. */
static depth;			/* Current depth of a hypothetical stack
				   holding deferred productions.  This is
				   used to determine the depth that will be
				   required of the real stack later on in
				   dfaanalyze(). */

/* Add the given token to the parse tree, maintaining the depth count and
   updating the maximum depth if necessary. */
static void
addtok(t)
     token t;
{
  REALLOC_IF_NECESSARY(dfa->tokens, token, dfa->talloc, dfa->tindex);
  dfa->tokens[dfa->tindex++] = t;

  switch (t)
    {
    case QMARK:
    case STAR:
    case PLUS:
      break;

    case CAT:
    case OR:
    case ORTOP:
      --depth;
      break;

    default:
      ++dfa->nleaves;
    case EMPTY:
      ++depth;
      break;
    }
  if (_br(depth > dfa->depth,0x483ee80,7334,7))
    dfa->depth = depth;
}

/* The grammar understood by the parser is as follows.

   regexp:
     regexp OR branch
     branch

   branch:
     branch closure
     closure

   closure:
     closure QMARK
     closure STAR
     closure PLUS
     atom

   atom:
     <normal character>
     CSET
     BACKREF
     BEGLINE
     ENDLINE
     BEGWORD
     ENDWORD
     LIMWORD
     NOTLIMWORD
     <empty>

   The parser builds a parse tree in postfix form in an array of tokens. */

#if __STDC__
static void regexp(int);
#else
static void regexp();
#endif

static void
atom()
{
  if (_br((tok >= 0 && tok < NOTCHAR) || tok >= CSET || tok == BACKREF
      || tok == BEGLINE || tok == ENDLINE || tok == BEGWORD
      || tok == ENDWORD || tok == LIMWORD || tok == NOTLIMWORD,0x48419d0,7377,7))
    {
      addtok(tok);
      tok = lex();
    }
  else if (_br(tok == LPAREN,0x4841c78,7384,12))
    {
      tok = lex();
      regexp(0);
      if (_br(tok != RPAREN,0x4841e90,7388,11))
	dfaerror("Unbalanced (");
      tok = lex();
    }
  else
    addtok(EMPTY);
}

/* Return the number of tokens in the given subexpression. */
static int
nsubtoks(tindex)
{
  int ntoks1;

  switch (dfa->tokens[tindex - 1])
    {
    default:
      return 1;
    case QMARK:
    case STAR:
    case PLUS:
      return 1 + nsubtoks(tindex - 1);
    case CAT:
    case OR:
    case ORTOP:
      ntoks1 = nsubtoks(tindex - 1);
      return 1 + ntoks1 + nsubtoks(tindex - 1 - ntoks1);
    }
}

/* Copy the given subexpression to the top of the tree. */
static void
copytoks(tindex, ntokens)
     int tindex, ntokens;
{
  int i;

  for (i = 0; _br(i < ntokens,0x4843048,7425,15); ++i)
    addtok(dfa->tokens[tindex + i]);
}

static void
closure()
{
  int tindex, ntokens, i;

  atom();
  while (_br(tok == QMARK || tok == STAR || tok == PLUS || tok == REPMN,0x4843ef8,7435,10))
    if (_br(tok == REPMN,0x4843fa0,7436,9))
      {
	ntokens = nsubtoks(dfa->tindex);
	tindex = dfa->tindex - ntokens;
	if (_br(maxrep == 0,0x48442c0,7440,6))
	  addtok(PLUS);
	if (_br(minrep == 0,0x4844418,7442,6))
	  addtok(QMARK);
	for (i = 1; _br(i < minrep,0x4844600,7444,14); ++i)
	  {
	    copytoks(tindex, ntokens);
	    addtok(CAT);
	  }
	for (; _br(i < maxrep,0x48448e0,7449,9); ++i)
	  {
	    copytoks(tindex, ntokens);
	    addtok(QMARK);
	    addtok(CAT);
	  }
	tok = lex();
      }
    else
      {
	addtok(tok);
	tok = lex();
      }
}

static void
branch()
{
  closure();
  while (_br(tok != RPAREN && tok != OR && tok >= 0,0x4845250,7468,10))
    {
      closure();
      addtok(CAT);
    }
}

static void
regexp(toplevel)
     int toplevel;
{
  branch();
  while (_br(tok == OR,0x4845610,7480,10))
    {
      tok = lex();
      branch();
      if (_br(toplevel,0x4845780,7484,11))
	addtok(ORTOP);
      else
	addtok(OR);
    }
}

/* Main entry point for the parser.  S is a string to be parsed, len is the
   length of the string, so s can include NUL characters.  D is a pointer to
   the struct dfa to parse into. */
void
dfaparse(s, len, d)
     char *s;
     size_t len;
     struct dfa *d;

{
  dfa = d;
  lexstart = lexptr = s;
  lexleft = len;
  lasttok = END;
  laststart = 1;
  parens = 0;

  if (_br(! syntax_bits_set,0x48464a0,7508,7))
    dfaerror("No syntax specified");

  tok = lex();
  depth = d->depth;

  regexp(1);

  if (_br(tok != END,0x4846868,7516,7))
    dfaerror("Unbalanced )");

  addtok(END - d->nregexps);
  addtok(CAT);

  if (_br(d->nregexps,0x4846be8,7522,7))
    addtok(ORTOP);

  ++d->nregexps;
}

/* Some primitives for operating on sets of positions. */

/* Copy one set to another; the destination must be large enough. */
static void
copy(src, dst)
     position_set *src;
     position_set *dst;
{
  int i;

  for (i = 0; _br(i < src->nelem,0x4847208,7538,15); ++i)
    dst->elems[i] = src->elems[i];
  dst->nelem = src->nelem;
}

/* Insert a position in a set.  Position sets are maintained in sorted
   order according to index.  If position already exists in the set with
   the same index then their constraints are logically or'd together.
   S->elems must point to an array large enough to hold the resulting set. */
static void
insert(p, s)
     position p;
     position_set *s;
{
  int i;
  position t1, t2;

  for (i = 0; i < s->nelem && p.index < s->elems[i].index; ++i)
    ;
  if (i < s->nelem && p.index == s->elems[i].index)
    s->elems[i].constraint |= p.constraint;
  else
    {
      t1 = p;
      ++s->nelem;
      while (_br(i < s->nelem,0x4848898,7563,14))
	{
	  t2 = s->elems[i];
	  s->elems[i++] = t1;
	  t1 = t2;
	}
    }
}

/* Merge two sets of positions into a third.  The result is exactly as if
   the positions of both sets were inserted into an initially empty set. */
static void
merge(s1, s2, m)
     position_set *s1;
     position_set *s2;
     position_set *m;
{
  int i = 0, j = 0;

  m->nelem = 0;
  while (_br(i < s1->nelem && j < s2->nelem,0x48493f0,7583,10))
    if (s1->elems[i].index > s2->elems[j].index)
      m->elems[m->nelem++] = s1->elems[i++];
    else if (s1->elems[i].index < s2->elems[j].index)
      m->elems[m->nelem++] = s2->elems[j++];
    else
      {
	m->elems[m->nelem] = s1->elems[i++];
	m->elems[m->nelem++].constraint |= s2->elems[j++].constraint;
      }
  while (_br(i < s1->nelem,0x484e5d8,7593,10))
    m->elems[m->nelem++] = s1->elems[i++];
  while (_br(j < s2->nelem,0x484e980,7595,10))
    m->elems[m->nelem++] = s2->elems[j++];
}

/* Delete a position from a set. */
static void
delete(p, s)
     position p;
     position_set *s;
{
  int i;

  for (i = 0; _br(i < s->nelem,0x484f020,7607,15); ++i)
    if (p.index == s->elems[i].index)
      break;
  if (_br(i < s->nelem,0x484f3c0,7610,7))
    for (--s->nelem; _br(i < s->nelem,0x484f550,7611,22); ++i)
      s->elems[i] = s->elems[i + 1];
}

/* Find the index of the state corresponding to the given position set with
   the given preceding context, or create a new state if there is no such
   state.  Newline and letter tell whether we got here on a newline or
   letter, respectively. */
static int
state_index(d, s, newline, letter)
     struct dfa *d;
     position_set *s;
     int newline;
     int letter;
{
  int hash = 0;
  int constraint;
  int i, j;

  newline = newline ? 1 : 0;
  letter = letter ? 1 : 0;

  for (i = 0; _br(i < s->nelem,0x4850200,7633,15); ++i)
    hash ^= s->elems[i].index + s->elems[i].constraint;

  /* Try to find a state that exactly matches the proposed one. */
  for (i = 0; _br(i < d->sindex,0x4850730,7637,15); ++i)
    {
      if (_br(hash != d->states[i].hash || s->nelem != d->states[i].elems.nelem
	  || newline != d->states[i].newline || letter != d->states[i].letter,0x4850f68,7639,11))
	continue;
      for (j = 0; _br(j < s->nelem,0x4851110,7642,19); ++j)
	if (s->elems[j].constraint
	    != d->states[i].elems.elems[j].constraint
	    || s->elems[j].index != d->states[i].elems.elems[j].index)
	  break;
      if (_br(j == s->nelem,0x4851a40,7647,11))
	return i;
    }

  /* We'll have to create a new state. */
  REALLOC_IF_NECESSARY(d->states, dfa_state, d->salloc, d->sindex);
  d->states[i].hash = hash;
  MALLOC(d->states[i].elems.elems, position, s->nelem);
  copy(s, &d->states[i].elems);
  d->states[i].newline = newline;
  d->states[i].letter = letter;
  d->states[i].backref = 0;
  d->states[i].constraint = 0;
  d->states[i].first_end = 0;
  for (j = 0; _br(j < s->nelem,0x4853440,7661,15); ++j)
    if (_br(d->tokens[s->elems[j].index] < 0,0x4853700,7662,9))
      {
	constraint = s->elems[j].constraint;
	if (SUCCEEDS_IN_CONTEXT(constraint, newline, 0, letter, 0)
	    || SUCCEEDS_IN_CONTEXT(constraint, newline, 0, letter, 1)
	    || SUCCEEDS_IN_CONTEXT(constraint, newline, 1, letter, 0)
	    || SUCCEEDS_IN_CONTEXT(constraint, newline, 1, letter, 1))
	  d->states[i].constraint |= constraint;
	if (_br(! d->states[i].first_end,0x4855688,7670,6))
	  d->states[i].first_end = d->tokens[s->elems[j].index];
      }
    else if (_br(d->tokens[s->elems[j].index] == BACKREF,0x4855cf0,7673,14))
      {
	d->states[i].constraint = NO_CONSTRAINT;
	d->states[i].backref = 1;
      }

  ++d->sindex;

  return i;
}

/* Find the epsilon closure of a set of positions.  If any position of the set
   contains a symbol that matches the empty string in some context, replace
   that position with the elements of its follow labeled with an appropriate
   constraint.  Repeat exhaustively until no funny positions are left.
   S->elems must be large enough to hold the result. */
void
epsclosure(s, d)
     position_set *s;
     struct dfa *d;
{
  int i, j;
  int *visited;
  position p, old;

  MALLOC(visited, int, d->tindex);
  for (i = 0; _br(i < d->tindex,0x4856b88,7699,15); ++i)
    visited[i] = 0;

  for (i = 0; _br(i < s->nelem,0x4856e60,7702,15); ++i)
    if (_br(d->tokens[s->elems[i].index] >= NOTCHAR
	&& d->tokens[s->elems[i].index] != BACKREF
	&& d->tokens[s->elems[i].index] < CSET,0x48576d8,7703,9))
      {
	old = s->elems[i];
	p.constraint = old.constraint;
	delete(s->elems[i], s);
	if (_br(visited[old.index],0x4857c08,7710,6))
	  {
	    --i;
	    continue;
	  }
	visited[old.index] = 1;
	switch (d->tokens[old.index])
	  {
	  case BEGLINE:
	    p.constraint &= BEGLINE_CONSTRAINT;
	    break;
	  case ENDLINE:
	    p.constraint &= ENDLINE_CONSTRAINT;
	    break;
	  case BEGWORD:
	    p.constraint &= BEGWORD_CONSTRAINT;
	    break;
	  case ENDWORD:
	    p.constraint &= ENDWORD_CONSTRAINT;
	    break;
	  case LIMWORD:
	    p.constraint &= LIMWORD_CONSTRAINT;
	    break;
	  case NOTLIMWORD:
	    p.constraint &= NOTLIMWORD_CONSTRAINT;
	    break;
	  default:
	    break;
	  }
	for (j = 0; _br(j < d->follows[old.index].nelem,0x48589d0,7739,14); ++j)
	  {
	    p.index = d->follows[old.index].elems[j].index;
	    insert(p, s);
	  }
	/* Force rescan to start at the beginning. */
	i = -1;
      }

  free(visited);
}

/* Perform bottom-up analysis on the parse tree, computing various functions.
   Note that at this point, we're pretending constructs like \< are real
   characters rather than constraints on what can follow them.

   Nullable:  A node is nullable if it is at the root of a regexp that can
   match the empty string.
   *  EMPTY leaves are nullable.
   * No other leaf is nullable.
   * A QMARK or STAR node is nullable.
   * A PLUS node is nullable if its argument is nullable.
   * A CAT node is nullable if both its arguments are nullable.
   * An OR node is nullable if either argument is nullable.

   Firstpos:  The firstpos of a node is the set of positions (nonempty leaves)
   that could correspond to the first character of a string matching the
   regexp rooted at the given node.
   * EMPTY leaves have empty firstpos.
   * The firstpos of a nonempty leaf is that leaf itself.
   * The firstpos of a QMARK, STAR, or PLUS node is the firstpos of its
     argument.
   * The firstpos of a CAT node is the firstpos of the left argument, union
     the firstpos of the right if the left argument is nullable.
   * The firstpos of an OR node is the union of firstpos of each argument.

   Lastpos:  The lastpos of a node is the set of positions that could
   correspond to the last character of a string matching the regexp at
   the given node.
   * EMPTY leaves have empty lastpos.
   * The lastpos of a nonempty leaf is that leaf itself.
   * The lastpos of a QMARK, STAR, or PLUS node is the lastpos of its
     argument.
   * The lastpos of a CAT node is the lastpos of its right argument, union
     the lastpos of the left if the right argument is nullable.
   * The lastpos of an OR node is the union of the lastpos of each argument.

   Follow:  The follow of a position is the set of positions that could
   correspond to the character following a character matching the node in
   a string matching the regexp.  At this point we consider special symbols
   that match the empty string in some context to be just normal characters.
   Later, if we find that a special symbol is in a follow set, we will
   replace it with the elements of its follow, labeled with an appropriate
   constraint.
   * Every node in the firstpos of the argument of a STAR or PLUS node is in
     the follow of every node in the lastpos.
   * Every node in the firstpos of the second argument of a CAT node is in
     the follow of every node in the lastpos of the first argument.

   Because of the postfix representation of the parse tree, the depth-first
   analysis is conveniently done by a linear scan with the aid of a stack.
   Sets are stored as arrays of the elements, obeying a stack-like allocation
   scheme; the number of elements in each set deeper in the stack can be
   used to determine the address of a particular set's array. */
void
dfaanalyze(d, searchflag)
     struct dfa *d;
     int searchflag;
{
  int *nullable;		/* Nullable stack. */
  int *nfirstpos;		/* Element count stack for firstpos sets. */
  position *firstpos;		/* Array where firstpos elements are stored. */
  int *nlastpos;		/* Element count stack for lastpos sets. */
  position *lastpos;		/* Array where lastpos elements are stored. */
  int *nalloc;			/* Sizes of arrays allocated to follow sets. */
  position_set tmp;		/* Temporary set for merging sets. */
  position_set merged;		/* Result of merging sets. */
  int wants_newline;		/* True if some position wants newline info. */
  int *o_nullable;
  int *o_nfirst, *o_nlast;
  position *o_firstpos, *o_lastpos;
  int i, j;
  position *pos;

#ifdef DEBUG
  fprintf(stderr, "dfaanalyze:\n");
  for (i = 0; i < d->tindex; ++i)
    {
      fprintf(stderr, " %d:", i);
      prtok(d->tokens[i]);
    }
  putc('\n', stderr);
#endif

  d->searchflag = searchflag;

  MALLOC(nullable, int, d->depth);
  o_nullable = nullable;
  MALLOC(nfirstpos, int, d->depth);
  o_nfirst = nfirstpos;
  MALLOC(firstpos, position, d->nleaves);
  o_firstpos = firstpos, firstpos += d->nleaves;
  MALLOC(nlastpos, int, d->depth);
  o_nlast = nlastpos;
  MALLOC(lastpos, position, d->nleaves);
  o_lastpos = lastpos, lastpos += d->nleaves;
  MALLOC(nalloc, int, d->tindex);
  for (i = 0; _br(i < d->tindex,0x485b188,7846,15); ++i)
    nalloc[i] = 0;
  MALLOC(merged.elems, position, d->nleaves);

  CALLOC(d->follows, position_set, d->tindex);

  for (i = 0; _br(i < d->tindex,0x485b960,7852,15); ++i)
#ifdef DEBUG
    {				/* Nonsyntactic #ifdef goo... */
#endif
    switch (d->tokens[i])
      {
      case EMPTY:
	/* The empty set is nullable. */
	*nullable++ = 1;

	/* The firstpos and lastpos of the empty leaf are both empty. */
	*nfirstpos++ = *nlastpos++ = 0;
	break;

      case STAR:
      case PLUS:
	/* Every element in the firstpos of the argument is in the follow
	   of every element in the lastpos. */
	tmp.nelem = nfirstpos[-1];
	tmp.elems = firstpos;
	pos = lastpos;
	for (j = 0; _br(j < nlastpos[-1],0x485c2a0,7873,14); ++j)
	  {
	    merge(&tmp, &d->follows[pos[j].index], &merged);
	    REALLOC_IF_NECESSARY(d->follows[pos[j].index].elems, position,
				 nalloc[pos[j].index], merged.nelem - 1);
	    copy(&merged, &d->follows[pos[j].index]);
	  }

      case QMARK:
	/* A QMARK or STAR node is automatically nullable. */
	if (_br(d->tokens[i] != PLUS,0x485d9e8,7883,6))
	  nullable[-1] = 1;
	break;

      case CAT:
	/* Every element in the firstpos of the second argument is in the
	   follow of every element in the lastpos of the first argument. */
	tmp.nelem = nfirstpos[-1];
	tmp.elems = firstpos;
	pos = lastpos + nlastpos[-1];
	for (j = 0; _br(j < nlastpos[-2],0x485e098,7893,14); ++j)
	  {
	    merge(&tmp, &d->follows[pos[j].index], &merged);
	    REALLOC_IF_NECESSARY(d->follows[pos[j].index].elems, position,
				 nalloc[pos[j].index], merged.nelem - 1);
	    copy(&merged, &d->follows[pos[j].index]);
	  }

	/* The firstpos of a CAT node is the firstpos of the first argument,
	   union that of the second argument if the first is nullable. */
	if (_br(nullable[-2],0x485f6e8,7903,6))
	  nfirstpos[-2] += nfirstpos[-1];
	else
	  firstpos += nfirstpos[-1];
	--nfirstpos;

	/* The lastpos of a CAT node is the lastpos of the second argument,
	   union that of the first argument if the second is nullable. */
	if (_br(nullable[-1],0x485fae8,7911,6))
	  nlastpos[-2] += nlastpos[-1];
	else
	  {
	    pos = lastpos + nlastpos[-2];
	    for (j = nlastpos[-1] - 1; _br(j >= 0,0x485ffd0,7916,33); --j)
	      pos[j] = lastpos[j];
	    lastpos += nlastpos[-2];
	    nlastpos[-2] = nlastpos[-1];
	  }
	--nlastpos;

	/* A CAT node is nullable if both arguments are nullable. */
	nullable[-2] = nullable[-1] && nullable[-2];
	--nullable;
	break;

      case OR:
      case ORTOP:
	/* The firstpos is the union of the firstpos of each argument. */
	nfirstpos[-2] += nfirstpos[-1];
	--nfirstpos;

	/* The lastpos is the union of the lastpos of each argument. */
	nlastpos[-2] += nlastpos[-1];
	--nlastpos;

	/* An OR node is nullable if either argument is nullable. */
	nullable[-2] = nullable[-1] || nullable[-2];
	--nullable;
	break;

      default:
	/* Anything else is a nonempty position.  (Note that special
	   constructs like \< are treated as nonempty strings here;
	   an "epsilon closure" effectively makes them nullable later.
	   Backreferences have to get a real position so we can detect
	   transitions on them later.  But they are nullable. */
	*nullable++ = d->tokens[i] == BACKREF;

	/* This position is in its own firstpos and lastpos. */
	*nfirstpos++ = *nlastpos++ = 1;
	--firstpos, --lastpos;
	firstpos->index = lastpos->index = i;
	firstpos->constraint = lastpos->constraint = NO_CONSTRAINT;

	/* Allocate the follow set for this position. */
	nalloc[i] = 1;
	MALLOC(d->follows[i].elems, position, nalloc[i]);
	break;
      }
#ifdef DEBUG
    /* ... balance the above nonsyntactic #ifdef goo... */
      fprintf(stderr, "node %d:", i);
      prtok(d->tokens[i]);
      putc('\n', stderr);
      fprintf(stderr, nullable[-1] ? " nullable: yes\n" : " nullable: no\n");
      fprintf(stderr, " firstpos:");
      for (j = nfirstpos[-1] - 1; j >= 0; --j)
	{
	  fprintf(stderr, " %d:", firstpos[j].index);
	  prtok(d->tokens[firstpos[j].index]);
	}
      fprintf(stderr, "\n lastpos:");
      for (j = nlastpos[-1] - 1; j >= 0; --j)
	{
	  fprintf(stderr, " %d:", lastpos[j].index);
	  prtok(d->tokens[lastpos[j].index]);
	}
      putc('\n', stderr);
    }
#endif

  /* For each follow set that is the follow set of a real position, replace
     it with its epsilon closure. */
  for (i = 0; _br(i < d->tindex,0x4861e00,7986,15); ++i)
    if (_br(d->tokens[i] < NOTCHAR || d->tokens[i] == BACKREF
	|| d->tokens[i] >= CSET,0x4862360,7987,9))
      {
#ifdef DEBUG
	fprintf(stderr, "follows(%d:", i);
	prtok(d->tokens[i]);
	fprintf(stderr, "):");
	for (j = d->follows[i].nelem - 1; j >= 0; --j)
	  {
	    fprintf(stderr, " %d:", d->follows[i].elems[j].index);
	    prtok(d->tokens[d->follows[i].elems[j].index]);
	  }
	putc('\n', stderr);
#endif
	copy(&d->follows[i], &merged);
	epsclosure(&merged, d);
	if (_br(d->follows[i].nelem < merged.nelem,0x4862820,8003,6))
	  REALLOC(d->follows[i].elems, position, merged.nelem);
	copy(&merged, &d->follows[i]);
      }

  /* Get the epsilon closure of the firstpos of the regexp.  The result will
     be the set of positions of state 0. */
  merged.nelem = 0;
  for (i = 0; _br(i < nfirstpos[-1],0x4863238,8011,15); ++i)
    insert(firstpos[i], &merged);
  epsclosure(&merged, d);

  /* Check if any of the positions of state 0 will want newline context. */
  wants_newline = 0;
  for (i = 0; _br(i < merged.nelem,0x48636f8,8017,15); ++i)
    if (PREV_NEWLINE_DEPENDENT(merged.elems[i].constraint))
      wants_newline = 1;

  /* Build the initial state. */
  d->salloc = 1;
  d->sindex = 0;
  MALLOC(d->states, dfa_state, d->salloc);
  state_index(d, &merged, wants_newline, 0);

  free(o_nullable);
  free(o_nfirst);
  free(o_firstpos);
  free(o_nlast);
  free(o_lastpos);
  free(nalloc);
  free(merged.elems);
}

/* Find, for each character, the transition out of state s of d, and store
   it in the appropriate slot of trans.

   We divide the positions of s into groups (positions can appear in more
   than one group).  Each group is labeled with a set of characters that
   every position in the group matches (taking into account, if necessary,
   preceding context information of s).  For each group, find the union
   of the its elements' follows.  This set is the set of positions of the
   new state.  For each character in the group's label, set the transition
   on this character to be to a state corresponding to the set's positions,
   and its associated backward context information, if necessary.

   If we are building a searching matcher, we include the positions of state
   0 in every state.

   The collection of groups is constructed by building an equivalence-class
   partition of the positions of s.

   For each position, find the set of characters C that it matches.  Eliminate
   any characters from C that fail on grounds of backward context.

   Search through the groups, looking for a group whose label L has nonempty
   intersection with C.  If L - C is nonempty, create a new group labeled
   L - C and having the same positions as the current group, and set L to
   the intersection of L and C.  Insert the position in this group, set
   C = C - L, and resume scanning.

   If after comparing with every group there are characters remaining in C,
   create a new group labeled with the characters of C and insert this
   position in that group. */
void
dfastate(s, d, trans)
     int s;
     struct dfa *d;
     int trans[];
{
  position_set grps[NOTCHAR];	/* As many as will ever be needed. */
  charclass labels[NOTCHAR];	/* Labels corresponding to the groups. */
  int ngrps = 0;		/* Number of groups actually used. */
  position pos;			/* Current position being considered. */
  charclass matches;		/* Set of matching characters. */
  int matchesf;			/* True if matches is nonempty. */
  charclass intersect;		/* Intersection with some label set. */
  int intersectf;		/* True if intersect is nonempty. */
  charclass leftovers;		/* Stuff in the label that didn't match. */
  int leftoversf;		/* True if leftovers is nonempty. */
  static charclass letters;	/* Set of characters considered letters. */
  static charclass newline;	/* Set of characters that aren't newline. */
  position_set follows;		/* Union of the follows of some group. */
  position_set tmp;		/* Temporary space for merging sets. */
  int state;			/* New state. */
  int wants_newline;		/* New state wants to know newline context. */
  int state_newline;		/* New state on a newline transition. */
  int wants_letter;		/* New state wants to know letter context. */
  int state_letter;		/* New state on a letter transition. */
  static initialized;		/* Flag for static initialization. */
  int i, j, k;

  /* Initialize the set of letters, if necessary. */
  if (_br(! initialized,0x4865d00,8095,7))
    {
      initialized = 1;
      for (i = 0; i < NOTCHAR; ++i)
	if (ISALNUM(i))
	  setbit(i, letters);
      setbit('\n', newline);
    }

  zeroset(matches);

  for (i = 0; _br(i < d->states[s].elems.nelem,0x48667f0,8106,15); ++i)
    {
      pos = d->states[s].elems.elems[i];
      if (d->tokens[pos.index] >= 0 && d->tokens[pos.index] < NOTCHAR)
	setbit(d->tokens[pos.index], matches);
      else if (_br(d->tokens[pos.index] >= CSET,0x4867230,8111,16))
	copyset(d->charclasses[d->tokens[pos.index] - CSET], matches);
      else
	continue;

      /* Some characters may need to be eliminated from matches because
	 they fail in the current context. */
      if (_br(pos.constraint != 0xFF,0x48676b8,8118,11))
	{
	  if (! MATCHES_NEWLINE_CONTEXT(pos.constraint,
					 d->states[s].newline, 1))
	    clrbit('\n', matches);
	  if (! MATCHES_NEWLINE_CONTEXT(pos.constraint,
					 d->states[s].newline, 0))
	    for (j = 0; j < CHARCLASS_INTS; ++j)
	      matches[j] &= newline[j];
	  if (! MATCHES_LETTER_CONTEXT(pos.constraint,
					d->states[s].letter, 1))
	    for (j = 0; j < CHARCLASS_INTS; ++j)
	      matches[j] &= ~letters[j];
	  if (! MATCHES_LETTER_CONTEXT(pos.constraint,
					d->states[s].letter, 0))
	    for (j = 0; j < CHARCLASS_INTS; ++j)
	      matches[j] &= letters[j];

	  /* If there are no characters left, there's no point in going on. */
	  for (j = 0; _br(j < CHARCLASS_INTS && !matches[j],0x486cc38,8137,16); ++j)
	    ;
	  if (j == CHARCLASS_INTS)
	    continue;
	}

      for (j = 0; _br(j < ngrps,0x486d220,8143,19); ++j)
	{
	  /* If matches contains a single character only, and the current
	     group's label doesn't contain that character, go on to the
	     next group. */
	  if (_br(d->tokens[pos.index] >= 0 && d->tokens[pos.index] < NOTCHAR
	      && !tstbit(d->tokens[pos.index], labels[j]),0x486d928,8148,8))
	    continue;

	  /* Check if this group's label has a nonempty intersection with
	     matches. */
	  intersectf = 0;
	  for (k = 0; k < CHARCLASS_INTS; ++k)
	    (intersect[k] = matches[k] & labels[j][k]) ? intersectf = 1 : 0;
	  if (_br(! intersectf,0x486e288,8157,8))
	    continue;

	  /* It does; now find the set differences both ways. */
	  leftoversf = matchesf = 0;
	  for (k = 0; k < CHARCLASS_INTS; ++k)
	    {
	      /* Even an optimizing compiler can't know this for sure. */
	      int match = matches[k], label = labels[j][k];

	      (leftovers[k] = ~match & label) ? leftoversf = 1 : 0;
	      (matches[k] = match & ~label) ? matchesf = 1 : 0;
	    }

	  /* If there were leftovers, create a new group labeled with them. */
	  if (_br(leftoversf,0x486f080,8172,8))
	    {
	      copyset(leftovers, labels[ngrps]);
	      copyset(intersect, labels[j]);
	      MALLOC(grps[ngrps].elems, position, d->nleaves);
	      copy(&grps[j], &grps[ngrps]);
	      ++ngrps;
	    }

	  /* Put the position in the current group.  Note that there is no
	     reason to call insert() here. */
	  grps[j].elems[grps[j].nelem++] = pos;

	  /* If every character matching the current position has been
	     accounted for, we're done. */
	  if (_br(! matchesf,0x486fc40,8187,8))
	    break;
	}

      /* If we've passed the last group, and there are still characters
	 unaccounted for, then we'll have to create a new group. */
      if (_br(j == ngrps,0x486fdb8,8193,11))
	{
	  copyset(matches, labels[ngrps]);
	  zeroset(matches);
	  MALLOC(grps[ngrps].elems, position, d->nleaves);
	  grps[ngrps].nelem = 1;
	  grps[ngrps].elems[0] = pos;
	  ++ngrps;
	}
    }

  MALLOC(follows.elems, position, d->nleaves);
  MALLOC(tmp.elems, position, d->nleaves);

  /* If we are a searching matcher, the default transition is to a state
     containing the positions of state 0, otherwise the default transition
     is to fail miserably. */
  if (_br(d->searchflag,0x4870cc0,8210,7))
    {
      wants_newline = 0;
      wants_letter = 0;
      for (i = 0; _br(i < d->states[0].elems.nelem,0x4870fc8,8214,19); ++i)
	{
	  if (PREV_NEWLINE_DEPENDENT(d->states[0].elems.elems[i].constraint))
	    wants_newline = 1;
	  if (PREV_LETTER_DEPENDENT(d->states[0].elems.elems[i].constraint))
	    wants_letter = 1;
	}
      copy(&d->states[0].elems, &follows);
      state = state_index(d, &follows, 0, 0);
      if (_br(wants_newline,0x48721a0,8223,11))
	state_newline = state_index(d, &follows, 1, 0);
      else
	state_newline = state;
      if (_br(wants_letter,0x4872448,8227,11))
	state_letter = state_index(d, &follows, 0, 1);
      else
	state_letter = state;
      for (i = 0; i < NOTCHAR; ++i)
	if (_br(i == '\n',0x48728c8,8232,6))
	  trans[i] = state_newline;
	else if (ISALNUM(i))
	  trans[i] = state_letter;
	else
	  trans[i] = state;
    }
  else
    for (i = 0; i < NOTCHAR; ++i)
      trans[i] = -1;

  for (i = 0; _br(i < ngrps,0x4873480,8243,15); ++i)
    {
      follows.nelem = 0;

      /* Find the union of the follows of the positions of the group.
	 This is a hideously inefficient loop.  Fix it someday. */
      for (j = 0; _br(j < grps[i].nelem,0x4873740,8249,19); ++j)
	for (k = 0; _br(k < d->follows[grps[i].elems[j].index].nelem,0x4873b18,8250,14); ++k)
	  insert(d->follows[grps[i].elems[j].index].elems[k], &follows);

      /* If we are building a searching matcher, throw in the positions
	 of state 0 as well. */
      if (_br(d->searchflag,0x4874068,8255,11))
	for (j = 0; _br(j < d->states[0].elems.nelem,0x4874290,8256,14); ++j)
	  insert(d->states[0].elems.elems[j], &follows);

      /* Find out if the new state will want any context information. */
      wants_newline = 0;
      if (_br(tstbit('\n', labels[i]),0x4874780,8261,11))
	for (j = 0; _br(j < follows.nelem,0x48748f8,8262,14); ++j)
	  if (PREV_NEWLINE_DEPENDENT(follows.elems[j].constraint))
	    wants_newline = 1;

      wants_letter = 0;
      for (j = 0; j < CHARCLASS_INTS; ++j)
	if (_br(labels[i][j] & letters[j],0x4875558,8268,6))
	  break;
      if (j < CHARCLASS_INTS)
	for (j = 0; _br(j < follows.nelem,0x4875aa0,8271,14); ++j)
	  if (PREV_LETTER_DEPENDENT(follows.elems[j].constraint))
	    wants_letter = 1;

      /* Find the state(s) corresponding to the union of the follows. */
      state = state_index(d, &follows, 0, 0);
      if (_br(wants_newline,0x4876220,8277,11))
	state_newline = state_index(d, &follows, 1, 0);
      else
	state_newline = state;
      if (_br(wants_letter,0x48764c8,8281,11))
	state_letter = state_index(d, &follows, 0, 1);
      else
	state_letter = state;

      /* Set the transitions for each character in the current label. */
      for (j = 0; j < CHARCLASS_INTS; ++j)
	for (k = 0; k < INTBITS; ++k)
	  if (_br(labels[i][j] & 1 << k,0x4876f38,8289,8))
	    {
	      int c = j * INTBITS + k;

	      if (_br(c == '\n',0x4877230,8293,12))
		trans[c] = state_newline;
	      else if (ISALNUM(c))
		trans[c] = state_letter;
	      else if (c < NOTCHAR)
		trans[c] = state;
	    }
    }

  for (i = 0; _br(i < ngrps,0x4877cc8,8302,15); ++i)
    free(grps[i].elems);
  free(follows.elems);
  free(tmp.elems);
}

/* Some routines for manipulating a compiled dfa's transition tables.
   Each state may or may not have a transition table; if it does, and it
   is a non-accepting state, then d->trans[state] points to its table.
   If it is an accepting state then d->fails[state] points to its table.
   If it has no table at all, then d->trans[state] is NULL.
   TODO: Improve this comment, get rid of the unnecessary redundancy. */

static void
build_state(s, d)
     int s;
     struct dfa *d;
{
  int *trans;			/* The new transition table. */
  int i;

  /* Set an upper limit on the number of transition tables that will ever
     exist at once.  1024 is arbitrary.  The idea is that the frequently
     used transition tables will be quickly rebuilt, whereas the ones that
     were only needed once or twice will be cleared away. */
  if (_br(d->trcount >= 1024,0x4878628,8327,7))
    {
      for (i = 0; _br(i < d->tralloc,0x4878790,8329,19); ++i)
	if (_br(d->trans[i],0x48788f8,8330,6))
	  {
	    free((ptr_t) d->trans[i]);
	    d->trans[i] = NULL;
	  }
	else if (_br(d->fails[i],0x4878da0,8335,11))
	  {
	    free((ptr_t) d->fails[i]);
	    d->fails[i] = NULL;
	  }
      d->trcount = 0;
    }

  ++d->trcount;

  /* Set up the success bits for this state. */
  d->success[s] = 0;
  if (ACCEPTS_IN_CONTEXT(d->states[s].newline, 1, d->states[s].letter, 0,
      s, *d))
    d->success[s] |= 4;
  if (ACCEPTS_IN_CONTEXT(d->states[s].newline, 0, d->states[s].letter, 1,
      s, *d))
    d->success[s] |= 2;
  if (ACCEPTS_IN_CONTEXT(d->states[s].newline, 0, d->states[s].letter, 0,
      s, *d))
    d->success[s] |= 1;

  MALLOC(trans, int, NOTCHAR);
  dfastate(s, d, trans);

  /* Now go through the new transition table, and make sure that the trans
     and fail arrays are allocated large enough to hold a pointer for the
     largest state mentioned in the table. */
  for (i = 0; i < NOTCHAR; ++i)
    if (_br(trans[i] >= d->tralloc,0x487e980,8364,9))
      {
	int oldalloc = d->tralloc;

	while (_br(trans[i] >= d->tralloc,0x487ec18,8368,9))
	  d->tralloc *= 2;
	REALLOC(d->realtrans, int *, d->tralloc + 1);
	d->trans = d->realtrans + 1;
	REALLOC(d->fails, int *, d->tralloc);
	REALLOC(d->success, int, d->tralloc);
	REALLOC(d->newlines, int, d->tralloc);
	while (_br(oldalloc < d->tralloc,0x487ff30,8375,9))
	  {
	    d->trans[oldalloc] = NULL;
	    d->fails[oldalloc++] = NULL;
	  }
      }

  /* Keep the newline transition in a special place so we can use it as
     a sentinel. */
  d->newlines[s] = trans['\n'];
  trans['\n'] = -1;

  if (ACCEPTING(s, *d))
    d->fails[s] = trans;
  else
    d->trans[s] = trans;
}

static void
build_state_zero(d)
     struct dfa *d;
{
  d->tralloc = 1;
  d->trcount = 0;
  CALLOC(d->realtrans, int *, d->tralloc + 1);
  d->trans = d->realtrans + 1;
  CALLOC(d->fails, int *, d->tralloc);
  MALLOC(d->success, int, d->tralloc);
  MALLOC(d->newlines, int, d->tralloc);
  build_state(0, d);
}

/* Search through a buffer looking for a match to the given struct dfa.
   Find the first occurrence of a string matching the regexp in the buffer,
   and the shortest possible version thereof.  Return a pointer to the first
   character after the match, or NULL if none is found.  Begin points to
   the beginning of the buffer, and end points to the first character after
   its end.  We store a newline in *end to act as a sentinel, so end had
   better point somewhere valid.  Newline is a flag indicating whether to
   allow newlines to be in the matching string.  If count is non-
   NULL it points to a place we're supposed to increment every time we
   see a newline.  Finally, if backref is non-NULL it points to a place
   where we're supposed to store a 1 if backreferencing happened and the
   match needs to be verified by a backtracking matcher.  Otherwise
   we store a 0 in *backref. */
char *
dfaexec(d, begin, end, newline, count, backref)
     struct dfa *d;
     char *begin;
     char *end;
     int newline;
     int *count;
     int *backref;
{
  register s, s1, tmp;		/* Current state. */
  register unsigned char *p;	/* Current input character. */
  register **trans, *t;		/* Copy of d->trans so it can be optimized
				   into a register. */
  static sbit[NOTCHAR];	/* Table for anding with d->success. */
  static sbit_init;

  if (_br(! sbit_init,0x4882b18,8436,7))
    {
      int i;

      sbit_init = 1;
      for (i = 0; i < NOTCHAR; ++i)
	if (_br(i == '\n',0x4882e38,8442,6))
	  sbit[i] = 4;
	else if (ISALNUM(i))
	  sbit[i] = 2;
	else
	  sbit[i] = 1;
    }

  if (_br(! d->tralloc,0x4883608,8450,7))
    build_state_zero(d);

  s = s1 = 0;
  p = (unsigned char *) begin;
  trans = d->trans;
  *end = '\n';

  for (;;)
    {
      /* The dreaded inner loop. */
      if (_br((t = trans[s]) != 0,0x4883ba0,8461,11))
	do
	  {
	    s1 = t[*p++];
	    if (_br(! (t = trans[s1]),0x4883e48,8465,10))
	      goto last_was_s;
	    s = t[*p++];
	  }
        while ((t = trans[s]) != 0);
      goto last_was_s1;
    last_was_s:
      tmp = s, s = s1, s1 = tmp;
    last_was_s1:

      if (_br(s >= 0 && p <= (unsigned char *) end && d->fails[s],0x4884788,8475,11))
	{
	  if (_br(d->success[s] & sbit[*p],0x48849b8,8477,8))
	    {
	      if (_br(backref,0x4884a08,8479,12))
		if (_br(d->states[s].backref,0x4884b50,8480,7))
		  *backref = 1;
		else
		  *backref = 0;
	      return (char *) p;
	    }

	  s1 = s;
	  s = d->fails[s][*p++];
	  continue;
	}

      /* If the previous character was a newline, count it. */
      if (_br(count && (char *) p <= end && p[-1] == '\n',0x48853c8,8493,11))
	++*count;

      /* Check if we've run off the end of the buffer. */
      if (_br((char *) p > end,0x4885568,8497,11))
	return NULL;

      if (_br(s >= 0,0x48856d8,8500,11))
	{
	  build_state(s, d);
	  trans = d->trans;
	  continue;
	}

      if (_br(p[-1] == '\n' && newline,0x4885aa0,8507,11))
	{
	  s = d->newlines[s1];
	  continue;
	}

      s = 0;
    }
}

/* Initialize the components of a dfa that the other routines don't
   initialize for themselves. */
void
dfainit(d)
     struct dfa *d;
{
  d->calloc = 1;
  MALLOC(d->charclasses, charclass, d->calloc);
  d->cindex = 0;

  d->talloc = 1;
  MALLOC(d->tokens, token, d->talloc);
  d->tindex = d->depth = d->nleaves = d->nregexps = 0;

  d->searchflag = 0;
  d->tralloc = 0;

  d->musts = 0;
}

/* Parse and analyze a single string of the given length. */
void
dfacomp(s, len, d, searchflag)
     char *s;
     size_t len;
     struct dfa *d;
     int searchflag;
{
  if (_br(case_fold,0x4888a08,8545,7))	/* dummy folding in service of dfamust() */
    {
      char *copy;
      int i;

      copy = malloc(len);
      if (_br(!copy,0x4888cb0,8551,11))
	dfaerror("out of memory");
      
      /* This is a kludge. */
      case_fold = 0;
      for (i = 0; _br(i < len,0x4888f80,8556,19); ++i)
	if (ISUPPER(s[i]))
	  copy[i] = tolower(s[i]);
	else
	  copy[i] = s[i];

      dfainit(d);
      dfaparse(copy, len, d);
      free(copy);
      dfamust(d);
      d->cindex = d->tindex = d->depth = d->nleaves = d->nregexps = 0;
      case_fold = 1;
      dfaparse(s, len, d);
      dfaanalyze(d, searchflag);
    }
  else
    {
        dfainit(d);
        dfaparse(s, len, d);
	dfamust(d);
        dfaanalyze(d, searchflag);
    }
}

/* Free the storage held by the components of a dfa. */
void
dfafree(d)
     struct dfa *d;
{
  int i;
  struct dfamust *dm, *ndm;

  free((ptr_t) d->charclasses);
  free((ptr_t) d->tokens);
  for (i = 0; _br(i < d->sindex,0x488aca8,8590,15); ++i)
    free((ptr_t) d->states[i].elems.elems);
  free((ptr_t) d->states);
  for (i = 0; _br(i < d->tindex,0x488b1f0,8593,15); ++i)
    if (_br(d->follows[i].elems,0x488b390,8594,9))
      free((ptr_t) d->follows[i].elems);
  free((ptr_t) d->follows);
  for (i = 0; _br(i < d->tralloc,0x488b880,8597,15); ++i)
    if (_br(d->trans[i],0x488bba8,8598,9))
      free((ptr_t) d->trans[i]);
    else if (_br(d->fails[i],0x488be70,8600,14))
      free((ptr_t) d->fails[i]);
  free((ptr_t) d->realtrans);
  free((ptr_t) d->fails);
  free((ptr_t) d->newlines);
  for (dm = d->musts; _br(dm,0x488c598,8605,23); dm = ndm)
    {
      ndm = dm->next;
      free(dm->must);
      free((ptr_t) dm);
    }
}

/* Having found the postfix representation of the regular expression,
   try to find a long sequence of characters that must appear in any line
   containing the r.e.
   Finding a "longest" sequence is beyond the scope here;
   we take an easy way out and hope for the best.
   (Take "(ab|a)b"--please.)

   We do a bottom-up calculation of sequences of characters that must appear
   in matches of r.e.'s represented by trees rooted at the nodes of the postfix
   representation:
	sequences that must appear at the left of the match ("left")
	sequences that must appear at the right of the match ("right")
	lists of sequences that must appear somewhere in the match ("in")
	sequences that must constitute the match ("is")

   When we get to the root of the tree, we use one of the longest of its
   calculated "in" sequences as our answer.  The sequence we find is returned in
   d->must (where "d" is the single argument passed to "dfamust");
   the length of the sequence is returned in d->mustn.

   The sequences calculated for the various types of node (in pseudo ANSI c)
   are shown below.  "p" is the operand of unary operators (and the left-hand
   operand of binary operators); "q" is the right-hand operand of binary
   operators.

   "ZERO" means "a zero-length sequence" below.

	Type	left		right		is		in
	----	----		-----		--		--
	char c	# c		# c		# c		# c
	
	CSET	ZERO		ZERO		ZERO		ZERO
	
	STAR	ZERO		ZERO		ZERO		ZERO

	QMARK	ZERO		ZERO		ZERO		ZERO

	PLUS	p->left		p->right	ZERO		p->in

	CAT	(p->is==ZERO)?	(q->is==ZERO)?	(p->is!=ZERO &&	p->in plus
		p->left :	q->right :	q->is!=ZERO) ?	q->in plus
		p->is##q->left	p->right##q->is	p->is##q->is :	p->right##q->left
						ZERO
					
	OR	longest common	longest common	(do p->is and	substrings common to
		leading		trailing	q->is have same	p->in and q->in
		(sub)sequence	(sub)sequence	length and	
		of p->left	of p->right	content) ?	
		and q->left	and q->right	p->is : NULL	

   If there's anything else we recognize in the tree, all four sequences get set
   to zero-length sequences.  If there's something we don't recognize in the tree,
   we just return a zero-length sequence.

   Break ties in favor of infrequent letters (choosing 'zzz' in preference to
   'aaa')?

   And. . .is it here or someplace that we might ponder "optimizations" such as
	egrep 'psi|epsilon'	->	egrep 'psi'
	egrep 'pepsi|epsilon'	->	egrep 'epsi'
					(Yes, we now find "epsi" as a "string
					that must occur", but we might also
					simplify the *entire* r.e. being sought)
	grep '[c]'		->	grep 'c'
	grep '(ab|a)b'		->	grep 'ab'
	grep 'ab*'		->	grep 'a'
	grep 'a*b'		->	grep 'b'

   There are several issues:

   Is optimization easy (enough)?

   Does optimization actually accomplish anything,
   or is the automaton you get from "psi|epsilon" (for example)
   the same as the one you get from "psi" (for example)?
  
   Are optimizable r.e.'s likely to be used in real-life situations
   (something like 'ab*' is probably unlikely; something like is
   'psi|epsilon' is likelier)? */

static char *
icatalloc(old, new)
     char *old;
     char *new;
{
  char *result;
  int oldsize, newsize;

  newsize = (new == NULL) ? 0 : strlen(new);
  if (old == NULL)
    oldsize = 0;
  else if (_br(newsize == 0,0x488d270,8704,12))
    return old;
  else	oldsize = strlen(old);
  if (old == NULL)
    result = (char *) malloc(newsize + 1);
  else
    result = (char *) realloc((void *) old, oldsize + newsize + 1);
  if (result != NULL && new != NULL)
    (void) strcpy(result + oldsize, new);
  return result;
}

static char *
icpyalloc(string)
     char *string;
{
  return icatalloc((char *) NULL, string);
}

static char *
istrstr(lookin, lookfor)
     char *lookin;
     char *lookfor;
{
  char *cp;
  int len;

  len = strlen(lookfor);
  for (cp = lookin; _br(*cp != '\0',0x488e778,8732,21); ++cp)
    if (_br(strncmp(cp, lookfor, len) == 0,0x488e9d8,8733,9))
      return cp;
  return NULL;
}

static void
ifree(cp)
     char *cp;
{
  if (cp != NULL)
    free(cp);
}

static void
freelist(cpp)
     char **cpp;
{
  int i;

  if (cpp == NULL)
    return;
  for (i = 0; cpp[i] != NULL; ++i)
    {
      free(cpp[i]);
      cpp[i] = NULL;
    }
}

static char **
enlist(cpp, new, len)
     char **cpp;
     char *new;
     int len;
{
  int i, j;

  if (cpp == NULL)
    return NULL;
  if ((new = icpyalloc(new)) == NULL)
    {
      freelist(cpp);
      return NULL;
    }
  new[len] = '\0';
  /* Is there already something in the list that's new (or longer)? */
  for (i = 0; cpp[i] != NULL; ++i)
    if (istrstr(cpp[i], new) != NULL)
      {
	free(new);
	return cpp;
      }
  /* Eliminate any obsoleted strings. */
  j = 0;
  while (cpp[j] != NULL)
    if (istrstr(new, cpp[j]) == NULL)
      ++j;
    else
      {
	free(cpp[j]);
	if (_br(--i == j,0x4891a18,8792,6))
	  break;
	cpp[j] = cpp[i];
	cpp[i] = NULL;
      }
  /* Add the new string. */
  cpp = (char **) realloc((char *) cpp, (i + 2) * sizeof *cpp);
  if (cpp == NULL)
    return NULL;
  cpp[i] = new;
  cpp[i + 1] = NULL;
  return cpp;
}

/* Given pointers to two strings, return a pointer to an allocated
   list of their distinct common substrings. Return NULL if something
   seems wild. */
static char **
comsubs(left, right)
     char *left;
     char *right;
{
  char **cpp;
  char *lcp;
  char *rcp;
  int i, len;

  if (left == NULL || right == NULL)
    return NULL;
  cpp = (char **) malloc(sizeof *cpp);
  if (cpp == NULL)
    return NULL;
  cpp[0] = NULL;
  for (lcp = left; _br(*lcp != '\0',0x4893468,8825,20); ++lcp)
    {
      len = 0;
      rcp = index(right, *lcp);
      while (rcp != NULL)
	{
	  for (i = 1; _br(lcp[i] != '\0' && lcp[i] == rcp[i],0x4893b88,8831,16); ++i)
	    ;
	  if (_br(i > len,0x4893cc0,8833,8))
	    len = i;
	  rcp = index(rcp + 1, *lcp);
	}
      if (_br(len == 0,0x4894070,8837,11))
	continue;
      if ((cpp = enlist(cpp, lcp, len)) == NULL)
	break;
    }
  return cpp;
}

static char **
addlists(old, new)
char **old;
char **new;
{
  int i;

  if (old == NULL || new == NULL)
    return NULL;
  for (i = 0; new[i] != NULL; ++i)
    {
      old = enlist(old, new[i], strlen(new[i]));
      if (old == NULL)
	break;
    }
  return old;
}

/* Given two lists of substrings, return a new list giving substrings
   common to both. */
static char **
inboth(left, right)
     char **left;
     char **right;
{
  char **both;
  char **temp;
  int lnum, rnum;

  if (left == NULL || right == NULL)
    return NULL;
  both = (char **) malloc(sizeof *both);
  if (both == NULL)
    return NULL;
  both[0] = NULL;
  for (lnum = 0; left[lnum] != NULL; ++lnum)
    {
      for (rnum = 0; right[rnum] != NULL; ++rnum)
	{
	  temp = comsubs(left[lnum], right[rnum]);
	  if (temp == NULL)
	    {
	      freelist(both);
	      return NULL;
	    }
	  both = addlists(both, temp);
	  freelist(temp);
	  if (both == NULL)
	    return NULL;
	}
    }
  return both;
}

typedef struct
{
  char **in;
  char *left;
  char *right;
  char *is;
} must;

static void
resetmust(mp)
must *mp;
{
  mp->left[0] = mp->right[0] = mp->is[0] = '\0';
  freelist(mp->in);
}

static void
dfamust(dfa)
struct dfa *dfa;
{
  must *musts;
  must *mp;
  char *result;
  int ri;
  int i;
  int exact;
  token t;
  static must must0;
  struct dfamust *dm;

  result = "";
  exact = 0;
  musts = (must *) malloc((dfa->tindex + 1) * sizeof *musts);
  if (musts == NULL)
    return;
  mp = musts;
  for (i = 0; _br(i <= dfa->tindex,0x4898868,8935,15); ++i)
    mp[i] = must0;
  for (i = 0; _br(i <= dfa->tindex,0x489abb0,8937,15); ++i)
    {
      mp[i].in = (char **) malloc(sizeof *mp[i].in);
      mp[i].left = malloc(2);
      mp[i].right = malloc(2);
      mp[i].is = malloc(2);
      if (mp[i].in == NULL || mp[i].left == NULL ||
	  mp[i].right == NULL || mp[i].is == NULL)
	goto done;
      mp[i].left[0] = mp[i].right[0] = mp[i].is[0] = '\0';
      mp[i].in[0] = NULL;
    }
#ifdef DEBUG
  fprintf(stderr, "dfamust:\n");
  for (i = 0; i < dfa->tindex; ++i)
    {
      fprintf(stderr, " %d:", i);
      prtok(dfa->tokens[i]);
    }
  putc('\n', stderr);
#endif
  for (ri = 0; _br(ri < dfa->tindex,0x489c488,8958,16); ++ri)
    {
      switch (t = dfa->tokens[ri])
	{
	case LPAREN:
	case RPAREN:
	  goto done;		/* "cannot happen" */
	case EMPTY:
	case BEGLINE:
	case ENDLINE:
	case BEGWORD:
	case ENDWORD:
	case LIMWORD:
	case NOTLIMWORD:
	case BACKREF:
	  resetmust(mp);
	  break;
	case STAR:
	case QMARK:
	  if (_br(mp <= musts,0x489cc70,8977,8))
	    goto done;		/* "cannot happen" */
	  --mp;
	  resetmust(mp);
	  break;
	case OR:
	case ORTOP:
	  if (_br(mp < &musts[2],0x489cf90,8984,8))
	    goto done;		/* "cannot happen" */
	  {
	    char **new;
	    must *lmp;
	    must *rmp;
	    int j, ln, rn, n;

	    rmp = --mp;
	    lmp = --mp;
	    /* Guaranteed to be.  Unlikely, but. . . */
	    if (_br(strcmp(lmp->is, rmp->is) != 0,0x489d710,8995,10))
	      lmp->is[0] = '\0';
	    /* Left side--easy */
	    i = 0;
	    while (_br(lmp->left[i] != '\0' && lmp->left[i] == rmp->left[i],0x489dcf0,8999,13))
	      ++i;
	    lmp->left[i] = '\0';
	    /* Right side */
	    ln = strlen(lmp->right);
	    rn = strlen(rmp->right);
	    n = ln;
	    if (_br(n > rn,0x489e310,9006,10))
	      n = rn;
	    for (i = 0; _br(i < n,0x489e4f0,9008,18); ++i)
	      if (_br(lmp->right[ln - i - 1] != rmp->right[rn - i - 1],0x489e910,9009,12))
		break;
	    for (j = 0; _br(j < i,0x489eaa0,9011,18); ++j)
	      lmp->right[j] = lmp->right[(ln - i) + j];
	    lmp->right[j] = '\0';
	    new = inboth(lmp->in, rmp->in);
	    if (new == NULL)
	      goto done;
	    freelist(lmp->in);
	    free((char *) lmp->in);
	    lmp->in = new;
	  }
	  break;
	case PLUS:
	  if (_br(mp <= musts,0x489f800,9023,8))
	    goto done;		/* "cannot happen" */
	  --mp;
	  mp->is[0] = '\0';
	  break;
	case END:
	  if (_br(mp != &musts[1],0x489fb40,9029,8))
	    goto done;		/* "cannot happen" */
	  for (i = 0; musts[0].in[i] != NULL; ++i)
	    if (_br(strlen(musts[0].in[i]) > strlen(result),0x48a0160,9032,10))
	      result = musts[0].in[i];
	  if (_br(strcmp(result, musts[0].is) == 0,0x48a05b0,9034,8))
	    exact = 1;
	  goto done;
	case CAT:
	  if (_br(mp < &musts[2],0x48a07e0,9038,8))
	    goto done;		/* "cannot happen" */
	  {
	    must *lmp;
	    must *rmp;

	    rmp = --mp;
	    lmp = --mp;
	    /* In.  Everything in left, plus everything in
	       right, plus catenation of
	       left's right and right's left. */
	    lmp->in = addlists(lmp->in, rmp->in);
	    if (lmp->in == NULL)
	      goto done;
	    if (_br(lmp->right[0] != '\0' &&
		rmp->left[0] != '\0',0x48a1100,9052,10))
	      {
		char *tp;

		tp = icpyalloc(lmp->right);
		if (tp == NULL)
		  goto done;
		tp = icatalloc(tp, rmp->left);
		if (tp == NULL)
		  goto done;
		lmp->in = enlist(lmp->in, tp,
				 strlen(tp));
		free(tp);
		if (lmp->in == NULL)
		  goto done;
	      }
	    /* Left-hand */
	    if (_br(lmp->is[0] != '\0',0x48a1e40,9070,10))
	      {
		lmp->left = icatalloc(lmp->left,
				      rmp->left);
		if (lmp->left == NULL)
		  goto done;
	      }
	    /* Right-hand */
	    if (_br(rmp->is[0] == '\0',0x48a23c0,9078,10))
	      lmp->right[0] = '\0';
	    lmp->right = icatalloc(lmp->right, rmp->right);
	    if (lmp->right == NULL)
	      goto done;
	    /* Guaranteed to be */
	    if (_br(lmp->is[0] != '\0' && rmp->is[0] != '\0',0x48a2bd0,9084,10))
	      {
		lmp->is = icatalloc(lmp->is, rmp->is);
		if (lmp->is == NULL)
		  goto done;
	      }
	    else
	      lmp->is[0] = '\0';
	  }
	  break;
	default:
	  if (_br(t < END,0x48a3260,9095,8))
	    {
	      /* "cannot happen" */
	      goto done;
	    }
	  else if (_br(t == '\0',0x48a3330,9100,13))
	    {
	      /* not on *my* shift */
	      goto done;
	    }
	  else if (_br(t >= CSET,0x48a3410,9105,13))
	    {
	      /* easy enough */
	      resetmust(mp);
	    }
	  else
	    {
	      /* plain character */
	      resetmust(mp);
	      mp->is[0] = mp->left[0] = mp->right[0] = t;
	      mp->is[1] = mp->left[1] = mp->right[1] = '\0';
	      mp->in = enlist(mp->in, mp->is, 1);
	      if (mp->in == NULL)
		goto done;
	    }
	  break;
	}
#ifdef DEBUG
      fprintf(stderr, " node: %d:", ri);
      prtok(dfa->tokens[ri]);
      fprintf(stderr, "\n  in:");
      for (i = 0; mp->in[i]; ++i)
	fprintf(stderr, " \"%s\"", mp->in[i]);
      fprintf(stderr, "\n  is: \"%s\"\n", mp->is);
      fprintf(stderr, "  left: \"%s\"\n", mp->left);
      fprintf(stderr, "  right: \"%s\"\n", mp->right);
#endif
      ++mp;
    }
 done:
  if (_br(strlen(result),0x48a4348,9135,7))
    {
      dm = (struct dfamust *) malloc(sizeof (struct dfamust));
      dm->exact = exact;
      dm->must = malloc(strlen(result) + 1);
      strcpy(dm->must, result);
      dm->next = dfa->musts;
      dfa->musts = dm;
    }
  mp = musts;
  for (i = 0; _br(i <= dfa->tindex,0x48a4e48,9145,15); ++i)
    {
      freelist(mp[i].in);
      ifree((char *) mp[i].in);
      ifree(mp[i].left);
      ifree(mp[i].right);
      ifree(mp[i].is);
    }
  free((char *) mp);
}
/* kwset.c - search for any of a set of keywords.
   Copyright 1989 Free Software Foundation
		  Written August 1989 by Mike Haertel.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 1, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

   The author may be reached (Email) at the address mike@ai.mit.edu,
   or (US mail) as Mike Haertel c/o Free Software Foundation. */

/* The algorithm implemented by these routines bears a startling resemblence
   to one discovered by Beate Commentz-Walter, although it is not identical.
   See "A String Matching Algorithm Fast on the Average," Technical Report,
   IBM-Germany, Scientific Center Heidelberg, Tiergartenstrasse 15, D-6900
   Heidelberg, Germany.  See also Aho, A.V., and M. Corasick, "Efficient
   String Matching:  An Aid to Bibliographic Search," CACM June 1975,
   Vol. 18, No. 6, which describes the failure function used below. */


#ifdef STDC_HEADERS
#undef RE_DUP_MAX
#include <limits.h>
#define flag_limits 1
#if flag_stdlib==0 
#include <stdlib.h>
#define flag_stdlib 1
#endif
#else
#define INT_MAX 2147483647
#define UCHAR_MAX 255
#ifdef __STDC__
#include <stddef.h>
#else
#if flag_systypes==0
#include <sys/types.h>
#define flag_systypes 1
#endif
#endif
extern char *malloc();
extern void free();
#endif

#ifdef HAVE_MEMCHR
#if flag_string==0
#include <string.h>
#define flag_string 1
#endif
#ifdef NEED_MEMORY_H
#if flag_memory==0
#include <memory.h>
#define flag_memory 1
#endif
#endif
#else
#ifdef __STDC__
extern void *memchr();
#else
extern char *memchr();
#endif
#endif

#ifdef GREP
extern char *xmalloc();
#define malloc xmalloc
#endif

#include "kwset.h"
#include "obstack.h"

#define NCHAR (UCHAR_MAX + 1)
#define obstack_chunk_alloc malloc
#define obstack_chunk_free free

/* Balanced tree of edges and labels leaving a given trie node. */
struct tree
{
  struct tree *llink;		/* Left link; MUST be first field. */
  struct tree *rlink;		/* Right link (to larger labels). */
  struct trie *trie;		/* Trie node pointed to by this edge. */
  unsigned char label;		/* Label on this edge. */
  char balance;			/* Difference in depths of subtrees. */
};

/* Node of a trie representing a set of reversed keywords. */
struct trie
{
  unsigned int accepting;	/* Word index of accepted word, or zero. */
  struct tree *links;		/* Tree of edges leaving this node. */
  struct trie *parent;		/* Parent of this node. */
  struct trie *next;		/* List of all trie nodes in level order. */
  struct trie *fail;		/* Aho-Corasick failure function. */
  int depth;			/* Depth of this node from the root. */
  int shift;			/* Shift function for search failures. */
  int maxshift;			/* Max shift of self and descendents. */
};

/* Structure returned opaquely to the caller, containing everything. */
struct kwset
{
  struct obstack obstack;	/* Obstack for node allocation. */
  int words;			/* Number of words in the trie. */
  struct trie *trie;		/* The trie itself. */
  int mind;			/* Minimum depth of an accepting node. */
  int maxd;			/* Maximum depth of any node. */
  unsigned char delta[NCHAR];	/* Delta table for rapid search. */
  struct trie *next[NCHAR];	/* Table of children of the root. */
  char *target;			/* Target string if there's only one. */
  int mind2;			/* Used in Boyer-Moore search for one string. */
  char *trans;			/* Character translation table. */
};

/* Allocate and initialize a keyword set object, returning an opaque
   pointer to it.  Return NULL if memory is not available. */
kwset_t
kwsalloc(trans)
     char *trans;
{
  struct kwset *kwset;

  kwset = (struct kwset *) malloc(sizeof (struct kwset));
  if (_br(!kwset,0x48c3be8,9286,7))
    return 0;

  obstack_init(&kwset->obstack);
  kwset->words = 0;
  kwset->trie
    = (struct trie *) obstack_alloc(&kwset->obstack, sizeof (struct trie));
  if (_br(!kwset->trie,0x48c5cb0,9293,7))
    {
      kwsfree((kwset_t) kwset);
      return 0;
    }
  kwset->trie->accepting = 0;
  kwset->trie->links = 0;
  kwset->trie->parent = 0;
  kwset->trie->next = 0;
  kwset->trie->fail = 0;
  kwset->trie->depth = 0;
  kwset->trie->shift = 0;
  kwset->mind = INT_MAX;
  kwset->maxd = -1;
  kwset->target = 0;
  kwset->trans = trans;

  return (kwset_t) kwset;
}

/* Add the given string to the contents of the keyword set.  Return NULL
   for success, an error message otherwise. */
char *
kwsincr(kws, text, len)
     kwset_t kws;
     char *text;
     size_t len;
{
  struct kwset *kwset;
  register struct trie *trie;
  register unsigned char label;
  register struct tree *link;
  register int depth;
  struct tree *links[12];
  enum { L, R } dirs[12];
  struct tree *t, *r, *l, *rl, *lr;

  kwset = (struct kwset *) kws;
  trie = kwset->trie;
  text += len;

  /* Descend the trie (built of reversed keywords) character-by-character,
     installing new nodes when necessary. */
  while (_br(len--,0x48d2e50,9336,10))
    {
      label = kwset->trans ? kwset->trans[(unsigned char) *--text] : *--text;

      /* Descend the tree of outgoing links for this trie node,
	 looking for the current character and keeping track
	 of the path followed. */
      link = trie->links;
      links[0] = (struct tree *) &trie->links;
      dirs[0] = L;
      depth = 1;

      while (_br(link && label != link->label,0x48d37c8,9348,14))
	{
	  links[depth] = link;
	  if (_br(label < link->label,0x48d3a00,9351,8))
	    dirs[depth++] = L, link = link->llink;
	  else
	    dirs[depth++] = R, link = link->rlink;
	}

      /* The current character doesn't have an outgoing link at
	 this trie node, so build a new trie node and install
	 a link in the current trie node's tree. */
      if (_br(!link,0x48d3f28,9360,11))
	{
	  link = (struct tree *) obstack_alloc(&kwset->obstack,
					       sizeof (struct tree));
	  if (_br(!link,0x48d6338,9364,8))
	    return "memory exhausted";
	  link->llink = 0;
	  link->rlink = 0;
	  link->trie = (struct trie *) obstack_alloc(&kwset->obstack,
						     sizeof (struct trie));
	  if (_br(!link->trie,0x48d8108,9370,8))
	    return "memory exhausted";
	  link->trie->accepting = 0;
	  link->trie->links = 0;
	  link->trie->parent = trie;
	  link->trie->next = 0;
	  link->trie->fail = 0;
	  link->trie->depth = trie->depth + 1;
	  link->trie->shift = 0;
	  link->label = label;
	  link->balance = 0;

	  /* Install the new tree node in its parent. */
	  if (_br(dirs[--depth] == L,0x48d8d48,9383,8))
	    links[depth]->llink = link;
	  else
	    links[depth]->rlink = link;

	  /* Back up the tree fixing the balance flags. */
	  while (_br(depth && !links[depth]->balance,0x48d91d8,9389,11))
	    {
	      if (_br(dirs[depth] == L,0x48d9318,9391,12))
		--links[depth]->balance;
	      else
		++links[depth]->balance;
	      --depth;
	    }

	  /* Rebalance the tree by pointer rotations if necessary. */
	  if (_br(depth && ((dirs[depth] == L && --links[depth]->balance)
			|| (dirs[depth] == R && ++links[depth]->balance)),0x48d9c30,9399,8))
	    {
	      switch (links[depth]->balance)
		{
		case (char) -2:
		  switch (dirs[depth + 1])
		    {
		    case L:
		      r = links[depth], t = r->llink, rl = t->rlink;
		      t->rlink = r, r->llink = rl;
		      t->balance = r->balance = 0;
		      break;
		    case R:
		      r = links[depth], l = r->llink, t = l->rlink;
		      rl = t->rlink, lr = t->llink;
		      t->llink = l, l->rlink = lr, t->rlink = r, r->llink = rl;
		      l->balance = t->balance != 1 ? 0 : -1;
		      r->balance = t->balance != (char) -1 ? 0 : 1;
		      t->balance = 0;
		      break;
		    }
		  break;
		case 2:
		  switch (dirs[depth + 1])
		    {
		    case R:
		      l = links[depth], t = l->rlink, lr = t->llink;
		      t->llink = l, l->rlink = lr;
		      t->balance = l->balance = 0;
		      break;
		    case L:
		      l = links[depth], r = l->rlink, t = r->llink;
		      lr = t->llink, rl = t->rlink;
		      t->llink = l, l->rlink = lr, t->rlink = r, r->llink = rl;
		      l->balance = t->balance != 1 ? 0 : -1;
		      r->balance = t->balance != (char) -1 ? 0 : 1;
		      t->balance = 0;
		      break;
		    }
		  break;
		}

	      if (_br(dirs[depth - 1] == L,0x48dd038,9442,12))
		links[depth - 1]->llink = t;
	      else
		links[depth - 1]->rlink = t;
	    }
	}

      trie = link->trie;
    }

  /* Mark the node we finally reached as accepting, encoding the
     index number of this word in the keyword set so far. */
  if (_br(!trie->accepting,0x48dd710,9454,7))
    trie->accepting = 1 + 2 * kwset->words;
  ++kwset->words;

  /* Keep track of the longest and shortest string of the keyword set. */
  if (_br(trie->depth < kwset->mind,0x48ddaf8,9459,7))
    kwset->mind = trie->depth;
  if (_br(trie->depth > kwset->maxd,0x48ddda8,9461,7))
    kwset->maxd = trie->depth;

  return 0;
}

/* Enqueue the trie nodes referenced from the given tree in the
   given queue. */
static void
enqueue(tree, last)
     struct tree *tree;
     struct trie **last;
{
  if (_br(!tree,0x48de3b8,9474,7))
    return;
  enqueue(tree->llink, last);
  enqueue(tree->rlink, last);
  (*last) = (*last)->next = tree->trie;
}

/* Compute the Aho-Corasick failure function for the trie nodes referenced
   from the given tree, given the failure function for their parent as
   well as a last resort failure node. */
static void
treefails(tree, fail, recourse)
     register struct tree *tree;
     struct trie *fail;
     struct trie *recourse;
{
  register struct tree *link;

  if (_br(!tree,0x48ded20,9492,7))
    return;

  treefails(tree->llink, fail, recourse);
  treefails(tree->rlink, fail, recourse);

  /* Find, in the chain of fails going back to the root, the first
     node that has a descendent on the current label. */
  while (_br(fail,0x48df0d8,9500,10))
    {
      link = fail->links;
      while (_br(link && tree->label != link->label,0x48df388,9503,14))
	if (_br(tree->label < link->label,0x48df500,9504,6))
	  link = link->llink;
	else
	  link = link->rlink;
      if (_br(link,0x48df768,9508,11))
	{
	  tree->trie->fail = link->trie;
	  return;
	}
      fail = fail->fail;
    }

  tree->trie->fail = recourse;
}

/* Set delta entries for the links of the given tree such that
   the preexisting delta value is larger than the current depth. */
static void
treedelta(tree, depth, delta)
     register struct tree *tree;
     register unsigned int depth;
     unsigned char delta[];
{
  if (_br(!tree,0x48dffd0,9527,7))
    return;
  treedelta(tree->llink, depth, delta);
  treedelta(tree->rlink, depth, delta);
  if (_br(depth < delta[tree->label],0x48e04c8,9531,7))
    delta[tree->label] = depth;
}

/* Return true if A has every label in B. */
static int
hasevery(a, b)
     register struct tree *a;
     register struct tree *b;
{
  if (_br(!b,0x48e0990,9541,7))
    return 1;
  if (_br(!hasevery(a, b->llink),0x48e0b68,9543,7))
    return 0;
  if (_br(!hasevery(a, b->rlink),0x48e0d40,9545,7))
    return 0;
  while (_br(a && b->label != a->label,0x48e0f88,9547,10))
    if (_br(b->label < a->label,0x48e1100,9548,9))
      a = a->llink;
    else
      a = a->rlink;
  return !!a;
}

/* Compute a vector, indexed by character code, of the trie nodes
   referenced from the given tree. */
static void
treenext(tree, next)
     struct tree *tree;
     struct trie *next[];
{
  if (_br(!tree,0x48e1718,9562,7))
    return;
  treenext(tree->llink, next);
  treenext(tree->rlink, next);
  next[tree->label] = tree->trie;
}

/* Compute the shift for each trie node, as well as the delta
   table and next cache for the given keyword set. */
char *
kwsprep(kws)
     kwset_t kws;
{
  register struct kwset *kwset;
  register int i;
  register struct trie *curr, *fail;
  register char *trans;
  unsigned char delta[NCHAR];
  struct trie *last, *next[NCHAR];

  kwset = (struct kwset *) kws;

  /* Initial values for the delta table; will be changed later.  The
     delta entry for a given character is the smallest depth of any
     node at which an outgoing edge is labeled by that character. */
  if (_br(kwset->mind < 256,0x48e3aa8,9587,7))
    for (i = 0; i < NCHAR; ++i)
      delta[i] = kwset->mind;
  else
    for (i = 0; i < NCHAR; ++i)
      delta[i] = 255;

  /* Check if we can use the simple boyer-moore algorithm, instead
     of the hairy commentz-walter algorithm. */
  if (_br(kwset->words == 1 && kwset->trans == 0,0x48e4470,9596,7))
    {
      /* Looking for just one string.  Extract it from the trie. */
      kwset->target = obstack_alloc(&kwset->obstack, kwset->mind);
      for (i = kwset->mind - 1, curr = kwset->trie; _br(i >= 0,0x48e61f8,9600,53); --i)
	{
	  kwset->target[i] = curr->links->label;
	  curr = curr->links->trie;
	}
      /* Build the Boyer Moore delta.  Boy that's easy compared to CW. */
      for (i = 0; _br(i < kwset->mind,0x48e6750,9606,19); ++i)
	delta[(unsigned char) kwset->target[i]] = kwset->mind - (i + 1);
      kwset->mind2 = kwset->mind;
      /* Find the minimal delta2 shift that we might make after
	 a backwards match has failed. */
      for (i = 0; _br(i < kwset->mind - 1,0x48e6e08,9611,19); ++i)
	if (_br(kwset->target[i] == kwset->target[kwset->mind - 1],0x48e7160,9612,6))
	  kwset->mind2 = kwset->mind - (i + 1);
    }
  else
    {
      /* Traverse the nodes of the trie in level order, simultaneously
	 computing the delta table, failure function, and shift function. */
      for (curr = last = kwset->trie; _br(curr,0x48e75b8,9619,39); curr = curr->next)
	{
	  /* Enqueue the immediate descendents in the level order queue. */
	  enqueue(curr->links, &last);

	  curr->shift = kwset->mind;
	  curr->maxshift = kwset->mind;

	  /* Update the delta table for the descendents of this node. */
	  treedelta(curr->links, curr->depth, delta);

	  /* Compute the failure function for the decendents of this node. */
	  treefails(curr->links, curr->fail, kwset->trie);

	  /* Update the shifts at each node in the current node's chain
	     of fails back to the root. */
	  for (fail = curr->fail; _br(fail,0x48e7f90,9635,28); fail = fail->fail)
	    {
	      /* If the current node has some outgoing edge that the fail
		 doesn't, then the shift at the fail should be no larger
		 than the difference of their depths. */
	      if (_br(!hasevery(fail->links, curr->links),0x48e8220,9640,12))
		if (_br(curr->depth - fail->depth < fail->shift,0x48e8418,9641,7))
		  fail->shift = curr->depth - fail->depth;

	      /* If the current node is accepting then the shift at the
		 fail and its descendents should be no larger than the
		 difference of their depths. */
	      if (_br(curr->accepting && fail->maxshift > curr->depth - fail->depth,0x48e8928,9647,12))
		fail->maxshift = curr->depth - fail->depth;
	    }
	}

      /* Traverse the trie in level order again, fixing up all nodes whose
	 shift exceeds their inherited maxshift. */
      for (curr = kwset->trie->next; _br(curr,0x48e8da8,9654,38); curr = curr->next)
	{
	  if (_br(curr->maxshift > curr->parent->maxshift,0x48e9010,9656,8))
	    curr->maxshift = curr->parent->maxshift;
	  if (_br(curr->shift > curr->maxshift,0x48e9310,9658,8))
	    curr->shift = curr->maxshift;
	}

      /* Create a vector, indexed by character code, of the outgoing links
	 from the root node. */
      for (i = 0; i < NCHAR; ++i)
	next[i] = 0;
      treenext(kwset->trie->links, next);

      if (_br((trans = kwset->trans) != 0,0x48e9b68,9668,11))
	for (i = 0; i < NCHAR; ++i)
	  kwset->next[i] = next[(unsigned char) trans[i]];
      else
	for (i = 0; i < NCHAR; ++i)
	  kwset->next[i] = next[i];
    }

  /* Fix things up for any translation table. */
  if (_br((trans = kwset->trans) != 0,0x48ea718,9677,7))
    for (i = 0; i < NCHAR; ++i)
      kwset->delta[i] = delta[(unsigned char) trans[i]];
  else
    for (i = 0; i < NCHAR; ++i)
      kwset->delta[i] = delta[i];

  return 0;
}

#define U(C) ((unsigned char) (C))

/* Fast boyer-moore search. */
static char *
bmexec(kws, text, size)
     kwset_t kws;
     char *text;
     size_t size;
{
  struct kwset *kwset;
  register unsigned char *d1;
  register char *ep, *sp, *tp;
  register int d, gc, i, len, md2;

  kwset = (struct kwset *) kws;
  len = kwset->mind;

  if (_br(len == 0,0x48ecb58,9704,7))
    return text;
  if (_br(len > size,0x48ecca8,9706,7))
    return 0;
  if (_br(len == 1,0x48ecdb8,9708,7))
    return memchr(text, kwset->target[0], size);

  d1 = kwset->delta;
  sp = kwset->target + len;
  gc = U(sp[-2]);
  md2 = kwset->mind2;
  tp = text + len;

  /* Significance of 12: 1 (initial offset) + 10 (skip loop) + 1 (md2). */
  if (_br(size > 12 * len,0x48ed768,9718,7))
    /* 11 is not a bug, the initial offset happens only once. */
    for (ep = text + size - 11 * len;;)
      {
	while (_br(tp <= ep,0x48ed9b8,9722,9))
	  {
	    d = d1[U(tp[-1])], tp += d;
	    d = d1[U(tp[-1])], tp += d;
	    if (_br(d == 0,0x48ee080,9726,10))
	      goto found;
	    d = d1[U(tp[-1])], tp += d;
	    d = d1[U(tp[-1])], tp += d;
	    d = d1[U(tp[-1])], tp += d;
	    if (_br(d == 0,0x48eeb00,9731,10))
	      goto found;
	    d = d1[U(tp[-1])], tp += d;
	    d = d1[U(tp[-1])], tp += d;
	    d = d1[U(tp[-1])], tp += d;
	    if (_br(d == 0,0x48ef538,9736,10))
	      goto found;
	    d = d1[U(tp[-1])], tp += d;
	    d = d1[U(tp[-1])], tp += d;
	  }
	break;
      found:
	if (U(tp[-2]) == gc)
	  {
	    for (i = 3; i <= len && U(tp[-i]) == U(sp[-i]); ++i)
	      ;
	    if (_br(i > len,0x48f0460,9747,10))
	      return tp - len;
	  }
	tp += md2;
      }

  /* Now we have only a few characters left to search.  We
     carefully avoid ever producing an out-of-bounds pointer. */
  ep = text + size;
  d = d1[U(tp[-1])];
  while (_br(d <= ep - tp,0x48f0b90,9757,10))
    {
      d = d1[U((tp += d)[-1])];
      if (_br(d != 0,0x48f0ef0,9760,11))
	continue;
      if (_br(tp[-2] == gc,0x48f1070,9762,11))
	{
	  for (i = 3; i <= len && U(tp[-i]) == U(sp[-i]); ++i)
	    ;
	  if (_br(i > len,0x48f1660,9766,8))
	    return tp - len;
	}
      d = md2;
    }

  return 0;
}

/* Hairy multiple string search. */
static char *
cwexec(kws, text, len, kwsmatch)
     kwset_t kws;
     char *text;
     size_t len;
     struct kwsmatch *kwsmatch;
{
  struct kwset *kwset;
  struct trie **next, *trie, *accept;
  char *beg, *lim, *mch, *lmch;
  register unsigned char c, *delta;
  register int d;
  register char *end, *qlim;
  register struct tree *tree;
  register char *trans;

  /* Initialize register copies and look for easy ways out. */
  kwset = (struct kwset *) kws;
  if (_br(len < kwset->mind,0x48f2700,9794,7))
    return 0;
  next = kwset->next;
  delta = kwset->delta;
  trans = kwset->trans;
  lim = text + len;
  end = text;
  if (_br((d = kwset->mind) != 0,0x48f2cf8,9801,7))
    mch = 0;
  else
    {
      mch = text, accept = kwset->trie;
      goto match;
    }

  if (_br(len >= 4 * kwset->mind,0x48f3130,9809,7))
    qlim = lim - 4 * kwset->mind;
  else
    qlim = 0;

  while (_br(lim - end >= d,0x48f34a8,9814,10))
    {
      if (_br(qlim && end <= qlim,0x48f35b8,9816,11))
	{
	  end += d - 1;
	  while (_br((d = delta[c = *end]) && end < qlim,0x48f3958,9819,11))
	    {
	      end += d;
	      end += delta[(unsigned char) *end];
	      end += delta[(unsigned char) *end];
	    }
	  ++end;
	}
      else
	d = delta[c = (end += d)[-1]];
      if (_br(d,0x48f4138,9829,11))
	continue;
      beg = end - 1;
      trie = next[c];
      if (_br(trie->accepting,0x48f43f0,9833,11))
	{
	  mch = beg;
	  accept = trie;
	}
      d = trie->shift;
      while (_br(beg > text,0x48f5290,9839,14))
	{
	  c = trans ? trans[(unsigned char) *--beg] : *--beg;
	  tree = trie->links;
	  while (_br(tree && c != tree->label,0x48f57c8,9843,11))
	    if (_br(c < tree->label,0x48f58f0,9844,10))
	      tree = tree->llink;
	    else
	      tree = tree->rlink;
	  if (_br(tree,0x48f5b58,9848,8))
	    {
	      trie = tree->trie;
	      if (_br(trie->accepting,0x48f5cc8,9851,12))
		{
		  mch = beg;
		  accept = trie;
		}
	    }
	  else
	    break;
	  d = trie->shift;
	}
      if (_br(mch,0x48f6030,9861,11))
	goto match;
    }
  return 0;

 match:
  /* Given a known match, find the longest possible match anchored
     at or before its starting point.  This is nearly a verbatim
     copy of the preceding main search loops. */
  if (_br(lim - mch > kwset->maxd,0x48f62b0,9870,7))
    lim = mch + kwset->maxd;
  lmch = 0;
  d = 1;
  while (_br(lim - end >= d,0x48f6668,9874,10))
    {
      if (_br((d = delta[c = (end += d)[-1]]) != 0,0x48f6960,9876,11))
	continue;
      beg = end - 1;
      if (_br(!(trie = next[c]),0x48f6bd0,9879,11))
	{
	  d = 1;
	  continue;
	}
      if (_br(trie->accepting && beg <= mch,0x48f6e00,9884,11))
	{
	  lmch = beg;
	  accept = trie;
	}
      d = trie->shift;
      while (_br(beg > text,0x48f7108,9890,14))
	{
	  c = trans ? trans[(unsigned char) *--beg] : *--beg;
	  tree = trie->links;
	  while (_br(tree && c != tree->label,0x48f7640,9894,11))
	    if (_br(c < tree->label,0x48f7768,9895,10))
	      tree = tree->llink;
	    else
	      tree = tree->rlink;
	  if (_br(tree,0x48f79d0,9899,8))
	    {
	      trie = tree->trie;
	      if (_br(trie->accepting && beg <= mch,0x48f7c00,9902,12))
		{
		  lmch = beg;
		  accept = trie;
		}
	    }
	  else
	    break;
	  d = trie->shift;
	}
      if (_br(lmch,0x48f7f78,9912,11))
	{
	  mch = lmch;
	  goto match;
	}
      if (_br(!d,0x48f80d8,9917,11))
	d = 1;
    }

  if (_br(kwsmatch,0x48f8240,9921,7))
    {
      kwsmatch->index = accept->accepting / 2;
      kwsmatch->beg[0] = mch;
      kwsmatch->size[0] = accept->depth;
    }
  return mch;
}
  
/* Search through the given text for a match of any member of the
   given keyword set.  Return a pointer to the first character of
   the matching substring, or NULL if no match is found.  If FOUNDLEN
   is non-NULL store in the referenced location the length of the
   matching substring.  Similarly, if FOUNDIDX is non-NULL, store
   in the referenced location the index number of the particular
   keyword matched. */
char *
kwsexec(kws, text, size, kwsmatch)
     kwset_t kws;
     char *text;
     size_t size;
     struct kwsmatch *kwsmatch;
{
  struct kwset *kwset;
  char *ret;

  kwset = (struct kwset *) kws;
  if (_br(kwset->words == 1 && kwset->trans == 0,0x48f8f40,9948,7))
    {
      ret = bmexec(kws, text, size);
      if (_br(kwsmatch != 0 && ret != 0,0x48fbdb8,9951,11))
	{
	  kwsmatch->index = 0;
	  kwsmatch->beg[0] = ret;
	  kwsmatch->size[0] = kwset->mind;
	}
      return ret;
    }
  else
    return cwexec(kws, text, size, kwsmatch);
}

/* Free the components of the given keyword set. */
void
kwsfree(kws)
     kwset_t kws;
{
  struct kwset *kwset;

  kwset = (struct kwset *) kws;
  obstack_free(&kwset->obstack, 0);
  free(kws);
}
/* obstack.c - subroutines used implicitly by object stack macros
   Copyright (C) 1988, 1993 Free Software Foundation, Inc.

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */

/*#include "obstack.h"*/

/* This is just to get __GNU_LIBRARY__ defined.  */
/*#include <stdio.h>*/

/* Comment out all this code if we are using the GNU C Library, and are not
   actually compiling the library itself.  This code is part of the GNU C
   Library, but also included in many other GNU distributions.  Compiling
   and linking in this code is a waste when using the GNU C library
   (especially if it is a shared library).  Rather than having every GNU
   program understand `configure --with-gnu-libc' and omit the object files,
   it is simpler to just do this in the source for each such file.  */

#if defined (_LIBC) || !defined (__GNU_LIBRARY__)


#ifdef __STDC__
#define POINTER void *
#else
#define POINTER char *
#endif

/* Determine default alignment.  */
struct fooalign {char x; double d;};
#define DEFAULT_ALIGNMENT  \
  ((PTR_INT_TYPE) ((char *)&((struct fooalign *) 0)->d - (char *)0))
/* If malloc were really smart, it would round addresses to DEFAULT_ALIGNMENT.
   But in fact it might be less smart and round addresses to as much as
   DEFAULT_ROUNDING.  So we prepare for it to do that.  */
union fooround {long x; double d;};
#define DEFAULT_ROUNDING (sizeof (union fooround))

/* When we copy a long block of data, this is the unit to do it with.
   On some machines, copying successive ints does not work;
   in such a case, redefine COPYING_UNIT to `long' (if that works)
   or `char' as a last resort.  */
#ifndef COPYING_UNIT
#define COPYING_UNIT int
#endif

/* The non-GNU-C macros copy the obstack into this global variable
   to avoid multiple evaluation.  */

struct obstack *_obstack;

/* Define a macro that either calls functions with the traditional malloc/free
   calling interface, or calls functions with the mmalloc/mfree interface
   (that adds an extra first argument), based on the state of use_extra_arg.
   For free, do not use ?:, since some compilers, like the MIPS compilers,
   do not allow (expr) ? void : void.  */

#define CALL_CHUNKFUN(h, size) \
  (((h) -> use_extra_arg) \
   ? (*(h)->chunkfun) ((h)->extra_arg, (size)) \
   : (*(h)->chunkfun) ((size)))

#define CALL_FREEFUN(h, old_chunk) \
  do { \
    if ((h) -> use_extra_arg) \
      (*(h)->freefun) ((h)->extra_arg, (old_chunk)); \
    else \
      (*(h)->freefun) ((old_chunk)); \
  } while (0)

#define CALL_FREEFUN(h, old_chunk) \
  do { \
      free ((old_chunk)); \
  } while (0)


/* Initialize an obstack H for use.  Specify chunk size SIZE (0 means default).
   Objects start on multiples of ALIGNMENT (0 means use default).
   CHUNKFUN is the function to use to allocate chunks,
   and FREEFUN the function to free them.  */

void
_obstack_begin (h, size, alignment, chunkfun, freefun)
     struct obstack *h;
     int size;
     int alignment;
     POINTER (*chunkfun) ();
     void (*freefun) ();
{
  register struct _obstack_chunk* chunk; /* points to new chunk */

  if (alignment == 0)
    alignment = DEFAULT_ALIGNMENT;
  if (size == 0)
    /* Default size is what GNU malloc can fit in a 4096-byte block.  */
    {
      /* 12 is sizeof (mhead) and 4 is EXTRA from GNU malloc.
	 Use the values for range checking, because if range checking is off,
	 the extra bytes won't be missed terribly, but if range checking is on
	 and we used a larger request, a whole extra 4096 bytes would be
	 allocated.

	 These number are irrelevant to the new GNU malloc.  I suspect it is
	 less sensitive to the size of the request.  */
      int extra = ((((12 + DEFAULT_ROUNDING - 1) & ~(DEFAULT_ROUNDING - 1))
		    + 4 + DEFAULT_ROUNDING - 1)
		   & ~(DEFAULT_ROUNDING - 1));
      size = 4096 - extra;
    }

  h->chunkfun = (struct _obstack_chunk * (*)()) chunkfun;
  h->freefun = freefun;
  h->chunk_size = size;
  h->alignment_mask = alignment - 1;
  h->use_extra_arg = 0;

  chunk = h->chunk = CALL_CHUNKFUN (h, h -> chunk_size);
  h->next_free = h->object_base = chunk->contents;
  h->chunk_limit = chunk->limit
    = (char *) chunk + h->chunk_size;
  chunk->prev = 0;
  /* The initial chunk now contains no empty object.  */
  h->maybe_empty_object = 0;
}

void
_obstack_begin_1 (h, size, alignment, chunkfun, freefun, arg)
     struct obstack *h;
     int size;
     int alignment;
     POINTER (*chunkfun) ();
     void (*freefun) ();
     POINTER arg;
{
  register struct _obstack_chunk* chunk; /* points to new chunk */

  if (alignment == 0)
    alignment = DEFAULT_ALIGNMENT;
  if (size == 0)
    /* Default size is what GNU malloc can fit in a 4096-byte block.  */
    {
      /* 12 is sizeof (mhead) and 4 is EXTRA from GNU malloc.
	 Use the values for range checking, because if range checking is off,
	 the extra bytes won't be missed terribly, but if range checking is on
	 and we used a larger request, a whole extra 4096 bytes would be
	 allocated.

	 These number are irrelevant to the new GNU malloc.  I suspect it is
	 less sensitive to the size of the request.  */
      int extra = ((((12 + DEFAULT_ROUNDING - 1) & ~(DEFAULT_ROUNDING - 1))
		    + 4 + DEFAULT_ROUNDING - 1)
		   & ~(DEFAULT_ROUNDING - 1));
      size = 4096 - extra;
    }

  h->chunkfun = (struct _obstack_chunk * (*)()) chunkfun;
  h->freefun = freefun;
  h->chunk_size = size;
  h->alignment_mask = alignment - 1;
  h->extra_arg = arg;
  h->use_extra_arg = 1;

  chunk = h->chunk = CALL_CHUNKFUN (h, h -> chunk_size);
  h->next_free = h->object_base = chunk->contents;
  h->chunk_limit = chunk->limit
    = (char *) chunk + h->chunk_size;
  chunk->prev = 0;
  /* The initial chunk now contains no empty object.  */
  h->maybe_empty_object = 0;
}

/* Allocate a new current chunk for the obstack *H
   on the assumption that LENGTH bytes need to be added
   to the current object, or a new object of length LENGTH allocated.
   Copies any partial object from the end of the old chunk
   to the beginning of the new one.  */

void
_obstack_newchunk (h, length)
     struct obstack *h;
     int length;
{
  register struct _obstack_chunk*	old_chunk = h->chunk;
  register struct _obstack_chunk*	new_chunk;
  register long	new_size;
  register int obj_size = h->next_free - h->object_base;
  register int i;
  int already;

  /* Compute size for new chunk.  */
  new_size = (obj_size + length) + (obj_size >> 3) + 100;
  if (new_size < h->chunk_size)
    new_size = h->chunk_size;

  /* Allocate and initialize the new chunk.  */
  new_chunk = h->chunk = CALL_CHUNKFUN (h, new_size);
  new_chunk->prev = old_chunk;
  new_chunk->limit = h->chunk_limit = (char *) new_chunk + new_size;

  /* Move the existing object to the new chunk.
     Word at a time is fast and is safe if the object
     is sufficiently aligned.  */
  if (h->alignment_mask + 1 >= DEFAULT_ALIGNMENT)
    {
      for (i = obj_size / sizeof (COPYING_UNIT) - 1;
	   i >= 0; i--)
	((COPYING_UNIT *)new_chunk->contents)[i]
	  = ((COPYING_UNIT *)h->object_base)[i];
      /* We used to copy the odd few remaining bytes as one extra COPYING_UNIT,
	 but that can cross a page boundary on a machine
	 which does not do strict alignment for COPYING_UNITS.  */
      already = obj_size / sizeof (COPYING_UNIT) * sizeof (COPYING_UNIT);
    }
  else
    already = 0;
  /* Copy remaining bytes one by one.  */
  for (i = already; i < obj_size; i++)
    new_chunk->contents[i] = h->object_base[i];

  /* If the object just copied was the only data in OLD_CHUNK,
     free that chunk and remove it from the chain.
     But not if that chunk might contain an empty object.  */
  if (h->object_base == old_chunk->contents && ! h->maybe_empty_object)
    {
      new_chunk->prev = old_chunk->prev;
      CALL_FREEFUN (h, old_chunk);
    }

  h->object_base = new_chunk->contents;
  h->next_free = h->object_base + obj_size;
  /* The new chunk certainly contains no empty object yet.  */
  h->maybe_empty_object = 0;
}

/* Return nonzero if object OBJ has been allocated from obstack H.
   This is here for debugging.
   If you use it in a program, you are probably losing.  */

int
_obstack_allocated_p (h, obj)
     struct obstack *h;
     POINTER obj;
{
  register struct _obstack_chunk*  lp;	/* below addr of any objects in this chunk */
  register struct _obstack_chunk*  plp;	/* point to previous chunk if any */

  lp = (h)->chunk;
  /* We use >= rather than > since the object cannot be exactly at
     the beginning of the chunk but might be an empty object exactly
     at the end of an adjacent chunk. */
  while (lp != 0 && ((POINTER)lp >= obj || (POINTER)(lp)->limit < obj))
    {
      plp = lp->prev;
      lp = plp;
    }
  return lp != 0;
}

/* Free objects in obstack H, including OBJ and everything allocate
   more recently than OBJ.  If OBJ is zero, free everything in H.  */

#undef obstack_free

/* This function has two names with identical definitions.
   This is the first one, called from non-ANSI code.  */

void
_obstack_free (h, obj)
     struct obstack *h;
     POINTER obj;
{
  register struct _obstack_chunk*  lp;	/* below addr of any objects in this chunk */
  register struct _obstack_chunk*  plp;	/* point to previous chunk if any */

  lp = h->chunk;
  /* We use >= because there cannot be an object at the beginning of a chunk.
     But there can be an empty object at that address
     at the end of another chunk.  */
  while (lp != 0 && ((POINTER)lp >= obj || (POINTER)(lp)->limit < obj))
    {
      plp = lp->prev;
      CALL_FREEFUN (h, lp);
      lp = plp;
      /* If we switch chunks, we can't tell whether the new current
	 chunk contains an empty object, so assume that it may.  */
      h->maybe_empty_object = 1;
    }
  if (lp)
    {
      h->object_base = h->next_free = (char *)(obj);
      h->chunk_limit = lp->limit;
      h->chunk = lp;
    }
  else if (obj != 0)
    /* obj is not in any of the chunks! */
    abort ();
}

/* This function is used from ANSI code.  */

void
obstack_free (h, obj)
     struct obstack *h;
     POINTER obj;
{
  register struct _obstack_chunk*  lp;	/* below addr of any objects in this chunk */
  register struct _obstack_chunk*  plp;	/* point to previous chunk if any */

  lp = h->chunk;
  /* We use >= because there cannot be an object at the beginning of a chunk.
     But there can be an empty object at that address
     at the end of another chunk.  */
  while (lp != 0 && ((POINTER)lp >= obj || (POINTER)(lp)->limit < obj))
    {
      plp = lp->prev;
      CALL_FREEFUN (h, lp);
      lp = plp;
      /* If we switch chunks, we can't tell whether the new current
	 chunk contains an empty object, so assume that it may.  */
      h->maybe_empty_object = 1;
    }
  if (lp)
    {
      h->object_base = h->next_free = (char *)(obj);
      h->chunk_limit = lp->limit;
      h->chunk = lp;
    }
  else if (obj != 0)
    /* obj is not in any of the chunks! */
    abort ();
}

#if 0
/* These are now turned off because the applications do not use it
   and it uses bcopy via obstack_grow, which causes trouble on sysV.  */

/* Now define the functional versions of the obstack macros.
   Define them to simply use the corresponding macros to do the job.  */

#ifdef __STDC__
/* These function definitions do not work with non-ANSI preprocessors;
   they won't pass through the macro names in parentheses.  */

/* The function names appear in parentheses in order to prevent
   the macro-definitions of the names from being expanded there.  */

POINTER (obstack_base) (obstack)
     struct obstack *obstack;
{
  return obstack_base (obstack);
}

POINTER (obstack_next_free) (obstack)
     struct obstack *obstack;
{
  return obstack_next_free (obstack);
}

int (obstack_object_size) (obstack)
     struct obstack *obstack;
{
  return obstack_object_size (obstack);
}

int (obstack_room) (obstack)
     struct obstack *obstack;
{
  return obstack_room (obstack);
}

void (obstack_grow) (obstack, pointer, length)
     struct obstack *obstack;
     POINTER pointer;
     int length;
{
  obstack_grow (obstack, pointer, length);
}

void (obstack_grow0) (obstack, pointer, length)
     struct obstack *obstack;
     POINTER pointer;
     int length;
{
  obstack_grow0 (obstack, pointer, length);
}

void (obstack_1grow) (obstack, character)
     struct obstack *obstack;
     int character;
{
  obstack_1grow (obstack, character);
}

void (obstack_blank) (obstack, length)
     struct obstack *obstack;
     int length;
{
  obstack_blank (obstack, length);
}

void (obstack_1grow_fast) (obstack, character)
     struct obstack *obstack;
     int character;
{
  obstack_1grow_fast (obstack, character);
}

void (obstack_blank_fast) (obstack, length)
     struct obstack *obstack;
     int length;
{
  obstack_blank_fast (obstack, length);
}

POINTER (obstack_finish) (obstack)
     struct obstack *obstack;
{
  return obstack_finish (obstack);
}

POINTER (obstack_alloc) (obstack, length)
     struct obstack *obstack;
     int length;
{
  return obstack_alloc (obstack, length);
}

POINTER (obstack_copy) (obstack, pointer, length)
     struct obstack *obstack;
     POINTER pointer;
     int length;
{
  return obstack_copy (obstack, pointer, length);
}

POINTER (obstack_copy0) (obstack, pointer, length)
     struct obstack *obstack;
     POINTER pointer;
     int length;
{
  return obstack_copy0 (obstack, pointer, length);
}

#endif /* __STDC__ */

#endif /* 0 */

#endif	/* _LIBC or not __GNU_LIBRARY__.  */
/* search.c - searching subroutines using dfa, kwset and regex for grep.
   Copyright (C) 1992 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

   Written August 1992 by Mike Haertel. */

/*#include <ctype.h>*/

#ifdef STDC_HEADERS
#if flag_limits==0
#undef RE_DUP_MAX
#include <limits.h>
#define flag_limits 1
#endif
#if flag_stdlib==0
#include <stdlib.h>
#define flag_stdlib 1
#endif
#else
#define UCHAR_MAX 255
#if flag_systypes==0
#include <sys/types.h>
#define flag_systypes 1
#endif
extern char *malloc();
#endif

#ifdef HAVE_MEMCHR
#if flag_string==0
#include <string.h>
#define flag_string 1
#endif
#ifdef NEED_MEMORY_H
#if flag_memory==0
#include <memory.h>
#define flag_memory 1
#endif
#endif
#else
#ifdef __STDC__
extern void *memchr();
#else
extern char *memchr();
#endif
#endif

#if defined(HAVE_STRING_H) || defined(STDC_HEADERS)
#undef bcopy
#define bcopy(s, d, n) memcpy((d), (s), (n))
#endif

#ifdef isascii
#define ISALNUM(C) (isascii(C) && isalnum(C))
#define ISUPPER(C) (isascii(C) && isupper(C))
#else
#define ISALNUM(C) isalnum(C)
#define ISUPPER(C) isupper(C)
#endif

#define TOLOWER(C) (ISUPPER(C) ? tolower(C) : (C))

/*#include "grep.h"*/
/*#include "dfa.h"*/
/*#include "kwset.h"*/
/*#include "regex.h"*/

#define NCHAR (UCHAR_MAX + 1)

#if __STDC__
static void Gcompile(char *, size_t);
static void Ecompile(char *, size_t);
static char *EGexecute(char *, size_t, char **);
static void Fcompile(char *, size_t);
static char *Fexecute(char *, size_t, char **);
#else
static void Gcompile();
static void Ecompile();
static char *EGexecute();
static void Fcompile();
static char *Fexecute();
#endif

/* Here is the matchers vector for the main program. */
struct matcher matchers[] = {
  { "default", Gcompile, EGexecute },
  { "grep", Gcompile, EGexecute },
  { "ggrep", Gcompile, EGexecute },
  { "egrep", Ecompile, EGexecute },
  { "posix-egrep", Ecompile, EGexecute },
  { "gegrep", Ecompile, EGexecute },
  { "fgrep", Fcompile, Fexecute },
  { "gfgrep", Fcompile, Fexecute },
  { 0, 0, 0 },
};

/* For -w, we also consider _ to be word constituent.  */
#define WCHAR(C) (ISALNUM(C) || (C) == '_')

/* DFA compiled regexp. */
static struct dfa dfa_1;

/* Regex compiled regexp. */
static struct re_pattern_buffer regex;

/* KWset compiled pattern.  For Ecompile and Gcompile, we compile
   a list of strings, at least one of which is known to occur in
   any string matching the regexp. */
static kwset_t kwset;

/* Last compiled fixed string known to exactly match the regexp.
   If kwsexec() returns < lastexact, then we don't need to
   call the regexp matcher at all. */
static int lastexact;

void
dfaerror(mesg)
     char *mesg;
{
  fatal(mesg, 0);
}

static void
kwsinit()
{
  static char trans[NCHAR];
  int i;

  if (_br(match_icase,0x48ff250,10573,7))
    for (i = 0; i < NCHAR; ++i)
      trans[i] = TOLOWER(i);

  if (_br(!(kwset = kwsalloc(match_icase ? trans : (char *) 0)),0x49008a8,10577,7))
    fatal("memory exhausted", 0);
}  

/* If the DFA turns out to have some set of fixed strings one of
   which must occur in the match, then we build a kwset matcher
   to find those strings, and thus quickly filter out impossible
   matches. */
static void
kwsmusts()
{
  struct dfamust *dm;
  char *err;

  if (_br(dfa_1.musts,0x4900c80,10591,7))
    {
      kwsinit();
      /* First, we compile in the substrings known to be exact
	 matches.  The kwset matcher will return the index
	 of the matching string that it chooses. */
      for (dm = dfa_1.musts; _br(dm,0x4900df0,10597,30); dm = dm->next)
	{
	  if (_br(!dm->exact,0x4900f78,10599,8))
	    continue;
	  ++lastexact;
	  if (_br((err = kwsincr(kwset, dm->must, strlen(dm->must))) != 0,0x4901348,10602,8))
	    fatal(err, 0);
	}
      /* Now, we compile the substrings that will require
	 the use of the regexp matcher.  */
      for (dm = dfa_1.musts; _br(dm,0x49015f0,10607,30); dm = dm->next)
	{
	  if (_br(dm->exact,0x4901760,10609,8))
	    continue;
	  if (_br((err = kwsincr(kwset, dm->must, strlen(dm->must))) != 0,0x4901ae0,10611,8))
	    fatal(err, 0);
	}
      if (_br((err = kwsprep(kwset)) != 0,0x4901de8,10614,11))
	fatal(err, 0);
    }
}

static void
Gcompile(pattern, size)
     char *pattern;
     size_t size;
{
#ifdef __STDC__
  const
#endif
  char *err;

  re_set_syntax(RE_SYNTAX_GREP | RE_HAT_LISTS_NOT_NEWLINE);
  dfasyntax(RE_SYNTAX_GREP | RE_HAT_LISTS_NOT_NEWLINE, match_icase);

  if (_br((err = re_compile_pattern(pattern, size, &regex)) != 0,0x4905340,10632,7))
    fatal(err, 0);

  dfainit(&dfa_1);

  /* In the match_words and match_lines cases, we use a different pattern
     for the DFA matcher that will quickly throw out cases that won't work.
     Then if DFA succeeds we do some hairy stuff using the regex matcher
     to decide whether the match should really count. */
  if (_br(match_words || match_lines,0x49055b0,10641,7))
    {
      /* In the whole-word case, we use the pattern:
	 (^|[^A-Za-z_])(userpattern)([^A-Za-z_]|$).
	 In the whole-line case, we use the pattern:
	 ^(userpattern)$.
	 BUG: Using [A-Za-z_] is locale-dependent!  */

      char *n = malloc(size + 50);
      int i = 0;

      strcpy(n, "");

      if (_br(match_lines,0x4905990,10654,11))
	strcpy(n, "^\\(");
      if (_br(match_words,0x4905b50,10656,11))
	strcpy(n, "\\(^\\|[^0-9A-Za-z_]\\)\\(");

      i = strlen(n);
      bcopy(pattern, n + i, size);
      i += size;

      if (_br(match_words,0x4906170,10663,11))
	strcpy(n + i, "\\)\\([^0-9A-Za-z_]\\|$\\)");
      if (_br(match_lines,0x4906390,10665,11))
	strcpy(n + i, "\\)$");

      i += strlen(n + i);
      dfacomp(n, i, &dfa_1, 1);
    }
  else
    dfacomp(pattern, size, &dfa_1, 1);

  kwsmusts();
}

static void
Ecompile(pattern, size)
     char *pattern;
     size_t size;
{
#ifdef __STDC__
  const
#endif
  char *err;

  if (_br(strcmp(matcher, "posix-egrep") == 0,0x4906f40,10687,7))
    {
      re_set_syntax(RE_SYNTAX_POSIX_EGREP);
      dfasyntax(RE_SYNTAX_POSIX_EGREP, match_icase);
    }
  else
    {
      re_set_syntax(RE_SYNTAX_EGREP);
      dfasyntax(RE_SYNTAX_EGREP, match_icase);
    }

  if (_br((err = re_compile_pattern(pattern, size, &regex)) != 0,0x490f3a8,10698,7))
    fatal(err, 0);

  dfainit(&dfa_1);

  /* In the match_words and match_lines cases, we use a different pattern
     for the DFA matcher that will quickly throw out cases that won't work.
     Then if DFA succeeds we do some hairy stuff using the regex matcher
     to decide whether the match should really count. */
  if (_br(match_words || match_lines,0x490f618,10707,7))
    {
      /* In the whole-word case, we use the pattern:
	 (^|[^A-Za-z_])(userpattern)([^A-Za-z_]|$).
	 In the whole-line case, we use the pattern:
	 ^(userpattern)$.
	 BUG: Using [A-Za-z_] is locale-dependent!  */

      char *n = malloc(size + 50);
      int i = 0;

      strcpy(n, "");

      if (_br(match_lines,0x490f9f8,10720,11))
	strcpy(n, "^(");
      if (_br(match_words,0x490fbb8,10722,11))
	strcpy(n, "(^|[^0-9A-Za-z_])(");

      i = strlen(n);
      bcopy(pattern, n + i, size);
      i += size;

      if (_br(match_words,0x49101d8,10729,11))
	strcpy(n + i, ")([^0-9A-Za-z_]|$)");
      if (_br(match_lines,0x49103f8,10731,11))
	strcpy(n + i, ")$");

      i += strlen(n + i);
      dfacomp(n, i, &dfa_1, 1);
    }
  else
    dfacomp(pattern, size, &dfa_1, 1);

  kwsmusts();
}

static char *
EGexecute(buf, size, endp)
     char *buf;
     size_t size;
     char **endp;
{
  register char *buflim, *beg, *end, save;
  int backref, start, len;
  struct kwsmatch kwsm;
  static struct re_registers regs; /* This is static on account of a BRAIN-DEAD
				    Q@#%!# library interface in regex.c.  */

  buflim = buf + size;

  for (beg = end = buf; _br(end < buflim,0x4911cb0,10757,25); beg = end + 1)
    {
      if (_br(kwset,0x4911dd8,10759,11))
	{
	  /* Find a possible match using the KWset matcher. */
	  beg = kwsexec(kwset, beg, buflim - beg, &kwsm);
	  if (_br(!beg,0x4912090,10763,8))
	    goto failure;
	  /* Narrow down to the line containing the candidate, and
	     run it through DFA. */
	  end = memchr(beg, '\n', buflim - beg);
	  if (_br(!end,0x49123b8,10768,8))
	    end = buflim;
	  while (_br(beg > buf && beg[-1] != '\n',0x4912660,10770,11))
	    --beg;
	  save = *end;
	  if (_br(kwsm.index < lastexact,0x4912870,10773,8))
	    goto success;
	  if (_br(!dfaexec(&dfa_1, beg, end, 0, (int *) 0, &backref),0x4912bb0,10775,8))
	    {
	      *end = save;
	      continue;
	    }
	  *end = save;
	  /* Successful, no backreferences encountered. */
	  if (_br(!backref,0x4912e08,10782,8))
	    goto success;
	}
      else
	{
	  /* No good fixed strings; start with DFA. */
	  save = *buflim;
	  beg = dfaexec(&dfa_1, beg, buflim, 0, (int *) 0, &backref);
	  *buflim = save;
	  if (_br(!beg,0x4913328,10791,8))
	    goto failure;
	  /* Narrow down to the line we've found. */
	  end = memchr(beg, '\n', buflim - beg);
	  if (_br(!end,0x4913608,10795,8))
	    end = buflim;
	  while (_br(beg > buf && beg[-1] != '\n',0x49138b0,10797,11))
	    --beg;
	  /* Successful, no backreferences encountered! */
	  if (_br(!backref,0x4913980,10800,8))
	    goto success;
	}
      /* If we've made it to this point, this means DFA has seen
	 a probable match, and we need to run it through Regex. */
      regex.not_eol = 0;
      if (_br((start = re_search(&regex, beg, end - beg, 0, end - beg, &regs)) >= 0,0x4913ef0,10806,11))
	{
	  len = regs.end[0] - start;
	  if (_br(!match_lines && !match_words || match_lines && len == end - beg,0x4914320,10809,8))
	    goto success;
	  /* If -w, check if the match aligns with word boundaries.
	     We do this iteratively because:
	     (a) the line may contain more than one occurence of the pattern, and
	     (b) Several alternatives in the pattern might be valid at a given
	     point, and we may need to consider a shorter one to find a word
	     boundary. */
	  if (_br(match_words,0x49143c0,10817,8))
	    while (_br(start >= 0,0x4914438,10818,13))
	      {
		if (_br((start == 0 || !WCHAR(beg[start - 1]))
		    && (len == end - beg || !WCHAR(beg[start + len])),0x49152b0,10820,7))
		  goto success;
		if (_br(len > 0,0x4915388,10823,7))
		  {
		    /* Try a shorter length anchored at the same place. */
		    --len;
		    regex.not_eol = 1;
		    len = re_match(&regex, beg, start + len, start, &regs);
		  }
		if (_br(len <= 0,0x4915830,10830,7))
		  {
		    /* Try looking further on. */
		    if (_br(start == end - beg,0x4915958,10833,11))
		      break;
		    ++start;
		    regex.not_eol = 0;
		    start = re_search(&regex, beg, end - beg,
				      start, end - beg - start, &regs);
		    len = regs.end[0] - start;
		  }
	      }
	}
    }

 failure:
  return 0;

 success:
  *endp = end < buflim ? end + 1 : end;
  return beg;
}

static void
Fcompile(pattern, size)
     char *pattern;
     size_t size;
{
  char *beg, *lim, *err;

  kwsinit();
  beg = pattern;
  do
    {
      for (lim = beg; _br(lim < pattern + size && *lim != '\n',0x4916c70,10864,23); ++lim)
	;
      if (_br((err = kwsincr(kwset, beg, lim - beg)) != 0,0x4916f90,10866,11))
	fatal(err, 0);
      if (_br(lim < pattern + size,0x49171c8,10868,11))
	++lim;
      beg = lim;
    }
  while (beg < pattern + size);

  if (_br((err = kwsprep(kwset)) != 0,0x49175c0,10874,7))
    fatal(err, 0);
}

static char *
Fexecute(buf, size, endp)
     char *buf;
     size_t size;
     char **endp;
{
  register char *beg, *try, *end;
  register size_t len;
  struct kwsmatch kwsmatch;

  for (beg = buf; _br(beg <= buf + size,0x4917dd8,10888,19); ++beg)
    {
      if (_br(!(beg = kwsexec(kwset, beg, buf + size - beg, &kwsmatch)),0x4918130,10890,11))
	return 0;
      len = kwsmatch.size[0];
      if (_br(match_lines,0x4918328,10893,11))
	{
	  if (_br(beg > buf && beg[-1] != '\n',0x4918500,10895,8))
	    continue;
	  if (_br(beg + len < buf + size && beg[len] != '\n',0x49187f8,10897,8))
	    continue;
	  goto success;
	}
      else if (_br(match_words,0x4918918,10901,16))
	for (try = beg; _br(len && try,0x4918a40,10902,18);)
	  {
	    if (try > buf && WCHAR((unsigned char) try[-1]))
	      break;
	    if (try + len < buf + size && WCHAR((unsigned char) try[len]))
	      {
		try = kwsexec(kwset, beg, --len, &kwsmatch);
		len = kwsmatch.size[0];
	      }
	    else
	      goto success;
	  }
      else
	goto success;
    }

  return 0;

 success:
  if (_br((end = memchr(beg + len, '\n', (buf + size) - (beg + len))) != 0,0x491c300,10921,7))
    ++end;
  else
    end = buf + size;
  *endp = end;
  while (_br(beg > buf && beg[-1] != '\n',0x491c740,10926,10))
    --beg;
  return beg;
}


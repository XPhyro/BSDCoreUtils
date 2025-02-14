/*	$OpenBSD: df.c,v 1.60 2019/06/28 13:34:59 deraadt Exp $	*/
/*	$NetBSD: df.c,v 1.21.2.1 1995/11/01 00:06:11 jtc Exp $	*/

/*
 * Copyright (c) 1980, 1990, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/mount.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mntent.h>
#include <assert.h>

#include "compat.h"

extern char *__progname;

/* combining data from getmntent() and statvfs() on Linux */
struct mntinfo {
    char *f_mntfromname;          /* mnt_fsname from getmntent */
    char *f_mntonname;            /* mnt_dir from getmntent */
    char *f_fstypename;           /* mnt_fsname from getmntent */
    char *f_opts;                 /* mnt_opts from getmntent */
    unsigned long f_bsize;        /* f_bsize from statvfs */
    fsblkcnt_t f_blocks;          /* f_blocks from statvfs */
    fsblkcnt_t f_bfree;           /* f_bfree from statvfs */
    fsblkcnt_t f_bavail;          /* f_bavail from statvfs */
    fsfilcnt_t f_files;           /* f_files from statvfs */
    fsfilcnt_t f_ffree;           /* f_ffree from statvfs */
    unsigned long f_flag;         /* f_flag from statvfs */
};

int		 bread(int, off_t, void *, int);
static void	 bsdprint(struct mntinfo *, long, int);
char		*getmntpt(char *);
static void	 maketypelist(char *);
static void	 posixprint(struct mntinfo *, long, int);
static void	 prthuman(struct mntinfo *, unsigned long long);
static void	 prthumanval(long long);
static void	 prtstat(struct mntinfo *, int, int, int);
static long	 regetmntinfo(struct mntinfo **, long);
static int	 selected(const char *);
static void usage(void);
static int getmntinfo(struct mntinfo **, int);
static void freemntinfo(struct mntinfo *, int);

int	hflag, iflag, kflag, lflag, nflag, Pflag;
char	**typelist = NULL;

int
main(int argc, char *argv[])
{
	struct stat stbuf;
	struct statvfs svfsbuf;
	struct mntinfo *mntbuf = NULL;
	long mntsize;
	int ch, i;
	int width, maxwidth;
	char *mntpt;

	while ((ch = getopt(argc, argv, "hiklnPt:")) != -1)
		switch (ch) {
		case 'h':
			hflag = 1;
			kflag = 0;
			break;
		case 'i':
			iflag = 1;
			break;
		case 'k':
			kflag = 1;
			hflag = 0;
			break;
		case 'l':
			lflag = 1;
			break;
		case 'n':
			nflag = 1;
			break;
		case 'P':
			Pflag = 1;
			break;
		case 't':
			if (typelist != NULL)
				errx(1, "only one -t option may be specified.");
			maketypelist(optarg);
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if ((iflag || hflag) && Pflag) {
		warnx("-h and -i are incompatible with -P");
		usage();
	}

	mntsize = getmntinfo(&mntbuf, 0);
	if (mntsize == 0)
		err(1, "retrieving information on mounted file systems");

	if (!*argv) {
		mntsize = regetmntinfo(&mntbuf, mntsize);
	} else {
		mntbuf = calloc(argc, sizeof(struct mntinfo));
		if (mntbuf == NULL)
			err(1, NULL);
		mntsize = 0;
		for (; *argv; argv++) {
			if (stat(*argv, &stbuf) == -1) {
				if ((mntpt = getmntpt(*argv)) == 0) {
					warn("%s", *argv);
					continue;
				}
			} else if (S_ISCHR(stbuf.st_mode) || S_ISBLK(stbuf.st_mode)) {
				++mntsize;
				continue;
			} else
				mntpt = *argv;
			/*
			 * Statvfs does not take a `wait' flag, so we cannot
			 * implement nflag here.
			 */
			if (!statvfs(mntpt, &svfsbuf))
				if (!selected(mntbuf[i].f_fstypename))
					warnx("%s mounted as a %s file system",
					    *argv, mntbuf[i].f_fstypename);
				else
					++mntsize;
			else
				warn("%s", *argv);
		}
	}

	if (mntsize) {
		maxwidth = 11;
		for (i = 0; i < mntsize; i++) {
			width = strlen(mntbuf[i].f_mntfromname);
			if (width > maxwidth)
				maxwidth = width;
		}

		if (Pflag)
			posixprint(mntbuf, mntsize, maxwidth);
		else
			bsdprint(mntbuf, mntsize, maxwidth);
	}

	freemntinfo(mntbuf, mntsize);

	return (mntsize ? 0 : 1);
}

char *
getmntpt(char *name)
{
	long mntsize, i;
	struct mntinfo *mntbuf;
	char *mntpt = NULL;

	mntsize = getmntinfo(&mntbuf, 0);
	for (i = 0; i < mntsize; i++) {
		if (!strcmp(mntbuf[i].f_mntfromname, name)) {
			mntpt = strdup(mntbuf[i].f_mntonname);
			break;
		}
	}
	freemntinfo(mntbuf, mntsize);
	return mntpt;
}

static enum { IN_LIST, NOT_IN_LIST } which;

static int
selected(const char *type)
{
	char **av;

	/* If no type specified, it's always selected. */
	if (typelist == NULL)
		return (1);
	for (av = typelist; *av != NULL; ++av)
		if (!strcmp(type, *av))
			return (which == IN_LIST ? 1 : 0);
	return (which == IN_LIST ? 0 : 1);
}

static void
maketypelist(char *fslist)
{
	int i;
	char *nextcp, **av;

	if ((fslist == NULL) || (fslist[0] == '\0'))
		errx(1, "empty type list");

	/*
	 * XXX
	 * Note: the syntax is "noxxx,yyy" for no xxx's and
	 * no yyy's, not the more intuitive "noyyy,noyyy".
	 */
	if (fslist[0] == 'n' && fslist[1] == 'o') {
		fslist += 2;
		which = NOT_IN_LIST;
	} else
		which = IN_LIST;

	/* Count the number of types. */
	for (i = 1, nextcp = fslist; (nextcp = strchr(nextcp, ',')) != NULL; i++)
		++nextcp;

	/* Build an array of that many types. */
	if ((av = typelist = calloc(i + 1, sizeof(char *))) == NULL)
		err(1, NULL);
	av[0] = fslist;
	for (i = 1, nextcp = fslist; (nextcp = strchr(nextcp, ',')) != NULL; i++) {
		*nextcp = '\0';
		av[i] = ++nextcp;
	}
	/* Terminate the array. */
	av[i] = NULL;
}

/*
 * Make a pass over the filesystem info in ``mntbuf'' filtering out
 * filesystem types not in ``fsmask'' and possibly re-stating to get
 * current (not cached) info.  Returns the new count of valid statvfs bufs.
 */
static long
regetmntinfo(struct mntinfo **mntbufp, long mntsize)
{
	int i, j;
	struct mntinfo *mntbuf;
	struct statvfs svfsbuf;

	if (!lflag && typelist == NULL)
		return (nflag ? mntsize : getmntinfo(mntbufp, 0));

	mntbuf = *mntbufp;
	j = 0;
	for (i = 0; i < mntsize; i++) {
		if (!selected(mntbuf[i].f_fstypename))
			continue;
		if (nflag)
			mntbuf[j] = mntbuf[i];
		else {
			(void)statvfs(mntbuf[i].f_mntonname, &svfsbuf);

			free(mntbuf[j].f_fstypename);
			mntbuf[j].f_fstypename = strdup(mntbuf[i].f_fstypename);
			free(mntbuf[j].f_mntfromname);
			mntbuf[j].f_mntfromname = strdup(mntbuf[i].f_mntfromname);
			free(mntbuf[j].f_mntfromname);
			mntbuf[j].f_mntonname = strdup(mntbuf[i].f_mntonname);
			free(mntbuf[j].f_opts);
			mntbuf[j].f_opts = strdup(mntbuf[i].f_opts);

			mntbuf[j].f_flag = svfsbuf.f_flag;
			mntbuf[j].f_blocks = svfsbuf.f_blocks;
			mntbuf[j].f_bsize = svfsbuf.f_bsize;
			mntbuf[j].f_bfree = svfsbuf.f_bfree;
			mntbuf[j].f_bavail = svfsbuf.f_bavail;
			mntbuf[j].f_files = svfsbuf.f_files;
			mntbuf[j].f_ffree = svfsbuf.f_ffree;
		}
		j++;
	}
	return (j);
}

/*
 * "human-readable" output: use 3 digits max.--put unit suffixes at
 * the end.  Makes output compact and easy-to-read esp. on huge disks.
 * Code moved into libutil; this is now just a wrapper.
 */
static void
prthumanval(long long bytes)
{
	char ret[FMT_SCALED_STRSIZE];

	if (fmt_scaled(bytes, ret) == -1) {
		(void)printf(" %lld", bytes);
		return;
	}
	(void)printf(" %7s", ret);
}

static void
prthuman(struct mntinfo *sfsp, unsigned long long used)
{
	prthumanval(sfsp->f_blocks * sfsp->f_bsize);
	prthumanval(used * sfsp->f_bsize);
	prthumanval(sfsp->f_bavail * sfsp->f_bsize);
}

/*
 * Convert statvfs returned filesystem size into BLOCKSIZE units.
 * Attempts to avoid overflow for large filesystems.
 */
#define fsbtoblk(num, fsbs, bs) \
	(((fsbs) != 0 && (fsbs) < (bs)) ? \
		(num) / ((bs) / (fsbs)) : (num) * ((fsbs) / (bs)))

/*
 * Print out status about a filesystem.
 */
static void
prtstat(struct mntinfo *sfsp, int maxwidth, int headerlen, int blocksize)
{
	uint64_t used, inodes;
	int64_t availblks;

	(void)printf("%-*.*s", maxwidth, maxwidth, sfsp->f_mntfromname);
	used = sfsp->f_blocks - sfsp->f_bfree;
	availblks = sfsp->f_bavail + used;
	if (hflag)
		prthuman(sfsp, used);
	else
		(void)printf(" %*llu %9llu %9lld", headerlen,
		    fsbtoblk(sfsp->f_blocks, sfsp->f_bsize, blocksize),
		    fsbtoblk(used, sfsp->f_bsize, blocksize),
		    fsbtoblk(sfsp->f_bavail, sfsp->f_bsize, blocksize));
	(void)printf(" %5.0f%%",
	    availblks == 0 ? 100.0 : (double)used / (double)availblks * 100.0);
	if (iflag) {
		inodes = sfsp->f_files;
		used = inodes - sfsp->f_ffree;
		(void)printf(" %7llu %7llu %5.0f%% ", used, sfsp->f_ffree,
		   inodes == 0 ? 100.0 : (double)used / (double)inodes * 100.0);
	} else
		(void)printf("  ");
	(void)printf("  %s\n", sfsp->f_mntonname);
}

/*
 * Print in traditional BSD format.
 */
static void
bsdprint(struct mntinfo *mntbuf, long mntsize, int maxwidth)
{
	int i;
	char *header;
	int headerlen;
	long blocksize;

	/* Print the header line */
	if (hflag) {
		header = "   Size";
		headerlen = strlen(header);
		(void)printf("%-*.*s %s    Used   Avail Capacity",
			     maxwidth, maxwidth, "Filesystem", header);
	} else {
		if (kflag) {
			blocksize = 1024;
			header = "1K-blocks";
			headerlen = strlen(header);
		} else
			header = getbsize(&headerlen, &blocksize);
		(void)printf("%-*.*s %s      Used     Avail Capacity",
			     maxwidth, maxwidth, "Filesystem", header);
	}
	if (iflag)
		(void)printf(" iused   ifree  %%iused");
	(void)printf("  Mounted on\n");


	for (i = 0; i < mntsize; i++)
		prtstat(&mntbuf[i], maxwidth, headerlen, blocksize);
	return;
}

/*
 * Print in format defined by POSIX 1002.2, invoke with -P option.
 */
static void
posixprint(struct mntinfo *mntbuf, long mntsize, int maxwidth)
{
	int i;
	int blocksize;
	char *blockstr;
	struct mntinfo *sfsp;
	long long used, avail;
	double percentused;

	if (kflag) {
		blocksize = 1024;
		blockstr = "1024-blocks";
	} else {
		blocksize = 512;
		blockstr = " 512-blocks";
	}

	(void)printf(
	    "%-*.*s %s       Used   Available Capacity Mounted on\n",
	    maxwidth, maxwidth, "Filesystem", blockstr);

	for (i = 0; i < mntsize; i++) {
		sfsp = &mntbuf[i];
		used = sfsp->f_blocks - sfsp->f_bfree;
		avail = sfsp->f_bavail + used;
		if (avail == 0)
			percentused = 100.0;
		else
			percentused = (double)used / (double)avail * 100.0;

		(void) printf ("%-*.*s %*lld %10lld %11lld %5.0f%%   %s\n",
			maxwidth, maxwidth, sfsp->f_mntfromname,
			(int)strlen(blockstr),
			fsbtoblk(sfsp->f_blocks, sfsp->f_bsize, blocksize),
			fsbtoblk(used, sfsp->f_bsize, blocksize),
			fsbtoblk(sfsp->f_bavail, sfsp->f_bsize, blocksize),
			percentused, sfsp->f_mntonname);
	}
}

int
bread(int rfd, off_t off, void *buf, int cnt)
{
	int nr;

	if ((nr = pread(rfd, buf, cnt, off)) != cnt) {
		/* Probably a dismounted disk if errno == EIO. */
		if (errno != EIO)
			(void)fprintf(stderr, "\ndf: %lld: %s\n",
			    (long long)off, strerror(nr > 0 ? EIO : errno));
		return (0);
	}
	return (1);
}

static void
usage(void)
{
	(void)fprintf(stderr,
	    "usage: %s [-hiklnP] [-t type] [[file | file_system] ...]\n",
	    __progname);
	exit(1);
}

static int
getmntinfo(struct mntinfo **mntbuf, int flags)
{
	struct mntinfo *list = NULL;
	struct mntinfo *current = NULL;
	struct mntent *ent = NULL;
	int mntsize = 0;
	FILE *fp = NULL;
	struct statvfs svfsbuf;

	fp = setmntent(_PATH_MOUNTED, "r");

	if (fp == NULL) {
	    err(1, "setmntent");
	}

	while ((ent = getmntent(fp)) != NULL) {
	    /* skip if necessary */
	    if (!strcmp(ent->mnt_opts, MNTTYPE_IGNORE)) {
	        continue;
	    }

	    /* skip any mount points that are not a device node or a tmpfs */
	    if (strncmp(ent->mnt_fsname, "/dev/", 5) && strcmp(ent->mnt_fsname, "tmpfs")) {
	        continue;
	    }

	    /* allocate the entry */
	    list = realloc(list, (mntsize + 1) * sizeof(*list));
	    assert(list != NULL);
	    current = list + mntsize;

	    /* fill the struct with getmntent fields */
	    current->f_fstypename = strdup(ent->mnt_type);
	    current->f_mntfromname = strdup(ent->mnt_fsname);
	    current->f_mntonname = strdup(ent->mnt_dir);
	    current->f_opts = strdup(ent->mnt_opts);

	    /* get statvfs fields and copy those over */
	    if (statvfs(current->f_mntonname, &svfsbuf) == -1) {
	        err(1, "statvfs");
	    }

	    current->f_flag = svfsbuf.f_flag;
	    current->f_blocks = svfsbuf.f_blocks;
	    current->f_bsize = svfsbuf.f_bsize;
	    current->f_bfree = svfsbuf.f_bfree;
	    current->f_bavail = svfsbuf.f_bavail;
	    current->f_files = svfsbuf.f_files;
	    current->f_ffree = svfsbuf.f_ffree;

	    mntsize++;
	}

	endmntent(fp);

	*mntbuf = list;
	return mntsize;
}

static void
freemntinfo(struct mntinfo *mntbuf, int mntsize)
{
	int i = 0;

	for (i = 0; i < mntsize; i++) {
	    free(mntbuf[i].f_fstypename);
	    free(mntbuf[i].f_mntfromname);
	    free(mntbuf[i].f_mntonname);
	    free(mntbuf[i].f_opts);
	}

	free(mntbuf);
	return;
}

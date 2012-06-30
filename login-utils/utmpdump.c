/*
 * utmpdump
 *
 * Simple program to dump UTMP and WTMP files in raw format, so they can be
 * examined.
 *
 * Based on utmpdump dump from sysvinit suite.
 *
 * Copyright (C) 1991-2000 Miquel van Smoorenburg <miquels@cistron.nl>
 *
 * Copyright (C) 1998 Danek Duvall <duvall@alumni.princeton.edu>
 * Copyright (C) 2012 Karel Zak <kzak@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <utmp.h>
#include <time.h>
#include <ctype.h>
#include <getopt.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "c.h"
#include "nls.h"
#include "xalloc.h"
#include "closestream.h"

static char *timetostr(const time_t time)
{
	static char s[29];    /* [Sun Sep 01 00:00:00 1998 PST] */

	if (time != 0)
		strftime(s, 29, "%a %b %d %T %Y %Z", localtime(&time));
	else
		s[0] = '\0';

	return s;
}

static time_t strtotime(const char *s_time)
{
	struct tm tm;

	memset(&tm, '\0', sizeof(struct tm));

	if (s_time[0] == ' ' || s_time[0] == '\0')
		return (time_t)0;

	strptime(s_time, "%a %b %d %T %Y", &tm);

	/* Cheesy way of checking for DST */
	if (s_time[26] == 'D')
		tm.tm_isdst = 1;

	return mktime(&tm);
}

#define cleanse(x) xcleanse(x, sizeof(x))
static void xcleanse(char *s, int len)
{
	for ( ; *s && len-- > 0; s++)
		if (!isprint(*s) || *s == '[' || *s == ']')
			*s = '?';
}

static void unspace(char *s, int len)
{
	while (*s && *s != ' ' && len--)
		++s;

	if (len > 0)
		*s = '\0';
}

static void print_utline(struct utmp ut)
{
	char *addr_string, *time_string;
	struct in_addr in;

	in.s_addr = ut.ut_addr;
	addr_string = inet_ntoa(in);
	time_string = timetostr(ut.ut_time);
	cleanse(ut.ut_id);
	cleanse(ut.ut_user);
	cleanse(ut.ut_line);
	cleanse(ut.ut_host);

        /*            pid    id       user     line     host     addr       time */
	printf("[%d] [%05d] [%-4.4s] [%-*.*s] [%-*.*s] [%-*.*s] [%-15.15s] [%-28.28s]\n",
	       ut.ut_type, ut.ut_pid, ut.ut_id, 8, UT_NAMESIZE, ut.ut_user,
	       12, UT_LINESIZE, ut.ut_line, 20, UT_HOSTSIZE, ut.ut_host,
               addr_string, time_string);
}

static void dump(FILE *fp, int forever)
{
	struct utmp ut;

	if (forever)
		fseek(fp, -10 * sizeof(ut), SEEK_END);

	do {
		while (fread(&ut, sizeof(ut), 1, fp) == 1)
			print_utline(ut);
		if (forever)
			sleep(1);
	} while (forever);
}

/* This function won't work properly if there's a ']' or a ' ' in the real
 * token.  Thankfully, this should never happen.  */
static int gettok(char *line, char *dest, int size, int eatspace)
{
	int bpos, epos, eaten;
        char *t;

	bpos = strchr(line, '[') - line;
	if (bpos < 0)
		errx(EXIT_FAILURE, _("Extraneous newline in file. Exiting."));

	line += 1 + bpos;
	epos = strchr(line, ']') - line;
	if (epos < 0)
		errx(EXIT_FAILURE,_("Extraneous newline in file. Exiting."));

	line[epos] = '\0';
	eaten = bpos + epos + 1;

	if (eatspace) {
                if ((t = strchr(line, ' ')))
                    *t = 0;
	}
        strncpy(dest, line, size);

	return eaten + 1;
}

static void undump(FILE *fp)
{
	struct utmp ut;
	char s_addr[16], s_time[29], *linestart, *line;
	int count = 0;

	line = linestart = xmalloc(1024 * sizeof(*linestart));
	s_addr[15] = 0;
	s_time[28] = 0;

	while (fgets(linestart, 1023, fp))
	{
		line = linestart;
                memset(&ut, '\0', sizeof(ut));
                sscanf(line, "[%hd] [%d] [%4c] ", &ut.ut_type, &ut.ut_pid, ut.ut_id);

		line += 19;
                line += gettok(line, ut.ut_user, sizeof(ut.ut_user), 1);
                line += gettok(line, ut.ut_line, sizeof(ut.ut_line), 1);
                line += gettok(line, ut.ut_host, sizeof(ut.ut_host), 1);
		line += gettok(line, s_addr, sizeof(s_addr)-1, 1);
		line += gettok(line, s_time, sizeof(s_time)-1, 0);

                ut.ut_addr = inet_addr(s_addr);
                ut.ut_time = strtotime(s_time);

                fwrite(&ut, sizeof(ut), 1, stdout);

		++count;
	}

	free(linestart);
}

static void __attribute__((__noreturn__)) usage(FILE *out)
{
	fputs(USAGE_HEADER, out);

	fprintf(out,
		_(" %s [options]\n"), program_invocation_short_name);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -f, --follow           output appended data as the file grows\n"
		" -r, --reverse          write back dumped data into utmp file\n"
		" -h, --help             display this help and exit\n"
		" -V, --version          output version information and exit\n"), out);

	fprintf(out, USAGE_MAN_TAIL("utmpdump(1)"));
	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
	int c;
	FILE *fp;
	int reverse = 0, forever = 0;
	const char *filename = NULL;

	static const struct option longopts[] = {
		{ "follow",  0, 0, 'f' },
		{ "reverse", 0, 0, 'r' },
		{ "help",    0, 0, 'h' },
		{ "version", 0, 0, 'V' },
		{ NULL, 0, 0, 0 }
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((c = getopt_long(argc, argv, "frhV", longopts, NULL)) != -1) {
		switch (c) {
		case 'r':
			reverse = 1;
			break;

		case 'f':
			forever = 1;
			break;

		case 'h':
			usage(stdout);
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		default:
			usage(stderr);
		}
	}

	if (optind < argc) {
		filename = argv[optind];
		fp = fopen(filename, "r");
		if (!fp)
			err(EXIT_FAILURE, _("%s: open failed"), filename);
	} else {
		filename = "stdin";
		fp = stdin;
	}

	if (reverse) {
		fprintf(stderr, _("Utmp undump of %s\n"), filename);
		undump(fp);
	} else {
		fprintf(stderr, _("Utmp dump of %s\n"), filename);
		dump(fp, forever);
	}

	if (fp != stdin)
		fclose(fp);

	return EXIT_SUCCESS;
}

/*
 * Copyright (c) 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 2000
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#include "varattrs.h"

#ifndef lint
static const char copyright[] _U_ =
    "@(#) Copyright (c) 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 2000\n\
The Regents of the University of California.  All rights reserved.\n";
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#ifdef _WIN32
  #include "getopt.h"
#else
  #include <unistd.h>
#endif
#include <errno.h>
#ifndef _WIN32
  #include <signal.h>
#endif
#include <sys/types.h>

#include <pcap.h>

#include "pcap/funcattrs.h"

#ifdef _WIN32
  #include "portability.h"
#endif

static char *program_name;

/* Forwards */
static void PCAP_NORETURN usage(void);
static void PCAP_NORETURN error(const char *, ...) PCAP_PRINTFLIKE(1, 2);
static void warning(const char *, ...) PCAP_PRINTFLIKE(1, 2);
static char *copy_argv(char **);

static pcap_t *pd;

#ifdef _WIN32
static BOOL WINAPI
stop_capture(DWORD ctrltype _U_)
{
	pcap_breakloop(pd);
	return TRUE;
}
#else
static void
stop_capture(int signum _U_)
{
	pcap_breakloop(pd);
}
#endif

#define COMMAND_OPTIONS	"Li:w:y:"

int
main(int argc, char **argv)
{
	register int op;
	register char *cp, *cmdbuf, *device, *savefile;
	pcap_if_t *devlist;
	int show_dlt_types = 0;
	int ndlts;
	int *dlts;
	bpf_u_int32 localnet, netmask;
	struct bpf_program fcode;
	char ebuf[PCAP_ERRBUF_SIZE];
#ifndef _WIN32
	struct sigaction action;
#endif
	int dlt;
	const char *dlt_name = NULL;
	int status;
	pcap_dumper_t *pdd;

	device = NULL;
	if ((cp = strrchr(argv[0], '/')) != NULL)
		program_name = cp + 1;
	else
		program_name = argv[0];

	opterr = 0;
	while ((op = getopt(argc, argv, COMMAND_OPTIONS)) != -1) {
		switch (op) {

		case 'L':
			show_dlt_types = 1;
			break;

		case 'i':
			device = optarg;
			break;

		case 'w':
			savefile = optarg;
			break;

		case 'y':
			dlt_name = optarg;
			break;

		default:
			usage();
			/* NOTREACHED */
		}
	}

	if (device == NULL) {
		if (pcap_findalldevs(&devlist, ebuf) == -1)
			error("%s", ebuf);
		if (devlist == NULL)
			error("no interfaces available for capture");
		device = strdup(devlist->name);
		pcap_freealldevs(devlist);
	}
	if (show_dlt_types) {
		pd = pcap_create(device, ebuf);
		if (pd == NULL)
			error("%s", ebuf);
		status = pcap_activate(pd);
		if (status < 0) {
			/*
			 * pcap_activate() failed.
			 */
			error("%s: %s\n(%s)", device,
			    pcap_statustostr(status), pcap_geterr(pd));
		}
		ndlts = pcap_list_datalinks(pd, &dlts);
		if (ndlts < 0) {
			/*
			 * pcap_list_datalinks() failed.
			 */
			error("%s: %s\n(%s)", device,
			    pcap_statustostr(status), pcap_geterr(pd));
		}
		for (int i = 0; i < ndlts; i++) {
			dlt_name = pcap_datalink_val_to_name(dlts[i]);
			if (dlt_name == NULL)
				printf("DLT %d", dlts[i]);
			else
				printf("%s", dlt_name);
			printf("\n");
		}
		pcap_free_datalinks(dlts);
		pcap_close(pd);
		return 0;
	}
		
	if (savefile == NULL)
		error("no savefile specified");

	*ebuf = '\0';

	pd = pcap_create(device, ebuf);
	if (pd == NULL)
		error("%s", ebuf);
	status = pcap_set_snaplen(pd, 262144);
	if (status != 0)
		error("%s: pcap_set_snaplen failed: %s",
			    device, pcap_statustostr(status));
	status = pcap_set_timeout(pd, 100);
	if (status != 0)
		error("%s: pcap_set_timeout failed: %s",
		    device, pcap_statustostr(status));
	status = pcap_activate(pd);
	if (status < 0) {
		/*
		 * pcap_activate() failed.
		 */
		error("%s: %s\n(%s)", device,
		    pcap_statustostr(status), pcap_geterr(pd));
	} else if (status > 0) {
		/*
		 * pcap_activate() succeeded, but it's warning us
		 * of a problem it had.
		 */
		warning("%s: %s\n(%s)", device,
		    pcap_statustostr(status), pcap_geterr(pd));
	}
	if (pcap_lookupnet(device, &localnet, &netmask, ebuf) < 0) {
		localnet = 0;
		netmask = 0;
		warning("%s", ebuf);
	}

	if (dlt_name != NULL) {
		dlt = pcap_datalink_name_to_val(dlt_name);
		if (dlt == PCAP_ERROR)
			error("%s isn't a valid DLT name", dlt_name);
		if (pcap_set_datalink(pd, dlt) == PCAP_ERROR)
			error("%s: %s", device, pcap_geterr(pd));
	}

	cmdbuf = copy_argv(&argv[optind]);

	if (pcap_compile(pd, &fcode, cmdbuf, 1, netmask) < 0)
		error("%s", pcap_geterr(pd));

	if (pcap_setfilter(pd, &fcode) < 0)
		error("%s", pcap_geterr(pd));

	pdd = pcap_dump_open(pd, savefile);
	if (pdd == NULL)
		error("%s", pcap_geterr(pd));

#ifdef _WIN32
	SetConsoleCtrlHandler(stop_capture, TRUE);
#else
	action.sa_handler = stop_capture;
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	if (sigaction(SIGINT, &action, NULL) == -1)
		error("Can't catch SIGINT: %s\n", strerror(errno));
#endif

	printf("Listening on %s, link-type ", device);
	dlt = pcap_datalink(pd);
	dlt_name = pcap_datalink_val_to_name(dlt);
	if (dlt_name == NULL)
		printf("DLT %d", dlt);
	else
		printf("%s", dlt_name);
	printf("\n");
	for (;;) {
		status = pcap_dispatch(pd, -1, pcap_dump, (u_char *)pdd);
		if (status < 0)
			break;
		if (status != 0) {
			printf("%d packets seen\n", status);
			struct pcap_stat ps;
			pcap_stats(pd, &ps);
			printf("%d ps_recv, %d ps_drop, %d ps_ifdrop\n",
			    ps.ps_recv, ps.ps_drop, ps.ps_ifdrop);
		}
	}
	if (status == -2) {
		/*
		 * We got interrupted, so perhaps we didn't
		 * manage to finish a line we were printing.
		 * Print an extra newline, just in case.
		 */
		putchar('\n');
		printf("Broken out of loop from SIGINT handler\n");
	}
	(void)fflush(stdout);
	if (status == -1) {
		/*
		 * Error.  Report it.
		 */
		(void)fprintf(stderr, "%s: pcap_dispatch: %s\n",
		    program_name, pcap_geterr(pd));
	}
	pcap_close(pd);
	pcap_freecode(&fcode);
	free(cmdbuf);
	exit(status == -1 ? 1 : 0);
}

static void
usage(void)
{
	(void)fprintf(stderr, "Usage: %s -L [ -i interface ] [ -w file ] [ -y dlt ] [expression]\n",
	    program_name);
	exit(1);
}

/* VARARGS */
static void
error(const char *fmt, ...)
{
	va_list ap;

	(void)fprintf(stderr, "%s: ", program_name);
	va_start(ap, fmt);
	(void)vfprintf(stderr, fmt, ap);
	va_end(ap);
	if (*fmt) {
		fmt += strlen(fmt);
		if (fmt[-1] != '\n')
			(void)fputc('\n', stderr);
	}
	exit(1);
	/* NOTREACHED */
}

/* VARARGS */
static void
warning(const char *fmt, ...)
{
	va_list ap;

	(void)fprintf(stderr, "%s: WARNING: ", program_name);
	va_start(ap, fmt);
	(void)vfprintf(stderr, fmt, ap);
	va_end(ap);
	if (*fmt) {
		fmt += strlen(fmt);
		if (fmt[-1] != '\n')
			(void)fputc('\n', stderr);
	}
}

/*
 * Copy arg vector into a new buffer, concatenating arguments with spaces.
 */
static char *
copy_argv(register char **argv)
{
	register char **p;
	register size_t len = 0;
	char *buf;
	char *src, *dst;

	p = argv;
	if (*p == 0)
		return 0;

	while (*p)
		len += strlen(*p++) + 1;

	buf = (char *)malloc(len);
	if (buf == NULL)
		error("copy_argv: malloc");

	p = argv;
	dst = buf;
	while ((src = *p++) != NULL) {
		while ((*dst++ = *src++) != '\0')
			;
		dst[-1] = ' ';
	}
	dst[-1] = '\0';

	return buf;
}

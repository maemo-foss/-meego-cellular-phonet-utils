/*
 * FBUS line discipline userland
 *
 * This file is part of phonet-utils
 *
 * Copyright (C) 2009 Nokia Corporation and/or its subsidiary(-ies).
 *
 * Contact: Rémi Denis-Courmont <remi.denis-courmont@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <stdio.h>
#include <errno.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>

static void usage (const char *path)
{
	printf ("Usage: %s [tty path]\n"
	        "Runs the FBUS protocol on a given terminal.\n", path);
}

static void version (void)
{
	puts ("Nokia FBUS line discipline\n");
}

int main (int argc, char *argv[])
{
	const char *msg;
	int fd, null;

	for (;;)
	{
		int c = getopt (argc, argv, "hV");

		if (c == -1)
			break;
		switch (c)
		{
		case 'h':
			usage (argv[0]);
			return 0;
		case 'V':
			version ();
			return 0;
		case '?':
			usage (argv[0]);
			return 2;
		}
	}

	if (optind < argc)
	{
		msg = argv[optind++];
		fd = open (msg, O_RDWR);
	}
	else
	{
		msg = "stdin";
		fd = dup (0);
	}
	if (fd == -1)
	{
		perror (msg);
		return 1;
	}

	if (!isatty (fd))
	{
		errno = ENOTTY;
		goto error;
	}
	else
	{
		struct termios p;

		if (tcgetattr (fd, &p))
			goto error;
		cfsetispeed (&p, B115200);
		cfsetospeed (&p, B115200);
		cfmakeraw (&p);
		if (tcsetattr (fd, TCSADRAIN, &p))
			goto error;
	}

	null = open ("/dev/null", O_WRONLY);
	if (null == -1)
	{
		msg = "/dev/null";
		goto error;
	}
	dup2 (null, 0);
	dup2 (null, 1);
	setsid ();

	if (ioctl (fd, TIOCSETD, &(int){ N_FBUS }))
		goto error;
	dup2 (null, 2);
	close (null);

	struct pollfd ufd = { .fd = fd, .events = 0, };
	while (poll (&ufd, 1, -1) == -1 || !ufd.revents);

	return 0;
error:
	perror (msg);
	close (fd);
	return 1;
}

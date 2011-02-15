/**
 * Phonet socket list
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

#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <search.h>
#include <sys/types.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <netinet/tcp.h>

struct fd
{
	unsigned long inode;
	unsigned long pid;
};

static int inocmp (const void *a, const void *b)
{
	const struct fd *fa = a, *fb = b;
	return fa->inode - fb->inode;
}

/* Load /proc/PID/fd socket inodes into the tree */
static int load_process (pid_t pid, void **proot)
{
	char path[16];
	snprintf (path, sizeof (path), "/proc/%d/fd", (int)pid);

	DIR *dir = opendir (path);
	if (dir == NULL)
		return -1;

	for (;;)
	{
		struct dirent *ent = readdir (dir);
		if (ent == NULL)
			break;
		if (ent->d_type != DT_LNK)
			continue; /* Uh? */

		char buf[PATH_MAX+1];
		ssize_t len = readlinkat (dirfd (dir), ent->d_name,
		                          buf, sizeof (buf) - 1);
		if (len == -1)
			continue;
		buf[len] = '\0';

		unsigned long inode;
		if (sscanf (buf, "socket:[%lu", &inode) != 1)
			continue; /* Not a socket */

		struct fd *node = malloc (sizeof (*node));
		if (node == NULL)
			break;
		node->inode = inode;
		node->pid = pid;

		/* Insert into the tree */
		struct fd ** res = tsearch (node, proot, inocmp);
		/* Duplicate entry (socket with multiple handles) */
		if (*res != node)
			free (node);
	}
	closedir (dir);
	return 0;
}

#define LINELEN 128

static void print_socket(const char *line, void *const *proot)
{
	unsigned long ino;
	unsigned pt, loc, res, st, wmem, rmem;

	if (sscanf (line, "%d %x:%*x:%x %x %x:%x %*d %lu",
	            &pt, &loc, &res, &st, &wmem, &rmem, &ino) != 7)
	{
		fprintf (stderr, "Cannot parse line:\n%s\n", line);
		return;
	}

	const char *proto = "?";
	switch (pt)
	{
		case 1: proto = "PN"; break;
		case 2: proto = "PEP"; break;
	}

	printf ("%5s %6u %6u   %04X:  %04X:", proto, rmem, wmem, loc, 0);
	if (res != 0)
		printf (" %02X", res);
	else
		fputs (" --", stdout);

	const char *state = "?";
	switch (st)
	{
		case TCP_ESTABLISHED: state = "ESTABLISHED"; break;
		case TCP_SYN_SENT: state = "SYN_SENT"; break;
		case TCP_SYN_RECV: state = "SYN_RECV"; break;
		case TCP_FIN_WAIT1: state = "FIN_WAIT1"; break;
		case TCP_FIN_WAIT2: state = "FIN_WAIT2"; break;
		case TCP_TIME_WAIT: state = "TIME_WAIT"; break;
		case TCP_CLOSE: state = "CLOSE"; break;
		case TCP_CLOSE_WAIT: state = "CLOSE_WAIT"; break;
		case TCP_LAST_ACK: state = "LAST_ACK"; break;
		case TCP_LISTEN: state = "LISTEN"; break;
		case TCP_CLOSING: state = "CLOSING"; break;
	}
	printf (" %-11s ", state);

	struct fd **node = tfind(&ino, proot, inocmp);
	if (node)
	{
		char exe[PATH_MAX + 1], path[PATH_MAX + 1], *name;
		ssize_t len;

		snprintf (path, sizeof (path), "/proc/%lu/exe", (*node)->pid);
		len = readlink (path, exe, sizeof (exe) - 1);
		if (len == -1)
			len = 0;
		exe[len] = '\0';
		name = strrchr (exe, '/');
		if (name == NULL)
			name = exe;
		else
			name++;
		printf ("%5lu/%s", (*node)->pid, name);
	}
	fputc ('\n', stdout);
}


static int pnstat (void)
{
	void *root = NULL;

	DIR *proc_dir = opendir ("/proc");
	if (proc_dir == NULL)
	{
		perror ("/proc");
		return -1;
	}

	for (;;)
	{
		struct dirent *ent = readdir (proc_dir);
		if (ent == NULL)
			break; /* End */
		if (ent->d_type != DT_DIR)
			continue; /* Not a directory */

		char *end;
		unsigned long pid = strtoul (ent->d_name, &end, 10);
		if (*end)
			continue; /* Not a PID number */
		load_process (pid, &root);
	}
	closedir (proc_dir);

	puts ("Active Phonet connections");
	puts ("Proto Recv-Q Send-Q Local  Remote Res State       "
	      "PID/Program");

	int fd = open ("/proc/net/phonet", O_RDONLY);
	if (fd == -1)
	{
		perror ("/proc/net/phonet");
		return -1;
	}
	lseek (fd, LINELEN, SEEK_SET);

	char buf[LINELEN + 1];
	buf[sizeof (buf) - 1] = '\0';
	while (read (fd, buf, sizeof (buf) - 1) == (sizeof (buf) - 1))
		print_socket (buf, &root);

	close (fd);
	tdestroy (root, free);
	return 0;
}

int main (void)
{
	return -pnstat ();
}

/**
 * Phonet router configuration
 *
 * This file is part of phonet-utils
 *
 * Copyright (C) 2007-2009 Nokia Corporation and/or its subsidiary(-ies).
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
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <net/if.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/phonet.h>

static int usage (const char *path)
{
	printf ("Usage: %s\n"
	        "       %s add <destination> <device>\n"
	        "       %s del <destination> <device>\n"
	        "Lists, adds or removes a Phonet route.\n", path, path, path);
	return 2;
}

static void print_route (const struct nlmsghdr *nlh)
{
	const struct rtmsg *rtm = NLMSG_DATA(nlh);
	int len = RTM_PAYLOAD(nlh);

	if (rtm->rtm_family != AF_PHONET)
		return; /* silly kernel falls back if Phonet is absent */

	char ifname[IFNAMSIZ] = "";
	uint8_t dst = 0xFF;

	for (const struct rtattr *rta = RTM_RTA (rtm);
	     RTA_OK (rta, len); rta = RTA_NEXT (rta, len))
	{
		switch (rta->rta_type)
		{
		case RTA_DST:
			memcpy (&dst, RTA_DATA (rta), sizeof (dst));
			break;
		case RTA_OIF:
			if_indextoname (*(uint32_t *)RTA_DATA (rta), ifname);
			break;
		}
	}

	printf (" %02"PRIX8" %s\n", dst, ifname);
}

int main (int argc, char **argv)
{
	struct {
		struct nlmsghdr nlh;
		struct rtmsg rtm;
		char buf[1024];
	} req;
	size_t buflen = sizeof (req.buf);

	//req.nlh.nlmsg_type = XXX;
	req.nlh.nlmsg_len = NLMSG_LENGTH (sizeof (req.rtm));
	req.nlh.nlmsg_flags = NLM_F_REQUEST;
	req.nlh.nlmsg_seq = 0;
	req.nlh.nlmsg_pid = getpid ();

	req.rtm.rtm_family = AF_PHONET;
	req.rtm.rtm_dst_len = 6;
	req.rtm.rtm_src_len = 0;
	req.rtm.rtm_tos = 0;

	req.rtm.rtm_table = RT_TABLE_MAIN;
	req.rtm.rtm_protocol = RTPROT_STATIC;
	req.rtm.rtm_scope = RT_SCOPE_UNIVERSE;
	req.rtm.rtm_type = RTN_UNICAST;
	req.rtm.rtm_flags = 0;

	if (argc <= 1)
	{
		req.nlh.nlmsg_type = RTM_GETROUTE;
		req.nlh.nlmsg_flags |= NLM_F_ROOT;
	}
	else
	if (argc != 4)
		return usage (argv[0]);
	else
	{
		struct rtattr *rta;

		if (strcmp (argv[1], "add") == 0)
			req.nlh.nlmsg_type = RTM_NEWROUTE;
		else
		if (strcmp (argv[1], "del") == 0)
			req.nlh.nlmsg_type = RTM_DELROUTE;
		else
			return usage (argv[0]);

		char *end;
		unsigned long dst = strtoul (argv[2], &end, 0);
		if (dst >= 256 || (dst & 3) || *end)
		{
			errno = EINVAL;
			perror (argv[2]);
			return 2;
		}

		rta = RTM_RTA (&req.rtm);
		rta->rta_len = RTA_LENGTH (sizeof (uint8_t));
		rta->rta_type = RTA_DST;
		memcpy(RTA_DATA(rta), &(uint8_t){ dst }, sizeof (uint8_t));
		req.nlh.nlmsg_len += RTA_LENGTH (rta->rta_len);
		req.nlh.nlmsg_flags |= NLM_F_ACK;

		uint32_t ifindex = if_nametoindex (argv[3]);
		if (ifindex == 0)
		{
			errno = ENODEV;
			perror (argv[3]);
			return 2;
		}

		rta = RTA_NEXT (rta, buflen);
		rta->rta_len = RTA_LENGTH (sizeof (ifindex));
		rta->rta_type = RTA_OIF;
		memcpy(RTA_DATA(rta), &ifindex, sizeof (ifindex));
		req.nlh.nlmsg_len += RTA_LENGTH (rta->rta_len);
	}

	int fd = socket (PF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	if (fd == -1)
	{
		perror ("Netlink socket error");
		return 1;
	}

	struct sockaddr_nl addr = { .nl_family = AF_NETLINK, };

	if (sendto (fd, &req, req.nlh.nlmsg_len, 0,
	            (struct sockaddr *)&addr, sizeof (addr)) == -1)
	{
		perror ("Netlink send error");
		close (fd);
		return 1;
	}

	for (;;) {
		struct iovec iov = { &req, sizeof(req), };
		struct msghdr msg = { .msg_iov = &iov, .msg_iovlen = 1, };

		ssize_t ret = recvmsg (fd, &msg, 0);
		if (ret == 0)
			break;
		if (ret == -1)
		{
			if (errno == EINTR)
				continue;
			perror ("Netlink receive error");
			goto err;
		}

		if (msg.msg_flags & MSG_TRUNC)
		{
			errno = EMSGSIZE;
			perror ("Netlink receive error");
			goto err;
		}

		for (struct nlmsghdr *nlh = (struct nlmsghdr *)&req;
		     NLMSG_OK (nlh, (size_t)ret); nlh = NLMSG_NEXT (nlh, ret))
		{
			if (nlh->nlmsg_type == NLMSG_DONE)
				goto out;

			if (nlh->nlmsg_type == NLMSG_ERROR)
			{
				const struct nlmsgerr *err;
				err = (struct nlmsgerr *)NLMSG_DATA (nlh);

				if (err->error)
				{
					errno = -err->error;
					perror ("Netlink error");
					goto err;
				}
				goto out;
			}

			if (nlh->nlmsg_type == RTM_NEWROUTE)
				print_route (nlh);
		}
	}

out:
	close (fd);
	return 0;

err:
	close (fd);
	return 1;
}


/*
 * This file is part of phonet-utils
 *
 * Copyright (C) 2007-2009 Nokia Corporation and/or its subsidiary(-ies).
 *
 * Contact: Andras Domokos <andras.domokos@nokia.com>
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
#include <inttypes.h>
#include <string.h>
#include <getopt.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <time.h>
#include <net/if.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/phonet.h>

#define NOFLAGS         0
#define DONT_CARE       0
#define SBZ             0
#define NOT_SET         0xff
#define SEND_FLAGS      NOFLAGS
#define MSGBUF_SIZE     2048

static const struct option opt_tbl[] = {
    { "addr-add", required_argument, NULL, 'a' },
    { "addr-lst", no_argument,       NULL, 'l' },
    { "addr-del", required_argument, NULL, 'd' },
    { "dev",      no_argument,       NULL, 'i' },
    { "help",     required_argument, NULL, 'h' },
    { NULL,       0,                 NULL, 0   }
};

static void usage(const char *path, int val)
{
    printf("Usage: %s <-a|--addr-add ADDR> | <-l|--addr-lst> | "
           "<-d|--addr-del ADDR>  <-i|--dev DEVICE>\n", path);
    exit(val);
}

int main(int argc, char **argv)
{
    int fd;
    unsigned ifa_index = 0;
    uint8_t pn_address;
    struct rtattr *rta;
    int bufsize = MSGBUF_SIZE;
    struct sockaddr_nl local_sa;
    struct sockaddr_nl other_sa;
    struct ifaddrmsg *ifa;
    uint32_t sequence = time(NULL);
    int exit_loop = 0;
    int display_info = 0;

    struct {
        struct nlmsghdr nlh;
        char buf[MSGBUF_SIZE];
    } nlreq;

    memset(&nlreq, 0, sizeof(nlreq));
    nlreq.nlh.nlmsg_flags = NLM_F_REQUEST;
    nlreq.nlh.nlmsg_type = NOT_SET;
    nlreq.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifaddrmsg));

    ifa = (struct ifaddrmsg *)NLMSG_DATA(&nlreq.nlh);
    ifa->ifa_family = AF_PHONET;
    ifa->ifa_prefixlen = 0;

    while (1) {
        int ret;

        ret = getopt_long(argc, argv, "a:ld:fi:h", opt_tbl, NULL);
        if (ret == -1)
            break;

        switch (ret) {
            case 'a':
            case 'd':

                if (sscanf(optarg, "%"SCNx8, &pn_address) != 1
                 || pn_address & 3) {
                    fprintf(stderr, "%s: invalid address `%s'\n",
                            argv[0], optarg);
                    exit(1);
                }

                if (ret == 'a')
                    nlreq.nlh.nlmsg_type = RTM_NEWADDR;
                else
                    nlreq.nlh.nlmsg_type = RTM_DELADDR;
                nlreq.nlh.nlmsg_flags |= NLM_F_ACK;

                rta = IFA_RTA(ifa);
                rta->rta_type = IFA_LOCAL;
                rta->rta_len = RTA_LENGTH(1);
                memcpy(RTA_DATA(rta), &pn_address, 1);
                nlreq.nlh.nlmsg_len = NLMSG_ALIGN(nlreq.nlh.nlmsg_len)
                                      +  RTA_LENGTH(1);
                break;

            case 'l':
                nlreq.nlh.nlmsg_type = RTM_GETADDR;
                nlreq.nlh.nlmsg_flags |= NLM_F_ROOT | NLM_F_MATCH;

                display_info = 1;
                break;

            case 'i':
	        if ((ifa_index = if_nametoindex (optarg)) == 0) {
                    fprintf(stderr, "%s: interface `%s' not found\n",
                            argv[0], optarg);
                    exit(1);
		}
                break;

            case 'h':
                usage(argv[0], 0);
                break;

            default:
                usage(argv[0], 1);
        }
    }

    if (nlreq.nlh.nlmsg_type == NOT_SET)
        usage(argv[0], 1);

    if ((ifa->ifa_index = ifa_index) == 0)
        usage(argv[0], 1);

    fd = socket(PF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (fd == -1) {
        perror("Netlink socket error");
        exit(1);
    }

    if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(bufsize))) {
        perror("SO_SNDBUF socket option error");
        exit(1);
    }

    if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize))) {
        perror("SO_RCVBUF socket option error");
        exit(1);
    }

    memset(&local_sa, 0, sizeof(local_sa));
    local_sa.nl_family = AF_NETLINK;
    //local_sa.nl_pid = 0;
    //local_sa.nl_groups = 0;

    if (bind(fd, (struct sockaddr *)&local_sa, sizeof(local_sa))) {
        perror("Socket bind error");
        exit(1);
    }

    memset(&other_sa, 0, sizeof(other_sa));
    other_sa.nl_family = AF_NETLINK;
    //other_sa.nl_pid = 0;
    //other_sa.nl_groups = 0;

    nlreq.nlh.nlmsg_seq = sequence;

    if (sendto(fd, &nlreq, nlreq.nlh.nlmsg_len, 0,
               (struct sockaddr *)&other_sa, sizeof(other_sa)) == -1) {
        perror("Socket msg send error");
        exit(1);
    }

    memset(nlreq.buf, 0, sizeof(nlreq.buf));

    while (!exit_loop) {
        struct iovec iov = { nlreq.buf, sizeof(nlreq.buf) };
        struct msghdr msg = {
            .msg_iov = &iov,
            .msg_iovlen = 1,
        };
        struct nlmsghdr *nlh;
        ssize_t ret;

        ret = recvmsg(fd, &msg, 0);
        if (ret == 0)
            break;
        if (ret == -1) {
            if (errno == EINTR)
                continue;
            perror("Socket msg receive error");
            exit(1);
        }

        if (msg.msg_flags & MSG_TRUNC) {
            fputs("Truncated netlink message received "
                  "(receive buffer too small?)\n", stderr);
            exit(1);
        }

        for (nlh = (struct nlmsghdr *)nlreq.buf;
             NLMSG_OK(nlh, (size_t)ret);
             nlh = NLMSG_NEXT(nlh, ret)) {

            if (nlh->nlmsg_type == NLMSG_DONE) {
                exit_loop = 1;
                break;
            }

            if (nlh->nlmsg_type == NLMSG_ERROR) {
                struct nlmsgerr *err = (struct nlmsgerr *)NLMSG_DATA(nlh);

                if (err->error) {
                    errno = -err->error;
                    perror("Netlink error");
                    exit(1);
                }
                exit_loop = 1;
                break;
            }

            if (!display_info)
                continue;

            if (nlh->nlmsg_type == RTM_NEWADDR) {
                ifa = (struct ifaddrmsg *)NLMSG_DATA(nlh);
                if (ifa->ifa_index == ifa_index) {
                    rta = IFA_RTA(ifa);
                    if (rta->rta_type == IFA_LOCAL) {
                        memcpy(&pn_address, RTA_DATA(rta), 1);
                        printf("  phonet addr: %02"PRIx8"\n", pn_address);
                    }
                }
            }
        }
    }

    close(fd);
    return 0;
}


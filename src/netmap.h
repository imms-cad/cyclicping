/******************************************************************************
* Copyright (C) 2016-2017 IMMS GmbH, Thomas Elste <thomas.elste@imms.de>

* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.

* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.

* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
* USA.
******************************************************************************/

#ifndef __NETMAP_H__
#define __NETMAP_H__

#include <poll.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <net/ethernet.h>
#include <netinet/in.h>

#define NETMAP_WITH_LIBS
#include <net/netmap_user.h>

#define MAX_DEV_LEN	32

enum recv_code {
	NETMAP_RECV_OK=0,
	NETMAP_RECV_TIMEOUT,
	NETMAP_RECV_NOPACKET,
	NETMAP_RECV_ERROR
};

struct pkt {
        struct ether_header eh;
        struct ip ip;
        struct udphdr udp;
} __attribute__((__packed__));

struct netmap_cfg {
	char nm_device[MAX_DEV_LEN];
	char *device;
	struct ether_addr dest_hwaddr;
	struct sockaddr_in dest_addr;
	struct sockaddr_in local_addr;
	int port;
	int fd;
	struct pollfd poll_fds;
	struct nm_desc *nmd;
	struct pollfd fds;
	struct netmap_ring *nmtxring;
	struct pkt out_pkt_header;
	struct pkt in_pkt_header;
};

int netmap_init(struct cyclicping_cfg *cfg, char **argv, int argc);
int netmap_client(struct cyclicping_cfg *cfg);
int netmap_server(struct cyclicping_cfg *cfg);
void netmap_deinit(struct cyclicping_cfg *cfg);
void netmap_usage(void);

#endif

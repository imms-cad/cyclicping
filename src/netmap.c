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

#include <stdio.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <inttypes.h>
#include <netinet/ether.h>

#include <sys/select.h>

#include <cyclicping.h>
#include <opts.h>
#include <stats.h>
#include <socket.h>
#include <netmap.h>

extern int run;
extern int abort_fd;

/**
 * Compute the checksum of the given ip header.
 *
 * \param data Data to checksum.
 * \param len Length of data.
 * \param sum Current sum value.
 * \return New checksum.
 */
static uint16_t checksum(const void *data, uint16_t len, uint32_t sum)
{
	const uint8_t *addr = data;
	uint32_t i;

	/* Checksum all the pairs of bytes first... */
	for (i = 0; i < (len & ~1U); i += 2) {
		sum += (u_int16_t)ntohs(*((u_int16_t *)(addr + i)));
		if (sum > 0xFFFF)
			sum -= 0xFFFF;
	}
	/*
	 * If there's a single byte left over, checksum it, too.
	 * Network byte order is big-endian, so the remaining byte is
	 * the high byte.
	 */
	if (i < len) {
		sum += addr[i] << 8;
		if (sum > 0xFFFF)
			sum -= 0xFFFF;
	}
	return sum;
}

/**
 * Wraps checksum
 *
 * \param sum Sum to wrap.
 * \return Wrapped sum.
 */
static u_int16_t wrapsum(u_int32_t sum)
{
	sum = ~sum & 0xFFFF;
	return (htons(sum));
}

/**
 * Get IP and MAC address of the network interface and store them in the
 * packet header of the outgoing packet.
 *
 * \param cfg Cyclicping config data.
 * \return 0 on success.
 */
int interface_info(struct cyclicping_cfg *cfg)
{
	struct netmap_cfg *ucfg=cfg->current_mod->modcfg;
	struct pkt *pkt=&ucfg->out_pkt_header;
	struct sockaddr *hwaddr;
	struct sockaddr_in *inaddr;
	int fd;
	struct ifreq ifr;

	fd = socket(AF_INET, SOCK_DGRAM, 0);

	/* set protocol family and interface name for ioctl request */
	ifr.ifr_addr.sa_family = AF_INET;
	strncpy(ifr.ifr_name, ucfg->device, IFNAMSIZ-1);

	/* request ip address if the interface */
	if(ioctl(fd, SIOCGIFADDR, &ifr)!=0) {
		fprintf(stderr, "failed to retrieve interface address for %s\n",
			ucfg->device);
		return 1;
	}

	inaddr=(struct sockaddr_in*)&ifr.ifr_addr;
	memcpy(&pkt->ip.ip_src, &inaddr->sin_addr, sizeof(struct in_addr));

	/* request hardware address */
	if(ioctl(fd, SIOCGIFHWADDR, &ifr)!=0) {
		fprintf(stderr, "failed to retrieve interface hw address\n");
		return 1;
	}

	hwaddr=(struct sockaddr*)&ifr.ifr_addr;
	memcpy(&pkt->eh.ether_shost, hwaddr->sa_data, ETH_ALEN);
	close(fd);

	return 0;
}

/**
 * Initializes some basic packet data in the ethernet, ip and udp header
 * of the outgoing packet.
 *
 * \param cfg Cyclicping config data.
 * \return 0 on success.
 */
void initialize_packet(struct cyclicping_cfg *cfg)
{
	struct netmap_cfg *ucfg=cfg->current_mod->modcfg;
	struct pkt *pkt=&ucfg->out_pkt_header;
	struct ether_header *eh;
	struct ip *ip;
	struct udphdr *udp;

	/* prepare the headers */
	ip = &pkt->ip;
	ip->ip_v = IPVERSION;
	ip->ip_hl = 5;
	ip->ip_id = 0;
	ip->ip_tos = IPTOS_LOWDELAY;
	ip->ip_len = ntohs(cfg->opts.length
		+ sizeof(struct udphdr) + sizeof(*ip));
	ip->ip_id = 0;
	ip->ip_off = htons(IP_DF); /* Don't fragment */
	ip->ip_ttl = IPDEFTTL;
	ip->ip_p = IPPROTO_UDP;
	ip->ip_dst.s_addr = ucfg->dest_addr.sin_addr.s_addr;
	ip->ip_sum = wrapsum(checksum(ip, sizeof(*ip), 0));

	udp = &pkt->udp;
	udp->uh_sport = htons(ucfg->port);
	udp->uh_dport = htons(ucfg->port);
	udp->uh_ulen = htons(cfg->opts.length + sizeof(struct udphdr));

	eh = &pkt->eh;
	memcpy(eh->ether_dhost, &ucfg->dest_hwaddr, sizeof(struct ether_addr));
	eh->ether_type = htons(ETHERTYPE_IP);
}

/**
 * Init Netmap connection module. Parse module args. Open netmap descriptor.
 * Init packet.
 *
 * \param cfg Cyclicping config data.
 * \param argv Interface module arguments.
 * \param argc Interface module count.
 * \return 0 on success.
 */
int netmap_init(struct cyclicping_cfg *cfg, char **argv, int argc)
{
	struct netmap_cfg *ucfg;
	int port_arg_idx=2;
	int i;

	ucfg=(struct netmap_cfg*)calloc(1, sizeof(struct netmap_cfg));
	if(ucfg==NULL) {
		perror("failed to allocate memory for netmap cfg");
		return 1;
	}

	cfg->current_mod->modcfg=ucfg;

	if(argc<2) {
		fprintf(stderr, "interface name requiered for netmap mode\n");
		return 1;
	}

	/* Store device name which comes from first arg. The netmap device
	 * name just prepend this with "netmap:". */
	strcpy(ucfg->nm_device, "netmap:");
	ucfg->device=ucfg->nm_device+strlen(ucfg->nm_device);
	strncpy(ucfg->device, argv[1], IFNAMSIZ);

	if(cfg->opts.client) {
		int i;

		if(argc<4) {
			fprintf(stderr, "ip and hw address required for "
				"netmap client mode\n");
			return 1;
		}

		/* prepare hw address */
		for(i=0; i<strlen(argv[2]); i++)
			if(argv[2][i]=='-')
				argv[2][i]=':';

		if(ether_aton_r(argv[2], &ucfg->dest_hwaddr)==NULL) {
			perror("failed to convert destination hw address");
			return 1;
		}

		/* convert destination address */
		if(inet_aton(argv[3], &ucfg->dest_addr.sin_addr)==0) {
			perror("failed to convert destination address");
			return 1;
		}
		port_arg_idx=4;
	}

	if(argc>=port_arg_idx+1) {
		ucfg->port=-1;
		ucfg->port=atoi(argv[port_arg_idx]);
		if(ucfg->port<=0 || ucfg->port>0xffff) {
			fprintf(stderr, "invalid port number for netmap "
				"destination port\n");
			return 1;
		}
	} else {
		ucfg->port=DEFAULT_PORT;
	}

	if(interface_info(cfg))
		return 1;

	initialize_packet(cfg);

	/* open the netmap device descriptor */
	ucfg->nmd=nm_open(ucfg->nm_device, NULL, 0, 0);
	if(ucfg->nmd==NULL) {
		fprintf(stderr, "failed to open netmap device\n");
		return 1;
	}

	/* get common fd and tx ring reference */
	ucfg->fd=NETMAP_FD(ucfg->nmd);
	ucfg->poll_fds.fd=ucfg->fd;
	abort_fd=ucfg->fd;
	ucfg->nmtxring=
		NETMAP_TXRING(ucfg->nmd->nifp, ucfg->nmd->first_tx_ring);

	if(!cfg->opts.quiet) {
		printf("waiting 5 seconds for netmap device to become ready ");
		fflush(stdout);
		for(i=0; i<5; i++) {
			printf(". ");
			fflush(stdout);
			sleep(1);
		}
		printf("\n");
	} else {
		sleep(5);
	}

	return 0;
}

/**
 * Send out packet via the netmap tx ring. Waits for ring to become ready and
 * takes and copy timestamp to packet.
 *
 * \param cfg Cyclicping config data.
 * \param server 1 if we are running in server mode, else 0.
 * \param tsend Sending timestamp gets stored here.
 * \return 0 on success.
 */
int netmap_send_packet(struct cyclicping_cfg *cfg, int server,
	struct timespec *tsend)
{
	struct netmap_cfg *ucfg=cfg->current_mod->modcfg;
	char *payload=server?cfg->recv_packet:cfg->send_packet;
	struct netmap_slot *slot;
	char *nmbuffer;

	ucfg->poll_fds.events = POLLOUT;
	if(poll(&ucfg->poll_fds, 1, 2000) <= 0) {
		fprintf(stderr, "poll timeout waiting for pollout\n");
		return 1;
	}

	if(ucfg->poll_fds.events & POLLERR) {
		fprintf(stderr, "poll error\n");
		return 1;
	}

	/* take timestamp and copy it to packet */
	clock_gettime(cfg->opts.clock, tsend);
	if(server)
		tspec2buffer(tsend, payload+2*sizeof(uint64_t));
	else
		tspec2buffer(tsend, payload);

	/* Magic: taken from sbin/dhclient/packet.c */
#if 0
	udp->uh_sum = wrapsum(checksum(udp, sizeof(*udp),
		checksum(payload, cfg->opts.length - sizeof(*udp),
			checksum(&ip->ip_src, 2 * sizeof(ip->ip_src),
			    IPPROTO_UDP + (u_int32_t)ntohs(udp->uh_ulen)
				)
			)
		));
#endif

	/* send packet to server */
	slot=&ucfg->nmtxring->slot[ucfg->nmtxring->cur];
	nmbuffer=NETMAP_BUF(ucfg->nmtxring, slot->buf_idx);
	memcpy(nmbuffer, &ucfg->out_pkt_header, sizeof(struct pkt));
	memcpy(nmbuffer+sizeof(struct pkt), payload,
		cfg->opts.length);

	slot->len=sizeof(struct pkt)+cfg->opts.length;

	/* this starts the packet transmission */
	ucfg->nmtxring->head=ucfg->nmtxring->cur=
		nm_ring_next(ucfg->nmtxring, ucfg->nmtxring->cur);

	return 0;
}

/**
 * Wait for and receive incoming packets via the netmap rx ring. A receive
 * timestamp gets set.
 *
 * \param cfg Cyclicping config data.
 * \param timeout Timeout to wait for incoming packets.
 * \param tsend Receive timestamp gets stored here.
 * \return Receive error code.
 */
enum recv_code netmap_receive_packet(struct cyclicping_cfg *cfg, int timeout,
	struct timespec *trecv)
{
	struct netmap_cfg *ucfg=cfg->current_mod->modcfg;
	unsigned char *nmbuffer;
	struct nm_pkthdr header;
	struct pkt *tpkt;

	ucfg->poll_fds.events = POLLIN;

	if(poll(&ucfg->poll_fds, 1, timeout) <= 0) {
		fprintf(stderr, "poll timeout waiting for pollin\n");
		return NETMAP_RECV_TIMEOUT;
	}

	if(ucfg->poll_fds.events & POLLERR) {
		fprintf(stderr, "poll error\n");
		return NETMAP_RECV_ERROR;
	}

	while(1) {
		nmbuffer=nm_nextpkt(ucfg->nmd, &header);
		if(nmbuffer==NULL) {
			fprintf(stderr, "no packet found\n");
			return NETMAP_RECV_NOPACKET;
		}

		/* size doesn't match, not for us */
		if(header.len!=sizeof(struct pkt)+cfg->opts.length)
			continue;

		/* ports dont't match, still not for us */
		tpkt=(struct pkt*)nmbuffer;
		if(tpkt->udp.uh_dport != htons(ucfg->port))
			continue;

		clock_gettime(cfg->opts.clock, trecv);

		/* copy header and payload */
		memcpy(&ucfg->in_pkt_header, tpkt, sizeof(struct pkt));
		memcpy(cfg->recv_packet, nmbuffer+sizeof(struct pkt),
			cfg->opts.length);

		break;
	}

	return NETMAP_RECV_OK;
}

/**
 * Netmap client.
 *
 * \param cfg Cyclicping config data.
 * \return 0 on success, else 1.
 */
int netmap_client(struct cyclicping_cfg *cfg)
{
	struct timespec tsend, trecv, tserver;
	enum recv_code recv_ret;

	if(netmap_send_packet(cfg, 0, &tsend)!=0)
		return 1;

	/* receive until we get a valid packet or a timeout */
	do {
		recv_ret=netmap_receive_packet(cfg, 1000, &trecv);
	} while(recv_ret==NETMAP_RECV_NOPACKET);

	if(recv_ret!=NETMAP_RECV_OK)
		return 1;

	/* add packet time to statistics */
	add_stats(cfg, STAT_ALL, &tsend, &trecv);

	if(cfg->opts.two_way) {
		buffer2tspec(cfg->recv_packet+2*sizeof(uint64_t), &tserver);
		add_stats(cfg, STAT_SEND, &tsend, &tserver);
		add_stats(cfg, STAT_RECV, &tserver, &trecv);
	}

	print_stats(cfg, &tsend, &tserver, &trecv);

	/* wait until next inverval */
	return client_wait(cfg, tsend);
}

/**
 * Netmap server.
 *
 * \param cfg Cyclicping config data.
 * \return 0 on success, else 1.
 */
int netmap_server(struct cyclicping_cfg *cfg)
{
	struct netmap_cfg *ucfg=cfg->current_mod->modcfg;
	struct pkt *pkt=&ucfg->out_pkt_header;
	struct udphdr *udp=&pkt->udp;
	struct ip *ip=&pkt->ip;
	struct ether_header *eh=&pkt->eh;
	struct timespec tsend, trecv;
	enum recv_code recv_ret;

	recv_ret=netmap_receive_packet(cfg, -1, &trecv);

	/* packets received but none for us, just start over */
	if(recv_ret==NETMAP_RECV_NOPACKET)
		return 0;

	if(recv_ret==NETMAP_RECV_ERROR)
		return 1;

	/* make source ip addr of received paket to destination address
	 * of the packet to send */
	ip->ip_dst.s_addr = ucfg->in_pkt_header.ip.ip_src.s_addr;
	ip->ip_sum = wrapsum(checksum(ip, sizeof(*ip), 0));

	/* switch udp ports */
	udp->uh_sport = ucfg->in_pkt_header.udp.uh_dport;
	udp->uh_dport = ucfg->in_pkt_header.udp.uh_sport;

	/* copy source mac address of received packet */
	memcpy(eh->ether_dhost, ucfg->in_pkt_header.eh.ether_shost,
		sizeof(struct ether_addr));

	/* send out reply packet */
	if(netmap_send_packet(cfg, 1, &tsend)!=0)
		return 1;

	return 0;
}

/**
 * Ouput Netmap interface module usage.
 */
void netmap_usage(void)
{
	printf("  netmap - Use a Netmap connection\n");
	printf("    netmap:interface[:port]                    "
		"Netmap server\n");
	printf("    netmap:interface:servermac:serverip[:port] "
		"Netmap client\n");
	printf("    mac address has to use \"-\" as separator\n");
}

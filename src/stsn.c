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
#include <arpa/inet.h>
#include <linux/if.h>
#include <linux/if_ether.h>
#include <sys/ioctl.h>

#include <sys/select.h>

#include <cyclicping.h>
#include <opts.h>
#include <stats.h>
#include <socket.h>
#include <stsn.h>

extern int run;
extern int abort_fd;

/**
 * Init STSN connection module. Parse module args. Open socket. Set socket
 * priority.
 *
 * \param cfg Cyclicping config data.
 * \param argv Interface module arguments.
 * \param argc Interface module count.
 * \return 0 on success.
 */
int stsn_init(struct cyclicping_cfg *cfg, char **argv, int argc)
{
	struct stsn_cfg *scfg;
	struct ifreq req;
	uint8_t mac[8];

	scfg=(struct stsn_cfg*)calloc(1, sizeof(struct stsn_cfg));
	if(scfg==NULL) {
		perror("failed to allocate memory for stsn cfg");
		return 1;
	}

	cfg->current_mod->modcfg=scfg;

	if(argc<2) {
		fprintf(stderr, "interface and mac address required\n");
		return 1;
	}

	if(sscanf(argv[2], "%hhx-%hhx-%hhx-%hhx-%hhx-%hhx",
		&mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5])!=6) {
		fprintf(stderr, "failed to convert mac address\n");
		return 1;
	}
	memcpy(&scfg->sk_addr.sll_addr, mac, ETH_ALEN);

	scfg->socket=socket(AF_PACKET, SOCK_DGRAM, htons(ETH_P_TSN));
	if(scfg->socket<0) {
		fprintf(stderr, "failed to open socket\n");
		return 1;
	}

	strncpy(req.ifr_name, argv[1], sizeof(req.ifr_name));
	if(ioctl(scfg->socket, SIOCGIFINDEX, &req) < 0) {
		fprintf(stderr, "failed to get interface index\n");
		close(scfg->socket);
		return 0;
	}

	scfg->sk_addr.sll_ifindex = req.ifr_ifindex;
	scfg->sk_addr.sll_family = AF_PACKET;
	scfg->sk_addr.sll_protocol = htons(ETH_P_TSN);
	scfg->sk_addr.sll_halen = ETH_ALEN;

	if(set_socket_priority(scfg->socket, cfg->opts.sopriority)) {
		return 1;
	}

	abort_fd=scfg->socket;

	/* vendor specific stream */
	cfg->send_packet[0]=0x6f;
	/* type specific data, version, etc */
	cfg->send_packet[1]=0x0;
	cfg->send_packet[2]=0x0;
	cfg->send_packet[3]=0x0;

	return 0;
}

/**
 * TSN client.
 *
 * \param cfg Cyclicping config data.
 * \return 0 on success, else 1.
 */
int stsn_client(struct cyclicping_cfg *cfg)
{
	struct stsn_cfg *scfg=cfg->current_mod->modcfg;
	int selectResult;
	struct timespec tsend, trecv, tserver;
	struct timeval timeout;
	fd_set set;
	socklen_t dest_addr_len=sizeof(scfg->sk_addr);

	FD_ZERO(&set);
	FD_SET(scfg->socket, &set);

	timeout.tv_sec=1;
	timeout.tv_usec=0;

	/* take timestamp and copy it to send packet */
	clock_gettime(cfg->opts.clock, &tsend);
	tspec2buffer(&tsend, cfg->send_packet+4);

	/* send packet to server */
	if(sendto(scfg->socket, cfg->send_packet, cfg->opts.length, 0,
		(const struct sockaddr *)&scfg->sk_addr,
		dest_addr_len)==-1) {
		perror("stsn client failed to send packet");
		return 1;
	}

	do {
		/* monitor socket fd via select */
		selectResult = select(scfg->socket+1, &set, NULL, NULL,
			&timeout);
		if (selectResult > 0) {
			/* receive packet and take timestamp */
			if(recv(scfg->socket, cfg->recv_packet,
				cfg->opts.length, 0)==-1) {
				perror("stsn client failed to receive packet");
				return 1;
			}
			clock_gettime(cfg->opts.clock, &trecv);
		} else if(selectResult == 0) {
			fprintf(stderr,
				"stsn client timeout receiving packet\n");
			return 1;
		}
		else {
			fprintf(stderr, "stsn client select failed\n");
			return 1;
		}
	} while(cfg->recv_packet[0]!=0x6f);

	if(cfg->send_packet[3]!=cfg->recv_packet[3]) {
		fprintf(stderr, "sequence number missmatch\n");
		return 1;
	}

	/* add packet time to statistics */
	if(add_stats(cfg, STAT_ALL, &tsend, &trecv))
		return 1;

	if(cfg->opts.two_way) {
		buffer2tspec(cfg->recv_packet+2*sizeof(uint64_t)+4, &tserver);
		if(add_stats(cfg, STAT_SEND, &tsend, &tserver))
			return 1;
		if(add_stats(cfg, STAT_RECV, &tserver, &trecv))
			return 1;
	}

	print_stats(cfg, &tsend, &tserver, &trecv);

	cfg->send_packet[3]++;

	/* wait until next inverval */
	return client_wait(cfg, tsend);
}

/**
 * STSN server.
 *
 * \param cfg Cyclicping config data.
 * \return 0 on success, else 1.
 */
int stsn_server(struct cyclicping_cfg *cfg)
{
	struct stsn_cfg *scfg=cfg->current_mod->modcfg;
	struct timespec tsend;
	socklen_t dest_addr_len=sizeof(scfg->sk_addr);

	/* wait for packet */
	if(recv(scfg->socket, cfg->recv_packet, cfg->opts.length, 0)!=
		cfg->opts.length) {
		perror("stsn server failed to receive packet");
		return 1;
	}

	if(cfg->recv_packet[0]!=0x6f)
		return 0;

	/* take timestamp and copy it to received packet */
	clock_gettime(cfg->opts.clock, &tsend);
	tspec2buffer(&tsend, cfg->recv_packet+2*sizeof(uint64_t)+4);

	/* send received packet back to the server */
	if(sendto(scfg->socket, cfg->recv_packet, cfg->opts.length, 0,
		(const struct sockaddr *)&scfg->sk_addr, dest_addr_len)==-1) {
		perror("stsn server failed to send packet");
		return 1;
	}

	return 0;
}

/**
 * Clean up STSN module ressource.
 *
 * \param cfg Cyclicping config data.
 */
void stsn_deinit(struct cyclicping_cfg *cfg)
{
	struct stsn_cfg *scfg=cfg->current_mod->modcfg;

	free(scfg);
}

/**
 * Ouput STSN interface module usage.
 */
void stsn_usage(void)
{
	printf("  stsn - Use a socket based TSN connection\n");
	printf("    stsn:interface:clientmac   STSN server\n");
	printf("    stsn:interface:servermac   STSN client\n");
}

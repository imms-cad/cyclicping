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

#include <sys/select.h>

#include <cyclicping.h>
#include <opts.h>
#include <stats.h>
#include <socket.h>
#include <udp.h>

extern int run;
extern int abort_fd;

/**
 * Init UDP connection module. Parse module args. Open socket. Set socket
 * priority.
 *
 * \param cfg Cyclicping config data.
 * \param argv Interface module arguments.
 * \param argc Interface module count.
 * \return 0 on success.
 */
int udp_init(struct cyclicping_cfg *cfg, char **argv, int argc)
{
	struct udp_cfg *ucfg;
	int port_arg_idx=1;

	ucfg=(struct udp_cfg*)calloc(1, sizeof(struct udp_cfg));
	if(ucfg==NULL) {
		perror("failed to allocate memory for udp cfg");
		return 1;
	}

	cfg->current_mod->modcfg=ucfg;

	if(cfg->opts.client) {
		if(argc<2) {
			fprintf(stderr, "destination address requiered for "
				"udp client mode\n");
			return 1;
		}
		if(inet_aton(argv[1], &ucfg->dest_addr.sin_addr)==0) {
			perror("failed to convert destination address");
		}
		port_arg_idx++;
	}

	if(argc>=port_arg_idx+1) {
		ucfg->port=-1;
		ucfg->port=atoi(argv[port_arg_idx]);
		if(ucfg->port<=0 || ucfg->port>0xffff) {
			fprintf(stderr, "invalid port number for udp "
				"destination port\n");
			return 1;
		}
	} else {
		ucfg->port=DEFAULT_PORT;
	}

	if ((ucfg->socket=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))==-1) {
		perror("failed to create socket");
		return 1;
	}

	if(set_socket_priority(ucfg->socket, cfg->opts.sopriority)) {
		return 1;
	}

	if(set_socket_tos(ucfg->socket)) {
		return 1;
	}

	abort_fd=ucfg->socket;

	ucfg->dest_addr.sin_family = AF_INET;
	ucfg->dest_addr.sin_port = htons(ucfg->port);

	ucfg->local_addr.sin_family = AF_INET;
	ucfg->local_addr.sin_port = cfg->opts.server?htons(ucfg->port):0;
	ucfg->local_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if(bind(ucfg->socket, (const struct sockaddr*)&ucfg->local_addr,
		sizeof(struct sockaddr_in))==-1) {
		perror("failed to bind socket");
		return 1;
	}


	return 0;
}

/**
 * UDP client.
 *
 * \param cfg Cyclicping config data.
 * \return 0 on success, else 1.
 */
int udp_client(struct cyclicping_cfg *cfg)
{
	struct udp_cfg *ucfg=cfg->current_mod->modcfg;
	int selectResult;
	struct timespec tsend, trecv, tserver;
	struct timeval timeout;
	fd_set set;
	socklen_t dest_addr_len=sizeof(ucfg->dest_addr);

	FD_ZERO(&set);
	FD_SET(ucfg->socket, &set);

	timeout.tv_sec=1;
	timeout.tv_usec=0;

	/* take timestamp and copy it to send packet */
	clock_gettime(cfg->opts.clock, &tsend);
	tspec2buffer(&tsend, cfg->send_packet);

	/* send packet to server */
	if(sendto(ucfg->socket, cfg->send_packet, cfg->opts.length, 0,
		(const struct sockaddr *)&ucfg->dest_addr,
		dest_addr_len)==-1) {
		perror("udp client failed to send packet");
		return 1;
	}

	/* monitor socket fd via select */
	selectResult = select(ucfg->socket+1, &set, NULL, NULL, &timeout);
	if (selectResult > 0) {
		/* receive packet and take timestamp */
		if(recv(ucfg->socket, cfg->recv_packet,
			cfg->opts.length, 0)==-1) {
			perror("udp client failed to receive packet");
			return 1;
		}
		clock_gettime(cfg->opts.clock, &trecv);
	} else if(selectResult == 0) {
		fprintf(stderr, "udp client timeout receiving packet\n");
		return 1;
	}
	else {
		fprintf(stderr, "udp client select failed\n");
		return 1;
	}

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
 * UDP server.
 *
 * \param cfg Cyclicping config data.
 * \return 0 on success, else 1.
 */
int udp_server(struct cyclicping_cfg *cfg)
{
	struct udp_cfg *ucfg=cfg->current_mod->modcfg;
	struct timespec tsend;
	struct sockaddr_storage peer_addr;
	socklen_t peer_addr_len=sizeof(struct sockaddr_storage);

	/* wait for packet */
	if(recvfrom(ucfg->socket, cfg->recv_packet, cfg->opts.length, 0,
		(struct sockaddr*)&peer_addr, &peer_addr_len)==-1) {
		perror("udp server failed to receive packet");
		return 1;
	}

	/* take timestamp and copy it to received packet */
	clock_gettime(cfg->opts.clock, &tsend);
	tspec2buffer(&tsend, cfg->recv_packet+2*sizeof(uint64_t));

	/* send received packet back to the server */
	if(sendto(ucfg->socket, cfg->recv_packet, cfg->opts.length, 0,
		(const struct sockaddr *)&peer_addr, peer_addr_len)==-1) {
		perror("udp server failed to send packet");
		return 1;
	}

	return 0;
}

/**
 * Clean up UDP module ressource.
 *
 * \param cfg Cyclicping config data.
 */
void udp_deinit(struct cyclicping_cfg *cfg)
{
	struct udp_cfg *ucfg=cfg->current_mod->modcfg;

	free(ucfg);
}

/**
 * Ouput UDP interface module usage.
 */
void udp_usage(void)
{
	printf("  udp - Use a UDP connection\n");
	printf("    udp[:port]          UDP server\n");
	printf("    udp:serverip[:port] UDP client\n");
}

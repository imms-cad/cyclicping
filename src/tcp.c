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
#include <unistd.h>
#include <inttypes.h>

#include <sys/select.h>

#include <cyclicping.h>
#include <opts.h>
#include <stats.h>
#include <socket.h>
#include <tcp.h>

extern int run;
extern int abort_fd;

/**
 * Init TCP connection module. Parse module args. Open and bind socket. Set
 * socket priority.
 *
 * \param cfg Cyclicping config data.
 * \param argv Interface module arguments.
 * \param argc Interface module count.
 * \return 0 on success.
 */
int tcp_init(struct cyclicping_cfg *cfg, char **argv, int argc)
{
	struct tcp_cfg *tcfg;
	int port_arg_idx=1;

	tcfg=(struct tcp_cfg*)calloc(1, sizeof(struct tcp_cfg));
	if(tcfg==NULL) {
		perror("failed to allocate memory for tcp cfg");
		return 1;
	}

	cfg->current_mod->modcfg=tcfg;

	if(cfg->opts.client) {
		if(argc<2) {
			fprintf(stderr, "destination address requiered for "
				"tcp client mode\n");
			return 1;
		}
		if(inet_aton(argv[1], &tcfg->dest_addr.sin_addr)==0) {
			perror("failed to convert destination address");
		}
		port_arg_idx++;
	}

	if(argc>=port_arg_idx+1) {
		tcfg->port=-1;
		tcfg->port=atoi(argv[port_arg_idx]);
		if(tcfg->port<=0 || tcfg->port>0xffff) {
			fprintf(stderr, "invalid port number for tcp "
				"destination port\n");
			return 1;
		}
	} else {
		tcfg->port=DEFAULT_PORT;
	}

	if ((tcfg->socket=socket(AF_INET, SOCK_STREAM, 0))==-1) {
		perror("failed to create socket");
		return 1;
	}

	if(set_socket_priority(tcfg->socket)) {
		return 1;
	}

	abort_fd=tcfg->socket;

	tcfg->dest_addr.sin_family = AF_INET;
	tcfg->dest_addr.sin_port = htons(tcfg->port);

	tcfg->local_addr.sin_family = AF_INET;
	tcfg->local_addr.sin_port = cfg->opts.server?htons(tcfg->port):0;
	tcfg->local_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if(bind(tcfg->socket, (const struct sockaddr*)&tcfg->local_addr,
		sizeof(struct sockaddr_in))==-1) {
		perror("failed to bind socket");
		return 1;
	}


	return 0;
}

/**
 * TCP client loop.
 *
 * \param cfg Cyclicping config data.
 * \return 0 on success, else 1.
 */
int tcp_client(struct cyclicping_cfg *cfg)
{
	struct tcp_cfg *tcfg=cfg->current_mod->modcfg;
	int selectResult;
	struct timespec tsend, trecv, tserver;
	struct timeval timeout;
	fd_set set;
	socklen_t dest_addr_len=sizeof(tcfg->dest_addr);

	if(connect(tcfg->socket, (const struct sockaddr *)&tcfg->dest_addr,
		dest_addr_len)<0) {
		fprintf(stderr, "failed to connect\n");
		return 1;
	}

	while(run) {
		timeout.tv_sec=1;
		timeout.tv_usec=0;

		FD_ZERO(&set);
		FD_SET(tcfg->socket, &set);

		/* take timestamp and copy it to send packet */
		clock_gettime(cfg->opts.clock, &tsend);
		tspec2buffer(&tsend, cfg->send_packet);

		/* send packet to server */
		if(write(tcfg->socket, cfg->send_packet, cfg->opts.length)!=
			cfg->opts.length) {
			fprintf(stderr, "failed to send packet\n");
			return 1;
		}

		/* wait for socket fd to become ready for read */
		selectResult = select(tcfg->socket+1, &set, NULL, NULL,
			&timeout);
		if (selectResult > 0) {
			/* read packet and take timestamp */
			if(read(tcfg->socket, cfg->recv_packet,
				cfg->opts.length)!=cfg->opts.length) {
				perror("failed to receive packet");
				return 1;
			}
			clock_gettime(cfg->opts.clock, &trecv);
		} else if(selectResult == 0) {
			fprintf(stderr, "timeout receiving packet\n");
			return 1;
		}
		else {
			fprintf(stderr, "select failed\n");
			return 1;
		}

		/* add packet time to statistics */
		add_stats(cfg, STAT_ALL, &tsend, &trecv);

		if(cfg->opts.two_way) {
			buffer2tspec(cfg->recv_packet+2*sizeof(uint64_t),
				&tserver);
			add_stats(cfg, STAT_SEND, &tsend, &tserver);
			add_stats(cfg, STAT_RECV, &tserver, &trecv);
		}

		/* print out runtime stats */
		print_stats(cfg, &tsend, &tserver, &trecv);

		/* wait until start of next interval */
		if(client_wait(cfg, tsend))
			return 1;
	}

	return 0;
}

/**
 * TCP server loop.
 *
 * \param cfg Cyclicping config data.
 * \return 0 on success, else 1.
 */
int tcp_server(struct cyclicping_cfg *cfg)
{
	struct tcp_cfg *tcfg=cfg->current_mod->modcfg;
	int socket;
	struct timespec tsend;
	struct sockaddr_in client_addr;
	socklen_t client_addr_len=sizeof(struct sockaddr_in);

	if(cfg->opts.verbose)
		printf("listening for connections\n");

	listen(tcfg->socket, 1);

	socket=accept(tcfg->socket,
		(struct sockaddr*)&client_addr, &client_addr_len);
	if(socket<0) {
		fprintf(stderr, "failed to accept connection\n");
		return 1;
	}

	if(cfg->opts.verbose)
		printf("accepted connection\n");

	while(run) {
		/* wait for incoming packet */
		if(read(socket, cfg->recv_packet, cfg->opts.length)!=
			cfg->opts.length) {
			if(cfg->opts.verbose)
				fprintf(stderr, "failed to read packet\n");
			break;
		}

		/* take timestamp and copy to receive buffer */
		clock_gettime(cfg->opts.clock, &tsend);
		tspec2buffer(&tsend, cfg->recv_packet+2*sizeof(uint64_t));

		/* send received packet back to client */
		if(write(socket, cfg->recv_packet, cfg->opts.length)!=
			cfg->opts.length) {
			fprintf(stderr, "failed to write packet\n");
			break;
		}
	}

	if(cfg->opts.verbose)
		printf("closing connection\n");

	close(socket);

	return 0;
}

/**
 * Ouput TCP interface module usage.
 */
void tcp_usage(void)
{
	printf("  tcp - Use a TCP connection\n");
	printf("    tcp[:port]	        TCP server\n");
	printf("    tcp:serverip[:port] TCP client\n");
}

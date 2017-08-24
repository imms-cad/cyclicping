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
#include <termios.h>
#include <fcntl.h>

#include <termio.h>
#include <linux/serial.h>

#include <sys/select.h>

#include <cyclicping.h>
#include <opts.h>
#include <stats.h>
#include <uart.h>

extern int run;
extern int abort_fd;

/**
 * Convert baud rate number to constant.\n
 *
 * \param baudrate Baudrate as numeric value.
 * \return Baudrate as termios constant.
 */
static int rate_to_constant(int baudrate) {
#define B(x) case x: return B##x
	switch(baudrate) {
		B(50);     B(75);     B(110);    B(134);    B(150);
		B(200);    B(300);    B(600);    B(1200);   B(1800);
		B(2400);   B(4800);   B(9600);   B(19200);  B(38400);
		B(57600);  B(115200); B(230400); B(460800); B(500000);
		B(576000); B(921600); B(1000000);B(1152000);B(1500000);
		default: return 0;
	}
#undef B
}

/**
 * Init UART connection module. Parse module args. Set UART options. Open
 * UART fd.
 *
 * \param cfg Cyclicping config data.
 * \param argv Interface module arguments.
 * \param argc Interface module count.
 * \return 0 on success.
 */
int uart_init(struct cyclicping_cfg *cfg, char **argv, int argc)
{
	struct uart_cfg *ucfg;
	struct termios ti;
	struct serial_struct serinfo;
	int rate=0;

	ucfg=(struct uart_cfg*)calloc(1, sizeof(struct uart_cfg));
	if(ucfg==NULL) {
		perror("failed to allocate memory for uart cfg");
		return 1;
	}

	cfg->current_mod->modcfg=ucfg;

	if(argc<2) {
		printf("no device for uart interface module specified\n");
		return 1;
	}
	ucfg->device=argv[1];

	if(argc>2) {
		rate=atoi(argv[2]);
		if(rate<=0) {
			printf("invalid baudrate\n");
			return 1;
		}
	} else {
		rate=115200;
	}

	ucfg->baud_rate=rate_to_constant(rate);

	if(argc>3) {
		ucfg->flow_ctrl=atoi(argv[3]);
	}

	ucfg->fd=open(ucfg->device, O_RDWR | O_NOCTTY);
	if(ucfg->fd<=0)
	{
		perror("open");
		fprintf(stderr, "failed to open uart %s\n", ucfg->device);
		return 1;
	}

	tcflush(ucfg->fd, TCIOFLUSH);

	if(!ucfg->baud_rate) {
		/* Custom divisor */
		serinfo.reserved_char[0] = 0;
		if(ioctl(ucfg->fd, TIOCGSERIAL, &serinfo) < 0) {
			fprintf(stderr, "failed to retrieve uart port info\n");
			return 1;
		}
		serinfo.flags &= ~ASYNC_SPD_MASK;
		serinfo.flags |= ASYNC_SPD_CUST;
		serinfo.custom_divisor =
			(serinfo.baud_base + (rate / 2)) / rate;
		if(serinfo.custom_divisor < 1)
			serinfo.custom_divisor = 1;
		if(ioctl(ucfg->fd, TIOCSSERIAL, &serinfo) < 0) {
			fprintf(stderr, "failed to set uart port info\n");
			return 1;
		}
		if(ioctl(ucfg->fd, TIOCGSERIAL, &serinfo) < 0) {
			fprintf(stderr, "failed to reread uart port info\n");
			return 1;
		}
		if(serinfo.custom_divisor * rate != serinfo.baud_base) {
			printf("actual baudrate is %d / %d = %f",
			  serinfo.baud_base, serinfo.custom_divisor,
			  (float)serinfo.baud_base / serinfo.custom_divisor);
		}
	}

	/* Get the attributes of the UART */
	if (tcgetattr(ucfg->fd, &ti) < 0) {
		perror("tcgetattr");
		fprintf(stderr, "failed to retrieve uart settings\n");
		return 1;
	}

	cfmakeraw(&ti);
	ti.c_cflag |= 1;

	/* Set the UART flow control */
	if (ucfg->flow_ctrl)
		ti.c_cflag |= CRTSCTS;
	else
		ti.c_cflag &= ~CRTSCTS;

	/* minimal packet length, read will return */
	ti.c_cc[VMIN]=cfg->opts.length;

	/* set input and output speed */
	if(cfsetispeed(&ti, ucfg->baud_rate)) {
		perror("cfsetispeed");
		fprintf(stderr, "failed to set input baud rate\n");
		return 1;
	}
	if(cfsetospeed(&ti, ucfg->baud_rate)) {
		perror("cfsetospeed");
		fprintf(stderr, "failed to set ouput baud rate\n");
		return 1;
	}

	/*
	 * Set the parameters associated with the UART
	 * The change will occur immediately by using TCSANOW
	 */
	if (tcsetattr(ucfg->fd, TCSANOW, &ti) < 0) {
		perror("tcsetattr");
		fprintf(stderr, "failed to set uart parameter\n");
		return 1;
	}

	tcflush(ucfg->fd, TCIOFLUSH);

	return 0;
}

/**
 * UART client.
 *
 * \param cfg Cyclicping config data.
 * \return 0 on success, else 1.
 */
int uart_client(struct cyclicping_cfg *cfg)
{
	struct uart_cfg *ucfg=cfg->current_mod->modcfg;
	int selectResult;
	struct timespec tsend, trecv, tserver;
	struct timeval timeout;
	fd_set set;

	FD_ZERO(&set);
	FD_SET(ucfg->fd, &set);

	timeout.tv_sec=1;
	timeout.tv_usec=0;

	/* take timestamp and copy it to send packet */
	clock_gettime(cfg->opts.clock, &tsend);
	tspec2buffer(&tsend, cfg->send_packet);

	/* send packet to server */
	if(write(ucfg->fd, cfg->send_packet, cfg->opts.length)!=
		cfg->opts.length) {
		perror("uart client failed to send packet");
		return 1;
	}

	/* monitor fd via select */
	selectResult = select(ucfg->fd+1, &set, NULL, NULL, &timeout);
	if (selectResult > 0) {
		/* receive packet and take timestamp */
		if(read(ucfg->fd, cfg->recv_packet,
			cfg->opts.length)!=cfg->opts.length) {
			perror("uart client failed to receive packet");
			return 1;
		}
		clock_gettime(cfg->opts.clock, &trecv);
	} else if(selectResult == 0) {
		fprintf(stderr, "uart client timeout receiving packet\n");
		return 1;
	}
	else {
		fprintf(stderr, "uart client select failed\n");
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
int uart_server(struct cyclicping_cfg *cfg)
{
	struct uart_cfg *ucfg=cfg->current_mod->modcfg;
	struct timespec tsend;

	/* wait for packet */
	if(read(ucfg->fd, cfg->recv_packet, cfg->opts.length)!=
		cfg->opts.length) {
		perror("uart server failed to receive packet");
		return 1;
	}

	/* take timestamp and copy it to received packet */
	clock_gettime(cfg->opts.clock, &tsend);
	tspec2buffer(&tsend, cfg->recv_packet+2*sizeof(uint64_t));

	/* send received packet back to the server */
	if(write(ucfg->fd, cfg->recv_packet, cfg->opts.length)!=
		cfg->opts.length) {
		perror("uart server failed to send packet");
		return 1;
	}

	return 0;
}

/**
 * Clean up UART module ressources.
 *
 * \param cfg Cyclicping config data.
 */
void uart_deinit(struct cyclicping_cfg *cfg)
{
	struct uart_cfg *ucfg=cfg->current_mod->modcfg;

	free(ucfg);
}

/**
 * Ouput UART interface module usage.
 */
void uart_usage(void)
{
	printf("  uart - Use UART connection\n");
	printf("    uart:device[:baud[:flow]] UART server\n");
	printf("    uart:device[:baud[:flow]] UART client\n");
}

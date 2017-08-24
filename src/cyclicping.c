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

#define _GNU_SOURCE
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <inttypes.h>
#include <sched.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/mman.h>

#include <cyclicping.h>
#include <tcp.h>
#include <udp.h>
#include <uart.h>
#include <ftrace.h>

#ifdef HAVE_NETMAP
#include <netmap.h>
#endif

int run=1;
int abort_fd=0, latency_target_fd;

static struct cyclicping_module modules[] = {
	{ "udp", udp_init, udp_client, udp_server, udp_deinit,
		udp_usage },
	{ "tcp", tcp_init, tcp_client, tcp_server, tcp_deinit,
		tcp_usage },
	{ "uart", uart_init, uart_client, uart_server, uart_deinit,
		uart_usage },
#ifdef HAVE_NETMAP
	{ "netmap", netmap_init, netmap_client, netmap_server, netmap_deinit,
		netmap_usage },
#endif
	{ NULL },
};

/* borrowed from cyclictest */
void set_latency_target(void)
{
	struct stat s;
	int err;
	uint32_t latency_target_value=0;

	err = stat("/dev/cpu_dma_latency", &s);
	if (err == -1) {
		return;
	}

	latency_target_fd = open("/dev/cpu_dma_latency", O_RDWR);
	if (latency_target_fd == -1) {
		perror("WARN: open /dev/cpu_dma_latency");
		return;
	}

	err = write(latency_target_fd, &latency_target_value,
		sizeof(latency_target_value));
	if (err < 1) {
		perror("# error setting cpu_dma_latency");
		close(latency_target_fd);
		return;
	}
}

/**
 * Waits until next packet is due. Called by interface module in client mode.
 *
 * \param cfg Cyclicping config data.
 * \param tfrom Last packet time stamp.
 * \return 0 on success.
 */
int client_wait(struct cyclicping_cfg *cfg, struct timespec tfrom)
{
	/* count down loops */
	if(cfg->opts.number) {
		cfg->opts.number--;
		if(!cfg->opts.number) {
			run=0;
			return 0;
		}
	}

	/* wait till start of next interval */
	tfrom.tv_nsec+=cfg->opts.interval*1000;
	while(tfrom.tv_nsec>=NSEC_PER_SEC) {
		tfrom.tv_nsec-=NSEC_PER_SEC;
		tfrom.tv_sec++;
	}
	clock_nanosleep(cfg->opts.clock, TIMER_ABSTIME, &tfrom, NULL);

	return 0;
}

/**
 * Runs the cyclicping main loop by either calling the server or client
 * run functions of the interface module.
 *
 * \param cfg Cyclicping config data.
 * \return 0 on success.
 */
int run_cyclicping(struct cyclicping_cfg *cfg)
{
	int i, ret=0;
	char *modargv[MAX_MOD_ARG];
	char *saveptr=NULL;

	/* get args for interface module */
	modargv[0]=strtok_r(cfg->opts.opt_mod, ":", &saveptr);
	for(i=1; i<MAX_MOD_ARG; i++) {
		modargv[i]=strtok_r(NULL, ":", &saveptr);
		if(modargv[i]==NULL)
			break;
	}

	if(cfg->current_mod->init(cfg, modargv, i))
		return 1;

	gettimeofday(&cfg->test_start, NULL);

	if(cfg->opts.ftrace)
		start_ftrace();

	while(run) {
		if(cfg->opts.server)
			ret=cfg->current_mod->run_server(cfg);
		else
			ret=cfg->current_mod->run_client(cfg);

		if(ret)
			break;
	}

	if(cfg->opts.ftrace)
		stop_ftrace();

	gettimeofday(&cfg->test_end, NULL);

	if(abort_fd)
		close(abort_fd);

	if(cfg->current_mod->deinit)
		cfg->current_mod->deinit(cfg);

	return ret;
}

/**
 * Get histogram-, packet- and payload buffers.
 *
 * \param cfg Cyclicping config data.
 */
void allocate_buffers(struct cyclicping_cfg *cfg)
{
	int i;

	/* histogram buffers for send, recv, and roundtrip */
	if(cfg->opts.histogram) {
		for(i=0; i<=STAT_ALL; i++) {
			cfg->stat[i].histogram_data=(uint32_t*)malloc(
				cfg->opts.histogram*sizeof(uint32_t));
			if(cfg->stat[i].histogram_data==NULL) {
				perror("failed to allocate histogram memory\n");
				exit(1);
			}
			memset(cfg->stat[i].histogram_data, 0,
				cfg->opts.histogram*sizeof(uint32_t));

		}
	}

	for(i=0; i<=STAT_ALL; i++) {
		cfg->stat[i].min=UINT32_MAX;
	}

	cfg->recv_packet=(char*)malloc(cfg->opts.length);
	if(cfg->recv_packet==NULL) {
		perror("failed to allocate receive memory\n");
		exit(1);
	}

	cfg->send_packet=(char*)malloc(cfg->opts.length);
	if(cfg->send_packet==NULL) {
		perror("failed to allocate send memory\n");
		exit(1);
	}

	/* data for each packet if packet dump was requested */
	if(cfg->opts.dumpfile) {
		cfg->dump=(struct pdump*)malloc(
			cfg->opts.number*sizeof(struct pdump));
		if(cfg->dump==NULL) {
			perror("failed to allocate dump memory\n");
			exit(1);
		}
	}
}

/**
 * Set CPU affinity.
 *
 * \param cfg Cyclicping config data.
 */
void set_affinity(struct cyclicping_cfg *cfg)
{
	cpu_set_t set;

	CPU_ZERO(&set);
	CPU_SET(cfg->opts.affinity, &set);

	if(sched_setaffinity(getpid(), sizeof(set), &set) == -1) {
		perror("failed to set affinity");
		exit(1);
	}
}

/**
 * Free all buffers we allocated.
 *
 * \param cfg Cyclicping config data.
 */
void cleanup_cfg(struct cyclicping_cfg *cfg)
{
	int i;

	if(cfg->send_packet)
		free(cfg->send_packet);

	if(cfg->recv_packet)
		free(cfg->recv_packet);

	if(cfg->opts.dumpfile)
		free(cfg->opts.dumpfile);

	for(i=0; i<=STAT_ALL; i++) {
		if(cfg->stat[i].histogram_data) {
			free(cfg->stat[i].histogram_data);
		}
	}

	if(cfg->dump) {
		free(cfg->dump);
	}
}

/**
 * Handler for SIGINT and SIGTERM.
 *
 * \param signum Signal number.
 */
void term_handler(int signum)
{
	run=0;

	/* An interface module might wait on a file descriptor. Close it to
	 * make the module return. */
	if(abort_fd) {
		close(abort_fd);
		abort_fd=0;
	}
}

int main(int argc, char *argv[])
{
	struct sigaction new_action;
	struct cyclicping_cfg cfg;
	struct sched_param param;
	int ret;

	new_action.sa_handler = term_handler;
	sigemptyset (&new_action.sa_mask);
	new_action.sa_flags = 0;
	sigaction (SIGINT, &new_action, NULL);
	sigaction (SIGTERM, &new_action, NULL);

	memset(&cfg, 0, sizeof(struct cyclicping_cfg));
	cfg.modules=modules;
	parse_cfg(argc, argv, &cfg);

	if(cfg.opts.mlock) {
		if(mlockall(MCL_CURRENT|MCL_FUTURE) == -1) {
			perror("mlockall failed");
			return -1;
		}
	}

	if(cfg.opts.priority) {
		param.sched_priority=cfg.opts.priority;
		if(sched_setscheduler(0, SCHED_FIFO, &param) == -1) {
			perror("sched_setscheduler failed");
			return -1;
		}
	}

	set_latency_target();

	if(cfg.opts.opt_affinity)
		set_affinity(&cfg);

	if(cfg.opts.ftrace) {
		if(setup_ftrace()) {
			return -1;
		}
	}

	allocate_buffers(&cfg);

	ret=run_cyclicping(&cfg);

	if(!cfg.opts.quiet)
		printf("\n\n\n");

	if(!ret) {
		if(cfg.opts.histogram) {
			if(cfg.opts.gnuplot)
				print_gnuplot_histogram(&cfg, argc, argv);
			else
				print_histogram(&cfg, argc, argv);
		}
	}

	if(cfg.dump)
		write_dump(&cfg);

	if(cfg.opts.ftrace)
		printf("trace available at: /sys/kernel/debug/tracing/trace\n");

	cleanup_cfg(&cfg);

	return ret;
}

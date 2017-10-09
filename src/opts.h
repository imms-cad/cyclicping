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

#ifndef __OPTS_H__
#define __OPTS_H__

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>

#define DEFAULT_PORT	15202
#define DEFAULT_LENGTH	64
#define DEFAULT_INTERVAL 1000000

#define MAX_MOD_ARG	10

struct cyclicping_cfg;

struct cyclicping_opts {
	char verbose;
	char version;
	char client;
	char server;
	int interval;
	int number;
	int length;
	char ftrace;
	int priority;
	int sopriority;
	int histogram;
	char ms;
	char mlock;
	char quiet;
	int clock;
	char two_way;
	char affinity;
	char *dumpfile;
	int breaktrace;
	char gnuplot;

	char *opt_interval;
	char *opt_number;
	char *opt_length;
	char *opt_priority;
	char *opt_sopriority;
	char *opt_histogram;
	char *opt_clock;
	char *opt_dumpfile;
	char *opt_affinity;
	char *opt_mod;
	char *opt_breaktrace;
};

void help();
int parse_cfg(int argc, char *argv[], struct cyclicping_cfg *cfg);

#endif

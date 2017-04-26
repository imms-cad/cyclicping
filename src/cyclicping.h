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

#ifndef __CYCLICPING_H__
#define __CYCLICPING_H__

#include <time.h>

#include <stats.h>
#include <opts.h>

#define VERSION         "0.1.0"

struct cyclicping_module {
	const char *name;
	int (*init)(struct cyclicping_cfg *cfg, char **argv, int argc);
	int (*run_client)(struct cyclicping_cfg *cfg);
	int (*run_server)(struct cyclicping_cfg *cfg);
	void (*usage)(void);
	void *modcfg;
};

struct cyclicping_cfg {
	struct cyclicping_opts opts;

	char *recv_packet;
	char *send_packet;

	uint64_t cnt;
	struct tstats stat[STAT_ALL+1];
	struct pdump *dump;

	struct timeval test_start;
	struct timeval test_end;

	struct cyclicping_module *current_mod;
	struct cyclicping_module *modules;
};

int client_wait(struct cyclicping_cfg *cfg, struct timespec tfrom);

#endif

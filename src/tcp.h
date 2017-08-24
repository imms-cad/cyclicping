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

#ifndef __TCP_H__
#define __TCP_H__

struct cyclicping_cfg;

struct tcp_cfg {
	struct sockaddr_in dest_addr;
	struct sockaddr_in local_addr;
	int port;
	int socket;
};

int tcp_init(struct cyclicping_cfg *cfg, char **argv, int argc);
int tcp_client(struct cyclicping_cfg *cfg);
int tcp_server(struct cyclicping_cfg *cfg);
void tcp_deinit(struct cyclicping_cfg *cfg);
void tcp_usage(void);

#endif

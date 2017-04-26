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

#ifndef __UART_H__
#define __UART_H__

struct uart_cfg {
	int fd;
	int flow_ctrl;
	int baud_rate;
	char *device;
};

int uart_init(struct cyclicping_cfg *cfg, char **argv, int argc);
int uart_client(struct cyclicping_cfg *cfg);
int uart_server(struct cyclicping_cfg *cfg);
void uart_usage(void);

#endif

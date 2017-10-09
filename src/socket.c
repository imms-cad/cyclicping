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
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

int set_socket_tos(int sockfd)
{
	/* class selector 7, network control */
	int tos = 224, toscheck=0;
	socklen_t toslen;

	/* set IP TOS field */
	if(setsockopt(sockfd, IPPROTO_IP, IP_TOS, &tos, sizeof(tos))!=0) {
		perror("setting IP_TOS failed");
		return 1;
	}

	/* check it */
	toslen=sizeof(toscheck);
	getsockopt(sockfd, IPPROTO_IP, IP_TOS, &toscheck, &toslen);
	if (toscheck != tos) {
		fprintf(stderr, "TOS %d != %d\n", toscheck, tos);
		return 1;
	}

	return 0;
}

int set_socket_priority(int sockfd)
{
	int soprio=255, sopriocheck=0;
	socklen_t sopriolen;

	/* set socket priority */
	if(setsockopt(sockfd, SOL_SOCKET, SO_PRIORITY, &soprio,
		sizeof(soprio))!=0) {
		perror("WARN: setting SO_PRIORITY failed");
		return 0;
	}

	/* check it */
	sopriolen=sizeof(sopriocheck);
	getsockopt(sockfd, SOL_SOCKET, SO_PRIORITY, &sopriocheck, &sopriolen);
	if (soprio != sopriocheck) {
		fprintf(stderr, "warning: socket prio failed: %d != %d\n",
			soprio, sopriocheck);
		return 0;
	}

	return 0;
}

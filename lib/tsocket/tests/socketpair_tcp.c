/*
   Unix SMB/CIFS implementation.
   Samba utility functions
   Copyright (C) Andrew Tridgell 1992-1998
   Copyright (C) Tim Potter      2000-2001

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "includes.h"
#include "system/network.h"
#include "socketpair_tcp.h"

/*******************************************************************
this is like socketpair but uses tcp. It is used by the Samba
regression test code
The function guarantees that nobody else can attach to the socket,
or if they do that this function fails and the socket gets closed
returns 0 on success, -1 on failure
the resulting file descriptors are symmetrical
 ******************************************************************/
int socketpair_tcp(int fd[2])
{
	int listener;
	struct sockaddr_in sock;
	struct sockaddr_in sock2;
	socklen_t socklen = sizeof(sock);
	int connect_done = 0;

	fd[0] = fd[1] = listener = -1;

	memset(&sock, 0, sizeof(sock));

	if ((listener = socket(PF_INET, SOCK_STREAM, 0)) == -1) goto failed;

	memset(&sock2, 0, sizeof(sock2));
#ifdef HAVE_SOCK_SIN_LEN
	sock2.sin_len = sizeof(sock2);
#endif
	sock2.sin_family = PF_INET;

	if (bind(listener, (struct sockaddr *)&sock2, sizeof(sock2)) != 0) goto failed;

	if (listen(listener, 1) != 0) goto failed;

	if (getsockname(listener, (struct sockaddr *)&sock, &socklen) != 0) goto failed;

	if ((fd[1] = socket(PF_INET, SOCK_STREAM, 0)) == -1) goto failed;

	set_blocking(fd[1], 0);

	sock.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	if (connect(fd[1], (struct sockaddr *)&sock, socklen) == -1) {
		if (errno != EINPROGRESS) goto failed;
	} else {
		connect_done = 1;
	}

	if ((fd[0] = accept(listener, (struct sockaddr *)&sock, &socklen)) == -1) goto failed;

	if (connect_done == 0) {
		if (connect(fd[1], (struct sockaddr *)&sock, socklen) != 0
		    && errno != EISCONN) goto failed;
	}
	close(listener);

	set_blocking(fd[1], 1);

	/* all OK! */
	return 0;

 failed:
	if (fd[0] != -1) close(fd[0]);
	if (fd[1] != -1) close(fd[1]);
	if (listener != -1) close(listener);
	return -1;
}

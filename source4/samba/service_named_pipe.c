/*
   Unix SMB/CIFS implementation.

   helper functions for NAMED PIPE servers

   Copyright (C) Stefan (metze) Metzmacher	2008

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
#include <tevent.h>
#include "samba/service.h"
#include "param/param.h"
#include "auth/auth.h"
#include "auth/session.h"
#include "auth/auth_sam_reply.h"
#include "lib/socket/socket.h"
#include "lib/tsocket/tsocket.h"
#include "libcli/util/tstream.h"
#include "librpc/gen_ndr/ndr_named_pipe_auth.h"
#include "system/passwd.h"
#include "system/network.h"
#include "libcli/raw/smb.h"
#include "auth/session.h"
#include "libcli/security/security.h"
#include "libcli/named_pipe_auth/npa_tstream.h"

struct named_pipe_socket {
	const char *pipe_name;
	const char *pipe_path;
	const struct stream_server_ops *ops;
	void *private_data;
};

static void named_pipe_accept_done(struct tevent_req *subreq);

static void named_pipe_accept(struct stream_connection *conn)
{
	struct tstream_context *plain_tstream;
	int fd;
	struct tevent_req *subreq;
	int ret;

	/* Let tstream take over fd operations */

	fd = socket_get_fd(conn->socket);
	socket_set_flags(conn->socket, SOCKET_FLAG_NOCLOSE);
	TALLOC_FREE(conn->event.fde);
	TALLOC_FREE(conn->socket);

	ret = tstream_bsd_existing_socket(conn, fd, &plain_tstream);
	if (ret != 0) {
		stream_terminate_connection(conn,
				"named_pipe_accept: out of memory");
		return;
	}
	/* as server we want to fail early */
	tstream_bsd_fail_readv_first_error(plain_tstream, true);

	subreq = tstream_npa_accept_existing_send(conn, conn->event.ctx,
						  plain_tstream,
						  FILE_TYPE_MESSAGE_MODE_PIPE,
						  0xff | 0x0400 | 0x0100,
						  4096);
	if (subreq == NULL) {
		stream_terminate_connection(conn,
			"named_pipe_accept: "
			"no memory for tstream_npa_accept_existing_send");
		return;
	}
	tevent_req_set_callback(subreq, named_pipe_accept_done, conn);
}

static void named_pipe_accept_done(struct tevent_req *subreq)
{
	struct stream_connection *conn = tevent_req_callback_data(subreq,
						struct stream_connection);
	struct named_pipe_socket *pipe_sock =
				talloc_get_type(conn->private_data,
						struct named_pipe_socket);
	enum dcerpc_transport_t transport;
	struct tsocket_address *remote_client_addr;
	char *remote_client_name;
	struct tsocket_address *local_server_addr;
	char *local_server_name;
	struct auth_session_info_transport *session_info_transport;
	const char *reason = NULL;
	TALLOC_CTX *tmp_ctx;
	int error;
	int ret;

	tmp_ctx = talloc_new(conn);
	if (!tmp_ctx) {
		reason = "Out of memory!\n";
		goto out;
	}

	ret = tstream_npa_accept_existing_recv(subreq, &error, tmp_ctx,
					       &conn->tstream,
					       NULL,
					       &transport,
					       &remote_client_addr,
					       &remote_client_name,
					       &local_server_addr,
					       &local_server_name,
					       &session_info_transport);
	TALLOC_FREE(subreq);
	if (ret != 0) {
		reason = talloc_asprintf(conn,
					 "tstream_npa_accept_existing_recv()"
					 " failed: %s", strerror(error));
		goto out;
	}

	conn->local_address = talloc_move(conn, &local_server_addr);
	conn->remote_address = talloc_move(conn, &remote_client_addr);

	DBG_DEBUG("Accepted npa connection from %s. "
		  "Client: %s (%s). Server: %s (%s)\n",
		  tsocket_address_string(conn->remote_address, tmp_ctx),
		  local_server_name,
		  tsocket_address_string(local_server_addr, tmp_ctx),
		  remote_client_name,
		  tsocket_address_string(remote_client_addr, tmp_ctx));

	conn->session_info = auth_session_info_from_transport(conn, session_info_transport,
							      conn->lp_ctx,
							      &reason);
	if (!conn->session_info) {
		goto out;
	}

	if (transport == NCACN_NP) {
		if (security_token_is_system(conn->session_info->security_token)) {
			reason = talloc_asprintf(
				conn,
				"System token not allowed on transport %d\n",
				transport);
			goto out;
		}
	} else if (transport == NCALRPC) {
		/*
		 * TODO:
		 * we should somehow remember the given transport on
		 * the connection, but that's a task for another day
		 * as it's not trivial to do...
		 */
	} else {
		reason = talloc_asprintf(
			conn,
			"Only allow NCACN_NP or NCALRPC transport on named pipes, "
			"got %d\n",
			(int)transport);
		goto out;
	}

	/*
	 * hand over to the real pipe implementation,
	 * now that we have setup the transport session_info
	 */
	conn->ops = pipe_sock->ops;
	conn->private_data = pipe_sock->private_data;
	conn->ops->accept_connection(conn);

	DBG_DEBUG("named pipe connection [%s] established\n", conn->ops->name);

	talloc_free(tmp_ctx);
	return;

out:
	talloc_free(tmp_ctx);
	if (!reason) {
		reason = "Internal error";
	}
	stream_terminate_connection(conn, reason);
}

/*
  called when a pipe socket becomes readable
*/
static void named_pipe_recv(struct stream_connection *conn, uint16_t flags)
{
	stream_terminate_connection(conn, "named_pipe_recv: called");
}

/*
  called when a pipe socket becomes writable
*/
static void named_pipe_send(struct stream_connection *conn, uint16_t flags)
{
	stream_terminate_connection(conn, "named_pipe_send: called");
}

static const struct stream_server_ops named_pipe_stream_ops = {
	.name			= "named_pipe",
	.accept_connection	= named_pipe_accept,
	.recv_handler		= named_pipe_recv,
	.send_handler		= named_pipe_send,
};

NTSTATUS tstream_setup_named_pipe(TALLOC_CTX *mem_ctx,
				  struct tevent_context *event_context,
				  struct loadparm_context *lp_ctx,
				  const struct model_ops *model_ops,
				  const struct stream_server_ops *stream_ops,
				  const char *pipe_name,
				  void *private_data,
				  void *process_context)
{
	char *dirname;
	struct named_pipe_socket *pipe_sock;
	NTSTATUS status = NT_STATUS_NO_MEMORY;;

	pipe_sock = talloc(mem_ctx, struct named_pipe_socket);
	if (pipe_sock == NULL) {
		goto fail;
	}

	/* remember the details about the pipe */
	pipe_sock->pipe_name	= strlower_talloc(pipe_sock, pipe_name);
	if (pipe_sock->pipe_name == NULL) {
		goto fail;
	}

	if (!directory_create_or_exist(lpcfg_ncalrpc_dir(lp_ctx), 0755)) {
		status = map_nt_error_from_unix_common(errno);
		DBG_ERR("Failed to create ncalrpc pipe directory '%s' - %s\n",
			lpcfg_ncalrpc_dir(lp_ctx), nt_errstr(status));
		goto fail;
	}

	dirname = talloc_asprintf(pipe_sock, "%s/np", lpcfg_ncalrpc_dir(lp_ctx));
	if (dirname == NULL) {
		goto fail;
	}

	if (!directory_create_or_exist_strict(dirname, geteuid(), 0700)) {
		status = map_nt_error_from_unix_common(errno);
		DBG_ERR("Failed to create stream pipe directory '%s' - %s\n",
			dirname, nt_errstr(status));
		goto fail;
	}

	if (strncmp(pipe_name, "\\pipe\\", 6) == 0) {
		pipe_name += 6;
	}

	pipe_sock->pipe_path = talloc_asprintf(pipe_sock, "%s/%s", dirname,
					       pipe_name);
	if (pipe_sock->pipe_path == NULL) {
		goto fail;
	}

	talloc_free(dirname);

	pipe_sock->ops = stream_ops;
	pipe_sock->private_data	= private_data;

	status = stream_setup_socket(pipe_sock,
				     event_context,
				     lp_ctx,
				     model_ops,
				     &named_pipe_stream_ops,
				     "unix",
				     pipe_sock->pipe_path,
				     NULL,
				     NULL,
				     pipe_sock,
				     process_context);
	if (!NT_STATUS_IS_OK(status)) {
		goto fail;
	}
	return NT_STATUS_OK;

 fail:
	talloc_free(pipe_sock);
	return status;
}

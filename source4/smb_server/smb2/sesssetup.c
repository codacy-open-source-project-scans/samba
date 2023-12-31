/* 
   Unix SMB2 implementation.
   
   Copyright (C) Andrew Bartlett	2001-2005
   Copyright (C) Stefan Metzmacher	2005
   
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
#include "auth/gensec/gensec.h"
#include "auth/auth.h"
#include "libcli/smb2/smb2.h"
#include "libcli/smb2/smb2_calls.h"
#include "smb_server/smb_server.h"
#include "smb_server/smb2/smb2_server.h"
#include "samba/service_stream.h"
#include "lib/stream/packet.h"

static void smb2srv_sesssetup_send(struct smb2srv_request *req, union smb_sesssetup *io)
{
	if (NT_STATUS_IS_OK(req->status)) {
		/* nothing */
	} else if (NT_STATUS_EQUAL(req->status, NT_STATUS_MORE_PROCESSING_REQUIRED)) {
		/* nothing */
	} else {
		smb2srv_send_error(req, req->status);
		return;
	}

	SMB2SRV_CHECK(smb2srv_setup_reply(req, 0x08, true, io->smb2.out.secblob.length));

	SBVAL(req->out.hdr, SMB2_HDR_SESSION_ID,	io->smb2.out.uid);

	SSVAL(req->out.body, 0x02, io->smb2.out.session_flags);
	SMB2SRV_CHECK(smb2_push_o16s16_blob(&req->out, 0x04, io->smb2.out.secblob));

	smb2srv_send_reply(req);
}

struct smb2srv_sesssetup_callback_ctx {
	struct smb2srv_request *req;
	union smb_sesssetup *io;
	struct smbsrv_session *smb_sess;
};

static void smb2srv_sesssetup_callback(struct tevent_req *subreq)
{
	struct smb2srv_sesssetup_callback_ctx *ctx = tevent_req_callback_data(subreq,
						     struct smb2srv_sesssetup_callback_ctx);
	struct smb2srv_request *req = ctx->req;
	union smb_sesssetup *io = ctx->io;
	struct smbsrv_session *smb_sess = ctx->smb_sess;
	struct auth_session_info *session_info = NULL;
	enum security_user_level user_level;
	NTSTATUS status;

	packet_recv_enable(req->smb_conn->packet);

	status = gensec_update_recv(subreq, req, &io->smb2.out.secblob);
	TALLOC_FREE(subreq);
	if (NT_STATUS_EQUAL(status, NT_STATUS_MORE_PROCESSING_REQUIRED)) {
		goto done;
	} else if (!NT_STATUS_IS_OK(status)) {
		goto failed;
	}

	status = gensec_session_info(smb_sess->gensec_ctx, smb_sess, &session_info);
	if (!NT_STATUS_IS_OK(status)) {
		goto failed;
	}

	/* Ensure this is marked as a 'real' vuid, not one
	 * simply valid for the session setup leg */
	status = smbsrv_session_sesssetup_finished(smb_sess, session_info);
	if (!NT_STATUS_IS_OK(status)) {
		goto failed;
	}
	req->session = smb_sess;

	user_level = security_session_user_level(smb_sess->session_info, NULL);
	if (user_level >= SECURITY_USER) {
		if (smb_sess->smb2_signing.required) {
			/* activate smb2 signing on the session */
			smb_sess->smb2_signing.active = true;
		}
		/* we need to sign the session setup response */
		req->is_signed = true;
	}

done:
	io->smb2.out.uid = smb_sess->vuid;
failed:
	req->status = nt_status_squash(status);
	smb2srv_sesssetup_send(req, io);
	if (!NT_STATUS_IS_OK(status) && !
	    NT_STATUS_EQUAL(status, NT_STATUS_MORE_PROCESSING_REQUIRED)) {
		talloc_free(smb_sess);
	}
}

static void smb2srv_sesssetup_backend(struct smb2srv_request *req, union smb_sesssetup *io)
{
	NTSTATUS status;
	struct smb2srv_sesssetup_callback_ctx *callback_ctx;
	struct smbsrv_session *smb_sess = NULL;
	uint64_t vuid;
	struct tevent_req *subreq;

	io->smb2.out.session_flags = 0;
	io->smb2.out.uid	= 0;
	io->smb2.out.secblob = data_blob(NULL, 0);

	vuid = BVAL(req->in.hdr, SMB2_HDR_SESSION_ID);

	/*
	 * only when we got '0' we should allocate a new session
	 */
	if (vuid == 0) {
		struct gensec_security *gensec_ctx;
		struct tsocket_address *remote_address, *local_address;

		status = samba_server_gensec_start(req,
						   req->smb_conn->connection->event.ctx,
						   req->smb_conn->connection->msg_ctx,
						   req->smb_conn->lp_ctx,
						   req->smb_conn->negotiate.server_credentials,
						   "cifs",
						   &gensec_ctx);
		if (!NT_STATUS_IS_OK(status)) {
			DEBUG(1, ("Failed to start GENSEC server code: %s\n", nt_errstr(status)));
			goto failed;
		}

		gensec_want_feature(gensec_ctx, GENSEC_FEATURE_SESSION_KEY);
		gensec_want_feature(gensec_ctx, GENSEC_FEATURE_SMB_TRANSPORT);

		remote_address = socket_get_remote_addr(req->smb_conn->connection->socket,
							req);
		if (!remote_address) {
			status = NT_STATUS_INTERNAL_ERROR;
			DBG_ERR("Failed to obtain remote address\n");
			goto failed;
		}

		status = gensec_set_remote_address(gensec_ctx,
						   remote_address);
		if (!NT_STATUS_IS_OK(status)) {
			DBG_ERR("Failed to set remote address\n");
			goto failed;
		}

		local_address = socket_get_local_addr(req->smb_conn->connection->socket,
						      req);
		if (!local_address) {
			status = NT_STATUS_INTERNAL_ERROR;
			DBG_ERR("Failed to obtain local address\n");
			goto failed;
		}

		status = gensec_set_local_address(gensec_ctx,
						  local_address);
		if (!NT_STATUS_IS_OK(status)) {
			DBG_ERR("Failed to set local address\n");
			goto failed;
		}

		status = gensec_set_target_service_description(gensec_ctx,
							       "SMB2");

		if (!NT_STATUS_IS_OK(status)) {
			DBG_ERR("Failed to set service description\n");
			goto failed;
		}

		status = gensec_start_mech_by_oid(gensec_ctx, GENSEC_OID_SPNEGO);
		if (!NT_STATUS_IS_OK(status)) {
			DEBUG(1, ("Failed to start GENSEC SPNEGO server code: %s\n", nt_errstr(status)));
			goto failed;
		}

		/* allocate a new session */
		smb_sess = smbsrv_session_new(req->smb_conn, req->smb_conn, gensec_ctx);
		if (!smb_sess) {
			status = NT_STATUS_INSUFFICIENT_RESOURCES;
			goto failed;
		}
		status = smbsrv_smb2_init_tcons(smb_sess);
		if (!NT_STATUS_IS_OK(status)) {
			goto failed;
		}
	} else {
		/* lookup an existing session */
		smb_sess = smbsrv_session_find_sesssetup(req->smb_conn, vuid);
	}

	if (!smb_sess) {
		status = NT_STATUS_USER_SESSION_DELETED;
		goto failed;
	}

	if (smb_sess->session_info) {
		/* see WSPP test suite - test 11 */
		status = NT_STATUS_REQUEST_NOT_ACCEPTED;
		goto failed;
	}

	if (!smb_sess->gensec_ctx) {
		status = NT_STATUS_INTERNAL_ERROR;
		DEBUG(1, ("Internal ERROR: no gensec_ctx on session: %s\n", nt_errstr(status)));
		goto failed;
	}

	callback_ctx = talloc(req, struct smb2srv_sesssetup_callback_ctx);
	if (!callback_ctx) goto nomem;
	callback_ctx->req	= req;
	callback_ctx->io	= io;
	callback_ctx->smb_sess	= smb_sess;

	subreq = gensec_update_send(callback_ctx,
				    req->smb_conn->connection->event.ctx,
				    smb_sess->gensec_ctx,
				    io->smb2.in.secblob);
	if (!subreq) goto nomem;
	tevent_req_set_callback(subreq, smb2srv_sesssetup_callback, callback_ctx);

	/* note that we ignore SMB2_NEGOTIATE_SIGNING_ENABLED from the client.
	   This is deliberate as windows does not set it even when it does 
	   set SMB2_NEGOTIATE_SIGNING_REQUIRED */
	if (io->smb2.in.security_mode & SMB2_NEGOTIATE_SIGNING_REQUIRED) {
		smb_sess->smb2_signing.required = true;
	}

	/* disable receipt of more packets on this socket until we've
	   finished with the session setup. This avoids a problem with
	   crashes if we get EOF on the socket while processing a session
	   setup */
	packet_recv_disable(req->smb_conn->packet);

	return;
nomem:
	status = NT_STATUS_NO_MEMORY;
failed:
	talloc_free(smb_sess);
	req->status = nt_status_squash(status);
	smb2srv_sesssetup_send(req, io);
}

void smb2srv_sesssetup_recv(struct smb2srv_request *req)
{
	union smb_sesssetup *io;

	SMB2SRV_CHECK_BODY_SIZE(req, 0x18, true);
	SMB2SRV_TALLOC_IO_PTR(io, union smb_sesssetup);

	io->smb2.level		       = RAW_SESSSETUP_SMB2;
	io->smb2.in.vc_number	       = CVAL(req->in.body, 0x02);
	io->smb2.in.security_mode      = CVAL(req->in.body, 0x03);
	io->smb2.in.capabilities       = IVAL(req->in.body, 0x04);
	io->smb2.in.channel            = IVAL(req->in.body, 0x08);
	io->smb2.in.previous_sessionid = BVAL(req->in.body, 0x10);
	SMB2SRV_CHECK(smb2_pull_o16s16_blob(&req->in, io, req->in.body+0x0C, &io->smb2.in.secblob));

	smb2srv_sesssetup_backend(req, io);
}

static int smb2srv_cleanup_session_destructor(struct smbsrv_session **session)
{
	/* TODO: call ntvfs backends to close file of this session */
	DEBUG(0,("free session[%p]\n", *session));
	talloc_free(*session);
	return 0;
}

static NTSTATUS smb2srv_logoff_backend(struct smb2srv_request *req)
{
	struct smbsrv_session **session_ptr;

	/* we need to destroy the session after sending the reply */
	session_ptr = talloc(req, struct smbsrv_session *);
	NT_STATUS_HAVE_NO_MEMORY(session_ptr);

	*session_ptr = req->session;
	talloc_set_destructor(session_ptr, smb2srv_cleanup_session_destructor);

	return NT_STATUS_OK;
}

static void smb2srv_logoff_send(struct smb2srv_request *req)
{
	if (NT_STATUS_IS_ERR(req->status)) {
		smb2srv_send_error(req, req->status);
		return;
	}

	SMB2SRV_CHECK(smb2srv_setup_reply(req, 0x04, false, 0));

	SSVAL(req->out.body, 0x02, 0);

	smb2srv_send_reply(req);
}

void smb2srv_logoff_recv(struct smb2srv_request *req)
{
	SMB2SRV_CHECK_BODY_SIZE(req, 0x04, false);

	req->status = smb2srv_logoff_backend(req);

	if (req->control_flags & SMB2SRV_REQ_CTRL_FLAG_NOT_REPLY) {
		talloc_free(req);
		return;
	}
	smb2srv_logoff_send(req);
}

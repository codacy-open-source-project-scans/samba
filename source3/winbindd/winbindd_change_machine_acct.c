/*
   Unix SMB/CIFS implementation.
   async implementation of WINBINDD_CHANGE_MACHINE_ACCT
   Copyright (C) Volker Lendecke 2009
   Copyright (C) Guenther Deschner 2009

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
#include "winbindd.h"
#include "librpc/gen_ndr/ndr_winbind_c.h"

struct winbindd_change_machine_acct_state {
	uint8_t dummy;
};

static void winbindd_change_machine_acct_done(struct tevent_req *subreq);

struct tevent_req *winbindd_change_machine_acct_send(TALLOC_CTX *mem_ctx,
						     struct tevent_context *ev,
						     struct winbindd_cli_state *cli,
						     struct winbindd_request *request)
{
	struct tevent_req *req, *subreq;
	struct winbindd_change_machine_acct_state *state;
	struct winbindd_domain *domain;
	const char *dcname = NULL;

	req = tevent_req_create(mem_ctx, &state,
				struct winbindd_change_machine_acct_state);
	if (req == NULL) {
		return NULL;
	}

	if (request->data.init_conn.dcname[0] != '\0') {
		dcname = request->data.init_conn.dcname;
	}

	domain = find_domain_from_name(request->domain_name);
	if (domain == NULL) {
		tevent_req_nterror(req, NT_STATUS_NO_SUCH_DOMAIN);
		return tevent_req_post(req, ev);
	}
	if (domain->internal) {
		/*
		 * Internal domains are passdb based, we can always
		 * contact them.
		 *
		 * This also protects us from changing the password on
		 * the AD DC without updating all the right databases.
		 * Do not remove this until that code is fixed.
		 */
		tevent_req_done(req);
		return tevent_req_post(req, ev);
	}

	subreq = dcerpc_wbint_ChangeMachineAccount_send(state, ev,
							dom_child_handle(domain),
							dcname);
	if (tevent_req_nomem(subreq, req)) {
		return tevent_req_post(req, ev);
	}
	tevent_req_set_callback(subreq, winbindd_change_machine_acct_done, req);
	return req;
}

static void winbindd_change_machine_acct_done(struct tevent_req *subreq)
{
	struct tevent_req *req = tevent_req_callback_data(
		subreq, struct tevent_req);
	struct winbindd_change_machine_acct_state *state = tevent_req_data(
		req, struct winbindd_change_machine_acct_state);
	NTSTATUS status, result;

	status = dcerpc_wbint_ChangeMachineAccount_recv(subreq, state, &result);
	if (any_nt_status_not_ok(status, result, &status)) {
		tevent_req_nterror(req, status);
		return;
	}
	tevent_req_done(req);
}

NTSTATUS winbindd_change_machine_acct_recv(struct tevent_req *req,
					   struct winbindd_response *presp)
{
	return tevent_req_simple_recv_ntstatus(req);
}

/*
   Unix SMB/CIFS implementation.
   async lookupgroupmembers
   Copyright (C) Volker Lendecke 2009

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
#include "../librpc/gen_ndr/ndr_security.h"
#include "../libcli/security/security.h"
#include "lib/util/util_tdb.h"
#include "lib/dbwrap/dbwrap.h"
#include "lib/dbwrap/dbwrap_rbt.h"

/*
 * We have 3 sets of routines here:
 *
 * wb_lookupgroupmem is the low-level one-group routine
 *
 * wb_groups_members walks a list of groups
 *
 * wb_group_members finally is the high-level routine expanding groups
 * recursively
 */

/*
 * TODO: fill_grent_mem_domusers must be re-added
 */

/*
 * Look up members of a single group. Essentially a wrapper around the
 * lookup_groupmem winbindd_methods routine.
 */

struct wb_lookupgroupmem_state {
	struct dom_sid sid;
	struct wbint_Principals members;
};

static void wb_lookupgroupmem_done(struct tevent_req *subreq);

static struct tevent_req *wb_lookupgroupmem_send(TALLOC_CTX *mem_ctx,
						 struct tevent_context *ev,
						 const struct dom_sid *group_sid,
						 enum lsa_SidType type)
{
	struct tevent_req *req, *subreq;
	struct wb_lookupgroupmem_state *state;
	struct winbindd_domain *domain;

	req = tevent_req_create(mem_ctx, &state,
				struct wb_lookupgroupmem_state);
	if (req == NULL) {
		return NULL;
	}
	sid_copy(&state->sid, group_sid);

	domain = find_domain_from_sid_noinit(group_sid);
	if (domain == NULL) {
		tevent_req_nterror(req, NT_STATUS_NO_SUCH_GROUP);
		return tevent_req_post(req, ev);
	}

	subreq = dcerpc_wbint_LookupGroupMembers_send(
		state, ev, dom_child_handle(domain), &state->sid, type,
		&state->members);
	if (tevent_req_nomem(subreq, req)) {
		return tevent_req_post(req, ev);
	}
	tevent_req_set_callback(subreq, wb_lookupgroupmem_done, req);
	return req;
}

static void wb_lookupgroupmem_done(struct tevent_req *subreq)
{
	struct tevent_req *req = tevent_req_callback_data(
		subreq, struct tevent_req);
	struct wb_lookupgroupmem_state *state = tevent_req_data(
		req, struct wb_lookupgroupmem_state);
	NTSTATUS status, result;

	status = dcerpc_wbint_LookupGroupMembers_recv(subreq, state, &result);
	TALLOC_FREE(subreq);
	if (any_nt_status_not_ok(status, result, &status)) {
		tevent_req_nterror(req, status);
		return;
	}
	tevent_req_done(req);
}

static NTSTATUS wb_lookupgroupmem_recv(struct tevent_req *req,
				       TALLOC_CTX *mem_ctx,
				       uint32_t *num_members,
				       struct wbint_Principal **members)
{
	struct wb_lookupgroupmem_state *state = tevent_req_data(
		req, struct wb_lookupgroupmem_state);
	NTSTATUS status;

	if (tevent_req_is_nterror(req, &status)) {
		return status;
	}

	*num_members = state->members.num_principals;
	*members = talloc_move(mem_ctx, &state->members.principals);
	return NT_STATUS_OK;
}

/*
 * Same as wb_lookupgroupmem for a list of groups
 */

struct wb_groups_members_state {
	struct tevent_context *ev;
	struct wbint_Principal *groups;
	uint32_t num_groups;
	uint32_t next_group;
	struct wbint_Principal *all_members;
};

static NTSTATUS wb_groups_members_next_subreq(
	struct wb_groups_members_state *state,
	TALLOC_CTX *mem_ctx, struct tevent_req **psubreq);
static void wb_groups_members_done(struct tevent_req *subreq);

static struct tevent_req *wb_groups_members_send(TALLOC_CTX *mem_ctx,
						 struct tevent_context *ev,
						 uint32_t num_groups,
						 struct wbint_Principal *groups)
{
	struct tevent_req *req, *subreq = NULL;
	struct wb_groups_members_state *state;
	NTSTATUS status;

	req = tevent_req_create(mem_ctx, &state,
				struct wb_groups_members_state);
	if (req == NULL) {
		return NULL;
	}
	state->ev = ev;
	state->groups = groups;
	state->num_groups = num_groups;
	state->next_group = 0;
	state->all_members = NULL;

	D_DEBUG("Looking up %"PRIu32" group(s).\n", num_groups);
	status = wb_groups_members_next_subreq(state, state, &subreq);
	if (tevent_req_nterror(req, status)) {
		return tevent_req_post(req, ev);
	}
	if (subreq == NULL) {
		tevent_req_done(req);
		return tevent_req_post(req, ev);
	}
	tevent_req_set_callback(subreq, wb_groups_members_done, req);
	return req;
}

static NTSTATUS wb_groups_members_next_subreq(
	struct wb_groups_members_state *state,
	TALLOC_CTX *mem_ctx, struct tevent_req **psubreq)
{
	struct tevent_req *subreq;
	struct wbint_Principal *g;

	if (state->next_group >= state->num_groups) {
		*psubreq = NULL;
		return NT_STATUS_OK;
	}

	g = &state->groups[state->next_group];
	state->next_group += 1;

	subreq = wb_lookupgroupmem_send(mem_ctx, state->ev, &g->sid, g->type);
	if (subreq == NULL) {
		return NT_STATUS_NO_MEMORY;
	}
	*psubreq = subreq;
	return NT_STATUS_OK;
}

static void wb_groups_members_done(struct tevent_req *subreq)
{
	struct tevent_req *req = tevent_req_callback_data(
		subreq, struct tevent_req);
	struct wb_groups_members_state *state = tevent_req_data(
		req, struct wb_groups_members_state);
	uint32_t i, num_all_members;
	uint32_t num_members = 0;
	struct wbint_Principal *members = NULL;
	NTSTATUS status;

	status = wb_lookupgroupmem_recv(subreq, state, &num_members, &members);
	TALLOC_FREE(subreq);

	/*
	 * In this error handling here we might have to be a bit more generous
	 * and just continue if an error occurred.
	 */

	if (!NT_STATUS_IS_OK(status)) {
		if (!NT_STATUS_EQUAL(
			    status, NT_STATUS_TRUSTED_DOMAIN_FAILURE)) {
			tevent_req_nterror(req, status);
			return;
		}
		num_members = 0;
	}

	num_all_members = talloc_array_length(state->all_members);

	D_DEBUG("Adding %"PRIu32" new member(s) to existing %"PRIu32" member(s)\n",
		num_members,
		num_all_members);

	state->all_members = talloc_realloc(
		state, state->all_members, struct wbint_Principal,
		num_all_members + num_members);
	if ((num_all_members + num_members != 0)
	    && tevent_req_nomem(state->all_members, req)) {
		return;
	}
	for (i=0; i<num_members; i++) {
		struct wbint_Principal *src, *dst;
		src = &members[i];
		dst = &state->all_members[num_all_members + i];
		sid_copy(&dst->sid, &src->sid);
		dst->name = talloc_move(state->all_members, &src->name);
		dst->type = src->type;
	}
	TALLOC_FREE(members);

	status = wb_groups_members_next_subreq(state, state, &subreq);
	if (tevent_req_nterror(req, status)) {
		return;
	}
	if (subreq == NULL) {
		tevent_req_done(req);
		return;
	}
	tevent_req_set_callback(subreq, wb_groups_members_done, req);
}

static NTSTATUS wb_groups_members_recv(struct tevent_req *req,
				       TALLOC_CTX *mem_ctx,
				       uint32_t *num_members,
				       struct wbint_Principal **members)
{
	struct wb_groups_members_state *state = tevent_req_data(
		req, struct wb_groups_members_state);
	NTSTATUS status;

	if (tevent_req_is_nterror(req, &status)) {
		return status;
	}
	*num_members = talloc_array_length(state->all_members);
	*members = talloc_move(mem_ctx, &state->all_members);
	return NT_STATUS_OK;
}


/*
 * This is the routine expanding a list of groups up to a certain level. We
 * collect the users in a rbt database: We have to add them without duplicates,
 * and the db is indexed by SID.
 */

struct wb_group_members_state {
	struct tevent_context *ev;
	int depth;
	struct db_context *users;
	struct wbint_Principal *groups;
};

static NTSTATUS wb_group_members_next_subreq(
	struct wb_group_members_state *state,
	TALLOC_CTX *mem_ctx, struct tevent_req **psubreq);
static void wb_group_members_done(struct tevent_req *subreq);

struct tevent_req *wb_group_members_send(TALLOC_CTX *mem_ctx,
					 struct tevent_context *ev,
					 const struct dom_sid *sid,
					 uint32_t num_sids,
					 enum lsa_SidType *type,
					 int max_depth)
{
	struct tevent_req *req, *subreq = NULL;
	struct wb_group_members_state *state;
	NTSTATUS status;
	struct dom_sid_buf buf;
	uint32_t i;

	req = tevent_req_create(mem_ctx, &state,
				struct wb_group_members_state);
	if (req == NULL) {
		return NULL;
	}
	D_INFO("WB command group_members start (max_depth=%d).\n", max_depth);
	for (i = 0; i < num_sids; i++) {
		D_INFO("Looking up members of group SID %s with SID type %d\n",
		       dom_sid_str_buf(&sid[i], &buf),
		       type[i]);
	}

	state->ev = ev;
	state->depth = max_depth;
	state->users = db_open_rbt(state);
	if (tevent_req_nomem(state->users, req)) {
		return tevent_req_post(req, ev);
	}

	state->groups = talloc_array(state, struct wbint_Principal, num_sids);
	if (tevent_req_nomem(state->groups, req)) {
		return tevent_req_post(req, ev);
	}

	for (i = 0; i < num_sids; i++) {
		state->groups[i].name = NULL;
		sid_copy(&state->groups[i].sid, &sid[i]);
		state->groups[i].type = type[i];
	}

	status = wb_group_members_next_subreq(state, state, &subreq);
	if (tevent_req_nterror(req, status)) {
		return tevent_req_post(req, ev);
	}
	if (subreq == NULL) {
		tevent_req_done(req);
		return tevent_req_post(req, ev);
	}
	tevent_req_set_callback(subreq, wb_group_members_done, req);
	return req;
}

static NTSTATUS wb_group_members_next_subreq(
	struct wb_group_members_state *state,
	TALLOC_CTX *mem_ctx, struct tevent_req **psubreq)
{
	struct tevent_req *subreq;

	if ((talloc_array_length(state->groups) == 0)
	    || (state->depth <= 0)) {
		*psubreq = NULL;
		D_DEBUG("Finished. The depth is %d.\n", state->depth);
		return NT_STATUS_OK;
	}
	state->depth -= 1;

	D_DEBUG("The depth is decremented to %d.\n", state->depth);
	subreq = wb_groups_members_send(
		mem_ctx, state->ev, talloc_array_length(state->groups),
		state->groups);
	if (subreq == NULL) {
		return NT_STATUS_NO_MEMORY;
	}
	*psubreq = subreq;
	return NT_STATUS_OK;
}

NTSTATUS add_member_to_db(struct db_context *db, struct dom_sid *sid,
			  const char *name)
{
	size_t len = ndr_size_dom_sid(sid, 0);
	uint8_t sidbuf[len];
	TDB_DATA key = { .dptr = sidbuf, .dsize = sizeof(sidbuf) };
	NTSTATUS status;

	sid_linearize(sidbuf, sizeof(sidbuf), sid);

	status = dbwrap_store(db, key, string_term_tdb_data(name), 0);
	return status;
}

static void wb_group_members_done(struct tevent_req *subreq)
{
	struct tevent_req *req = tevent_req_callback_data(
		subreq, struct tevent_req);
	struct wb_group_members_state *state = tevent_req_data(
		req, struct wb_group_members_state);
	uint32_t i, num_groups, new_groups;
	uint32_t num_members = 0;
	struct wbint_Principal *members = NULL;
	NTSTATUS status;

	status = wb_groups_members_recv(subreq, state, &num_members, &members);
	TALLOC_FREE(subreq);
	if (tevent_req_nterror(req, status)) {
		return;
	}

	new_groups = 0;
	for (i=0; i<num_members; i++) {
		switch (members[i].type) {
		case SID_NAME_DOM_GRP:
		case SID_NAME_ALIAS:
		case SID_NAME_WKN_GRP:
			new_groups += 1;
			break;
		default:
			/* Ignore everything else */
			break;
		}
	}

	num_groups = 0;
	TALLOC_FREE(state->groups);
	state->groups = talloc_array(state, struct wbint_Principal,
				     new_groups);

	/*
	 * Collect the users into state->users and the groups into
	 * state->groups for the next iteration.
	 */

	for (i=0; i<num_members; i++) {
		switch (members[i].type) {
		case SID_NAME_USER:
		case SID_NAME_COMPUTER: {
			/*
			 * Add a copy of members[i] to state->users
			 */
			status = add_member_to_db(state->users, &members[i].sid,
						  members[i].name);
			if (tevent_req_nterror(req, status)) {
				return;
			}

			break;
		}
		case SID_NAME_DOM_GRP:
		case SID_NAME_ALIAS:
		case SID_NAME_WKN_GRP: {
			struct wbint_Principal *g;
			/*
			 * Save members[i] for the next round
			 */
			g = &state->groups[num_groups];
			sid_copy(&g->sid, &members[i].sid);
			g->name = talloc_move(state->groups, &members[i].name);
			g->type = members[i].type;
			num_groups += 1;
			break;
		}
		default:
			/* Ignore everything else */
			break;
		}
	}

	status = wb_group_members_next_subreq(state, state, &subreq);
	if (tevent_req_nterror(req, status)) {
		return;
	}
	if (subreq == NULL) {
		tevent_req_done(req);
		return;
	}
	tevent_req_set_callback(subreq, wb_group_members_done, req);
}

NTSTATUS wb_group_members_recv(struct tevent_req *req, TALLOC_CTX *mem_ctx,
			       struct db_context **members)
{
	struct wb_group_members_state *state = tevent_req_data(
		req, struct wb_group_members_state);
	NTSTATUS status;

	D_INFO("WB command group_members end.\n");
	if (tevent_req_is_nterror(req, &status)) {
		D_WARNING("Failed with %s.\n", nt_errstr(status));
		return status;
	}
	*members = talloc_move(mem_ctx, &state->users);
	return NT_STATUS_OK;
}

/*
   RID allocation helper functions

   Copyright (C) Andrew Bartlett 2010
   Copyright (C) Andrew Tridgell 2010

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

/*
 *  Name: ldb
 *
 *  Component: RID allocation logic
 *
 *  Description: manage RID Set and RID Manager objects
 *
 */

#include "includes.h"
#include "ldb_module.h"
#include "lib/util/server_id.h"
#include "dsdb/samdb/samdb.h"
#include "dsdb/samdb/ldb_modules/util.h"
#include "lib/messaging/irpc.h"
#include "param/param.h"
#include "librpc/gen_ndr/ndr_misc.h"
#include "dsdb/samdb/ldb_modules/ridalloc.h"

/*
  Note: the RID allocation attributes in AD are very badly named. Here
  is what we think they really do:

  in RID Set object:
    - rIDPreviousAllocationPool: the pool which a DC is currently
      pulling RIDs from. Managed by client DC

    - rIDAllocationPool: the pool that the DC will switch to next,
      when rIDPreviousAllocationPool is exhausted. Managed by RID Manager.

    - rIDNextRID: the last RID allocated by this DC. Managed by client DC

  in RID Manager object:
    - rIDAvailablePool: the pool where the RID Manager gets new rID
      pools from when it gets a EXOP_RID_ALLOC getncchanges call (or
      locally when the DC is the RID Manager)
 */


/*
  make a IRPC call to the drepl task to ask it to get the RID
  Manager to give us another RID pool.

  This function just sends the message to the drepl task then
  returns immediately. It should be called well before we
  completely run out of RIDs
 */
static int ridalloc_poke_rid_manager(struct ldb_module *module)
{
	struct imessaging_context *msg;
	unsigned num_servers;
	struct server_id *servers;
	struct ldb_context *ldb = ldb_module_get_ctx(module);
	struct loadparm_context *lp_ctx =
		(struct loadparm_context *)ldb_get_opaque(ldb, "loadparm");
	TALLOC_CTX *tmp_ctx = talloc_new(module);
	NTSTATUS status;

	msg = imessaging_client_init(tmp_ctx, lp_ctx,
				    ldb_get_event_context(ldb));
	if (!msg) {
		ldb_asprintf_errstring(ldb_module_get_ctx(module),
				"Failed to send MSG_DREPL_ALLOCATE_RID, "
				"unable init client messaging context");
		DEBUG(3,(__location__ ": Failed to create messaging context\n"));
		talloc_free(tmp_ctx);
		return LDB_ERR_UNWILLING_TO_PERFORM;
	}

	status = irpc_servers_byname(msg, msg, "dreplsrv",
				     &num_servers, &servers);
	if (!NT_STATUS_IS_OK(status)) {
		ldb_asprintf_errstring(ldb_module_get_ctx(module),
				"Failed to send MSG_DREPL_ALLOCATE_RID, "
				"unable to locate dreplsrv");
		/* this means the drepl service is not running */
		talloc_free(tmp_ctx);
		return LDB_ERR_UNWILLING_TO_PERFORM;
	}

	status = imessaging_send(msg, servers[0], MSG_DREPL_ALLOCATE_RID, NULL);

	/* Only error out if an error happened, not on STATUS_MORE_ENTRIES, ie a delayed message */
	if (NT_STATUS_IS_ERR(status)) {
		struct server_id_buf idbuf;
		ldb_asprintf_errstring(ldb_module_get_ctx(module),
				"Failed to send MSG_DREPL_ALLOCATE_RID to dreplsrv at %s: %s",
				server_id_str_buf(*servers, &idbuf),
				nt_errstr(status));
		talloc_free(tmp_ctx);
		return LDB_ERR_UNWILLING_TO_PERFORM;
	}

	talloc_free(tmp_ctx);
	return LDB_SUCCESS;
}


static const char * const ridalloc_ridset_attrs[] = {
	"rIDAllocationPool",
	"rIDPreviousAllocationPool",
	"rIDNextRID",
	"rIDUsedPool",
	NULL
};

struct ridalloc_ridset_values {
	uint64_t alloc_pool;
	uint64_t prev_pool;
	uint32_t next_rid;
	uint32_t used_pool;
};

static void ridalloc_get_ridset_values(struct ldb_message *msg, struct ridalloc_ridset_values *v)
{
	v->alloc_pool = ldb_msg_find_attr_as_uint64(msg, "rIDAllocationPool", UINT64_MAX);
	v->prev_pool = ldb_msg_find_attr_as_uint64(msg, "rIDPreviousAllocationPool", UINT64_MAX);
	v->next_rid = ldb_msg_find_attr_as_uint(msg, "rIDNextRID", UINT32_MAX);
	v->used_pool = ldb_msg_find_attr_as_uint(msg, "rIDUsedPool", UINT32_MAX);
}

static int ridalloc_set_ridset_values(struct ldb_module *module,
				      struct ldb_message *msg,
				      const struct ridalloc_ridset_values *o,
				      const struct ridalloc_ridset_values *n)
{
	const uint32_t *o32, *n32;
	const uint64_t *o64, *n64;
	int ret;

#define SETUP_PTRS(field, optr, nptr, max) do { \
	optr = &o->field; \
	nptr = &n->field; \
	if (o->field == max) { \
		optr = NULL; \
	} \
	if (n->field == max) { \
		nptr = NULL; \
	} \
	if (o->field == n->field) { \
		optr = NULL; \
		nptr = NULL; \
	} \
} while(0)

	SETUP_PTRS(alloc_pool, o64, n64, UINT64_MAX);
	ret = dsdb_msg_constrainted_update_uint64(module, msg,
						  "rIDAllocationPool",
						  o64, n64);
	if (ret != LDB_SUCCESS) {
		return ret;
	}

	SETUP_PTRS(prev_pool, o64, n64, UINT64_MAX);
	ret = dsdb_msg_constrainted_update_uint64(module, msg,
						  "rIDPreviousAllocationPool",
						  o64, n64);
	if (ret != LDB_SUCCESS) {
		return ret;
	}

	SETUP_PTRS(next_rid, o32, n32, UINT32_MAX);
	ret = dsdb_msg_constrainted_update_uint32(module, msg,
						  "rIDNextRID",
						  o32, n32);
	if (ret != LDB_SUCCESS) {
		return ret;
	}

	SETUP_PTRS(used_pool, o32, n32, UINT32_MAX);
	ret = dsdb_msg_constrainted_update_uint32(module, msg,
						  "rIDUsedPool",
						  o32, n32);
	if (ret != LDB_SUCCESS) {
		return ret;
	}
#undef SETUP_PTRS

	return LDB_SUCCESS;
}

/*
  allocate a new range of RIDs in the RID Manager object
 */
static int ridalloc_rid_manager_allocate(struct ldb_module *module, struct ldb_dn *rid_manager_dn, uint64_t *new_pool,
					 struct ldb_request *parent)
{
	int ret;
	TALLOC_CTX *tmp_ctx = talloc_new(module);
	const char *attrs[] = { "rIDAvailablePool", NULL };
	uint64_t rid_pool, new_rid_pool, dc_pool;
	uint32_t rid_pool_lo, rid_pool_hi;
	struct ldb_result *res;
	struct ldb_context *ldb = ldb_module_get_ctx(module);
	const unsigned alloc_size = 500;

	ret = dsdb_module_search_dn(module, tmp_ctx, &res, rid_manager_dn,
	                            attrs, DSDB_FLAG_NEXT_MODULE, parent);
	if (ret != LDB_SUCCESS) {
		ldb_asprintf_errstring(ldb, "Failed to find rIDAvailablePool in %s - %s",
				       ldb_dn_get_linearized(rid_manager_dn), ldb_errstring(ldb));
		talloc_free(tmp_ctx);
		return ret;
	}

	rid_pool = ldb_msg_find_attr_as_uint64(res->msgs[0], "rIDAvailablePool", 0);
	rid_pool_lo = rid_pool & 0xFFFFFFFF;
	rid_pool_hi = rid_pool >> 32;
	if (rid_pool_lo >= rid_pool_hi) {
		ldb_asprintf_errstring(ldb, "Out of RIDs in RID Manager - rIDAvailablePool is %u-%u",
				       rid_pool_lo, rid_pool_hi);
		talloc_free(tmp_ctx);
		return ret;
	}

	/* lower part of new pool is the low part of the rIDAvailablePool */
	dc_pool = rid_pool_lo;

	/* allocate 500 RIDs to this DC */
	rid_pool_lo = MIN(rid_pool_hi, rid_pool_lo + alloc_size);

	/* work out upper part of new pool */
	dc_pool |= (((uint64_t)rid_pool_lo-1)<<32);

	/* and new rIDAvailablePool value */
	new_rid_pool = rid_pool_lo | (((uint64_t)rid_pool_hi)<<32);

	ret = dsdb_module_constrainted_update_uint64(module, rid_manager_dn, "rIDAvailablePool",
						     &rid_pool, &new_rid_pool, parent);
	if (ret != LDB_SUCCESS) {
		ldb_asprintf_errstring(ldb, "Failed to update rIDAvailablePool - %s",
				       ldb_errstring(ldb));
		talloc_free(tmp_ctx);
		return ret;
	}

	(*new_pool) = dc_pool;
	talloc_free(tmp_ctx);
	return LDB_SUCCESS;
}

/*
  create a RID Set object for the specified DC
 */
static int ridalloc_create_rid_set_ntds(struct ldb_module *module, TALLOC_CTX *mem_ctx,
					struct ldb_dn *rid_manager_dn,
					struct ldb_dn *ntds_dn, struct ldb_dn **dn,
					struct ldb_request *parent)
{
	TALLOC_CTX *tmp_ctx = talloc_new(mem_ctx);
	struct ldb_dn *server_dn, *machine_dn, *rid_set_dn;
	int ret;
	struct ldb_message *msg;
	struct ldb_context *ldb = ldb_module_get_ctx(module);
	static const struct ridalloc_ridset_values o = {
		.alloc_pool	= UINT64_MAX,
		.prev_pool	= UINT64_MAX,
		.next_rid	= UINT32_MAX,
		.used_pool	= UINT32_MAX,
	};
	struct ridalloc_ridset_values n = {
		.alloc_pool	= 0,
		.prev_pool	= 0,
		.next_rid	= 0,
		.used_pool	= 0,
	};
	const char *no_attrs[] = { NULL };
	struct ldb_result *res;

	/*
	  steps:

	  find the machine object for the DC
	  construct the RID Set DN
	  load rIDAvailablePool to find next available set
	  modify RID Manager object to update rIDAvailablePool
	  add the RID Set object
	  link to the RID Set object in machine object
	 */

	server_dn = ldb_dn_get_parent(tmp_ctx, ntds_dn);
	if (!server_dn) {
		talloc_free(tmp_ctx);
		return ldb_module_oom(module);
	}

	ret = dsdb_module_reference_dn(module, tmp_ctx, server_dn, "serverReference", &machine_dn, parent);
	if (ret != LDB_SUCCESS) {
		ldb_asprintf_errstring(ldb, "Failed to find serverReference in %s - %s",
				       ldb_dn_get_linearized(server_dn), ldb_errstring(ldb));
		talloc_free(tmp_ctx);
		return ret;
	}

	rid_set_dn = ldb_dn_copy(tmp_ctx, machine_dn);
	if (rid_set_dn == NULL) {
		talloc_free(tmp_ctx);
		return ldb_module_oom(module);
	}

	if (! ldb_dn_add_child_fmt(rid_set_dn, "CN=RID Set")) {
		talloc_free(tmp_ctx);
		return ldb_module_oom(module);
	}

	/* grab a pool from the RID Manager object */
	ret = ridalloc_rid_manager_allocate(module, rid_manager_dn, &n.alloc_pool, parent);
	if (ret != LDB_SUCCESS) {
		talloc_free(tmp_ctx);
		return ret;
	}

	/* create the RID Set object */
	msg = ldb_msg_new(tmp_ctx);
	msg->dn = rid_set_dn;

	ret = ldb_msg_add_string(msg, "objectClass", "rIDSet");
	if (ret != LDB_SUCCESS) {
		talloc_free(tmp_ctx);
		return ret;
	}

	ret = ridalloc_set_ridset_values(module, msg, &o, &n);
	if (ret != LDB_SUCCESS) {
		talloc_free(tmp_ctx);
		return ret;
	}

	/* we need this to go all the way to the top of the module
	 * stack, as we need all the extra attributes added (including
	 * complex ones like ntsecuritydescriptor).  We must do this
	 * as system, otherwise a user might end up owning the RID
	 * set, and that would be bad... */
	ret = dsdb_module_add(module, msg,
			      DSDB_FLAG_TOP_MODULE | DSDB_FLAG_AS_SYSTEM
			      | DSDB_MODIFY_RELAX, parent);
	if (ret != LDB_SUCCESS) {
		ldb_asprintf_errstring(ldb, "Failed to add RID Set %s - %s",
				       ldb_dn_get_linearized(msg->dn),
				       ldb_errstring(ldb));
		talloc_free(tmp_ctx);
		return ret;
	}

	/* add the rIDSetReferences link */
	msg = ldb_msg_new(tmp_ctx);
	msg->dn = machine_dn;

	/* we need the extended DN of the RID Set object for
	 * rIDSetReferences */
	ret = dsdb_module_search_dn(module, msg, &res, rid_set_dn, no_attrs,
				    DSDB_FLAG_NEXT_MODULE | DSDB_SEARCH_SHOW_DN_IN_STORAGE_FORMAT, parent);
	if (ret != LDB_SUCCESS) {
		ldb_asprintf_errstring(ldb, "Failed to find extended DN of RID Set %s - %s",
				       ldb_dn_get_linearized(msg->dn),
				       ldb_errstring(ldb));
		talloc_free(tmp_ctx);
		return ret;
	}
	rid_set_dn = res->msgs[0]->dn;


	ret = ldb_msg_add_string(msg, "rIDSetReferences", ldb_dn_get_extended_linearized(msg, rid_set_dn, 1));
	if (ret != LDB_SUCCESS) {
		talloc_free(tmp_ctx);
		return ret;
	}
	msg->elements[0].flags = LDB_FLAG_MOD_ADD;

	ret = dsdb_module_modify(module, msg,
				 DSDB_FLAG_NEXT_MODULE|DSDB_FLAG_AS_SYSTEM,
				 parent);
	if (ret != LDB_SUCCESS) {
		ldb_asprintf_errstring(ldb, "Failed to add rIDSetReferences to %s - %s",
				       ldb_dn_get_linearized(msg->dn),
				       ldb_errstring(ldb));
		talloc_free(tmp_ctx);
		return ret;
	}

	(*dn) = talloc_steal(mem_ctx, rid_set_dn);

	talloc_free(tmp_ctx);
	return LDB_SUCCESS;
}


/*
  create a RID Set object for this DC
 */
int ridalloc_create_own_rid_set(struct ldb_module *module, TALLOC_CTX *mem_ctx,
				struct ldb_dn **dn, struct ldb_request *parent)
{
	TALLOC_CTX *tmp_ctx = talloc_new(mem_ctx);
	struct ldb_dn *rid_manager_dn, *fsmo_role_dn;
	int ret;
	struct ldb_context *ldb = ldb_module_get_ctx(module);
	struct GUID fsmo_role_guid;
	const struct GUID *our_ntds_guid;
	NTSTATUS status;

	/* work out who is the RID Manager */
	ret = dsdb_module_rid_manager_dn(module, tmp_ctx, &rid_manager_dn, parent);
	if (ret != LDB_SUCCESS) {
		ldb_asprintf_errstring(ldb, "Failed to find RID Manager object - %s",
				       ldb_errstring(ldb));
		talloc_free(tmp_ctx);
		return ret;
	}

	/* find the DN of the RID Manager */
	ret = dsdb_module_reference_dn(module, tmp_ctx, rid_manager_dn, "fSMORoleOwner", &fsmo_role_dn, parent);
	if (ret != LDB_SUCCESS) {
		ldb_asprintf_errstring(ldb, "Failed to find fSMORoleOwner in RID Manager object - %s",
				       ldb_errstring(ldb));
		talloc_free(tmp_ctx);
		return ret;
	}

	status = dsdb_get_extended_dn_guid(fsmo_role_dn, &fsmo_role_guid, "GUID");
	if (!NT_STATUS_IS_OK(status)) {
		talloc_free(tmp_ctx);
		return ldb_operr(ldb_module_get_ctx(module));
	}

	/* clear the cache so we don't get an old ntds_guid */
	if (ldb_set_opaque(ldb, "cache.ntds_guid", NULL) != LDB_SUCCESS) {
		talloc_free(tmp_ctx);
		return ldb_operr(ldb_module_get_ctx(module));
	}

	our_ntds_guid = samdb_ntds_objectGUID(ldb_module_get_ctx(module));
	if (!our_ntds_guid) {
		talloc_free(tmp_ctx);
		return ldb_operr(ldb_module_get_ctx(module));
	}

	if (!GUID_equal(&fsmo_role_guid, our_ntds_guid)) {
		ret = ridalloc_poke_rid_manager(module);
		if (ret != LDB_SUCCESS) {
			ldb_asprintf_errstring(ldb,
					"Request for remote creation of "
					"RID Set for this DC failed: %s",
					ldb_errstring(ldb));
		} else {
			ldb_asprintf_errstring(ldb,
					"Remote RID Set creation needed");
		}
		talloc_free(tmp_ctx);
		return LDB_ERR_UNWILLING_TO_PERFORM;
	}

	ret = ridalloc_create_rid_set_ntds(module, mem_ctx, rid_manager_dn, fsmo_role_dn, dn, parent);
	talloc_free(tmp_ctx);
	return ret;
}

/*
  get a new RID pool for ourselves
  also returns the first rid for the new pool
 */

int ridalloc_new_own_pool(struct ldb_module *module, uint64_t *new_pool, struct ldb_request *parent)
{
	TALLOC_CTX *tmp_ctx = talloc_new(module);
	struct ldb_dn *rid_manager_dn, *fsmo_role_dn;
	int ret;
	struct ldb_context *ldb = ldb_module_get_ctx(module);
	bool is_us;

	/* work out who is the RID Manager */
	ret = dsdb_module_rid_manager_dn(module, tmp_ctx, &rid_manager_dn, parent);
	if (ret != LDB_SUCCESS) {
		ldb_asprintf_errstring(ldb, "Failed to find RID Manager object - %s",
				       ldb_errstring(ldb));
		talloc_free(tmp_ctx);
		return ret;
	}

	/* find the DN of the RID Manager */
	ret = dsdb_module_reference_dn(module, tmp_ctx, rid_manager_dn, "fSMORoleOwner", &fsmo_role_dn, parent);
	if (ret != LDB_SUCCESS) {
		ldb_asprintf_errstring(ldb, "Failed to find fSMORoleOwner in RID Manager object - %s",
				       ldb_errstring(ldb));
		talloc_free(tmp_ctx);
		return ret;
	}

	ret = samdb_dn_is_our_ntdsa(ldb, fsmo_role_dn, &is_us);
	if (ret != LDB_SUCCESS) {
		ldb_asprintf_errstring(ldb, "Failed to confirm if our ntdsDsa is %s: %s",
				       ldb_dn_get_linearized(fsmo_role_dn), ldb_errstring(ldb));
		talloc_free(tmp_ctx);
		return ret;
	}
	
	if (!is_us) {
		ret = ridalloc_poke_rid_manager(module);
		if (ret != LDB_SUCCESS) {
			ldb_asprintf_errstring(ldb, "Request for remote refresh of RID Set allocation failed: %s",
					       ldb_errstring(ldb));
		} else {
			ldb_asprintf_errstring(ldb, "Remote RID Set refresh needed");
		}
		talloc_free(tmp_ctx);
		return LDB_ERR_UNWILLING_TO_PERFORM;
	}

	/* grab a pool from the RID Manager object */
	ret = ridalloc_rid_manager_allocate(module, rid_manager_dn, new_pool, parent);
	if (ret != LDB_SUCCESS) {
		talloc_free(tmp_ctx);
		return ret;
	}

	talloc_free(tmp_ctx);
	return ret;
}


/* allocate a RID using our RID Set
   If we run out of RIDs then allocate a new pool
   either locally or by contacting the RID Manager
*/
int ridalloc_allocate_rid(struct ldb_module *module, uint32_t *rid, struct ldb_request *parent)
{
	struct ldb_context *ldb;
	int ret;
	struct ldb_dn *rid_set_dn;
	struct ldb_result *res;
	struct ldb_message *msg;
	struct ridalloc_ridset_values oridset;
	struct ridalloc_ridset_values nridset;
	uint32_t prev_pool_lo, prev_pool_hi;
	TALLOC_CTX *tmp_ctx = talloc_new(module);

	(*rid) = 0;
	ldb = ldb_module_get_ctx(module);

	ret = samdb_rid_set_dn(ldb, tmp_ctx, &rid_set_dn);
	if (ret == LDB_ERR_NO_SUCH_ATTRIBUTE) {
		ret = ridalloc_create_own_rid_set(module, tmp_ctx, &rid_set_dn, parent);
	}
	if (ret != LDB_SUCCESS) {
		ldb_asprintf_errstring(ldb, __location__ ": No RID Set DN - %s",
				       ldb_errstring(ldb));
		talloc_free(tmp_ctx);
		return ret;
	}

	ret = dsdb_module_search_dn(module, tmp_ctx, &res, rid_set_dn,
				    ridalloc_ridset_attrs, DSDB_FLAG_NEXT_MODULE, parent);
	if (ret != LDB_SUCCESS) {
		ldb_asprintf_errstring(ldb, __location__ ": No RID Set %s",
				       ldb_dn_get_linearized(rid_set_dn));
		talloc_free(tmp_ctx);
		return ret;
	}

	ridalloc_get_ridset_values(res->msgs[0], &oridset);
	if (oridset.alloc_pool == UINT64_MAX) {
		ldb_asprintf_errstring(ldb, __location__ ": Bad RID Set %s",
				       ldb_dn_get_linearized(rid_set_dn));
		talloc_free(tmp_ctx);
		return LDB_ERR_OPERATIONS_ERROR;
	}

	nridset = oridset;

	/*
	 * If we never used a pool, setup out first pool
	 */
	if (nridset.prev_pool == UINT64_MAX ||
	    nridset.next_rid == UINT32_MAX) {
		nridset.prev_pool = nridset.alloc_pool;
		nridset.next_rid = nridset.prev_pool & 0xFFFFFFFF;
	} else {
		nridset.next_rid += 1;
	}

	/*
	 * Now check if our current pool is still usable
	 */
	prev_pool_lo = nridset.prev_pool & 0xFFFFFFFF;
	prev_pool_hi = nridset.prev_pool >> 32;
	if (nridset.next_rid > prev_pool_hi) {
		/*
		 * We need a new pool, check if we already have a new one
		 * Otherwise we need to get a new pool.
		 */
		if (nridset.alloc_pool == nridset.prev_pool) {
			/*
			 * if we are the RID Manager,
			 * we can get a new pool locally.
			 * Otherwise we fail the operation and
			 * ask async for a new pool.
			 */
			ret = ridalloc_new_own_pool(module, &nridset.alloc_pool, parent);
			if (ret != LDB_SUCCESS) {
				ldb_asprintf_errstring(ldb, "NO RID values available: %s",
						       ldb_errstring(ldb));
				talloc_free(tmp_ctx);
				return ret;
			}
		}

		/*
		 * increment the rIDUsedPool attribute
		 *
		 * Note: w2k8r2 doesn't update this attribute,
		 *       at least if it's itself the rid master.
		 */
		nridset.used_pool += 1;

		/* now use the new pool */
		nridset.prev_pool = nridset.alloc_pool;
		prev_pool_lo = nridset.prev_pool & 0xFFFFFFFF;
		prev_pool_hi = nridset.prev_pool >> 32;
		nridset.next_rid = prev_pool_lo;
	}

	if (nridset.next_rid < prev_pool_lo || nridset.next_rid > prev_pool_hi) {
		ldb_asprintf_errstring(ldb, __location__ ": Bad rid chosen %u from range %u-%u",
				       (unsigned)nridset.next_rid,
				       (unsigned)prev_pool_lo,
				       (unsigned)prev_pool_hi);
		talloc_free(tmp_ctx);
		return LDB_ERR_OPERATIONS_ERROR;
	}

	/*
	 * if we are half-exhausted then try to get a new pool.
	 */
	if (nridset.next_rid > (prev_pool_hi + prev_pool_lo)/2 &&
	    nridset.alloc_pool == nridset.prev_pool) {
		/*
		 * if we are the RID Manager,
		 * we can get a new pool locally.
		 * Otherwise we fail the operation and
		 * ask async for a new pool.
		 */
		ret = ridalloc_new_own_pool(module, &nridset.alloc_pool, parent);
		if (ret == LDB_ERR_UNWILLING_TO_PERFORM) {
			ldb_reset_err_string(ldb);
			ret = LDB_SUCCESS;
		}
		if (ret != LDB_SUCCESS) {
			talloc_free(tmp_ctx);
			return ret;
		}
	}

	/*
	 * update the values
	 */
	msg = ldb_msg_new(tmp_ctx);
	if (msg == NULL) {
		return ldb_module_oom(module);
	}
	msg->dn = rid_set_dn;

	ret = ridalloc_set_ridset_values(module, msg,
					 &oridset, &nridset);
	if (ret != LDB_SUCCESS) {
		talloc_free(tmp_ctx);
		return ret;
	}

	ret = dsdb_module_modify(module, msg, DSDB_FLAG_NEXT_MODULE|DSDB_FLAG_AS_SYSTEM, parent);
	if (ret != LDB_SUCCESS) {
		talloc_free(tmp_ctx);
		return ret;
	}

	talloc_free(tmp_ctx);
	*rid = nridset.next_rid;
	return LDB_SUCCESS;
}


/*
  called by DSDB_EXTENDED_ALLOCATE_RID_POOL extended operation in samldb

  This is for the DRS server to allocate a RID Pool for another server.

  Called by another server over DRS (which calls this extended
  operation), it runs on the RID Manager only.
 */
int ridalloc_allocate_rid_pool_fsmo(struct ldb_module *module, struct dsdb_fsmo_extended_op *exop,
				    struct ldb_request *parent)
{
	struct ldb_dn *ntds_dn, *server_dn, *machine_dn, *rid_set_dn;
	struct ldb_dn *rid_manager_dn;
	TALLOC_CTX *tmp_ctx = talloc_new(module);
	int ret;
	struct ldb_context *ldb = ldb_module_get_ctx(module);
	struct ldb_result *res;
	struct ldb_message *msg;
	struct ridalloc_ridset_values oridset, nridset;

	ret = dsdb_module_dn_by_guid(module, tmp_ctx, &exop->destination_dsa_guid, &ntds_dn, parent);
	if (ret != LDB_SUCCESS) {
		ldb_asprintf_errstring(ldb, __location__ ": Unable to find NTDS object for guid %s - %s\n",
				       GUID_string(tmp_ctx, &exop->destination_dsa_guid), ldb_errstring(ldb));
		talloc_free(tmp_ctx);
		return ret;
	}

	server_dn = ldb_dn_get_parent(tmp_ctx, ntds_dn);
	if (!server_dn) {
		talloc_free(tmp_ctx);
		return ldb_module_oom(module);
	}

	ret = dsdb_module_reference_dn(module, tmp_ctx, server_dn, "serverReference", &machine_dn, parent);
	if (ret != LDB_SUCCESS) {
		ldb_asprintf_errstring(ldb, __location__ ": Failed to find serverReference in %s - %s",
				       ldb_dn_get_linearized(server_dn), ldb_errstring(ldb));
		talloc_free(tmp_ctx);
		return ret;
	}

	ret = dsdb_module_rid_manager_dn(module, tmp_ctx, &rid_manager_dn, parent);
	if (ret != LDB_SUCCESS) {
		ldb_asprintf_errstring(ldb, __location__ ": Failed to find RID Manager object - %s",
				       ldb_errstring(ldb));
		talloc_free(tmp_ctx);
		return ret;
	}

	ret = dsdb_module_reference_dn(module, tmp_ctx, machine_dn, "rIDSetReferences", &rid_set_dn, parent);
	if (ret == LDB_ERR_NO_SUCH_ATTRIBUTE) {
		ret = ridalloc_create_rid_set_ntds(module, tmp_ctx, rid_manager_dn, ntds_dn, &rid_set_dn, parent);
		talloc_free(tmp_ctx);
		return ret;
	}

	if (ret != LDB_SUCCESS) {
		ldb_asprintf_errstring(ldb, "Failed to find rIDSetReferences in %s - %s",
				       ldb_dn_get_linearized(machine_dn), ldb_errstring(ldb));
		talloc_free(tmp_ctx);
		return ret;
	}

	ret = dsdb_module_search_dn(module, tmp_ctx, &res, rid_set_dn,
				    ridalloc_ridset_attrs, DSDB_FLAG_NEXT_MODULE, parent);
	if (ret != LDB_SUCCESS) {
		ldb_asprintf_errstring(ldb, __location__ ": No RID Set %s",
				       ldb_dn_get_linearized(rid_set_dn));
		talloc_free(tmp_ctx);
		return ret;
	}

	ridalloc_get_ridset_values(res->msgs[0], &oridset);
	if (oridset.alloc_pool == UINT64_MAX) {
		ldb_asprintf_errstring(ldb, __location__ ": Bad RID Set %s",
				       ldb_dn_get_linearized(rid_set_dn));
		talloc_free(tmp_ctx);
		return LDB_ERR_OPERATIONS_ERROR;
	}

	nridset = oridset;

	if (exop->fsmo_info != 0) {

		if (nridset.alloc_pool != exop->fsmo_info) {
			/* it has already been updated */
			DEBUG(2,(__location__ ": rIDAllocationPool fsmo_info mismatch - already changed (0x%llx 0x%llx)\n",
				 (unsigned long long)exop->fsmo_info,
				 (unsigned long long)nridset.alloc_pool));
			talloc_free(tmp_ctx);
			return LDB_SUCCESS;
		}
	}

	/* grab a pool from the RID Manager object */
	ret = ridalloc_rid_manager_allocate(module, rid_manager_dn, &nridset.alloc_pool, parent);
	if (ret != LDB_SUCCESS) {
		talloc_free(tmp_ctx);
		return ret;
	}

	/*
	 * update the values
	 */
	msg = ldb_msg_new(tmp_ctx);
	if (msg == NULL) {
		return ldb_module_oom(module);
	}
	msg->dn = rid_set_dn;

	ret = ridalloc_set_ridset_values(module, msg,
					 &oridset, &nridset);
	if (ret != LDB_SUCCESS) {
		talloc_free(tmp_ctx);
		return ret;
	}

	ret = dsdb_module_modify(module, msg, DSDB_FLAG_NEXT_MODULE|DSDB_FLAG_AS_SYSTEM, parent);
	if (ret != LDB_SUCCESS) {
		ldb_asprintf_errstring(ldb, "Failed to modify RID Set object %s - %s",
				       ldb_dn_get_linearized(rid_set_dn), ldb_errstring(ldb));
		talloc_free(tmp_ctx);
		return ret;
	}

	talloc_free(tmp_ctx);
	return LDB_SUCCESS;
}

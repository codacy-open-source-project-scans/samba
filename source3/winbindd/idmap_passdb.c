/*
   Unix SMB/CIFS implementation.

   idmap PASSDB backend

   Copyright (C) Simo Sorce 2006

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
#include "idmap.h"
#include "passdb.h"

#undef DBGC_CLASS
#define DBGC_CLASS DBGC_IDMAP

/*****************************
 Initialise idmap database.
*****************************/

static NTSTATUS idmap_pdb_init(struct idmap_domain *dom)
{
	return NT_STATUS_OK;
}

/**********************************
 lookup a set of unix ids.
**********************************/

static NTSTATUS idmap_pdb_unixids_to_sids(struct idmap_domain *dom, struct id_map **ids)
{
	int i;

	for (i = 0; ids[i]; i++) {
		/* unmapped by default */
		ids[i]->status = ID_UNMAPPED;

		if (pdb_id_to_sid(&ids[i]->xid, ids[i]->sid)) {
			ids[i]->status = ID_MAPPED;
		}
	}

	return NT_STATUS_OK;
}

/**********************************
 lookup a set of sids.
**********************************/

static NTSTATUS idmap_pdb_sids_to_unixids(struct idmap_domain *dom, struct id_map **ids)
{
	int i;

	for (i = 0; ids[i]; i++) {
		if (pdb_sid_to_id(ids[i]->sid, &ids[i]->xid)) {
			ids[i]->status = ID_MAPPED;
		} else {
			/* Query Failed */
			ids[i]->status = ID_UNMAPPED;
		}
	}

	return NT_STATUS_OK;
}

static const struct idmap_methods passdb_methods = {
	.init = idmap_pdb_init,
	.unixids_to_sids = idmap_pdb_unixids_to_sids,
	.sids_to_unixids = idmap_pdb_sids_to_unixids,
};

NTSTATUS idmap_passdb_init(TALLOC_CTX *mem_ctx)
{
	return smb_register_idmap(SMB_IDMAP_INTERFACE_VERSION, "passdb", &passdb_methods);
}

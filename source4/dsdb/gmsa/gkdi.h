/*
   Unix SMB/CIFS implementation.
   Group Key Distribution Protocol functions

   Copyright (C) Catalyst.Net Ltd 2024

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef DSDB_GMSA_GKDI_H
#define DSDB_GMSA_GKDI_H

#include <talloc.h>
#include "lib/util/data_blob.h"
#include "lib/util/time.h"
#include "libcli/util/ntstatus.h"
#include "librpc/gen_ndr/misc.h"

struct ldb_message;

/*
 * Create and return a new GKDI root key.
 *
 * This function goes unused.
 */
struct ldb_context;
int gkdi_new_root_key(TALLOC_CTX *mem_ctx,
		      struct ldb_context *const ldb,
		      const NTTIME current_time,
		      const NTTIME use_start_time,
		      struct GUID *const root_key_id_out,
		      const struct ldb_message **const root_key_out);


#endif /* DSDB_GMSA_GKDI_H */

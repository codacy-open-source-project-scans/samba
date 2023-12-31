/*
   Unix SMB/CIFS implementation.
   Main SMB server routines

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

#ifndef _SMBD_SMBD_H
#define _SMBD_SMBD_H

struct dptr_struct;

#include "smb_acls.h"
#include "vfs.h"
#include "smbd/proto.h"
#include "locking/proto.h"
#include "locking/share_mode_lock.h"
#include "smbd/fd_handle.h"
#if defined(WITH_SMB1SERVER)
#include "smbd/smb1_message.h"
#include "smbd/smb1_sesssetup.h"
#include "smbd/smb1_lanman.h"
#include "smbd/smb1_aio.h"
#include "smbd/smb1_ipc.h"
#include "smbd/smb1_negprot.h"
#include "smbd/smb1_nttrans.h"
#include "smbd/smb1_oplock.h"
#include "smbd/smb1_pipes.h"
#include "smbd/smb1_reply.h"
#include "smbd/smb1_service.h"
#include "smbd/smb1_signing.h"
#include "smbd/smb1_process.h"
#include "smbd/smb1_utils.h"
#include "smbd/smb1_trans2.h"
#endif

struct trans_state {
	struct trans_state *next, *prev;
	uint64_t vuid; /* SMB2 compat */
	uint64_t mid;

	uint32_t max_param_return;
	uint32_t max_data_return;
	uint32_t max_setup_return;

	uint8_t cmd;		/* SMBtrans or SMBtrans2 */

	char *name;		/* for trans requests */
	uint16_t call;		/* for trans2 and nttrans requests */

	bool close_on_completion;
	bool one_way;

	unsigned int setup_count;
	uint16_t *setup;

	size_t received_data;
	size_t received_param;

	size_t total_param;
	char *param;

	size_t total_data;
	char *data;
};

/*
 * unix_convert_flags
 */
/* UCF_SAVE_LCOMP 0x00000001 is no longer used. */
/* UCF_ALWAYS_ALLOW_WCARD_LCOMP	0x00000002 is no longer used. */
/* UCF_COND_ALLOW_WCARD_LCOMP 0x00000004 is no longer used. */
#define UCF_POSIX_PATHNAMES		0x00000008
/* #define UCF_UNIX_NAME_LOOKUP 0x00000010 is no longer used. */
#define UCF_PREP_CREATEFILE		0x00000020
/*
 * Return a non-fsp smb_fname for a symlink
 */
#define UCF_LCOMP_LNK_OK                0x00000040
/*
 * Use the same bit as FLAGS2_REPARSE_PATH
 * which means the same thing.
 */
#define UCF_GMT_PATHNAME		0x00000400
/*
 * Use the same bit as FLAGS2_DFS_PATHNAMES
 * which means the same thing.
 */
#define UCF_DFS_PATHNAME		0x00001000

#endif /* _SMBD_SMBD_H */

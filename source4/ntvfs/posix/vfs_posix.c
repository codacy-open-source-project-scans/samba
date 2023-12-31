/* 
   Unix SMB/CIFS implementation.

   POSIX NTVFS backend

   Copyright (C) Andrew Tridgell 2004

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
  this implements most of the POSIX NTVFS backend
  This is the default backend
*/

#include "includes.h"
#include "vfs_posix.h"
#include "librpc/gen_ndr/security.h"
#include <tdb.h>
#include "lib/tdb_wrap/tdb_wrap.h"
#include "libcli/security/security.h"
#include "lib/events/events.h"
#include "param/param.h"
#include "lib/util/idtree.h"

/*
  setup config options for a posix share
*/
static void pvfs_setup_options(struct pvfs_state *pvfs)
{
	struct share_config *scfg = pvfs->ntvfs->ctx->config;
	char *eadb;
	char *xattr_backend;
	bool def_perm_override = false;

	if (share_bool_option(scfg, SHARE_MAP_HIDDEN, SHARE_MAP_HIDDEN_DEFAULT))
		pvfs->flags |= PVFS_FLAG_MAP_HIDDEN;
	if (share_bool_option(scfg, SHARE_MAP_ARCHIVE, SHARE_MAP_ARCHIVE_DEFAULT))
		pvfs->flags |= PVFS_FLAG_MAP_ARCHIVE;
	if (share_bool_option(scfg, SHARE_MAP_SYSTEM, SHARE_MAP_SYSTEM_DEFAULT))
		pvfs->flags |= PVFS_FLAG_MAP_SYSTEM;
	if (share_bool_option(scfg, SHARE_READONLY, SHARE_READONLY_DEFAULT))
		pvfs->flags |= PVFS_FLAG_READONLY;
	if (share_bool_option(scfg, SHARE_STRICT_SYNC, SHARE_STRICT_SYNC_DEFAULT))
		pvfs->flags |= PVFS_FLAG_STRICT_SYNC;
	if (share_bool_option(scfg, SHARE_STRICT_LOCKING, SHARE_STRICT_LOCKING_DEFAULT))
		pvfs->flags |= PVFS_FLAG_STRICT_LOCKING;
	if (share_bool_option(scfg, SHARE_CI_FILESYSTEM, SHARE_CI_FILESYSTEM_DEFAULT))
		pvfs->flags |= PVFS_FLAG_CI_FILESYSTEM;
	if (share_bool_option(scfg, PVFS_FAKE_OPLOCKS, PVFS_FAKE_OPLOCKS_DEFAULT))
		pvfs->flags |= PVFS_FLAG_FAKE_OPLOCKS;

#if defined(O_DIRECTORY) && defined(O_NOFOLLOW)
	/* set PVFS_PERM_OVERRIDE by default only if the system
	 * supports the necessary capabilities to make it secure
	 */
	def_perm_override = true;
#endif
	if (share_bool_option(scfg, PVFS_PERM_OVERRIDE, def_perm_override))
		pvfs->flags |= PVFS_FLAG_PERM_OVERRIDE;

	/* file perm options */
	pvfs->options.create_mask       = share_int_option(scfg,
							   SHARE_CREATE_MASK,
							   SHARE_CREATE_MASK_DEFAULT);
	pvfs->options.dir_mask          = share_int_option(scfg,
							   SHARE_DIR_MASK,
							   SHARE_DIR_MASK_DEFAULT);
	pvfs->options.force_dir_mode    = share_int_option(scfg,
							   SHARE_FORCE_DIR_MODE,
							   SHARE_FORCE_DIR_MODE_DEFAULT);
	pvfs->options.force_create_mode = share_int_option(scfg,
							   SHARE_FORCE_CREATE_MODE,
							   SHARE_FORCE_CREATE_MODE_DEFAULT);
	/* this must be a power of 2 */
	pvfs->alloc_size_rounding = share_int_option(scfg,
							PVFS_ALLOCATION_ROUNDING,
							PVFS_ALLOCATION_ROUNDING_DEFAULT);

	pvfs->search.inactivity_time = share_int_option(scfg,
							PVFS_SEARCH_INACTIVITY,
							PVFS_SEARCH_INACTIVITY_DEFAULT);

#ifdef HAVE_XATTR_SUPPORT
	if (share_bool_option(scfg, PVFS_XATTR, PVFS_XATTR_DEFAULT))
		pvfs->flags |= PVFS_FLAG_XATTR_ENABLE;
#endif

	pvfs->sharing_violation_delay = share_int_option(scfg,
							PVFS_SHARE_DELAY,
							PVFS_SHARE_DELAY_DEFAULT);

	pvfs->oplock_break_timeout = share_int_option(scfg,
						      PVFS_OPLOCK_TIMEOUT,
						      PVFS_OPLOCK_TIMEOUT_DEFAULT);

	pvfs->writetime_delay = share_int_option(scfg,
						 PVFS_WRITETIME_DELAY,
						 PVFS_WRITETIME_DELAY_DEFAULT);

	pvfs->share_name = talloc_strdup(pvfs, scfg->name);

	pvfs->fs_attribs = 
		FS_ATTR_CASE_SENSITIVE_SEARCH | 
		FS_ATTR_CASE_PRESERVED_NAMES |
		FS_ATTR_UNICODE_ON_DISK;

	/* allow xattrs to be stored in a external tdb */
	eadb = share_string_option(pvfs, scfg, PVFS_EADB, NULL);
	if (eadb != NULL) {
		pvfs->ea_db = tdb_wrap_open(
			pvfs, eadb, 50000,
			lpcfg_tdb_flags(pvfs->ntvfs->ctx->lp_ctx, TDB_DEFAULT),
			O_RDWR|O_CREAT, 0600);
		if (pvfs->ea_db != NULL) {
			pvfs->flags |= PVFS_FLAG_XATTR_ENABLE;
		} else {
			DEBUG(0,("Failed to open eadb '%s' - %s\n",
				 eadb, strerror(errno)));
			pvfs->flags &= ~PVFS_FLAG_XATTR_ENABLE;
		}
		TALLOC_FREE(eadb);
	}

	if (pvfs->flags & PVFS_FLAG_XATTR_ENABLE) {
		pvfs->fs_attribs |= FS_ATTR_NAMED_STREAMS;
	}
	if (pvfs->flags & PVFS_FLAG_XATTR_ENABLE) {
		pvfs->fs_attribs |= FS_ATTR_PERSISTANT_ACLS;
	}

	pvfs->sid_cache.creator_owner = dom_sid_parse_talloc(pvfs, SID_CREATOR_OWNER);
	pvfs->sid_cache.creator_group = dom_sid_parse_talloc(pvfs, SID_CREATOR_GROUP);

	/* check if the system really supports xattrs */
	if (pvfs->flags & PVFS_FLAG_XATTR_ENABLE) {
		pvfs_xattr_probe(pvfs);
	}

	/* enable an ACL backend */
	xattr_backend = share_string_option(pvfs, scfg, PVFS_ACL, "xattr");
	pvfs->acl_ops = pvfs_acl_backend_byname(xattr_backend);
	TALLOC_FREE(xattr_backend);
}

static int pvfs_state_destructor(struct pvfs_state *pvfs)
{
	struct pvfs_file *f, *fn;
	struct pvfs_search_state *s, *sn;

	/* 
	 * make sure we cleanup files and searches before anything else
	 * because there destructors need to access the pvfs_state struct
	 */
	for (f=pvfs->files.list; f; f=fn) {
		fn = f->next;
		talloc_free(f);
	}

	for (s=pvfs->search.list; s; s=sn) {
		sn = s->next;
		talloc_free(s);
	}

	return 0;
}

/*
  connect to a share - used when a tree_connect operation comes
  in. For a disk based backend we needs to ensure that the base
  directory exists (tho it doesn't need to be accessible by the user,
  that comes later)
*/
static NTSTATUS pvfs_connect(struct ntvfs_module_context *ntvfs,
			     struct ntvfs_request *req,
			     union smb_tcon* tcon)
{
	struct pvfs_state *pvfs;
	struct stat st;
	char *base_directory;
	NTSTATUS status;
	const char *sharename;

	switch (tcon->generic.level) {
	case RAW_TCON_TCON:
		sharename = tcon->tcon.in.service;
		break;
	case RAW_TCON_TCONX:
		sharename = tcon->tconx.in.path;
		break;
	case RAW_TCON_SMB2:
		sharename = tcon->smb2.in.path;
		break;
	default:
		return NT_STATUS_INVALID_LEVEL;
	}

	if (strncmp(sharename, "\\\\", 2) == 0) {
		char *p = strchr(sharename+2, '\\');
		if (p) {
			sharename = p + 1;
		}
	}

	/*
	 * TODO: call this from ntvfs_posix_init()
	 *       but currently we don't have a lp_ctx there
	 */
	status = pvfs_acl_init();
	NT_STATUS_NOT_OK_RETURN(status);

	pvfs = talloc_zero(ntvfs, struct pvfs_state);
	NT_STATUS_HAVE_NO_MEMORY(pvfs);

	/* for simplicity of path construction, remove any trailing slash now */
	base_directory = share_string_option(pvfs, ntvfs->ctx->config, SHARE_PATH, "");
	NT_STATUS_HAVE_NO_MEMORY(base_directory);
	if (strcmp(base_directory, "/") != 0) {
		trim_string(base_directory, NULL, "/");
	}

	pvfs->ntvfs = ntvfs;
	pvfs->base_directory = base_directory;

	/* the directory must exist. Note that we deliberately don't
	   check that it is readable */
	if (stat(pvfs->base_directory, &st) != 0 || !S_ISDIR(st.st_mode)) {
		DEBUG(0,("pvfs_connect: '%s' is not a directory, when connecting to [%s]\n", 
			 pvfs->base_directory, sharename));
		return NT_STATUS_BAD_NETWORK_NAME;
	}

	ntvfs->ctx->fs_type = talloc_strdup(ntvfs->ctx, "NTFS");
	NT_STATUS_HAVE_NO_MEMORY(ntvfs->ctx->fs_type);

	ntvfs->ctx->dev_type = talloc_strdup(ntvfs->ctx, "A:");
	NT_STATUS_HAVE_NO_MEMORY(ntvfs->ctx->dev_type);

	if (tcon->generic.level == RAW_TCON_TCONX) {
		tcon->tconx.out.fs_type = ntvfs->ctx->fs_type;
		tcon->tconx.out.dev_type = ntvfs->ctx->dev_type;
	}

	ntvfs->private_data = pvfs;

	pvfs->brl_context = brlock_init(pvfs, 
				     pvfs->ntvfs->ctx->server_id,
				     pvfs->ntvfs->ctx->lp_ctx,
				     pvfs->ntvfs->ctx->msg_ctx);
	if (pvfs->brl_context == NULL) {
		return NT_STATUS_INTERNAL_DB_CORRUPTION;
	}

	pvfs->odb_context = odb_init(pvfs, pvfs->ntvfs->ctx);
	if (pvfs->odb_context == NULL) {
		return NT_STATUS_INTERNAL_DB_CORRUPTION;
	}

	/* allow this to be NULL - we just disable change notify */
	pvfs->notify_context = notify_init(pvfs, 
					   pvfs->ntvfs->ctx->server_id,  
					   pvfs->ntvfs->ctx->msg_ctx, 
					   pvfs->ntvfs->ctx->lp_ctx,
					   pvfs->ntvfs->ctx->event_ctx,
					   pvfs->ntvfs->ctx->config);

	/* allocate the search handle -> ptr tree */
	pvfs->search.idtree = idr_init(pvfs);
	NT_STATUS_HAVE_NO_MEMORY(pvfs->search.idtree);

	status = pvfs_mangle_init(pvfs);
	NT_STATUS_NOT_OK_RETURN(status);

	pvfs_setup_options(pvfs);

	talloc_set_destructor(pvfs, pvfs_state_destructor);

#ifdef SIGXFSZ
	/* who had the stupid idea to generate a signal on a large
	   file write instead of just failing it!? */
	BlockSignals(true, SIGXFSZ);
#endif

	return NT_STATUS_OK;
}

/*
  disconnect from a share
*/
static NTSTATUS pvfs_disconnect(struct ntvfs_module_context *ntvfs)
{
	return NT_STATUS_OK;
}

/*
  check if a directory exists
*/
static NTSTATUS pvfs_chkpath(struct ntvfs_module_context *ntvfs,
			     struct ntvfs_request *req,
			     union smb_chkpath *cp)
{
	struct pvfs_state *pvfs = talloc_get_type(ntvfs->private_data,
				  struct pvfs_state);
	struct pvfs_filename *name;
	NTSTATUS status;

	/* resolve the cifs name to a posix name */
	status = pvfs_resolve_name(pvfs, req, cp->chkpath.in.path, 0, &name);
	NT_STATUS_NOT_OK_RETURN(status);

	if (!name->exists) {
		return NT_STATUS_OBJECT_NAME_NOT_FOUND;
	}

	if (!S_ISDIR(name->st.st_mode)) {
		return NT_STATUS_NOT_A_DIRECTORY;
	}

	return NT_STATUS_OK;
}

/*
  copy a set of files
*/
static NTSTATUS pvfs_copy(struct ntvfs_module_context *ntvfs,
			  struct ntvfs_request *req, struct smb_copy *cp)
{
	DEBUG(0,("pvfs_copy not implemented\n"));
	return NT_STATUS_NOT_SUPPORTED;
}

/*
  return print queue info
*/
static NTSTATUS pvfs_lpq(struct ntvfs_module_context *ntvfs,
			 struct ntvfs_request *req, union smb_lpq *lpq)
{
	return NT_STATUS_NOT_SUPPORTED;
}

/* SMBtrans - not used on file shares */
static NTSTATUS pvfs_trans(struct ntvfs_module_context *ntvfs,
			   struct ntvfs_request *req, struct smb_trans2 *trans2)
{
	return NT_STATUS_ACCESS_DENIED;
}

/*
  initialise the POSIX disk backend, registering ourselves with the ntvfs subsystem
 */
NTSTATUS ntvfs_posix_init(TALLOC_CTX *ctx)
{
	NTSTATUS ret;
	struct ntvfs_ops ops;
	NTVFS_CURRENT_CRITICAL_SIZES(vers);

	ZERO_STRUCT(ops);

	ops.type = NTVFS_DISK;
	
	/* fill in all the operations */
	ops.connect_fn = pvfs_connect;
	ops.disconnect_fn = pvfs_disconnect;
	ops.unlink_fn = pvfs_unlink;
	ops.chkpath_fn = pvfs_chkpath;
	ops.qpathinfo_fn = pvfs_qpathinfo;
	ops.setpathinfo_fn = pvfs_setpathinfo;
	ops.open_fn = pvfs_open;
	ops.mkdir_fn = pvfs_mkdir;
	ops.rmdir_fn = pvfs_rmdir;
	ops.rename_fn = pvfs_rename;
	ops.copy_fn = pvfs_copy;
	ops.ioctl_fn = pvfs_ioctl;
	ops.read_fn = pvfs_read;
	ops.write_fn = pvfs_write;
	ops.seek_fn = pvfs_seek;
	ops.flush_fn = pvfs_flush;
	ops.close_fn = pvfs_close;
	ops.exit_fn = pvfs_exit;
	ops.lock_fn = pvfs_lock;
	ops.setfileinfo_fn = pvfs_setfileinfo;
	ops.qfileinfo_fn = pvfs_qfileinfo;
	ops.fsinfo_fn = pvfs_fsinfo;
	ops.lpq_fn = pvfs_lpq;
	ops.search_first_fn = pvfs_search_first;
	ops.search_next_fn = pvfs_search_next;
	ops.search_close_fn = pvfs_search_close;
	ops.trans_fn = pvfs_trans;
	ops.logoff_fn = pvfs_logoff;
	ops.async_setup_fn = pvfs_async_setup;
	ops.cancel_fn = pvfs_cancel;
	ops.notify_fn = pvfs_notify;

	/* register ourselves with the NTVFS subsystem. We register
	   under the name 'default' as we wish to be the default
	   backend, and also register as 'posix' */
	ops.name = "default";
	ret = ntvfs_register(&ops, &vers);

	if (!NT_STATUS_IS_OK(ret)) {
		DEBUG(0,("Failed to register POSIX backend as '%s'!\n", ops.name));
	}

	ops.name = "posix";
	ret = ntvfs_register(&ops, &vers);

	if (!NT_STATUS_IS_OK(ret)) {
		DEBUG(0,("Failed to register POSIX backend as '%s'!\n", ops.name));
	}

	if (NT_STATUS_IS_OK(ret)) {
		ret = ntvfs_common_init();
	}

	return ret;
}

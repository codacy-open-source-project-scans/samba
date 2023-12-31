/* 
   Unix SMB/CIFS implementation.

   simple NTVFS filesystem backend

   Copyright (C) Andrew Tridgell 2003

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
  utility functions for simple backend
*/

#include "includes.h"
#include "system/filesys.h"
#include "svfs.h"
#include "system/time.h"
#include "system/dir.h"
#include "ntvfs/ntvfs.h"
#include "ntvfs/simple/proto.h"

/*
  convert a windows path to a unix path - don't do any mangling or case sensitive handling
*/
char *svfs_unix_path(struct ntvfs_module_context *ntvfs,
		     struct ntvfs_request *req, const char *name)
{
	struct svfs_private *p = ntvfs->private_data;
	char *ret;
	char *name_lower = strlower_talloc(p, name);

	if (*name != '\\') {
		ret = talloc_asprintf(req, "%s/%s", p->connectpath, name_lower);
	} else {
		ret = talloc_asprintf(req, "%s%s", p->connectpath, name_lower);
	}
	all_string_sub(ret, "\\", "/", 0);
	talloc_free(name_lower);
	return ret;
}


/*
  read a directory and find all matching file names and stat info
  returned names are separate unix and DOS names. The returned names
  are relative to the directory
*/
struct svfs_dir *svfs_list_unix(TALLOC_CTX *mem_ctx, struct ntvfs_request *req, const char *unix_path)
{
	char *p, *mask;
	struct svfs_dir *dir;
	DIR *odir;
	struct dirent *dent;
	unsigned int allocated = 0;
	char *low_mask;

	dir = talloc(mem_ctx, struct svfs_dir);
	if (!dir) { return NULL; }

	dir->count = 0;
	dir->files = 0;

	/* find the base directory */
	p = strrchr(unix_path, '/');
	if (!p) { return NULL; }

	dir->unix_dir = talloc_strndup(mem_ctx, unix_path, PTR_DIFF(p, unix_path));
	if (!dir->unix_dir) { return NULL; }

	/* the wildcard pattern is the last part */
	mask = p+1;

	low_mask = strlower_talloc(mem_ctx, mask);
	if (!low_mask) { return NULL; }

	odir = opendir(dir->unix_dir);
	if (!odir) { return NULL; }

	while ((dent = readdir(odir))) {
		unsigned int i = dir->count;
		char *full_name;
		char *low_name;

		if (strchr(dent->d_name, ':') && !strchr(unix_path, ':')) {
			/* don't show streams in dir listing */
			continue;
		}

		low_name = strlower_talloc(mem_ctx, dent->d_name);
		if (!low_name) { continue; }

		/* check it matches the wildcard pattern */
		if (ms_fnmatch_protocol(low_mask, low_name, PROTOCOL_NT1,
					false) != 0) {
			continue;
		}
		
		if (dir->count >= allocated) {
			allocated = (allocated + 100) * 1.2;
			dir->files = talloc_realloc(dir, dir->files, struct svfs_dirfile, allocated);
			if (!dir->files) { 
				closedir(odir);
				return NULL;
			}
		}

		dir->files[i].name = low_name;
		if (!dir->files[i].name) { continue; }

		full_name = talloc_asprintf(mem_ctx, "%s/%s", dir->unix_dir,
					    dir->files[i].name);
		if (!full_name) { continue; }

		if (stat(full_name, &dir->files[i].st) == 0) { 
			dir->count++;
		}

		talloc_free(full_name);
	}

	closedir(odir);

	return dir;
}

/*
  read a directory and find all matching file names and stat info
  returned names are separate unix and DOS names. The returned names
  are relative to the directory
*/
struct svfs_dir *svfs_list(struct ntvfs_module_context *ntvfs, struct ntvfs_request *req, const char *pattern)
{
	struct svfs_private *p = ntvfs->private_data;
	char *unix_path;

	unix_path = svfs_unix_path(ntvfs, req, pattern);
	if (!unix_path) { return NULL; }

	return svfs_list_unix(p, req, unix_path);
}


/*******************************************************************
set the time on a file via file descriptor
*******************************************************************/
int svfs_file_utime(int fd, struct utimbuf *times)
{
	char *fd_path = NULL;
	int ret;

	ret = asprintf(&fd_path, "/proc/self/%d", fd);
	if (ret == -1) {
		errno = ENOMEM;
		return -1;
	}

	if (!fd_path) {
		errno = ENOMEM;
		return -1;
	}
	
	ret = utime(fd_path, times);
	free(fd_path);
	return ret;
}


/*
  map a unix file attrib to a DOS attribute
*/
uint16_t svfs_unix_to_dos_attrib(mode_t mode)
{
	uint16_t ret = 0;
	if (S_ISDIR(mode)) ret |= FILE_ATTRIBUTE_DIRECTORY;
	if (!(mode & S_IWUSR)) ret |= FILE_ATTRIBUTE_READONLY;
	return ret;
}


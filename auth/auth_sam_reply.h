/*
   Unix SMB/CIFS implementation.

   Convert a server info struct into the form for PAC and NETLOGON replies

   Copyright (C) Andrew Bartlett <abartlet@samba.org> 2004
   Copyright (C) Stefan Metzmacher <metze@samba.org>  2005

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

#ifndef __AUTH_AUTH_SAM_REPLY_H__
#define __AUTH_AUTH_SAM_REPLY_H__

#include "libcli/util/ntstatus.h"
#include "libcli/util/werror.h"
#include "librpc/gen_ndr/auth.h"

#undef _PRINTF_ATTRIBUTE
#define _PRINTF_ATTRIBUTE(a1, a2) PRINTF_ATTRIBUTE(a1, a2)
/* this file contains prototypes for functions that are private
 * to this subsystem or library. These functions should not be
 * used outside this particular subsystem! */


/* The following definitions come from auth/auth_sam_reply.c  */

NTSTATUS make_user_info_SamBaseInfo(TALLOC_CTX *mem_ctx,
				    const char *account_name,
				    const struct netr_SamBaseInfo *base,
				    bool authenticated,
				    struct auth_user_info **_user_info);

struct auth_user_info *auth_user_info_copy(TALLOC_CTX *mem_ctx,
					   const struct auth_user_info *src);

NTSTATUS auth_convert_user_info_dc_saminfo6(TALLOC_CTX *mem_ctx,
					   const struct auth_user_info_dc *user_info_dc,
					   enum auth_group_inclusion group_inclusion,
					   struct netr_SamInfo6 **_sam6,
					   struct PAC_DOMAIN_GROUP_MEMBERSHIP **_resource_groups);
NTSTATUS auth_convert_user_info_dc_saminfo2(TALLOC_CTX *mem_ctx,
					   const struct auth_user_info_dc *user_info_dc,
					   enum auth_group_inclusion group_inclusion,
					   struct netr_SamInfo2 **_sam2);
NTSTATUS auth_convert_user_info_dc_saminfo3(TALLOC_CTX *mem_ctx,
					   const struct auth_user_info_dc *user_info_dc,
					   enum auth_group_inclusion group_inclusion,
					   struct netr_SamInfo3 **_sam3,
					   struct PAC_DOMAIN_GROUP_MEMBERSHIP **_resource_groups);

/**
 * Make a user_info_dc struct from the info3 returned by a domain logon
 */
NTSTATUS make_user_info_dc_netlogon_validation(TALLOC_CTX *mem_ctx,
					      const char *account_name,
					      uint16_t validation_level,
					      const union netr_Validation *validation,
					      bool authenticated,
					      struct auth_user_info_dc **_user_info_dc);

/**
 * Make a user_info_dc struct from the PAC_LOGON_INFO supplied in the krb5 logon
 */
NTSTATUS make_user_info_dc_pac(TALLOC_CTX *mem_ctx,
			      const struct PAC_LOGON_INFO *pac_logon_info,
			      const struct PAC_UPN_DNS_INFO *pac_upn_dns_info,
			      enum auth_group_inclusion group_inclusion,
			      struct auth_user_info_dc **_user_info_dc);

/* The following definitions come from auth/wbc_auth_util.c  */

struct wbcAuthUserInfo;

struct netr_SamInfo6 *wbcAuthUserInfo_to_netr_SamInfo6(TALLOC_CTX *mem_ctx,
						       const struct wbcAuthUserInfo *info);

#undef _PRINTF_ATTRIBUTE
#define _PRINTF_ATTRIBUTE(a1, a2)

#endif /* __AUTH_AUTH_SAM_REPLY_H__ */

/*
   Unix SMB/CIFS implementation.

   Manually parsed structures found in the DNSP IDL

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

void ndr_print_dnsp_name(struct ndr_print *ndr, const char *name,
				  const char *dns_name);
enum ndr_err_code ndr_pull_dnsp_name(struct ndr_pull *ndr, ndr_flags_type ndr_flags, const char **name);
enum ndr_err_code ndr_push_dnsp_name(struct ndr_push *ndr, ndr_flags_type ndr_flags, const char *name);
void ndr_print_dnsp_string(struct ndr_print *ndr, const char *name,
				  const char *dns_string);
enum ndr_err_code ndr_pull_dnsp_string(struct ndr_pull *ndr, ndr_flags_type ndr_flags, const char **string);
enum ndr_err_code ndr_push_dnsp_string(struct ndr_push *ndr, ndr_flags_type ndr_flags, const char *string);

enum ndr_err_code ndr_dnsp_string_list_copy(TALLOC_CTX *mem_ctx,
					    const struct dnsp_string_list *src,
					    struct dnsp_string_list *dst);

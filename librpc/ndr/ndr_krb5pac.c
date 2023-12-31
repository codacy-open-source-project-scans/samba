/* 
   Unix SMB/CIFS implementation.

   routines for marshalling/unmarshalling spoolss subcontext buffer structures

   Copyright (C) Stefan Metzmacher 2005
   
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
#include "librpc/gen_ndr/ndr_krb5pac.h"

size_t _ndr_size_PAC_INFO(const union PAC_INFO *r, uint32_t level, libndr_flags flags)
{
	size_t s = ndr_size_PAC_INFO(r, level, flags);
	switch (level) {
		case PAC_TYPE_LOGON_INFO:
			return NDR_ROUND(s,8);
		case PAC_TYPE_UPN_DNS_INFO:
			return NDR_ROUND(s,8);
		default:
			return s;
	}
}

enum ndr_err_code ndr_push_PAC_BUFFER(struct ndr_push *ndr, ndr_flags_type ndr_flags, const struct PAC_BUFFER *r)
{
	if (ndr_flags & NDR_SCALARS) {
		NDR_CHECK(ndr_push_align(ndr, 4));
		NDR_CHECK(ndr_push_PAC_TYPE(ndr, NDR_SCALARS, r->type));
		NDR_CHECK(ndr_push_uint32(ndr, NDR_SCALARS, _ndr_size_PAC_INFO(r->info,r->type,LIBNDR_FLAG_ALIGN8)));
		{
			libndr_flags _flags_save_PAC_INFO = ndr->flags;
			ndr_set_flags(&ndr->flags, LIBNDR_FLAG_ALIGN8);
			NDR_CHECK(ndr_push_relative_ptr1(ndr, r->info));
			ndr->flags = _flags_save_PAC_INFO;
		}
		NDR_CHECK(ndr_push_uint32(ndr, NDR_SCALARS, 0));
	}
	if (ndr_flags & NDR_BUFFERS) {
		{
			libndr_flags _flags_save_PAC_INFO = ndr->flags;
			ndr_set_flags(&ndr->flags, LIBNDR_FLAG_ALIGN8);
			if (r->info) {
				NDR_CHECK(ndr_push_relative_ptr2_start(ndr, r->info));
				{
					struct ndr_push *_ndr_info_pad;
					struct ndr_push *_ndr_info;
					size_t _ndr_size = _ndr_size_PAC_INFO(r->info, r->type, LIBNDR_FLAG_ALIGN8);
					NDR_CHECK(ndr_push_subcontext_start(ndr, &_ndr_info_pad, 0, NDR_ROUND(_ndr_size, 8)));
					NDR_CHECK(ndr_push_subcontext_start(_ndr_info_pad, &_ndr_info, 0, _ndr_size));
					NDR_CHECK(ndr_push_set_switch_value(_ndr_info, r->info, r->type));
					NDR_CHECK(ndr_push_PAC_INFO(_ndr_info, NDR_SCALARS|NDR_BUFFERS, r->info));
					NDR_CHECK(ndr_push_subcontext_end(_ndr_info_pad, _ndr_info, 0, _ndr_size));
					NDR_CHECK(ndr_push_subcontext_end(ndr, _ndr_info_pad, 0, NDR_ROUND(_ndr_size, 8)));
				}
				NDR_CHECK(ndr_push_relative_ptr2_end(ndr, r->info));
			}
			ndr->flags = _flags_save_PAC_INFO;
		}
	}
	return NDR_ERR_SUCCESS;
}

enum ndr_err_code ndr_pull_PAC_BUFFER(struct ndr_pull *ndr, ndr_flags_type ndr_flags, struct PAC_BUFFER *r)
{
	uint32_t _ptr_info;
	TALLOC_CTX *_mem_save_info_0;
	if (ndr_flags & NDR_SCALARS) {
		NDR_CHECK(ndr_pull_align(ndr, 4));
		NDR_CHECK(ndr_pull_PAC_TYPE(ndr, NDR_SCALARS, &r->type));
		NDR_CHECK(ndr_pull_uint32(ndr, NDR_SCALARS, &r->_ndr_size));
		{
			libndr_flags _flags_save_PAC_INFO = ndr->flags;
			ndr_set_flags(&ndr->flags, LIBNDR_FLAG_ALIGN8);
			NDR_CHECK(ndr_pull_generic_ptr(ndr, &_ptr_info));
			if (_ptr_info) {
				NDR_PULL_ALLOC(ndr, r->info);
				NDR_CHECK(ndr_pull_relative_ptr1(ndr, r->info, _ptr_info));
			} else {
				r->info = NULL;
			}
			ndr->flags = _flags_save_PAC_INFO;
		}
		NDR_CHECK(ndr_pull_uint32(ndr, NDR_SCALARS, &r->_pad));
	}
	if (ndr_flags & NDR_BUFFERS) {
		{
			libndr_flags _flags_save_PAC_INFO = ndr->flags;
			ndr_set_flags(&ndr->flags, LIBNDR_FLAG_ALIGN8);
			if (r->info) {
				uint32_t _relative_save_offset;
				_relative_save_offset = ndr->offset;
				NDR_CHECK(ndr_pull_relative_ptr2(ndr, r->info));
				_mem_save_info_0 = NDR_PULL_GET_MEM_CTX(ndr);
				NDR_PULL_SET_MEM_CTX(ndr, r->info, 0);
				{
					struct ndr_pull *_ndr_info_pad;
					struct ndr_pull *_ndr_info;
					NDR_CHECK(ndr_pull_subcontext_start(ndr, &_ndr_info_pad, 0, NDR_ROUND(r->_ndr_size, 8)));
					NDR_CHECK(ndr_pull_subcontext_start(_ndr_info_pad, &_ndr_info, 0, r->_ndr_size));
					NDR_CHECK(ndr_pull_set_switch_value(_ndr_info, r->info, r->type));
					NDR_CHECK(ndr_pull_PAC_INFO(_ndr_info, NDR_SCALARS|NDR_BUFFERS, r->info));
					NDR_CHECK(ndr_pull_subcontext_end(_ndr_info_pad, _ndr_info, 0, r->_ndr_size));
					NDR_CHECK(ndr_pull_subcontext_end(ndr, _ndr_info_pad, 0, NDR_ROUND(r->_ndr_size, 8)));
				}
				NDR_PULL_SET_MEM_CTX(ndr, _mem_save_info_0, 0);
				if (ndr->offset > ndr->relative_highest_offset) {
					ndr->relative_highest_offset = ndr->offset;
				}
				ndr->offset = _relative_save_offset;
			}
			ndr->flags = _flags_save_PAC_INFO;
		}
	}
	return NDR_ERR_SUCCESS;
}

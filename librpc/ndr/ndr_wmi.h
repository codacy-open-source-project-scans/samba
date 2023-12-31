/*
   Unix SMB/CIFS implementation.

   routines for marshalling/unmarshalling DCOM string arrays

   Copyright (C) Jelmer Vernooij 2004

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

enum ndr_err_code ndr_push_BSTR(struct ndr_push *ndr, ndr_flags_type ndr_flags, const struct BSTR *r);
enum ndr_err_code ndr_pull_BSTR(struct ndr_pull *ndr, ndr_flags_type ndr_flags, struct BSTR *r);

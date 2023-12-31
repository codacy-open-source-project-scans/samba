/*
   Unix SMB/CIFS implementation.

   routines for marshalling/unmarshalling witness structures

   Copyright (C) Guenther Deschner 2015

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

_PUBLIC_ enum ndr_err_code ndr_push_witness_notifyResponse(struct ndr_push *ndr, ndr_flags_type ndr_flags, const struct witness_notifyResponse *r);
_PUBLIC_ enum ndr_err_code ndr_pull_witness_notifyResponse(struct ndr_pull *ndr, ndr_flags_type ndr_flags, struct witness_notifyResponse *r);

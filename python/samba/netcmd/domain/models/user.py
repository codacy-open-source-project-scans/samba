# Unix SMB/CIFS implementation.
#
# User model.
#
# Copyright (C) Catalyst.Net Ltd. 2023
#
# Written by Rob van der Linde <rob@catalyst.net.nz>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

from ldb import Dn

from samba.dsdb import DS_GUID_USERS_CONTAINER

from .fields import DnField, SIDField, StringField
from .model import Model


class User(Model):
    username = StringField("sAMAccountName")
    assigned_policy = DnField("msDS-AssignedAuthNPolicy")
    assigned_silo = DnField("msDS-AssignedAuthNPolicySilo")
    object_sid = SIDField("objectSid")

    def __str__(self):
        """Return username rather than cn for User model."""
        return self.username

    @staticmethod
    def get_base_dn(ldb):
        """Return the base DN for the User model.

        :param ldb: Ldb connection
        :return: Dn to use for new objects
        """
        return ldb.get_wellknown_dn(ldb.get_default_basedn(),
                                    DS_GUID_USERS_CONTAINER)

    @classmethod
    def get_search_dn(cls, ldb):
        """Return Dn used for searching so Computers will also be found.

        :param ldb: Ldb connection
        :return: Dn to use for searching
        """
        return ldb.get_root_basedn()

    @staticmethod
    def get_object_class():
        return "user"

    @classmethod
    def find(cls, ldb, name):
        """Helper function to find a user first by Dn then username.

        If the Dn can't be parsed, use sAMAccountName instead.
        """
        try:
            query = {"dn": Dn(ldb, name)}
        except ValueError:
            query = {"username": name}

        return cls.get(ldb, **query)

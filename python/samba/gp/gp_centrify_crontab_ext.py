# gp_centrify_crontab_ext samba gpo policy
# Copyright (C) David Mulder <dmulder@suse.com> 2022
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

import os, re
from subprocess import Popen, PIPE
from samba.gp.gpclass import gp_pol_ext, drop_privileges, gp_file_applier, \
    gp_misc_applier
from tempfile import NamedTemporaryFile
from samba.gp.gp_scripts_ext import fetch_crontab, install_crontab, \
    install_user_crontab

intro = '''
### autogenerated by samba
#
# This file is generated by the gp_centrify_crontab_ext Group Policy
# Client Side Extension. To modify the contents of this file,
# modify the appropriate Group Policy objects which apply
# to this machine. DO NOT MODIFY THIS FILE DIRECTLY.
#

'''
end = '''
### autogenerated by samba ###
'''

class gp_centrify_crontab_ext(gp_pol_ext, gp_file_applier):
    def __str__(self):
        return 'Centrify/CrontabEntries'

    def process_group_policy(self, deleted_gpo_list, changed_gpo_list,
                             cdir=None):
        for guid, settings in deleted_gpo_list:
            if str(self) in settings:
                for attribute, script in settings[str(self)].items():
                    self.unapply(guid, attribute, script)

        for gpo in changed_gpo_list:
            if gpo.file_sys_path:
                section = \
                    'Software\\Policies\\Centrify\\UnixSettings\\CrontabEntries'
                pol_file = 'MACHINE/Registry.pol'
                path = os.path.join(gpo.file_sys_path, pol_file)
                pol_conf = self.parse(path)
                if not pol_conf:
                    continue
                entries = []
                for e in pol_conf.entries:
                    if e.keyname == section and e.data.strip():
                        entries.append(e.data)
                def applier_func(entries):
                    cron_dir = '/etc/cron.d' if not cdir else cdir
                    with NamedTemporaryFile(prefix='gp_', mode="w+",
                            delete=False, dir=cron_dir) as f:
                        contents = intro
                        for entry in entries:
                            contents += '%s\n' % entry
                        contents += end
                        f.write(contents)
                        return [f.name]
                attribute = self.generate_attribute(gpo.name)
                value_hash = self.generate_value_hash(*entries)
                self.apply(gpo.name, attribute, value_hash, applier_func,
                           entries)

                # Remove scripts for this GPO which are no longer applied
                self.clean(gpo.name, keep=attribute)

    def rsop(self, gpo, target='MACHINE'):
        output = {}
        section = 'Software\\Policies\\Centrify\\UnixSettings\\CrontabEntries'
        pol_file = '%s/Registry.pol' % target
        if gpo.file_sys_path:
            path = os.path.join(gpo.file_sys_path, pol_file)
            pol_conf = self.parse(path)
            if not pol_conf:
                return output
            for e in pol_conf.entries:
                if e.keyname == section and e.data.strip():
                    if str(self) not in output.keys():
                        output[str(self)] = []
                    output[str(self)].append(e.data)
        return output

class gp_user_centrify_crontab_ext(gp_centrify_crontab_ext, gp_misc_applier):
    def unapply(self, guid, attribute, entry):
        others, entries = fetch_crontab(self.username)
        if entry in entries:
            entries.remove(entry)
            install_user_crontab(self.username, others, entries)
        self.cache_remove_attribute(guid, attribute)

    def apply(self, guid, attribute, entry):
        old_val = self.cache_get_attribute_value(guid, attribute)
        others, entries = fetch_crontab(self.username)
        if not old_val or entry not in entries:
            entries.append(entry)
            install_user_crontab(self.username, others, entries)
            self.cache_add_attribute(guid, attribute, entry)

    def process_group_policy(self, deleted_gpo_list, changed_gpo_list):
        for guid, settings in deleted_gpo_list:
            if str(self) in settings:
                for attribute, entry in settings[str(self)].items():
                    self.unapply(guid, attribute, entry)

        for gpo in changed_gpo_list:
            if gpo.file_sys_path:
                section = \
                    'Software\\Policies\\Centrify\\UnixSettings\\CrontabEntries'
                pol_file = 'USER/Registry.pol'
                path = os.path.join(gpo.file_sys_path, pol_file)
                pol_conf = drop_privileges('root', self.parse, path)
                if not pol_conf:
                    continue
                attrs = []
                for e in pol_conf.entries:
                    if e.keyname == section and e.data.strip():
                        attribute = self.generate_attribute(e.data)
                        attrs.append(attribute)
                        self.apply(gpo.name, attribute, e.data)
                self.clean(gpo.name, keep=attrs)

    def rsop(self, gpo):
        return super().rsop(gpo, target='USER')

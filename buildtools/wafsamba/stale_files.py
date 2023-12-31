# encoding: utf-8
# Thomas Nagy, 2006-2010 (ita)

"""
Add a pre-build hook to remove all build files
which do not have a corresponding target

This can be used for example to remove the targets
that have changed name without performing
a full 'waf clean'

Of course, it will only work if there are no dynamically generated
nodes/tasks, in which case the method will have to be modified
to exclude some folders for example.
"""

from waflib import Logs, Build, Options, Utils, Errors
import os
from wafsamba import samba_utils
from Runner import Parallel

old_refill_task_list = Parallel.refill_task_list
def replace_refill_task_list(self):
    '''replacement for refill_task_list() that deletes stale files'''

    iit = old_refill_task_list(self)
    bld = self.bld

    if not getattr(bld, 'new_rules', False):
        # we only need to check for stale files if the build rules changed
        return iit

    if Options.options.compile_targets:
        # not safe when --target is used
        return iit

    # execute only once
    if getattr(self, 'cleanup_done', False):
        return iit
    self.cleanup_done = True

    def group_name(g):
        tm = self.bld.task_manager
        return [x for x in tm.groups_names if id(tm.groups_names[x]) == id(g)][0]

    bin_base = bld.bldnode.abspath()
    bin_base_len = len(bin_base)

    # paranoia
    if bin_base[-4:] != '/bin':
        raise Errors.WafError("Invalid bin base: %s" % bin_base)

    # obtain the expected list of files
    expected = []
    for i in range(len(bld.task_manager.groups)):
        g = bld.task_manager.groups[i]
        tasks = g.tasks_gen
        for x in tasks:
            try:
                if getattr(x, 'target'):
                    tlist = samba_utils.TO_LIST(getattr(x, 'target'))
                    ttype = getattr(x, 'samba_type', None)
                    task_list = getattr(x, 'compiled_tasks', [])
                    if task_list:
                        # this gets all of the .o files, including the task
                        # ids, so foo.c maps to foo_3.o for idx=3
                        for tsk in task_list:
                            for output in tsk.outputs:
                                objpath = os.path.normpath(output.abspath(bld.env))
                                expected.append(objpath)
                    for t in tlist:
                        if ttype in ['LIBRARY', 'PLUGIN', 'MODULE']:
                            t = samba_utils.apply_pattern(t, bld.env.shlib_PATTERN)
                        if ttype == 'PYTHON':
                            t = samba_utils.apply_pattern(t, bld.env.pyext_PATTERN)
                        p = os.path.join(x.path.abspath(bld.env), t)
                        p = os.path.normpath(p)
                        expected.append(p)
                for n in x.allnodes:
                    p = n.abspath(bld.env)
                    if p[0:bin_base_len] == bin_base:
                        expected.append(p)
            except:
                pass

    for root, dirs, files in os.walk(bin_base):
        for f in files:
            p = root + '/' + f
            if os.path.islink(p):
                link = os.readlink(p)
                if link[0:bin_base_len] == bin_base:
                    p = link
            if f in ['config.h']:
                continue
            (froot, fext) = os.path.splitext(f)
            if fext not in [ '.c', '.h', '.so', '.o' ]:
                continue
            if f[-7:] == '.inst.h':
                continue
            if p.find("/.conf") != -1:
                continue
            if not p in expected and os.path.exists(p):
                Logs.warn("Removing stale file: %s" % p)
                os.unlink(p)
    return iit


def AUTOCLEANUP_STALE_FILES(bld):
    """automatically clean up any files in bin that shouldn't be there"""
    global old_refill_task_list
    old_refill_task_list = Parallel.refill_task_list
    Parallel.refill_task_list = replace_refill_task_list
    Parallel.bld = bld
Build.BuildContext.AUTOCLEANUP_STALE_FILES = AUTOCLEANUP_STALE_FILES

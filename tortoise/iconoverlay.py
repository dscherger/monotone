# Published under the GNU GPL, v2 or later.
# Copyright (C) 2007 Henry Ludemann <misc@hl.id.au>
# Copyright (C) 2007 TK Soh <teekaysoh@gmail.com>

import os
import win32api
import win32con
from win32com.shell import shell, shellcon
import _winreg
from mercurial import hg, cmdutil, util
from mercurial import repo as _repo
import thgutil
import sys

# FIXME: quick workaround traceback caused by missing "closed" 
# attribute in win32trace.
from mercurial import ui
def write_err(self, *args):
    for a in args:
        sys.stderr.write(str(a))
ui.ui.write_err = write_err

# file/directory status
UNCHANGED = "unchanged"
ADDED = "added"
MODIFIED = "modified"
UNKNOWN = "unknown"
NOT_IN_REPO = "n/a"

# file status cache
CACHE_TIMEOUT = 5000
overlay_cache = {}
cache_tick_count = 0
cache_root = None
cache_pdir = None

# some misc constants
S_OK = 0
S_FALSE = 1

def add_dirs(list):
    dirs = set()
    for f in list:
        dir = os.path.dirname(f)
        if dir in dirs:
            continue
        while dir:
            dirs.add(dir)
            dir = os.path.dirname(dir)
    list.extend(dirs)

class IconOverlayExtension(object):
    """
    Class to implement icon overlays for source controlled files.

    Displays a different icon based on version control status.

    NOTE: The system allocates only 15 slots in _total_ for all
        icon overlays; we (will) use 6, tortoisecvs uses 7... not a good
        recipe for a happy system.
    """
    
    counter = 0

    _com_interfaces_ = [shell.IID_IShellIconOverlayIdentifier]
    _public_methods_ = [
        "GetOverlayInfo", "GetPriority", "IsMemberOf"
        ]
    _reg_threading_ = 'Apartment'

    def GetOverlayInfo(self): 
        icon = thgutil.get_icon_path("status", self.icon)
        print "icon = ", icon

        if icon:
            return (icon, 0, shellcon.ISIOI_ICONFILE)
        else:
            return ("", 0, 0) 

    def GetPriority(self):
        return 0

    def _get_installed_overlays():
        key = win32api.RegOpenKeyEx(win32con.HKEY_LOCAL_MACHINE,
                                    "Software\\Microsoft\\Windows\\" +
                                        "CurrentVersion\\Explorer\\" +
                                        "ShellIconOverlayIdentifiers",
                                    0,
                                    win32con.KEY_READ)
        keys = win32api.RegEnumKeyEx(key)
        handlercount = len(keys)
        print "number of overlay handlers installed = %d" % handlercount
        for i, k in enumerate(keys):
            print i, k
        win32api.RegCloseKey(key)
        return handlercount
        
    def _get_state(self, upath):
        """
        Get the state of a given path in source control.
        """
        global overlay_cache, cache_tick_count
        global cache_root, cache_pdir
        
        #print "called: _get_state(%s)" % path
        tc = win32api.GetTickCount()
        
        try:
            # handle some Asian charsets
            path = upath.encode('mbcs')
        except:
            path = upath

        # check if path is cached
        pdir = os.path.dirname(path)
        if cache_pdir == pdir and overlay_cache:
            if tc - cache_tick_count < CACHE_TIMEOUT:
                try:
                    status = overlay_cache[path]
                except:
                    status = UNKNOWN
                print "%s: %s (cached)" % (path, status)
                return status
            else:
                print "Timed out!!"
                overlay_cache.clear()

        # path is a drive
        if path.endswith(":\\"):
            overlay_cache[path] = UNKNOWN
            return NOT_IN_REPO

        # open repo
        if cache_pdir == pdir:
            root = cache_root
        else:
            print "find new root"
            cache_pdir = pdir
            cache_root = root = thgutil.find_root(pdir)
        print "_get_state: root = ", root
        if root is None:
            print "_get_state: not in repo"
            overlay_cache = {None : None}
            cache_tick_count = win32api.GetTickCount()
            return NOT_IN_REPO

        try:
            tc1 = win32api.GetTickCount()
            repo = hg.repository(ui.ui(), path=root)
            print "hg.repository() took %d ticks" % (win32api.GetTickCount() - tc1)

            # check if to display overlay icons in this repo
            global_opts = ui.ui().configlist('tortoisehg', 'overlayicons', [])
            repo_opts = repo.ui.configlist('tortoisehg', 'overlayicons', [])
            
            print "%s: global overlayicons = " % path, global_opts
            print "%s: repo overlayicons = " % path, repo_opts
            is_netdrive =  thgutil.netdrive_status(path) is not None
            if (is_netdrive and 'localdisks' in global_opts) \
                    or 'False' in repo_opts:
                print "%s: overlayicons disabled" % path
                overlay_cache = {None : None}
                cache_tick_count = win32api.GetTickCount()
                return NOT_IN_REPO
        except _repo.RepoError:
            # We aren't in a working tree
            print "%s: not in repo" % dir
            overlay_cache[path] = UNKNOWN
            return NOT_IN_REPO

        # get file status
        tc1 = win32api.GetTickCount()

        modified, added, removed, deleted = [], [], [], []
        unknown, ignored, clean = [], [], []
        files = []
        try:
            files, matchfn, anypats = cmdutil.matchpats(repo, [pdir])
            modified, added, removed, deleted, unknown, ignored, clean = \
                    repo.status(files=files, list_ignored=True, 
                            list_clean=True, list_unknown=True)

            # add directory status to list
            for grp in (clean,modified,added,removed,deleted,ignored,unknown):
                add_dirs(grp)
        except util.Abort, inst:
            print "abort: %s" % inst
            print "treat as unknown : %s" % path
            return UNKNOWN
        
        print "status() took %d ticks" % (win32api.GetTickCount() - tc1)
                
        # cached file info
        tc = win32api.GetTickCount()
        overlay_cache = {}
        for grp, st in (
                (ignored, UNKNOWN),
                (unknown, UNKNOWN),                
                (clean, UNCHANGED),
                (added, ADDED),
                (removed, MODIFIED),
                (deleted, MODIFIED),
                (modified, MODIFIED)):
            for f in grp:
                fpath = os.path.join(repo.root, os.path.normpath(f))
                overlay_cache[fpath] = st

        if path in overlay_cache:
            status = overlay_cache[path]
        else:
            status = overlay_cache[path] = UNKNOWN
        print "%s: %s" % (path, status)
        cache_tick_count = win32api.GetTickCount()
        return status

    def IsMemberOf(self, path, attrib):                  
        try:
            tc = win32api.GetTickCount()
            if self._get_state(path) == self.state:
                return S_OK
            return S_FALSE
        finally:
            print "IsMemberOf: _get_state() took %d ticks" % \
                    (win32api.GetTickCount() - tc)
            
def make_icon_overlay(name, icon, state, clsid):
    """
    Make an icon overlay COM class.

    Used to create different COM server classes for highlighting the
    files with different source controlled states (eg: unchanged, 
    modified, ...).
    """
    classname = "%sOverlay" % name
    prog_id = "Mercurial.ShellExtension.%s" % classname
    desc = "Mercurial icon overlay shell extension for %s files" % name.lower()
    reg = [
        (_winreg.HKEY_LOCAL_MACHINE, r"Software\Microsoft\Windows\CurrentVersion\Explorer\ShellIconOverlayIdentifiers\%s" % name) ]
    cls = type(
            classname,
            (IconOverlayExtension, ),
            dict(_reg_clsid_=clsid, _reg_progid_=prog_id, _reg_desc_=desc, registry_keys=reg, icon=icon, state=state))

    _overlay_classes.append(cls)
    # We need to register the class as global, as pythoncom will
    # create an instance of it.
    globals()[classname] = cls

_overlay_classes = []
make_icon_overlay("Changed", "changed.ico", MODIFIED, "{102C6A24-5F38-4186-B64A-237011809FAB}")
make_icon_overlay("Unchanged", "unchanged.ico", UNCHANGED, "{00FEE959-5773-424B-88AC-A01BFC8E4555}")
make_icon_overlay("Added", "added.ico", ADDED, "{8447DB75-5875-4BA8-9F38-A727DAA484A0}")

def get_overlay_classes():
    """
    Get a list of all registerable icon overlay classes
    """
    return _overlay_classes

# Published under the GNU GPL, v2 or later.
# Copyright (C) 2007 Jelmer Vernooij <jelmer@samba.org>
# Copyright (C) 2007 Henry Ludemann <misc@hl.id.au>
# Copyright (C) 2007 TK Soh <teekaysoh@gmail.com>

import os
import tempfile
import pythoncom
from win32com.shell import shell, shellcon
import win32con
import win32process
import win32event
import win32ui
import win32gui
import win32gui_struct
import win32api
import _winreg
from mercurial import hg
from mercurial import repo as _repo
from thgutil import *

# FIXME: quick workaround traceback caused by missing "closed" 
# attribute in win32trace.
import sys
from mercurial import ui
def write_err(self, *args):
    for a in args:
        sys.stderr.write(str(a))
ui.ui.write_err = write_err

S_OK = 0
S_FALSE = 1

class TortoiseMenu(object):
    def __init__(self, menutext, helptext, handler, icon=None, state=True):
        self.menutext = menutext
        self.helptext = helptext
        self.handler = handler
        self.state = state
        self.icon = icon

class TortoiseSubmenu(object):
    def __init__(self, menutext, menus=[], icon=None):
        self.menutext = menutext
        self.menus = menus[:]
        self.icon = icon
        
    def add_menu(self, menutext, helptext, handler, icon=None, state=True):
        self.menus.append(TortoiseMenu(menutext, helptext, handler, icon, state))

    def add_sep(self):
        self.menus.append(TortoiseMenuSep())
        
    def get_menus(self):
        return self.menus
        
class TortoiseMenuSep(object):
    def __init__(self):
        pass
    
def open_repo(path):
    root = find_root(path)
    if root:
        try:
            repo = hg.repository(ui.ui(), path=root)
            return repo
        except _repo.RepoError:
            pass

    return None

def open_dialog(cmd, cmdopts='', cwd=None, root=None, filelist=[], gui=True):
    app_path = find_path("hgproc", get_prog_root(), '.EXE;.BAT')

    if filelist:
        fd, tmpfile = tempfile.mkstemp(prefix="tortoisehg_filelist_")
        os.write(fd, "\n".join(filelist))
        os.close(fd)

    # start gpopen
    gpopts = "--command %s" % cmd
    if root:
        gpopts += " --root %s" % shellquote(root)
    if filelist:
        gpopts += " --listfile %s --deletelistfile" % (shellquote(tmpfile))
    if cwd:
        gpopts += " --cwd %s" % shellquote(cwd)
    if not gui:
        gpopts += " --nogui"

    cmdline = '%s %s -- %s' % (shellquote(app_path), gpopts, cmdopts)

    try:
        run_program(cmdline)
    except win32api.error, details:
        win32ui.MessageBox("Error executing command - %s" % (details), "gpopen")

def get_clone_repo_name(dir, repo_name):
    dest_clone = os.path.join(dir, repo_name)
    if os.path.exists(dest_clone):
        dest_clone = os.path.join(dir, "Clone of " + repo_name)

    i = 2
    while os.path.exists(dest_clone):
        dest_clone = os.path.join(dir, "Clone of (%s) %s" % (i, repo_name))
        i += 1
    return dest_clone

def run_program(cmdline):
    print "run_program: %s" % (cmdline)

    import subprocess
    pop = subprocess.Popen(cmdline, 
                           shell=False,
                           creationflags=win32con.CREATE_NO_WINDOW,
                           stderr=subprocess.STDOUT,
                           stdout=subprocess.PIPE,
                           stdin=subprocess.PIPE)
    
"""Windows shell extension that adds context menu items to Mercurial repository"""
class ContextMenuExtension:
    _reg_progid_ = "Mercurial.ShellExtension.ContextMenu"
    _reg_desc_ = "Mercurial Shell Extension"
    _reg_clsid_ = "{EEE9936B-73ED-4D45-80C9-AF918354F885}"
    _com_interfaces_ = [shell.IID_IShellExtInit, shell.IID_IContextMenu]
    _public_methods_ = [
        "Initialize", # From IShellExtInit
        "QueryContextMenu", "InvokeCommand", "GetCommandString" # IContextMenu
        ]

    registry_keys = [
        (_winreg.HKEY_CLASSES_ROOT, r"*\shellex\ContextMenuHandlers\TortoiseHg"),
        (_winreg.HKEY_CLASSES_ROOT, r"Directory\Background\shellex\ContextMenuHandlers\TortoiseHg"),
        (_winreg.HKEY_CLASSES_ROOT, r"Directory\shellex\ContextMenuHandlers\TortoiseHg"),
        (_winreg.HKEY_CLASSES_ROOT, r"Folder\shellex\ContextMenuHandlers\TortoiseHg"),
        (_winreg.HKEY_CLASSES_ROOT, r"Directory\shellex\DragDropHandlers\TortoiseHg"),
        (_winreg.HKEY_CLASSES_ROOT, r"Folder\shellex\DragDropHandlers\TortoiseHg"),
        ]

    def __init__(self):
        self._folder = None
        self._filenames = []
        self._handlers = {}

    def Initialize(self, folder, dataobj, hkey):
        if folder:
            self._folder = shell.SHGetPathFromIDList(folder)

        if dataobj:
            format_etc = win32con.CF_HDROP, None, 1, -1, pythoncom.TYMED_HGLOBAL
            sm = dataobj.GetData(format_etc)
            num_files = shell.DragQueryFile(sm.data_handle, -1)
            for i in range(num_files):
                self._filenames.append(shell.DragQueryFile(sm.data_handle, i))

    def _create_menu(self, parent, menus, pos, idCmd, idCmdFirst):
        for menu_info in menus:
            if type(menu_info) == TortoiseMenuSep:
                win32gui.InsertMenu(parent, pos, 
                        win32con.MF_BYPOSITION|win32con.MF_SEPARATOR, 
                        idCmdFirst + idCmd, None)
            elif type(menu_info) == TortoiseSubmenu:
                submenu = win32gui.CreatePopupMenu()
                idCmd = self._create_menu(submenu, menu_info.get_menus(), 0,
                        idCmd, idCmdFirst)
                opt = {
                    'text' : menu_info.menutext,
                    'wID' : idCmdFirst + idCmd,
                    'hSubMenu' : submenu, 
                }

                if menu_info.icon:
                    icon_path = get_icon_path("tortoise", menu_info.icon)
                    opt['hbmpChecked'] = opt['hbmpUnchecked'] = \
                            icon_to_bitmap(icon_path)
                
                item, _ = win32gui_struct.PackMENUITEMINFO(**opt)
                win32gui.InsertMenuItem(parent, pos, True, item)
                self._handlers[idCmd] = ("", lambda x,y: 0)
            elif type(menu_info) == TortoiseMenu:
                fstate = win32con.MF_BYCOMMAND
                if menu_info.state is False:
                    fstate |= win32con.MF_GRAYED
                
                opt = {
                    'text' : menu_info.menutext,
                    'fState' : fstate,
                    'wID' : idCmdFirst + idCmd,
                }

                if menu_info.icon:
                    icon_path = get_icon_path("tortoise", menu_info.icon)
                    opt['hbmpChecked'] = opt['hbmpUnchecked'] = \
                            icon_to_bitmap(icon_path)
                
                item, _ = win32gui_struct.PackMENUITEMINFO(**opt)
                win32gui.InsertMenuItem(parent, pos, True, item)
                self._handlers[idCmd] = (menu_info.helptext, menu_info.handler)
            idCmd += 1
            pos += 1
        return idCmd

    def QueryContextMenu(self, hMenu, indexMenu, idCmdFirst, idCmdLast, uFlags):
        if uFlags & shellcon.CMF_DEFAULTONLY:
            return 0

        thgmenu = []    # hg menus

        # a brutal hack to detect if we are the first menu to go on to the 
        # context menu. If we are not the first, then add a menu separator
        # The number '30000' is just a guess based on my observation
        print "idCmdFirst = ", idCmdFirst
        if idCmdFirst >= 30000:
            thgmenu.append(TortoiseMenuSep())
            
        # As we are a context menu handler, we can ignore verbs.
        self._handlers = {}
        if self._folder and self._filenames:
            # get menus with drag-n-drop support
            commands = self._get_commands_dragdrop()
        else:
            # add regularly used commit menu to main context menu
            rpath = self._folder or self._filenames[0]
            if open_repo(rpath):
                thgmenu.append(TortoiseMenu(_("HG Commit..."), 
                               _("Commit changes in repository"),
                               self._commit, icon="menucommit.ico"))
                               
            # get other menus for hg submenu
            commands = self._get_commands()

        # add common menu items
        commands.append(TortoiseMenuSep())
        commands.append(TortoiseMenu(_("About"),
                       _("About TortoiseHg"),
                       self._about, icon="menuabout.ico"))
       
        # create submenus with Hg commands
        thgmenu.append(TortoiseSubmenu("TortoiseHG", commands, icon="hg.ico"))
        thgmenu.append(TortoiseMenuSep())
        
        idCmd = self._create_menu(hMenu, thgmenu, indexMenu, 0, idCmdFirst)

        # Return total number of menus & submenus we've added
        return idCmd

    def _get_commands_dragdrop(self):
        """
        Get a list of commands valid for the current selection.

        Each command is a tuple containing (display text, handler).
        """
        
        print "_get_commands_dragdrop() on %s" % ", ".join(self._filenames)        

        # we can only accept dropping one item
        if len(self._filenames) > 1:
            return []

        # open repo
        drag_repo = None
        drop_repo = None
        
        print "drag = %s" % self._filenames[0]
        print "drop = %s" % self._folder
        
        drag_path = self._filenames[0]
        drag_repo = open_repo(drag_path)
        if not drag_repo:
            return []
        if drag_repo and drag_repo.root != drag_path:
            return []   # dragged item must be a hg repo root directory

        drop_repo = open_repo(self._folder)
        
        result = []
        result.append(TortoiseMenu(_("Create Clone"), 
                       _("Create clone here from source"),
                       self._clone_here, icon="menuclone.ico"))

        if drop_repo:
            result.append(TortoiseMenu(_("Synchronize"),
                           _("Synchronize with dragged repository"),
                           self._synch_here, icon="menusynch.ico"))
        return result
        
    def _get_commands(self):
        """
        Get a list of commands valid for the current selection.

        Each command is a tuple containing (display text, handler).
        """
        
        print "_get_commands() on %s" % ", ".join(self._filenames)        

        # open repo
        result = []
        rpath = self._folder or self._filenames[0]
        repo = open_repo(rpath)
        if repo is None:
            result.append(TortoiseMenu(_("Clone a Repository"),
                           _("clone a repository"),
                           self._clone, icon="menuclone.ico"))
            result.append(TortoiseMenu(_("Create Repository Here"),
                           _("create a new repository in this directory"),
                           self._init, icon="menucreaterepos.ico"))
        else:
            result.append(TortoiseMenu(_("View File Status"),
                           _("Repository status"),
                           self._status, icon="menushowchanged.ico"))

            # Visual Diff (any extdiff command)
            has_vdiff = repo.ui.config('tortoisehg', 'vdiff', '') != ''
            result.append(TortoiseMenu(_("Visual Diff"),
                           _("View changes using GUI diff tool"),
                           self._vdiff, icon="TortoiseMerge.ico",
                           state=has_vdiff))
                           
            result.append(TortoiseMenu(_("Add Files"),
                           _("Add files to Hg repository"),
                           self._add, icon="menuadd.ico"))
            result.append(TortoiseMenu(_("Remove Files"),
                           _("Remove selected files on the next commit"),
                           self._remove, icon="menudelete.ico"))
            result.append(TortoiseMenu(_("Undo Changes"),
                           _("Revert selected files"),
                           self._revert, icon="menurevert.ico"))
            result.append(TortoiseMenu(_("Annotate Files"),
                           _("show changeset information per file line"),
                           self._annotate, icon="menublame.ico"))

            result.append(TortoiseMenuSep())
            result.append(TortoiseMenu(_("Update To Revision"),
                           _("update working directory"),
                           self._update, icon="menucheckout.ico"))

            can_merge = len(repo.heads()) > 1 and \
                        len(repo.workingctx().parents()) < 2
            result.append(TortoiseMenu(_("Merge Revisions"),
                           _("merge working directory with another revision"),
                           self._merge, icon="menumerge.ico",
                           state=can_merge))

            # show un-merge menu per merge status of working directory
            if len(repo.workingctx().parents()) > 1:
                result.append(TortoiseMenu(_("Undo Merge"),
                               _("Undo merge by updating to revision"),
                               self._merge, icon="menuunmerge.ico"))

            result.append(TortoiseMenuSep())

            result.append(TortoiseMenu(_("View Changelog"),
                           _("View revision history"),
                           self._history, icon="menulog.ico"))

            result.append(TortoiseMenu(_("Search Repository"),
                           _("Search revisions of files for a text pattern"),
                           self._grep, icon="menurepobrowse.ico"))
                           
            if repo.ui.config('tortoisehg', 'view'):
                result.append(TortoiseMenu(_("Revision Graph"),
                               _("View history with DAG graph"),
                               self._view, icon="menurevisiongraph.ico"))

            result.append(TortoiseMenuSep())

            result.append(TortoiseMenu(_("Synchronize..."),
                           _("Synchronize with remote repository"),
                           self._synch, icon="menusynch.ico"))
            result.append(TortoiseMenu(_("Recovery..."),
                           _("General repair and recovery of repository"),
                           self._recovery, icon="general.ico"))
            result.append(TortoiseMenu(_("Web Server"),
                           _("start web server for this repository"),
                           self._serve, icon="proxy.ico"))
            result.append(TortoiseMenu(_("Create Clone"),
                           _("Clone a repository here"),
                           self._clone, icon="menuclone.ico"))
            if repo.root != rpath:
                result.append(TortoiseMenu(_("Create Repository Here"),
                               _("create a new repository in this directory"),
                               self._init, icon="menucreaterepos.ico"))

        # config setttings menu
        result.append(TortoiseMenuSep())
        optmenu = TortoiseSubmenu(_("Settings"),icon="menusettings.ico")
        optmenu.add_menu(_("Global"),
                         _("Configure user wide settings"),
                         self._config_user)
        if repo:
            optmenu.add_menu(_("Repository"),
                             _("Configure settings local to this repository"),
                             self._config_repo)
        result.append(optmenu)

        return result

    def InvokeCommand(self, ci):
        mask, hwnd, verb, params, dir, nShow, hotkey, hicon = ci
        if verb >> 16:
            # This is a textual verb invocation... not supported.
            return S_FALSE
        if verb not in self._handlers:
            raise Exception("Unsupported command id %i!" % verb)
        self._handlers[verb][1](hwnd)

    def GetCommandString(self, cmd, uFlags):
        if uFlags & shellcon.GCS_VALIDATEA or uFlags & shellcon.GCS_VALIDATEW:
            if cmd in self._handlers:
                return S_OK
            return S_FALSE
        if uFlags & shellcon.GCS_VERBA or uFlags & shellcon.GCS_VERBW:
            return S_FALSE
        if uFlags & shellcon.GCS_HELPTEXTA or uFlags & shellcon.GCS_HELPTEXTW:
            # The win32com.shell implementation encodes the resultant
            # string into the correct encoding depending on the flags.
            return self._handlers[cmd][0]
        return S_FALSE

    def _commit(self, parent_window):
        self._run_dialog('commit')

    def _config_user(self, parent_window):
        self._run_dialog('config', noargs=True)

    def _config_repo(self, parent_window):
        self._run_dialog('config')

    def _vdiff(self, parent_window):
        '''[tortoisehg] vdiff = <any extdiff command>'''
        diff = ui.ui().config('tortoisehg', 'vdiff', None)
        if not diff:
            msg = "You must configure tortoisehg.vdiff in your Mercurial.ini"
            title = "Visual Diff Not Configured"
            win32ui.MessageBox(msg, title, win32con.MB_OK|win32con.MB_ICONERROR)
            return
        targets = self._filenames or [self._folder]
        root = find_root(targets[0])
        open_dialog(diff, root=root, filelist=targets, gui=False)

    def _view(self, parent_window):
        '''[tortoisehg] view = [hgk | hgview]'''
        view = ui.ui().config('tortoisehg', 'view', '')
        if not view:
            msg = "You must configure tortoisehg.view in your Mercurial.ini"
            title = "Revision Graph Tool Not Configured"
            win32ui.MessageBox(msg, title, win32con.MB_OK|win32con.MB_ICONERROR)
            return

        targets = self._filenames or [self._folder]
        root = find_root(targets[0])
        if view == 'hgview':
            hgviewpath = find_path('hgview')
            cmd = "%s --repository=%s" % \
                    (shellquote(hgviewpath), shellquote(root))
            if len(self._filenames) == 1:
                cmd += " --file=%s" % shellquote(self._filenames[0])
            run_program(cmd)
        else:
            if view == 'hgk':
                open_dialog('view', root=root, gui=False)
            else:
                msg = "Revision graph viewer %s not recognized" % view
                title = "Unknown history tool"
                win32ui.MessageBox(msg, title, win32con.MB_OK|win32con.MB_ICONERROR)

    def _history(self, parent_window):
        self._log(parent_window)

    def _clone_here(self, parent_window):
        src = self._filenames[0]
        dest = self._folder
        repo_name = os.path.basename(src)
        dest_clone = get_clone_repo_name(dest, repo_name)
        cmdopts = "--verbose"
        repos = [src, dest_clone]
        open_dialog('clone', cmdopts, cwd=dest, filelist=repos)

    def _push_here(self, parent_window):
        src = self._filenames[0]
        dest = self._folder
        msg = "Push changes from %s into %s?" % (src, dest)
        title = "Mercurial: push"
        rv = win32ui.MessageBox(msg, title, win32con.MB_OKCANCEL)
        if rv == 2:
            return

        cmdopts = "--verbose"
        open_dialog('push', cmdopts, root=src, filelist=[dest])

    def _pull_here(self, parent_window):
        src = self._filenames[0]
        dest = self._folder
        msg = "Pull changes from %s?" % (src)
        title = "Mercurial: pull"
        rv = win32ui.MessageBox(msg, title, win32con.MB_OKCANCEL)
        if rv == 2:
            return

        cmdopts = "--verbose"
        open_dialog('pull', cmdopts, root=src, filelist=[dest])

    def _incoming_here(self, parent_window):
        src = self._filenames[0]
        dest = self._folder
        cmdopts = "--verbose"
        open_dialog('incoming', cmdopts, root=src, filelist=[dest])

    def _outgoing_here(self, parent_window):
        src = self._filenames[0]
        dest = self._folder
        cmdopts = "--verbose"
        open_dialog('outgoing', cmdopts, root=src, filelist=[dest])

    def _init(self, parent_window):
        dest = self._folder or self._filenames[0]
        msg = "Create Hg repository in %s?" % (dest)
        title = "Mercurial: init"
        rv = win32ui.MessageBox(msg, title, win32con.MB_OKCANCEL)
        if rv == 2:
            return
        try:
            hg.repository(ui.ui(), dest, create=1)
        except:
            msg = "Error creating repo"
            win32ui.MessageBox(msg, title, 
                               win32con.MB_OK|win32con.MB_ICONERROR)
            
    def _status(self, parent_window):
        self._run_dialog('status')

    def _clone(self, parent_window):
        self._run_dialog('clone', True)

    def _synch(self, parent_window):
        self._run_dialog('synch', True)

    def _synch_here(self, parent_window):
        self._run_dialog('synch', False)
        
    def _pull(self, parent_window):
        self._run_dialog('pull', True)

    def _push(self, parent_window):
        self._run_dialog('push', True)

    def _incoming(self, parent_window):
        self._run_dialog('incoming', True)

    def _outgoing(self, parent_window):
        self._run_dialog('outgoing', True)

    def _serve(self, parent_window):
        self._run_dialog('serve', noargs=True)

    def _add(self, parent_window):
        self._run_dialog('add', modal=True)

    def _remove(self, parent_window):
        self._run_dialog('remove')

    def _revert(self, parent_window):
        self._run_dialog('status')

    def _tip(self, parent_window):
        self._run_dialog('tip', True)

    def _parents(self, parent_window):
        self._run_dialog('parents', True)

    def _heads(self, parent_window):
        self._run_dialog('heads', True)

    def _log(self, parent_window):
        self._run_dialog('log', verbose=False)

    def _show_tags(self, parent_window):
        self._run_dialog('tags', True, verbose=False)

    def _add_tag(self, parent_window):
        self._run_dialog('tag', True, verbose=False)

    def _diff(self, parent_window):
        self._run_dialog('diff')

    def _merge(self, parent_window):
        self._run_dialog('merge', noargs=True)

    def _recovery(self, parent_window):
        self._run_dialog('recovery')

    def _update(self, parent_window):
        self._run_dialog('update', noargs=True)

    def _grep(self, parent_window):
        # open datamine dialog with no file brings up a search tab
        self._run_dialog('datamine', noargs=True)

    def _annotate(self, parent_window):
        # open datamine dialog with files brings up the annotate
        # tabs for each file
        self._run_dialog('datamine')

    def _run_dialog(self, hgcmd, noargs=False, verbose=True, modal=False):
        if self._folder:
            cwd = self._folder
        elif self._filenames:
            f = self._filenames[0]
            cwd = os.path.isdir(f) and f or os.path.dirname(f)
        else:
            win32ui.MessageBox("Can't get cwd!", 'Hg ERROR', 
                   win32con.MB_OK|win32con.MB_ICONERROR)
            return

        targets = self._filenames or [self._folder]
        root = find_root(targets[0])
        filelist = []
        if noargs == False:
            filelist = targets
        cmdopts = "%s" % (verbose and "--verbose" or "")
        open_dialog(hgcmd, cmdopts, cwd=cwd, root=root, filelist=filelist)

    def _help(self, parent_window):
        open_dialog('help', '--verbose')

    def _about(self, parent_window):
        open_dialog('about')

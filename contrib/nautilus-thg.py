# Trivial Mercurial plugin for Nautilus
#
# Copyright (C) 2007 Steve Borho
#
# Stolen mercilessly from nautilus-bzr, thanks guys
# Copyright (C) 2006 Jeff Bailey
# Copyright (C) 2006 Wouter van Heyst
# Copyright (C) 2006 Jelmer Vernooij
#
# Published under the GNU GPL

import gconf
import gtk
import gobject
from mercurial import hg, ui, util, repo
from mercurial.node import short
import nautilus
import os
import subprocess
import sys
import tempfile
import time
import urllib

TORTOISEHG_PATH = '~/tools/tortoisehg-dev'
TERMINAL_KEY = '/desktop/gnome/applications/terminal/exec'

class HgExtension(nautilus.MenuProvider,
                  nautilus.ColumnProvider,
                  nautilus.InfoProvider,
                  nautilus.PropertyPageProvider):

    def __init__(self):
        self.cacherepo = None
        self.cacheroot = None
        self.client = gconf.client_get_default()
        thgpath = os.environ.get('TORTOISEHG_PATH',
                os.path.expanduser(TORTOISEHG_PATH))
        os.environ['TORTOISEHG_PATH'] = thgpath
        os.environ['THG_ICON_PATH'] = os.path.join(thgpath, 'icons')
        self.hgproc = os.path.join(thgpath, 'hgproc.py')
        self.ipath = os.path.join(thgpath, 'icons', 'tortoise')

    def icon(self, iname):
        return os.path.join(self.ipath, iname)

    def get_path_for_vfs_file(self, vfs_file):
        if vfs_file.get_uri_scheme() != 'file':
            return None
        return urllib.unquote(vfs_file.get_uri()[7:])

    def clear_cached_repo(self):
        self.cacheroot = None
        self.cacherepo = None

    def get_repo_for_path(self, path):
        '''
        Find mercurial repository for vfs_file
        Returns hg.repo
        '''
        p = os.path.isdir(path) and path or os.path.dirname(path)
        while not os.path.isdir(os.path.join(p, ".hg")):
            oldp = p
            p = os.path.dirname(p)
            if p == oldp:
                return None

        if p == self.cacheroot:
            return self.cacherepo
        # Keep one repo cached
        try:
            self.cacheroot = p
            self.cacherepo = hg.repository(ui.ui(), path=p)
            return self.cacherepo
        except repo.RepoError:
            self.cacheroot = None
            self.cacherepo = None
            return None

    def _open_terminal_cb(self, window, vfs_file):
        path = self.get_path_for_vfs_file(vfs_file)
        if path is None:
            return
        os.chdir(path)
        terminal = self.client.get_string(TERMINAL_KEY)
        os.system('%s &' % terminal)

    def _about_cb(self, window, vfs_file):
        self._run_dialog('about', [vfs_file])

    def _add_cb(self, window, vfs_files):
        self._run_dialog('add', vfs_files)
        self.clear_cached_repo()

    def _clone_cb(self, window, vfs_file):
        self._run_dialog('clone', [vfs_file])

    def _commit_cb(self, window, vfs_files):
        self._run_dialog('commit', vfs_files)
        self.clear_cached_repo()

    def _datamine_cb(self, window, vfs_files):
        self._run_dialog('datamine', vfs_files)

    def _diff_cb(self, window, vfs_files):
        path = self.get_path_for_vfs_file(vfs_files[0])
        if path is None:
            return
        repo = self.get_repo_for_path(path)
        if repo is None:
            return
        diffcmd = repo.ui.config('tortoisehg', 'vdiff', None)
        if diffcmd is None:
            self._run_dialog('diff', vfs_files)
        else:
            cmdline = ['hg', diffcmd]
            cwd = os.path.isdir(path) and path or os.path.dirname(path)
            paths = [self.get_path_for_vfs_file(f) for f in vfs_files]
            subprocess.Popen(cmdline + paths, shell=False, cwd=cwd)

    def _history_cb(self, window, vfs_files):
        self._run_dialog('history', vfs_files)
        self.clear_cached_repo()

    def _init_cb(self, window, vfs_file):
        self._run_dialog('init', [vfs_file])

    def _recovery_cb(self, window, vfs_file):
        self._run_dialog('recovery', [vfs_file])
        self.clear_cached_repo()

    def _revert_cb(self, window, vfs_files):
        self._run_dialog('revert', vfs_files)
        self.clear_cached_repo()

    def _serve_cb(self, window, vfs_file):
        self._run_dialog('serve', [vfs_file], filelist=False)

    def _status_cb(self, window, vfs_file):
        self._run_dialog('status', [vfs_file])

    def _sync_cb(self, window, vfs_file):
        self._run_dialog('synch', [vfs_file], filelist=False)
        self.clear_cached_repo()

    def _thgconfig_repo_cb(self, window, vfs_file):
        self._run_dialog('config', [vfs_file])

    def _thgconfig_user_cb(self, window, vfs_file):
        self._run_dialog('config', [vfs_file], filelist=False)

    def _unmerge_cb(self, window, vfs_file):
        self._run_dialog('checkout', [vfs_file], filelist=False,
                extras=['--', '--clean', str(self.rev0)])
        self.clear_cached_repo()

    def _run_dialog(self, hgcmd, vfs_files, filelist=True, extras=[]):
        '''
        hgcmd - hgproc subcommand
        vfs_files - directory, or list of selected files
        filelist  - bool for whether to generate file list for hgproc
        '''
        paths = [self.get_path_for_vfs_file(f) for f in vfs_files]
        if paths[0] is None:
            return

        path = paths[0]
        repo = self.get_repo_for_path(path)
        cwd = os.path.isdir(path) and path or os.path.dirname(path)

        cmdopts  = [sys.executable, self.hgproc]
        cmdopts += ['--root', repo.root]
        cmdopts += ['--cwd', cwd]
        cmdopts += ['--command', hgcmd]

        if filelist:
            # Use temporary file to store file list (avoid shell command
            # line limitations)
            fd, tmpfile = tempfile.mkstemp(prefix="tortoisehg_filelist_")
            os.write(fd, "\n".join(paths))
            os.close(fd)
            cmdopts += ['--listfile', tmpfile, '--deletelistfile']
        cmdopts.extend(extras)

        subprocess.Popen(cmdopts, cwd=cwd, shell=False)

        # Remove cached repo object, dirstate may change
        self.cacherepo = None
        self.cacheroot = None

    def get_background_items(self, window, vfs_file):
        '''Build context menu for current directory'''
        items = []
        path = self.get_path_for_vfs_file(vfs_file)
        if path is None:
            return
        repo = self.get_repo_for_path(path)
        if repo is None:
            item = nautilus.MenuItem('HgNautilus::newtree',
                                 'Create New Repository',
                                 'Make directory versioned',
                                 self.icon('menucreaterepos.ico'))
            item.connect('activate', self._init_cb, vfs_file)
            items.append(item)
            item = nautilus.MenuItem('HgNautilus::clone',
                                 'Create Clone',
                                 'Create clone here from source',
                                 self.icon('menuclone.ico'))
            item.connect('activate', self._clone_cb, vfs_file)
            items.append(item)
            item = nautilus.MenuItem('HgNautilus::about',
                                 'About TortoiseHg',
                                 'Information about TortoiseHg installation',
                                 self.icon('menuabout.ico'))
            item.connect('activate', self._about_cb, vfs_file)
            items.append(item)
            item = nautilus.MenuItem('HgNautilus::terminal',
                                 'Open Terminal Here',
                                 'Open terminal in current directory')
            item.connect('activate', self._open_terminal_cb, vfs_file)
            items.append(item)
            return items

        if len(repo.workingctx().parents()) > 1:
            self.rev0 = repo.workingctx().parents()[0].rev()
            item = nautilus.MenuItem('HgNautilus::undomerge',
                                 'Undo Merge',
                                 'Clean checkout of original parent revision',
                                 self.icon('menuunmerge.ico'))
            item.connect('activate', self._unmerge_cb, vfs_file)
            items.append(item)

        item = nautilus.MenuItem('HgNautilus::commit',
                             'Commit',
                             'Commit changes',
                             self.icon('menucommit.ico'))
        item.connect('activate', self._commit_cb, [vfs_file])
        items.append(item)

        item = nautilus.MenuItem('HgNautilus::status',
                             'Show Status',
                             'Show Repository Status',
                             self.icon('menushowchanged.ico'))
        item.connect('activate', self._status_cb, vfs_file)
        items.append(item)

        item = nautilus.MenuItem('HgNautilus::diff',
                             'Visual Diff',
                             'Show Changes to Repository',
                             self.icon('menudiff.ico'))
        item.connect('activate', self._diff_cb, [vfs_file])
        items.append(item)

        item = nautilus.MenuItem('HgNautilus::dag',
                             'Revision History',
                             'Show revision DAG',
                             self.icon('menurevisiongraph.ico'))
        item.connect('activate', self._history_cb, [vfs_file])
        items.append(item)

        item = nautilus.MenuItem('HgNautilus::datamine',
                             'Data Mining',
                             'Search revision history',
                             self.icon('menulog.ico'))
        item.connect('activate', self._datamine_cb, [vfs_file])
        items.append(item)

        item = nautilus.MenuItem('HgNautilus::sync',
                             'Synchronize',
                             'Sync with another repository',
                             self.icon('menusynch.ico'))
        item.connect('activate', self._sync_cb, vfs_file)
        items.append(item)

        item = nautilus.MenuItem('HgNautilus::serve',
                             'Web Server',
                             'Start internal web server',
                             self.icon('proxy.ico'))
        item.connect('activate', self._serve_cb, vfs_file)
        items.append(item)

        item = nautilus.MenuItem('HgNautilus::recover',
                             'Recovery',
                             'General repair and recovery of repository',
                             self.icon('general.ico'))
        item.connect('activate', self._recovery_cb, vfs_file)
        items.append(item)

        item = nautilus.MenuItem('HgNautilus::repoconfig',
                             'Repository Settings',
                             'Configure Mercurial settings for this repo',
                             self.icon('menusettings.ico'))
        item.connect('activate', self._thgconfig_repo_cb, vfs_file)
        items.append(item)

        item = nautilus.MenuItem('HgNautilus::userconfig',
                             'User-Global Settings',
                             'Configure global Mercurial settings',
                             self.icon('menusettings.ico'))
        item.connect('activate', self._thgconfig_user_cb, vfs_file)
        items.append(item)

        item = nautilus.MenuItem('HgNautilus::about',
                             'About TortoiseHg',
                             'Information about TortoiseHg installation',
                             self.icon('menuabout.ico'))
        item.connect('activate', self._about_cb, vfs_file)
        items.append(item)

        item = nautilus.MenuItem('HgNautilus::terminal',
                             'Open Terminal Here',
                             'Open terminal in current directory')
        item.connect('activate', self._open_terminal_cb, vfs_file)
        items.append(item)
        return items

    def get_file_items(self, window, vfs_files):
        '''Build context menu for selected files'''
        items = []
        if not vfs_files:
            return items

        vfs_file = vfs_files[0]
        path = self.get_path_for_vfs_file(vfs_file)
        repo = self.get_repo_for_path(path)
        if repo is None:
            if not vfs_file.is_directory():
                return items

            # Menu for unrevisioned subdirectory
            name = vfs_files[0].get_name()
            item = nautilus.MenuItem('HgNautilus::newtree',
                                 'Make directory versioned',
                                 'Create Repository in %s' % name,
                                 self.icon('menucreaterepos.ico'))
            item.connect('activate', self._init_cb, vfs_file)
            items.append(item)
            item = nautilus.MenuItem('HgNautilus::clone',
                                 'Create clone from source',
                                 'Create Clone in %s' % name,
                                 self.icon('menuclone.ico'))
            item.connect('activate', self._clone_cb, vfs_file)
            items.append(item)
            item = nautilus.MenuItem('HgNautilus::about',
                                 'About TortoiseHg',
                                 'Information about TortoiseHg installation',
                                 self.icon('menuabout.ico'))
            item.connect('activate', self._about_cb, vfs_file)
            items.append(item)
            item = nautilus.MenuItem('HgNautilus::terminal',
                                 'Open Terminal Here',
                                 'Open Terminal in %s' % name)
            item.connect('activate', self._open_terminal_cb, vfs_file)
            items.append(item)
            return items

        localpaths = []
        for vfs_file in vfs_files:
            path = self.get_path_for_vfs_file(vfs_file)
            if path is None:
                continue
            localpath = path[len(repo.root)+1:]
            localpaths.append(localpath)

        changes = repo.dirstate.status(localpaths, util.always, True, True)
        (lookup, modified, added, removed, deleted, unknown,
                ignored, clean) = changes

        # Add menu items based on states list
        if unknown:
            item = nautilus.MenuItem('HgNautilus::add',
                                 'Add Files',
                                 'Add unversioned files',
                                 self.icon('menuadd.ico'))
            item.connect('activate', self._add_cb, vfs_files)
            items.append(item)

        if modified or added or removed or deleted or unknown:
            item = nautilus.MenuItem('HgNautilus::commit',
                                 'Commit Files',
                                 'Commit changes',
                                 self.icon('menucommit.ico'))
            item.connect('activate', self._commit_cb, vfs_files)
            items.append(item)
            item = nautilus.MenuItem('HgNautilus::revert',
                                 'Undo Changes',
                                 'Revert changes to files',
                                 self.icon('menurevert.ico'))
            item.connect('activate', self._revert_cb, vfs_files)
            items.append(item)

        if modified or clean:
            item = nautilus.MenuItem('HgNautilus::log',
                                 'File Changelog',
                                 'Show file revision history',
                                 self.icon('menulog.ico'))
            item.connect('activate', self._history_cb, vfs_files)
            items.append(item)
            item = nautilus.MenuItem('HgNautilus::annotate',
                                 'Annotate File',
                                 'Annotate file at current revision',
                                 self.icon('menulog.ico'))
            item.connect('activate', self._datamine_cb, vfs_files)
            items.append(item)


        if modified:
            item = nautilus.MenuItem('HgNautilus::diff',
                                 'File Diffs',
                                 'Show file changes',
                                 self.icon('menudiff.ico'))
            item.connect('activate', self._diff_cb, vfs_files)
            items.append(item)

        return items

    def get_columns(self):
        return nautilus.Column("HgNautilus::hg_status",
                               "hg_status",
                               "HG Status",
                               "Version control status"),

    def _get_file_status(self, repo, localpath):
        emblem = None
        status = '?'

        # This is not what the API is optimized for, but this appears
        # to work efficiently enough
        changes = repo.dirstate.status([localpath], util.always, True, True)
        (lookup, modified, added, removed, deleted, unknown,
                ignored, clean) = changes

        if localpath in clean:
            emblem = 'default'
            status = 'clean'
        elif localpath in modified:
            emblem = 'cvs-modified'
            status = 'modified'
        elif localpath in added:
            emblem = 'cvs-aded'
            status = 'added'
        elif localpath in unknown:
            emblem = 'new'
            status = 'unrevisioned'
        elif localpath in ignored:
            status = 'ignored'
        elif localpath in deleted:
            # Should be hard to reach this state
            emblem = 'stockmail-priority-high'
            status = 'deleted'
        return emblem, status


    def update_file_info(self, file):
        '''Return emblem and hg status for this file'''
        path = self.get_path_for_vfs_file(file)
        if path is None or file.is_directory():
            return
        repo = self.get_repo_for_path(path)
        if repo is None:
            return
        localpath = path[len(repo.root)+1:]
        emblem, status = self._get_file_status(repo, localpath)
        if emblem is not None:
            file.add_emblem(emblem)
        file.add_string_attribute('hg_status', status)

    # property page borrowed from http://www.gnome.org/~gpoo/hg/nautilus-hg/
    def __add_row(self, table, row, label_item, label_value):
        label = gtk.Label(label_item)
        label.set_use_markup(True)
        label.set_alignment(1, 0)
        table.attach(label, 0, 1, row, row + 1, gtk.FILL, gtk.FILL, 0, 0)
        label.show()

        label = gtk.Label(label_value)
        label.set_use_markup(True)
        label.set_alignment(0, 1)
        label.show()
        table.attach(label, 1, 2, row, row + 1, gtk.FILL, 0, 0, 0)

    def get_property_pages(self, vfs_files):
        if len(vfs_files) != 1:
            return
        file = vfs_files[0]
        path = self.get_path_for_vfs_file(file)
        if path is None or file.is_directory():
            return
        repo = self.get_repo_for_path(path)
        if repo is None:
            return

        localpath = path[len(repo.root)+1:]
        emblem, status = self._get_file_status(repo, localpath)

        # Get the information from Mercurial
        ctx = repo.workingctx().parents()[0]
        try:
            fctx = ctx.filectx(localpath)
            rev = fctx.filelog().linkrev(fctx.filenode())
        except:
            rev = ctx.rev()
        ctx = repo.changectx(rev)
        node = short(ctx.node())
        date = time.strftime("%Y-%m-%d %H:%M:%S", time.gmtime(ctx.date()[0]))
        parents = '\n'.join([short(p.node()) for p in ctx.parents()])
        description = ctx.description()
        user = ctx.user()
        user = gobject.markup_escape_text(user)
        tags = ', '.join(ctx.tags())
        branch = ctx.branch()

        self.property_label = gtk.Label('Mercurial')

        table = gtk.Table(7, 2, False)
        table.set_border_width(5)
        table.set_row_spacings(5)
        table.set_col_spacings(5)

        self.__add_row(table, 0, '<b>Status</b>:', status)
        self.__add_row(table, 1, '<b>Last-Commit-Revision</b>:', str(rev))
        self.__add_row(table, 2, '<b>Last-Commit-Description</b>:', description)
        self.__add_row(table, 3, '<b>Last-Commit-Date</b>:', date)
        self.__add_row(table, 4, '<b>Last-Commit-User</b>:', user)
        if tags:
            self.__add_row(table, 5, '<b>Tags</b>:', tags)
        if branch != 'default':
            self.__add_row(table, 6, '<b>Branch</b>:', branch)

        table.show()

        return nautilus.PropertyPage("MercurialPropertyPage::status",
                                     self.property_label, table),

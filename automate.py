# -*- coding: utf-8 -*-
"""
Trac Plugin for Monotone

Copyright 2006-2008 Thomas Moschny (thomas.moschny@gmx.de)

{{{
This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or (at
your option) any later version.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
USA
}}}

"""

from subprocess import Popen, PIPE
try:
    from threading import Lock
except ImportError:
    from dummy_threading import Lock #IGNORE:E0611
from tracmtn import basic_io
from tracmtn.util import add_slash, to_unicode, natsort_key


class AutomateException(Exception):
    """Thrown when the status of an automate command
    is not null, indicating that there is no valid result."""


class Automate(object):
    """General interface to the 'automate stdio' command."""

    def __init__(self, database, binary):
        self.process = Popen(
            (binary, '--norc', '--root=.', '--automate-stdio-size=1048576',
             '--db=%s' % database, 'automate', 'stdio'),
            stdin=PIPE, stdout=PIPE, stderr=None)
        self.lock = Lock()

    def _write(self, data):
        """Write data to automate process."""
        return self.process.stdin.write(data)

    def _flush(self):
        """Send flush to automate process."""
        return self.process.stdin.flush()

    def _read(self, maxlen = -1):
        """Read maxlen bytes from automate process."""
        return self.process.stdout.read(maxlen)

    def _read_until_colon(self):
        """Return bytes until and excluding next colon."""
        result = ''
        while True:
            char = self._read(1)
            if char == ':':
                break
            elif not char:
                raise AutomateException("EOF while reading from Monotone")
            result += char
        return result

    def _read_packet(self):
        """Read exactly one chunk of Monotone automate output."""
        _      = self._read_until_colon() # ignore the cmd number
        status = int(self._read_until_colon())
        cont   = self._read_until_colon()
        size   = int(self._read_until_colon())
        val    = self._read(size)
        return status, cont, val

    def _get_result(self):
        """Read and concatenate the result packets."""
        result = ''
        while True:
            status, cont, val = self._read_packet()
            result += val
            if cont == 'l':
                break
        return status, result

    def _write_cmd(self, cmd, args, opts):
        """Assemble the cmdline from command, args and opts and send
        it to mtn."""

        def lstring(string):
            """Prepend string with its length followed by a colon."""
            return "%d:%s" % (len(string), string)

        cmdstring = ""

        if opts:
            cmdstring += "o"
            for name, val in opts.iteritems():
                cmdstring += lstring(name) + lstring(val)
            cmdstring += "e"

        cmdstring += "l"
        cmdstring += lstring(cmd)
        for arg in args:
            cmdstring += lstring(arg)
        cmdstring += "e"
        self._write(cmdstring)
        self._flush()

    def command(self, cmd, args=[], opts={}):
        """Send a command to mtn. Returns a tuple (status, result)."""
        # critical region: only one thread may send a command and read
        # back the result at a time
        self.lock.acquire()
        try:
            if self.process.poll():
                raise AutomateException("Monotone process died")
            self._write_cmd(cmd, args, opts)
            status, result = self._get_result()
        finally:
            self.lock.release()
        if status == 0:
            return result
        raise AutomateException("Monotone error code %d: %s (%s)" %
                                (status, to_unicode(result), cmd))


class MTN(object):
    """Connect to a Monotone repository using the automation interface."""

    def __init__(self, database, log, binary):
        self.automate = Automate(database, binary)
        self.log = log
        self.roots_cache = []
        self.interface_version = None

    def leaves(self):
        """Returns a list containing the current leaves."""
        return self.automate.command("leaves").splitlines()

    def heads(self, name):
        """Returns a list containing the head revs of branch 'name'."""
        return self.automate.command("heads", [name]).splitlines()

    def children(self, rev):
        """Returns a list of the children of rev."""
        return self.automate.command("children", [rev]).splitlines()

    def parents(self, rev):
        """Returns a list of the parents of rev."""
        return self.automate.command("parents", [rev]).splitlines()

    def ancestors(self, revs):
        """Returns a list of the ancestors of rev."""
        return self.automate.command("ancestors", revs).splitlines()

    def toposort(self, revs):
        """Sorts revisions topologically."""
        return self.automate.command("toposort", revs).splitlines()

    def all_revs(self):
        """Returns a list of all revs in the repository."""
        return self.automate.command("select", ['']).splitlines()

    def roots(self):
        """Returns a list of all root revisions."""
        if self.roots_cache:
            return self.roots_cache
        if self.min_interface_version('4.3'):
            roots = self.automate.command("roots").splitlines()
        else:
            roots = []
            for line in self.automate.command("graph").splitlines():
                rev_and_parents = line.split(' ')
                if len(rev_and_parents) == 1:
                    roots.append(rev_and_parents[0])
        self.roots_cache = roots
        return roots

    def select(self, selector):
        """Returns a list of revisions selected by the selector."""
        return self.automate.command("select",
                 [selector.encode('utf-8')]).splitlines()

    def manifest(self, rev):
        """ Returns a processed manifest for rev.

        The manifest is a dictionary: path -> (kind, file_id, attrs),
        with kind being 'file' or 'dir', and attrs being a dictionary
        attr_name -> attr_value."""
        raw_manifest = self.automate.command("get_manifest_of", [rev])
        manifest = {}

        # stanzas have variable length, trigger on next 'path' ...
        path, kind, content, attrs = None, None, None, {}
        for key, values in basic_io.items(raw_manifest):
            if key == 'dir' or key == 'file':
                if path:
                    manifest[path] = (kind, content, attrs)
                path = add_slash(to_unicode(values[0]))
                kind, content, attrs = key, None, {}
            elif key == 'content':
                content = values[0]
            elif key == 'attrs':
                attrs[to_unicode(values[0])] = to_unicode(values[1])
        if path: #  ... or eof
            manifest[path] = (kind, content, attrs)
        return manifest

    def certs(self, rev):
        """Returns a dictionary of certs for rev. There might be more
        than one cert of the same name, so their values are collected
        in a list."""
        raw_certs = self.automate.command("certs", [rev])
        certs = {}

        for key, values in basic_io.items(raw_certs):
            if key == 'name':
                name = to_unicode(values[0])
            elif key == 'value':
                value = to_unicode(values[0])
                certs.setdefault(name, []).append(value)
        return certs

    def get_file(self, file_id):
        """Returns the file contents for a given file id."""
        return self.automate.command("get_file", [file_id])

    def file_length(self, file_id):
        """Return the file length."""
        return len(self.get_file(file_id))

    def changesets(self, rev):
        """Parses a textual changeset into an instance of the
        Changeset class."""
        raw_changesets = self.automate.command("get_revision", [rev])

        changesets = []
        oldpath = None
        for key, values in basic_io.items(raw_changesets):
            if key == 'old_revision':
                # start a new changeset
                changeset = Changeset(values[0])
                changesets.append(changeset)
                oldpath = None
            elif key == 'delete':
                path = add_slash(to_unicode(values[0]))
                changeset.deleted.append(path)
            elif key == 'rename':
                oldpath = add_slash(to_unicode(values[0]))
            elif key == 'to':
                if oldpath != None:
                    newpath = add_slash(to_unicode(values[0]))
                    changeset.renamed[newpath] = oldpath
                    oldpath = None
            elif key == 'add_dir':
                path = add_slash(to_unicode(values[0]))
                changeset.added[path] = 'dir'
            elif key == 'add_file':
                path = add_slash(to_unicode(values[0]))
                changeset.added[path] = 'file'
            elif key == 'patch':
                path = add_slash(to_unicode(values[0]))
                changeset.patched.append(path)
            # fixme: what about 'set' and 'clear'?  These are edits,
            # but not if applied to new files.
        return changesets

    def branchnames(self):
        """Returns a list of branch names."""
        return map(to_unicode, #IGNORE:W0141
           self.automate.command("branches").splitlines())

    def branches(self):
        """Returns a list of (branch, oneoftheheads) tuples. Caveat:
        this method is really slow."""
        branches = []
        for branch in self.branchnames():
            revs = self.heads(branch)
            if revs:
                branches.append((branch, revs[0]))
                # multiple heads not supported
        return branches

    def non_merged_branches(self):
        """Returns a list of (branch, rev) tuples for all leave revs."""
        leaves = []
        for leave in self.leaves():
            branches = self.certs(leave)['branch']
            for branch in branches:
                leaves.append((branch, leave))
        leaves.sort(key=lambda i: i[0])
        return leaves

    def tags(self):
        """Returns a list of tags and their revs."""
        raw_tags = self.automate.command("tags")
        tags = []

        for key, values in basic_io.items(raw_tags):
            if key == 'tag':
                tag = to_unicode(values[0])
            elif key == 'revision':
                revision = values[0]
                tags.append((tag, revision))
        tags.sort(key=lambda i: natsort_key(i[0]))
        return tags

    def content_changed(self, rev, path):
        """Returns the list of content marks for the path, starting at
        the specified revision.

        Currently returns an empty list for directories."""
        raw_content_changed = self.automate.command("get_content_changed",
                                [rev, path[1:]])
        revs = []
        for key, values in basic_io.items(raw_content_changed):
            if key == 'content_mark':
                revs.append(values[0])
        return revs

    def get_interface_version(self):
        """Returns the automation interface version."""
        if not self.interface_version:
            self.interface_version = self.automate.command(
                "interface_version").strip()
        return self.interface_version

    def min_interface_version(self, v):
        return natsort_key(self.get_interface_version()) \
            >= natsort_key(v)

class Changeset(object):
    """Represents a monotone changeset in parsed form."""

    def __init__(self, oldrev):
        self.oldrev = oldrev             # the old rev this cs is against
        self.added = {}                  # nodename -> kind
        self.renamed = {}                # newname -> oldname
        self.patched = []                # list of newnames
        self.deleted = []                # list of oldnames

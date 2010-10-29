#!/usr/bin/env python
#
# Copyright (C) Nathaniel Smith <njs@pobox.com>
#               Timothy Brownawell <tbrownaw@gmail.com>
#               Thomas Moschny <thomas.moschny@gmx.de>
#               Richard Levitte <richard@levitte.org>
# Licensed under the MIT license:
#   http://www.opensource.org/licenses/mit-license.html
# I.e., do what you like, but keep copyright and there's NO WARRANTY.
#
# CIA bot client script for Monotone repositories, written in python.  This
# generates commit messages using CIA's XML commit format, and can deliver
# them using either XML-RPC or email.  Based on the script 'ciabot_svn.py' by
# Micah Dowty <micah@navi.cx>.

# This version is modified to be called by a server hook, instead of a cron job.

# To use:
#   -- create a configuration file called ciabot.conf in the server's
#      configuration directory (whatever is returned by get_confdir()).
#      That configuration file follows basic-io syntax, the comments in
#      the config class is the best documentation for now.
#   -- include ciabot_monotone_hookversion.lua in the server's monotonerc

import sys
import re
import os
import fnmatch

TOKEN = re.compile(r'''
    "(?P<str>(\\\\|\\"|[^"])*)"
    |\[(?P<id>[a-f0-9]{40}|)\]
    |(?P<key>\w+)
    |(?P<ws>\s+)
''', re.VERBOSE)

def parse_basic_io(raw):
    parsed = []
    key = None
    for m in TOKEN.finditer(raw):
        if m.lastgroup == 'key':
            if key:
                parsed.append((key, values))
            key = m.group('key')
            values = []
        elif m.lastgroup == 'id':
            values.append(m.group('id'))
        elif m.lastgroup == 'str':
            value = m.group('str')
            # dequote: replace \" with "
            value = re.sub(r'\\"', '"', value)
            # dequote: replace \\ with \
            value = re.sub(r'\\\\', r'\\', value)
            values.append(value)
    if key:
        parsed.append((key, values))
    return parsed

class config:
    # The server to deliver XML-RPC messages to, if using XML-RPC delivery.
    # In the configuration file, this is set with the key 'xmlrpc_server'
    xmlrpc_server = "http://cia.navi.cx"

    # The 'from' address to put on email, if using email delivery.
    # In the configuration file, this is set with the key 'from_address'
    from_address = "cia-user@FIXME"

    # The email address to deliver messages to, if using email delivery.
    # In the configuration file, this is set with the key 'to_address'
    smtp_address = "cia@cia.navi.cx"

    # The SMTP server to connect to, if using email delivery.
    # In the configuration file, this is set with the key 'smtp_server'
    smtp_server = "localhost"

    # Set to one of "xmlrpc", "email", "debug".
    # In the configuration file, this is set with the key 'delivery'
    delivery = "debug"

    # The default URL template to be used when none other is found.
    # In the configuration file, this is set with the key 'url'.
    # NOTE: THIS MUST COME BEFORE ANY 'pattern'
    # The URL template can contain %s constructs as follows:
    #   %(revision)s      is replaced with the revision identity
    #   %(branch)s        is replaced with the branch name
    #   %(projectid)s     is replaced with the project identity
    #   %(projectname)s   is replaced with the project name
    default_url = None
    
    # These are three dicionaries, where the first stores project identities
    # keyed with globs and the others store project names for CIA and commit
    # URLs, both keyed with project identities.
    # In the configuration file, this is set with stanzas started with
    # the key(s) 'pattern' followed by the keys 'projectid' (project id),
    # 'projectname' (project name) and 'url' (URL template) to set the
    # values.  Each stanza can have several patterns but only one of
    # 'projectid', 'projectname' and 'url'.
    # You MUST define a project identity in each stanza.
    # Project names are names that are used officially with CIA.  if
    # undefined, the project identity is used.
    projectids = {}
    projectnames = {}
    urls = {}

    # This is internal, an array to keep the patterns in order.
    # THIS MEANS THE ORDER IN THE CONFIGURATION FILE IS SIGNIFICANT!
    patterns = []

    def __init__(self, config_file):
        s = ""
        with open(config_file) as f:
            for line in f:
                s = s + line
        previous_key = ""
        current_patterns = []
        for key, value in parse_basic_io(s):
            if key == 'xmlrpc_server':
                self.xmlrcp_server = value[0]
            elif key == 'to_address':
                self.smtp_address = value[0]
            elif key == 'from_address':
                self.from_address = value[0]
            elif key == 'smtp_server':
                self.smtp_server = value[0]
            elif key == 'delivery':
                self.delivery = value[0]
            elif key == 'pattern':
                if previous_key != key:
                    # new series of patterns, new stanza
                    current_patterns = []
                self.patterns.append(value[0])
                current_patterns.append(value[0])
            elif key == 'projectid':
                for p in current_patterns:
                    self.projectids[p] = value[0]
            elif key == 'projectname':
                for p in current_patterns:
                    self.projectnames[p] = value[0]
            elif key == 'url':
                url_set = False
                for p in current_patterns:
                    self.urls[p] = value[0]
                    url_set = True
                if not url_set:
                    # It means we haven't seen any pattern yet, and
                    # therefore have a default URL
                    self.default_url = value[0]
            previous_key = key

    def _pattern_for_branch(self, branchname):
        l = [ p for p in self.patterns if fnmatch.fnmatchcase(branchname, p) ]
        if len(l) > 0:
            return l[0]
        return None
        
    def projectid_for_branch(self, branchname):
        pat = self._pattern_for_branch(branchname)
        if pat:
            return self.projectids[pat]
        return None

    def projectname_for_branch(self, branchname):
        pat = self._pattern_for_branch(branchname)
        if pat:
            return self.projectnames[pat] if pat in self.projectnames else self.projectids[pat]
        return None

    def url_for_revision(self, branchname, revid):
        pat = self._pattern_for_branch(branchname)
        if pat:
            substs = {}
            substs["revision"] = revid;
            substs["branch"] = branchname;
            substs["projectid"] = self.projectids[pat];
            substs["projectname"] = self.projectnames[pat] if pat in self.projectnames else self.projectids[pat]
            return (self.urls[pat] if pat in self.urls else self.default_url) % substs
        return ""

################################################################################

def escape_for_xml(text, is_attrib=0):
    text = text.replace("&", "&amp;")
    text = text.replace("<", "&lt;")
    text = text.replace(">", "&gt;")
    if is_attrib:
        text = text.replace("'", "&apos;")
        text = text.replace("\"", "&quot;")
    return text

def send_message(message, c):
    if c.delivery == "debug":
        print message
    elif c.delivery == "xmlrpc":
        import xmlrpclib
        xmlrpclib.ServerProxy(c.xmlrpc_server).hub.deliver(message)
    elif c.delivery == "email":
        import smtplib
        smtp = smtplib.SMTP(c.smtp_server)
        smtp.sendmail(c.from_address, c.smtp_address,
                      "From: %s\r\nTo: %s\r\n"
                      "Subject: DeliverXML\r\n\r\n%s"
                      % (c.from_address, c.smtp_address, message))
    else:
        sys.exit("delivery option must be one of 'debug', 'xmlrpc', 'email'")

def send_change_for(rid, branch, author, log, rev, c):
    message_tmpl = """<message>
    <generator>
        <name>Monotone CIA Bot client python script</name>
        <version>0.9</version>
    </generator>
    <source>
        <project>%(project)s</project>
        <branch>%(branch)s</branch>
    </source>
    <body>
        <commit>
            <revision>%(rid)s</revision>
            <author>%(author)s</author>
            <files>%(files)s</files>
            <log>%(log)s</log>
            %(extra)s
        </commit>
    </body>
</message>"""
    
    substs = {}
    files = []
    for key, values in parse_basic_io(rev):
        if key == 'old_revision':
            # start a new changeset
            oldpath = None
        if key == 'delete':
            files.append('<file action="remove">%s</file>'
                         % escape_for_xml(values[0]))
        elif key == 'rename':
            oldpath = values[0]
        elif key == 'to':
            if oldpath:
                files.append('<file action="rename" to="%s">%s</file>'
                             % (escape_for_xml(values[0]), escape_for_xml(oldpath)))
                oldpath = None
        elif key == 'add_dir':
            files.append('<file action="add">%s</file>'
                         % escape_for_xml(values[0] + '/'))
        elif key == 'add_file':
            files.append('<file action="add">%s</file>'
                          % escape_for_xml(values[0]))
        elif key == 'patch':
            files.append('<file action="modify">%s</file>'
                         % escape_for_xml(values[0]))
            
    substs["files"] = "\n".join(files)
    changelog = log.strip()
    project = c.projectname_for_branch(branch)
    if project is None:
        return
    commiturl = c.url_for_revision(branch, rid)
    substs["author"] = escape_for_xml(author)
    substs["project"] = escape_for_xml(project)
    substs["branch"] = escape_for_xml(branch)
    substs["rid"] = escape_for_xml(rid)
    substs["log"] = escape_for_xml(changelog)
    substs["extra"] = ""
    if not commiturl is None:
        substs["extra"] += '<url>%s</url>' % escape_for_xml(commiturl)

    message = message_tmpl % substs
    send_message(message, c)

def main(progname, args):
    if len(args) != 6:
        sys.exit("Usage: %s confdir revid branch author changelog revision_text" % (progname, ))
    # We don't want to clutter the process table with zombies; but we also
    # don't want to force the monotone server to wait around while we call the
    # CIA server.  So we fork -- the original process exits immediately, and
    # the child continues (orphaned, so it will eventually be reaped by init).
    if hasattr(os, "fork"):
        if os.fork():
            return
    (confdir, rid, branch, author, log, rev, ) = args
    c = config(confdir + "/ciabot.conf")
    send_change_for(rid, branch, author, log, rev, c)

if __name__ == "__main__":
    main(sys.argv[0], sys.argv[1:])

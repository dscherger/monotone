# we need paramiko for sftp protocol support
import paramiko
import getpass
import fs
import os.path
import posixpath
import base64

# All of this heavily cribbed from demo{,_simple}.py in the paramiko
# distribution, which is LGPL.
def load_host_keys(hostkeyfile):
    # this file won't exist on windows, but windows doesn't have a standard
    # location for this file anyway.
    filename = os.path.expanduser(hostkeyfile)
    keys = {}
    try:
        f = open(filename, 'r')
    except Exception, e:
        print '*** Unable to open host keys file (%s)' % filename
        return
    for line in f:
        keylist = line.split(' ')
        if len(keylist) != 3:
            continue
        hostlist, keytype, key = keylist
        hosts = hostlist.split(',')
        for host in hosts:
            if not keys.has_key(host):
                keys[host] = {}
            if keytype == 'ssh-rsa':
                keys[host][keytype] = paramiko.RSAKey(data=base64.decodestring(key))
            elif keytype == 'ssh-dss':
                keys[host][keytype] = paramiko.DSSKey(data=base64.decodestring(key))
    f.close()
    return keys

def get_user_password_host_port(hostspec):
    username, password, hostname, port = None, None, None, None
    if hostspec.find("@") >= 0:
        userspec, hostspec = hostspec.split("@")
        if userspec.find(":") >= 0:
            username, password = userspec.split(":")
        else:
            username = userspec
    if hostspec.find(":") >= 0:
        hostname, port_str = hostspec.split(":")
        port = int(port_str)
    else:
        hostname = hostspec
        port = 22
    assert None not in (hostname, port)
    if username is None:
        username = getpass.getuser()
    # FIXME: support agents etc. (see demo.py in paramiko dist)
    if password is None:
        password = getpass.getpass("Password for %s@%s: " % (username, hostname))
    return username, password, hostname, port

def get_host_key(hostname, hostkeyfile):
    hkeys = load_host_keys(hostkeyfile)
    if hkeys.has_key(hostname):
        return hkeys[hostname].values()[0]
    else:
        return None

class SFTPReadableFS(fs.ReadableFS):
    def __init__(self, hostspec, path, **kwargs):
        self.dir = path
        username, password, hostname, port = get_user_password_host_port(hostspec)

        hostkeyfile = kwargs.get('hostfile','~/.ssh/known_hosts')
        hostkey = get_host_key(hostname, hostkeyfile)

        key = None
        if kwargs.has_key("dsskey"):
            keypath=os.path.expanduser(kwargs["dsskey"])
            key = paramiko.DSSKey.from_private_key_file(keypath, 
                                password=password)
        elif kwargs.has_key("rsakey"):
            keypath=os.path.expanduser(kwargs["rsakey"])
            key = paramiko.RSAKey.from_private_key_file(keypath, 
                                password=password)

        self.transport = paramiko.Transport((hostname, port))
        if key:
            self.transport.connect(username=username, pkey=key,
                               hostkey=hostkey)
        else:
            self.transport.connect(username=username, password=password,
                               hostkey=hostkey)
        self.client = self.transport.open_sftp_client()
        
    def _fname(self, filename):
        return os.path.join(self.dir, filename)

    def open_read(self, filename):
        return self.client.open(self._fname(filename), "rb")

    def fetch(self, filenames):
        files = {}
        for fn in filenames:
            try:
                f = self.open_read(fn)
                files[fn] = f.read()
                f.close()
            except IOError:
                files[fn] = None
        return files

    def fetch_bytes(self, filename, bytes):
        f = self.open_read(filename)
        for offset, length in bytes:
            f.seek(offset)
            yield ((offset, length), f.read(length))

class SFTPWriteableFS(SFTPReadableFS, fs.WriteableFS):
    def open_append(self, filename):
        return self.client.open(self._fname(filename), "ab")

    def size(self, filename):
        try:
            return self.client.stat(self._fname(filename)).st_size
        except IOError:
            return 0

    def _exists(self, full_fn):
        try:
            self.client.stat(full_fn)
        except IOError:
            return False
        return True

    def put(self, filenames):
        for fn, data in filenames.iteritems():
            tmpname = self._fname("__tmp")
            tmph = self.client.open(tmpname, "wb")
            tmph.write(data)
            tmph.close()
            ## This is a race!  SFTP (at least until protocol draft 3, which
            ## is what paramiko and openssh both implement) only has
            ## non-clobbering rename.
            full_fn = self._fname(fn)
            # clobber any backup unconditionally, just in case, ignoring
            # errors
            try:
                self.client.remove(full_fn + ".back")
            except IOError:
                pass
            # then try moving it out of the way, again ignoring errors (maybe
            # it doesn't exist to move, which is fine)
            try:
                self.client.rename(full_fn, full_fn + ".back")
            except IOError:
                pass
            # finally, try clobbering it; this is he only operation that we
            # actually care about the success of, and this time we do check
            # for errors -- but do a local rollback if we can.
            try:
                self.client.rename(tmpname, full_fn)
            except IOError:
                if self._exists(full_fn + ".back"):
                    self.client.rename(full_fn + ".back", full_fn)
                raise
            # and clobber the backup we made (if it exists)
            try:
                self.client.remove(full_fn + ".back")
            except IOError:
                pass

    def rollback_interrupted_puts(self, filenames):
        for fn in filenames:
            full_fn = self._fname(fn)
            if not self._exists(full_fn) and self._exists(full_fn + ".back"):
                self.client.rename(full_fn + ".back", full_fn)

    def mkdir(self, filename):
        try:
            self.client.mkdir(self._fname(filename))
        except IOError:
            return 0
        return 1

    def rmdir(self, filename):
        try:
            self.client.rmdir(self._fname(filename))
        except IOError:
            pass

    def ensure_dir(self, absdir=None):
        if absdir is None:
            absdir = self._fname("")
        absdir = posixpath.normpath(absdir)
        print "ensuring dir %s" % absdir
        try:
            self.client.stat(absdir)
            print "stat succeeded"
            return
        except IOError, e:
            print "stat failed: %s" % (e,)
            pass  # fall through to actually create dir
        # logic cribbed from os.makedirs in python dist
        head, tail = os.path.split(absdir)
        if not tail:
            head, tail = os.path.split(head)
        if head and tail:
            print "recursing to %s" % head
            self.ensure_dir(head)
        if tail == ".":
            return
        print "actually making %s" % absdir
        self.client.mkdir(absdir)

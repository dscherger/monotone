# we need paramiko for sftp protocol support
import paramiko
import getpass
import fs
import os.path
import base64

# All of this heavily cribbed from demo{,_simple}.py in the paramiko
# distribution, which is LGPL.
def load_host_keys():
    # this file won't exist on windows, but windows doesn't have a standard
    # location for this file anyway.
    filename = os.path.expanduser('~/.ssh/known_hosts')
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
        hostname, port = hostspec.split(":")
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

def get_host_key(hostname):
    hkeys = load_host_keys()
    if hkeys.has_key(hostname):
        return hkeys[hostname].values()[0]
    else:
        return None

class SFTPReadableFS(fs.ReadableFS):
    def __init__(self, hostspec, path):
        self.dir = path
        username, password, hostname, port = get_user_password_host_port(hostspec)
        hostkey = get_host_key(hostname)
        self.transport = paramiko.Transport((hostname, port))
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
        return self.client.stat(self._fname(filename)).st_size

    def put(self, filenames):
        for fn, data in filenames.iteritems():
            tmpname = self._fname("__tmp")
            tmph = self.client.open(tmpname, "wb")
            tmph.write(data)
            tmph.close()
            self.client.rename(tmpname, self._fname(fn))

    def rollback_interrupted_puts(self, filenames):
        # for now, we assume we have atomic put
        pass

    def mkdir(self, filename):
        try:
            self.client.mkdir(self._fname(filename))
        except IOError:
            return 0
        return 1

    def rmdir(self, filename):
        self.client.rmdir(self._fname(filename))

    def ensure_dir(self):
        try:
            self.client.stat(self._fname(""))
            return
        except IOError:
            pass  # fall through to actually create dir
        pieces = []
        rest = self.dir
        while rest:
            (rest, next_piece) = os.path.split(rest)
            pieces.insert(0, next_piece)
        sofar = ""
        for piece in pieces:
            sofar = os.path.join(sofar, piece)
            try:
                self.client.mkdir(sofar)
            except OSError:
                pass

#
# This file is part of 'mtnplain'
#
# Copyright (C) Zbigniew Zagorski <zzbigg@o2.pl>
# licensed to the public under the terms of the GNU GPL (>= 2)
# see the file COPYING for details
# I.e., do what you like, but keep copyright and there's NO WARRANTY.
#

# we need paramiko for sftp protocol support
import fs
import os.path
import posixpath
import base64
import dws.dws
import sys
import StringIO

class DWSReadableFS(fs.ReadableFS):
    def __init__(self, hostspec):
        pathIndex = hostspec.rfind(":")
        if pathIndex == -1:
            self.path = ""
            self.hostspec = hostspec
        else:
            self.path = hostspec[pathIndex+1:]
            self.hostspec = hostspec[:pathIndex]
        self.conn = dws.Connection(self.hostspec,self.path)
        self.version = 0

    def list(self):
        return self.conn.list()
        
    def get(self, name):
        if name not in self.list(): raise IOError("dws: not found: %s" % name)
        content = self.conn.get(name)
        return content

    def put(self, name, content):
        return self.conn.put(name, content)

    def open_read(self, filename):
        content = self.get(filename)
        return StringIO.StringIO(content)

    def fetch(self, filenames):
        files = {}
        list = self.list()
        for fn in filenames:
            try:
                if fn not in list: raise IOError("not found: %s" % fn)
                files[fn] = self.conn.get(fn)
            except IOError,e:
                files[fn] = None
        return files

    def _real_fetch_bytes(self, filename, bytes):
        fc = self.conn.getParts(filename, bytes)
        resultOffset = 0
        for offset, length in bytes:
            yield ((offset, length), fc[resultOffset:resultOffset+length])
            resultOffset += length

class AppendableFileFake:
    def __init__(self, conn,name):
        self.conn = conn
        self.name = name
        self.content = ""
        self._need_flush = False
        
    def write(self, a):
        self.content += a
        self._need_flush = True
        return len(a)
        
    def flush(self):
        return self._flush()
        
    def close(self):
        self._flush()
        return True
        
    def _flush(self):
        if self._need_flush:
            self.conn.append(self.name, self.content)
        self._need_flush = False
        return True

class DWSWriteableFS(DWSReadableFS, fs.WriteableFS):
    def open_append(self, filename):
        return AppendableFileFake( self.conn, filename )

    def size(self, filename):
        try:
            return int(self.conn.stat(filename)['size'])
        except Exception,e:
            return 0

    def _exists(self, full_fn):
        try:
            return full_fn in self.list()
        except IOError:
            return False
        return True

    def put(self, filenames):
        for fn, data in filenames.iteritems():
            self.conn.put(fn, data)   
            
    def rollback_interrupted_puts(self, filenames):
        pass

    def mkdir(self, filename):
        try:
            pass
        except IOError:
            return 0
        return 1

    def rmdir(self, filename):
        try:
            pass
        except IOError:
            pass

    def ensure_dir(self, absdir=None):
        pass


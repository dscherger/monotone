# interface to remote (dumb) servers

import os
import os.path

class ReadableServer:
    # All operators are blocking

    # takes an iterable of filenames
    # returns a map {filename -> contents of file}
    def fetch(self, filenames):
        raise NotImplementedError

    # bytes is an iterable of pairs (offset, length)
    # this is a generator
    # it yields nested tuples ((offset, length), data)
    def fetch_bytes(self, filename, bytes):
        raise NotImplementedError

    def exists(self, filename):
        raise NotImplementedError


class WriteableServer (ReadableServer):
    # returns None
    def append(self, filename, data):
        raise NotImplementedError

    # files is a map {filename -> contents of file}
    # this operation must be atomic
    def replace(self, files):
        raise NotImplementedError

    def delete(self, filename):
        raise NotImplementedError


class LocalServer (WriteableServer:
    def __init__(self, dir):
        self.dir = dir

    def fetch(self, filenames):
        files = {}
        for fn in filenames:
            f = open(os.path.join(self.dir, fn), "rb")
            files[fn] = f.read()
            f.close()
        return files

    def fetch_bytes(self, filename, bytes):
        f = open(os.path.join(self.dir, filename), "rb")
        for offset, length in bytes:
            f.seek(offset)
            yield ((offset, length), f.read(length))

    def exists(self, filename):
        return os.path.exists(os.path.join(self.dir, filename))

    def append(self, filename, data):
        f = open(os.path.join(self.dir, filename), "ab")
        f.write(data)
        f.close()

    def replace(self, filenames):
        for fn, data in filenames.iteritems():
            tmpname = os.path.join(self.dir, "_tmp")
            tmph = open(tmpname, "wb")
            tmph.write(data)
            tmph.close()
            os.rename(tmpname, os.path.join(self.dir, fn))

    def delete(self, filename):
        os.unlink(os.path.join(self.dir, filename))

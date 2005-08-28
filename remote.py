# interface to remote (dumb) servers

import os
import os.path

class ReadableServer:
    # All operators are blocking

    # returns an object that supports read(), close() with standard file
    # object semantics
    def open_read(self, filename):
        raise NotImplementedError

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
    # returns an object that supports write(), flush(), close() with standard
    # file object semantics
    def open_append(self, filename):
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

    def _fname(self, filename):
        return os.path.join(self.dir, filename)

    def open_read(self, filename):
        return open(self._fname(filename), "rb")

    def fetch(self, filenames):
        files = {}
        for fn in filenames:
            f = open(self._fname(fn), "rb")
            files[fn] = f.read()
            f.close()
        return files

    def fetch_bytes(self, filename, bytes):
        f = open(self._fname(filename), "rb")
        for offset, length in bytes:
            f.seek(offset)
            yield ((offset, length), f.read(length))

    def exists(self, filename):
        return os.path.exists(self._fname(filename))

    def open_append(self, filename):
        return open(self._fname(filename), "ab")

    def replace(self, filenames):
        for fn, data in filenames.iteritems():
            tmpname = self._fname("__tmp")
            tmph = open(tmpname, "wb")
            tmph.write(data)
            tmph.close()
            os.rename(tmpname, self._fname(fn))

    def delete(self, filename):
        os.unlink(self._fname(filename))

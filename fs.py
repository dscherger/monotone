# interface to FS-like things

import os
import os.path

def readable_fs_for_url(url):
    return LocalReadableFS(url)

def writeable_fs_for_url(url):
    return LocalWriteableFs(url)

class ReadableFS:
    # All operators are blocking

    # returns an object that supports read(), close() with standard file
    # object semantics
    def open_read(self, filename):
        raise NotImplementedError

    # takes an iterable of filenames
    # returns a map {filename -> contents of file}
    # if some file does not exist, will have a None for contents
    def fetch(self, filenames):
        raise NotImplementedError

    # bytes is an iterable of pairs (offset, length)
    # this is a generator
    # it yields nested tuples ((offset, length), data)
    # subclasses should implement _real_fetch_bytes which has the same API;
    # but will receive massaged (seek-optimized) arguments
    def fetch_bytes(self, filename, bytes):
        # FIXME: implement block coalescing/decoalescing, and sort to optimize
        # seeks.
        return self._real_fetch_bytes(filename, bytes)

    def _real_fetch_bytes(self, filename, bytes):
        raise NotImplementedError


class WriteableFS (ReadableFS):
    # returns an object that supports write(), flush(), close() with standard
    # file object semantics
    def open_append(self, filename):
        raise NotImplementedError

    # Must return 0 for non-existent files
    # you might think this was a ReadableFS sort of thing, except that it's
    # only used in conjunction with open_append.
    def size(self, filename):
        raise NotImplementedError

    # files is a map {filename -> contents of file}
    # this operation must be atomic and clobbering
    def put(self, files):
        raise NotImplementedError

    # in case put() cannot be made atomic (ftp, ntfs, maybe other situations),
    # puts should still be done in a way that they can be rolled back.
    # This function checks for puts that were not completed, and if any are
    # found, rolls them back.
    # files is an iterable of filenames
    def rollback_interrupted_puts(self, filenames):
        raise NotImplementedError

    # returns true if mkdir succeeded, false if failed
    # used for locking
    def mkdir(self, filename):
        raise NotImplementedError

    def rmdir(self, filename):
        raise NotImplementedError

    # ensure that the directory that this fs is running over exists, etc.
    def ensure_dir(self):
        raise NotImplementedError

class LocalReadableFS(ReadableFS):
    def __init__(self, dir):
        self.dir = dir

    def _fname(self, filename):
        return os.path.join(self.dir, filename)

    def open_read(self, filename):
        return open(self._fname(filename), "rb")

    def fetch(self, filenames):
        files = {}
        for fn in filenames:
            try:
                f = open(self._fname(fn), "rb")
                files[fn] = f.read()
                f.close()
            except IOError:
                files[fn] = None
        return files

    def fetch_bytes(self, filename, bytes):
        f = open(self._fname(filename), "rb")
        for offset, length in bytes:
            f.seek(offset)
            yield ((offset, length), f.read(length))

    def size(self, filename):
        try:
            return os.stat(self._fname(filename)).st_size
        except OSError:
            return 0

class LocalWriteableFs(LocalReadableFS, WriteableFS):
    def open_append(self, filename):
        return open(self._fname(filename), "ab")

    def put(self, filenames):
        for fn, data in filenames.iteritems():
            tmpname = self._fname("__tmp")
            tmph = open(tmpname, "wb")
            tmph.write(data)
            tmph.close()
            os.rename(tmpname, self._fname(fn))

    def rollback_interrupted_puts(self, filenames):
        # we have atomic put
        pass

    def mkdir(self, filename):
        try:
            os.mkdir(self._fname(filename))
        except OSError:
            return 0
        return 1

    def rmdir(self, filename):
        os.rmdir(self._fname(filename))

    def ensure_dir(self):
        name = self._fname("")
        if os.path.exists(name):
            return
        os.mkdirs(name)

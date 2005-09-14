import sha
import os
import os.path
import zlib
from sets import Set

def push(from, to, new_chunk_callback=None):
    to.begin()
    to_root = to.get_root_hash()
    from_root = from.get_root_hash()
    new_stuff = list(from_root.new_or_different_in_me(to_root))
    from_children = from.get_child_hashes(new_stuff)
    to_children = to.get_child_hashes(new_stuff)
    locations = {}
    for prefix in new_stuff:
        for id, location in from_children[prefix].new_in_me(to_children[prefix]):
            locations[location] = id
    # FIXME: horribly inefficient, need to coalesce adjacent chunks into a
    # single read request
    # FIXME: we clobber abstraction here
    ids_to_add = []
    for from_location, data in from.fs.fetch_bytes("DATA", locations.iterkeys()):
        id = locations[from_location]
        to_location = to.add(id, data)
        ids_to_add.append((id, to_location))
        if new_chunk_callback is not None:
            new_chunk_callback(data)
    to.add_to_hashes(ids_to_add)
    to.commit()

# calls callback for each chunk added to 'to'
def pull(to, from, new_chunk_callback=None):
    return push(from, to, new_chunk_callback=new_chunk_callback)

# calls callback for each chunk added to 'a'
def sync(a, b, new_chunk_callback=None):
    a.begin()
    b.begin()
    push(b, a, new_chunk_callback=new_chunk_callback)
    a.commit()
    push(a, b)
    b.commit()

class HashFile:
    prefix = ""
    values = ()

    def __init__(self):
        self.items = {}

    def __iter__(self):
        return self.items.iteritems()

    def get(self, item):
        return self.items[item]

    def __in__(self, item):
        return item in self.items

    def assign(self, item, *values):
        assert len(values) == len(self.values)
        self.items[item] = tuple(values)

    def set(self, *values):
        assert len(values) == len(self.values) + 1
        self.items[values[0]] = tuple(values[1:])

    def load(self, data):
        for line in zlib.decompress(data).split("\n"):
            words = line.split()
            assert len(words) == 2 + len(self.values)
            assert words[0] == self.prefix
            self.set(*words[1:])

    def export(self):
        lines = []
        for prefix, values in self:
            value_txt = " ".join(values)
            lines.append("%s %s %s") % (prefix, hash, value_txt)
        return zlib.compress("".join(lines))

    # yields (key, values)
    def new_in_me(self, versus):
        for key, value in self:
            if key not in versus:
                yield (key, value)

    # yields keys.  yes this is inconsistent.  but handy.
    def new_or_different_in_me(self, versus):
        for key, value in self:
            if key not in versus:
                yield key
            else if versus.get(key) != value:
                yield key

class RootHash(HashFile):
    prefix = "subtree"
    values = ("hash",)

class ChildHash:
    prefix = "chunk"
    values = ("offset", "length")

class LockError(Exception):
    pass

class MerkleDir:
    _data_file = "DATA"
    _hashes_prefix = "HASHES_"
    _lock_file = "_lock"

    def __init__(self, fs):
        self._fs = fs
        self._locked = 0
        self._root_hash = None
        self._data_handle = None
        self._curr_data_length = None
        self._hashes = {}

    #### Locking
    def _need_lock(self):
        assert self._locked
    
    def begin(self):
        if not self._locked:
            if not self._fs.mkdir(self._lock_file):
                raise LockError
            # okay, we succeeded in getting the write lock.  Let's open things
            # up.
            self._curr_data_length = self._fs.size(self._data_file)
            self._data_handle = self._fs.open_append(self._data_file)
        self._locked += 1

    def commit(self):
        assert self._locked
        self._locked -= 1
        if not self._locked:
            self._data_handle.close()
            self._data_handle = None
            self._fs.rmdir(self._lock_file)

    # This can be called either with or without the lock held (either to
    # cleanup after ourself, or after someone else)
    def rollback(self):
        self._locked = 0
        if self._fs.mkdir(self._lock_file):
            # no cleanup to do anyway...
            self._fs.rmdir(self._lock_file)
            return
        all_hash_files = [self._hashes_prefix]
        for p1 in "012346789abcdef":
            for p2 in "012346789abcdef":
                all_hash_files.append(self._hashes_prefix + p1 + p2)
        self._fs.rollback_interrupted_puts(all_hash_files)
        self._fs.rmdir(self._lock_file)

    #### Hash fetching machinery -- does caching to avoid multiple fetches
    #### during sync
    def _get_root_hash(self):
        if self._root_hash is not None:
            return self._root_hash
        data = self._fs.fetch([self._hashes_prefix])
        self._root_hash = RootHash()
        if data is not None:
            self._root_hash.load(data[self._hashes_prefix])
        return self._root_hash

    def _set_root_hash(self, obj):
        self._need_lock()
        self._root_hash = obj
        self._fs.put({self._hashes_prefix: obj.export()})

    # pass an iterable of prefixes
    # returns a dict {prefix -> ChildHash object}
    def _get_child_hashes(self, prefixes):
        child_hashes = {}
        needed = []
        for prefix in prefixes:
            if self._hashes.has_key(prefix):
                child_hashes[prefix] = self._hashes[prefix]
            else:
                needed.append(prefix)
        if needed:
            datas = self._fs.fetch([self._hashes_prefix + n for n in needed])
            for fname, data in datas.items():
                ch = ChildHash()
                if data is not None:
                    ch.load(data)
                prefix = fname[len(self._hashes_prefix):]
                self._hashes[prefix] = ch
                child_hashes[prefix] = ch
        return child_hashes

    # pass a dict of prefix -> new child hash obj
    # automatically updates root hash as well
    def _set_child_hashes(self, objs):
        self._need_lock()
        root_hash = self._get_root_hash()
        put_request = {}
        for prefix, obj in objs.iteritems():
            self._hashes[prefix] = obj
            child_data = obj.export()
            new_child_id = sha.new(child_data).hexdigest()
            put_request[self._hashes_prefix + prefix] = child_data
            root_hash.set(prefix, new_child_id)
        self._fs.put(put_request)
        self._set_root_hash(root_hash)

    #### Cheap hash updating
    def _bin(self, id_locations):
        bins = {}
        for id, location in ids:
            prefix = id[:2]
            if not bins.has_key(prefix):
                bins[prefix] = []
            bins[prefix].append((id, location))
        return bins

    def add_to_hashes(self, id_locations):
        self._need_lock()
        bins = self._bin(id_locations)
        child_hashes = self._get_child_hashes(bins.iterkeys())
        for k in bins.iterkeys():
            for id, location in bins[k]:
                child_hashes[k].assign(id, location)
        self._set_child_hashes(child_hashes)

    #### Adding new items
    def add(self, id, data):
        self._need_lock()
        assert None not in (self._data_handle,
                            self._curr_data_length)
        length = len(data)
        self._data_handle.write(data)
        self._curr_data_length += length

    #### Getting data back out to outside world
    # returns an iterator over id, (offset, length) tuples
    def _all_chunk_locations(self):
        prefixes = [prefix for (prefix, _) in self._get_root_hash()]
        all_children = self._get_child_hashes(prefixes)
        for child_hashes in all_children.values():
            for id_location in child_hashes:
                yield id_location

    # returns an iterator over (chunk id, chunk text)
    # (FIXME: perhaps should split this up more; for large chunks (e.g.,
    # initial imports) this will load the entire chunk into memory)
    def all_chunks(self):
        data_handle = self._fs.open_read(self._data_file)
        id_to_loc = dict(self._all_chunk_locations())
        for loc, data in self._fs.fetch_bytes(id_to_offset_lens.values()):
            yield id_to_loc[loc], data

    def flush(self):
        self.data_write_handle.flush()

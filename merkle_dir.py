import sha
import os
import os.path
import zlib
from sets import Set

def push(from, to, new_chunk_callback=None):
    # FIXME: transactionalness
    to.lock()
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
    to.unlock()

# calls callback for each chunk added to 'to'
def pull(to, from, new_chunk_callback=None):
    return push(from, to, new_chunk_callback=new_chunk_callback)

# calls callback for each chunk added to 'a'
def sync(a, b, new_chunk_callback=None):
    # FIXME: transactionalness
    a.lock()
    b.lock()
    push(b, a, new_chunk_callback=new_chunk_callback)
    push(a, b)
    b.unlock()
    a.unlock()

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
    data_file = "DATA"
    index_file = "INDEX"
    lengths_file = "LENGTHS"
    hashes_prefix = "HASHES_"
    lock_file = "_lock"
    rollback_file = "_rollback"

    def __init__(self, fs):
        self.fs = fs
        self.locked = 0
        self.add_open = 0
        self.added_ids = Set()
        self.root_hash = None
        self.data_handle = None
        self.index_handle = None
        self.curr_data_length = None
        self.curr_index_length = None
        self.hashes = {}

    #### Locking
    def need_lock(self):
        assert self.locked
    
    def begin(self):
        if not self.locked:
            if not self.fs.mkdir(self.lock_file):
                raise LockError
            # okay, we succeeded in getting the write lock.  Let's open things
            # up.
            lengths_data = self.fs.fetch([self.lengths_file])[self.lengths_file]
            self.curr_data_length, self.curr_index_length = lengths_data.strip().split()
            self.fs.put({self.rollback_file: lengths_data})
            self.data_handle = self.fs.open_append(self.data_file)
            self.index_handle = self.fs.open_append(self.index_file)
        self.locked += 1

    def commit(self):
        assert self.locked
        self.locked -= 1
        if not self.locked:
            self.data_handle.close()
            self.data_handle = None
            self.index_handle.close()
            self.index_handle = None
            self.fs.put({self.lengths_file:
                         "%s %s\n" % (self.curr_data_length,
                                      self.curr_index_length)})
            # if we've gotten this far, we're in a fully consistent state
            self.fs.delete(self.rollback_file)
            # FIXME: cleanup backup hash files here
            self.fs.rmdir(self.lock_file)

    # This can be called either with or without the lock held (either to
    # cleanup after ourself, or after someone else)
    def rollback(self):
        self.locked = 0
        if self.fs.mkdir(self.lock_file):
            # no cleanup to do anyway...
            self.fs.rmdir(self.lock_file)
            return
        old_lengths_data = self.fs.fetch([self.rollback_file])[self.rollback_file]
        if old_lengths_data is None:
            # we got so far in rolling back before that _all_ we have left is
            # the lockdir.  so we can just delete it and be done.
            self.fs.rmdir(self.lock_file)
            return
        old_data_length, old_index_length = old_lengths_data.strip().split()
        self.fs.put({self.lengths_file: old_lengths_data})
        try:
            self.fs.truncate(self.index_file, old_index_length)
            self.fs.truncate(self.data_file, old_data_length)
        except NotImplementedError:
            raise NotImplementedError, "rollback not supported on this backend"
        self.rehash_from_scratch()
        self.delete(self.rollback_file)
        self.fs.rmdir(self.lock_file)

    def __del__(self):
        if self.locked:
            self.rollback()

    #### Hash fetching machinery -- does caching to avoid multiple fetches
    #### during sync
    def get_root_hash(self):
        if self.root_hash is not None:
            return self.root_hash
        data = self.fs.fetch([self.hashes_prefix])
        self.root_hash = RootHash()
        if data is not None:
            self.root_hash.load(data[self.hashes_prefix])

    def set_root_hash(self, obj):
        self.need_lock()
        self.root_hash = obj
        self.fs.put({self.hashes_prefix: obj.export()})

    # pass an iterable of prefixes
    # returns a dict {prefix -> ChildHash object}
    def get_child_hashes(self, prefixes):
        child_hashes = {}
        needed = []
        for prefix in prefixes:
            if self.hashes.has_key(prefix):
                child_hashes[prefix] = self.hashes[prefix]
            else:
                needed.append(prefix)
        if needed:
            datas = self.fs.fetch([self.hashes_prefix + n for n in needed])
            for fname, data in datas.items():
                ch = ChildHash()
                if data is not None:
                    ch.load(data)
                prefix = fname[len(self.hashes_prefix):]
                self.hashes[prefix] = ch
                child_hashes[prefix] = ch
        return child_hashes

    # pass a dict of prefix -> new child hash obj
    # automatically updates root hash as well
    def set_child_hashes(self, objs):
        self.need_lock()
        root_hash = self.get_root_hash()
        put_request = {}
        for prefix, obj in objs.iteritems():
            self.hashes[prefix] = obj
            child_data = obj.export()
            new_child_id = sha.new(child_data).hexdigest()
            put_request[self.hashes_prefix + prefix] = child_data
            root_hash.set(prefix, new_child_id)
        self.fs.put(put_request)
        self.set_root_hash(root_hash)

    #### Cheap hash updating
    def bin(self, id_locations):
        bins = {}
        for id, location in ids:
            prefix = id[:2]
            if not bins.has_key(prefix):
                bins[prefix] = []
            bins[prefix].append((id, location))
        return bins

    def add_to_hashes(self, id_locations):
        self.need_lock()
        bins = self.bin(id_locations)
        child_hashes = self.get_child_hashes(bins.iterkeys())
        for k in bins.iterkeys():
            for id, location in bins[k]:
                child_hashes[k].assign(id, location)
        self.set_child_hashes(child_hashes)

    #### Adding new items
    def add(self, id, data):
        self.need_lock()
        assert None not in (self.data_handle,
                            self.index_handle,
                            self.curr_data_length,
                            self.curr_index_length)
        length = len(data)
        index_text = "%s %s %s\n" % (id, self.curr_data_length, length)
        self.data_handle.write(data)
        self.index_handle.write(index_text)
        self.curr_index_length += len(index_text)
        self.curr_data_length += length

    #### Getting data back out to outside world
    # returns an iterator over id, offset, length tuples
    def read_index(self):
        index_handle = self.fs.open_read(self.index_file)
        # can't assume that fs handles have line iterators
        remainder = ""
        for block in iter(lambda: index_handle.read(1024), None):
            all = remainder + block
            lines = all.split("\n")
            remainder = lines[-1]
            for line in lines[:-1]:
                id, offset, length = line.split()
                yield (id, offset, length)
        
    # returns an iterator over chunk texts (FIXME: perhaps should split this
    # up more; for large chunks (e.g., initial imports) this will load the
    # entire chunk into memory)
    def all_chunks(self):
        data_handle = self.fs.open_read(self.data_file)
        total_read = 0
        for id, offset, length in self.read_index():
            assert total_read == offset
            yield data_handle.read(length)
            total_read += length
        assert remainder == ""

    #### Fixing things up
    def rehash_from_scratch(self):
        self.need_lock()
        # clear out old hashes
        child_hashes = {}
        for a in "01234567890abcdef":
            for b in "01234567890abcdef":
                child_hashes[a+b] = ChildHash()
        self.set_child_hashes(child_hashes)
        # update from scratch
        self.add_to_hashes(self.read_index())

    def add_to_index(self, id, offset, length):
        assert not self.ids.has_key(id)
        self.ids[id] = (offset, length)
        self.index_write_handle.write("%s %s %s\n" % (id, offset, length))

    def add(self, id):
        assert not self.add_open
        if self.ids.has_key(id):
            return None
        else:
            self.add_open = 1
            return MerkleAdder(self, id)

    def flush(self):
        length = self.data_write_handle.tell()
        open(os.path.join(self.dir, self.data_length_file), "w").write(str(length))
        self.data_write_handle.flush()
        self.index_write_handle.flush()

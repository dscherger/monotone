import sha
import os
import os.path
import zlib

# What's on disk:
#   We write all interesting data out to a file DATA
#     It consists of a bunch of "chunks", appended one after another
#     each chunk is a raw block of bytes, as passed to add()
#   each chunk has a unique id.  it is assumed that the id<->data mapping is
#     immutable; in particular, when transferring, we assumed that if we have
#     an id already, then we already have the corresponding data
#   ids should probably be hashes of something or the other; they don't
#     actually have to be, but we do rely on them "looking like" hex encoded
#     hashes (e.g., first 2 characters are randomly distributed, doesn't
#     contain spaces, etc.)
#   the HASHES files implement the merkle trie algorithm
#   we chop off our merkle tree at depth 2; this should give good loading for
#     even very large projects, and is not so large that we pay excessive
#     overhead on smaller projects
#   this means there are two kinds of HASHES files -- the root file
#     ("HASHES_"), and the child files ("HASHES_<2 digits of hex>")
#   both kinds of HASHES_ files are deflated (not gzipped) as a whole
#   both sorts of files are sorted asciibetically
#   the child files contain a bunch of lines of the form:
#       chunk <id> <offset> <length>
#     where <id> begins with the 2 hex digits that this file is responsible
#     for, and <offset> and <length> are the location of the corresponding
#     data in DATA.
#   the root file contains a bunch of lines of the form:
#       subtree <prefix> <hash> <filename>
#     where <prefix> is the prefix of the tree being referred to (always two
#     digits), <filename> is the child file containing the corresponding
#     information, and <hash> is the sha1 of the string
#     "<id1>\0<id2>\0...<idn>\0", for all ids mentioned in <filename>.
#
#   Finally, there may be a directory "_lock".  This is used for locking; see
#   below.
#
# Transaction handling:
#   Readers pay no attention to transactions or writers.  Writers assume the
#     full burden of keeping the repo fully consistent at all times.
#   Writers:
#     -- first acquire a lock by creating the directory _lock.  This ensures
#        that only one writer will run at a time.
#     -- append everything they want to append to DATA.  Until this part of
#        the file is referenced by HASHES files, it won't be noticed.
#        Before appending any particular item, they read child hash file that
#        would reference it, and make sure that it does not already exist.
#        If interrupted at this point, unreferenced garbage may be left in
#        DATA, but this is harmless.
#     -- start atomically replacing child hash files.
#        During this phase, the root hash file will lag behind the child hash
#        files -- it will describe them as containing less than they actually
#        do.  This does not cause any problems, because
#           a) when determining whether an id exists, we always read the child
#              hash file (as noted in previous step)
#           b) the only things that can happen to readers are that they fetch
#              a file that is _even newer_ than they were expecting (in which
#              case, they actually wanted it _even more_ than they realized),
#              or that they skip a file that has been replaced, but was
#              unininteresting before being replaced.
#        This does mean that a pull is not guaranteed to fetch everything that
#        was included in the most recent push; it may instead fetch only some
#        random subset.  Users of this library must be robust against this
#        possibility -- even if two items A and B were added at the same time,
#        a client may receive only A, or only B.
#
#        In some situations (FTP, NTFS, ...) it is actually impossible to
#        atomically replace a file.  In these cases we simply bite the bullet
#        and have a very small race condition, while each file is being
#        swapped around.  If readers try to open a file but find it does not
#        exist, they should try again after a short pause, before giving up.
#     -- atomically replace the root hash file (subject again to the above
#        proviso)
#     -- remove the lockdir
#  Rollback:
#     If a connection is interrupted uncleanly, or there is a stale lock, we:
#       -- check for any missing files left by non-atomic renames
#       -- remove the lockdir

class HashFile:
    prefix = ""
    values = ()

    def __init__(self):
        self.items = {}

    def __iter__(self):
        return self.items.iteritems()

    def get(self, item):
        return self.items[item]

    def __contains__(self, item):
        return self.items.has_key(item)

    def assign(self, item, values):
        assert len(values) == len(self.values)
        self.items[item] = tuple(values)

    def set(self, *values):
        assert len(values) == len(self.values) + 1
        self.items[values[0]] = tuple(values[1:])

    def load(self, data):
        for line in zlib.decompress(data).split("\n"):
            if not line:
                continue
            words = line.split()
            assert len(words) == 2 + len(self.values)
            assert words[0] == self.prefix
            item = words[1]
            values = []
            for i in xrange(len(self.values)):
                values.append(self.value_type(words[2+i]))
            self.assign(item, values)

    def export(self):
        lines = []
        for key, values in self:
            value_txt = " ".join([str(v) for v in values])
            lines.append("%s %s %s" % (self.prefix, key, value_txt))
        lines.sort()
        return zlib.compress("\n".join(lines))

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
            elif versus.get(key) != value:
                yield key

class RootHash(HashFile):
    prefix = "subtree"
    values = ("hash",)
    value_type = str

class ChildHash(HashFile):
    prefix = "chunk"
    values = ("offset", "length")
    value_type = int

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
        self._ids_to_flush = []
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
        self._need_lock()
        if self._locked == 1:
            # have to do this before decrementing, because flushes require
            # holding the lock
            self.flush()
        self._locked -= 1
        if not self._locked:
            self._data_handle.close()
            self._data_handle = None
            self._curr_data_length = None
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
        data = self._fs.fetch([self._hashes_prefix])[self._hashes_prefix]
        self._root_hash = RootHash()
        if data is not None:
            self._root_hash.load(data)
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
        for id, location in id_locations:
            prefix = id[:2]
            if not bins.has_key(prefix):
                bins[prefix] = []
            bins[prefix].append((id, location))
        return bins

    def _flush_hashes(self):
        self._need_lock()
        bins = self._bin(self._ids_to_flush)
        child_hashes = self._get_child_hashes(bins.iterkeys())
        for k in bins.iterkeys():
            for id, location in bins[k]:
                assert id not in child_hashes[k]
                child_hashes[k].assign(id, location)
        print ("writing hashes for %s new ids to %s hash files"
               % (len(self._ids_to_flush), len(bins)))
        self._set_child_hashes(child_hashes)
        self._ids_to_flush = []

    #### Adding new items
    # can only be called from inside a transaction.
    def add(self, id, data):
        self._need_lock()
        assert None not in (self._data_handle,
                            self._curr_data_length)
        length = len(data)
        self._data_handle.write(data)
        location = (self._curr_data_length, length)
        self._curr_data_length += length
        self._ids_to_flush.append((id, location))

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
        return self.get_chunks(self._all_chunk_locations())

    # id_locations is an iterable over (id, location) tuples
    # yields (id, data) tuples
    def get_chunks(self, id_locations):
        locations_to_ids = {}
        for id, location in id_locations:
            if location[1] == 0:
                # just go ahead and process null-length chunks directly,
                # rather than calling fetch_bytes on them -- these are the
                # only chunks for which the location->chunk map may not be
                # one-to-one.
                yield (id, "")
            else:
                assert not locations_to_ids.has_key(location)
                locations_to_ids[location] = id
        if locations_to_ids:
            for loc, data in self._fs.fetch_bytes(self._data_file,
                                                  locations_to_ids.keys()):
                yield locations_to_ids[loc], data

    def flush(self):
        if self._locked:
            self._data_handle.flush()
            self._flush_hashes()

    def push(self, target, new_chunk_callback=None):
        try:
            self.flush()
            target.begin()
            source_root = self._get_root_hash()
            target_root = target._get_root_hash()
            new_stuff = list(source_root.new_or_different_in_me(target_root))
            source_children = self._get_child_hashes(new_stuff)
            target_children = target._get_child_hashes(new_stuff)
            locations = {}
            for prefix in new_stuff:
                source_hash = source_children[prefix]
                target_hash = target_children[prefix]
                new_in_source = list(source_hash.new_in_me(target_hash))
                for id, data in self.get_chunks(new_in_source):
                    target.add(id, data)
                    if new_chunk_callback is not None:
                        new_chunk_callback(id, data)
            target.flush()
            target.commit()
        except:
            target.rollback()
            raise

    def pull(self, source, new_chunk_callback=None):
        source.push(self, new_chunk_callback)

    def sync(self, other,
             new_self_chunk_callback=None, new_other_chunk_callback=None):
        self.pull(other, new_self_chunk_callback)
        self.push(other, new_other_chunk_callback)

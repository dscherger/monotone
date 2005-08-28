import sha
import os
import os.path
import zlib
import glob

class LockError(Exception):
    pass

class MerkleDir:
    data_file = "DATA"
    index_file = "INDEX"
    data_length_file = "DATA_LENGTH"
    lock_file = "__lock"
    hashes_prefix = "HASHES_"

    def __init__(self, directory):
        self.dir = directory
        if not os.path.isdir(self.dir):
            os.makedirs(self.dir)
        if not os.path.exists(os.path.join(self.dir, self.data_file)):
            open(os.path.join(self.dir, self.data_file), "w").close()
        if not os.path.exists(os.path.join(self.dir, self.index_file)):
            open(os.path.join(self.dir, self.index_file), "w").close()
        # dict: id -> (offset, length)
        self.index_write_handle = open(os.path.join(self.dir, self.index_file), "a")
        self.data_write_handle = open(os.path.join(self.dir, self.data_file), "ab")
        self.add_open = 0
        self.reread_index()

    def __del__(self):
        self.flush()
        self.unlock()

    # returns an iterator over (id, offset, length)
    def chunk_locations(self):
        self.flush()
        handle = open(os.path.join(self.dir, self.index_file))
        for line in handle:
            id, offset, length = line.split()
            yield (id, offset, length)

    # returns an iterator over data chunks
    def chunks(self):
        self.flush()
        handle = open(os.path.join(self.dir, self.data_file))
        curr_offset = 0
        for id, offset, length in self.chunk_locations():
            assert curr_offset == offset
            cdata = self.data_file.read(length)
            curr_offset += length
            yield zlib.decompress(cdata)
        
    def reread_index(self):
        self.ids = {}
        for id, offset, length in self.chunk_locations():
            self.ids[id] = (int(offset), int(length))

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

    def rehash(self):
        self.flush()
        old_hashes = glob.glob(os.path.join(self.dir, self.hashes_prefix + "*"))
        for old in old_hashes:
            os.unlink(old)
        # We only do two levels of merkle hashing; with a branching factor of
        # 256, this gives 65536 bins for the actual ids, which should give us
        # a reasonable loading factor even for large repos.
        self.binned_indexes = {}
        for id in self.ids.iterkeys():
            bin = id[:2]
            if not self.binned_indexes.has_key(bin):
                self.binned_indexes[bin] = []
            self.binned_indexes[bin].append(id)
        bin_hashes = {}
        for bin, ids in self.binned_indexes.iteritems():
            handle = HashWriter(os.path.join(self.dir, self.hashes_prefix + bin))
            ids.sort()
            for id in ids:
                handle.write("chunk %s %s %s\n" % ((id,) + self.ids[id]))
            handle.close()
            bin_hashes[bin] = handle.hash()
        root_hashes = ""
        for bin, hash in bin_hashes.iteritems():
            root_hashes += "subtree %s %s %s" % (bin, hash, self.hashes_prefix + bin)
        open(os.path.join(self.dir, self.hashes_prefix), "wb").write(zlib.compress(root_hashes))

class HashWriter:
    def __init__(self, filename):
        self.sha = sha.new()
        self.file = open(filename, "wb")
        self.compressor = zlib.compressobj()
    def write(self, data):
        self.sha.update(data)
        self.file.write(self.compressor.compress(data))
    def close(self):
        self.file.write(self.compressor.flush())
        self.file.close()
    def hash(self):
        return self.sha.hexdigest()

class MerkleAdder:
    def __init__(self, store, id):
        self.store = store
        self.offset = self.store.data_write_handle.tell()
        self.id = id
        self.compressor = zlib.compressobj()
    def write(self, data):
        compressed_data = self.compressor.compress(data)
        self.store.data_write_handle.write(compressed_data)
    def close(self):
        last_data = self.compressor.flush()
        self.store.data_write_handle.write(last_data)
        length = self.store.data_write_handle.tell() - self.offset
        self.store.add_to_index(self.id, self.offset, length)
        self.store.add_open = 0


import subprocess
import sha
from sets import Set
import os
import os.path
from cStringIO import StringIO
import re
import zlib
import glob

# What's on disk:
#   We write all interesting data out to a file DATA
#     It consists of a bunch of "chunks", appended one after another
#     each chunk is a deflated (not gzipped) block
#     after decompression, each such chunk can be fed to 'monotone read'
#   each chunk has a unique id.  it is assumed that the id<->data mapping is
#     immutable; in particular, when transferring, we assumed that if we have
#     an id already, then we already have the corresponding data
#   a file INDEX is maintained which contains table-of-contents information
#     for DATA -- each line in INDEX gives metadata for the next chunk in
#     DATA.  this metadata is associated line, offset in DATA, number of bytes
#     taken up in DATA.  This allows, for instance, random access into DATA.
#   the INDEX file is used entirely locally; we never look at it over the
#     network.
#   locally, the INDEX file is used to generate the HASHES files
#   the HASHES files implement the merkle trie algorithm
#   we chop off our merkle tree at depth 2; this should give good loading for
#     even very large projects, and is not so large that we pay excessive
#     overhead on smaller projects
#   this means there are two kinds of HASHES files -- the root file
#     ("HASHES_"), and the child files ("HASHES_<2 digits of hex>")
#   both kinds of HASHES_ files are deflated as a whole
#   the child files contain a bunch of lines of the form:
#       chunk <id> <offset> <length>
#     where <id> begins with the 2 hex digits that this file is responsible
#     for, and <offset> and <length> are the location of the corresponding
#     data in DATA.  These files are sorted lexicographically by id.
#   the root file contains a bunch of lines of the form:
#       subtree <prefix> <hash> <filename>
#     where <prefix> is the prefix of the tree being referred to (always two
#     digits), <filename> is the child file containing the corresponding
#     information, and <hash> is the sha1 of the string
#     "<id1>\0<id2>\0...<idn>\0", for all ids mentioned in <filename>.
#     This file is always sorted lexicographically by prefix.
#     (FIXME: should we always generate child files for all prefixes, even if
#     some are unused?)
#   finally, there's a file VERSION, which contains the sha1 of the string
#   "<hash1>\0<hash2>\0...<hashn>\0", plus a newline.

# How a pull works:
#   -- pull VERSION, see if it matches ours.  (note down what it says for
#      later.)
#   -- if not, pull root hashes file.  compare each line to our hashes.  for
#      each one that differs, fetch the child file.
#   -- compare fetched child files to our child files.  For each line that's
#      new in the fetched files, note down the id/offset/length in a list.
#   -- after we have processed _all_ child files, use the list to figure out
#      which parts of the DATA file to fetch.  coalesce adjacent sections at
#      this stage.  (but keep track of how everything matches back up to ids.)
#   -- do one or more byte-range fetches to grab the stuff we need
#   -- append the results to our DATA
#   -- add the new stuff to INDEX, with the appropriate offsets for our DATA
#      file
#   -- rehash
#  (-- pull VERSION again, see if it's the same as it was at the beginning.
#      If not, warn the user or just print a message and go back to step 1.)
# How a push works:
#   -- lock the remote copy (see below)
#   -- pull VERSION/pull HASHES_/pull any child hash files we need
#   -- save HASHES_ and the child hash files so we can update them!
#   -- before we write anything, replace remote VERSION with a random nonce
#      (so that reads can reliably detect concurrent updates)
#   -- for each id we have that are not mentioned in the appropriate child
#      hash file, pull out the appropriate chunk from our DATA, assemble these
#      pieces together, and append them to the remote DATA file
#      also append appropriate lines to the remote INDEX
#   -- _update_ atomically each child hash file we pulled to include the stuff
#      we appended (do _not_ just push our version, remote side may have stuff
#      we don't, and everything may be at different offsets)
#   -- after child hash files have all been written, update root hash file
#      similarly
#   -- finally update VERSION

## FIXME: I guess INDEX should just contain lengths, so that it can be updated
## when pushing (without having to know the length of the remote DATA file)
#### No -- still need to update child hash files, which do have to know
#### offset.  So add a file LENGTH_OF_DATA or something, that we read after we
#### get our edit lock, and update after we finished appending.

# remote operations:
#   -- synchronous append
#   -- atomic update -- can queue multiple, block on all completing
#   -- fetch file (sync and async)
#   -- fetch byte ranges (sync, but perhaps should be able to pipeline
#      fetching multiple different ranges?)


# locking:
#   writers:
#   -- if the lockfile exists, abort
#   -- append a unique nonce to the lockfile
#   -- fetch the lockfile; if our nonce is at the beginning proceed
#      otherwise, abort
#   (with ftp, can use APPE command: ftp.storebinary("APPE <name>", fileobj))
#   (with sftp, can open in mode "a")
#   -- when done, delete lockfile
#
#   readers:
#   -- fetches VERSION, then root hashfile, then child hashes, then data
#   -- data is not referenced until fully written, so that's okay
#   -- as long as hash files are updated atomically (!), they will always
#      contain a list of things that really exist and may be wanted; and the
#      root file will always contain a list of things that may be interesting
#      (even if yet more interesting things are being written at the same
#      time)
#   so the worst thing that can happen is that we get some stuff without its
#   prerequisites, which will be fixed at next pull anyway.
#   should probably check the push version before and after the pull anyway,
#   so we can tell the user when they raced and there's more stuff to get...?
#   or even just start the pull over...?

# TODO:
# NEED more tightly packed disk format!
#  (maybe make pull-only, not sync?  so can do linear reads of "new stuff"?)
#  (push via rsync or the like?)
#   -- puller, syncer (no pusher)
#      -- with some sort of abstract pipelined IO class
#   -- don't pull full revision stuff into memory
#   -- compress packets on disk
#   -- cat revision and packet commands -> automate?
#   -- don't pull full packets into memory
#   -- store stuff in tree under given id -- so can skip loading all the
#      revision info if revision is already there

class MonotoneError (Exception):
    pass

class Monotone:
    def __init__(self, db, executable="monotone"):
        self.db = db
        self.executable = executable

    def init_db(self):
        self.run_monotone(["db", "init"])

    def ensure_db(self):
        if not os.path.exists(self.db):
            self.init_db()

    def revisions_list(self):
        output = self.run_monotone(["automate", "select", "i:"])
        return output.split()
    
    def get_revision(self, rid):
        return self.run_monotone(["cat", "revision", rid])

    def get_revision_packet(self, rid):
        return self.run_monotone(["rdata", rid])

    def get_file_packet(self, fid):
        return self.run_monotone(["fdata", fid])

    def get_file_delta_packet(self, old_fid, new_fid):
        return self.run_monotone(["fdelta", old_fid, new_fid])

    def get_manifest_packet(self, mid):
        return self.run_monotone(["mdata", mid])

    def get_manifest_delta_packet(self, old_mid, new_mid):
        return self.run_monotone(["mdelta", old_mid, new_mid])

    def get_cert_packets(self, rid):
        output = self.run_monotone(["certs", rid])
        packets = []
        curr_packet = ""
        for line in output.strip().split("\n"):
            curr_packet += line + "\n"
            if line == "[end]":
                packets.append(curr_packet)
                curr_packet = ""
        assert not curr_packet
        return packets

    # returns output as a string, raises an error on error
    def run_monotone(self, args):
        process = subprocess.Popen([self.executable, "--db", self.db] + args,
                                   stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE)
        stdout, stderr = process.communicate()
        if process.returncode:
            raise MonotoneError, stderr
        return stdout

    # feeds stuff into 'monotone read'
    def feed(self, iterator):
        process = subprocess.Popen([self.executable, "--db", self.db, "read"],
                                   stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE)
        # this is technically broken; we might deadlock.
        # subprocess.Popen.communicate uses threads to do this; that'd be
        # better.
        for chunk in iterator:
            process.stdin.write(chunk)
        process.stdin.close()
        stdout, stderr = process.communicate()
        if process.returncode:
            raise MonotoneError, stderr

    # copied wholesale from viewmtn (08fd7bf8143512bfcabe5f65cf40013e10b89d28)'s
    # monotone.py.  hacked to remove the []s from hash values, and to leave in
    # ("from", "") tuples.
    def basic_io_parser(self, data):
            "returns a list of lists of (key, value) tuples."
            basic_io_hex_re = re.compile(r'^ *(\S+) \[([0-9A-Za-z]*)\]$')
            basic_io_string_re = re.compile(r'^ *(\S+) (\".*)$')
            def unescape_string_value(str):
                    rv = ""
                    is_terminated = False
                    in_escape = False
                    if str[0] != '"':
                            raise Exception("basic_io parse error; not a string.")
                    for c in str[1:]:
                            if in_escape:
                                    if c != '\\' and c != '\"':
                                            raise Exception("basic_io parse error; expected \" or \\")
                                    rv += c
                                    in_escape = False
                            else:
                                    if c == '\\': in_escape = True
                                    if c == '"':
                                            if is_terminated:
                                                    raise Exception("basic_io parse error; string ends twice!")
                                            is_terminated = True
                                    else: rv += c
                    return is_terminated, rv
            rv = []
            stanza = []
            ongoing_string = None
            for line in data.split('\n'):
                    if not ongoing_string:
                            if line == '' and len(stanza) != 0:
                                    rv.append(stanza)
                                    stanza = []
                            m = basic_io_hex_re.match(line)
                            if m:
                                    key, value = m.groups()
                                    stanza.append((key, value))
                                    continue
                            m = basic_io_string_re.match(line)
                            if m:
                                    key, value = m.groups()
                                    is_terminated, e_value = unescape_string_value(value)
                                    if not is_terminated: ongoing_string = value
                                    else: stanza.append((key, e_value))
                                    continue
                    else:
                            ongoing_string += '\n' + line
                            is_terminated, e_value = unescape_string_value(ongoing_string)
                            if is_terminated:
                                    stanza.append((key, e_value))
                                    ongoing_string = None
            return rv


# class MerkleDir:
#     # how deeply do we nest our directories?  rule of thumb is that you want
#     # around (certs + revisions) to be less than 256**(depth + 1).
#     # At depth = 2, 256**(depth+1) is 16777216.  That should suffice.
#     depth = 2

#     index_name = "HASHES"

#     def __init__(self, directory):
#         self.dir = directory
#         self.ids = Set()
#         for dir, subdirs, files in os.walk(self.dir):
#             for f in files:
#                 if self.ishash(f):
#                     self.ids.add(f)

#     def ishash(self, string):
#         return len(string) == sha.digestsize*2

#     def hash(self, string):
#         return sha.new(string).hexdigest()

#     def dir_for_text(self, textid):
#         pieces = []
#         for i in range(self.depth):
#             pieces.append(textid[2*i:2*i+2])
#         return os.path.join(*pieces)

#     def add(self, text):
#         textid = self.hash(text)
#         if textid in self.ids:
#             return
#         textdir = os.path.join(self.dir, self.dir_for_text(textid))
#         if not os.path.exists(textdir):
#             os.makedirs(textdir)
#         handle = open(os.path.join(textdir, textid), "w")
#         handle.write(text)
#         handle.close()
#         self.ids.add(textid)
        
#     def rehash(self):
#         for dir, subdirs, files in os.walk(self.dir, topdown=0):
#             files = [f for f in files if self.ishash(f)]
#             index_path = os.path.join(dir, self.index_name)
#             index_handle = open(index_path, "w")
#             if files:
#                 assert not subdirs
#                 for f in files:
#                     index_handle.write("file %s\n" % (f,))
#             elif subdirs:
#                 assert not files
#                 for d in subdirs:
#                     subindex = open(os.path.join(dir, d, self.index_name), "r").read()
#                     index_handle.write("dir %s %s\n" % (d, self.hash(subindex)))
#             else:
#                 assert 0

#     # returns an iterator over strings; all strings when concatenated == all
#     # data put into this tree when concatenated
#     def all_data(self):
#         for dir, subdirs, files in os.walk(self.dir):
#             for f in files:
#                 if self.ishash(f):
#                     handle = open(f, "r")
#                     for chunk in iter(lambda: f.read(4096), ""):
#                         yield chunk


class MerkleDir:
    data_file = "DATA"
    index_file = "INDEX"
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

def do_import(monotone, dir):
    monotone.ensure_db()
    md = MerkleDir(dir)
    monotone.feed(md.chunks())

def do_export(monotone, dir):
    md = MerkleDir(dir)
    for rid in monotone.revisions_list():
        certs = monotone.get_cert_packets(rid)
        for cert in certs:
            handle = md.add(sha.new(cert).hexdigest())
            if handle:
                handle.write(cert)
                handle.close()
        handle = md.add(rid)
        if handle:
            revision_text = monotone.get_revision(rid)
            revision_parsed = monotone.basic_io_parser(revision_text)
            new_manifest = None
            old_manifest = ""
            new_files = {}
            for stanza in revision_parsed:
                stanza_type = stanza[0][0]
                if stanza_type == "new_manifest":
                    new_manifest = stanza[0][1]
                elif stanza_type == "old_revision":
                    if not old_manifest:
                        old_manifest = stanza[1][1]
                elif stanza_type == "patch":
                    old_fid = stanza[1][1]
                    new_fid = stanza[2][1]
                    if not new_files.has_key(new_fid):
                        new_files[new_fid] = None
                    if old_fid:
                        new_files[new_fid] = old_fid

            handle.write(monotone.get_revision_packet(rid))
            if old_manifest:
                handle.write(monotone.get_manifest_delta_packet(old_manifest, new_manifest))
            else:
                handle.write(monotone.get_manifest_packet(new_manifest))
            for new_fid, old_fid in new_files.items():
                if old_fid:
                    handle.write(monotone.get_file_delta_packet(old_fid, new_fid))
                else:
                    handle.write(monotone.get_file_packet(new_fid))
            handle.close()

    md.rehash()
    

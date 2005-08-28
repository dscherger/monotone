import sha
from sets import Set
import os
import os.path
from cStringIO import StringIO

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
#   finally2, there's a file called DATA_LENGTH, which contains the size of
#     the file DATA in bytes.  (this is used to synthesize offsets into it for
#     remotely appended data.)

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

# so: "append and then return"
#     "fetch all of these files and then return"
#     "atomically replace all of these files and then return"
#     "fetch this list of byte ranges from a given file and then return"
#     "query if this file exists and then return" (may just attempt fetching
#       it and see what happens)
#     "delete file"

# also write a file called _lock_info or something containing info on who
# created the lock, to ease in cleaning things up.
#
# there is no atomic replace in sftp (probably not ftp either).  can write,
# delete, rename, to minimize window.  but clients should be prepared to retry
# if a file fetch fails.

# TODO:
#   -- cat revision and packet commands -> automate?

def do_import(monotone, dir):
    monotone.ensure_db()
    md = MerkleDir(dir)
    monotone.feed(md.chunks())

def do_export(monotone, dir):
    md = MerkleDir(dir)
    for rid in monotone.toposort(monotone.revisions_list()):
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
    

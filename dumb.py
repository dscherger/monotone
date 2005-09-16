import sha
from sets import Set
import os
import os.path
from cStringIO import StringIO
from merkle_dir import MerkleDir
from fs import readable_fs_for_url, writeable_fs_for_url
import zlib

def do_rollback(url):
    md = MerkleDir(writeable_fs_for_url(url))
    md.rollback()

def do_full_import(monotone, url):
    monotone.ensure_db()
    md = MerkleDir(readable_fs_for_url(url))
    feeder = monotone.feeder()
    for id, data in md.all_chunks():
        feeder.write(zlib.decompress(data))
    feeder.close()

def do_export(monotone, url):
    md = MerkleDir(writeable_fs_for_url(url))
    md.begin()
    curr_ids = Set(md.all_ids())
    for rid in monotone.toposort(monotone.revisions_list()):
        certs = monotone.get_cert_packets(rid)
        for cert in certs:
            id = sha.new(cert).hexdigest()
            if id not in curr_ids:
                data = zlib.compress(cert)
                md.add(id, data)
        if rid not in curr_ids:
            rdata = StringIO()
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

            rdata.write(monotone.get_revision_packet(rid))
            if old_manifest:
                rdata.write(monotone.get_manifest_delta_packet(old_manifest, new_manifest))
            else:
                rdata.write(monotone.get_manifest_packet(new_manifest))
            for new_fid, old_fid in new_files.items():
                if old_fid:
                    rdata.write(monotone.get_file_delta_packet(old_fid, new_fid))
                else:
                    rdata.write(monotone.get_file_packet(new_fid))
            md.add(rid, zlib.compress(rdata.getvalue()))
    md.commit()


def do_push(monotone, local_url, target_url):
    print "Exporting changes from monotone db to %s" % (local_url,)
    do_export(monotone, local_url)
    print "Pushing changes from %s to %s" % (local_url, target_url)
    local_md = MerkleDir(readable_fs_for_url(local_url))
    target_md = MerkleDir(writeable_fs_for_url(target_url))
    added = 0
    def count_new(id, data):
        added += 1
    local_md.push(target_md, count_new)
    print "Pushed %s packets to %s" % (added, target_url)

def do_pull(monotone, local_url, source_url):
    print "Pulling changes from %s to %s" % (source_url, local_url)
    local_md = MerkleDir(writeable_fs_for_url(local_url))
    source_md = MerkleDir(readable_fs_for_url(source_url))
    feeder = monotone.feeder()
    added = 0
    def feed_new(id, data):
        feeder.write(zlib.decompress(data))
        added += 1
    local_md.pull(source_md, feed_new)
    feeder.close()
    print "Pulled and imported %s packets from %s" % (added, source_url)

def do_sync(monotone, local_url, other_url):
    print "Exporting changes from monotone db to %s" % (local_url,)
    do_export(monotone, local_url)
    print "Synchronizing %s and %s" % (local_url, other_url)
    local_md = MerkleDir(writeable_fs_for_url(local_url))
    other_md = MerkleDir(writeable_fs_for_url(other_url))
    feeder = monotone.feeder()
    pulled = 0
    pushed = 0
    def feed_pull(id, data):
        feeder.write(zlib.decompress(data))
        pulled += 1
    def count_push(id, data):
        pushed += 1
    local_md.sync(other_md, feed_pull, count_push)
    feeder.close()
    print "Pulled and imported %s packets from %s" % (pulled, other_url)
    print "Pushed %s packets to %s" % (pushed, other_url)

def main(name, args):
    pass

if __name__ == "__main__":
    import sys
    main(sys.argv[0], sys.argv[1:])

import sha
from sets import Set
import os
import os.path
from cStringIO import StringIO
import merkle_dir
import fs
import zlib

def do_full_import(monotone, url):
    monotone.ensure_db()
    md = merkle_dir.MerkleDir(fs.readable_fs_for_url(url))
    def all_data():
        for id, data in md.all_chunks():
            yield zlib.decompress(data)
    monotone.feed(all_data())

def do_export(monotone, url):
    md = merkle_dir.MerkleDir(fs.writeable_fs_for_url(url))
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

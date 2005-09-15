import sha
from sets import Set
import os
import os.path
from cStringIO import StringIO

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
    

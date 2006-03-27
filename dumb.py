import sha
from sets import Set
import os
import os.path
from cStringIO import StringIO
from merkle_dir import MerkleDir, LockError
from fs import readable_fs_for_url, writeable_fs_for_url
from monotone import Monotone
import zlib

class Dumbtone:

    def __init__(self, db, verbosity=0):
        """ receives the db name and a verbosity level from 0 to 2
             0, the default, means normal messaging
             1 enables some additional info
             2 shows detailed informations
        """
        self.monotone = Monotone(db)
        self.verbosity = verbosity

    def do_rollback(self, url):
        md = MerkleDir(writeable_fs_for_url(url))
        md.rollback()
    
    def do_full_import(self, url):
        if self.verbosity > 0:
            print "starting import from:", url
        self.monotone.ensure_db()
        md = MerkleDir(readable_fs_for_url(url))
        feeder = self.monotone.feeder(self.verbosity)
        if self.verbosity > 1:
            # verbose op, splits the chunk in the individual packets,
            # and reads them one by one
            for id, data in md.all_chunks():
                uncdata = zlib.decompress(data)
                for pkt in uncdata.split("[end]"):
                    if len(pkt)>1:
                       feeder.write(pkt+"[end]")
        else:
            for id, data in md.all_chunks():
                feeder.write(zlib.decompress(data))
        feeder.close()
    
    def do_export(self, url):
        md = MerkleDir(writeable_fs_for_url(url))
        try:
            md.begin()
            curr_ids = Set(md.all_ids())
            keys = self.monotone.key_names()
            for k in keys:
                kp = self.monotone.get_pubkey_packet(k)
                id = sha.new(kp).hexdigest()
                if id not in curr_ids:
                    md.add(id, kp)
            for rid in self.monotone.toposort(self.monotone.revisions_list()):
                print "processing revision ", rid
                if rid not in curr_ids:
                    rdata = StringIO()
                    revision_text = self.monotone.get_revision(rid)
                    revision_parsed = self.monotone.basic_io_parser(revision_text)
                    new_files = {}
                    for stanza in revision_parsed:
                        stanza_type = stanza[0][0]
                        if stanza_type == "add_file":
                            new_files[stanza[1][1]] = None
                            if self.verbosity > 0:
                                print stanza_type, ":", stanza[1][1]
                        elif stanza_type == "patch":
                            old_fid = stanza[1][1]
                            new_fid = stanza[2][1]
                            if not new_files.has_key(new_fid):
                                new_files[new_fid] = None
                            if old_fid:
                                new_files[new_fid] = old_fid
                            if self.verbosity > 0:
                                print stanza_type, ":", stanza[1][1],":", stanza[2][1]
    
                    for new_fid, old_fid in new_files.items():
                        if old_fid:
                            if self.verbosity > 0:
                                print "get_file_delta:",old_fid, new_fid
                            fdp =self.monotone.get_file_delta_packet(old_fid, new_fid)
                            if self.verbosity > 0:
                                print "file_delta (", old_fid, ",", new_fid,"):",fdp
                            rdata.write(fdp)
                        else:
                            if self.verbosity > 0:
                                print "get file_packet:",new_fid
                            fpp = self.monotone.get_file_packet(new_fid)
                            if self.verbosity > 0:
                                print "file_packet(",new_fid,"):",fpp
                            rdata.write(fpp)
                    rdata.write(self.monotone.get_revision_packet(rid))
                    md.add(rid, rdata.getvalue())
                certs = self.monotone.get_cert_packets(rid)
                if self.verbosity > 0:
                    print "rev ", rid, " certs:",certs
                for cert in certs:
                    id = sha.new(cert).hexdigest()
                    if id not in curr_ids:
                        md.add(id, cert)
            md.commit()
        except LockError:
            raise
        except:
            md.rollback()
            raise
    
    class CounterCallback:
        def __init__(self):
            self.added = 0
        def __call__(self, id, data):
            self.added += 1
    
    class FeederCallback:
        def __init__(self, feeder):
            self.added = 0
            self.feeder = feeder
        def __call__(self, id, data):
            self.added += 1
            self.feeder.write(zlib.decompress(data))
    
    def do_push(self, local_url, target_url):
        print "Exporting changes from monotone db to %s" % (local_url,)
        self.do_export(local_url)
        print "Pushing changes from %s to %s" % (local_url, target_url)
        local_md = MerkleDir(readable_fs_for_url(local_url))
        target_md = MerkleDir(writeable_fs_for_url(target_url))
        c = CounterCallback()
        local_md.push(target_md, c)
        print "Pushed %s packets to %s" % (c.added, target_url)
    
    def do_pull(self, local_url, source_url):
        print "Pulling changes from %s to %s" % (source_url, local_url)
        local_md = MerkleDir(writeable_fs_for_url(local_url))
        source_md = MerkleDir(readable_fs_for_url(source_url))
        self.monotone.ensure_db()
        feeder = self.monotone.feeder(self.verbosity)
        fc = FeederCallback(feeder)
        local_md.pull(source_md, fc)
        feeder.close()
        print "Pulled and imported %s packets from %s" % (fc.added, source_url)
    
    def do_sync(self, local_url, other_url):
        print "Exporting changes from monotone db to %s" % (local_url,)
        self.do_export(local_url)
        print "Synchronizing %s and %s" % (local_url, other_url)
        local_md = MerkleDir(writeable_fs_for_url(local_url))
        other_md = MerkleDir(writeable_fs_for_url(other_url))
        feeder = self.monotone.feeder(self.verbosity)
        pull_fc = FeederCallback(feeder)
        push_c = CounterCallback()
        local_md.sync(other_md, pull_fc, push_c)
        feeder.close()
        print "Pulled and imported %s packets from %s" % (pull_fc.added, other_url)
        print "Pushed %s packets to %s" % (push_c.added, other_url)

def main(name, args):
    pass

if __name__ == "__main__":
    import sys
    main(sys.argv[0], sys.argv[1:])

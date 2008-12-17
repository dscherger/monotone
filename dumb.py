#
# This file is part of 'mtndumb'
#
# Copyright (C) Nathaniel Smith <njs@pobox.com> and others, 
# licensed to the public under the terms of the GNU GPL (>= 2)
# see the file COPYING for details
# I.e., do what you like, but keep copyright and there's NO WARRANTY. 
#
import sha
from sets import Set
import os
import sys
import os.path
import zlib

from cStringIO import StringIO
from merkle_dir import MerkleDir, MemoryMerkleDir, LockError
from fs import readable_fs_for_url, writeable_fs_for_url
from monotone import Monotone, MonotoneError, find_stanza_entry, decode_cert_packet_info, validate_packet

#
# DelegateFunctor
#
#  used to create function-like object that
#  calls arbitrary function with predefined parameters.
#
class DelegateFunctor:
    def __init__(self, fn, *args):
        self.__fn = fn
        self.__args = args[:]
        
    def __call__(self, *args):
        finalArgs = self.__args + args
        return self.__fn(*finalArgs)

#
# ConstantValueFunctor
#
#  used when function object should return always specified
#  constant value
#
class ConstantValueFunctor:
    def __init__(self, value):
        self.__value = value
    def __call__(self):
        return self.__value
        
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
        for id, data in md.all_chunks():
            feeder.write(data)
        feeder.close()
        
    def __make_revision_packet(self, rid):
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
        return rdata.getvalue()
        
    def create_all_cert_map(self):
        result = {}
        for rid,cid in  self.monotone.get_cert_ids("i:"):
            result.setdefault(rid,[]).append(cid)
        return result
            
    def do_export(self, url, branch_pattern = None, callback = None):
        if url is None:
            md = MemoryMerkleDir()
        else:
            md = MerkleDir(writeable_fs_for_url(url))
        try:
            md.begin()
            curr_ids = Set(md.all_ids())
            keys = self.monotone.keys()
            exported_keys = Set()
            key_packets = {}
            for stanza in keys:
                keyid = find_stanza_entry(stanza, "name")[0]
                publicHash = find_stanza_entry(stanza, "public_hash")[0]
                publicLocations = find_stanza_entry(stanza, "public_location")                
                if "database" in publicLocations:
                    kp = DelegateFunctor(self.monotone.get_pubkey_packet,keyid)
                    ids = "\n".join((keyid,publicHash))
                    id = sha.new(ids).hexdigest()
                    key_packets[keyid] = (id, kp) # keys are queued for export
                                                  # and are exported only if used 
                                                  # to sign some exported cert
                    # currently we export ALL keys :(
                    md.add(id, kp)
            if branch_pattern:
                revision_list = self.monotone.revisions_for_pattern(branch_pattern)
            else:
                revision_list = self.monotone.revisions_list()
                
            all_certs_map = self.create_all_cert_map()
            for rid in self.monotone.toposort(revision_list):
                if rid not in curr_ids:
                    md.add(rid, DelegateFunctor(self.__make_revision_packet,rid))
                    if callback: callback(id, "", None)
                cert_ids = all_certs_map[rid]
                
                #print "rev ", rid, " certs:", cert_ids
                for cert_id in cert_ids:                    
                    if cert_id not in curr_ids:
                        #print "rev ", rid, " cert:", cert_id
                        md.add(cert_id, DelegateFunctor(self.monotone.get_cert_packet, cert_id) )
                        if callback: callback(id, "", None)
            md.commit()
            return md
        except LockError:
            raise
        except:
            md.rollback()
            raise
    
    class PushCallback:
        def __init__(self, message):
            self._message = message
            self._count = 0
            self._ignored_total = None
            
        def callback(self, id, data, total):
            pass
            
        def __call__(self, id, data, total):
            self._count += 1
            if self._count % 50 == 0:
                self.printProgress(total)
                self._ignored_total = None
            else:
                self._ignored_total = (total,)
            self.callback(id, data, total)           
            
        def printProgress(self, total = None):
            if self._message is None : return
            if total is None:
                if self._count != 0:
                    sys.stdout.write("\r" + self._message + " : %i ...         " % ( self._count ))
                else:
                    sys.stdout.write(self._message + " ... ")                    
            else:
                sys.stdout.write("\r" + self._message + " : %i / %i         " % ( self._count, int(total)))
                
        def finish(self):
            if self._ignored_total is not None:
                self.printProgress(self._ignored_total[0])
            if self._message is None or self._count == 0: return
	    self._count = 0
            sys.stdout.write("\n")
            
    class CounterCallback(PushCallback):
        def __init__(self, message = None):
            Dumbtone.PushCallback.__init__(self,message)
            self.added = 0
        def callback(self, id, data, total):
            self.added += 1
    
    class FeederCallback(PushCallback):
        def __init__(self, feeder, message = None):
            Dumbtone.PushCallback.__init__(self,message)
            self.added = 0
            self.feeder = feeder
        def callback(self, id, data, total):
            uncdata = zlib.decompress(data)
            self.added += 1            
            try:
                validate_packet(uncdata)
            except MonotoneError,e:
                raise MonotoneError(str(e) + "\npacket %s is broken, you must fix destination repository" % id)
            self.feeder.write(uncdata)

	def finish(self):
	    Dumbtone.PushCallback.finish(self)
	    self.feeder.close()

    def __prepare_local_md(self, branch_pattern):
        callback = Dumbtone.PushCallback("finding items to synchronize")
	try:
            memory_md = self.do_export(None, branch_pattern, callback)
    	    return memory_md
	finally:
	    callback.finish()

    def do_push(self,target_url, branch_pattern, **kwargs):
        print "Pushing changes from DB to %s" % (target_url,)        
        memory_md = self.__prepare_local_md(branch_pattern)
        
        target_md = MerkleDir(writeable_fs_for_url(target_url, **kwargs))
        try:
            callback = Dumbtone.CounterCallback("pushing packets")
            memory_md.push(target_md, callback)
        finally:
            callback.finish()
        
        print "Pushed %s packets" % (callback.added)
    
    def do_pull(self, source_url, branch_pattern, **kwargs):
        print "Pulling changes from %s to database" % (source_url, )
        memory_md = self.__prepare_local_md(branch_pattern)
        source_md = MerkleDir(readable_fs_for_url(source_url, **kwargs))
        
        self.monotone.ensure_db()
	
        feeder = self.monotone.feeder(self.verbosity)
        try:        
            fc = Dumbtone.FeederCallback(feeder, "pulling packets")
            memory_md.pull(source_md, fc)
        finally:
            fc.finish()
        print "Pulled %s packets" % (fc.added)
    
    def do_sync(self, other_url, branch_pattern, **kwargs):                        
        print "Synchronizing database and %s" % (other_url,)        
        memory_md = self.__prepare_local_md(branch_pattern)
        other_md = MerkleDir(writeable_fs_for_url(other_url, **kwargs))
        
	# pull    
	feeder = self.monotone.feeder(self.verbosity)
	try:
	    pull_fc = Dumbtone.FeederCallback(feeder, "pulling packets")
    	    memory_md.pull(other_md, pull_fc)
	finally:
	    pull_fc.finish()
	
	# push
	try:
	    push_c = Dumbtone.CounterCallback("pushing packets")
	    memory_md.push(other_md, push_c)
	finally:
	    push_c.finish()
	    
        print "Pulled %s packets" % (pull_fc.added)
        print "Pushed %s packets" % (push_c.added)
   

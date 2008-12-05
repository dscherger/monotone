#
# This file is part of 'mtndumb'
#
# Copyright (C) Nathaniel Smith <njs@pobox.com> and others,
# licensed to the public under the terms of the GNU GPL (>= 2)
# see the file COPYING for details
# I.e., do what you like, but keep copyright and there's NO WARRANTY.
#

import subprocess
import threading
import os.path
import re
import zlib
import fnmatch

class MonotoneError (Exception):
    pass

class Feeder:
    def __init__(self, verbosity, args):
        # We delay the actual process spawn, so as to avoid running monotone
        # unless some packets are actually written (this is more efficient,
        # and also avoids spurious errors from monotone when 'read' doesn't
        # actually succeed in reading anything).
        self.verbosity=verbosity
        self.args = args
        self.process = None

    # this is technically broken; we might deadlock.
    # subprocess.Popen.communicate uses threads to do this; that'd be
    # better.
    def _write(self, data):        
        if self.process is None:
            self.process = subprocess.Popen(self.args,
                                            stdin=subprocess.PIPE,
                                            stdout=subprocess.PIPE,
                                            stderr=None)
        self.process.stdin.write(data)
        if self.verbosity>1:
            # processing every single call with a new process
            # to give immediate error reporting
            print "writing: >>>\n",data,"\n<<<\n"
            stdout = self.process.communicate()
            
            if self.process.returncode:
                raise MonotoneError, "monotone rejected packets"
            self.process = None

    # uncompresses and writes the data 
    def write(self, uncdata):
        # first, uncompress data
        # feeder mustn't care about mtndumb communication details
        # compression is done at dumb/merkle_dir level
        # uncdata = zlib.decompress(data)
        

        if self.verbosity > 1:
            # verbose op, splits the chunk in the individual packets,
            # and reads them one by one
            for pkt in uncdata.split("[end]"):
                if len(pkt)>1:
                   self._write(pkt+"[end]")
        else:
            self._write(uncdata)

    def close(self):
        if self.process is None:
            return
        try:
            self.process.stdin.close()
            rc = self.process.wait();
            if self.process.returncode:
                raise MonotoneError, self.process.stderr.read()
        finally:
            self.process = None

class Monotone:
    def __init__(self, db, executable="mtn-auto-certs"):
        self.db = db
        self.executable = executable
        self.process = None

    def __del__(self):
        self.ensure_stopped()

    def init_db(self):
        self.run_monotone(["db", "init"])

    def db_exists(self):
        return os.path.exists(self.db)

    def ensure_db(self):
        if not self.db_exists():
            self.init_db()

    def revisions_list(self):
        output = self.automate("select", "i:")
        return output.split()
    
    def revisions_for_pattern(self,branch_pattern):
        branches = self.branches_for_pattern(branch_pattern)
        revs = set()
        for branch in branches:            
            heads = self.automate("heads", branch).split()            
            revs.update(heads)
            revs.update(self.automate("ancestors", *heads).split())        
        return list(revs)

    def graph(self):
        output = self.automate("graph")
        result = {}
        for line in output.splitlines():
            t = line.split(" ")
            result[t[0]] = t[1:]
        return result

    def toposort(self, revisions):
        output = self.automate("toposort", *revisions)
        sorted = output.split()
        assert len(sorted) == len(revisions)
        return sorted

    def get_revision(self, rid):
        return self.automate("get_revision", rid)

    def get_pubkey_packet(self, keyid):
        return check_packet("pubkey", self.run_monotone(["pubkey", keyid]) )
        
    def get_revision_packet(self, rid):
        return check_packet("rdata", self.automate("packet_for_rdata", rid))

    def get_file_packet(self, fid):
        return check_packet("fdata", self.automate("packet_for_fdata", fid) )

    def get_file_delta_packet(self, old_fid, new_fid):
        return check_packet("fdelta", self.automate("packet_for_fdelta", old_fid, new_fid))

    def get_cert_packets(self, rid):
        output = self.automate("packets_for_certs", rid)
        packets = []
        curr_packet = ""
        for line in output.strip().split("\n"):
            curr_packet += line + "\n"
            if line == "[end]":
                packets.append(check_packet("rcert",curr_packet))
                curr_packet = ""
        assert not curr_packet
        return packets

    def get_cert_packet(self, cert_id):
        #print "get_cert_packet(%s)" % cert_id
        return check_packet("rcert", self.automate("packet_for_cert", cert_id))

    def get_cert_ids(self, revisions_selector):
        output = self.automate("select_cert", revisions_selector)        
        for line in output.splitlines():
            t = line.split(" ")
            yield t[0],t[1]
        
    def branches(self):
        return self.automate("branches").split("\n")
        
    def branches_for_pattern(self, branch_pattern):
        """all_branches = self.branches()
        x =  fnmatch.filter( all_branches, branch_pattern )
        print "branch filter : %s" % branch_pattern
        print "           in : %s" % ",".join(all_branches)
        print "          out : %s" % ",".join(x)"""
        return fnmatch.filter(self.branches(), branch_pattern)

    def keys(self):
        return self.basic_io_parser( self.automate("keys") )
        
    def key_names(self):
        output = self.automate("keys")
        keys_parsed = self.basic_io_parser(output)
        ids = {}
        for stanza in keys_parsed:
            assert stanza[0][0] == "name"
            key_name = stanza[0][1]
            ids[key_name[0]] = None

        return ids.keys()

    def certs(self, revision):
        output = self.automate("certs", revision)
        certs_parsed = self.basic_io_parser(output)
        return certs_parsed

    # returns output as a string, raises an error on error
    def run_monotone(self, args, input=None):
        self.ensure_stopped()
        process = subprocess.Popen([self.executable, "--db", self.db] + args,
                                   stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE)
        stdout, stderr = process.communicate(input)
        if process.returncode:
            raise MonotoneError, stderr
        return stdout
    
    def ensure_running(self):
        if self.process and self.process.poll() is not None:
            self.ensure_stopped()
        if not self.process:
            self.process = subprocess.Popen([self.executable, "--db", self.db, "automate", "stdio"],
                                   stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE)
            if self.process.poll() is not None:
                self.process.wait()
                self.process = None
                raise MonotoneError, "failed to start monotone backend"
       
    
    def ensure_stopped(self):
        if self.process:    
            self.process.stdin.close()
            self.process.wait()
            self.process = None
    
    def automate(self, *args):
        def formater(args):
            length_prefixed_args = [str(len(command)) + ":" + command for command in args]
            return "l" + "".join(length_prefixed_args) + "e"

        def parser(pipe):
            def get_fields():
                for i in range(0,4):
                    field = ""
                    sep = pipe.read(1)
                    while not sep == ":":
                        field += sep
                        sep = pipe.read(1)
                        if sep == "": raise MonotoneError, "automate stdio parsing failed (1)"
                    yield field

            data = ""
            while 1:
                cmd, status, cont, size = get_fields()
                if int(size) > 0:
                    data += pipe.read(int(size))
                    # TODO: why this check is sometimes failing !?
                    #if len(data) != int(size): raise MonotoneError, "automate stdio parsing failed (2)"
                    if len(data) == 0: raise MonotoneError, "automate stdio parsing failed, expected %i bytes, got %i (3)" % (int(size), len(data))
                if cont != "m": break
            return data
            
        self.ensure_running()
        stdin_write = threading.Thread(target=self.process.stdin.write, args=[formater(args)])
        stdin_write.setDaemon(True)
        stdin_write.start()
        try:
            return parser(self.process.stdout)
        except MonotoneError, e:
            error = strip_mtn_error_garbage(self.process.stderr.read())
            import time
            time.sleep(0)
            if self.process.poll() != None:
                if error:
                    raise MonotoneError, error
                else:
                    raise MonotoneError, "monotone process died unexpectedly (exit code %i)" % self.process.poll()
            else:
                raise

    # feeds stuff into 'monotone read'
    def feeder(self, verbosity):
        args = [self.executable, "--db", self.db, "read"]
        return Feeder(verbosity, args)

    # copied wholesale from viewmtn (08fd7bf8143512bfcabe5f65cf40013e10b89d28)'s
    # monotone.py.  hacked to remove the []s from hash values, and to leave in
    # ("from", "") tuples.
    def basic_io_parser(self, data):
            "returns a list of lists of (key, value) tuples."
            basic_io_hex_re = re.compile(r'^ *(\S+) \[([0-9A-Za-z]*)\]$')
            basic_io_string_re = re.compile(r'^ *(\S+) (\".*)$')
            def unescape_string_value(str):
                    rv = ""
                    valuestr = []
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
                                    if c == '\\':
                                        in_escape = True
                                    elif c == ' ' and is_terminated:
                                        pass
                                    elif c == '"':
                                        # this can be a start of new string (when is_terminated true) or the
                                        # end of the current one (is_terminated false)
                                        if is_terminated:
                                            if len(rv)>0:
                                                raise Exception("basic_io parse error; string ends twice! '"+ str+"'")
                                            else:
                                                is_terminated=False
                                        else:
                                            is_terminated = True
                                            valuestr.append(rv)
                                            rv = ""
                                    else:
                                        rv += c
                    return is_terminated, valuestr
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


def basic_io_parser(data):
        return Monotone(None,None).basic_io_parser(data)
        
def find_stanza_entry(stanza, name):
        for entry in stanza:
            if entry[0] == name:
                return entry[1]
        raise Exception("entry '%s' not found in stanza" % name)
    
        
cert_packet_info_re = re.compile(r'^\[rcert ([0-9a-f]+)\r?\n\s+(\S+)\s*\r?\n\s+(.*)\r?\n')

def check_packet(type, data):
    validate_packet(data, type)
    return data
    
valid_packet_end = re.compile(r'\[end\](\r\n )+$')

def validate_packet(data, type = None): 
    valid = True
    if data.startswith("error: "):
        valid = False
    if valid_packet_end.match(data):
        valid = False
    if valid and type is not None and not data.startswith("[%s " % type):
        valid = False    
    if not valid:
        firstline = data.splitlines()[0]
        if type is not None:
            raise MonotoneError("unexpected or bad packet (wanted %s), got: %s" % (type, firstline))
        else:
            raise MonotoneError("unknown packet starting with: %s" % firstline)    

def decode_cert_packet_info(cert_packet):
    m = cert_packet_info_re.match(cert_packet)
    if not m:
        raise Exception("bad cert packet: %s..." % repr(cert_packet))
    return (m.group(1), m.group(2), m.group(3))

def strip_mtn_error_garbage(str):
    return "".join( line.lstrip("mtn:") for line in str.splitlines(True) )

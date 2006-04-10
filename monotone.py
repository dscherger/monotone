import subprocess
import threading
import os.path
import re
import zlib

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
                                            stderr=subprocess.PIPE)
        self.process.stdin.write(data)
        if self.verbosity>1:
            # processing every single call with a new process
            # to give immediate error reporting
            stdout, stderr = self.process.communicate()
            print "writing: >>>",data,"<<<\n",stdout,stderr
            if self.process.returncode:
                raise MonotoneError, stderr
            self.process = None

    # uncompresses and writes the data 
    def write(self, data):
        # first, uncompress data
        uncdata = zlib.decompress(data)

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
        stdout, stderr = self.process.communicate()
        if self.process.returncode:
            raise MonotoneError, stderr

class Monotone:
    def __init__(self, db, executable="monotone"):
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
    
    def toposort(self, revisions):
        output = self.automate("toposort", *revisions)
        sorted = output.split()
        assert len(sorted) == len(revisions)
        return sorted

    def get_revision(self, rid):
        return self.automate("get_revision", rid)

    def get_pubkey_packet(self, keyid):
        return self.run_monotone(["pubkey", keyid])
        
    def get_revision_packet(self, rid):
        return self.automate("packet_for_rdata", rid)

    def get_file_packet(self, fid):
        return self.automate("packet_for_fdata", fid)

    def get_file_delta_packet(self, old_fid, new_fid):
        return self.automate("packet_for_fdelta", old_fid, new_fid)

    def get_cert_packets(self, rid):
        output = self.automate("packets_for_certs", rid)
        packets = []
        curr_packet = ""
        for line in output.strip().split("\n"):
            curr_packet += line + "\n"
            if line == "[end]":
                packets.append(curr_packet)
                curr_packet = ""
        assert not curr_packet
        return packets

    def key_names(self):
        output = self.automate("keys")
        keys_parsed = self.basic_io_parser(output)
        ids = {}
        for stanza in keys_parsed:
            assert stanza[0][0] == "name"
            key_name = stanza[0][1]
            ids[key_name[0]] = None

        return ids.keys()

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
        if not self.process:
            self.process = subprocess.Popen([self.executable, "--db", self.db, "automate", "stdio"],
                                   stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE)
    
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
                    yield field
            
            data = "" 
            while 1:
                cmd, status, cont, size = get_fields()
                data += pipe.read(int(size))
                if cont != "m": break
            return data
            
        self.ensure_running()
        stdin_write = threading.Thread(target=self.process.stdin.write, args=[formater(args)])
        stdin_write.setDaemon(True)
        stdin_write.start()
        return parser(self.process.stdout)

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



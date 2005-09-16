import subprocess
import os.path
import re

class MonotoneError (Exception):
    pass

class Feeder:
    def __init__(self, process):
        self.process = process

    # this is technically broken; we might deadlock.
    # subprocess.Popen.communicate uses threads to do this; that'd be
    # better.
    def write(self, data):
        self.process.stdin.write(data)

    def close(self):
        self.process.stdin.close()
        stdout, stderr = process.communicate()
        if process.returncode:
            raise MonotoneError, stderr

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
    
    def toposort(self, revisions):
        output = self.run_monotone(["automate", "toposort", "-@-"],
                                   "\n".join(revisions) + "\n")
        sorted = output.split()
        assert len(sorted) == len(revisions)
        return sorted

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
    def run_monotone(self, args, input=None):
        process = subprocess.Popen([self.executable, "--db", self.db] + args,
                                   stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE)
        stdout, stderr = process.communicate(input)
        if process.returncode:
            raise MonotoneError, stderr
        return stdout

    # feeds stuff into 'monotone read'
    def feeder(self):
        process = subprocess.Popen([self.executable, "--db", self.db, "read"],
                                   stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE)
        return Feeder(process)

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



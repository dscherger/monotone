
import utility
import urllib
import signal
import string
import syslog
import pipes
import sets
import sha
import re
import os
from colorsys import hls_to_rgb

#
# a python wrapper for the "monotone" command
#
# should really use the "automate" interface as much as possible
#

id_re = re.compile(r'^[0-9a-f]+$')
dash_re = re.compile(r'^-+$')
cert_value_re = re.compile(r'^(\S*) *: (.*)$')
basic_io_re = re.compile(r'^ *(\S+) [\"\[](.*)[\"\]]$')
basic_io_hex_re = re.compile(r'^ *(\S+) (\[[0-9A-Fa-f]*\])$')
basic_io_string_re = re.compile(r'^ *(\S+) (\".*)$')
manifest_entry_re = re.compile(r'^(\S+) *(.*)$')
log_entry_re = re.compile(r'^(\S+): (.*)$')

def colour_from_string(str):
    def f(off):
        return ord(hashval[off]) / 256.0
    hashval = sha.new(str).digest()
    hue = f(5)
    li = f(1) * 0.15 + 0.55
    sat = f(2) * 0.5 + .5
    return ''.join(map(lambda x: "%.2x" % int(x * 256), hls_to_rgb(hue, li, sat)))

class Automation:
    def __init__(self, base_command):
        self.command = "%s automate stdio" % (base_command)
        self.process = None
	self.debug = False
    def __del__(self):
        self.stop()
        self.process = None
    def start(self):
        self.process = popen2.Popen3(self.command)
        set_nonblocking(self.process.fromchild)
	if self.debug: syslog.syslog("AUTOMATE; %s starting %s" % (os.getpid(), self.process.pid))
    def stop(self):
        if not self.process: return
	if self.debug: syslog.syslog("AUTOMATE: %s stopping %s" % (os.getpid(), self.process.pid))
        try:
            self.process.tochild.close()
            self.process.fromchild.close()
            if self.process.poll() == -1:
                # the process is still running, so kill it.
                os.kill(self.process.pid, signal.SIGKILL)
            self.process.wait()
        except:
	    if self.debug: syslog.syslog("AUTOMATE: %s failed_to_stop %s" % (os.getpid(), self.process.pid))
            pass
    def run(self, command, args):
        if self.process == None: self.start()
	if self.debug: 
	    syslog.syslog("AUTOMATE: %s run_via %s %s %s" % (os.getpid(), self.process.pid, str(command), str(args)))
        enc = "l%d:%s" % (len(command), command)
        enc += ''.join(map(lambda x: "%d:%s" % (len(x), x), args)) + 'e'
	if self.debug:
	    syslog.syslog("AUTOMATE: %s run_via_send %s %s %s" % (os.getpid(), self.process.pid, str(command), enc))
        # note that fromchild is nonblocking, but tochild is blocking
	self.process.tochild.write(enc)
        self.process.tochild.flush()
        r = RecvPacket()
        complete = False
        result_string = ""
        result_code = None
        while not complete:
	    if self.debug:
		syslog.syslog("AUTOMATE: %s run_via_select %s" % (os.getpid(), self.process.pid))
            ro, rw, re = select.select([self.process.fromchild], [], [], None)
            if not ro and not rw and not re:
                break
            if self.process.fromchild in ro:
                recv = self.process.fromchild.read()
                if recv == "": break
		if self.debug:
		    syslog.syslog("AUTOMATE: %s run_via_recv %s %s %s" % (os.getpid(), self.process.pid, str(command), recv))
		
		data_to_parse = recv
		while not complete:
		    tv = r.process_data(data_to_parse)
		    if tv == None:
			# this data did not result in a complete packet; we need more data from the client 
			break
		    else:
			cmdnum, error, length, is_last, result = tv
			if result_code == None: result_code = int(error)
			result_string += result
			if is_last:
			    complete = True
			else:
			    # any left-over bytes must be parsed in case we have another complete packet
			    data_to_parse = r.buffer
			    r = RecvPacket()
        return result_code, result_string

class Monotone:
    def __init__(self, mt, dbfile):
        self.mt = mt
        self.dbfile = dbfile
        self.base_command = "%s --db=%s" % (self.mt, pipes.quote(self.dbfile))
        self.automate = Automation(self.base_command)
    def branches(self):
        result = utility.run_command(self.base_command + " ls branches")
        if result['exitcode'] != 0:
            raise Exception("Unable to list branches: %s" % (result['childerr']))
        else:
            return filter(None, result['fromchild'].split('\n'))
    def tags(self):
        result = utility.run_command(self.base_command + " ls tags")
        if result['exitcode'] != 0:
            raise Exception("Unable to list tags: %s" % (result['childerr']))
        else:
            return map(lambda x: x.split(' ', 2), filter(None, result['fromchild'].split('\n')))
    def heads(self, branch):
        error, result = self.automate.run('heads', [branch])
        if error != 0:
            raise Exception("Unable to get list of heads for %s: %s" % (branch, result))
        else:
            return filter(None, result.split('\n'))
    def basic_io_parser(self, data):
        """returns a list of lists of (key, value) tuples.  hashes are returned with []s around
        them; strings are returned raw."""
	def unescape_string_value(str):
            rv = ""
            is_terminated = False
            in_escape = False
            if str[0] != '"':
                raise Exception("basic_io parse error; not a string.")
            for c in str[1:]:
                if in_escape:
                    if c != '\\' and c != '\"':
                        raise Exception(r'basic_io parse error; expected \" or \\')
                    rv += c
                    in_escape = False
                else:
                    if c == '\\':
                        in_escape = True
                    elif c == '"':
                        if is_terminated:
                            raise Exception("basic_io parse error; string ends twice!")
                        is_terminated = True
                    else:
                        rv += c
            return is_terminated, rv

        # 14:46 < tbrownaw> list<multimap<string, array<string>>>, with the outer list divided according to 
        #                   what item starts a stanza?

	rv = {}

        stanza = []
        ongoing_string = None

	for line in data.split('\n'):
	    if ongoing_string != None:
                ongoing_string += '\n' + line
                is_terminated, e_value = unescape_string_value(ongoing_string)
                if is_terminated:
		    stanza += [key, e_value]
                    ongoing_string = None
		continue

	    if line == '' and len(stanza) != 0:
		rv.setdefault(stanza[0], []).append(stanza)
		stanza = []
		continue

	    m = basic_io_hex_re.match(line)
	    if m:
		key, value = m.groups()
		stanza += [key, value[1:-1]]
		continue

	    m = basic_io_string_re.match(line)
	    if m:
		key, value = m.groups()
		is_terminated, e_value = unescape_string_value(value)
		if not is_terminated: ongoing_string = value
		else: stanza += [key, e_value]
		continue
        return rv
    def certs(self, id):
	"returns a list of certs, each a list of tuples (attribute,value)"
        error, data = self.automate.run('certs', [id])
        if error != 0: raise Exception("Error obtaining cert for %s: %s" % (id, data))
        parsed = self.basic_io_parser(data)
	if len(parsed.keys()) != 1 or parsed.keys()[0] != 'key':
	    raise Exception("basic_io format for certs has changed: unknown cert types '%s' found" % (str(parsed.keys())))
	certs = parsed['key']
	rv = []
	for cert in certs:
	    rv.append([(cert[t], cert[t+1]) for t in range(0, len(cert), 2)])
	return rv
    def ancestors(self, ids):
	if type(ids) == type(""): ids = [ids]
        error, result = self.automate.run('ancestors', ids)
        if error != 0:
            raise Exception("Unable to get ancestors of %s: %s" % (str(ids),
								   result))
        else:
            return filter(None, result.split('\n'))
    def toposort(self, ids):
	if type(ids) == type(""): ids = [ids]
        error, result = self.automate.run('toposort', ids)
        if error != 0:
            raise Exception("Unable to toposort: %s" % (result))
        else:
            return filter(None, result.split('\n'))
    def revision(self, id):
        error, result = self.automate.run('get_revision', [id])
        if error != 0:
            raise Exception("Unable to get revision %s: %s" % (id, result))
	return self.basic_io_parser(result)
    def parents(self, id):
	error, result = self.automate.run('parents', [id])
        if error != 0:
            raise Exception("Unable to get parents of %s: %s" % (id, result))
	else:
            return filter(None, result.split('\n'))
    def manifest(self, id):
        error, result = self.automate.run('get_manifest', [id])
        if error != 0:
            raise Exception("Unable to get manifest %s: %s" % (id, result))
        rv = []
        for line in result.split('\n'):
            m = manifest_entry_re.match(line)
            if not m: continue
            rv.append(m.groups())
        return rv
    def file(self, id):
        error, result = self.automate.run('get_file', [id])
        if error != 0:
            raise Exception("Unable to get file %s: %s" % (id, result))
        else:
            return result

    def annotate(self, id, file):
        result = utility.run_command(self.base_command + " annotate --revision=%s %s" % (pipes.quote(id), 
											 pipes.quote(file)))
        if result['exitcode'] != 0:
            raise Exception("Unable to annotate file: %s using command '%s'" % (result['childerr'], 
										result['run_command']))
        else:
            return result['fromchild']
        
    def diff(self, rev_from, rev_to, files=None):
        command = self.base_command + " diff -r %s -r %s" % (pipes.quote(rev_from), pipes.quote(rev_to))
        if files != None: command += ' ' + ' '.join(map(pipes.quote, files))
        syslog.syslog(command)
        result = utility.run_command(command)
        if result['exitcode'] != 0:
            raise Exception("Unable to calculate diff: %s" % (result['childerr']))
        else:
            return result['fromchild']
    def log(self, ids, limit=0):
        rv = []
        entry = None
        command = self.base_command + " log " + ' '.join(map(lambda x: '-r ' + pipes.quote(x), ids))
        if limit > 0: command += " --last=%d" % (limit)
        iterator = utility.iter_command(command)
        for line in iterator:
            if dash_re.match(line):
                entry = {}
            if entry == None: continue
            if not line:
                rv.append(entry)
                entry = None
                continue
            m = log_entry_re.match(line)
            if m:
                attr, value = m.groups()
                if not entry.has_key(attr): entry[attr] = []
                entry[attr].append(value)
        # clean up; otherwise we'll leak filehandlers
        map(None, iterator)
        if entry: rv.append(entry)
        return rv
    def ancestry_graph(self, graphopts, id, limit=0):
        def dot_escape(s):
            # kinda paranoid, should probably revise later
            permitted=string.digits + string.letters + ' -<>-:,*@!$%^&.+_~?/'
            return ''.join(filter(lambda x: x in permitted, s))
        graphdir = graphopts['directory']
        graphuri = graphopts['uri']
        graph_id = "%s.%d" % (id, limit)
        rv = {
            'dot_file' : os.path.join(graphdir, graph_id + ".dot"),
            'image_file' : os.path.join(graphdir, graph_id + ".png"),
            'imagemap_file' : os.path.join(graphdir, graph_id + ".html"),
            'dot_uri' : "%s/%s.dot" % (graphuri, urllib.quote(graph_id)), 
            'image_uri' : "%s/%s.png" % (graphuri, urllib.quote(graph_id)), 
            'imagemap_uri' : "%s/%s.html" % (graphuri, urllib.quote(graph_id)), 
        }
        need_access = ['dot_file', 'image_file', 'imagemap_file']
        missing = filter(lambda x: x != True, map(lambda x: os.access(rv[x], os.R_OK), need_access))
        if len(missing) == 0:
            rv['cached'] = True
            return rv
        contents = 'digraph ancestry {\nratio=compress\nnodesep=0.1\nranksep=0.2\nedge [dir=back];\n'
        revisions = {}
        for attrs in self.log([id], limit):
            if not attrs.has_key("Revision") or not attrs.has_key("Ancestor"):
                continue
            revision = attrs['Revision'][0]
            revisions[revision] = attrs
            for ancestor in attrs['Ancestor']:
                if len(ancestor) == 0: continue
                if not revisions.has_key(ancestor): revisions[ancestor] = None
                contents += '"%s"->"%s"[href="getdiff.py?id1=%s&id2=%s"]\n' % (urllib.quote(revision), 
									       urllib.quote(ancestor), 
									       urllib.quote(ancestor), 
									       urllib.quote(revision))
        for revision in revisions.keys():
            label = "%s..." % (revision[0:8])
            attrs = revisions[revision]
            if attrs == None:
                # fill in the gaps; would be nice to clean this up.
                # shouldn't take long, anyway.
                attrs = self.log([revision], 1)[0]
            if attrs.has_key('Date'):
                d = dot_escape(attrs['Date'][0])
                d = d[0:d.find("T")]
                label += " on %s" % d
            if attrs.has_key('Author'):
                label += "\\n%s" % (dot_escape(attrs['Author'][0]))
                override_fillcolor = colour_from_string(attrs['Author'][0])
            else: override_fillcolor = None
            opts = 'label="%s"' % label #revision[0:8]
            for opt in graphopts['nodeopts']:
                if opt == 'fillcolor' and override_fillcolor != None: continue
                opts += ',%s="%s"' % (opt, graphopts['nodeopts'][opt])
            if revision == id: opts += ",color=blue"
            opts += ',href="revision.psp?id=%s"' % urllib.quote(revision)
            if override_fillcolor != None: opts += ',fillcolor="#%s"' % (override_fillcolor)
            contents += '"%s" [%s]\n' % (revision, opts)
        contents += "}\n"
        open(rv['dot_file'], 'w').write(contents)
        os.system("%s -Tcmapx -o %s -Tpng -o %s %s" % (graphopts['dot'], 
						       pipes.quote(rv['imagemap_file']), 
						       rv['image_file'], 
						       rv['dot_file']))
        rv['cached'] = False
        return rv

def is_valid_id(s):
    return len(s) == 40 and id_re.match(s) != None

import popen2
import select
from utility import set_nonblocking
            
packet_header_re = re.compile(r'(\d+):(\d+):([lm]):(\d+):')

class RecvPacket:
    "A packet received from monotone automate stdio"
    def __init__(self, init_buffer=""):
        self.buffer = init_buffer
        self.cmdnum = None
        self.error = None
        self.length = None
        self.is_last = None
        self.result = ""
    def process_data(self, data):
        self.buffer += data
        if self.length == None:
	    # we have not yet read a complete header line for this packet
            m = packet_header_re.match(self.buffer)
            if m:
                self.cmdnum, self.error, pstate, self.length = m.groups()
                self.length = int(self.length)
                self.is_last = pstate == "l"
                self.buffer = self.buffer[m.end(m.lastindex)+1:]
        if self.length != None and len(self.result) < self.length:
            needed = self.length - len(self.result)
            if len(self.buffer) >= needed:
                available = needed
            else:
                available = len(self.buffer)
            self.result += self.buffer[:available]
            self.buffer = self.buffer[available:]
        if len(self.result) == self.length:
            return (self.cmdnum, self.error, self.length, self.is_last, self.result)
        else: return None


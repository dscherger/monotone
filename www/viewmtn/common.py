
import datetime
import urllib
import pydoc
import time

escape_function = pydoc.HTMLRepr().escape

def type_wrapper(e, x):
    if x == None:
        return ""
    elif type(x) == type([]):
        return '<br />'.join(map(e, x))
    else:
        return e(x)

def parse_timecert(value):
    return apply(datetime.datetime, time.strptime(value, "%Y-%m-%dT%H:%M:%S")[:6])

def get_branch_links(mt, branches):
    if len(branches) > 1:
	branch_links = "branches "
    else:
	branch_links = "branch "
    links = []
    for branch in branches:
	links.append(link(mt, "branch", branch))
    return branch_links + ', '.join(links)

def extract_cert_from_certs(certs, certname, as_list=False):
    rv = []
    for cert in certs:
	name, value = None, None
	for k, v in cert:
	    if k == "name": name = v
	    elif k == "value": value = v
	if name == None or value == None: continue
	if name == certname:
	    if not as_list:
		return value
	    else:
		rv.append(value)
    return rv

def determine_date(certs):
    dateval = extract_cert_from_certs(certs, "date")
    if dateval == None:
	return None
    else:
	return parse_timecert(dateval)

def quicklog(value):
    hq = html_escape()
    rv = hq(value.strip().split('\n')[0])
    if rv.startswith('*'):
	rv = rv[1:].strip()
    return rv

def ago_string(event, now):
    def plural(v, singular, plural):
	if v == 1:
	    return "%d %s" % (v, singular)
	else:
	    return "%d %s" % (v, plural)
    now = datetime.datetime.utcnow()
    ago = now - event
    if ago.days > 0:
	rv = "%s, %s" % (plural(ago.days, "day", "days"), 
			 plural(ago.seconds / 3600, "hour", "hours"))
    elif ago.seconds > 3600:
        hours = ago.seconds / 3600
        minutes = (ago.seconds - (hours * 3600)) / 60
	rv = "%s, %s" % (plural(hours, "hour", "hours"), 
			 plural(minutes, "minute", "minutes"))
    else:
        minutes = ago.seconds / 60
        seconds = (ago.seconds - (minutes * 60))
        rv = "%s, %s" % (plural(minutes, "minute", "minutes"), 
			 plural(seconds, "second", "seconds"))
    return rv

def link(mt, link_type, link_to, description = None, no_quote = False):
    hq = html_escape()
    if not no_quote and description != None: description = hq(description)
    if link_type == "revision":
	rv = '<a href="revision.psp?id=%s">' % (urllib.quote(link_to))
	if description != None: rv += description
	else: rv +=  hq(link_to[:8]) + ".."
	rv += '</a>'
	if description == None: rv = '[' + rv + ']'
    elif link_type == "diff" or link_type == "download_diff":
	link_to = map(urllib.quote, filter(lambda x: x != None, link_to))
	if link_type == "diff":
	    handler = "diff.psp"
	else:
	    handler = "getdiff.py"
	uri = '%s?id1=%s&amp;id2=%s' % (handler, link_to[0], link_to[1])
	if len(link_to) == 3:
	    uri += '&amp;fname=%s' % (link_to[2])
	rv = '<a href="' + uri + '">'
	if description != None: rv += description
	else: rv += "diff"
	rv += '</a>'
    elif link_type == "download":
	if type(link_to) == type([]):
	    rv = '<a href="getfile.py?id=%s&amp;path=%s">' % (urllib.quote(link_to[0]), 
							      urllib.quote(link_to[1]))
	    link_id = link_to[0]
	else:
	    rv = '<a href="getfile.py?id=%s">' % (urllib.quote(link_to))
	    link_id = link_to
	if description != None: rv += description + "</a>"
	else: rv = "[" + rv + hq(link_id[:8]) + ".." + "</a>]"
    elif link_type == "file":
	revision_id, path = link_to
	rv = '<a href="file.psp?id=%s&amp;path=%s">' % (urllib.quote(revision_id), 
							urllib.quote(path))
	if description != None: rv += description + "</a>"
	else: rv = "[" + rv + hq(path + '@' + revision_id[:8]) + ".." + "</a>]"
    elif link_type == "fileinbranch":
	branch, path = link_to
	rv = '<a href="fileinbranch.psp?branch=%s&amp;path=%s">' % (urllib.quote(branch), 
								    urllib.quote(path))
	if description != None: rv += description + "</a>"
	else: rv = "[" + rv + hq(path + '@' + branch) + "</a>]"
    elif link_type == "branch":
	rv = '<a href="branch.psp?branch=%s">' % (urllib.quote(link_to))
	if description != None: rv += description
	else: rv +=  hq(link_to)
	rv += '</a>'
    elif link_type == "tar":
	rv = '<a href="gettar.py?id=%s">' % (urllib.quote(link_to))
	if description != None: rv += description
	else: rv = "tar of [" + rv + hq(link_to[:8]) + "..]" + "</a>]"
	rv += '</a>'
    elif link_type == "headofbranch":
	rv = '<a href="headofbranch.psp?branch=%s">' % (urllib.quote(link_to))
	if description != None: rv += description
	else: rv +=  "head of " + hq(link_to)
	rv += '</a>'
    elif link_type == "manifest":
	if type(link_to) == type([]):
	    link_to, path = link_to
	    rv = '<a href="manifest.psp?id=%s&amp;path=%s">' % (urllib.quote(link_to), urllib.quote(path))
	else:
	    rv = '<a href="manifest.psp?id=%s">' % (urllib.quote(link_to))
	if description != None: rv += description
	else: rv +=  hq(link_to[:8]) + ".."
	rv += '</a>'
	if description == None: rv = '[' + rv + ']'
    else:
	rv = '<span style="color:red;">Unknown link type: %s</span>' % (hq(link_type))
    return '<span class="%s">%s</span>' % (hq(link_type+'Link'), rv)

def html_escape():
    "returns a function stolen from pydoc that can be used to escape HTML"
    return lambda x: type_wrapper(escape_function, x)

from enscriptlangs import enscript_langs
from utility import run_command
import mimetypes
import config
import pipes

# is it binary?
def is_binary(str):
    nontext_chars = "\x01\x02\x03\x04\x05\x06\x0e\x0f\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1c\x1d\x1e\x1f"
    check = {}
    for char in nontext_chars:
	check[char] = True
	for i in str:
	    if check.has_key(i): return True
    return False

# hm, later on might make this be some javascript that does an call back to the server.
# then could have a pull down to let people choose which enscript encoding to use, and 
# just update the DOM with the new data.
def colourise_code(req, hq, path, contents, filter=None):
    mime_type = mimetypes.guess_type(path)[0]
    if mime_type == None: mime_type = 'text/plain'
    if mime_type == 'image/png' or mime_type == 'image/jpeg' or mime_type == 'image/gif':
	display_as_image = True
    else: display_as_image = False
    
    # okay; can we guess a valid enscript filter to run this through?
    tsp = mime_type.split('/', 1)
    if filter == None and tsp[0] == 'text':
	candidate = tsp[1]
	if candidate.startswith('x-'): candidate = candidate[2:]
	if candidate.endswith('src'): candidate = candidate[:-3]
	if candidate.endswith('hdr'): candidate = candidate[:-3]
	if candidate == 'c++': candidate = 'cpp' # ugly
	if candidate in enscript_langs: filter = candidate
    if filter == None:
	# heh, will at least work for lua files
	last_dot = path.rfind('.')
	if last_dot == -1: last_dot = 0
	candidate = path[last_dot:]
	if candidate in enscript_langs: filter = candidate

    # if no filter then let's check if it's binary or not; if not binary 
    # we'll just treat it as text; otherwise display a warning and a download 
    # link
    if filter == None and not is_binary(contents):
	filter = 'text'

    req.write('''<div style="border-color: black; border-style: solid; border-width: 1px; padding-left: 0.5em; padding-right: 0.5em;">''')
    if display_as_image:
	req.write('''<img border='0' src='getfile.py?id=%s&path=%s />''' % (urllib.quote(matching_file_id), urllib.quote(path)))
    elif filter != None:
	def start_code():
	    req.write('<PRE style="padding: 0;">')
	def stop_code():
	    req.write('</PRE>')
	def text():
	    start_code()
	    req.write(hq(contents))
	    stop_code()
	def enscript():
	    command = config.enscript_path + ' -o - --color --language=html'
	    command += ' --highlight=%s' % (pipes.quote(filter))
	    result = run_command(command, to_child=contents)
	    if result['exitcode'] != 0:
		raise Exception('Error running enscript (%s) : "%s".' % (hq(command), hq(result['childerr'])))
	    in_contents = False
	    for line in result['fromchild'].split('\n'):
		if line.startswith('<PRE>'):
		    in_contents = True
		    start_code()
		elif line.startswith('</PRE>'): 
		    in_contents = False
		    stop_code()
		elif in_contents:
		    req.write(line + '\r\n')
	if filter == "text": text()
	else: enscript()
    else:
	req.write('''<p style="text-align: center"><em>This file seems to binary and not suitable for display in the browser. You must %s the file and use a suitable viewer.</em></p>''' % (link("download", [matching_file_id, path], "download")))
    req.write('''</div>''')

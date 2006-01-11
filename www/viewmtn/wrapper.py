
from mod_python import apache,psp,util
from common import parse_timecert, ago_string, determine_date
import mimetypes
import monotone
import urlparse
import datetime
import os.path
import tarfile
import common
import config
import urllib
import json
import os
import re

from monotone import Monotone
from html import Template

# paranoid
sane_uri_re = re.compile('^\w+$')

def get_file(req, vars):
    mt = vars['mt']
    form = util.FieldStorage(req)
    if not form.has_key('id'):
        return apache.HTTP_BAD_REQUEST
    id = form['id']
    if not monotone.is_valid_id(id):
        return apache.HTTP_BAD_REQUEST
    if form.has_key('plain'):
        mime_type = "text/plain"
    else:
        mime_type = None
    if form.has_key('path'):
        if mime_type == None: mime_type = mimetypes.guess_type(form['path'])[0]
        req.headers_out["Content-Disposition"] = "attachment; filename=%s" % urllib.quote(os.path.split(form['path'])[-1])
    if mime_type == None: mime_type = "text/plain"
    req.content_type = mime_type + "; charset=utf-8"
    req.write(mt.file(id))
    return apache.OK

def get_diff(req, vars):
    mt = vars['mt']
    form = util.FieldStorage(req)
    if not form.has_key('id1') or not form.has_key('id2'):
        return apache.HTTP_BAD_REQUEST
    if form.has_key('fname'): files = [form['fname']]
    else: files = None
    id1, id2 = form['id1'], form['id2']
    if not monotone.is_valid_id(id1) or not monotone.is_valid_id(id2):
        return apache.HTTP_BAD_REQUEST
    req.content_type = "text/plain; charset=utf-8"
    req.write(mt.diff(id1, id2, files))
    return apache.OK

def get_json(req, vars):
    mt = vars['mt']
    form = util.FieldStorage(req)
    if not form.has_key('className') or not form.has_key('linkUri'):
    	return apache.HTTP_BAD_REQUEST
    class_name, link_uri = form['className'], form['linkUri']
    req.content_type = "text/plain; charset=utf-8"
    writer = json.JsonWriter()
    query = {}
    for key, value in [t.split('=', 1) for t in urlparse.urlparse(link_uri)[4].split('&')]:
	query[key] = value

    now = datetime.datetime.utcnow()
    rv = {}
    if class_name == "revisionLink":
	rv['type'] = 'revision'
	certs = mt.certs(query['id'])
	rv['author'] = common.extract_cert_from_certs(certs, 'author')
	change_date = determine_date(certs)
	if change_date != None:
	    ago = ago_string(change_date, now)
	else:
	    ago = ''
	rv['ago'] = ago
    elif class_name == "branchLink":
	rv['type'] = 'branch'
	heads = mt.heads(query['branch'])
	most_recent_change = None
	for head in heads:
	    certs = mt.certs(head)
	    this_change = determine_date(certs)
	    if this_change == None:
		continue
	    if most_recent_change == None or this_change > most_recent_change:
		most_recent_change = this_change
		most_recent_change_certs = certs
	if most_recent_change != None:
	    ago = ago_string(most_recent_change, now) + ' ago'
	    last_author = common.extract_cert_from_certs(most_recent_change_certs,
							 "author") or ''
	else:
	    ago = ''
	rv['ago'] = ago
	rv['author'] = last_author
    elif class_name == "manifestLink":
	rv['type'] = 'manifest'
	revision = mt.revision(query['id'])
	if revision.has_key('new_manifest'):
	    manifest_id = revision['new_manifest'][0][1]
	    manifest = mt.manifest(manifest_id)
	    dir_seen = {} # would use a set, but need python2.4 really
	    for file_id, filename in manifest:
		fsp = filename.rsplit('/', 1)
		if len(fsp) == 2 and not dir_seen.has_key(fsp[1]):
		    dir_seen[fsp[1]] = True
	    rv['file_count'] = len(manifest)
	    rv['directory_count'] = len(dir_seen.keys()) + 1 # root dir
	else:
	    rv['file_count'] = 0
	    rv['directory_count'] = 0

    req.write(writer.write(rv))
    return apache.OK

def get_tar(req, vars):
    "make a tar file out of a given manifest ID"
    class DummyFile:
        def __init__(self, buf):
            self.buf = buf
        def seek(offset, whence=None):
            # blah
            return
        def read(self, size):
            rv, nb = self.buf[:size], self.buf[size:]
            self.buf = nb
            return rv
        def write(self, s):
            self.buf += s
    mt = vars['mt'] 
    form = util.FieldStorage(req)
    if not form.has_key('id'):
        return apache.HTTP_BAD_REQUEST
    id = form['id']
    tar_file = DummyFile("")
    tar_file_name = "%s.tar" % (id)
    req.content_type = 'application/x-tar; charset=utf-8'
    req.headers_out["Content-Disposition"] = "attachment; filename=%s" % tar_file_name
    tf = tarfile.open(mode="w", fileobj=tar_file)
    for fileid, filename in mt.manifest(id):
        data = mt.file(fileid)
        ti = tarfile.TarInfo()
        ti.mode = 00700
        ti.mtime = 0
        ti.type = tarfile.REGTYPE
        ti.uid = 0
        ti.gid = 0
        ti.name = os.path.join(id, filename)
        ti.size = len(data)
        tf.addfile(ti, DummyFile(data))
    tf.close()
    req.write(tar_file.buf)
    return apache.OK

handlers = {
    'getfile.py' : get_file,
    'getdiff.py' : get_diff,
    'gettar.py' : get_tar,
    'getjson.py' : get_json
}

def cleanup(req, vars):
    mt = vars['mt']
    mt.automate.stop()
    del mt

def handler(req):
    uri = req.uri
    slash = uri.rfind('/')
    if slash <> -1: uri = uri[slash+1:]

    # most monotone output is utf8
    req.content_type = "text/html; charset=utf-8"

    # common variables which all handlers (PSP or not)
    # should have access to; ensure that these variables
    # are cleaned up
    mt = Monotone(config.monotone, config.dbfile(req.uri))
    def our_link (link_type, link_to, description=None, no_quote=False):
	return common.link(mt, link_type, link_to, description, no_quote)
    vars = { 
	'mt' : mt, 
	'link' : our_link, 
	'hq' : common.html_escape(),
	'template' : Template()
	}
    req.register_cleanup(cleanup, (req, vars))

    # 
    # these handlers don't use PSP, for example if they need 
    # to return binary or otherwise pristine data to the user 
    # agent
    #
    if handlers.has_key(uri):
	if req.header_only:
	    return apache.OK
        return handlers[uri](req, vars)

    # 
    # PSP or 404
    #
    try:
        if uri.endswith('.psp') and sane_uri_re.match(uri[:-4]):
	    if req.header_only:
		return apache.OK
            instance = psp.PSP(req, filename=uri, vars=vars)
            instance.run()
            return apache.OK
    except ValueError:
        return apache.HTTP_NOT_FOUND

    return apache.HTTP_NOT_FOUND
 

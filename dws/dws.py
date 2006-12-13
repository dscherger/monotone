#!/usr/bin/env python
#
# This file is part of 'mtnplain'
#
# Copyright (C) Zbigniew Zagorski <zzbigg@o2.pl>
# licensed to the public under the terms of the GNU GPL (>= 2)
# see the file COPYING for details
# I.e., do what you like, but keep copyright and there's NO WARRANTY.
#

import urllib2
import httplib
import urlparse
import sys
import re

class Urllib2Opener:
    def __init__(self, baseurl):
        (scheme, host, path, query, frag) = urlparse.urlsplit(baseurl, "http")
        self.baseUrl = scheme + "://" + host
        
    def request(self, uri, headers = None, postData = None):
        theUrl = self.baseUrl + uri
        if postData:
            req = urllib2.Request( url=theUrl, data = postData )
        else:
            req = urllib2.Request( url=theUrl )

        f = urllib2.urlopen(req)
        return (f.code, f.msg, f.headers.dict, f.read())
        
        
class HTTPLibOpener:
    def __init__(self, baseurl):
        (scheme, host, path, query, frag) = urlparse.urlsplit(baseurl, "http")
        if scheme == "http":
            self.conn = httplib.HTTPConnection(host)
        else:
            self.verbose(1,"https connection to %s" % host)
        self.baseUrl = path
        
    def request(self, uri, headers = None, postData = None):
        h = (headers or {}).copy()
        h['Connection'] = 'keep-alive'
        if postData:
            self.conn.request("POST", uri, body = postData, headers = h)
        else:
            self.conn.request("GET", uri, headers = h)
        req = self.conn.getresponse()
        return (req.status, req.reason, dict(req.getheaders()), req.read())

class Connection:
    MAX_POST = 512*1024
    
    def __init__(self, base, path="", verbosity=0):
        """
            base - address of DWS server (example: http://yourhost.org/dws.php"
            name - base path in DWS namespace  
        """
        self.base = base
        self.path = path
        if len(self.path) > 0 and self.path[-1] != '/':
            self.path += '/'
        self.verbosity = verbosity
        (scheme, host, path, query, frag) = urlparse.urlsplit(self.base, "http")
        if scheme not in ("http","https"): raise Exception("bad scheme: http,https are supported")
        self.opener = HTTPLibOpener(self.base)
        self.baseUri = path
        
    def request(self, what, method="GET", postData = "", requestHeaders = None, responseHeaders = None, **kwargs):
        theUri = self.baseUri
        args = {}
        args.update(kwargs)
        args['r'] = str(what)
        if theUri[-1].find('?') != -1:
            d = "&"
        else:
            d = "?"
        for k,v in args.iteritems():
            theUri += d + k + "=" + str(v)
            d = "&"
        if postData:
            self.verbose( 1, "requesting %s (POST %i bytes)" % (theUri, len(postData)) )
        else:
            self.verbose( 1, "requesting %s" % theUri )
        respStatus, respReason, respHeaders, respContent = self.opener.request(theUri, requestHeaders, postData)
        if respStatus != httplib.OK:
            raise IOError("DWS not responding, HTTP response %i %s (%s)" % (respStatus,respReason,theUri))
        dwsStatus = respHeaders.get("x-dws-status","BAD")
        if dwsStatus != "OK":
            self.warning("DWS bad response\ndws server: " + "\ndws server: ".join( respContent.split("\n") ) ) 
            dwsMessage = respHeaders.get("x-dws-message","unknown error")
            raise IOError("DWS server: %s: %s (%s)" % (dwsStatus,dwsMessage,theUri))            
        if responseHeaders is not None:
            responseHeaders.update(respHeaders)
            
        self.verbose(3,"'%s': headers: '%s'" % (theUri, repr(respHeaders)))        
        self.verbose(2,"'%s': readed %i bytes" % (theUri, len(respContent)))
        self.verbose(3,"'%s': content: '%s'" % (theUri, repr(str(respContent))))
        return respContent

    def clear(self):
        self.request("clear")

    def list(self):
        allFiles = self.request("list_files").split("\n")[0:-1]
        plen = len(self.path)
        if plen == 0: return allFiles
        r = []
        for ri in allFiles:
            if ri[0:plen] == self.path:
                r.append(ri[plen:])
        return r
        
    matchSimpleValue = re.compile('^\\s*(\\S+)\\s*:\\s*(\\S(.*))$')
    
    def stat(self, name):
        realName = self.path + name
        response = self.request("stat", n = realName)
        result = {}
        for line in response.split("\n"):
            if line == "": continue
            m = self.matchSimpleValue.match(line)
            if not m:
                self.warning( "stat: unknown line: '%s'" % line)
                continue
            result[m.group(1)] = m.group(2)
        if not result:
            raise Exception("DWS FATAL ERROR: bad 'stat' response for file '%s'" % name)
        return result

    def get(self,name):
        realName = self.path + name
        return self.request("get", n =  realName)

    def getParts(self, name, parts):
        realName = self.path + name
        partsArg = ",".join( [ "%i:%i" % (off, size) for off, size in parts ] )
        return  self.request("getparts", n = realName, parts = partsArg)

    # TODO: test split!
    def put(self,name, content):
        realName = self.path + name
        if len(content) > self.MAX_POST:
            i = splitContent(content, self.MAX_POST)
            chunk = i.next()
            result = self.request("put", n = realName, postData=chunk)
            for chunk in i:
                result = self.request("append", n = realName, postData=chunk)
            return result
        else:
            return self.request("put", n = realName, postData=content)

    # params:
    #    files = [ (name, content), ... ]
    # returns:
    #    dws.Response
    # TODO: test split!
    def putMany(self, files):        
        agrContent = ""
        names = []
        sizes = []
        result = None
        for name, content in files:
            if  len(content) > self.MAX_POST:
                result = self.put(name,content)
            elif len(agrContent) + len(content) > self.MAX_POST:
                result = self.request("put_many", n=",".join(names), s=",".join(sizes), postData = agrContent)
                agrContent = ""
                names = []
                sizes = []
            else:
                agrContent += content
                names.append(self.path + name)
                sizes.append(str(len(content)))        
        if names:
            result = self.request("put_many", n=",".join(names), s=",".join(sizes), postData = agrContent)
        return result
     
    # 
    # params:
    #    names = [ filename, ... ]
    # returns:
    #    false -> error
    #    [ content, ... ] -> success
    #    content[i] == None -> error occured during rading i-th file
    #
    def getMany(self, names):
        responseHeaders = {}
        agrContent = self.request("get_many", n=",".join(self.path+name for name in names), responseHeaders = responseHeaders)
        if not responseHeaders.has_key('x-dws-sizes'):
            raise Exception("DWS: getMany response invalid")
        sizesStr = responseHeaders['x-dws-sizes']
        try:
            sizes = map(int,sizesStr.split(","))
        except ValueError:
            raise Exception("DWS: getMany response invalid: invalid sizes descriptor")
        if len(sizes) != len(names):
            raise Exception("DWS: getMany response invalid: mismatched files count")
        expectedSizes = [s for s in sizes if s != -1]
        expectedSize = sum(expectedSizes) 
        if expectedSize != len(agrContent):
            adinfo = ""
            if expectedSize < len(agrContent) and len(agrContent)-expectedSize < 100:
                adinfo = " extra: '%s'" % repr(agrContent[expectedSize:])
            raise Exception("DWS: getMany response invalid: mismatched response size (exp=%i, act=%i)%s" % (expectedSize, len(agrContent),adinfo))
        result = []
        offset = 0
        for size in sizes:
            if size != -1:
                content = agrContent[offset:offset+size]
                offset += size
                result.append(content)
            else:
                result.append(None)
        return result
        
    # TODO: test split!
    def append(self, name, content):
        realName = self.path + name
        lastResult = None
        for chunk in splitContent(content, self.MAX_POST):
            lastResult = self.request("append", n = realName, postData=chunk)
        return lastResult
            

    def error(self, msg):
        print >> sys.stderr, "dws: error: %s" % msg

    def verbose(self, level, msg):
        if level <= self.verbosity:
            print >> sys.stderr, "dws: info %i: %s" % (level,msg)

    def warning(self, msg):
        print >> sys.stderr, "dws: warning: %s" % msg

        
def splitContent(content, size):
    offset = 0
    todo = len(content)
    while todo > 0:
        if todo > size:
            yield content[offset:offset+size]
            todo -= size
            offset += size
        else:
            yield content[offset:offset+todo]
            break


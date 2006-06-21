#!/usr/bin/env python

import urllib2
import httplib
import urlparse
import sys
import re

class Connection:
    def __init__(self, base, path=""):
        self.base = base
        self.path = path
        (scheme, host, path, query, frag) = urlparse.urlsplit(self.base, "http")
        if scheme not in ("http","https"): raise Exception("bad scheme: http,https are supported")
        self.host = host
        if scheme == "http":
            self.conn = httplib.HTTPConnection(host)
        else:
            self.conn = httplib.HTTPSConnection(host)        
        self.baseUrl = "/" + path
        
    def request(self, what, method="GET", postData = "", **kwargs):
        theUrl = self.baseUrl
        args = {}
        args.update(kwargs)
        args['r'] = str(what)
        if theUrl[-1].find('?') != -1:
            d = "&"
        else:
            d = "?"
        for k,v in args.iteritems():
            theUrl += d + k + "=" + str(v)
            d = "&"
        if postData:
            self.verbose( "requesting %s (POST %i bytes)" % (theUrl, len(postData)) )
        else:
            self.verbose( "requesting %s" % theUrl )
        if self.conn:
            headers = {
                'Connection': 'keep-alive'
            }
            if postData:
                self.conn.request("POST", theUrl, body = postData, headers = headers)
            else:
                self.conn.request("GET", theUrl, headers = headers)
            req = self.conn.getresponse()
            try:
                return str(req.read())
            except Exception,e:
                self.error( "request failed (%s): %s" %(theUrl, str(e)) )
                raise
        else:
            if postData:
                req = urllib2.Request( url=theUrl, data = postData )
            else:
                req = urllib2.Request( url=theUrl )
            
            try:
                f = urllib2.urlopen(req)
                r = str(f.read())
            except Exception,e:
                self.error( "request failed (%s): %s" %(theUrl, str(e)) )
                raise
            return r
        
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

    def put(self,name, content):
        realName = self.path + name
        return self.request("put", n = realName, postData=content)
        
    def append(self, name, content):
        realName = self.path + name    
        return self.request("append", n = realName, postData=content)

    def error(self, msg):
        print >> sys.stderr, "dws: error: %s" % msg

    def verbose(self, msg):
        pass
        
    def warning(self, msg):
        print >> sys.stderr, "dws: warning: %s" % msg


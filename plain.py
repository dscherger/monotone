#! /usr/bin/env python
"""
Simpler sync for monotone
"""
from optparse import OptionParser
from dumb import Dumbtone
import os.path
import monotone
import sys

ACTIONS = [ "pull", "push", "sync"]

def readConfig(cfgfile):
    cfp = ConfigParser.SafeConfigParser()
    cfp.addSection("default")
    sfp.set("default","verbose","0")
    sfp.set("default","hostKeys","~/.ssh/known_hosts")
    cfp.read(cfgfile)

def getTempDir(database):
    try:
        tmpDir = os.environ['TEMP']
    except KeyError:
        try:
            tmpDir = os.environ['TMP']
        except KeyError:
            tmpDir = "/tmp"
    if database[0].isalpha() and database[1] == ':':
        database = "_" + database[0] + "_" + database[2:]
    database = os.path.normpath(database)
    return os.path.join(tmpDir, database + "-mtndumbtemp")

def getDefaultDatabase():
    dir = "."
    while True:
        optionsFN = os.path.join(dir,"_MTN","options")
        if os.path.exists(optionsFN):
            fh = file(optionsFN,"r")
            try:
                for stanza in monotone.basic_io_parser(fh.read()):
                    for k,v in stanza:
                        if k == "database":
                            return v[0]
            finally:
                fh.close()
        dir = os.path.join("..",dir)
        if not os.path.exists(dir): break
    return None

def getDefaultUrl():
    try:
        fh = file(".mtndumboptions","r")
        try:
            for stanza in monotone.basic_io_parser(fh.read()):
                for k,v in stanza:
                    if k == "repository":
                        return v[0]
        finally:
            fh.close()
    except IOError:
        pass
    return None

def setDefaultUrl(url):
    f = file(".mtndumboptions","w+")
    print >> f, 'repository "%s"' % url
    f.close()
    return

def parseOpt():
    par = OptionParser(usage=
"""%prog [options] pull|push|sync remote-URL

   Pulls|Pushes|Synces with monotone plain instance at remote-URL"
   Where remote-URL is:
      <dirname>
      file://<dirname>
      http[s]://<siteurl>  (pull only)
      sftp://[user[:password]@]address[:port]<siteurl>
         When used with -dsskey or --rsakey the optional password argument 
         is used to decrypt the private key file.
""")
    par.add_option("-d","--db", help="monotone db to use", metavar="STRING")
    par.add_option("-l","--local", help="local transit directory", metavar="PATH")
    par.add_option("--dsskey", 
        help="optional, sftp only. DSS private key file. Can't be specified with --rsakey", metavar="FILE")
    par.add_option("--rsakey",
        help="optional, sftp only. RSA private key file. Can't be specified with --dsskey", metavar="FILE")
    par.add_option("--hostkey", default="~/.ssh/known_hosts",
        help="sftp only. File containing host keys. On unices defaults to %default. Must be specified on Win32.", metavar="FILE")
    par.add_option("--proxy", 
        help="http(s),ftp only. Proxy to use for connecting.", metavar="http://[proxyuser[:proxypwd]@]proxyhost[:proxyport]")
    par.add_option("-v", "--verbose", type="int", default=0,
        help="verbosity level from 0 (normal) to 2 (debug)", metavar="NUM")
    
    (options, args) = par.parse_args()
    if len(args)!=2 or args[0] not in ACTIONS:
        if not len(args):
            par.print_help()
            sys.exit(1)
        elif args[0] not in ACTIONS:
            par.print_help()
            sys.exit("\nERROR: Invalid operation specified\n")
        elif len(args)==1:
            defaultUrl = getDefaultUrl()
            if defaultUrl is None:
                par.print_help()
                sys.exit("\nERROR: Missing remote-URL\n")
            args = [ args[0], defaultUrl ]
        else:
            par.print_help()
            sys.exit("\nERROR: Only one remote-URL allowed\n")

    if options.db is None:
        options.db = getDefaultDatabase()
        if options.db is None:
                sys.exit("\nERROR: monotone db not specified and not in workspace\n")
    if options.local is None:
        import urlparse
        defaultTmpDir = getTempDir(options.db)
        if defaultTmpDir is None:
            sys.exit("\nERROR: local transit directory not specified\n")
        options.local = "file://" + defaultTmpDir
    return (options, args)

def main():
    (options, args) = parseOpt()

    optdict = {"dsskey":options.dsskey,
                   "rsakey":options.rsakey,
                   "hostkey":options.hostkey,
                   "verbose":options.verbose,
                   "proxy":options.proxy}

    mtn = Dumbtone(options.db, options.verbose)
    if args[0]=="pull":
        mtn.do_pull(options.local, args[1], **optdict)
    elif args[0]=="push":
        mtn.do_push(options.local, args[1], **optdict)
    elif args[0]=="sync":
        mtn.do_sync(options.local, args[1], **optdict)
    setDefaultUrl(args[1])

if __name__ == "__main__":
    main()


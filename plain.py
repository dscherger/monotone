#! /usr/bin/env python
#
# This file is part of 'mtnplain'
#
# Copyright (C) Zbigniew Zagorski <zzbigg@o2.pl>
# licensed to the public under the terms of the GNU GPL (>= 2)
# see the file COPYING for details
# I.e., do what you like, but keep copyright and there's NO WARRANTY.
#

"""
Simpler sync for monotone
"""
from optparse import OptionParser
from ConfigParser import SafeConfigParser
from dumb import Dumbtone
import os.path
import monotone
import sys

ACTIONS = [ "pull", "push", "sync"]

    
def readConfig(cfgfile):
    class NoneReturningOptionParser(SafeConfigParser):
        def get(self, section, name):
            if self.has_option(section,name):
                return SafeConfigParser.get(self,section,name)
            else:
                return None
                
        def set(self, section,name, value):
            if not self.has_section(section):
                SafeConfigParser.add_section(self, section)                
            SafeConfigParser.set(self,section,name,value)
            
        def items(self, section):
            if self.has_section(section):
                return SafeConfigParser.items(self,section)
            else:
                return []
    cfg = NoneReturningOptionParser()
    cfg.read(cfgfile)
    return cfg

def getTempDir(database):
    try:
        tmpDir = os.environ['TEMP']
    except KeyError:
        try:
            tmpDir = os.environ['TMP']
        except KeyError:
            tmpDir = "/tmp"
    tmpDir = os.path.normpath(tmpDir)
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

def getDefaultConfigFile():
    try:
        homeDir = os.environ['HOME']
    except KeyError:
        try:
            homeDir = os.environ['USERPROFILE']
        except KeyError:
            homeDir = "."
    return os.path.normpath(os.path.join(homeDir, ".mtnplain"))   
    
def parseOpt():
    
    par = OptionParser(usage=
"""%prog [options] pull|push|sync remote-URL

   Pulls|Pushes|Synces with monotone plain instance at remote-URL"
   Where remote-URL is:
      <dirname>
      file://<dirname>
      http[s]://<siteurl>  (pull only)
      sftp://[user[:password]@]address[:port]<siteurl>
         When used with --dsskey or --rsakey the optional password argument 
         is used to decrypt the private key file.""")
    
    par.add_option("-d","--db", help="monotone db to use", metavar="STRING")
    par.add_option("-l","--local", help="local transit directory", metavar="PATH")
    par.add_option("--dsskey", 
        help="optional, sftp only. DSS private key file. Can't be specified with --rsakey", metavar="FILE")
    par.add_option("--rsakey",
        help="optional, sftp only. RSA private key file. Can't be specified with --dsskey", metavar="FILE")
    par.add_option("--hostkey",
        help="sftp only. File containing host keys. On unices defaults to %default. Must be specified on Win32.", metavar="FILE")
    par.add_option("--proxy", 
        help="http(s),ftp only. Proxy to use for connecting.", metavar="http://[proxyuser[:proxypwd]@]proxyhost[:proxyport]")
    par.add_option("--config", 
        help="config file to use (default %default)", metavar="FILE")
    par.add_option("-s","--storeconfig", 
        help="store 'repository' and 'local' settings in config file (default %default)", action="store_true")
    par.add_option("-v", "--verbose", type="int",
        help="verbosity level from 0 (normal) to 2 (debug)", metavar="NUM")
    
    # defaults are read from config according to selected database, so
    # args must be parsed twice:
    #  - first to determine database and config file
    defaultConfigFile = getDefaultConfigFile()
    hardCodedDefaults = {
        'config': defaultConfigFile,
        'hostkey': "~/.ssh/known_hosts",
        'rsskey': None,
        'dsskey': None,
        'verbose': 0,
        'storeconfig': False
    }
    par.set_defaults(**hardCodedDefaults)
    (options, args) = par.parse_args()
    #
    #  - second to determine rest params and possibly overwrite those set in config
    #
    defaults = {}
    config = readConfig(options.config)
    if options.db is None:
        options.db = getDefaultDatabase()
        defaults['db'] = options.db
        if options.db is None:
            par.error("executed outside workspace and no monotone database specified")        
    defaults.update(config.items("default"))   
    defaults.update(config.items(options.db))
    
    par.set_defaults(**defaults)
    (options, args) = par.parse_args()
    
    if len(args) == 0:
        par.error("no action specified")

    action = args[0]
    url = None
    if action not in ACTIONS:
        par.error("invalid operation specified")
        
    if len(args) == 1:
        url = config.get(options.db, "repository")
        if not url:
            par.error("missing remote-URL")
    elif len(args) == 2:
        url = args[1]
    else:
        par.error("only one remote-URL allowed")
        
    if options.local is None:        
        defaultTmpDir = getTempDir(options.db)
        if defaultTmpDir is None:
            par.error("local transit directory not specified")
        options.local = "file:" + defaultTmpDir
            
    config.set(options.db, "repository", url)
    config.set(options.db, "local", options.local)
    return (options, config, action, url)

def saveConfig(options,config):
    if not options.storeconfig: return
    try:        
        config.write(file(options.config, "w+"))
    except IOError:
        pass

def main():
    (options, config, action, url) = parseOpt()    
    optdict = {"dsskey":options.dsskey,
                   "rsakey":options.rsakey,
                   "hostkey":options.hostkey,
                   "verbose":options.verbose,
                   "proxy":options.proxy}

    mtn = Dumbtone(options.db, options.verbose)
    if action=="pull":
        mtn.do_pull(options.local, url, **optdict)
    elif action=="push":
        mtn.do_push(options.local, url, **optdict)
    elif action=="sync":
        mtn.do_sync(options.local, url, **optdict)
                
    saveConfig(options,config)

if __name__ == "__main__":
    main()


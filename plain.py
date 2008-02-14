#! /usr/bin/env python
#
# This file is part of 'mtndumb'
#
# Copyright (C) Zbigniew Zagorski <zzbigg@o2.pl>
# licensed to the public under the terms of the GNU GPL (>= 2)
# see the file COPYING for details
# I.e., do what you like, but keep copyright and there's NO WARRANTY.
#

"""
Simpler sync for monotone
"""

import sys

import os.path
sys.path.insert(0, os.path.dirname(__file__))

from optparse import OptionParser
from ConfigParser import SafeConfigParser
from dumb import Dumbtone
import os.path
import monotone
import merkle_dir
import sys

ACTIONS = [ "pull", "push", "sync", "clone" ]

    
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

def getWorkspaceRoot():
    dir = "."
    while True:
        bookkeepingDir = os.path.join(dir,"_MTN")
        if os.path.isdir(bookkeepingDir):
            return os.path.abspath(dir)

        dir = os.path.join("..",dir)
        if not os.path.exists(dir): break
    return None
    
def getDefaultDatabase():
    workspaceRoot = getWorkspaceRoot()
    if not workspaceRoot: return None
    
    optionsFN = os.path.join(workspaceRoot,"_MTN","options")
    if os.path.exists(optionsFN):
        fh = file(optionsFN,"r")
        try:
            for stanza in monotone.basic_io_parser(fh.read()):
                for k,v in stanza:
                    if k == "database": 
                        return v[0]
        finally:
            fh.close()
    return None

def getWorkspaceBranch():
    workspaceRoot = getWorkspaceRoot()
    if not workspaceRoot: return None
    
    optionsFN = os.path.join(workspaceRoot,"_MTN","options")
    if os.path.exists(optionsFN):
        fh = file(optionsFN,"r")
        try:
            for stanza in monotone.basic_io_parser(fh.read()):
                for k,v in stanza:
                    if k == "branch": 
                        return v[0]
        finally:
            fh.close()
    return None
def getDefaultConfigFile():
    try:
        homeDir = os.environ['HOME']
    except KeyError:
        try:
            homeDir = os.environ['USERPROFILE']
        except KeyError:
            homeDir = "."
    return os.path.normpath(os.path.join(homeDir, ".mtndumb"))   
    
def print_prefixed(where, prefix, message):
    prefix_str = ""
    if prefix:
        prefix_str = prefix + ": "
    
    for line in [ line.strip() for line in message.splitlines() if line ]:
        print >> where, "%s: %s%s" % ("mtndumb", prefix_str, line)
        
def informative(message):
    print_prefixed(sys.stdout, None, message)
    
def verbose(message):
    print_prefixed(sys.stdout, None, message)

def warning(message):
    print_prefixed(sys.stderr, "warning", message)

def error_and_exit(message, error_code=1):
    print_prefixed(sys.stderr, "error", message)
    sys.exit(error_code)
    
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
    
    par.add_option("-b","--workspace-branch", 
        help="use workspace branch as push pattern", action="store_true")   
    par.add_option("-d","--db", help="monotone db to use", metavar="STRING")
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
        'storeconfig': False,
        'workspace_branch': False
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
        
    branch_pattern = None
    if options.workspace_branch:
        branch_pattern = getWorkspaceBranch()
        if branch_pattern is None:
            par.error("unable to find branch of workspace, are you in monotone workspace")
    if len(args) == 1:
        url = config.get(options.db, "repository")
        if not url:
            par.error("missing remote-URL")
    elif len(args) == 2:
        url = args[1]
    elif len(args) == 3:
        url = args[1]
        branch_pattern = args[2]
    else:
        par.error("only one remote-URL allowed")    
        
    config.set(options.db, "repository", url)
    
    workspaceRoot = getWorkspaceRoot()
    if branch_pattern:
        config.set(workspaceRoot, "branch_pattern", branch_pattern)
    else:        
        branch_pattern = config.get(workspaceRoot, "branch_pattern")

    informative("performing %s of %s with %s" % (action, options.db, url))
    if branch_pattern:
        informative("using branch pattern: %s" %  branch_pattern) 

    return (options, config, action, url, branch_pattern)

def saveConfig(options,config):
    if not options.storeconfig: return
    try:        
        config.write(file(options.config, "w+"))
    except IOError,e:
        warning("can't store configuration in %s: %s" % (options.config, e))

def main():
    (options, config, action, url, branch_pattern) = parseOpt()    
    optdict = {"dsskey":options.dsskey,
                   "rsakey":options.rsakey,
                   "hostkey":options.hostkey,
                   "verbose":options.verbose,
                   "proxy":options.proxy}

    mtn = Dumbtone(options.db, options.verbose)
    
    try:
        if action=="pull":
            mtn.do_pull(url, branch_pattern, **optdict)
        elif action=="push":
            mtn.do_push(url, branch_pattern, **optdict)
        elif action=="sync":
            mtn.do_sync(url, branch_pattern, **optdict)
        elif action=="clone":
            mtn.monotone.ensure_db()
            mtn.do_pull(url, branch_pattern, **optdict)	
    except monotone.MonotoneError,e:
        error_and_exit(str(e))
    except merkle_dir.LockError:
        error_and_exit("repository '%s' is locked, please try again later" % url, 2)
    saveConfig(options,config)

if __name__ == "__main__":
    main()


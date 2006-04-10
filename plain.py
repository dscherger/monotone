"""
Simpler sync for monotone
"""
from optparse import OptionParser
from dumb import Dumbtone
import sys

ACTIONS = [ "pull", "push", "sync"]

def readConfig(cfgfile):
    cfp = ConfigParser.SafeConfigParser()
    cfp.addSection("default")
    sfp.set("default","verbose","0")
    sfp.set("default","hostKeys","~/.ssh/known_hosts")
    cfp.read(cfgfile)

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
    par.add_option("-v", "--verbose", type="int", default=0,
        help="verbosity level from 0 (normal) to 2 (debug)", metavar="NUM")
    
    (options, args) = par.parse_args()
    if len(args)!=2 or args[0] not in ACTIONS:
        par.print_help()
        if not len(args):
            sys.exit(1)
        elif args[0] not in ACTIONS:
            sys.exit("\nERROR: Invalid operation specified\n")
        elif len(args)==1:
            sys.exit("\nERROR: Missing remote-URL\n")
        else:
            sys.exit("\nERROR: Only one remote-URL allowed\n")

    if options.db is None:
        sys.exit("\nERROR: monotone db not specified\n")
    elif options.local is None:
        sys.exit("\nERROR: local transit directory not specified\n")
    return (options, args)

if __name__ == "__main__":
    (options, args) = parseOpt()

    optdict = {"dsskey":options.dsskey,
                   "rsakey":options.rsakey,
                   "hostkey":options.hostkey,
                   "verbose":options.verbose}

    mtn = Dumbtone(options.db, options.verbose)
    if args[0]=="pull":
        mtn.do_pull(options.local, args[1], **optdict)
    elif args[0]=="push":
        mtn.do_push(options.local, args[1], **optdict)
    elif args[0]=="sync":
        mtn.do_sync(options.local, args[1], **optdict)

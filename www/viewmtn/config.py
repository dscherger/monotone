#
# config.py
#
# This python script is imported by ViewMTN
# Note that changes to the script may not be 
# noticed immediately by ViewMTN, as mod_python 
# caches imported Python modules.
#
# If config changes are not picked up, reloading
# the web server should solve the issue.
#
# If you want to run multiple viewmtn installs from
# a single apache server, you might want to look at
# giving them seperate python interpreter instances,
# ie set PythonInterpreter viewmtn1, viewmtn2 etc
# in .htaccess.

import sys
import os

# the base URL of this install 'http://%s%s/' % (req.hostname, os.path.dirname(req.uri))#
def base_url(uri):
	d = os.path.dirname(os.path.dirname(uri))
	return 'http://localhost/~timothy/host/projects/%s/viewmtn/' % os.path.basename(d)

# the path to the 'monotone' binary
monotone = '/home/timothy/bin/monotone'

# the monotone database to be shared out
# obviously, everything in this database might 
# become public if something goes wrong; probably 
# a good idea not to leave your private key in it
def dbfile(uri):
	d = os.path.dirname(os.path.dirname(uri))
	return '/home/timothy/experiment/projects/%s/database.viewmtn' % os.path.basename(d)

# where to find GNOME icons (used in manifest listing)
gnome_mimetype_icon_path = '/usr/share/icons/gnome/'

# and where they are on the web
gnome_mimetype_uri = 'mimetypes/'

# where to find GNU enscript
enscript_path = '/usr/bin/enscript'

graphopts = {
	# a directory (must be writable by the web user)
	# in which viewmtn can output graph files
	# (you should set up a cronjob to delete old ones
	#  periodically)
	'directory' : '/var/viewmtn-graphs',

	# a URL, relative or absolute, at which the files 
	# in the 'graphdir' directory can be found. Should 
	# end in a '/' character
	'uri' : 'graph/',

	# the path to the 'dot' program
	'dot' : '/usr/bin/dot',

	# options to use for nodes in the dot input file
	# we generate.
	'nodeopts' : { 'fontname'  : 'Windsor', 
		       'fontsize'  : '8',
		       'shape'     : 'box',
		       'height'    : '0.3',
		       'spline'    : 'true',
		       'style'     : 'filled',
		       'fillcolor' : '#dddddd' }
}



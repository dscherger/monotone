#
# This file is part of 'mtnplain'
#
# Copyright (C) Zbigniew Zagorski <zzbigg@o2.pl> and others, 
# licensed to the public under the terms of the GNU GPL (>= 2)
# see the file COPYING for details
# I.e., do what you like, but keep copyright and there's NO WARRANTY.
#

# urlgrabber distutils setup
import re as _re

name = "plain"
description = """The monotone plain database synchronizer"""

long_description = """The monotone plain (or "dumb") protocol allows
two-way synchronization via sftp or plain files,
and one-way (pull) via http(s) and ftp."""

license = "LGPL"
version = "0.0.1"
_authors = [ 'Nathaniel Smith',
             'Zbynek Winkler',
             'Riccardo "rghetta"',
             'Zbyszek Zagorski' ]

author       = ', '.join(_authors)
url = 'http://www.venge.net/monotone'

packages = ['monotone_plain', 'monotone_plain.dws','monotone_plain.paramiko']
package_dir = {
    'monotone_plain' : '.',
    'monotone_plain.dws' : 'dws',
    'monotone_plain.paramiko' : 'paramiko'
}
package_data = {
    'mtn' : ['__init__.py']
}

scripts = ['mtnplain']
options = { 'clean' : { 'all' : 1 } }
classifiers = [
        'Development Status :: 4 - Beta',
        'Topic :: Software Development :: Libraries :: Python Modules'
      ]

# load up distutils
if __name__ == '__main__':
  config = globals().copy()
  keys = config.keys()
  for k in keys:
    if k.startswith('_'): del config[k]

  from distutils.core import setup
  setup(**config)

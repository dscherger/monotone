#
# This file is part of 'mtndumb'
#
# Copyright (C) Zbigniew Zagorski <zzbigg@o2.pl> and others,
# licensed to the public under the terms of the GNU GPL (>= 2)
# see the file COPYING for details
# I.e., do what you like, but keep copyright and there's NO WARRANTY.
#

import re as _re

name = "mtndumb"
description = """The monotone dumb database synchronizer"""

long_description = """The monotone dumb (aka 'plain') protocol allows
two-way synchronization via sftp or plain files,
and one-way (pull) via http(s) and ftp."""

license = "LGPL"
version = "0.0.5"
_authors = [ 'Nathaniel Smith',
             'Zbynek Winkler',
             'Riccardo "rghetta"',
             'Zbyszek Zagorski' ]

author       = ', '.join(_authors)
url = 'http://www.venge.net/monotone'

packages = ['monotone_dumb', 'monotone_dumb.dws','monotone_dumb.paramiko']
package_dir = {
    'monotone_dumb' : '.',
    'monotone_dumb.dws' : 'dws',
    'monotone_dumb.paramiko' : 'paramiko'
}

scripts = ['mtndumb']
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

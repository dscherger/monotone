#!/usr/bin/env python

import sys
sys.exit("Polytony isn't ready to be installed.  Just run \"python run.py\".")

##################################################################

from distutils.core import setup
import os

def accumulate_packages(packages, dir, files):

    if "__init__.py" in files:
        # We found a package directory.
        package = dir.replace(os.sep, ".")
        if os.altsep:
            package = pkg.replace(os.altsep, ".")
        packages.append(package)

packages = []
os.path.walk("polytony", accumulate_packages, packages)

import polytony

setup(name="polytony",
      description="Experimental history browser for Monotone.",
      author="Nathaniel Smith",
      author_email="njs@pobox.com",
      version=polytony.version,
      packages=packages)

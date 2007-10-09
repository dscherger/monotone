#
# This file is part of 'mtndumb'
#
# Copyright (C) Zbigniew Zagorski <zzbigg@o2.pl>
# licensed to the public under the terms of the GNU GPL (>= 2)
# see the file COPYING for details
# I.e., do what you like, but keep copyright and there's NO WARRANTY.
#
PYTHON ?= python
default: install

install:
	$(PYTHON) setup.py install

dist:
	$(PYTHON) setup.py sdist --formats zip,gztar

.PHONY: dist

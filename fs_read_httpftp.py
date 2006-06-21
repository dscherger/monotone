#
# This file is part of 'mtnplain'
#
# Copyright (C) Nathaniel Smith <njs@pobox.com> and others, 
# licensed to the public under the terms of the GNU GPL (>= 2)
# see the file COPYING for details
# I.e., do what you like, but keep copyright and there's NO WARRANTY. 
#

# we need urlgrabber to do range fetches
import fs
import urlparse
import urlgrabber
import urlgrabber.grabber

class HTTPFTPReadableFS(fs.ReadableFS):
    def __init__(self, url, proxy):
        self.url = url
        assert self.url
        if self.url[-1] != "/":
            self.url += "/"
        if proxy:
            self.proxies={ "http" : proxy, "https" : proxy, "ftp":proxy}
        else:
            self.proxies = None
        self.urlgrabber = urlgrabber.grabber.URLGrabber(proxies = self.proxies)

    def _url(self, filename):
        return urlparse.urljoin(self.url, filename)

    def open_read(self, filename):
        return self.urlgrabber.urlopen(self._url(filename))

    def fetch(self, filenames):
        files = {}
        for fn in filenames:
            try:
                files[fn] = self.urlgrabber.urlread(self._url(fn))
            except urlgrabber.grabber.URLGrabError:
                files[fn] = None
        return files

    def _real_fetch_bytes(self, filename, bytes):
        url = self._url(filename)
        for offset, length in bytes:
            # for HTTP, this is actually somewhat inefficient
            yield ((offset, length),
                   self.urlgrabber.urlread(url, range=(offset, offset+length)))


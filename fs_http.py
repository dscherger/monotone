# we need urlgrabber to do range fetches
import fs
import urlparse
import urlgrabber

class HTTPReadableFS(fs.ReadableFS):
    def __init__(self, url):
        self.url = url

    def _url(self, filename):
        return urlparse.urljoin(self.url, filename)

    def open_read(self, filename):
        return urlgrabber.urlopen(self._url(filename))

    def fetch(self, filenames):
        files = {}
        for fn in filenames:
            try:
                files[fn] = urlgrabber.urlread(self._url(fn))
            except urlgrabber.URLGrabError:
                files[fn] = None
        return files

    def _real_fetch_bytes(self, filename, bytes):
        url = self._url(filename)
        for offset, length in bytes:
            yield urlgrabber.urlread(url, range=(offset, offset+length))


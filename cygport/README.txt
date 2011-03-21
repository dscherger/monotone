Instructions for building the Cygwin package.

We use the cygport Cygwin package.

$ cd monotone/cygport
$ mtn mv monotone-$OLD-1.cygport monotone-$NEW-1.cygport
$ mtn mv monotone-$OLD-1.cygwin.patch monotone-$NEW-1.cygwin.patch

    edit *.cygport and/or *.patch as needed
    add the new version to Port Notes in *.patch

$ cd ../monotone-build_cygwin; make dist-gzip
$ cp ../monotone-build_cygwin/monotone-$NEW.tar.gz .
$ cygport monotone-$NEW-1 all
$ cp monotone-$NEW-1/spkg/monotone-$NEW-1.cygwin.patch .

If cygport hangs, it may be due to a parallel make bug. Try the
following:

echo "export MAKEOPTS=-j1" >> ~/.cygport.conf

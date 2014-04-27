#!/usr/bin/env perl
use strict;
use warnings;

# colortone.pl - colors monotone output such as status, log and diff
#
# Reads input from STDIN and adds ANSI escape codes before outputting
# to STDOUT. Passing filenames on the command line is also supported.
#
# Windows systems need Win32::Console::ANSI installed to support showing
# color within cmd.exe.
#
# Usage
# mtn diff | colortone.pl
# mtn diff > diff.txt && colortone.pl diff.txt
#
# Windows Usage
# To make it easier to use, create a new file 'myname.bat' with the
# following contents (keeping quotes).
#
# @"C:\Program Files\monotone\mtn.exe" %* | perl "C:\Full\Path\To\colortone.pl"
#
# This allows you to use 'myname' as a replacement for mtn and output will
# be colored automatically. Only use it if your key passphrase is not
# required: diff, status, and log are fine.
#
# The piping has to be done through perl manually due to
# http://support.microsoft.com/kb/321788.
#
# Copyright 2011 Richard Hopkins
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

use Term::ANSIColor;

if ( $^O eq 'MSWin32' ) {
    require Win32::Console::ANSI;
    import  Win32::Console::ANSI;
}

# pattern => attributes
#
# pattern should contain a grouping as only the group will be colored.
#
# attributes is a single string containing any attributes supported by
# Term::ANSIColor separated with a space.
# eg. 'red on_white bold'
my %patterns = (
    q/(^#.*)/             => 'yellow',          # diff header
    q/(^@@.*)/            => 'magenta',         # diff encloser
    q/(^\+.*)/            => 'green',           # diff added
    q/(^\|\s+\+.*)/       => 'green',           # diff added (with graph)
    q/(^-.*)/             => 'red',             # diff removed
    q/(^\|\s+-.*)/        => 'red',             # diff removed (with graph)
    q/([a-f0-9]{40})/     => 'magenta bold',    # revision or file id
    q/(^\| +\w+:)/        => 'white bold',      # log header (with graph)
    q/(^\w+:)/            => 'white bold',      # status header
    q/(^\*\*\*.+\*\*\*$)/ => 'yellow bold',     # status warning
    q/(  added)/          => 'green',           # status added
    q/(  patched)/        => 'cyan bold',       # status patched
    q/(  dropped)/        => 'red',             # status dropped
    q/(  renamed)/        => 'blue bold',       # status renamed
);

my $reset = color('reset');

while (<>) {

    # colorize any matching patterns in the current line.
    foreach my $pattern ( keys %patterns ) {
        my $color = color( $patterns{$pattern} );
        $_ =~ s/$pattern/$color$1$reset/;
    }

    print colored $_;
}

exit(0);

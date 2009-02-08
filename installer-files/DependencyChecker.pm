##############################################################################
#
#   File Name    - DependencyChecker.pm
#
#   Description  - Mmodule that provides a simple module dependency checker.
#                  The idea was taken from ExtUtils::MakeMaker. I would have
#                  used the ExtUtils::Installed module but this does not seem
#                  to search for site specific module directories specified in
#                  PERLLIB.
#
#   Author       - A.E.Cooper.
#
#   Legal Stuff  - Copyright (c) 2009 Anthony Edward Cooper
#                  <aecooper@coosoft.plus.com>.
#
#                  This library is free software; you can redistribute it
#                  and/or modify it under the terms of the GNU Lesser General
#                  Public License as published by the Free Software
#                  Foundation; either version 3 of the License, or (at your
#                  option) any later version.
#
#                  This library is distributed in the hope that it will be
#                  useful, but WITHOUT ANY WARRANTY; without even the implied
#                  warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
#                  PURPOSE. See the GNU Lesser General Public License for
#                  more details.
#
#                  You should have received a copy of the GNU Lesser General
#                  Public License along with this library; if not, write to
#                  the Free Software Foundation, Inc., 59 Temple Place - Suite
#                  330, Boston, MA 02111-1307 USA.
#
##############################################################################
#
##############################################################################
#
#   Package      - DependencyChecker
#
#   Description  - See above.
#
##############################################################################



# ***** PACKAGE DECLARATION *****

package DependencyChecker;

# ***** DIRECTIVES *****

require 5.008005;

use strict;
use warnings;

# ***** FUNCTIONAL PROTOTYPES *****

# Public routines.

sub check($);

# ***** PACKAGE INFORMATION *****

use base qw(Exporter);

our @EXPORT = qw();
our @EXPORT_OK = qw();
our $VERSION = 0.1;
#
##############################################################################
#
#   Routine      - check
#
#   Description  - Checks that the specified Perl package dependencies are
#                  met.
#
#   Data         - $dependencies : A reference to a hash containing the
#                                  dependencies, where the key is the package
#                                  name and the value is the required version
#                                  number. If the version number is zero then
#                                  only the presence of the package is tested.
#                  Return Value  : True if the dependencies are met, otherwise
#                                  false if they are not.
#
##############################################################################



sub check($)
{

    my $dependencies = $_[0];

    my $met = 1;

    foreach my $dep (sort(keys(%$dependencies)))
    {

        # Attempt to eval the package in. Perl 5.8.0 has a bug with require
	# Foo::Bar alone in an eval, so an extra statement is a workaround.

        eval "require $dep; 0";

	# What was the outcome?

	if ($@ ne "")
	{

	    # Not installed.

	    printf(STDERR "Warning: prerequisite %s %s not found.\n",
		   $dep,
		   $dependencies->{$dep});
	    $met = undef;

	}
	elsif ($dependencies->{$dep} > 0)
	{

	    my $version;

	    # Installed, and we need to check the version number.

	    # Get the version number, converting X.Y_Z alpha version #s to X.YZ
	    # for easier comparisons.

	    $version = defined($dep->VERSION) ? $dep->VERSION : 0;
	    $version =~ s/(\d+)\.(\d+)_(\d+)/$1.$2$3/;

	    # Now check the version numbers.

	    if ($version < $dependencies->{$dep})
	    {
		printf(STDERR
		       "Warning: prerequisite %s %s not found. We have %s.\n",
		       $dep,
		       $dependencies->{$dep},
		       $version);
		$met = undef;
	    }

	}

    }

    return $met;

}

1;

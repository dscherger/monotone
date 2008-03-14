##############################################################################
#
#   File Name    - Completion.pm
#
#   Description  - Class module that provides a basic auto-completion
#                  mechanism.
#
#   Author       - A.E.Cooper.
#
#   Legal Stuff  - Copyright (c) 2007 Anthony Edward Cooper
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
#   Package      - Completion
#
#   Description  - A class for using Monotone's automate stdio interface.
#
##############################################################################



# ***** PACKAGE DECLARATION *****

package Completion;

# ***** DIRECTIVES *****

require 5.008;

use strict;
use integer;

# ***** PACKAGE INFORMATION *****

# We are just a base class.

use base qw(Exporter);

our @EXPORT = qw();
our @EXPORT_OK = qw();
our $VERSION = 0.1;

# ***** FUNCTIONAL PROTOTYPES FOR THIS FILE *****

# Public methods.

sub get_completion($$$$);
sub new($@);
#
##############################################################################
#
#   Routine      - new
#
#   Description  - Class constructor.
#
#   Data         - $class       : Either the name of the class that is to be
#                                 created or an object of that class.
#                  $list        : A reference to a list of all possible
#                                 completions.
#                  Return Value : A reference to the newly created object.
#
##############################################################################



sub new($@)
{

    my ($class, $list) = @_;
    $class = ref($class) if ref($class);

    my($char,
       $item,
       $level,
       $this);

    $this = {};
    $this->{tree} = {};

    # Build up a hash tree for the list of possible items.

    foreach $item (@$list)
    {

	# Build up nodes for an item.

	$level = $this->{tree};
	foreach $char (split(//o, $item))
	{
	    if (! exists($level->{$char}))
	    {
		$level->{$char} = {};
	    }
	    $level = $level->{$char};
	}

	# By adding this dummy node here it stops the auto-complete moving too
	# far should another item extend beyond this point. I.e. auto
	# completion stops at `net.venge.monotone.contrib' and not
	# `net.venge.monotone.contrib.'. You could simply think of this node as
	# an `end of string' token if you prefer.

	$level->{""} = "";

    }

    return bless($this, $class);

}
#
##############################################################################
#
#   Routine      - get_completion
#
#   Description  - Given a value, work out the largest unique match.
#
#   Data         - $this        : The object.
#                  $value       : The value to be completed.
#                  $result      : A reference to a buffer that is to contain
#                                 the result.
#                  $complete    : A reference to a boolean that is to contain
#                                 a `result is complete' indicator.
#                  Return Value : True if $value was expanded, otherwise false
#                                 if $value had to be truncated due to no
#                                 match (the maximum valid completion is still
#                                 returned in $result).
#
##############################################################################



sub get_completion($$$$)
{

    my($this, $value, $result, $complete) = @_;

    my($char,
       $level);

    # Lookup value, stopping when it becomes ambiguous or we get to the end of
    # $value.

    $level = $this->{tree};
    $$result = "";
    foreach $char (split(//o, $value))
    {
	last unless (exists($level->{$char}));
	$level = $level->{$char};
	$$result .= $char;
    }

    # Detect truncations.

    return if (length($value) > length($$result));

    # Now try and expand it further.

    while (defined(%$level) && keys(%$level) == 1)
    {
	($char) = keys(%$level);
	$$result .= $char;
	$level = $level->{$char};
    }

    # Detect complete completions (doesn't mean to say that it can't be
    # extended, just that as it stands at the moment $$result does contain a
    # valid unique value).

    if (! defined(%$level) || exists($level->{""}))
    {
	$$complete = 1;
    }
    else
    {
	$$complete = 0;
    }

    return 1;

}

1;

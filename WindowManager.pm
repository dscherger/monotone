##############################################################################
#
#   File Name    - WindowManager.pm
#
#   Description  - Class module that provides a class for managing application
#                  window, i.e. their recycling, busy cursor management etc.
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
#   Package      - WindowManager
#
#   Description  - See above.
#
##############################################################################



# ***** PACKAGE DECLARATION *****

package WindowManager;

# ***** DIRECTIVES *****

require 5.008;

use strict;
use integer;

# ***** REQUIRED PACKAGES *****

# Standard Perl and CPAN modules.

use Carp;
use Gtk2;

# ***** GLOBAL DATA DECLARATIONS *****

# The singleton object.

my $singleton;

# ***** PACKAGE INFORMATION *****

# We are just a base class.

use base qw(Exporter);

our @EXPORT = qw();
our @EXPORT_OK = qw();
our $VERSION = 0.1;

# ***** FUNCTIONAL PROTOTYPES FOR THIS FILE *****

# Public methods.

sub add_busy_widgets($$@);
sub cleanup($);
sub cond_find($$$);
sub find_unused($$);
sub instance($);
sub make_busy($$$);
sub manage($$$;$);
#
##############################################################################
#
#   Routine      - instance
#
#   Description  - Class constructor and accessor for a singleton class.
#
#   Data         - $class       : Either the name of the class that is to be
#                                 created or an object of that class.
#                  Return Value : A reference to the WindowManager object,
#                                 which may have been created.
#
##############################################################################



sub instance($)
{

    my $class = ref($_[0]) ? ref($_[0]) : $_[0];

    if (! defined($singleton))
    {
	$singleton = {};
	$singleton->{windows} = [];
	$singleton->{busy_cursor} = undef;
	$singleton->{grab_widget_stack} = [];
	return bless($singleton, $class);

    }
    else
    {
	return $singleton;
    }

}
#
##############################################################################
#
#   Routine      - cleanup
#
#   Description  - Cleans up all resources managed by this object.
#
#   Data         - $this : The object.
#
##############################################################################



sub cleanup($)
{

    my $this = $_[0];

    # Destroy all the window widgets and thereby all their children.

    foreach my $window (@{$this->{windows}})
    {
	$window->{window}->destroy();
    }

    # Free up everything used by this object and associated window instance
    # records.

    $this->{windows} = [];
    $this->{busy_cursor} = undef;
    $this->{grab_widget_stack} = [];
    $singleton = undef;

}
#
##############################################################################
#
#   Routine      - manage
#
#   Description  - Take the given window instance record and start managing
#                  it. Please note that it is expected that the top level
#                  window widget will be in a field called `window'.
#
#   Data         - $this        : The object.
#                  $instance    : A reference to the window instance record
#                                 that is to be managed.
#                  $type        : The type of window that is to be managed.
#                  $grab_widget : The widget that should be used with an input
#                                 grab when making the window busy. This is
#                                 optional but if not present then the
#                                 instance record is expected to contain an
#                                 `appbar' widget field.
#
##############################################################################



sub manage($$$;$)
{

    my($this, $instance, $type, $grab_widget) = @_;

    # Check for instance record compliance.

    croak(__("No window field found")) unless (exists($instance->{window}));
    croak(__("No appbar field found"))
	unless (exists($instance->{appbar}) || defined($grab_widget));
    foreach my $field ("busy_widgets", "grab_widget", "type")
    {
	croak(__x("{field} field found - I manage this", field => $field))
	    if (exists($instance->{$field}));
    }
    croak(__("Cannot manage unrealised windows"))
	unless(defined($instance->{window}->window()));

    # Ok so store what we need in the instance record.

    $instance->{type} = $type;
    $instance->{busy_widgets} = [$instance->{window}->window()];
    $instance->{grab_widget} =
	defined($grab_widget) ? $grab_widget : $instance->{appbar};

    # Store the instance record in our window list.

    push(@{$this->{windows}}, $instance);

}
#
##############################################################################
#
#   Routine      - add_busy_widgets
#
#   Description  - Add the specified additional widgets for busy cursor
#                  handling.
#
#   Data         - $this     : The object.
#                  $instance : A reference to the window instance record that
#                              is to be updated.
#                  @widgets  : The list of additional widgets that are to be
#                              handled.
#
##############################################################################



sub add_busy_widgets($$@)
{

    my($this, $instance, @widgets) = @_;

    push(@{$instance->{busy_widgets}}, @widgets);

}
#
##############################################################################
#
#   Routine      - find_unused
#
#   Description  - Try and find an unused window of the specified type in the
#                  list of managed window instance records.
#
#   Data         - $this        : The object.
#                  $type        : The type of window that is to be found.
#                  Return Value : A reference to the found window instance
#                                 record on success, otherwise undef on
#                                 failure.
#
##############################################################################



sub find_unused($$)
{

    my($this, $type) = @_;

    foreach my $window (@{$this->{windows}})
    {
	return $window
	    if ($window->{type} eq $type && ! $window->{window}->mapped());
    }

    return;

}
#
##############################################################################
#
#   Routine      - cond_find
#
#   Description  - Try and find a window of the specified type that also
#                  satisfies the conditions laid down in the specified
#                  predicate routine.
#
#   Data         - $this        : The object.
#                  $type        : The type of window that is to be found. If
#                                 this is undef then the predicate routine is
#                                 called for all window instance records.
#                  $predicate   : A reference to the predicate routine that is
#                                 to be called against every window of the
#                                 specified type. The one argument to this
#                                 routine is a window instance record.
#                  Return Value : A reference to the found window instance
#                                 record on success, otherwise undef on
#                                 failure.
#
##############################################################################



sub cond_find($$$)
{

    my($this, $type, $predicate) = @_;

    foreach my $window (@{$this->{windows}})
    {
	return $window if ((! defined ($type) || $window->{type} eq $type)
			   && &$predicate($window));
    }

    return;

}
#
##############################################################################
#
#   Routine      - make_busy
#
#   Description  - Make all managed windows either busy or active.
#
#   Data         - $this     : The object.
#                  $instance : The window instance that is to have the input
#                              grab (this is invariably the window that is
#                              busy processing something).
#                  $busy     : True if the window is to be made busy,
#                              otherwise false if the window is to be made
#                              active.
#
##############################################################################



sub make_busy($$$)
{

    my($this, $instance, $busy) = @_;

    croak(__("Called with an unmanaged instance record"))
	unless (exists($instance->{grab_widget}));

    # Create and store the cursors if we haven't done so already.

    $this->{busy_cursor} = Gtk2::Gdk::Cursor->new("watch")
	unless (defined($this->{busy_cursor}));

    # Do it. Make the grab widget, usually the window's application bar,  grab
    # the input when the window is busy, that way we gobble up keyboard and
    # mouse events that could muck up the application state.

    if ($busy)
    {
	Gtk2->grab_add($instance->{grab_widget});
	foreach my $win_instance (@{$this->{windows}})
	{
	    foreach my $window (@{$win_instance->{busy_widgets}})
	    {
		$window->set_cursor($this->{busy_cursor});
	    }
	}
	push(@{$this->{grab_widget_stack}}, $instance->{grab_widget});
    }
    else
    {
	my $grab_widget;
	if (defined($grab_widget = pop(@{$this->{grab_widget_stack}})))
	{
	    Gtk2->grab_remove($grab_widget);
	    if ($#{$this->{grab_widget_stack}} < 0)
	    {
		foreach my $win_instance (@{$this->{windows}})
		{
		    foreach my $window (@{$win_instance->{busy_widgets}})
		    {
			$window->set_cursor(undef);
		    }
		}
	    }
	    else
	    {
		Gtk2->grab_add($this->{grab_widget_stack}->
			       [$#{$this->{grab_widget_stack}}]);
	    }
	}
    }

}

1;

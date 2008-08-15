##############################################################################
#
#   File Name    - WindowManager.pm
#
#   Description  - Class module that provides a class for managing application
#                  windows, i.e. their recycling, busy cursor management etc.
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

use integer;
use strict;
use warnings;

# ***** REQUIRED PACKAGES *****

# Standard Perl and CPAN modules.

use Carp;
use Glib;
use Gtk2;

# ***** GLOBAL DATA DECLARATIONS *****

# A hash of event types that are to be filtered out when updating a busy GUI.

my %filtered_events = ("2button-press"  => 1,
		       "3button-press"  => 1,
                       "button-press"   => 1,
		       "button-release" => 1,
		       "delete"         => 1,
		       "key-press"      => 1,
		       "key-release"    => 1,
		       "motion-notify"  => 1,
		       "scroll"         => 1);

# The singleton object.

my $singleton;

# ***** FUNCTIONAL PROTOTYPES *****

# Public methods.

sub add_busy_windows($$@);
sub allow_input($&);
sub cleanup($);
sub cond_find($$&);
sub find_unused($$);
sub instance($);
sub make_busy($$$;$);
sub manage($$$$;$);
sub reset_state($);
sub update_gui();

# Private routines.

sub event_filter($$);

# ***** PACKAGE INFORMATION *****

# We are just a base class.

use base qw(Exporter);

our @EXPORT = qw();
our @EXPORT_OK = qw();
our $VERSION = 0.1;
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

    my $class = (ref($_[0]) ne "") ? ref($_[0]) : $_[0];

    if (! defined($singleton))
    {
	$singleton = {windows     => [],
		      busy_cursor => Gtk2::Gdk::Cursor->new("watch"),
		      state_stack => [],
		      allow_input => 0};
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
#                  $window      : The Gtk2::Window object for the window that
#                                 is to be managed.
#                  $grab_widget : The widget that is to still remain
#                                 responsive when making the window busy, most
#                                 typically this will be a `stop' button. This
#                                 is optional.
#
##############################################################################



sub manage($$$$;$)
{

    my($this, $instance, $type, $window, $grab_widget) = @_;

    # Simply store the details in our window list.

    push(@{$this->{windows}},
	 {instance     => $instance,
	  type         => $type,
	  window       => $window,
	  busy_windows => [$window->window()],
	  grab_widget  => $grab_widget});

}
#
##############################################################################
#
#   Routine      - add_busy_windows
#
#   Description  - Add the specified additional windows for busy cursor
#                  handling.
#
#   Data         - $this     : The object.
#                  $instance : A reference to the window instance record that
#                              is to be updated.
#                  @windows  : The list of additional Gtk2::Gdk::Window
#                              objects that are to be handled.
#
##############################################################################



sub add_busy_windows($$@)
{

    my($this, $instance, @windows) = @_;

    my $entry;

    # Find the relevant entry for this instance.

    foreach my $win_instance (@{$this->{windows}})
    {
	if ($win_instance->{instance} == $instance)
	{
	    $entry = $win_instance;
	    last;
	}
    }
    croak(__("Called with an unmanaged instance record"))
	unless (defined($entry));

    push(@{$entry->{busy_windows}}, @windows);

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
	return $window->{instance}
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



sub cond_find($$&)
{

    my($this, $type, $predicate) = @_;

    foreach my $window (@{$this->{windows}})
    {
	return $window->{instance}
	    if ((! defined($type) || $window->{type} eq $type)
		&& &$predicate($window->{instance}));
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
#                  $exclude  : True if the window referenced by $instance is
#                              to be excluded from the list of windows that
#                              are to be made busy (this is used by modal
#                              dialogs).
#
##############################################################################



sub make_busy($$$;$)
{

    my($this, $instance, $busy, $exclude) = @_;

    my($entry,
       $found,
       $head,
       @list);

    $exclude = defined($exclude) ? $exclude : 0;

    # Find the relevant entry for this instance.

    foreach my $win_instance (@{$this->{windows}})
    {
	if ($win_instance->{instance} == $instance)
	{
	    $entry = $win_instance;
	    last;
	}
    }
    croak(__("Called with an unmanaged instance record"))
	unless (defined($entry));

    # When making things busy filter out keyboard and mouse button events
    # unless they relate to the grab widget (usually a `stop' button) and make
    # the mouse cursor busy. Making things unbusy is simply the reverse. Also
    # cope with nested calls.

    if ($busy)
    {
	Gtk2::Gdk::Event->handler_set(\&event_filter,
				      {singleton   => $this,
				       grab_widget => $entry->{grab_widget}})
	    if (! $exclude);
	foreach my $win_instance (@{$this->{windows}})
	{
	    if (! $exclude || $win_instance->{instance} != $instance)
	    {
		foreach my $window (@{$win_instance->{busy_windows}})
		{
		    if ($window->is_visible())
		    {
			$window->set_cursor($this->{busy_cursor});
			push(@list, $window);
		    }
		}
	    }
	}
	push(@{$this->{state_stack}},
	     {exclude     => $exclude,
	      grab_widget => $instance->{grab_widget},
	      window_list => \@list});
    }
    else
    {
	pop(@{$this->{state_stack}});
	if (scalar(@{$this->{state_stack}}) == 0)
	{
	    reset_state($this);
	}
	else
	{
	    $head = $this->{state_stack}->[$#{$this->{state_stack}}];
	    foreach my $win_instance (@{$this->{windows}})
	    {
		foreach my $window (@{$win_instance->{busy_windows}})
		{
		    $found = 0;
		    foreach my $busy_window (@{$head->{window_list}})
		    {
			if ($window == $busy_window)
			{
			    $found = 1;
			    last;
			}
		    }
		    if ($found)
		    {
			$window->set_cursor($this->{busy_cursor});
		    }
		    else
		    {
			$window->set_cursor(undef);
		    }
		}
	    }
	    if ($head->{exclude})
	    {
		Gtk2::Gdk::Event->handler_set(undef);
	    }
	    else
	    {
		Gtk2::Gdk::Event->handler_set
		    (\&event_filter,
		     {singleton   => $this,
		      grab_widget => $entry->{grab_widget}});
	    }
	}
    }

}
#
##############################################################################
#
#   Routine      - allow_input
#
#   Description  - Execute the specified code block whilst allowing mouse and
#                  keyboard input. Used for displaying dialog windows when the
#                  application is busy.
#
#   Data         - $this : The object.
#                  $code : The code block to be executed.
#
##############################################################################



sub allow_input($&)
{

    my($this, $code) = @_;

    local $this->{allow_input} = 1;

    &$code();

}
#
##############################################################################
#
#   Routine      - reset_state
#
#   Description  - Completely resets the state of all windows and input
#                  handling. Useful when resetting the GUI after an exception
#                  was raised.
#
#   Data         - $this : The object.
#
##############################################################################



sub reset_state($)
{

    my $this = $_[0];

    $this->{state_stack} = [];
    $this->{allow_input} = 0;

    foreach my $win_instance (@{$this->{windows}})
    {
	foreach my $window (@{$win_instance->{busy_windows}})
	{
	    $window->set_cursor(undef);
	}
    }
    Gtk2::Gdk::Event->handler_set(undef);

}
#
##############################################################################
#
#   Routine      - update_gui
#
#   Description  - Process all outstanding Gtk2 toolkit events. This is used
#                  to update the GUI whilst the application is busy doing
#                  something.
#
#   Data         - None.
#
##############################################################################



sub update_gui()
{

    return if (Gtk2->main_level() == 0);
    while (Gtk2->events_pending())
    {
	Gtk2->main_iteration();
    }

}
#
##############################################################################
#
#   Routine      - event_filter
#
#   Description  - Filter for getting rid of unwanted keyboard and mouse
#                  button events when the application is busy.
#
#   Data         - $event       : The Gtk2::Gdk::Event object representing the
#                                 current event.
#                  $client_data : The client data that was registered along
#                                 with this event handler.
#
##############################################################################



sub event_filter($$)
{

    my($event, $client_data) = @_;

    my($event_for_grab_widget,
       $widget);
    my $grab_widget = $client_data->{grab_widget};
    my $this        = $client_data->{singleton};
    my $type        = $event->type();

    $event_for_grab_widget =
	(defined($grab_widget)
	 && defined($widget = Gtk2->get_event_widget($event))
	 && $grab_widget == $widget)
	? 1 : 0;

    # Ignore the event if we are blocking input, the event is input related and
    # it isn't destined for the grab widget (if there is one).

    return if (! $this->{allow_input}
	       && exists($filtered_events{$type})
	       && ! $event_for_grab_widget);

    # If there is a grab widget then reset the mouse cursor to the default
    # whilst it is inside that widget.

    if ($event_for_grab_widget)
    {
	if ($type eq "enter-notify")
	{
	    $grab_widget->window()->set_cursor(undef);
	}
	elsif ($type eq "leave-notify")
	{
	    $grab_widget->window()->set_cursor($this->{busy_cursor});
	}
    }

    # Actually process the event.

    Gtk2->main_do_event($event);

}

1;

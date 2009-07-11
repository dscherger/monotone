##############################################################################
#
#   File Name    - WindowManager.pm
#
#   Description  - A class module for managing application windows, i.e. their
#                  recycling, busy cursor management and application help
#                  management.
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

require 5.008005;

use locale;
use strict;
use warnings;

# ***** REQUIRED PACKAGES *****

# Standard Perl and CPAN modules.

use Carp;
use Glib qw(FALSE TRUE);
use Gtk2;
use Gtk2::Gdk::Keysyms;

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

# Singleton instance constructor/accessor and destructor methods.

sub instance($);
sub cleanup($);

# Public methods.

sub activate_context_sensitive_help($$);
sub allow_input($&);
sub cond_find($$&);
sub display_window_help($$);
sub find_unused($$);
sub help_connect($$$$);
sub make_busy($$$;$);
sub manage($$$$;$);
sub reset_state($);
sub update_gui();

# Private routines.

sub busy_event_filter($$);
sub find_event_record($$);
sub find_gdk_windows($$);
sub find_record($$);
sub find_top_level_window($);
sub find_window_record($$);
sub head_of_busy_state_stack($);
sub help_event_filter($$);
sub window_is_busy($$);

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
	my $accel = Gtk2::AccelGroup->new();
	$accel->connect($Gtk2::Gdk::Keysyms{F1},
			[],
			[],
			sub {
			    my $this = WindowManager->instance();
			    if (defined($this->{help_contents_cb}))
			    {
				&{$this->{help_contents_cb}}(undef, undef);
				return TRUE;
			    }
			    return FALSE;
			});
	$accel->connect($Gtk2::Gdk::Keysyms{F1},
			["shift-mask"],
			[],
			sub {
			    my($accel, $widget, $key_code, $modifier) = @_;
			    my $entry;
			    my $this = WindowManager->instance();
			    return TRUE
				unless (defined($entry =
						$this->find_window_record
						    ($widget)));
			    $this->display_window_help($entry->{instance});
			    return TRUE;
			});
	$accel->connect($Gtk2::Gdk::Keysyms{F1},
			["control-mask"],
			[],
			sub {
			    my $this = WindowManager->instance();
			    $this->activate_context_sensitive_help
				(! $this->{help_active});
			    return TRUE;
			});
	$singleton = {managed_windows  => [],
		      busy_cursor      => Gtk2::Gdk::Cursor->new("watch"),
		      busy_state_stack => [],
		      allow_input      => 0,
		      help_active      => 0,
		      help_gdk_windows => [],
		      help_accel       => $accel,
		      help_cursor      => Gtk2::Gdk::Cursor->
			                      new("question-arrow"),
		      help_contents_cb => undef};
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

    foreach my $win_instance (@{$this->{managed_windows}})
    {
	$win_instance->{window}->destroy();
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
#                  it.
#
#   Data         - $this        : The object.
#                  $instance    : A reference to the window instance record
#                                 that is to be managed.
#                  $type        : The type of window that is to be managed.
#                  $window      : The main Gtk2::Window object that is
#                                 associated with $instance.
#                  $grab_widget : The widget object that is to still remain
#                                 responsive when making the window busy, most
#                                 typically this will be a `stop' button. This
#                                 is optional.
#
##############################################################################



sub manage($$$$;$)
{

    my($this, $instance, $type, $window, $grab_widget) = @_;

    my $list;

    croak("Window argument must be an object derived from Gtk2::Window")
	unless ($window->isa("Gtk2::Window"));

    # Set up the <F1> key to be an accelerator key that activates the context
    # sensitive help mechanism.

    $window->add_accel_group($this->{help_accel});

    # Traverse the widget hierarchy looking for additional windows that need to
    # be handled WRT mouse cursor changes.

    $list = [];
    find_gdk_windows($window, $list);

    # Store the details in our window list.

    push(@{$this->{managed_windows}},
	 {instance       => $instance,
	  type           => $type,
	  window         => $window,
	  gdk_windows    => $list,
	  grab_widget    => $grab_widget,
	  help_callbacks => {}});

}
#
##############################################################################
#
#   Routine      - help_connect
#
#   Description  - Register the specified help callback for the specified
#                  window instance and widget.
#
#   Data         - $this     : The object.
#                  $instance : Either the window instance that is to have a
#                              context sensitive help callback registered or
#                              undef if the top level help contents callback
#                              is to be registered.
#                  $widget   : Either the containing widget inside which the
#                              help event needs to occur for this callback to
#                              be invoked or undef if this callback is to be
#                              invoked if there are no more specific callbacks
#                              registered for the help event.
#                  $callback : A reference to the help callback routine that
#                              is to be called.
#
##############################################################################



sub help_connect($$$$)
{

    my($this, $instance, $widget, $callback) = @_;

    $widget = "" if (! defined($widget));

    # Simply store the callback details depending upon its type.

    if (defined($instance))
    {
	my $entry = $this->find_record($instance);
	$entry->{help_callbacks}->{$widget} = $callback;
    }
    else
    {
	$this->{help_contents_cb} = $callback;
    }

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

    foreach my $win_instance (@{$this->{managed_windows}})
    {
	return $win_instance->{instance}
	    if ($win_instance->{type} eq $type
		&& ! $win_instance->{window}->mapped());
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

    foreach my $win_instance (@{$this->{managed_windows}})
    {
	return $win_instance->{instance}
	    if ((! defined($type) || $win_instance->{type} eq $type)
		&& &$predicate($win_instance->{instance}));
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
#                              are to be made busy, otherwise false if all
#                              windows are to be made busy. This is used by
#                              windows that wish to be modal. This is
#                              optional.
#
##############################################################################



sub make_busy($$$;$)
{

    my($this, $instance, $busy, $exclude) = @_;

    # When making things busy filter out keyboard and mouse button events
    # unless they relate to the grab widget (usually a `stop' button) or the
    # excluded window and make the mouse cursor busy. Making things unbusy is
    # simply the reverse. Also cope with nested calls.

    if ($busy)
    {

	my $entry = $this->find_record($instance);
	my $exclude_window = $exclude ? $entry->{window} : undef;
	my @list;

	# Make the application busy.

	# Install a custom event handler that will filter out unwanted user
	# input events.

	Gtk2::Gdk::Event->handler_set
	    (\&busy_event_filter,
	     {singleton      => $this,
	      exclude_window => $exclude_window,
	      grab_widget    => $entry->{grab_widget}});

	# Make all visible windows busy by changing their mouse cursors,
	# excluding the current window if required. Also make a record of what
	# windows were affected.

	foreach my $win_instance (@{$this->{managed_windows}})
	{
	    if (! $exclude || $win_instance->{instance} != $instance)
	    {
		foreach my $gdk_window (@{$win_instance->{gdk_windows}})
		{
		    if ($gdk_window->is_visible()
			&& (! defined ($exclude_window)
			    || (defined($exclude_window)
				&& $exclude_window->window() != $gdk_window)))
		    {
			$gdk_window->set_cursor($this->{busy_cursor});
			push(@list, $gdk_window);
		    }
		}
	    }
	}

	# Save what we have just done on the state stack (we allow nested
	# calls).

	push(@{$this->{busy_state_stack}},
	     {affected_gdk_windows => \@list,
	      exclude_window       => $exclude_window,
	      grab_widget          => $entry->{grab_widget}});

    }
    else
    {

	my $head;

	# Restore the application to its previous busy state (typically
	# unbusy).

	# Get rid of the current state off the stack.

	pop(@{$this->{busy_state_stack}});

	# Do we have any saved states left?

	if (! defined($head = $this->head_of_busy_state_stack()))
	{

	    # No we don't.

	    # We now must be unbusy so unconditionally reset everything to the
	    # unbusy state.

	    $this->reset_state();

	}
	else
	{

	    # Yes we have.

	    # Reinstate the window mouse cursor state as indicated by the head
	    # of the state stack.

	    foreach my $win_instance (@{$this->{managed_windows}})
	    {
		foreach my $gdk_window (@{$win_instance->{gdk_windows}})
		{
		    if ($this->window_is_busy($gdk_window))
		    {
			$gdk_window->set_cursor($this->{busy_cursor});
		    }
		    else
		    {
			$gdk_window->set_cursor(undef);
		    }
		}
	    }

	    # Reinstate the event handling context for the head of the state
	    # stack (although a handler is registered, really the only thing
	    # that has changed is its client data, it's still the same
	    # handler).

	    Gtk2::Gdk::Event->handler_set
		(\&busy_event_filter,
		 {singleton      => $this,
		  exclude_window => $head->{exclude_window},
		  grab_widget    => $head->{grab_widget}});

	}

    }

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

    # Only allow this if we are actually running inside Gtk2.

    return if (Gtk2->main_level() == 0);

    # Process all outstanding events.

    while (Gtk2->events_pending())
    {
	Gtk2->main_iteration();
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
#   Routine      - activate_context_sensitive_help
#
#   Description  - Starts and stops the context sensitive help mode for the
#                  application (in this mode the user can click on a widget
#                  and get help on it).
#
#   Data         - $this     : The object.
#                  $activate : True if the context sensitive help mode should
#                              be activated, otherwise false if it should be
#                              deactivated.
#
##############################################################################



sub activate_context_sensitive_help($$)
{

    my($this, $activate) = @_;

    # Ignore duplicate calls.

    return if (($activate && $this->{help_active})
	       || ! ($activate || $this->{help_active}));

    if ($activate)
    {

	# Activate context sensitive help. Do this by installing our custom
	# event handler and then go through all visible unbusy windows changing
	# their mouse cursor to a question mark (also keep a record of what
	# windows have been changed in help_gdk_windows so it can be undone).

	$this->{help_active} = 1;
	$this->{help_gdk_windows} = [];

	Gtk2::Gdk::Event->handler_set(\&help_event_filter, $this);

	# Do we have any busy windows?

	if (scalar(@{$this->{busy_state_stack}}) == 0)
	{

	    # No we don't so change all visible windows.

	    foreach my $win_instance (@{$this->{managed_windows}})
	    {
		foreach my $gdk_window (@{$win_instance->{gdk_windows}})
		{
		    if ($gdk_window->is_visible())
		    {
			$gdk_window->set_cursor($this->{help_cursor});
			push(@{$this->{help_gdk_windows}}, $gdk_window);
		    }
		}
	    }

	}
	else
	{

	    # Yes we have so change only the non-busy windows.

	    foreach my $win_instance (@{$this->{managed_windows}})
	    {
		foreach my $gdk_window (@{$win_instance->{gdk_windows}})
		{
		    if (! $this->window_is_busy($gdk_window)
			&& $gdk_window->is_visible())
		    {
			$gdk_window->set_cursor($this->{help_cursor});
			push(@{$this->{help_gdk_windows}}, $gdk_window);
		    }
		}
	    }

	}

    }
    else
    {

	my $head;

	# Deactivate context sensitive help. Simply do this by reinstating the
	# previous event handling setup and then reset the mouse cursor on all
	# of those windows that had their cursor changed in the first place.

	$this->{help_active} = 0;
	if (! defined($head = $this->head_of_busy_state_stack()))
	{
	    $this->reset_state();
	}
	else
	{
	    Gtk2::Gdk::Event->handler_set
		(\&busy_event_filter,
		 {singleton      => $this,
		  grab_widget    => $head->{grab_widget},
		  exclude_window => $head->{exclude_window}});
	    foreach my $gdk_window (@{$this->{help_gdk_windows}})
	    {
		$gdk_window->set_cursor(undef);
	    }
	    $this->{help_gdk_windows} = [];
	}

    }

}
#
##############################################################################
#
#   Routine      - display_window_help
#
#   Description  - Calls the default context sensitive help callback for the
#                  specified window instance. This default callback, one where
#                  the widget is set to '', is used to display help on the
#                  entire window rather than just a widget or area of the
#                  window.
#
#   Data         - $this     : The object.
#                  $instance : The window instance that is to have its default
#                              context sensitive help callback called.
#
##############################################################################



sub display_window_help($$)
{

    my($this, $instance) = @_;

    my $entry = $this->find_record($instance);

    &{$entry->{help_callbacks}->{""}}($entry->{window}, $entry->{instance})
	if (exists($entry->{help_callbacks}->{""}));

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

    $this->{busy_state_stack} = [];
    $this->{allow_input} = 0;
    $this->{help_active} = 0;
    $this->{help_gdk_windows} = [];

    foreach my $win_instance (@{$this->{managed_windows}})
    {
	foreach my $gdk_window (@{$win_instance->{gdk_windows}})
	{
	    $gdk_window->set_cursor(undef);
	}
    }
    Gtk2::Gdk::Event->handler_set(undef);

}
#
##############################################################################
#
#   Routine      - window_is_busy
#
#   Description  - Determine whether the specified top level window is busy or
#                  not.
#
#   Data         - $this        : The object.
#                  $window      : Either a Gtk2::Window or Gtk2::Gdk::Window
#                                 object that is to be tested.
#                  Return Value : True if the window is busy, otherwise false.
#
##############################################################################



sub window_is_busy($$)
{

    my($this, $window) = @_;

    my $gdk_window = (ref($window) eq "Gtk2::Gdk::Window")
	? $window : $window->window();
    my $head;

    return unless (defined($head = $this->head_of_busy_state_stack()));
    foreach my $gdk_busy_window (@{$head->{affected_gdk_windows}})
    {
	return 1 if ($gdk_window == $gdk_busy_window);
    }

    return;

}
#
##############################################################################
#
#   Routine      - find_record
#
#   Description  - Find and return the internal management record for the
#                  specified window instance.
#
#   Data         - $this        : The object.
#                  $instance    : The window instance that is to be found.
#                  Return Value : A reference to the internal management
#                                 record that manages the specified window
#                                 instance.
#
##############################################################################



sub find_record($$)
{

    my($this, $instance) = @_;

    foreach my $win_instance (@{$this->{managed_windows}})
    {
	return $win_instance if ($win_instance->{instance} == $instance);
    }
    croak("Called with an unmanaged instance record");

}
#
##############################################################################
#
#   Routine      - find_event_record
#
#   Description  - Find and return the internal management record relating to
#                  the specified Gtk2/GDK event.
#
#   Data         - $this        : The object.
#   Data         - $event       : The Gtk2::Gdk::Event object describing the
#                                 event.
#                  Return Value : A reference to the internal management
#                                 record that manages the window that received
#                                 the event, otherwise undef if one cannot be
#                                 found.
#
##############################################################################



sub find_event_record($$)
{

    my($this, $event) = @_;

    my $event_widget;

    return unless (defined($event_widget = Gtk2->get_event_widget($event)));
    return $this->find_window_record(find_top_level_window($event_widget));

}
#
##############################################################################
#
#   Routine      - find_window_record
#
#   Description  - Find and return the internal management record for the
#                  specified top level window.
#
#   Data         - $this        : The object.
#                  $window      : The top level window for which a window
#                                 instance that is to be found.
#                  Return Value : A reference to the internal management
#                                 record that manages the specified top level
#                                 window, otherwise undef if one cannot be
#                                 found.
#
##############################################################################



sub find_window_record($$)
{

    my($this, $window) = @_;

    foreach my $win_instance (@{$this->{managed_windows}})
    {
	return $win_instance if ($win_instance->{window} == $window);
    }

    return;

}
#
##############################################################################
#
#   Routine      - head_of_busy_state_stack
#
#   Description  - Return the head record on the busy state stack.
#
#   Data         - $this        : The object.
#                  Return Value : The head entry on the busy state stack,
#                                 otherwise undef if the stack is empty.
#
##############################################################################



sub head_of_busy_state_stack($)
{

    my $this = $_[0];

    return $this->{busy_state_stack}->[$#{$this->{busy_state_stack}}]
	unless (scalar(@{$this->{busy_state_stack}}) == 0);

    return;

}
#
##############################################################################
#
#   Routine      - find_gdk_windows
#
#   Description  - Recursively descends the widget hierarchy from the
#                  specified widget, looking for widgets that need their own
#                  mouse cursor handling.
#
#   Data         - $widget : The widget from where the search is to be
#                            started.
#                  $list   : A reference to a list that is to contain the
#                            Gtk2::Gdk:Window widgets for all of those widgets
#                            that need their own mouse cursor handling.
#
##############################################################################



sub find_gdk_windows($$)
{

    my($widget, $list) = @_;

    if ($widget->isa("Gtk2::Window"))
    {
	push(@$list, $widget->window());
    }
    elsif ($widget->isa("Gtk2::TextView"))
    {
	push(@$list, $widget->get_window("text"));
    }
    if ($widget->isa("Gtk2::Container"))
    {
	foreach my $child ($widget->get_children())
	{
	    find_gdk_windows($child, $list);
	}
    }

}
#
##############################################################################
#
#   Routine      - find_top_level_window
#
#   Description  - For the specified widget, find the top level window
#                  containing that widget.
#
#   Data         - $widget      : The widget for which the top level window is
#                                 to be found.
#                  Return Value : The Gtk2 object for the top level window,
#                                 typically derived from Gtk2::Window.
#
##############################################################################



sub find_top_level_window($)
{

    my $widget = $_[0];

    my($current,
       $parent);

    $current = $widget;
    while (defined($parent = $current->get_parent()))
    {
	$current = $parent;
    }

    return $current;

}
#
##############################################################################
#
#   Routine      - busy_event_filter
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



sub busy_event_filter($$)
{

    my($event, $client_data) = @_;

    my $event_widget   = Gtk2->get_event_widget($event);
    my $exclude_window = $client_data->{exclude_window};
    my $grab_widget    = $client_data->{grab_widget};
    my $this           = $client_data->{singleton};
    my $type           = $event->type();

    if (defined($exclude_window))
    {

	my $allow_event;

	# There is an excluded window so allow all user input events destined
	# for that window and any other window that wasn't made busy in the
	# first place (like subordinate dialog windows), otherwise filter them
	# out if we are currently blocking input.

	# Find out whether the event is destined for the excluded window or one
	# of its subordinates.

	if (defined($event_widget))
	{

	    # Find the top level window for the widget that received the event.

	    my $event_window = find_top_level_window($event_widget);

	    # Allow the event if it is for the excluded window or for a newly
	    # displayed non-busy window.

	    $allow_event = 1 if ($event_window == $exclude_window
				 || ! $this->window_is_busy($event_window));

	}

	# Ignore the event if we are blocking input, it isn't destined for the
	# excluded window (or one of its subordinates) and it is user input
	# related.

	return if (! $this->{allow_input} && ! $allow_event
		   && exists($filtered_events{$type}));

    }
    else
    {

	# No excluded windows so only allow user input events into the grab
	# widget if one is defined.

	my $event_for_grab_widget =
	    (defined($grab_widget) && defined($event_widget)
	     && $grab_widget == $event_widget)
	    ? 1 : undef;

	# Ignore the event if we are blocking input, it isn't destined for the
	# grab widget (if there is one) and it is user input related.

	return if (! $this->{allow_input}
		   && ! $event_for_grab_widget
		   && exists($filtered_events{$type}));

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

    }

    # Actually process the event.

    Gtk2->main_do_event($event);

}
#
##############################################################################
#
#   Routine      - help_event_filter
#
#   Description  - Filter for getting rid of unwanted keyboard and mouse
#                  button events when the application is in context sensitive
#                  help mode.
#
#   Data         - $event       : The Gtk2::Gdk::Event object representing the
#                                 current event.
#                  $client_data : The client data that was registered along
#                                 with this event handler.
#
##############################################################################



sub help_event_filter($$)
{

    my($event, $this) = @_;

    my $type = $event->type();

    # See if it is an event we are interested in (pressing the Ctrl-<F1> key or
    # pressing the left mouse button).

    # Key presses.

    if ($type eq "key-press")
    {

	my($consumed_modifiers,
	   $entry,
	   $keymap,
	   $keyval,
	   $state);

	# Ignore the state of the caps-lock key.

	$state = $event->state() - "lock-mask";

	# Work out what the key is having taken into account any modifier keys
	# (except caps-lock).

	$keymap = Gtk2::Gdk::Keymap->get_for_display
	    (Gtk2::Gdk::Display->get_default());
	($keyval, $consumed_modifiers) =
	    ($keymap->translate_keyboard_state
	     ($event->hardware_keycode(), $state, $event->group()))[0, 3];

	# Make sure that there is an active window for the event and that it
	# isn't busy.

	return unless (defined($entry = $this->find_event_record($event))
		       && ! $this->window_is_busy($entry->{window}));

	# We are only interested in Ctrl-<F1> (in which case just let it
	# through to be processed in the normal way, which in turn will
	# deactivate context sensitive help mode).

	return if (! (defined($keyval) && $keyval == $Gtk2::Gdk::Keysyms{F1}
		      && ($state - $consumed_modifiers) eq "control-mask"));

    }

    # Mouse button presses.

    elsif ($type eq "button-press")
    {

	# Only interested in the left mouse button without any keyboard
	# modifiers active (except caps-lock).

	if ($event->button == 1 && ($event->state() - "lock-mask") == [])
	{
	    my $event_widget;
	    if (defined($event_widget = Gtk2->get_event_widget($event)))
	    {

		my($entry,
		   $help_cb,
		   $widget);

		# Find the active window for the event. Ignore the event if
		# there isn't an active window or if it is busy.

		return unless (defined($entry =
				       $this->find_event_record($event))
			       && ! $this->window_is_busy($entry->{window}));

		# Now find the relevant help callback for the widget or one of
		# its containing parents. If nothing is found then check to see
		# if there is a default help callback for the window (indicated
		# by having the widget set to '').

		$widget = $event_widget;
		do
		{
		    $help_cb = $entry->{help_callbacks}->{$widget}
		        if (exists($entry->{help_callbacks}->{$widget}));
		}
		while (defined($widget = $widget->get_parent())
		       && ! defined($help_cb));
		if (! defined($help_cb))
		{
		    return unless (exists($entry->{help_callbacks}->{""}));
		    $help_cb = $entry->{help_callbacks}->{""};
		}

		# Now simply call the help callback.

		&$help_cb($event_widget, $entry->{instance});

		$this->activate_context_sensitive_help(0);

	    }
	}
	return;

    }

    # Discard any remaining user input events.

    elsif (exists($filtered_events{$type}))
    {
	return;
    }

    # Actually process the event.

    Gtk2->main_do_event($event);

}

1;

##############################################################################
#
#   File Name    - ComboAutoCompletion.pm
#
#   Description  - The combo box auto-completion utilities module for the
#                  mtn-browse application. This module contains assorted
#                  routines that implement auto-completion for all branch and
#                  revision comboboxentry boxes.
#
#   Author       - A.E.Cooper.
#
#   Legal Stuff  - Copyright (c) 2007 Anthony Edward Cooper
#                  <aecooper@coosoft.plus.com>.
#
#                  This program is free software; you can redistribute it
#                  and/or modify it under the terms of the GNU General Public
#                  License as published by the Free Software Foundation;
#                  either version 3 of the License, or (at your option) any
#                  later version.
#
#                  This program is distributed in the hope that it will be
#                  useful, but WITHOUT ANY WARRANTY; without even the implied
#                  warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
#                  PURPOSE. See the GNU General Public License for more
#                  details.
#
#                  You should have received a copy of the GNU General Public
#                  License along with this software; if not, write to the Free
#                  Software Foundation, Inc., 59 Temple Place - Suite 330,
#                  Boston, MA 02111-1307 USA.
#
##############################################################################
#
##############################################################################
#
#   Global Data For This Module
#
##############################################################################



# ***** DIRECTIVES *****

require 5.008005;

use locale;
use strict;
use warnings;

# ***** GLOBAL DATA DECLARATIONS *****

# The type of window that is going to be managed by this module.

my $window_type = "tooltip_window";

# ***** FUNCTIONAL PROTOTYPES *****

# Public routines.

sub activate_auto_completion($$);
sub tagged_checkbutton_toggled_cb($$);

# Private routines.

sub auto_completion_comboboxentry_changed_cb($$);
sub auto_completion_comboboxentry_key_release_event_cb($$$);
sub get_tooltip_window($$$$);
sub hide_tooltip_window();
#
##############################################################################
#
#   Routine      - activate_auto_completion
#
#   Description  - Sets up the specified comboboxentry widget for
#                  auto-completion.
#
#   Data         - $comboboxentry : The comboboxentry widget that is to be set
#                                   up for auto-completion.
#                  $instance      : The window instance that is associated
#                                   with this widget. It is expected to have
#                                   window, appbar, update_handler and
#                                   combobox details fields.
#
##############################################################################



sub activate_auto_completion($$)
{

    my($comboboxentry, $instance) = @_;

    my($change_state,
       $combo_details,
       $details,
       $name);

    # Sort out the precise details depending upon which comboboxentry widget
    # has been passed.

    if ($comboboxentry == $instance->{branch_comboboxentry})
    {
	$change_state = BRANCH_CHANGED;
	$combo_details = $instance->{branch_combo_details};
	$name = __("branch");
    }
    elsif ($comboboxentry == $instance->{revision_comboboxentry})
    {
	$change_state = REVISION_CHANGED;
	$combo_details = $instance->{revision_combo_details};
	$name = __("revision");
    }
    elsif ($comboboxentry == $instance->{directory_comboboxentry})
    {
	$change_state = DIRECTORY_CHANGED;
	$combo_details = $instance->{directory_combo_details};
	$name = __("directory");
    }
    else
    {
	return;
    }

    # Set up all the required callbacks.

    $details = {instance      => $instance,
		change_state  => $change_state,
		combo_details => $combo_details,
		name          => $name};
    $comboboxentry->signal_connect("changed",
				   \&auto_completion_comboboxentry_changed_cb,
				   $details);
    $comboboxentry->signal_connect
	("key_release_event",
	 \&auto_completion_comboboxentry_key_release_event_cb,
	 $details);
    $comboboxentry->child()->signal_connect("focus_out_event",
					    sub {
						hide_tooltip_window();
						return FALSE;
					    });

}
#
##############################################################################
#
#   Routine      - tagged_checkbutton_toggled_cb
#
#   Description  - Callback routine called when the user changes the value of
#                  the tagged check button.
#
#   Data         - $widget   : The widget object that received the signal.
#                  $instance : The window instance that is associated with
#                              this widget.
#
##############################################################################



sub tagged_checkbutton_toggled_cb($$)
{

    my($widget, $instance) = @_;

    return if ($instance->{in_cb});
    local $instance->{in_cb} = 1;

    $instance->{appbar}->clear_stack();
    &{$instance->{update_handler}}($instance, BRANCH_CHANGED);

}
#
##############################################################################
#
#   Routine      - auto_completion_comboboxentry_changed_cb
#
#   Description  - Callback routine called when the user changes the value of
#                  a branch, revision or directory comboboxentry by selecting
#                  an entry from its pulldown list.
#
#   Data         - $widget  : The widget object that received the signal.
#                  $details : A reference to an anonymous hash containing the
#                             window instance, change state, comboboxentry
#                             details and the name for that comboboxentry.
#
##############################################################################



sub auto_completion_comboboxentry_changed_cb($$)
{

    my($widget, $details) = @_;

    my $instance = $details->{instance};

    return if ($instance->{in_cb});
    local $instance->{in_cb} = 1;

    my $value;
    my $change_state = $details->{change_state};
    my $combo_details = $details->{combo_details};

    # For some reason best known to itself, Gtk+ calls this callback when the
    # user presses a key for the first time (but not subsequently) after a
    # value is selected via the pulldown menu. So we have to guard against
    # this. Under these circumstances the key release callback is also called.
    # So, put simply, only do something inside this callback if the value is a
    # direct match to one in our list.

    $value = $widget->child()->get_text();
    foreach my $item (@{$combo_details->{list}})
    {
	if ($value eq $item)
	{
	    $combo_details->{value} = $value;
	    $combo_details->{complete} = 1;
	    $instance->{appbar}->clear_stack();
	    &{$instance->{update_handler}}($instance, $change_state);
	    hide_tooltip_window();
	    last;
	}
    }

}
#
##############################################################################
#
#   Routine      - auto_completion_comboboxentry_key_release_event_cb
#
#   Description  - Callback routine called when the user changes the value of
#                  a branch or revision comboboxentry by entering a character
#                  (key release event).
#
#   Data         - $widget      : The widget object that received the signal.
#                  $event       : A Gtk2::Gdk::Event object describing the
#                                 event that has occurred.
#                  $details     : A reference to an anonymous hash containing
#                                 the window instance, change state,
#                                 comboboxentry details and the name for that
#                                 comboboxentry.
#                  Return Value : TRUE if the event has been handled and needs
#                                 no further handling, otherwise false if the
#                                 event should carry on through the remaining
#                                 event handling.
#
##############################################################################



sub auto_completion_comboboxentry_key_release_event_cb($$$)
{

    my($widget, $event, $details) = @_;

    my $instance = $details->{instance};

    return FALSE if ($instance->{in_cb});
    local $instance->{in_cb} = 1;

    my $combo_details = $details->{combo_details};
    my $entry = $widget->child();
    my $old_value = $combo_details->{value};
    my $value = $entry->get_text();

    # The user has typed something in then validate it and auto-complete it if
    # necessary.

    if ($value ne $old_value)
    {

	my($busy,
	   $completion,
	   $len,
	   $success);
	my $change_state = $details->{change_state};
	my $complete = 0;
	my $name = $details->{name};
	my $old_complete = $combo_details->{complete};
	my $wm = WindowManager->instance();

	# Don't auto-complete if the user is simply deleting from the extreme
	# right.

	$len = length($value);
	if ($len >= length($old_value)
	    || $value ne substr($old_value, 0, $len))
	{

	    # Initialise a new auto-completion object with a new list of terms
	    # if it hasn't been done so already.

	    $combo_details->{completion} =
		Completion->new($combo_details->{list})
		if (! defined($combo_details->{completion}));

	    # Try auto-completing with what we have got, if that fails then try
	    # stripping off any trailing white space before trying again (this
	    # means the user can use the spacebar to trigger auto-completion in
	    # a similar fashion to bash's use of <Tab>).

	    if (! ($success = $combo_details->{completion}->
		       get_completion($value, \$completion, \$complete)))
	    {
		$value =~ s/\s+$//;
		$success = $combo_details->{completion}->
		       get_completion($value, \$completion, \$complete);
	    }
	    if ($success)
	    {
		$instance->{appbar}->clear_stack();
		hide_tooltip_window();
	    }
	    else
	    {

		my $message;

		# Tell the user what is wrong via the status bar.

		$message = __x("Invalid {name} name `{value}'",
			       name  => $name,
			       value => $value);
		$instance->{appbar}->set_status($message);

		# Also via a tooltip as well if so desired (need to position it
		# to be just below the comboboxentry widget).

		if ($user_preferences->{completion_tooltips})
		{
		    my($height,
		       $root_x,
		       $root_y,
		       $x,
		       $y);
		    ($x, $y) =
			$widget->translate_coordinates($instance->{window},
						       0,
						       0);
		    $height = ($widget->child()->window()->get_geometry())[3];
		    ($root_x, $root_y) =
			$instance->{window}->window()->get_origin();
		    $x += $root_x - 10;
		    $y += $height + $root_y + 5;
		    get_tooltip_window($instance->{window}, $message, $x, $y);
		}

	    }

	    $value = $completion;
	    $len = length($value);
	    $entry->set_text($value);
	    $entry->set_position(-1);

	}
	else
	{
	    $instance->{appbar}->clear_stack();
	    hide_tooltip_window();
	}
	$wm->update_gui();
	$combo_details->{value} = $value;
	$combo_details->{complete} = $complete;

	# Update the pulldown choices if the value has actually changed (what
	# the user has entered may have been discarded due to not being valid)
	# and that is what the user wants.

	if ($value ne $old_value)
	{

	    my @item_list;

	    foreach my $item (@{$combo_details->{list}})
	    {
		my $item_len = length($item);
		if ($len <= $item_len && $value eq substr($item, 0, $len))
		{
		    push(@item_list, $item)
			unless ($user_preferences->{static_lists});

		    # The following check is needed in the case when the user
		    # is simply deleting characters from the right.

		    $combo_details->{complete} = 1 if ($len == $item_len);
		}
	    }
	    if (! $user_preferences->{static_lists})
	    {
		my $counter = 1;
		$busy = 1;
		$wm->make_busy($instance, 1);
		$instance->{appbar}->set_progress_percentage(0);
		$instance->{appbar}->push(__x("Populating {name} list",
					      name => $name));
		$wm->update_gui();
		$widget->get_model()->clear()
		    unless ($user_preferences->{static_lists});
		foreach my $item (@item_list)
		{
		    $widget->append_text($item);
		    if (($counter % 10) == 0)
		    {
			$instance->{appbar}->set_progress_percentage
			    ($counter / scalar(@item_list));
			$wm->update_gui();
		    }
		    ++ $counter;
		}
		$instance->{appbar}->set_progress_percentage(1);
		$wm->update_gui();
		$instance->{appbar}->set_progress_percentage(0);
		$instance->{appbar}->pop();
		$wm->update_gui();
	    }

	}

	# Update the window state on a significant change.

	&{$instance->{update_handler}}($instance, $change_state)
	    if ($combo_details->{complete} != $old_complete
		|| ($combo_details->{complete}
		    && $combo_details->{value} ne $old_value));

	$wm->make_busy($instance, 0) if ($busy);

    }

    return FALSE;

}
#
##############################################################################
#
#   Routine      - get_tooltip_window
#
#   Description  - Creates or prepares an existing tooltip window for use.
#
#   Data         - $parent       : The parent window widget for the multiple
#                                  revisions dialog window.
#                  $message      : The tooltip that is to be displayed.
#                  $x            : The x coordinate for where the tooltip
#                                  window is to be placed.
#                  $y            : The y coordinate for where the tooltip
#                                  window is to be placed.
#                  Return Value  : A reference to the newly created or unused
#                                  multiple revisions instance record.
#
##############################################################################



sub get_tooltip_window($$$$)
{

    my($parent, $message, $x, $y) = @_;

    my($instance,
       $new);
    my $wm = WindowManager->instance();

    # Create a new tooltip window if an existing one wasn't found, otherwise
    # reuse an existing one (used or unused).

    if (! defined($instance = $wm->cond_find($window_type, sub { return 1; })))
    {

	$new = 1;
	$instance = {};
	$instance->{glade} = Gtk2::GladeXML->new($glade_file,
						 $window_type,
						 APPLICATION_NAME);

	# Flag to stop recursive calling of callbacks.

	$instance->{in_cb} = 0;
	local $instance->{in_cb} = 1;

	# Connect Glade registered signal handlers.

	glade_signal_autoconnect($instance->{glade}, $instance);

	# Get the widgets that we are interested in.

	$instance->{window} = $instance->{glade}->get_widget($window_type);
	foreach my $widget ("eventbox", "message_label")
	{
	    $instance->{$widget} = $instance->{glade}->get_widget($widget);
	}

	# Setup the colours used for the tooltip window.

	$instance->{window}->modify_bg("normal",
				       Gtk2::Gdk::Color->parse("Black"));
	$instance->{eventbox}->modify_bg("normal",
					 Gtk2::Gdk::Color->parse("Pink"));

    }
    else
    {
	$instance->{in_cb} = 0;
	local $instance->{in_cb} = 1;
	$instance->{window}->hide();
	Glib::Source->remove($instance->{timeout_source_id});
    }

    local $instance->{in_cb} = 1;

    # Update the tooltip message text and setup a timeout handler to dismiss
    # the window after three seconds.

    $instance->{message_label}->set_text($message);
    $instance->{timeout_source_id} =
	Glib::Timeout->add(3000,
			   sub {
			       my $instance = $_[0];
			       $instance->{window}->hide();
			       return FALSE;
			   },
			   $instance);

    # Position it, reparent window and display it.

    $instance->{window}->move($x, $y);
    $instance->{window}->set_transient_for($parent);
    $instance->{window}->show_all();
    $instance->{window}->present();

    # If necessary, register the window for management.

    $wm->manage($instance, $window_type, $instance->{window}) if ($new);

    return $instance;

}
#
##############################################################################
#
#   Routine      - hide_tooltip_window
#
#   Description  - Hides the tooltip window if it is visible.
#
#   Data         - None.
#
##############################################################################



sub hide_tooltip_window()
{

    my $instance;

    # Look for a mapped tooltip window, if found then hide it and cancel its
    # hide timeout handler.

    if (defined($instance = WindowManager->instance()->cond_find
		($window_type,
		 sub {
		     my $instance = $_[0];
		     return $instance->{window}->mapped();
		 })))
    {
	$instance->{in_cb} = 0;
	local $instance->{in_cb} = 1;
	$instance->{window}->hide();
	Glib::Source->remove($instance->{timeout_source_id});
    }

}

1;

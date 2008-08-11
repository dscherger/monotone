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

require 5.008;

use strict;
use warnings;

# ***** FUNCTIONAL PROTOTYPES *****

# Public routines.

sub comboboxentry_changed_cb($$);
sub comboboxentry_key_release_event_cb($$$);
sub tagged_checkbutton_toggled_cb($$);
#
##############################################################################
#
#   Routine      - comboboxentry_changed_cb
#
#   Description  - Callback routine called when the user changes the value of
#                  a branch or revision comboboxentry by selecting an entry
#                  from its pulldown list.
#
#   Data         - $widget   : The widget object that received the signal.
#                  $instance : The window instance that is associated with
#                              this widget.
#
##############################################################################



sub comboboxentry_changed_cb($$)
{

    my($widget, $instance) = @_;

    return if ($instance->{in_cb});
    local $instance->{in_cb} = 1;

    my($change_state,
       $combo_details,
       $item,
       $value);

    if ($widget == $instance->{branch_comboboxentry})
    {
	$change_state = BRANCH_CHANGED;
	$combo_details = $instance->{branch_combo_details};
    }
    elsif ($widget == $instance->{revision_comboboxentry})
    {
	$change_state = REVISION_CHANGED;
	$combo_details = $instance->{revision_combo_details};
    }
    elsif ($widget == $instance->{directory_comboboxentry})
    {
	$change_state = DIRECTORY_CHANGED;
	$combo_details = $instance->{directory_combo_details};
    }
    else
    {
	return;
    }

    # For some reason best known to itself, Gtk+ calls this callback when the
    # user presses a key for the first time (but not subsequently) after a
    # value is selected via the pulldown menu. So we have to guard against
    # this. Under these circumstances the key release callback is also called.
    # So, put simply, only do something inside this callback if the value is a
    # direct match to one in our list.

    $value = $widget->child()->get_text();
    foreach $item (@{$combo_details->{list}})
    {
	if ($value eq $item)
	{
	    $combo_details->{value} = $value;
	    $combo_details->{complete} = 1;
	    $instance->{appbar}->clear_stack();
	    &{$instance->{update_handler}}($instance, $change_state);
	    last;
	}
    }

}
#
##############################################################################
#
#   Routine      - comboboxentry_key_release_event_cb
#
#   Description  - Callback routine called when the user changes the value of
#                  a branch or revision comboboxentry by entering a character
#                  (key release event).
#
#   Data         - $widget      : The widget object that received the signal.
#                  $event       : A Gtk2::Gdk::Event object describing the
#                                 event that has occurred.
#                  $instance    : The window instance that is associated with
#                                 this widget.
#                  Return Value : TRUE if the event has been handled and needs
#                                 no further handling, otherwise false if the
#                                 event should carry on through the remaining
#                                 event handling.
#
##############################################################################



sub comboboxentry_key_release_event_cb($$$)
{

    my($widget, $event, $instance) = @_;

    return FALSE if ($instance->{in_cb});
    local $instance->{in_cb} = 1;

    my($change_state,
       $combo,
       $combo_details,
       $complete,
       $completion,
       $item,
       $len,
       $name,
       $old_complete,
       $old_value,
       $value);

    if ($widget == $instance->{branch_comboboxentry}->child())
    {
	$combo = $instance->{branch_comboboxentry};
	$change_state = BRANCH_CHANGED;
	$combo_details = $instance->{branch_combo_details};
	$name = "branch";
    }
    elsif ($widget == $instance->{revision_comboboxentry}->child())
    {
	$combo = $instance->{revision_comboboxentry};
	$change_state = REVISION_CHANGED;
	$combo_details = $instance->{revision_combo_details};
	$name = "revision";
    }
    elsif ($widget == $instance->{directory_comboboxentry}->child())
    {
	$combo = $instance->{directory_comboboxentry};
	$change_state = DIRECTORY_CHANGED;
	$combo_details = $instance->{directory_combo_details};
	$name = "directory";
    }
    else
    {
	return FALSE;
    }

    # The user has typed something in then validate it and auto-complete it if
    # necessary.

    $complete = 0;
    $old_complete = $combo_details->{complete};
    $old_value = $combo_details->{value};
    $value = $widget->get_text();
    if ($value ne $old_value)
    {

	# Don't auto-complete if the user is simply deleting from the extreme
	# right.

	$len = length($value);
	if ($value ne substr($old_value, 0, $len))
	{

	    # So that the spacebar triggers auto-complete.

	    $value =~ s/\s+$//;
	    $len = length($value);

	    $combo_details->{completion} =
		Completion->new($combo_details->{list})
		if (! defined($combo_details->{completion}));

	    if ($combo_details->{completion}->get_completion($value,
							     \$completion,
							     \$complete))
	    {
		$instance->{appbar}->clear_stack();
	    }
	    else
	    {
		$instance->{appbar}->
		    push(__x("Invalid {name} name `{value}'",
			     name  => $name,
			     value => $value));
	    }
	    $value = $completion;
	    $len = length($value);
	    $widget->set_text($value);
	    $widget->set_position(-1);

	}
	$combo_details->{value} = $value;
	$combo_details->{complete} = $complete;

	# Update the pulldown choices.

	$combo->get_model()->clear();
	foreach $item (@{$combo_details->{list}})
	{
	    $combo->append_text($item) if ($value eq substr($item, 0, $len));
	    $combo_details->{complete} = 1 if (! $complete && $value eq $item);
	}

	# Update the window state on a significant change.

	&{$instance->{update_handler}}($instance, $change_state)
	    if ($combo_details->{complete} != $old_complete
		|| $combo_details->{value} ne $old_value);

    }

    return FALSE;

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

1;

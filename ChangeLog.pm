##############################################################################
#
#   File Name    - ChangeLog.pm
#
#   Description  - The change log module for the mtn-browse application. This
#                  module contains all the routines for implementing the
#                  change log window.
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

# ***** FUNCTIONAL PROTOTYPES FOR THIS FILE *****

# Public routines.

sub display_change_log($$;$$);

# Private routines.

sub get_change_log_window();
#
##############################################################################
#
#   Routine      - display_change_log
#
#   Description  - Display a revision's change log in a window.
#
#   Data         - $mtn         : The Monotone instance handle that is to be
#                                 used to display the change log.
#                  $revision_id : The revision id that is to have its change
#                                 log displayed.
#                  $text_colour : Either the colour of the text or undef or ""
#                                 if the colour should be black.
#                  $tag         : Either a tag name for the specified revision
#                                 that is to be used in the window title
#                                 instead of the revision id or undef if the
#                                 revision id should be used.
#
##############################################################################



sub display_change_log($$;$$)
{

    my($mtn, $revision_id, $text_colour, $tag) = @_;

    my(@certs_list,
       $instance,
       @revision_details);

    $instance = get_change_log_window();
    $instance->{changelog_buffer}->set_text("");
    $instance->{window}->set_title(__x("Revision {rev}",
				       rev => ($tag ? $tag : $revision_id)));
    $mtn->certs(\@certs_list, $revision_id);
    $mtn->get_revision(\@revision_details, $revision_id);
    generate_revision_report($instance->{changelog_buffer},
			     $revision_id,
			     \@certs_list,
			     $text_colour ? $text_colour : "",
			     \@revision_details);
    $instance->{changelog_buffer}->
	place_cursor($instance->{changelog_buffer}->get_start_iter());
    if ($instance->{changelog_scrolledwindow}->realized())
    {
	$instance->{changelog_scrolledwindow}->get_vadjustment()->set_value(0);
	$instance->{changelog_scrolledwindow}->get_hadjustment()->set_value(0);
    }
    $instance->{window}->show_all();

}
#
##############################################################################
#
#   Routine      - get_change_log_window
#
#   Description  - Creates or prepares an existing change log window for use.
#
#   Data         - Return Value : A reference to the newly created or unused
#                                 change log instance record.
#
##############################################################################



sub get_change_log_window()
{

    my($height,
       $instance,
       $width);
    my $window_type = "changelog_window";
    my $wm = WindowManager->instance();

    # Create a new change log window if an unused one wasn't found, otherwise
    # reuse an existing unused one.

    if (! defined($instance = $wm->find_unused($window_type)))
    {
	$instance = {};
	$instance->{glade} = Gtk2::GladeXML->new($glade_file, $window_type);

	# Flag to stop recursive calling of callbacks.

	$instance->{in_cb} = 0;

	# Connect Glade registered signal handlers.

	glade_signal_autoconnect($instance->{glade}, $instance);

	# Get the widgets that we are interested in.

	$instance->{window} = $instance->{glade}->get_widget($window_type);
	foreach my $widget ("changelog_textview",
			    "changelog_scrolledwindow")
	{
	    $instance->{$widget} = $instance->{glade}->get_widget($widget);
	}

	# Setup the changelog window deletion handler.

	$instance->{window}->signal_connect
	    ("delete_event",
	     sub {
		 my($widget, $event, $instance) = @_;
		 return TRUE if ($instance->{in_cb});
		 local $instance->{in_cb} = 1;
		 $widget->hide();
		 $instance->{changelog_buffer}->set_text("");
		 return TRUE;
	     },
	     $instance);

	# Setup the revision changelog viewer.

	$instance->{changelog_buffer} =
	    $instance->{changelog_textview}->get_buffer();
	create_format_tags($instance->{changelog_buffer});
	$instance->{changelog_textview}->modify_font($mono_font);

	# Register the window for management.

	$wm->manage($instance, $window_type, $instance->{window});
	$wm->add_busy_widgets($instance,
			      $instance->{changelog_textview}->
			          get_window("text"));
    }
    else
    {
	$instance->{in_cb} = 0;
	($width, $height) = $instance->{window}->get_default_size();
	$instance->{window}->resize($width, $height);
    }

    # Empty out the contents.

    $instance->{changelog_buffer}->set_text("");

    return $instance;

}

1;

##############################################################################
#
#   File Name    - MultipleRevisions.pm
#
#   Description  - The multple revisions module for the mtn-browse
#                  application. This module contains all the routines for
#                  implementing the multiple revisions dialog window.
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

# ***** FUNCTIONAL PROTOTYPES *****

# Public routines.

sub multiple_revisions_selection($$$@);

# Private routines.

sub get_multiple_revisions_window($);
#
##############################################################################
#
#   Routine      - multiple_revisions_selection
#
#   Description  - Displays the multiple revisions dialog window and allows
#                  the user to select a specific revision.
#
#   Data         - $parent            : The parent window widget for the
#                                       multiple revisions dialog window.
#                  $message           : The message that is to be displayed.
#                  $selected_revision : A reference to a buffer that is to be
#                                       set to either the selected revision
#                                       id, if the user clicks the ok button,
#                                       or undef if the user gives any other
#                                       response.
#                  @revision_ids      : A list of revision ids that the user
#                                       is to select from.
#                  Return Value       : True if a revision has been selected,
#                                       otherwise false.
#
##############################################################################



sub multiple_revisions_selection($$$@)
{

    my($parent, $message, $selected_revision, @revision_ids) = @_;

    my($instance,
       $response);

    $instance = get_multiple_revisions_window($parent);
    local $instance->{in_cb} = 1;

    # Update the message text and the revisions combobox with the supplied
    # information.

    $instance->{message_label}->set_markup($message);
    $instance->{revisions_combobox}->get_model()->clear();
    foreach my $revision_id (@revision_ids)
    {
	$instance->{revisions_combobox}->append_text($revision_id);
    }
    $instance->{revisions_combobox}->set_active(0);

    # Wait for the user to respond.

    WindowManager->instance()->allow_input
	(sub { $response = $instance->{window}->run(); });
    $instance->{window}->hide();

    # Deal with the result, remember that the advanced find button is not a
    # standard dialog button and so gets the default response of 0.

    $response = "advanced-find" if ($response eq "0");
    if ($response eq "ok")
    {
	my $iter;
	$iter = $instance->{revisions_combobox}->get_active_iter();
	$$selected_revision =
	    $instance->{revisions_combobox}->get_model()->get($iter, 0);
    }
    else
    {
	$$selected_revision = undef;
    }

    return $response;

}
#
##############################################################################
#
#   Routine      - get_multiple_revisions_window
#
#   Description  - Creates or prepares an existing multiple revisions dialog
#                  window for use.
#
#   Data         - $parent       : The parent window widget for the multiple
#                                  revisions dialog window.
#                  Return Value  : A reference to the newly created or unused
#                                  multiple revisions instance record.
#
##############################################################################



sub get_multiple_revisions_window($)
{

    my $parent = $_[0];

    my($instance,
       $new);
    my $window_type = "multiple_revisions_window";
    my $wm = WindowManager->instance();

    # Create a new multiple revisions window if an unused one wasn't found,
    # otherwise reuse an existing unused one.

    if (! defined($instance = $wm->find_unused($window_type)))
    {

	my $renderer;

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
	foreach my $widget ("message_label", "revisions_combobox")
	{
	    $instance->{$widget} = $instance->{glade}->get_widget($widget);
	}

	# Setup the revisions combobox.

	$instance->{revisions_combobox}->
	    set_model(Gtk2::ListStore->new("Glib::String"));
	$renderer = Gtk2::CellRendererText->new();
	$instance->{revisions_combobox}->pack_start($renderer, TRUE);
	$instance->{revisions_combobox}->add_attribute($renderer, "text" => 0);

    }
    else
    {
	$instance->{in_cb} = 0;
    }

    local $instance->{in_cb} = 1;

    # Reparent window and display it.

    $instance->{window}->set_transient_for($parent);
    $instance->{window}->show_all();
    $instance->{window}->present();

    # If necessary, register the window for management.

    $wm->manage($instance, $window_type, $instance->{window}) if ($new);

    return $instance;

}

1;

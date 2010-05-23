##############################################################################
#
#   File Name    - ManageServerBookmarks.pm
#
#   Description  - The manage server bookmarks module for the mtn-browse
#                  application. This module contains all the routines for
#                  implementing manage server bookmarks window.
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

my $window_type = "manage_server_bookmarks_window";

# ***** FUNCTIONAL PROTOTYPES *****

# Public routines.

sub manage_server_bookmarks($$);

# Private routines.

sub add_server_button_clicked_cb($$);
sub get_manage_server_bookmarks_window($$);
sub load_servers_treeview($);
sub remove_server_button_clicked_cb($$);
sub server_entry_changed_cb($$);
sub servers_treeselection_changed_cb($$);
#
##############################################################################
#
#   Routine      - manage_server_bookmarks
#
#   Description  - Displays the manage server bookmarks window and then lets
#                  the user change the server bookmark list.
#
#   Data         - $parent      : The parent window widget for the find text
#                                 window.
#                  $bookmarks   : The list of server bookmarks that is to be
#                                 edited.
#                  Return Value : True if the server bookmarks list was
#                                 modified, otherwise false if no changes were
#                                 made.
#
##############################################################################



sub manage_server_bookmarks($$)
{

    my($parent, $bookmarks) = @_;

    my($changed,
       $instance,
       $response);

    # Only go looking for a spare find text window, creating one if necessary,
    # if there isn't one already mapped for the specified textview widget.

    $instance = get_manage_server_bookmarks_window($parent, $bookmarks);
    $response = $instance->{window}->run();
    $instance->{window}->hide();
    if ($response eq "ok")
    {
	$changed = 1;
	@$bookmarks = @{$instance->{server_bookmarks}};
    }
    $instance->{servers_liststore}->clear();
    $instance->{server_bookmarks} = [];

    return $changed;

}
#
##############################################################################
#
#   Routine      - servers_treeselection_changed_cb
#
#   Description  - Callback routine called when the user selects an entry in
#                  the servers treeview in the manage server bookmarks window.
#
#   Data         - $widget   : The widget object that received the signal.
#                  $instance : The window instance that is associated with
#                              this widget.
#
##############################################################################



sub servers_treeselection_changed_cb($$)
{

    my($widget, $instance) = @_;

    return if ($instance->{in_cb});
    local $instance->{in_cb} = 1;

    # Store the details of the newly selected server if one was selected, also
    # enabling the remove button if appropriate.

    if ($widget->count_selected_rows() > 0)
    {
	my($iter,
	   $model);
	($model, $iter) = $widget->get_selected();
	$instance->{selected_server} = $model->get($iter, 0);
	$instance->{remove_server_button}->set_sensitive(TRUE);
    }
    else
    {
	$instance->{selected_server} = undef;
	$instance->{remove_server_button}->set_sensitive(FALSE);
    }

}
#
##############################################################################
#
#   Routine      - server_entry_changed_cb
#
#   Description  - Callback routine called when the user changes the value of
#                  the server entry field in the manage server bookmarks
#                  window.
#
#   Data         - $widget   : The widget object that received the signal.
#                  $instance : The window instance that is associated with
#                              this widget.
#
##############################################################################



sub server_entry_changed_cb($$)
{

    my($widget, $instance) = @_;

    return if ($instance->{in_cb});
    local $instance->{in_cb} = 1;

    $instance->{add_server_button}->set_sensitive
	((length($instance->{server_entry}->get_text()) > 0) ?
	 TRUE : FALSE);

}
#
##############################################################################
#
#   Routine      - add_server_button_clicked_cb
#
#   Description  - Callback routine called when the user clicks on the add
#                  server button in the manage server bookmarks window.
#
#   Data         - $widget   : The widget object that received the signal.
#                  $instance : The window instance that is associated with
#                              this widget.
#
##############################################################################



sub add_server_button_clicked_cb($$)
{

    my($widget, $instance) = @_;

    return if ($instance->{in_cb});
    local $instance->{in_cb} = 1;

    my $server;

    # Check entry to see if it is valid.

    $server = $instance->{server_entry}->get_text();
    if ($server !~ m/^[A-Za-z0-9._-]+(:\d+)?$/)
    {
	my $dialog = Gtk2::MessageDialog->new
	    ($instance->{window},
	     ["modal"],
	     "warning",
	     "close",
	     __x("`{server}' is an invalid server\n"
		 . "name (<server>[:port] is expected).",
		 server => $server));
	$dialog->run();
	$dialog->destroy();
	return;
    }

    # Now check for duplicate entries.

    if (grep(/^\Q$server\E$/, @{$instance->{server_bookmarks}}) > 0)
    {
	my $dialog = Gtk2::MessageDialog->new
	    ($instance->{window},
	     ["modal"],
	     "warning",
	     "close",
	     __x("`{server}' is already entered\ninto your bookmarks list.",
		 server => $server));
	$dialog->run();
	$dialog->destroy();
	return;
    }

    # Ok so add it to the server bookmarks list and reload the servers
    # treeview.

    push(@{$instance->{server_bookmarks}}, $server);
    @{$instance->{server_bookmarks}} = sort(@{$instance->{server_bookmarks}});
    load_servers_treeview($instance);

}
#
##############################################################################
#
#   Routine      - remove_server_button_clicked_cb
#
#   Description  - Callback routine called when the user clicks on the remove
#                  server button in the manage server bookmarks window.
#
#   Data         - $widget   : The widget object that received the signal.
#                  $instance : The window instance that is associated with
#                              this widget.
#
##############################################################################



sub remove_server_button_clicked_cb($$)
{

    my($widget, $instance) = @_;

    return if ($instance->{in_cb});
    local $instance->{in_cb} = 1;

    my $i;

    # Simply remove the selected file name pattern from the list.

    if (defined($instance->{selected_server}))
    {

	# Locate the server and remove it from the list.

	for ($i = 0; $i < scalar(@{$instance->{server_bookmarks}}); ++ $i)
	{
	    last if ($instance->{server_bookmarks}->[$i]
		     eq $instance->{selected_server});
	}
	splice(@{$instance->{server_bookmarks}}, $i, 1);

	# Reload the servers treeview.

	load_servers_treeview($instance);
	$instance->{remove_server_button}->set_sensitive(FALSE);

    }

}
#
##############################################################################
#
#   Routine      - get_manage_server_bookmarks_window
#
#   Description  - Creates or prepares an existing manage server bookmarks
#                  window for use.
#
#   Data         - $parent      : The parent window widget for the manage
#                                 server bookmarks window.
#                  $bookmarks   : The list of server bookmarks that is to be
#                                 edited.
#                  Return Value : A reference to the newly created or unused
#                                 manage server bookmarks instance record.
#
##############################################################################



sub get_manage_server_bookmarks_window($$)
{

    my($parent, $bookmarks) = @_;

    my($instance,
       $new);
    my $wm = WindowManager->instance();

    # Create a new manage server bookmarks window if an unused one wasn't
    # found, otherwise reuse an existing unused one.

    if (! defined($instance = $wm->find_unused($window_type)))
    {

	my($renderer,
	   $tv_column);

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
	foreach my $widget ("servers_treeview",
			    "server_entry",
			    "add_server_button",
			    "remove_server_button")
	{
	    $instance->{$widget} = $instance->{glade}->get_widget($widget);
	}

	# Setup the servers list.

	$instance->{servers_liststore} = Gtk2::ListStore->new("Glib::String");
	$instance->{servers_treeview}->
	    set_model($instance->{servers_liststore});

	$tv_column = Gtk2::TreeViewColumn->new();
	$tv_column->set_sizing("grow-only");
	$renderer = Gtk2::CellRendererText->new();
	$tv_column->pack_start($renderer, TRUE);
	$tv_column->set_attributes($renderer, "text" => 0);
	$instance->{servers_treeview}->append_column($tv_column);

	$instance->{servers_treeview}->set_search_column(0);
	$instance->{servers_treeview}->
	    set_search_equal_func(\&treeview_column_searcher);

	$instance->{servers_treeview}->get_selection()->
	    signal_connect("changed",
			   \&servers_treeselection_changed_cb,
			   $instance);

    }

    local $instance->{in_cb} = 1;

    $instance->{selected_server} = undef;
    $instance->{server_bookmarks} = [];

    # Disable the add and remove buttons and make sure the server entry field
    # is empty.

    $instance->{server_entry}->set_text("");
    $instance->{add_server_button}->set_sensitive(FALSE);
    $instance->{remove_server_button}->set_sensitive(FALSE);

    # Reparent window and display it.

    $instance->{window}->set_transient_for($parent);
    $instance->{window}->show_all();
    $instance->{window}->present();

    # Load in the server bookmarks.

    @{$instance->{server_bookmarks}} = @$bookmarks;
    load_servers_treeview($instance);

    # Make sure that the server entry field has the focus.

    $instance->{server_entry}->grab_focus();
    $instance->{server_entry}->set_position(-1);

    # If necessary, register the window for management and set up the help
    # callbacks.

    if ($new)
    {
	$wm->manage($instance, $window_type, $instance->{window});
	register_help_callbacks
	    ($instance,
	     {widget   => undef,
	      help_ref => __("mtnb-upc-the-manage-server-bookmarks-dialog-"
			     . "window")});
    }

    return $instance;

}
#
##############################################################################
#
#   Routine      - load_servers_treeview
#
#   Description  - Load up the servers treeview with the current server
#                  bookmarks.
#
#   Data         - $instance : The associated window instance.
#
##############################################################################



sub load_servers_treeview($)
{

    my $instance = $_[0];

    # Load up the server bookmarks treeview.

    $instance->{servers_liststore}->clear();
    foreach my $pattern (@{$instance->{server_bookmarks}})
    {
	$instance->{servers_liststore}->
	    set($instance->{servers_liststore}->append(),
		0,
		$pattern);
    }
    $instance->{servers_treeview}->scroll_to_point(0, 0)
	if ($instance->{servers_treeview}->realized());

}

1;

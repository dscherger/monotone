##############################################################################
#
#   File Name    - FindText.pm
#
#   Description  - The find text module for the mtn-browse application. This
#                  module contains all the routines for implementing find text
#                  window.
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

# ***** GLOBAL DATA DECLARATIONS *****

# The type of window that is going to be managed by this module.

my $window_type = "find_text_window";

# ***** FUNCTIONAL PROTOTYPES FOR THIS FILE *****

# Public routines.

sub disable_find_text($$);
sub find_text($$);
sub reset_find_text($);

# Private routines.

sub find_comboboxentry_changed_cb($$);
sub find_current_window($);
sub get_find_text_window($$);
#
##############################################################################
#
#   Routine      - find_text
#
#   Description  - Display the find text window associated with the specified
#                  textview widget, creating one if necessary, and allow the
#                  user to search through the related text buffer.
#
#   Data         - $parent    : The parent window widget for the find text
#                               window.
#                  $text_view : The textview widget that is to be searched.
#
##############################################################################



sub find_text($$)
{

    my($parent, $text_view) = @_;

    # Only go looking for a spare find text window, creating one if necessary,
    # if there isn't one already mapped for the specified textview widget.

    get_find_text_window($parent, $text_view)
	if (! defined(find_current_window($text_view)));

}
#
##############################################################################
#
#   Routine      - reset_find_text
#
#   Description  - Resets the search context for the find text window
#                  associated with the specified textview widget.
#
#   Data         - $text_view : The textview widget to which the find text
#                               window is associated.
#
##############################################################################



sub reset_find_text($)
{

    my $text_view = $_[0];

    my $instance;

    # Simply reset the search context for the found find text window.

    $instance->{match_offset_start} = $instance->{match_offset_end} = -1
	if (defined($instance = find_current_window($text_view)));

}
#
##############################################################################
#
#   Routine      - disable_find_text
#
#   Description  - Disables or enables the find text window associated with
#                  the specified textview widget.
#
#   Data         - $text_view : The textview widget to which the find text
#                               window is associated.
#                  $disable   : True if the window is to be disabled,
#                               otherwise false if it is to be enabled.
#
##############################################################################



sub disable_find_text($$)
{

    my($text_view, $disable) = @_;

    my $instance;

    # Simply disable/enable the found find text window.

    if (defined($instance = find_current_window($text_view)))
    {
	if ($disable)
	{
	    $instance->{main_vbox}->set_sensitive(FALSE);
	    $instance->{find_button}->set_sensitive(FALSE);
	}
	else
	{
	    $instance->{main_vbox}->set_sensitive(TRUE);
	    $instance->{find_button}->set_sensitive
		((length($instance->{find_comboboxentry}->child()->get_text())
		  > 0) ?
		 TRUE : FALSE);
	}
    }

}
#
##############################################################################
#
#   Routine      - find_button_clicked_cb
#
#   Description  - Callback routine called when the user clicks on the find
#                  button in the find text window.
#
#   Data         - $widget   : The widget object that received the signal.
#                  $instance : The window instance that is associated with
#                              this widget.
#
##############################################################################



sub find_button_clicked_cb($$)
{

    my($widget, $instance) = @_;

    return if ($instance->{in_cb});
    local $instance->{in_cb} = 1;

    my($case_sensitive,
       $done,
       $end_iter,
       $expr,
       $forward,
       $found,
       $line,
       $rect,
       $search_term,
       $start_iter);

    $search_term = $instance->{find_comboboxentry}->child()->get_text();

    # Store the search term in the history.

    $found = 0;
    foreach my $entry (@{$instance->{search_history}})
    {
	if ($entry eq $search_term)
	{
	    $found = 1;
	    last;
	}
    }
    if (! $found)
    {
	if (unshift(@{$instance->{search_history}}, $search_term) > 20)
	{
	    pop(@{$instance->{search_history}});
	}
	$instance->{find_comboboxentry}->get_model()->clear();
	foreach my $entry (@{$instance->{search_history}})
	{
	    $instance->{find_comboboxentry}->append_text($entry);
	}
    }

    # Get the search parameters.

    $case_sensitive = $instance->{case_sensitive_checkbutton}->get_active();
    $forward = ! $instance->{search_backwards_checkbutton}->get_active();

    # Work out where to start searching from.

    $rect = $instance->{text_view}->get_visible_rect();
    $done = 0;
    if ($search_term eq $instance->{old_search_term}
	&& $instance->{old_y} == $rect->y()
	&& $instance->{match_offset_start} >= 0)
    {

	# Resume searching from where we left off. Adjust the iters so as to
	# encompass the remaining text on the line, or if there isn't any then
	# move them to the next/previous line.

	if ($forward)
	{
	    $start_iter = $instance->{text_buffer}->
		get_iter_at_offset($instance->{match_offset_end});
	    $end_iter = $instance->{text_buffer}->
		get_iter_at_offset($instance->{match_offset_end});
	    if ($start_iter->ends_line())
	    {
		$done = ! $start_iter->forward_line();
		$end_iter->forward_to_line_end();
	    }
	    else
	    {
		$end_iter->forward_to_line_end();
	    }
	}
	else
	{
	    $start_iter = $instance->{text_buffer}->
		get_iter_at_offset($instance->{match_offset_start});
	    $end_iter = $instance->{text_buffer}->
		get_iter_at_offset($instance->{match_offset_start});
	    if ($start_iter->starts_line())
	    {
		$done = ! $start_iter->backward_line();
		$end_iter->backward_line();
		$end_iter->forward_to_line_end()
		    unless ($end_iter->ends_line());
	    }
	    else
	    {
		$start_iter->backward_line();
	    }
	}

    }
    else
    {

	# Start searching from the visible part of the file.

	my $y;

	if ($forward)
	{
	    $y = $rect->y();
	}
	else
	{
	    $y = $rect->y() + $rect->height();
	}
	$start_iter = ($instance->{text_view}->get_line_at_y($y))[0];
	$end_iter = ($instance->{text_view}->get_line_at_y($y))[0];
	$end_iter->forward_to_line_end();

    }

    # Precompile the regular expression based upon the search term.

    if ($case_sensitive)
    {
	$expr = qr/\Q$search_term\E/;
    }
    else
    {
	$expr = qr/\Q$search_term\E/i;
    }

    # Search for the text.

    $found = 0;
    while (! $done)
    {
	$line =
	    $instance->{text_buffer}->get_text($start_iter, $end_iter, TRUE);
	if ($line =~ m/$expr/g)
	{
	    $instance->{match_offset_start} =
		$start_iter->get_offset() + pos($line) - length($search_term);
	    $instance->{match_offset_end} =
		$start_iter->get_offset() + pos($line);
	    $done = $found = 1;
	}
	else
	{
	    if ($forward)
	    {
		$done = ! $start_iter->forward_line();
		$end_iter->forward_to_line_end();
	    }
	    else
	    {
		$done = ! $start_iter->backward_line();
		$end_iter->backward_line();
		$end_iter->forward_to_line_end()
		    unless ($end_iter->ends_line());
	    }
	}
    }

    # Either select the found text or tell the user that nothing could be
    # found.

    if ($found)
    {
	my $start_line_iter;
	$start_iter = $instance->{text_buffer}->
	    get_iter_at_offset($instance->{match_offset_start});
	$start_line_iter = $instance->{text_buffer}->
	    get_iter_at_offset($instance->{match_offset_start});
	$start_line_iter->backward_line()
	    unless $start_line_iter->starts_line();
	$end_iter = $instance->{text_buffer}->
	    get_iter_at_offset($instance->{match_offset_end});
	$instance->{text_buffer}->select_range($start_iter, $end_iter);
	$instance->{text_view}->scroll_to_iter
	    ($start_line_iter, 0, FALSE, 0, 0);
	$instance->{text_view}->scroll_to_iter($end_iter, 0, FALSE, 0, 0);
    }
    else
    {
	my $dialog;
	$dialog = Gtk2::MessageDialog->new
	    ($instance->{window},
	     ["modal"],
	     "info",
	     "close",
	     sprintf("Could not find\n`%s'.", $search_term));
	$dialog->run();
	$dialog->destroy();
    }

    $rect = $instance->{text_view}->get_visible_rect();
    $instance->{old_y} = $rect->y();
    $instance->{old_search_term} = $search_term;

}
#
##############################################################################
#
#   Routine      - find_comboboxentry_changed_cb
#
#   Description  - Callback routine called when the user changes the value of
#                  the find text comboboxentry by selecting an entry from its
#                  pulldown list or entering text directly.
#
#   Data         - $widget   : The widget object that received the signal.
#                  $instance : The window instance that is associated with
#                              this widget.
#
##############################################################################



sub find_comboboxentry_changed_cb($$)
{

    my($widget, $instance) = @_;

    return if ($instance->{in_cb});
    local $instance->{in_cb} = 1;

    $instance->{find_button}->set_sensitive
	((length($instance->{find_comboboxentry}->child()->get_text()) > 0) ?
	 TRUE : FALSE);

}
#
##############################################################################
#
#   Routine      - find_current_window
#
#   Description  - Look for a find text window that is mapped and belongs to
#                  the specified textview widget.
#
#   Data         - $text_view : The textview widget that is to be searched.
#
##############################################################################



sub find_current_window($)
{

    my $text_view = $_[0];

    return WindowManager->instance()->cond_find
	($window_type,
	 sub {
	     my $instance = $_[0];
	     return 1
		 if ($instance->{window}->mapped()
		     && $instance->{text_view} == $text_view);
	     return;
	 });

}
#
##############################################################################
#
#   Routine      - get_find_text_window
#
#   Description  - Creates or prepares an existing find text window for use.
#
#   Data         - $parent      : The parent window widget for the find text
#                                 window.
#                  $text_view   : The textview widget that is to be searched.
#                  Return Value : A reference to the newly created or unused
#                                 find text instance record.
#
##############################################################################



sub get_find_text_window($$)
{

    my($parent, $text_view) = @_;

    my($instance,
       $new);
    my $wm = WindowManager->instance();

    # Create a new find text window if an unused one wasn't found, otherwise
    # reuse an existing unused one.

    if (! defined($instance = $wm->find_unused($window_type)))
    {
	$new = 1;
	$instance = {};
	$instance->{glade} = Gtk2::GladeXML->new($glade_file, $window_type);

	# Flag to stop recursive calling of callbacks.

	$instance->{in_cb} = 0;

	# Connect Glade registered signal handlers.

	glade_signal_autoconnect($instance->{glade}, $instance);

	# Get the widgets that we are interested in.

	$instance->{window} = $instance->{glade}->get_widget($window_type);
	foreach my $widget ("main_vbox",
			    "find_comboboxentry",
			    "case_sensitive_checkbutton",
			    "search_backwards_checkbutton",
			    "find_button")
	{
	    $instance->{$widget} = $instance->{glade}->get_widget($widget);
	}

	# Setup the find text window deletion handlers.

	$instance->{window}->signal_connect
	    ("delete_event",
	     sub {
		 my($widget, $event, $instance) = @_;
		 return TRUE if ($instance->{in_cb});
		 local $instance->{in_cb} = 1;
		 $instance->{window}->hide();
		 return TRUE;
	     },
	     $instance);
	$instance->{glade}->get_widget("close_button")->signal_connect
	    ("clicked",
	     sub {
		 my($widget, $instance) = @_;
		 return TRUE if ($instance->{in_cb});
		 local $instance->{in_cb} = 1;
		 $instance->{window}->hide();
	     },
	     $instance);

	# Setup the comboboxentry changed signal handler.

	$instance->{find_comboboxentry}->child()->
	    signal_connect("changed",
			   \&find_comboboxentry_changed_cb,
			   $instance);

	# Setup the combobox.

	$instance->{find_comboboxentry}->
	    set_model(Gtk2::ListStore->new("Glib::String"));
	$instance->{find_comboboxentry}->set_text_column(0);
	$instance->{search_history} = [];
    }
    else
    {
	$new = 0;
	$instance->{in_cb} = 0;
	$instance->{main_vbox}->set_sensitive(TRUE);
    }

    # Reset the search context.

    $instance->{match_offset_start} = $instance->{match_offset_end} = -1;
    $instance->{old_y} = 0;
    $instance->{old_search_term} = "";

    # Make sure the find button is only enabled when there is something entered
    # into the comboboxentry widget.

    $instance->{find_button}->set_sensitive
	((length($instance->{find_comboboxentry}->child()->get_text()) > 0) ?
	 TRUE : FALSE);

    # Store the associated textview and text buffer.

    $instance->{text_view} = $text_view;
    $instance->{text_buffer} = $instance->{text_view}->get_buffer();

    # Reparent window and display it.

    $instance->{window}->set_transient_for($parent);
    $instance->{window}->show_all();

    # If necessary, register the window for management.

    $wm->manage($instance, $window_type, $instance->{window}) if ($new);

    return $instance;

}

1;

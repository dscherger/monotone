##############################################################################
#
#   File Name    - History.pm
#
#   Description  - The history module for the mtn-browse application. This
#                  module contains all the routines for implementing the
#                  revision and file history windows.
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

# Constants for the columns within the comparison files ListStore widget.

use constant CLS_FILE_NAME_COLUMN => 0;
use constant CLS_LINE_NR_COLUMN   => 1;
use constant CLS_FILE_ID_1_COLUMN => 2;
use constant CLS_FILE_ID_2_COLUMN => 3;

# ***** FUNCTIONAL PROTOTYPES *****

# Public routines.

sub display_file_change_history($$$);
sub display_revision_change_history($$);

# Private routines.

sub compare_button_clicked_cb($$);
sub compare_revisions($$$;$);
sub comparison_revision_change_log_button_clicked_cb($$);
sub external_diffs_button_clicked_cb($$);
sub file_comparison_combobox_changed_cb($$);
sub get_file_history_helper($$$);
sub get_history_window();
sub get_revision_comparison_window();
sub get_revision_history_helper($$$);
sub history_list_button_clicked_cb($$);
sub mtn_diff($$$$;$);
sub save_differences_button_clicked_cb($$);
#
##############################################################################
#
#   Routine      - display_revision_change_history
#
#   Description  - Display a revision's change history, complete with
#                  selection and comparison buttons.
#
#   Data         - $mtn         : The Monotone instance handle that is to be
#                                 used to get the change history.
#                  $revision_id : The id of the revision that is to have its
#                                 change log displayed.
#
##############################################################################



sub display_revision_change_history($$)
{

    my($mtn, $revision_id) = @_;

    my($button,
       @certs_list,
       $counter,
       %history_hash,
       $instance);
    my $wm = WindowManager->instance();

    $instance = get_history_window();
    local $instance->{in_cb} = 1;

    $instance->{mtn} = $mtn;
    $instance->{file_name} = undef;
    $instance->{first_revision_id} = "";
    $instance->{second_revision_id} = "";
    $instance->{window}->set_title(__x("Revision History For {rev}",
				       rev => $revision_id));
    $instance->{history_label}->set_markup(__("<b>Revision History</b>"));
    $instance->{window}->show_all();

    $wm->make_busy($instance, 1);
    $instance->{appbar}->push("");
    $wm->update_gui();

    $instance->{stop_button}->set_sensitive(TRUE);

    # Get the list of file change revisions. Remember to include the current
    # revision in the history.

    $instance->{appbar}->set_progress_percentage(0);
    $instance->{appbar}->set_status(__("Fetching revision list"));
    $wm->update_gui();
    $history_hash{$revision_id} = 1;
    get_revision_history_helper($instance, \%history_hash, $revision_id);

    # Sort the list.

    $instance->{appbar}->set_status(__("Sorting revision list"));
    $wm->update_gui();
    $instance->{history} = [];
    $instance->{mtn}->toposort($instance->{history}, keys(%history_hash));
    %history_hash = ();
    @{$instance->{history}} = reverse(@{$instance->{history}});

    # Display the file's history.

    $instance->{appbar}->set_progress_percentage(0);
    $instance->{appbar}->set_status(__("Displaying revision history"));
    $wm->update_gui();
    $counter = 0;
    $instance->{stop} = 0;
    $instance->{history_buffer}->set_text("");
    for my $revision_id (@{$instance->{history}})
    {

	++ $counter;

	# Print out the revision summary.

	$instance->{mtn}->certs(\@certs_list, $revision_id);
	generate_revision_report($instance->{history_buffer},
				 $revision_id,
				 \@certs_list,
				 "");
	$instance->{history_buffer}->
	    insert($instance->{history_buffer}->get_end_iter(), "\n\n ");

	# Add the buttons.

	$button = Gtk2::Button->new(__("Select As Id 1"));
	$button->signal_connect("clicked",
				\&history_list_button_clicked_cb,
				{instance    => $instance,
				 revision_id => $revision_id,
				 button_type => "1"});
	$tooltips->set_tip($button,
			   __("Select this revision for comparison\n")
			       . __("as the first revision"));
	$instance->{history_textview}->add_child_at_anchor
	    ($button,
	     $instance->{history_buffer}->
	         create_child_anchor($instance->{history_buffer}->
				     get_end_iter()));
	$button->show_all();
	$instance->{history_buffer}->
	    insert($instance->{history_buffer}->get_end_iter(), " ");

	$button = Gtk2::Button->new(__("Select As Id 2"));
	$button->signal_connect("clicked",
				\&history_list_button_clicked_cb,
				{instance    => $instance,
				 revision_id => $revision_id,
				 button_type => "2"});
	$tooltips->set_tip($button,
			   __("Select this revision for comparison\n")
			       . __("as the second revision"));
	$instance->{history_textview}->add_child_at_anchor
	    ($button,
	     $instance->{history_buffer}->
	         create_child_anchor($instance->{history_buffer}->
				     get_end_iter()));
	$button->show_all();
	$instance->{history_buffer}->
	    insert($instance->{history_buffer}->get_end_iter(), " ");

	$button = Gtk2::Button->new(__("Browse Revision"));
	$button->signal_connect("clicked",
				\&history_list_button_clicked_cb,
				{instance    => $instance,
				 revision_id => $revision_id,
				 button_type => "browse-revision"});
	$tooltips->set_tip($button,
			   __("Browse the revision in\na new browser window"));
	$instance->{history_textview}->add_child_at_anchor
	    ($button,
	     $instance->{history_buffer}->
	         create_child_anchor($instance->{history_buffer}->
				     get_end_iter()));
	$button->show_all();
	$instance->{history_buffer}->
	    insert($instance->{history_buffer}->get_end_iter(), " ");

	$button = Gtk2::Button->new(__("Full Change Log"));
	$button->signal_connect("clicked",
				\&history_list_button_clicked_cb,
				{instance    => $instance,
				 revision_id => $revision_id,
				 button_type => "revision-changelog"});
	$tooltips->set_tip($button, __("View the revision's full change log"));
	$instance->{history_textview}->add_child_at_anchor
	    ($button,
	     $instance->{history_buffer}->
	         create_child_anchor($instance->{history_buffer}->
				     get_end_iter()));
	$button->show_all();

	if ($counter % 100 == 0)
	{
	    $instance->{appbar}->set_progress_percentage
		($counter / scalar(@{$instance->{history}}));
	    $wm->update_gui();
	}

	# Stop if the user wants to.

	last if ($instance->{stop});

	# If we aren't at the end, print out the revision separator.

	if ($counter < scalar(@{$instance->{history}}))
	{
	    $instance->{history_buffer}->
		insert($instance->{history_buffer}->get_end_iter(), "\n");
	    $instance->{history_buffer}->
		insert_pixbuf($instance->{history_buffer}->get_end_iter(),
			      $line_image);
	    $instance->{history_buffer}->
		insert($instance->{history_buffer}->get_end_iter(), "\n");
	}

    }

    $instance->{stop_button}->set_sensitive(FALSE);
    set_label_value($instance->{numbers_value_label}, $counter)
	if ($instance->{stop});

    # Make sure we are at the top.

    $instance->{history_buffer}->
	place_cursor($instance->{history_buffer}->get_start_iter());
    $instance->{history_scrolledwindow}->get_vadjustment()->set_value(0);
    $instance->{history_scrolledwindow}->get_hadjustment()->set_value(0);
    $instance->{appbar}->set_progress_percentage(0);
    $instance->{appbar}->set_status("");
    $wm->update_gui();

    $instance->{appbar}->pop();
    $wm->make_busy($instance, 0);

}
#
##############################################################################
#
#   Routine      - display_file_change_history
#
#   Description  - Display a file's change history, complete with selection
#                  and comparison buttons.
#
#   Data         - $mtn         : The Monotone instance handle that is to be
#                                 used to get the change history.
#                  $revision_id : The revision id in which the desired version
#                                 of the file resides.
#                  $file_name   : The name of the file that is to have its
#                                 change log displayed.
#
##############################################################################



sub display_file_change_history($$$)
{

    my($mtn, $revision_id, $file_name) = @_;

    my($button,
       @certs_list,
       $counter,
       %history_hash,
       $instance);
    my $wm = WindowManager->instance();

    $instance = get_history_window();
    local $instance->{in_cb} = 1;

    $instance->{mtn} = $mtn;
    $instance->{file_name} = $file_name;
    $instance->{first_revision_id} = "";
    $instance->{second_revision_id} = "";
    $instance->{window}->set_title(__x("File History For {file}",
				       file=> $instance->{file_name}));
    $instance->{history_label}->set_markup(__("<b>File History</b>"));
    $instance->{window}->show_all();

    $wm->make_busy($instance, 1);
    $instance->{appbar}->push("");
    $wm->update_gui();

    # Get the list of file change revisions. Remember that a warning is
    # generated when one goes back beyond a file's addition revision, so
    # temporarily disable the warning handler.

    $instance->{appbar}->set_progress_percentage(0);
    $instance->{appbar}->set_status(__("Fetching revision list"));
    $instance->{stop_button}->set_sensitive(TRUE);
    $wm->update_gui();
    Monotone::AutomateStdio->register_error_handler("warning");
    get_file_history_helper($instance, \%history_hash, $revision_id);
    Monotone::AutomateStdio->register_error_handler("both",
						    \&mtn_error_handler);
    $instance->{stop_button}->set_sensitive(FALSE);

    # Sort the list.

    $instance->{appbar}->set_status(__("Sorting revision list"));
    $wm->update_gui();
    $instance->{history} = [];
    $instance->{mtn}->toposort($instance->{history}, keys(%history_hash));
    %history_hash = ();
    @{$instance->{history}} = reverse(@{$instance->{history}});

    # Display the file's history.

    $instance->{appbar}->set_progress_percentage(0);
    $instance->{appbar}->set_status(__("Displaying file history"));
    $wm->update_gui();
    $counter = 0;
    $instance->{history_buffer}->set_text("");
    for my $revision_id (@{$instance->{history}})
    {

	++ $counter;

	# Print out the revision summary.

	$instance->{mtn}->certs(\@certs_list, $revision_id);
	generate_revision_report($instance->{history_buffer},
				 $revision_id,
				 \@certs_list,
				 "");
	$instance->{history_buffer}->
	    insert($instance->{history_buffer}->get_end_iter(), "\n\n ");

	# Add the buttons.

	$button = Gtk2::Button->new(__("Select As Id 1"));
	$button->signal_connect("clicked",
				\&history_list_button_clicked_cb,
				{instance    => $instance,
				 revision_id => $revision_id,
				 button_type => "1"});
	$tooltips->set_tip($button,
			   __("Select this file revision for\n")
			       . __("comparison as the first file"));
	$instance->{history_textview}->add_child_at_anchor
	    ($button,
	     $instance->{history_buffer}->
	         create_child_anchor($instance->{history_buffer}->
				     get_end_iter()));
	$button->show_all();
	$instance->{history_buffer}->
	    insert($instance->{history_buffer}->get_end_iter(), " ");

	$button = Gtk2::Button->new(__("Select As Id 2"));
	$button->signal_connect("clicked",
				\&history_list_button_clicked_cb,
				{instance    => $instance,
				 revision_id => $revision_id,
				 button_type => "2"});
	$tooltips->set_tip($button,
			   __("Select this file revision for\n")
			       . __("comparison as the second file"));
	$instance->{history_textview}->add_child_at_anchor
	    ($button,
	     $instance->{history_buffer}->
	         create_child_anchor($instance->{history_buffer}->
				     get_end_iter()));
	$button->show_all();
	$instance->{history_buffer}->
	    insert($instance->{history_buffer}->get_end_iter(), " ");

	$button = Gtk2::Button->new(__("Browse File"));
	$button->signal_connect("clicked",
				\&history_list_button_clicked_cb,
				{instance    => $instance,
				 revision_id => $revision_id,
				 button_type => "browse-file"});
	$tooltips->set_tip($button,
			   __("Browse the file in\na new browser window"));
	$instance->{history_textview}->add_child_at_anchor
	    ($button,
	     $instance->{history_buffer}->
	         create_child_anchor($instance->{history_buffer}->
				     get_end_iter()));
	$button->show_all();
	$instance->{history_buffer}->
	    insert($instance->{history_buffer}->get_end_iter(), " ");

	$button = Gtk2::Button->new(__("Full Change Log"));
	$button->signal_connect("clicked",
				\&history_list_button_clicked_cb,
				{instance    => $instance,
				 revision_id => $revision_id,
				 button_type => "revision-changelog"});
	$tooltips->set_tip($button, __("View the revision's full change log"));
	$instance->{history_textview}->add_child_at_anchor
	    ($button,
	     $instance->{history_buffer}->
	         create_child_anchor($instance->{history_buffer}->
				     get_end_iter()));
	$button->show_all();

	# If we aren't at the end, print out the revision separator.

	if ($counter < scalar(@{$instance->{history}}))
	{
	    $instance->{history_buffer}->
		insert($instance->{history_buffer}->get_end_iter(), "\n");
	    $instance->{history_buffer}->
		insert_pixbuf($instance->{history_buffer}->get_end_iter(),
			      $line_image);
	    $instance->{history_buffer}->
		insert($instance->{history_buffer}->get_end_iter(), "\n");
	}

	$instance->{appbar}->set_progress_percentage
	    ($counter / scalar(@{$instance->{history}}));
	$wm->update_gui();

    }

    # Make sure we are at the top.

    $instance->{history_buffer}->
	place_cursor($instance->{history_buffer}->get_start_iter());
    $instance->{history_scrolledwindow}->get_vadjustment()->set_value(0);
    $instance->{history_scrolledwindow}->get_hadjustment()->set_value(0);
    $instance->{appbar}->set_progress_percentage(0);
    $instance->{appbar}->set_status("");
    $wm->update_gui();

    $instance->{appbar}->pop();
    $wm->make_busy($instance, 0);

}
#
##############################################################################
#
#   Routine      - history_list_button_clicked_cb
#
#   Description  - Callback routine called when the user clicks on any of the
#                  buttons displayed in the history list in a history window.
#
#   Data         - $widget  : The widget object that received the signal.
#                  $details : A reference to an anonymous hash containing the
#                             window instance, revision and action that is
#                             associated with this widget.
#
##############################################################################



sub history_list_button_clicked_cb($$)
{

    my($widget, $details) = @_;

    my($instance,
       $revision_id);

    $instance = $details->{instance};
    $revision_id = $details->{revision_id};

    return if ($instance->{in_cb});
    local $instance->{in_cb} = 1;

    if ($details->{button_type} eq "1" || $details->{button_type} eq "2")
    {
	if ($details->{button_type} eq "1")
	{
	    $instance->{first_revision_id} = $revision_id;
	    set_label_value($instance->{revision_id_1_value_label},
			    $revision_id);
	    if ($instance->{first_revision_id}
		eq $instance->{second_revision_id})
	    {
		$instance->{second_revision_id} = "";
		set_label_value($instance->{revision_id_2_value_label}, "");
	    }
	}
	else
	{
	    $instance->{second_revision_id} = $revision_id;
	    set_label_value($instance->{revision_id_2_value_label},
			    $revision_id);
	    if ($instance->{second_revision_id}
		eq $instance->{first_revision_id})
	    {
		$instance->{first_revision_id} = "";
		set_label_value($instance->{revision_id_1_value_label}, "");
	    }
	}
	if ($instance->{first_revision_id} ne ""
	    && $instance->{second_revision_id} ne "")
	{
	    $instance->{compare_button}->set_sensitive(TRUE);
	}
	else
	{
	    $instance->{compare_button}->set_sensitive(FALSE);
	}
    }
    elsif ($details->{button_type} eq "browse-revision")
    {

	my($branch,
	   @certs_list);

	# First find out what branch the revision is on (take the first one).

	$instance->{mtn}->certs(\@certs_list, $revision_id);
	$branch = "";
	foreach my $cert (@certs_list)
	{
	    if ($cert->{name} eq "branch")
	    {
		$branch = $cert->{value};
		last;
	    }
	}

	# Get a new browser window preloaded with the desired file.

	get_browser_window($instance->{mtn}, $branch, $revision_id);

    }
    elsif ($details->{button_type} eq "browse-file")
    {

	my($branch,
	   @certs_list,
	   $dir,
	   $file);

	# First find out what branch the revision is on (take the first one).

	$instance->{mtn}->certs(\@certs_list, $revision_id);
	$branch = "";
	foreach my $cert (@certs_list)
	{
	    if ($cert->{name} eq "branch")
	    {
		$branch = $cert->{value};
		last;
	    }
	}

	# Split the file name into directory and file components.

	$dir = dirname($instance->{file_name});
	$dir = "" if ($dir eq ".");
	$file = basename($instance->{file_name});

	# Get a new browser window preloaded with the desired file.

	get_browser_window($instance->{mtn},
			   $branch,
			   $revision_id,
			   $dir,
			   $file);

    }
    else
    {

	# Display the full revision change log.

	display_change_log($instance->{mtn}, $revision_id);

    }

}
#
##############################################################################
#
#   Routine      - compare_button_clicked_cb
#
#   Description  - Callback routine called when the user clicks on the
#                  revision comparison button in a history window.
#
#   Data         - $widget   : The widget object that received the signal.
#                  $instance : The window instance that is associated with
#                              this widget.
#
##############################################################################



sub compare_button_clicked_cb($$)
{

    my($widget, $instance) = @_;

    return if ($instance->{in_cb});
    local $instance->{in_cb} = 1;

    my @revision_ids;

    # Sort the revisions by date, oldest first.

    @revision_ids = ($instance->{first_revision_id},
		     $instance->{second_revision_id});
    $instance->{mtn}->toposort(\@revision_ids, @revision_ids);

    # Now compare them.

    compare_revisions($instance->{mtn},
		      $revision_ids[0],
		      $revision_ids[1],
		      $instance->{file_name});

}
#
##############################################################################
#
#   Routine      - compare_revisions
#
#   Description  - Compares and then displays the differeneces between the two
#                  specified revisions, optionally restricting it to the
#                  specified file.
#
#   Data         - $mtn           : The Monotone instance handle that is to be
#                                   used to do the comparison.
#                  $revision_id_1 : The first revision id that is to be
#                                   compared.
#                  $revision_id_2 : The second revision id that is to be
#                                   compared.
#                  $file_name     : Either the name of the file that the
#                                   revision comparison should be restricted
#                                   to or undef for a full revision
#                                   comparison.
#
##############################################################################



sub compare_revisions($$$;$)
{

    my($mtn, $revision_id_1, $revision_id_2, $file_name) = @_;

    my(@files,
       $i,
       $instance,
       $iter);
    my $wm = WindowManager->instance();

    $instance = get_revision_comparison_window();
    local $instance->{in_cb} = 1;

    $instance->{window}->
	set_title(__x("Differences Between Revisions {rev_1} and {rev_2}",
		      rev_1 => $revision_id_1,
		      rev_2 => $revision_id_2));
    if (defined($file_name))
    {
	$instance->{comparison_label}->
	    set_markup(__("<b>File Comparison</b>"));
    }
    else
    {
	$instance->{comparison_label}->
	    set_markup(__("<b>Revision Comparison</b>"));
    }
    $instance->{window}->show_all();

    $wm->make_busy($instance, 1);
    $instance->{appbar}->push("");
    $wm->update_gui();

    $instance->{mtn} = $mtn;
    $instance->{revision_id_1} = $revision_id_1;
    $instance->{revision_id_2} = $revision_id_2;

    # Get Monotone to do the comparison.

    $instance->{appbar}->set_status(__("Calculating differences"));
    $wm->update_gui();
    mtn_diff($instance->{diff_output},
	     $mtn->get_db_name(),
	     $revision_id_1,
	     $revision_id_2,
	     $file_name);

    $instance->{stop_button}->set_sensitive(TRUE);

    # Does the user want pretty printed differences output?

    if ($user_preferences->{coloured_diffs})
    {

	my($char,
	   $file_id_1,
	   $file_id_2,
	   $is_binary,
	   $len,
	   $line,
	   @lines,
	   $max_len,
	   $name,
	   $padding,
	   $rest,
	   $separator);

	# Yes the user wants pretty printed differences output.

	@lines = @{$instance->{diff_output}};

	# Find the longest line for future padding, having expanded tabs
	# (except for file details lines as tab is used as a separator (these
	# are expanded later)).

	$max_len = $separator = 0;
	foreach my $line (@lines)
	{
	    if ($line =~ m/^==/o)
	    {
		$separator = 1;
	    }
	    elsif ($separator)
	    {
		$rest = expand(substr($line, 3)) . " ";
		$max_len = $len if (($len = length($rest)) > $max_len);
		$separator = 0 if ($line =~ m/^\+\+\+ .+\s+[0-9a-f]{40}$/o);
	    }
	    else
	    {
		($char, $rest) = unpack("a1a*", $line);
		$rest =~ s/\s+$//o;
		$rest = expand($rest);
		$max_len = $len if (($len = length($rest)) > $max_len);
		$line = $char . $rest;
	    }
	}

	# Display the result, highlighting according to the diff output.
	# Remember the first two lines are just empty comment lines.

	$instance->{appbar}->
	    set_status(__("Formatting and displaying differences"));
	$wm->update_gui();
	$padding = " " x $max_len;
	$line = substr(" Summary" . $padding, 0, $max_len);
	$instance->{comparison_buffer}->insert_with_tags_by_name
	    ($instance->{comparison_buffer}->get_end_iter(),
	     $line . "\n",
	     "compare-info");
	for ($i = 1; $i <= $#lines; ++ $i)
	{

	    # Deal with the initial comment lines that summarise the entire set
	    # of differences between the revisions.

	    if ($lines[$i] =~ m/^\#/o)
	    {
		$line = substr($lines[$i], 1);
		$instance->{comparison_buffer}->insert
		    ($instance->{comparison_buffer}->get_end_iter(),
		     $line . "\n");
	    }

	    # Deal with lines that introduce a new file comparison.

	    elsif ($lines[$i] =~ m/^==/o)
	    {

		# Check for aborts.

		last if ($instance->{stop});

		# Print separator.

		$instance->{comparison_buffer}->
		    insert_pixbuf($instance->{comparison_buffer}->
				      get_end_iter(),
				  $line_image);
		$instance->{comparison_buffer}->
		    insert($instance->{comparison_buffer}->get_end_iter(),
			   "\n");

		# Attempt to extract the file name and its id, if this doesn't
		# work then it is probably a comment stating that the file is
		# binary.

		++ $i;
		($name) = ($lines[$i] =~ m/^--- (.+)\t[0-9a-f]{40}$/o);
		if (defined($name))
		{
		    $is_binary = 0;
		    ($file_id_1) =
			($lines[$i] =~ m/^--- .+\t([0-9a-f]{40})$/o);
		    ($file_id_2) =
			($lines[$i + 1] =~ m/^\+\+\+ .+\t([0-9a-f]{40})$/o);
		}
		else
		{
		    $is_binary = 1;
		    ($name) = ($lines[$i] =~ m/^\# (.+) is binary$/o);
		    $file_id_1 = $file_id_2 = "";
		}

		# Print out the details for the first file.

		$line = substr(expand(substr($lines[$i], $is_binary ? 1 : 3))
			           . $padding,
			       0,
			       $max_len);
		$instance->{comparison_buffer}->insert_with_tags_by_name
		    ($instance->{comparison_buffer}->get_end_iter(),
		     $line . "\n",
		     "compare-file-info-1");

		# Store the file name and the starting line number so that the
		# user can later jump straight to it using the file combobox.

		$iter = $instance->{comparison_buffer}->get_end_iter();
		$iter->backward_line();
		push(@files, {file_name => $name,
			      line_nr   => $iter->get_line(),
			      file_id_1 => $file_id_1,
			      file_id_2 => $file_id_2});

		# Print out the details for the second file if there is one.

		if (! $is_binary)
		{
		    ++ $i;
		    $line = substr(expand(substr($lines[$i], 3)) . $padding,
				   0,
				   $max_len);
		    $instance->{comparison_buffer}->insert_with_tags_by_name
			($instance->{comparison_buffer}->get_end_iter(),
			 $line . "\n",
			 "compare-file-info-2");
		}

	    }

	    # Deal with difference context lines.

	    elsif ($lines[$i] =~ m/^@@/o)
	    {
		$line = substr(substr($lines[$i], 2) . $padding, 0, $max_len);
		$instance->{comparison_buffer}->insert_with_tags_by_name
		    ($instance->{comparison_buffer}->get_end_iter(),
		     $line . "\n",
		     "compare-info");
	    }

	    # Deal with - change lines.

	    elsif ($lines[$i] =~ m/^-/o)
	    {
		$line = substr(substr($lines[$i], 1) . $padding, 0, $max_len);
		$instance->{comparison_buffer}->insert_with_tags_by_name
		    ($instance->{comparison_buffer}->get_end_iter(),
		     $line . "\n",
		     "compare-file-1");
	    }

	    # Deal with + change lines.

	    elsif ($lines[$i] =~ m/^\+/o)
	    {
		$line = substr(substr($lines[$i], 1) . $padding, 0, $max_len);
		$instance->{comparison_buffer}->insert_with_tags_by_name
		    ($instance->{comparison_buffer}->get_end_iter(),
		     $line . "\n",
		     "compare-file-2");
	    }

	    # Print out the rest.

	    else
	    {
		$line = substr($lines[$i], 1);
		$instance->{comparison_buffer}->insert
		    ($instance->{comparison_buffer}->get_end_iter(),
		     $line . "\n");
	    }

	    if (($i % 100) == 0)
	    {
		$instance->{appbar}->set_progress_percentage
		    (($i + 1) / scalar(@lines));
		$wm->update_gui();
	    }

	}

    }
    else
    {

	my($file_id_1,
	   $file_id_2,
	   $is_binary,
	   $name);

	# No the user wants the raw differences output.

	# Display the result, storing the locations of the files.

	$instance->{appbar}->set_status(__("Displaying differences"));
	$wm->update_gui();
	for ($i = 0; $i <= $#{$instance->{diff_output}}; ++ $i)
	{

	    # Deal with lines that introduce a new file comparison.

	    if ($instance->{diff_output}->[$i] =~ m/^==/o)
	    {

		# Extract the file name, if this doesn't work then it is
		# probably a comment stating that the file is binary.

		++ $i;
		($name) = ($instance->{diff_output}->[$i] =~
			   m/^--- (.+)\t[0-9a-f]{40}$/o);
		if (defined($name))
		{
		    $is_binary = 0;
		    ($file_id_1) = ($instance->{diff_output}->[$i] =~
				    m/^--- .+\t([0-9a-f]{40})$/o);
		    ($file_id_2) = ($instance->{diff_output}->[$i + 1]
				    =~ m/^\+\+\+ .+\t([0-9a-f]{40})$/o);
		}
		else
		{
		    ($name) = ($instance->{diff_output}->[$i] =~
			       m/^\# (.+) is binary$/o);
		    $is_binary = 1;
		    $file_id_1 = $file_id_2 = "";
		}

		# Print out the details for the first file.

		$instance->{comparison_buffer}->insert
		    ($instance->{comparison_buffer}->get_end_iter(),
		     $instance->{diff_output}->[$i] . "\n");

		# Store the file name and the starting line number so that the
		# user can later jump straight to it using the file combobox.

		$iter = $instance->{comparison_buffer}->get_end_iter();
		$iter->backward_line();
		push(@files, {file_name => $name,
			      line_nr   => $iter->get_line(),
			      file_id_1 => $file_id_1,
			      file_id_2 => $file_id_2});

		# Print out the details for the second file if there is one.

		if (! $is_binary)
		{
		    ++ $i;
		    $instance->{comparison_buffer}->insert
			($instance->{comparison_buffer}->get_end_iter(),
			 $instance->{diff_output}->[$i] . "\n");
		}

	    }

	    # Print out the rest.

	    else
	    {
		$instance->{comparison_buffer}->insert
		    ($instance->{comparison_buffer}->get_end_iter(),
		     $instance->{diff_output}->[$i] . "\n");
	    }

	    if (($i % 100) == 0)
	    {
		$instance->{appbar}->set_progress_percentage
		    (($i + 1) / scalar(@{$instance->{diff_output}}));
		$wm->update_gui();
	    }

	}

    }

    $instance->{stop_button}->set_sensitive(FALSE);

    # Delete the trailing newline.

    $iter = $instance->{comparison_buffer}->get_end_iter();
    $instance->{comparison_buffer}->delete
	($iter, $instance->{comparison_buffer}->get_end_iter())
	if ($iter->backward_char());

    # Populate the file combobox.

    $instance->{appbar}->set_progress_percentage(0);
    $instance->{appbar}->set_status(__("Populating file list"));
    $wm->update_gui();
    @files = sort({ $a->{file_name} cmp $b->{file_name} } @files);
    $i = 1;
    $instance->{file_comparison_combobox}->get_model()->clear();
    $instance->{file_comparison_combobox}->get_model()->set
	($instance->{file_comparison_combobox}->get_model()->append(),
	 CLS_FILE_NAME_COLUMN, __("Summary"),
	 CLS_LINE_NR_COLUMN,
	     $instance->{comparison_buffer}->get_start_iter()->get_line(),
	 CLS_FILE_ID_1_COLUMN, "",
	 CLS_FILE_ID_2_COLUMN, "");
    foreach my $file (@files)
    {
	$instance->{file_comparison_combobox}->get_model()->set
	    ($instance->{file_comparison_combobox}->get_model()->append(),
	     CLS_FILE_NAME_COLUMN, $file->{file_name},
	     CLS_LINE_NR_COLUMN, $file->{line_nr},
	     CLS_FILE_ID_1_COLUMN, $file->{file_id_1},
	     CLS_FILE_ID_2_COLUMN, $file->{file_id_2});
	$instance->{appbar}->set_progress_percentage($i ++ / scalar(@files));
	$wm->update_gui();
    }
    if (defined($file_name))
    {
	$instance->{file_comparison_combobox}->set_active(1);
	$instance->{external_diffs_button}->set_sensitive(TRUE);
    }
    else
    {
	$instance->{file_comparison_combobox}->set_active(0);
    }
    $instance->{appbar}->set_progress_percentage(0);
    $instance->{appbar}->set_status("");
    $wm->update_gui();

    # Make sure we are at the top.

    $instance->{comparison_buffer}->
	place_cursor($instance->{comparison_buffer}->get_start_iter());
    $instance->{comparison_scrolledwindow}->get_vadjustment()->set_value(0);
    $instance->{comparison_scrolledwindow}->get_hadjustment()->set_value(0);

    $instance->{appbar}->pop();
    $wm->make_busy($instance, 0);

}
#
##############################################################################
#
#   Routine      - file_comparison_combobox_changed_cb
#
#   Description  - Callback routine called when the user changes the value of
#                  the file combobox by selecting an entry from its pulldown
#                  list in a revision comparison window.
#
#   Data         - $widget   : The widget object that received the signal.
#                  $instance : The window instance that is associated with
#                              this widget.
#
##############################################################################



sub file_comparison_combobox_changed_cb($$)
{

    my($widget, $instance) = @_;

    return if ($instance->{in_cb});
    local $instance->{in_cb} = 1;

    my($iter,
       $line_iter,
       $line_nr);

    # Get the line number related to the selected file and then jump to it.

    $iter = $instance->{file_comparison_combobox}->get_active_iter();
    $line_nr = $instance->{file_comparison_combobox}->get_model()->
	get($iter, CLS_LINE_NR_COLUMN);
    $line_iter = $instance->{comparison_buffer}->get_iter_at_line($line_nr);
    $instance->{comparison_textview}->
	scroll_to_iter($line_iter, 0, TRUE, 0, 0);

    # Only enable the external differences button if an actual file is
    # selected.

    if ($line_nr > 0)
    {
	my $file_id_1 = $instance->{file_comparison_combobox}->get_model()->
	    get($iter, CLS_FILE_ID_1_COLUMN);
	my $file_id_2 = $instance->{file_comparison_combobox}->get_model()->
	    get($iter, CLS_FILE_ID_2_COLUMN);
	if ($file_id_1 ne "" && $file_id_1 ne $file_id_2)
	{
	    $instance->{external_diffs_button}->set_sensitive(TRUE);
	}
	else
	{
	    $instance->{external_diffs_button}->set_sensitive(FALSE);
	}
    }
    else
    {
	$instance->{external_diffs_button}->set_sensitive(FALSE);
    }

}
#
##############################################################################
#
#   Routine      - external_diffs_button_clicked_cb
#
#   Description  - Callback routine called when the user clicks on the
#                  external differences button in a revision comparison
#                  window.
#
#   Data         - $widget   : The widget object that received the signal.
#                  $instance : The window instance that is associated with
#                              this widget.
#
##############################################################################



sub external_diffs_button_clicked_cb($$)
{

    my($widget, $instance) = @_;

    return if ($instance->{in_cb});
    local $instance->{in_cb} = 1;

    my($cmd,
       $data,
       $file_id_1,
       $file_id_2,
       $file_name,
       $iter,
       $new_fh,
       $new_file,
       $old_fh,
       $old_file);

    # Get the details associated with the currently selected file.

    $iter = $instance->{file_comparison_combobox}->get_active_iter();
    $file_name = basename($instance->{file_comparison_combobox}->get_model()->
			  get($iter, CLS_FILE_NAME_COLUMN));
    $file_id_1 = $instance->{file_comparison_combobox}->get_model()->
	get($iter, CLS_FILE_ID_1_COLUMN);
    $file_id_2 = $instance->{file_comparison_combobox}->get_model()->
	get($iter, CLS_FILE_ID_2_COLUMN);

    # Generate temporary disk file names.

    if (! defined($old_file = generate_tmp_path("OLDER_" . $file_name))
	|| ! defined($new_file = generate_tmp_path("NEWER_" . $file_name)))
    {
	my $dialog = Gtk2::MessageDialog->new
	    ($instance->{window},
	     ["modal"],
	     "warning",
	     __x("Cannot generate temporary file name:\n{error_message}.",
		 error_message => $!));
	$dialog->run();
	$dialog->destroy();
	return;
    }

    # Attempt to save the contents to the files.

    if (! defined($old_fh = IO::File->new($old_file, "w"))
	|| ! defined($new_fh = IO::File->new($new_file, "w")))
    {
	my $dialog = Gtk2::MessageDialog->new
	    ($instance->{window},
	     ["modal"],
	     "warning",
	     "close",
	     __x("{error_message}.", error_message => $!));
	$dialog->run();
	$dialog->destroy();
	return;
    }
    $instance->{mtn}->get_file(\$data, $file_id_1);
    $old_fh->print($data);
    $instance->{mtn}->get_file(\$data, $file_id_2);
    $new_fh->print($data);
    $old_fh->close();
    $new_fh->close();
    $old_fh = $new_fh = undef;

    # Use the specified differences application, replacing `{file1}' and
    # `{file2}' with the real file names.

    $cmd = $user_preferences->{diffs_application};
    $cmd =~ s/\{file1\}/$old_file/g;
    $cmd =~ s/\{file2\}/$new_file/g;

    # Launch it.

    system($cmd . " &");

}
#
##############################################################################
#
#   Routine      - save_differences_button_clicked_cb
#
#   Description  - Callback routine called when the user clicks on the save
#                  differences button in a revision comparison window.
#
#   Data         - $widget   : The widget object that received the signal.
#                  $instance : The window instance that is associated with
#                              this widget.
#
##############################################################################



sub save_differences_button_clicked_cb($$)
{

    my($widget, $instance) = @_;

    return if ($instance->{in_cb});
    local $instance->{in_cb} = 1;

    my $data;

    $data = join("\n", @{$instance->{diff_output}}) . "\n";
    save_as_file($instance->{window}, "unified_diff.out", \$data);

}
#
##############################################################################
#
#   Routine      - comparison_revision_change_log_button_clicked_cb
#
#   Description  - Callback routine called when the user clicks on either of
#                  the two revision change log buttons in a revision
#                  comparison window.
#
#   Data         - $widget  : The widget object that received the signal.
#                  $details : A reference to an anonymous hash containing the
#                             window instance, revision and action that is
#                             associated with this widget.
#
##############################################################################



sub comparison_revision_change_log_button_clicked_cb($$)
{

    my($widget, $instance) = @_;

    return if ($instance->{in_cb});
    local $instance->{in_cb} = 1;

    my($colour,
       $revision_id,
       $revision_name);

    # Work out what to do.

    if ($widget == $instance->{revision_change_log_1_button})
    {
	$revision_id = $instance->{revision_id_1};
	if ($user_preferences->{coloured_diffs})
	{
	    $colour = "compare-1";
	}
	else
	{
	    $revision_name = "- " . $revision_id;
	}
    }
    else
    {
	$revision_id = $instance->{revision_id_2};
	if ($user_preferences->{coloured_diffs})
	{
	    $colour = "compare-2";
	}
	else
	{
	    $revision_name = "+ " . $revision_id;
	}
    }

    # Display the full revision change log.

    display_change_log($instance->{mtn},
		       $revision_id,
		       $colour,
		       $revision_name);

}
#
##############################################################################
#
#   Routine      - get_history_window
#
#   Description  - Creates or prepares an existing history window for use.
#
#   Data         - Return Value : A reference to the newly created or unused
#                                 history instance record.
#
##############################################################################



sub get_history_window()
{

    my $instance;
    my $window_type = "history_window";
    my $wm = WindowManager->instance();

    # Create a new file history window if an unused one wasn't found, otherwise
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
	foreach my $widget ("appbar",
			    "history_label",
			    "history_textview",
			    "history_scrolledwindow",
			    "stop_button",
			    "compare_button",
			    "numbers_value_label",
			    "revision_id_1_value_label",
			    "revision_id_2_value_label")
	{
	    $instance->{$widget} = $instance->{glade}->get_widget($widget);
	}

	# Setup the file history callbacks.

	$instance->{window}->signal_connect
	    ("delete_event",
	     sub {
		 my($widget, $event, $instance) = @_;
		 return TRUE if ($instance->{in_cb});
		 local $instance->{in_cb} = 1;
		 $widget->hide();
		 $instance->{history_buffer}->set_text("");
		 $instance->{mtn} = undef;
		 return TRUE;
	     },
	     $instance);
	$instance->{stop_button}->signal_connect
	    ("clicked", sub { $_[1]->{stop} = 1; }, $instance);

	# Setup the file history viewer.

	$instance->{history_buffer} =
	    $instance->{history_textview}->get_buffer();
	create_format_tags($instance->{history_buffer});
	$instance->{history_textview}->modify_font($mono_font);

	# Register the window for management.

	$wm->manage($instance,
		    $window_type,
		    $instance->{window},
		    $instance->{stop_button});
	$wm->add_busy_windows($instance,
			      $instance->{history_textview}->
			          get_window("text"));
    }
    else
    {
	my($height,
	   $width);
	$instance->{in_cb} = 0;
	local $instance->{in_cb} = 1;
	($width, $height) = $instance->{window}->get_default_size();
	$instance->{window}->resize($width, $height);
	$instance->{stop_button}->set_sensitive(FALSE);
	$instance->{compare_button}->set_sensitive(FALSE);
	set_label_value($instance->{numbers_value_label}, "");
	set_label_value($instance->{revision_id_1_value_label}, "");
	set_label_value($instance->{revision_id_2_value_label}, "");
	$instance->{appbar}->set_progress_percentage(0);
	$instance->{appbar}->clear_stack();
    }

    $instance->{stop} = 0;

    # Empty out the contents.

    $instance->{history_buffer}->set_text("");

    return $instance;

}
#
##############################################################################
#
#   Routine      - get_file_history_helper
#
#   Description  - Recursive routine for getting the revisions in a file's
#                  change history.
#
#   Data         - $instance    : The file history window instance.
#                  $hash        : A reference to a hash that is to contain the
#                                 list of revision ids.
#                  $revision_id : The revision id from where the search is to
#                                 commence.
#
##############################################################################



sub get_file_history_helper($$$)
{

    my($instance, $hash, $revision_id) = @_;

    return if ($instance->{stop});

    my(@change_parents,
       @parents);

    $instance->{mtn}->get_content_changed(\@change_parents,
					  $revision_id,
					  $instance->{file_name});
    foreach my $revision (@change_parents)
    {
	if (! exists($hash->{$revision}))
	{
	    $hash->{$revision} = 1;
	    set_label_value($instance->{numbers_value_label},
			    scalar(keys(%$hash)));
	    WindowManager->update_gui();
	    @parents = ();
	    $instance->{mtn}->parents(\@parents, $revision);
	    foreach my $parent (@parents)
	    {
		get_file_history_helper($instance, $hash, $parent);
	    }
	}
    }

}
#
##############################################################################
#
#   Routine      - get_revision_history_helper
#
#   Description  - Recursive routine for getting the revisions in a revision's
#                  change history.
#
#   Data         - $instance    : The revision history window instance.
#                  $hash        : A reference to a hash that is to contain the
#                                 list of revision ids.
#                  $revision_id : The revision id from where the search is to
#                                 commence.
#
##############################################################################



sub get_revision_history_helper($$$)
{

    my($instance, $hash, $revision_id) = @_;

    return if ($instance->{stop});

    my @parents;

    $instance->{mtn}->parents(\@parents, $revision_id);
    foreach my $parent (@parents)
    {
	if (! exists($hash->{$parent}))
	{
	    $hash->{$parent} = 1;
	    set_label_value($instance->{numbers_value_label},
			    scalar(keys(%$hash)));
	    WindowManager->update_gui();
	    get_revision_history_helper($instance, $hash, $parent);
	}
    }

}
#
##############################################################################
#
#   Routine      - get_revision_comparison_window
#
#   Description  - Creates or prepares an existing revision comparison window
#                  for use.
#
#   Data         - Return Value : A reference to the newly created or unused
#                                 change log instance record.
#
##############################################################################



sub get_revision_comparison_window()
{

    my($height,
       $instance,
       $renderer,
       $width);
    my $window_type = "revision_comparison_window";
    my $wm = WindowManager->instance();

    # Create a new revision comparison window if an unused one wasn't found,
    # otherwise reuse an existing unused one.

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
	foreach my $widget ("appbar",
			    "comparison_label",
			    "file_comparison_combobox",
			    "external_diffs_button",
			    "stop_button",
			    "comparison_textview",
			    "comparison_scrolledwindow",
			    "revision_change_log_1_button",
			    "revision_change_log_1_button_label",
			    "revision_change_log_2_button_label")
	{
	    $instance->{$widget} = $instance->{glade}->get_widget($widget);
	}

	# Setup the file history callbacks.

	$instance->{window}->signal_connect
	    ("delete_event",
	     sub {
		 my($widget, $event, $instance) = @_;
		 return TRUE if ($instance->{in_cb});
		 local $instance->{in_cb} = 1;
		 $widget->hide();
		 $instance->{file_comparison_combobox}->get_model()->clear();
		 $instance->{comparison_buffer}->set_text("");
		 $instance->{diff_output} = [];
		 $instance->{mtn} = undef;
		 return TRUE;
	     },
	     $instance);
	$instance->{stop_button}->signal_connect
	    ("clicked", sub { $_[1]->{stop} = 1; }, $instance);

	# Setup the file combobox.

	$instance->{file_comparison_combobox}->
	    set_model(Gtk2::ListStore->new("Glib::String",
					   "Glib::Int",
					   "Glib::String",
					   "Glib::String"));
	$renderer = Gtk2::CellRendererText->new();
	$instance->{file_comparison_combobox}->pack_start($renderer, TRUE);
	$instance->{file_comparison_combobox}->add_attribute($renderer,
							     "text" => 0);
	$instance->{file_comparison_combobox}->get_model()->set
	    ($instance->{file_comparison_combobox}->get_model()->append(),
	     CLS_FILE_NAME_COLUMN, " ",
	     CLS_LINE_NR_COLUMN, 0,
	     CLS_FILE_ID_1_COLUMN, "",
	     CLS_FILE_ID_2_COLUMN, "");

	# Setup the revision comparison viewer.

	$instance->{comparison_buffer} =
	    $instance->{comparison_textview}->get_buffer();
	create_format_tags($instance->{comparison_buffer});
	$instance->{comparison_textview}->modify_font($mono_font);

	# Setup the revision log button labels.

	if ($user_preferences->{coloured_diffs})
	{
	    $instance->{revision_change_log_1_button_label}->
		set_markup("<span foreground='"
			   . $user_preferences->{colours}->{cmp_revision_1}->
			       {fg}
			   . "'>"
			   . __("Revision Change Log")
			   . "</span>");
	    $instance->{revision_change_log_2_button_label}->
		set_markup("<span foreground='"
			   . $user_preferences->{colours}->{cmp_revision_2}->
			       {fg}
			   . "'>"
			   . __("Revision Change Log")
			   . "</span>");
	}
	else
	{
	    $instance->{revision_change_log_1_button_label}->
		set_text(__("- Revision Change Log"));
	    $instance->{revision_change_log_2_button_label}->
		set_text(__("+ Revision Change Log"));
	}

	# Register the window for management.

	$wm->manage($instance,
		    $window_type,
		    $instance->{window},
		    $instance->{stop_button});
	$wm->add_busy_windows($instance,
			      $instance->{comparison_textview}->
			          get_window("text"));
    }
    else
    {
	$instance->{in_cb} = 0;
	local $instance->{in_cb} = 1;
	($width, $height) = $instance->{window}->get_default_size();
	$instance->{window}->resize($width, $height);
	$instance->{external_diffs_button}->set_sensitive(FALSE);
	$instance->{stop_button}->set_sensitive(FALSE);
	$instance->{file_comparison_combobox}->get_model()->clear();
	$instance->{appbar}->set_progress_percentage(0);
	$instance->{appbar}->clear_stack();
    }

    # Empty out the contents.

    $instance->{diff_output} = [];
    $instance->{comparison_buffer}->set_text("");
    $instance->{stop} = 0;

    return $instance;

}
#
##############################################################################
#
#   Routine      - mtn_diff
#
#   Description  - Compare two the specified two revisions, optionally
#                  restricting the comparison to the specified file.
#
#   Data         - $list          : A reference to the list that is to contain
#                                   the output from the diff command.
#                  $mtn           : The Monotone database that is to be used
#                                   or undef if the database associated with
#                                   the current workspace is to be used.
#                  $revision_id_1 : The first revision id that is to be
#                                   compared.
#                  $revision_id_2 : The second revision id that is to be
#                                   compared.
#                  $file_name     : Either the name of the file that the
#                                   revision comparison should be restricted
#                                   to or undef for a full revision
#                                   comparison.
#                  Return Value   : True if the comparison worked, otherwise
#                                   false if something went wrong.
#
##############################################################################



sub mtn_diff($$$$;$)
{

    my($list, $mtn_db, $revision_id_1, $revision_id_2, $file_name) = @_;

    my($buffer,
       @cmd);

    # Run mtn diff.

    @$list = ();
    push(@cmd, "mtn");
    push(@cmd, "--db=" . $mtn_db) if (defined($mtn_db));
    push(@cmd, "diff");
    push(@cmd, "-r");
    push(@cmd, "i:" . $revision_id_1);
    push(@cmd, "-r");
    push(@cmd, "i:" . $revision_id_2);
    push(@cmd, $file_name) if (defined($file_name));
    run_command(\$buffer, @cmd) or return;

    # Break up the input into a list of lines.

    @$list = split(/\n/o, $buffer);

    return 1;

}

1;

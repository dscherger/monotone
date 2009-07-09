##############################################################################
#
#   File Name    - FindFiles.pm
#
#   Description  - The find files module for the mtn-browse application. This
#                  module contains all the routines for implementing the find
#                  files window.
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

# Constants for the columns within the results listsore widget.

use constant RLS_NAME_COLUMN           => 0;
use constant RLS_MANIFEST_ENTRY_COLUMN => 1;

# Constants for representing combobox values.

use constant DURATION_MINUTES => 0;
use constant DURATION_HOURS   => 1;
use constant DURATION_DAYS    => 2;
use constant DURATION_MONTHS  => 3;
use constant DURATION_YEARS   => 4;

use constant CMP_ANY_SIZE => 0;
use constant CMP_AT_LEAST => 1;
use constant CMP_AT_MOST  => 2;
use constant CMP_EQUAL_TO => 3;

use constant SIZE_B  => 0;
use constant SIZE_KB => 1;
use constant SIZE_MB => 2;

# ***** FUNCTIONAL PROTOTYPES *****

# Public routines.

sub display_find_files($$$$$);

# Private routines.

sub between_range_radiobutton_toggled_cb($$);
sub date_range_checkbutton_toggled_cb($$);
sub get_find_files_window();
sub results_treeview_cursor_changed_cb($$);
sub results_treeview_row_activated_cb($$$$);
sub save_query_from_gui($);
sub search_files_button_clicked_cb($$);
sub size_comparitor_combobox_changed_cb($$);
sub validate_query($$);
#
##############################################################################
#
#   Routine      - display_find_files
#
#   Description  - Display the find files window.
#
#   Data         - $mtn            : The Monotone::AutomateStdio object that
#                                    is to be used to search for files.
#                  $tag            : Either a tag name for the specified
#                                    revision that is to be used in the window
#                                    title instead of the revision id or undef
#                                    if the revision id should be used.
#                  $revision_id    : The id of the revision that is to be
#                                    searched.
#                  $manifest       : A reference to the corresponding revision
#                                    manifest.
#                  $starting_point : The directory from which the search is to
#                                    be started.
#
##############################################################################



sub display_find_files($$$$$)
{

    my($mtn, $tag, $revision_id, $manifest, $starting_point) = @_;

    my $instance;

    $instance = get_find_files_window();
    local $instance->{in_cb} = 1;

    $instance->{mtn} = $mtn;
    $instance->{revision_id} = $revision_id;
    $instance->{manifest} = $manifest;
    $instance->{starting_point} = $starting_point;

    $instance->{window}->set_title(__x("Find Files Within Revision {rev}",
				       rev => defined($tag) ?
				           $tag : $revision_id));
    set_label_value($instance->{searching_from_value_label},
		    $instance->{starting_point});

    $instance->{window}->show_all();
    $instance->{window}->present();

}
#
##############################################################################
#
#   Routine      - date_range_checkbutton_toggled_cb
#
#   Description  - Callback routine called when the user changes the value of
#                  the date range check button in the find files window.
#
#   Data         - $widget   : The widget object that received the signal.
#                  $instance : The window instance that is associated with
#                              this widget.
#
##############################################################################



sub date_range_checkbutton_toggled_cb($$)
{

    my($widget, $instance) = @_;

    return if ($instance->{in_cb});
    local $instance->{in_cb} = 1;

    # Simply enable and disable the appropriate widgets.

    if ($instance->{date_range_checkbutton}->get_active())
    {
	for my $widget (@{$instance->{date_sensitive_group}})
	{
	    $widget->set_sensitive(TRUE);
	}
	if ($instance->{between_range_radiobutton}->get_active())
	{
	    for my $widget (@{$instance->{during_dates_sensitive_group}})
	    {
		$widget->set_sensitive(FALSE);
	    }
	}
	else
	{
	    for my $widget (@{$instance->{between_dates_sensitive_group}})
	    {
		$widget->set_sensitive(FALSE);
	    }
	}
    }
    else
    {
	for my $widget (@{$instance->{date_sensitive_group}})
	{
	    $widget->set_sensitive(FALSE);
	}
    }

}
#
##############################################################################
#
#   Routine      - between_range_radiobutton_toggled_cb
#
#   Description  - Callback routine called when the user changes the value of
#                  the between radio button in the find files window.
#
#   Data         - $widget   : The widget object that received the signal.
#                  $instance : The window instance that is associated with
#                              this widget.
#
##############################################################################



sub between_range_radiobutton_toggled_cb($$)
{

    my($widget, $instance) = @_;

    return if ($instance->{in_cb});
    local $instance->{in_cb} = 1;

    # Simply enable and disable the appropriate widgets.

    if ($instance->{between_range_radiobutton}->get_active())
    {
	for my $widget (@{$instance->{between_dates_sensitive_group}})
	{
	    $widget->set_sensitive(TRUE);
	}
	for my $widget (@{$instance->{during_dates_sensitive_group}})
	{
	    $widget->set_sensitive(FALSE);
	}
    }
    else
    {
	for my $widget (@{$instance->{during_dates_sensitive_group}})
	{
	    $widget->set_sensitive(TRUE);
	}
	for my $widget (@{$instance->{between_dates_sensitive_group}})
	{
	    $widget->set_sensitive(FALSE);
	}
    }

}
#
##############################################################################
#
#   Routine      - size_comparitor_combobox_changed_cb
#
#   Description  - Callback routine called when the user changes the value of
#                  the file size combobox by selecting an entry from its
#                  pulldown list in the find files window.
#
#   Data         - $widget   : The widget object that received the signal.
#                  $instance : The window instance that is associated with
#                              this widget.
#
##############################################################################



sub size_comparitor_combobox_changed_cb($$)
{

    my($widget, $instance) = @_;

    return if ($instance->{in_cb});
    local $instance->{in_cb} = 1;

    # Simply enable and disable the appropriate widgets.

    if ($widget->get_active() == CMP_ANY_SIZE)
    {
	for my $widget (@{$instance->{size_sensitive_group}})
	{
	    $widget->set_sensitive(FALSE);
	}
    }
    else
    {
	for my $widget (@{$instance->{size_sensitive_group}})
	{
	    $widget->set_sensitive(TRUE);
	}
    }

}
#
##############################################################################
#
#   Routine      - search_files_button_clicked_cb
#
#   Description  - Callback routine called when the user clicks on the find
#                  button in a find files window.
#
#   Data         - $widget   : The widget object that received the signal.
#                  $instance : The window instance that is associated with
#                              this widget.
#
##############################################################################



sub search_files_button_clicked_cb($$)
{

    my($widget, $instance) = @_;

    return if ($instance->{in_cb});
    local $instance->{in_cb} = 1;

    my($contents_re,
       $counter,
       $file_glob_re,
       $file_prefix_length,
       $manifest,
       $matches,
       $name,
       $period,
       $query,
       $size);
    my $wm = WindowManager->instance();

    # Get the query from the GUI and validate it.

    $query = save_query_from_gui($instance);
    return if (! validate_query($instance, $query));

    $wm->make_busy($instance, 1);
    $instance->{appbar}->push($instance->{appbar}->get_status()->get_text());
    $wm->update_gui();

    # Update the comboxentries histories.

    handle_comboxentry_history($instance->{files_named_comboboxentry},
			       "find_files_named",
			       $query->{file_glob});
    handle_comboxentry_history($instance->{files_containing_comboboxentry},
			       "find_files_containing",
			       $query->{contents_pattern});
    handle_comboxentry_history($instance->{modified_by_comboboxentry},
			       "find_files_modified_by",
			       $query->{modified_by});

    # Build up a sub-manifest representing the items that we are going to
    # search.

    $file_prefix_length = 0;
    if ($query->{file_glob_search_subdirectories}
	&& $instance->{starting_point} eq "")
    {
	$manifest = $instance->{manifest};
    }
    else
    {
	my @manifest;
	if ($query->{file_glob_search_subdirectories})
	{
	    my $match_re = qr/^$instance->{starting_point}\/.+$/;
	    @manifest =
		grep($_->{name} =~ m/$match_re/, @{$instance->{manifest}});
	}
	else
	{
	    my @result;
	    get_dir_contents($instance->{starting_point},
			     $instance->{manifest},
			     \@result);
	    @manifest = map($_->{manifest_entry}, @result);
	}
	$manifest = \@manifest;
	$file_prefix_length = length($instance->{starting_point});
	++ $file_prefix_length if ($file_prefix_length > 0);
    }

    # Precompile the regexps and more complex comparison values.

    if ($query->{file_glob} ne "")
    {
	my $re_text = file_glob_to_regexp($query->{file_glob});
	if ($query->{file_glob_case_sensitive})
	{
	    $file_glob_re = qr/$re_text/;
	}
	else
	{
	    $file_glob_re = qr/$re_text/i;
	}
    }
    else
    {
	$file_glob_re = qr/^.*$/;
    }
    if ($query->{contents_pattern} ne "")
    {

	my $pattern;

	# We need to convert the contents pattern from possibly UTF-8 to binary
	# as we may have to search binary data.

	$pattern = encode_utf8($query->{contents_pattern});

	if ($query->{contents_pattern_is_regexp})
	{
	    if ($query->{contents_case_sensitive})
	    {
		$contents_re = qr/$pattern/;
	    }
	    else
	    {
		$contents_re = qr/$pattern/i;
	    }
	}
	else
	{
	    if ($query->{contents_case_sensitive})
	    {
		$contents_re = qr/\Q$pattern\E/;
	    }
	    else
	    {
		$contents_re = qr/\Q$pattern\E/i;
	    }
	}

    }
    if (exists($query->{period}))
    {

	my($time,
	   @time_val);

	# Please note:
	#     1) Values from localtime etc start from 0.
	#     2) You may think that gmtime() should be used instead of
	#        localtime(), but what happens when the user jumps back in time
	#        by 5 months and goes from say GMT to BST? Would they expect
	#        the hours to be adjusted? Probably not.

	@time_val = localtime();
	$time = timelocal(@time_val[0 .. 5]);
	if ($query->{period_units} == DURATION_MINUTES)
	{
	    $time -= ($query->{period} * 60);
	}
	elsif ($query->{period_units} == DURATION_HOURS)
	{
	    $time -= ($query->{period} * 60 * 60);
	}
	elsif ($query->{period_units} == DURATION_DAYS)
	{
	    $time -= ($query->{period} * 60 * 60 * 24);
	}
	elsif ($query->{period_units} == DURATION_MONTHS)
	{
	    my($month,
	       $query_months,
	       $year);
	    $query_months = $query->{period};
	    ($month, $year) = (@time_val)[4, 5];
	    if ($query_months > 12)
	    {
		$year -= int($query_months / 12);
		$query_months %= 12;
	    }
	    if ($query_months > $month)
	    {
		-- $year;
		$month = 12 - ($query_months - $month);
	    }
	    else
	    {
		$month -= $query_months;
	    }
	    @time_val[4, 5] = ($month, $year);
	    $time = timegm(@time_val[0 .. 5]);
	}
	else
	{
	    $time_val[5] -= $query->{period};
	    $time = timegm(@time_val[0 .. 5]);
	}
	$period = strftime("%Y-%m-%dT%H:%M:%S", gmtime($time));
    }
    if (exists($query->{file_size}))
    {
	if ($query->{file_size_units} == SIZE_B)
	{
	    $size = $query->{file_size};
	}
	elsif ($query->{file_size_units} == SIZE_KB)
	{
	    $size = $query->{file_size} * 1024;
	}
	else
	{
	    $size = $query->{file_size} * 1024 * 1024;
	}
    }

    $instance->{appbar}->set_status(__("Finding matching files"));
    $instance->{results_liststore}->clear();
    set_label_value($instance->{author_value_label}, "");
    set_label_value($instance->{file_id_value_label}, "");
    set_label_value($instance->{last_update_value_label}, "");
    set_label_value($instance->{file_revision_id_value_label}, "");
    $wm->update_gui();

    $instance->{stop_button}->set_sensitive(TRUE);

    # Now do the search.

    $counter = 1;
    $matches = 0;
    foreach my $entry (@$manifest)
    {

	# Check for aborts.

	last if ($instance->{stop});

	# Exclude entries using the cheapest critera first.

	# Name.

	next if (basename($entry->{name}) !~ m/$file_glob_re/
		 || ($query->{file_glob} eq "" && $entry->{type} ne "file"));

	# The remaining tests only make sense for files.

	if ($entry->{type} eq "file")
	{

	    # Date and/or modifier.

	    if (exists($query->{older_date}) || exists($query->{period})
		|| $query->{modified_by} ne "")
	    {
		my($author,
		   $last_update);
		if (! exists($entry->{author}))
		{
		    cache_extra_file_info($instance->{mtn},
					  $instance->{revision_id},
					  $entry);
		}
		$author = $entry->{author};
		$last_update = $entry->{last_update};
		if (exists($query->{older_date}))
		{
		    next if ($last_update lt $query->{older_date}
			     || $last_update gt $query->{younger_date});
		}
		elsif (exists($query->{period}))
		{
		    next if ($last_update lt $period);
		}
		next if ($query->{modified_by} ne ""
			 && $query->{modified_by} ne $author);
	    }

	    # Contents and/or size.

	    if (defined($contents_re) || exists($query->{file_size}))
	    {
		my $data;
		if (exists($query->{file_size}))
		{
		    if (! exists($entry->{size}))
		    {
			$instance->{mtn}->get_file(\$data, $entry->{file_id});
			$entry->{size} = length($data);
		    }
		    if ($query->{file_size_operator} == CMP_AT_LEAST)
		    {
			next if ($entry->{size} < $size);
		    }
		    elsif ($query->{file_size_operator} == CMP_AT_MOST)
		    {
			next if ($entry->{size} > $size);
		    }
		    else
		    {
			next if ($entry->{size} != $size);
		    }
		}
		if (defined($contents_re))
		{
		    if (! exists($entry->{is_binary}))
		    {
			$instance->{mtn}->get_file(\$data, $entry->{file_id})
			    if (! defined($data));
			$entry->{is_binary} = data_is_binary(\$data);
		    }

		    # Ignore binary files if requested.

		    next if ($entry->{is_binary}
			     && ! $query->{contents_search_binary_files});

		    $instance->{mtn}->get_file(\$data, $entry->{file_id})
			if (! defined($data));
		    next if ($data !~ m/$contents_re/);
		}
	    }

	}

	# If we have got this far then it is a match.

	++ $matches;
	$name = substr($entry->{name}, $file_prefix_length);
	$instance->{results_liststore}->
	    set($instance->{results_liststore}->append(),
		RLS_NAME_COLUMN, $name,
		RLS_MANIFEST_ENTRY_COLUMN, $entry);

    }
    continue
    {
	if (($counter % 10) == 0)
	{
	    $instance->{appbar}->set_progress_percentage
		($counter / scalar(@$manifest));
	    $wm->update_gui();
	}
	++ $counter;
    }
    $instance->{appbar}->set_progress_percentage(1);
    $wm->update_gui();

    $instance->{stop_button}->set_sensitive(FALSE);
    $instance->{stop} = 0;

    $instance->{results_treeview}->scroll_to_point(0, 0);
    $instance->{appbar}->set_progress_percentage(0);
    $instance->{appbar}->set_status("");
    $wm->update_gui();

    $instance->{appbar}->pop();
    if ($matches > 0)
    {
	$instance->{appbar}->set_status(__nx("Found 1 file",
					     "Found {files_found} files",
					     $matches,
					     files_found => $matches));
    }
    else
    {
	my $dialog;
	$dialog = Gtk2::MessageDialog->new
	    ($instance->{window},
	     ["modal"],
	     "info",
	     "close",
	     __("No files matched your query."));
	WindowManager::instance()->allow_input(sub { $dialog->run(); });
	$dialog->destroy();
	$instance->{appbar}->set_status(__("Nothing found"));
    }
    $wm->make_busy($instance, 0);

}
#
##############################################################################
#
#   Routine      - results_treeview_cursor_changed_cb
#
#   Description  - Callback routine called when the user selects an entry in
#                  the results treeview in a find files window.
#
#   Data         - $widget   : The widget object that received the signal.
#                  $instance : The window instance that is associated with
#                              this widget.
#
##############################################################################



sub results_treeview_cursor_changed_cb($$)
{

    my($widget, $instance) = @_;

    return if ($instance->{in_cb});
    local $instance->{in_cb} = 1;

    my $manifest_entry;

    # Get the manifest entry details for the item that was selected.

    $widget->get_selection()->selected_foreach
	(sub {
	     my($model, $path, $iter) = @_;
	     $manifest_entry = $model->get($iter, RLS_MANIFEST_ENTRY_COLUMN);
	 });

    if (defined($manifest_entry))
    {
	my($author,
	   $file_id,
	   $last_changed_revision,
	   $last_update);
	if ($manifest_entry->{type} eq "file")
	{
	    if (! exists($manifest_entry->{author}))
	    {
		cache_extra_file_info($instance->{mtn},
				      $instance->{revision_id},
				      $manifest_entry);
	    }
	    $author = $manifest_entry->{author};
	    $file_id = $manifest_entry->{file_id};
	    $last_changed_revision = $manifest_entry->{last_changed_revision};
	    $last_update = $manifest_entry->{last_update};
	    $last_update =~ s/T/ /;
	}
	else
	{
	    $author = $file_id = $last_changed_revision = $last_update = "";
	}
	set_label_value($instance->{author_value_label}, $author);
	set_label_value($instance->{file_id_value_label}, $file_id);
	set_label_value($instance->{last_update_value_label}, $last_update);
	set_label_value($instance->{file_revision_id_value_label},
			$last_changed_revision);
    }

}
#
##############################################################################
#
#   Routine      - results_treeview_row_activated_cb
#
#   Description  - Callback routine called when the user double clicks on an
#                  entry in the results treeview in a find files window.
#
#   Data         - $widget           : The widget object that received the
#                                      signal.
#                  $tree_path        : A Gtk2::TreePath object for the
#                                      selected item.
#                  $tree_view_column : A Gtk2::TreeViewColumn object for the
#                                      selected item.
#                  $instance         : The window instance that is associated
#                                      with this widget.
#
##############################################################################



sub results_treeview_row_activated_cb($$$$)
{

    my($widget, $tree_path, $tree_view_column, $instance) = @_;

    return if ($instance->{in_cb});
    local $instance->{in_cb} = 1;

    my $manifest_entry;

    # Get the manifest entry details for the item that was double-clicked.

    $widget->get_selection()->selected_foreach
	(sub {
	     my($model, $path, $iter) = @_;
	     $manifest_entry = $model->get($iter, RLS_MANIFEST_ENTRY_COLUMN);
	 });

    if (defined($manifest_entry))
    {

	my($branch,
	   @certs_list);

	# First find out what branch the revision is on (take the first one).

	$instance->{mtn}->certs(\@certs_list, $instance->{revision_id});
	$branch = "";
	foreach my $cert (@certs_list)
	{
	    if ($cert->{name} eq "branch")
	    {
		$branch = $cert->{value};
		last;
	    }
	}

	# Either display the file or the directory depending upon the entry's
	# type.

	if ($manifest_entry->{type} eq "file")
	{
	    my($dir,
	       $file);
	    $dir = dirname($manifest_entry->{name});
	    $dir = "" if ($dir eq ".");
	    $file = basename($manifest_entry->{name});
	    get_browser_window($instance->{mtn},
			       $branch,
			       $instance->{revision_id},
			       $dir,
			       $file);
	}
	else
	{
	    get_browser_window($instance->{mtn},
			       $branch,
			       $instance->{revision_id},
			       $manifest_entry->{name});
	}

    }

}
#
##############################################################################
#
#   Routine      - get_find_files_window
#
#   Description  - Creates or prepares an existing find files window for use.
#
#   Data         - Return Value : A reference to the newly created or unused
#                                 find files instance record.
#
##############################################################################



sub get_find_files_window()
{

    my $instance;
    my $window_type = "find_files_window";
    my $wm = WindowManager->instance();

    # Create a new find files window if an unused one wasn't found, otherwise
    # reuse an existing unused one.

    if (! defined($instance = $wm->find_unused($window_type)))
    {

	my($renderer,
	   $tv_column);

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
	foreach my $widget ("appbar",
			    "search_files_button",
			    "stop_button",

			    "searching_from_value_label",
			    "files_named_comboboxentry",
			    "name_case_sensitive_checkbutton",
			    "search_subdirectories_checkbutton",

			    "files_containing_comboboxentry",
			    "contents_case_sensitive_checkbutton",
			    "regular_expression_checkbutton",
			    "search_binary_files_checkbutton",

			    "date_range_checkbutton",
			    "between_range_radiobutton",
			    "older_date_dateedit",
			    "and_label",
			    "younger_date_dateedit",
			    "during_range_radiobutton",
			    "time_spinbutton",
			    "time_units_combobox",
			    "size_comparitor_combobox",
			    "size_spinbutton",
			    "size_units_combobox",
			    "modified_by_comboboxentry",

			    "results_scrolledwindow",
			    "results_treeview",
			    "author_value_label",
			    "file_id_value_label",
			    "last_update_value_label",
			    "file_revision_id_value_label")
	{
	    $instance->{$widget} = $instance->{glade}->get_widget($widget);
	}

	# Setup widget sensitivity groups.

	$instance->{date_sensitive_group} = [];
	foreach my $widget ("between_range_radiobutton",
			    "older_date_dateedit",
			    "and_label",
			    "younger_date_dateedit",
			    "during_range_radiobutton",
			    "time_spinbutton",
			    "time_units_combobox")
	{
	    push(@{$instance->{date_sensitive_group}}, $instance->{$widget});
	}
	$instance->{between_dates_sensitive_group} = [];
	foreach my $widget ("older_date_dateedit",
			    "and_label",
			    "younger_date_dateedit")
	{
	    push(@{$instance->{between_dates_sensitive_group}},
		 $instance->{$widget});
	}
	$instance->{during_dates_sensitive_group} = [];
	foreach my $widget ("time_spinbutton", "time_units_combobox")
	{
	    push(@{$instance->{during_dates_sensitive_group}},
		 $instance->{$widget});
	}
	$instance->{size_sensitive_group} = [];
	foreach my $widget ("size_spinbutton", "size_units_combobox")
	{
	    push(@{$instance->{size_sensitive_group}}, $instance->{$widget});
	}

	# Setup the find files callbacks.

	$instance->{window}->signal_connect
	    ("delete_event",
	     sub {
		 my($widget, $event, $instance) = @_;
		 return TRUE if ($instance->{in_cb});
		 local $instance->{in_cb} = 1;
		 $widget->hide();
		 $instance->{manifest} = undef;
		 $instance->{mtn} = undef;
		 $instance->{results_liststore}->clear();
		 return TRUE;
	     },
	     $instance);
	$instance->{stop_button}->signal_connect
	    ("clicked", sub { $_[1]->{stop} = 1; }, $instance);

	# Setup the comboboxes.

	$instance->{files_named_comboboxentry}->
	    set_model(Gtk2::ListStore->new("Glib::String"));
	$instance->{files_named_comboboxentry}->set_text_column(0);
	$instance->{files_containing_comboboxentry}->
	    set_model(Gtk2::ListStore->new("Glib::String"));
	$instance->{files_containing_comboboxentry}->set_text_column(0);
	$instance->{modified_by_comboboxentry}->
	    set_model(Gtk2::ListStore->new("Glib::String"));
	$instance->{modified_by_comboboxentry}->set_text_column(0);
	$instance->{size_comparitor_combobox}->set_active(CMP_ANY_SIZE);
	$instance->{size_units_combobox}->set_active(SIZE_KB);
	$instance->{time_units_combobox}->set_active(DURATION_DAYS);

	# Setup the results list browser.

	$instance->{results_liststore} = Gtk2::ListStore->new("Glib::String",
							      "Glib::Scalar");
	$instance->{results_treeview}->
	    set_model($instance->{results_liststore});
	$tv_column = Gtk2::TreeViewColumn->new();
	$tv_column->set_sort_column_id(0);
	$renderer = Gtk2::CellRendererText->new();
	$tv_column->pack_start($renderer, FALSE);
	$tv_column->set_attributes($renderer, "text" => 0);
	$instance->{results_treeview}->append_column($tv_column);
	$instance->{results_treeview}->set_search_column(0);
	$instance->{results_treeview}->
	    set_search_equal_func(\&treeview_column_searcher);

	# Disable the appropriate widgets by default.

	for my $widget (@{$instance->{date_sensitive_group}},
			@{$instance->{size_sensitive_group}})
	{
	    $widget->set_sensitive(FALSE);
	}

	# Register the window for management and set up the help callbacks.

	$wm->manage($instance,
		    $window_type,
		    $instance->{window},
		    $instance->{stop_button});
	register_help_callbacks
	    ($instance,
	     {widget   => "name_vbox",
	      help_ref => __("mtnb-ffwarc-query-fields")},
	     {widget   => "contents_vbox",
	      help_ref => __("mtnb-ffwarc-query-fields")},
	     {widget   => "properties_table",
	      help_ref => __("mtnb-ffwarc-query-fields")},
	     {widget   => "button_vbox",
	      help_ref => __("mtnb-ffwarc-query-buttons")},
	     {widget   => undef,
	      help_ref => __("mtnb-ffwarc-the-find-files-window")});

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
	$instance->{appbar}->set_progress_percentage(0);
	$instance->{appbar}->clear_stack();

    }

    $instance->{stop} = 0;
    $instance->{results_liststore}->clear();
    set_label_value($instance->{author_value_label}, "");
    set_label_value($instance->{file_id_value_label}, "");
    set_label_value($instance->{last_update_value_label}, "");
    set_label_value($instance->{file_revision_id_value_label}, "");

    # Load in the comboboxentries histories.

    handle_comboxentry_history($instance->{files_named_comboboxentry},
			       "find_files_named");
    handle_comboxentry_history($instance->{files_containing_comboboxentry},
			       "find_files_containing");
    handle_comboxentry_history($instance->{modified_by_comboboxentry},
			       "find_files_modified_by");

    return $instance;

}
#
##############################################################################
#
#   Routine      - validate_query
#
#   Description  - Check the specified query record, making sure it is valid.
#
#   Data         - $instance    : The associated window instance.
#                  $query       : The query record that is to be validated.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub validate_query($$)
{

    my($instance, $query) = @_;

    my $re_text;
    my $wm = WindowManager->instance();

    # Check that the file name glob is valid.

    $re_text = file_glob_to_regexp($query->{file_glob});
    eval
    {
	qr/$re_text/;
    };
    if ($@)
    {
	my $dialog = Gtk2::MessageDialog->new
	    ($instance->{window},
	     ["modal"],
	     "warning",
	     "close",
	     __x("`{pattern}' is an invalid\nfile name pattern.",
		 pattern => $query->{file_glob}));
	$wm->allow_input(sub { $dialog->run(); });
	$dialog->destroy();
	return;
    }

    # Check that the search pattern is valid.

    if ($query->{contents_pattern_is_regexp})
    {
	$re_text = file_glob_to_regexp($query->{contents_pattern});
	eval
	{
	    qr/$re_text/;
	};
	if ($@)
	{
	    my $dialog = Gtk2::MessageDialog->new
		($instance->{window},
		 ["modal"],
		 "warning",
		 "close",
		 __x("`{pattern}' is an invalid\ncontent search pattern.",
		     pattern => $query->{contents_pattern}));
	    $wm->allow_input(sub { $dialog->run(); });
	    $dialog->destroy();
	    return;
	}
    }

    # Check that any date range is the right way around.

    if (exists($query->{older_date})
	&& $query->{older_date} ge $query->{younger_date})
    {
	my $dialog = Gtk2::MessageDialog->new
	    ($instance->{window},
	     ["modal"],
	     "warning",
	     "close",
	     __("The `between' dates are either\n"
		. "the same or the wrong way round."));
	$wm->allow_input(sub { $dialog->run(); });
	$dialog->destroy();
	return;
    }

    # Check that the duration period is not too large.

    if (exists($query->{period}))
    {
	my($month, $year) = (localtime())[4, 5];
	if ($query->{period_units} == DURATION_MONTHS
	    && $query->{period} > ((($year - 70) * 12) + $month))
	{
	    my $dialog = Gtk2::MessageDialog->new
		($instance->{window},
		 ["modal"],
		 "warning",
		 "close",
		 __x("A duration of {months} months is too long.",
		     months => $query->{period}));
	    $wm->allow_input(sub { $dialog->run(); });
	    $dialog->destroy();
	    return;
	}
	elsif ($query->{period_units} == DURATION_YEARS
	       && $query->{period} > ($year - 70))
	{
	    my $dialog = Gtk2::MessageDialog->new
		($instance->{window},
		 ["modal"],
		 "warning",
		 "close",
		 __x("A duration of {years} years is too long.",
		     years => $query->{period}));
	    $wm->allow_input(sub { $dialog->run(); });
	    $dialog->destroy();
	    return;
	}
    }

    return 1;

}
#
##############################################################################
#
#   Routine      - save_query_from_gui
#
#   Description  - Saves the query information from the find files window into
#                  a record.
#
#   Data         - $instance    : The associated window instance.
#                  Return Value : A reference to the newly created query
#                                 record.
#
##############################################################################



sub save_query_from_gui($)
{

    my $instance = $_[0];

    my %query;

    # Do the name page.

    $query{file_glob} =
	$instance->{files_named_comboboxentry}->child()->get_text();
    $query{file_glob_case_sensitive} =
	$instance->{name_case_sensitive_checkbutton}->get_active() ? 1 : 0;
    $query{file_glob_search_subdirectories} =
	$instance->{search_subdirectories_checkbutton}->get_active() ? 1 : 0;

    # Do the contents page.

    $query{contents_pattern} =
	$instance->{files_containing_comboboxentry}->child()->get_text();
    $query{contents_case_sensitive} =
	$instance->{contents_case_sensitive_checkbutton}->get_active() ? 1 : 0;
    $query{contents_pattern_is_regexp} =
	$instance->{regular_expression_checkbutton}->get_active() ? 1 : 0;
    $query{contents_search_binary_files} =
	$instance->{search_binary_files_checkbutton}->get_active() ? 1 : 0;

    # Do the properties page.

    if ($instance->{date_range_checkbutton}->get_active())
    {
	if ($instance->{between_range_radiobutton}->get_active())
	{
	    $query{older_date} =
		strftime("%Y-%m-%dT%H:%M:%S",
			 localtime($instance->{older_date_dateedit}->
				   get_time()));
	    $query{younger_date} =
		strftime("%Y-%m-%dT%H:%M:%S",
			 localtime($instance->{younger_date_dateedit}->
				   get_time()));
	}
	else
	{

	    # Please note that the update method needs to be called on the
	    # spinbutton so as to make sure that it's internal state is
	    # completely up to date (the user might have entered a value
	    # directly into the entry field). Updates are usually done when it
	    # looses the focus, however this window does not make use of any
	    # focus stealing buttons.

	    $instance->{time_spinbutton}->update();
	    $query{period} =
		$instance->{time_spinbutton}->get_value_as_int();
	    $query{period_units} =
		$instance->{time_units_combobox}->get_active();

	}
    }
    if ($instance->{size_comparitor_combobox}->get_active() != CMP_ANY_SIZE)
    {
	$query{file_size} = $instance->{size_spinbutton}->get_value_as_int();
	$query{file_size_operator} =
	    $instance->{size_comparitor_combobox}->get_active();
	$query{file_size_units} =
	    $instance->{size_units_combobox}->get_active();
    }
    $query{modified_by} =
	$instance->{modified_by_comboboxentry}->child()->get_text();

    return \%query;

}

1;

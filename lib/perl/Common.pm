##############################################################################
#
#   File Name    - Common.pm
#
#   Description  - The common module for the mtn-browse application. This
#                  module contains assorted general purpose routines used
#                  throughout the application.
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

use strict;
use warnings;

# ***** GLOBAL DATA DECLARATIONS *****

# Constants for various parameters used in detecting binary data.

use constant CHUNK_SIZE => 10240;
use constant THRESHOLD  => 20;

# The saved directory locations where assorted Gtk2::FileChooserDialog dialog
# windows were last used.

my %file_chooser_dir_locations;

# A map for converting Gnome help references into valid file based URLs for the
# HTML help mode (used when yelp is not available).

my %help_ref_to_url_map;

# ***** FUNCTIONAL PROTOTYPES *****

# Public routines.

sub cache_extra_file_info($$$);
sub colour_to_string($);
sub create_format_tags($);
sub data_is_binary($);
sub display_help(;$);
sub display_html($);
sub file_glob_to_regexp($);
sub generate_tmp_path($);
sub get_branch_revisions($$$$$);
sub get_dir_contents($$$);
sub get_file_details($$$$$$);
sub get_revision_ids($$;$);
sub glade_signal_autoconnect($$);
sub handle_comboxentry_history($$;$);
sub hex_dump($);
sub open_database($$$);
sub register_help_callbacks($@);
sub run_command($@);
sub save_as_file($$$);
sub set_label_value($$);
sub treeview_column_searcher($$$$);
sub treeview_setup_search_column_selection($@);

# Private routines.

sub build_help_ref_to_url_map();
#
##############################################################################
#
#   Routine      - generate_tmp_path
#
#   Description  - Generate a unique and temporary path for the specified file
#                  name. The file name is included in the result and will be
#                  unchanged.
#
#   Data         - $file_name   : The file name component that is to be used.
#                  Return Value : The full, unique, temporary path on success,
#                                 otherwise undef on failure.
#
##############################################################################



sub generate_tmp_path($)
{

    my $file_name = $_[0];

    my($path,
       $i);

    # Loop through looking for a temporary subdirectory not containing the
    # specified file.

    for ($i = 0; ; ++ $i)
    {
	if (-d File::Spec->catfile($tmp_dir, $i))
	{
	    if (! -e ($path = File::Spec->catfile($tmp_dir, $i, $file_name)))
	    {
		return $path;
	    }
	}
	else
	{
	    return unless mkdir(File::Spec->catfile($tmp_dir, $i));
	    return File::Spec->catfile($tmp_dir, $i, $file_name);
	}
    }

    return;

}
#
##############################################################################
#
#   Routine      - run_command
#
#   Description  - Run the specified command and return its output.
#
#   Data         - $buffer      : A reference to the buffer that is to contain
#                                 the output from the command.
#                  $args        : A list containing the command to run and its
#                                 arguments.
#                  Return Value : True if the command worked, otherwise false
#                                 if something went wrong.
#
##############################################################################



sub run_command($@)
{

    my($buffer, @args) = @_;

    my(@err,
       $fd_err,
       $fd_in,
       $fd_out,
       $pid,
       $ret_val,
       $status,
       $stop,
       $total_bytes,
       $watcher);

    # Run the command.

    $fd_err = gensym();
    eval
    {
	$pid = open3($fd_in, $fd_out, $fd_err, @args);
    };
    if ($@ ne "")
    {
	my $dialog = Gtk2::MessageDialog->new
	    (undef,
	     ["modal"],
	     "warning",
	     "close",
	     __x("The {name} subprocess could not start,\n"
		     . "the system gave:\n<b><i>{error_message}</b></i>",
		 name => Glib::Markup::escape_text($args[0]),
		 error_message => Glib::Markup::escape_text($@)));
	WindowManager->instance()->allow_input(sub { $dialog->run(); });
	$dialog->destroy();
	return;
    }

    # Setup a watch handler to get read our data and handle GTK2 events whilst
    # the command is running.

    $stop = $total_bytes = 0;
    $$buffer = "";
    $watcher = Gtk2::Helper->add_watch
	(fileno($fd_out), "in",
	 sub {
	     my $bytes_read;
	     if (($bytes_read = sysread($fd_out,
					$$buffer,
					32768,
					$total_bytes))
		 == 0)
	     {
		 $stop = 1;
	     }
	     else
	     {
		 $total_bytes += $bytes_read;
	     }
	     return TRUE;
	 });
    while (! $stop)
    {
	Gtk2->main_iteration();
    }
    Gtk2::Helper->remove_watch($watcher);

    # Get any error output.

    @err = readline($fd_err);

    close($fd_in);
    close($fd_out);
    close($fd_err);

    # Reap the process and deal with any errors.

    if (($ret_val = waitpid($pid, 0)) == -1)
    {
	if ($! != ECHILD)
	{
	    my $dialog = Gtk2::MessageDialog->new_with_markup
		(undef,
		 ["modal"],
		 "warning",
		 "close",
		 __x("waitpid failed with:\n<b><i>{error_message}</i></b>",
		     error_message => Glib::Markup::escape_text($!)));
	    WindowManager->instance()->allow_input(sub { $dialog->run(); });
	    $dialog->destroy();
	    return;
	}
    }
    $status = $?;
    if (WIFEXITED($status) && WEXITSTATUS($status) != 0)
    {
	my $dialog = Gtk2::MessageDialog->new_with_markup
	    (undef,
	     ["modal"],
	     "warning",
	     "close",
	     __x("The {name} subprocess failed with an exit status\n"
		     . "of {exit_code} and printed the following on stderr:\n"
		     . "<b><i>{error_message}</i></b>",
		 name => Glib::Markup::escape_text($args[0]),
		 exit_code => WEXITSTATUS($status),
		 error_message => Glib::Markup::escape_text(join("", @err))));
	WindowManager->instance()->allow_input(sub { $dialog->run(); });
	$dialog->destroy();
	return;
    }
    elsif (WIFSIGNALED($status))
    {
	my $dialog = Gtk2::MessageDialog->new
	    (undef,
	     ["modal"],
	     "warning",
	     "close",
	     __x("The {name} subprocess was terminated by signal {number}.",
		 name   => Glib::Markup::escape_text($args[0]),
		 number => WTERMSIG($status)));
	WindowManager->instance()->allow_input(sub { $dialog->run(); });
	$dialog->destroy();
	return;
    }

    return 1;

}
#
##############################################################################
#
#   Routine      - get_dir_contents
#
#   Description  - Given a path and a Monotone manifest, return a subset of
#                  the manifest that represents the contents of just that
#                  directory along with the directory entry names.
#
#   Data         - $path     : The path to the directory from the top level of
#                              the manifest.
#                  $manifest : A reference to a Monotone manifest.
#                  $result   : A reference to a list that is to contain the
#                              result (a list of records containing the short
#                              directory entry name and a reference to the
#                              related manifest entry).
#
##############################################################################



sub get_dir_contents($$$)
{

    my($path, $manifest, $result) = @_;

    my($entry,
       $extract_re,
       $match_re,
       $name);

    if ($path eq "")
    {
	$match_re = qr/^[^\/]+$/;
	$extract_re = qr/^([^\/]+)$/;
    }
    else
    {
	$match_re = qr/^${path}\/[^\/]+$/;
	$extract_re = qr/^${path}\/([^\/]+)$/;
    }
    @$result = ();
    foreach $entry (@$manifest)
    {
	if ($entry->{name} =~ m/$match_re/)
	{
	    ($name) = ($entry->{name} =~ m/$extract_re/);
	    push(@$result, {manifest_entry => $entry, name => $name});
	}
    }

}
#
##############################################################################
#
#   Routine      - open_database
#
#   Description  - Allows the user to select a Monotone Database and then
#                  opens it, making sure that it is a valid database or
#                  dealing with the consequences if it isn't.
#
#   Data         - $parent      : The parent window for any dialogs that are
#                                 to be displayed.
#                  $mtn         : A reference to a variable that is to contain
#                                 the newly created Monotone::AutomateStdio
#                                 object. This parameter can be undef if the
#                                 object is not required.
#                  $file_name   : A reference to a variable that is to contain
#                                 the full file name of the selected database.
#                                 This parameter can be undef if the file name
#                                 is not required.
#                  Return Value : True on success, otherwise false on
#                                 cancellation.
#
##############################################################################



sub open_database($$$)
{

    my($parent, $mtn, $file_name) = @_;

    my($chooser_dialog,
       $done,
       $ret_val);

    $chooser_dialog = Gtk2::FileChooserDialog->new(__("Open Database"),
						   $parent,
						   "open",
						   "gtk-cancel" => "cancel",
						   "gtk-open" => "ok");
    $chooser_dialog->
	set_current_folder($file_chooser_dir_locations{open_db_dir})
	if (exists($file_chooser_dir_locations{open_db_dir}));

    do
    {
	if ($chooser_dialog->run() eq "ok")
	{

	    my($err,
	       $fh,
	       $fname,
	       $mtn_obj);

	    $fname = $chooser_dialog->get_filename();

	    # The user has selected a file. First make sure we can open it for
	    # reading (I know I could use the -r test but this takes care of
	    # any other unforeseen access problems as well).

	    if (! defined($fh = IO::File->new($fname, "r")))
	    {
		my $dialog = Gtk2::MessageDialog->new
		    ($parent,
		     ["modal"],
		     "warning",
		     "close",
		     $! . ".");
		$dialog->run();
		$dialog->destroy();
	    }
	    else
	    {

		$fh->close();
		$fh = undef;

		# Ok it is a readable file, try and open it but deal with any
		# errors in a nicer way than normal.

		CachingAutomateStdio->register_error_handler(MTN_SEVERITY_ALL);
		eval
		{
		    $mtn_obj = CachingAutomateStdio->new($fname);
		};
		$err = $@;
		CachingAutomateStdio->register_error_handler
		    (MTN_SEVERITY_ALL, \&mtn_error_handler);
		if ($err ne "")
		{
		    my $dialog = Gtk2::MessageDialog->new
			($parent,
			 ["modal"],
			 "warning",
			 "close",
			 __("Not a valid Monotone database."));
		    $dialog->run();
		    $dialog->destroy();
		}
		else
		{

		    # Seems to be ok so tell the caller.

		    $$mtn = $mtn_obj if (defined($mtn));
		    $$file_name = $fname if (defined($file_name));
		    $done = $ret_val = 1;

		}

	    }

	}
	else
	{
	    $done = 1;
	}
    }
    while (! $done);

    $file_chooser_dir_locations{open_db_dir} =
	$chooser_dialog->get_current_folder();
    $chooser_dialog->destroy();

    return $ret_val;

}
#
##############################################################################
#
#   Routine      - save_as_file
#
#   Description  - Allows the user to save the specified data as a file on
#                  disk.
#
#   Data         - $parent    : The parent window for any dialogs that are to
#                               be displayed.
#                  $file_name : The suggested name of the file that is to be
#                               saved.
#                  $data      : A reference to a variable containing the raw
#                               file data.
#
##############################################################################



sub save_as_file($$$)
{

    my($parent, $file_name, $data) = @_;

    my($chooser_dialog,
       $continue,
       $done);

    $chooser_dialog = Gtk2::FileChooserDialog->new(__("Save As"),
						   $parent,
						   "save",
						   "gtk-cancel" => "cancel",
						   "gtk-save" => "ok");
    $chooser_dialog->set_current_name($file_name) if ($file_name ne "");
    $chooser_dialog->
	set_current_folder($file_chooser_dir_locations{save_as_dir})
	if (exists($file_chooser_dir_locations{save_as_dir}));

    do
    {
	if ($chooser_dialog->run() eq "ok")
	{

	    my($fh,
	       $fname);

	    $continue = 1;
	    $fname = $chooser_dialog->get_filename();

	    # See if the file exists, if so then get a confirmation from the
	    # user.

	    if (-e $fname)
	    {
		my $dialog = Gtk2::MessageDialog->new
		    ($parent,
		     ["modal"],
		     "question",
		     "yes-no",
		     __("File already exists.\nDo you want to replace it?"));
		$dialog->set_title(__("Confirm"));
		$continue = 0 if ($dialog->run() ne "yes");
		$dialog->destroy();
	    }

	    if ($continue)
	    {

		# Attempt to save the contents to the file.

		if (! defined($fh = IO::File->new($fname, "w")))
		{
		    my $dialog = Gtk2::MessageDialog->new
			($parent,
			 ["modal"],
			 "warning",
			 "close",
			 __x("{error_message}.", error_message => $!));
		    $dialog->run();
		    $dialog->destroy();
		}
		else
		{
		    binmode($fh);
		    $fh->print($$data);
		    $fh->close();
		    $done = 1;
		}

	    }

	}
	else
	{
	    $done = 1;
	}
    }
    while (! $done);

    $file_chooser_dir_locations{save_as_dir} =
	$chooser_dialog->get_current_folder();
    $chooser_dialog->destroy();

}
#
##############################################################################
#
#   Routine      - treeview_setup_search_column_selection
#
#   Description  - Setup the specified treeview column headers so that the
#                  user can select which column to search in.
#
#   Data         - $treeview  : The treeview widget that is to have this
#                               feature enabled on it.
#                  @columns   : A list of column numbers that are to be setup.
#
##############################################################################



sub treeview_setup_search_column_selection($@)
{

    my($treeview, @columns) = @_;

    foreach my $col_nr (@columns)
    {

	my($button,
	   $col,
	   $label);

	next unless (defined($col = $treeview->get_column($col_nr)));

	# We need to add a widget if we are going to get back a button widget
	# from $treeview->get_parent() (this is just how Gtk2 works, I guess
	# the header widgets are by default some sort of cut down affair).

	$label = Gtk2::Label->new($col->get_title());
	$col->set_widget($label);
	$label->show();

	# Find the header button widget.

	for ($button = $col->get_widget();
	     defined($button) && ! $button->isa("Gtk2::Button");
	     $button = $button->get_parent())
	{
	}
	next unless (defined($button));

	# Attach a mouse button press event callback to the column header
	# button.

	$button->signal_connect
	    ("button_press_event",
	     sub {

		 my($widget, $event, $data) = @_;

		 # We are only interested in right button mouse clicks.

		 return FALSE unless ($event->button() == 3);

		 my($menu,
		    $menu_item);

		 # Create a popup menu with the search option in it.

		 $menu = Gtk2::Menu->new();
		 $menu_item =
		     Gtk2::MenuItem->new(__("Select As Search Column"));
		 $menu->append($menu_item);
		 $menu_item->show();

		 # Setup a callback that will set up that column for searchin
		 # if the user should select the option.

		 $menu_item->signal_connect
		     ("activate",
		      sub {

			  my($widget, $data) = @_;

			  $data->{treeview}->
			      set_search_column($data->{col_nr});

		      },
		      $data);

		 # Display the popup menu.

		 $menu->popup(undef,
			      undef,
			      undef,
			      undef,
			      $event->button(),
			      $event->time());

		 return TRUE;

	     },
	     {treeview => $treeview,
	      col_nr   => $col_nr});

    }

}
#
##############################################################################
#
#   Routine      - treeview_column_searcher
#
#   Description  - Callback routine used for comparing search terms with data
#                  inside a particular treeview's cell.
#
#   Data         - $model       : The underlying data model used by the
#                                 treeview.
#                  $column      : The number of the search column.
#                  $key         : The data that is to be searched for.
#                  $iter        : The treeview iterator for the row that is to
#                                 be compared.
#                  Return Value : TRUE if there is no match, otherwise FALSE
#                                 if there is.
#
##############################################################################



sub treeview_column_searcher($$$$)
{

    my($model, $column, $key, $iter) = @_;

    my($re,
       $value);

    # Get the value in the treeview's cell.

    $value = $model->get($iter, $column);

    # Compile the user's search term (either as a regular expression or plain
    # text depending upon the user's preferences) and return a no-match if it
    # doesn't compile.

    eval
    {
	if ($user_preferences->{list_search_as_re})
	{
	    $re = qr/$key/;
	}
	else
	{
	    $re = qr/\Q$key\E/;
	}
    };
    return TRUE if ($@ ne "");

    # Actually do the match.

    if ($value =~ m/$re/)
    {
	return FALSE;
    }
    else
    {
	return TRUE;
    }

}
#
##############################################################################
#
#   Routine      - get_branch_revisions
#
#   Description  - Get a list of revision ids or tags for the specified branch
#                  that take into account the user's preferences for ordering
#                  and the maximum number of revisions to display.
#
#   Data         - $mtn       : The Monotone::AutomateStdio object that is to
#                               be used.
#                  $branch    : The name of the branch that revisions are to
#                               be found for.
#                  $tags      : True if the list of revisions are to be tags,
#                               otherwise false if they are to be ids.
#                  $appbar    : If defined, the application progress bar
#                               widget that is to be updated with the progress
#                               of this operation. It is assumed that the
#                               progress is set at 0 and will end up being set
#                               to 1.
#                  $revisions : A reference to a list that is to contain the
#                               resultant list of sorted revision tags or ids.
#
##############################################################################



sub get_branch_revisions($$$$$)
{

    my($mtn, $branch, $tags, $appbar, $revisions) = @_;

    @$revisions = ();

    if ($tags)
    {

	my(%rev_id_to_tags,
	   %seen,
	   @sorted_rev_ids,
	   @tags);

	# Get the list of revision tags.

	$mtn->tags(\@tags, $branch);
	$appbar->set_progress_percentage(0.5) if (defined($appbar));
	WindowManager->update_gui();

	# Does the list need truncating (in which case we need to sort by date
	# to keep the most recent tags) or does the user want to sort tags by
	# date?

	if (($user_preferences->{query}->{tagged}->{limit} > 0
	     && scalar(@tags) > $user_preferences->{query}->{tagged}->{limit})
	    || $user_preferences->{query}->{tagged}->{sort_chronologically})
	{

	    # Yes tags are to be either sorted by date or need to be truncated
	    # (requiring them to temporarily be sorted by date).

	    # Build up a hash mapping revision id to tag(s).

	    foreach my $tag (@tags)
	    {
		if (exists($rev_id_to_tags{$tag->{revision_id}}))
		{
		    push(@{$rev_id_to_tags{$tag->{revision_id}}}, $tag->{tag});
		}
		else
		{
		    $rev_id_to_tags{$tag->{revision_id}} = [$tag->{tag}];
		}
	    }

	    # Sort the revision ids into date order (youngest first).

	    $mtn->toposort(\@sorted_rev_ids, keys(%rev_id_to_tags));
	    @sorted_rev_ids = reverse(@sorted_rev_ids);

	    # Now build up a list of tags based on this ordering, deduping
	    # items and stopping when we have enough tags.

	    revision: foreach my $rev_id (@sorted_rev_ids)
	    {
		foreach my $tag (sort(@{$rev_id_to_tags{$rev_id}}))
		{
		    push(@$revisions, $tag) if (! $seen{$tag} ++);
		    last revision
			if ($user_preferences->{query}->{tagged}->{limit} > 0
			    && scalar(@$revisions) >=
			        $user_preferences->{query}->{tagged}->{limit});
		}
	    }

	}
	else
	{

	    # No tags are to be sorted by name, without truncation.

	    # At this stage simply extract the tags and dedupe them.

	    @$revisions = map($_->{tag}, grep(! $seen{$_->{tag}} ++, @tags));

	}

	# We now have a list of tags in @$revisions of the correct size and
	# sorted by date if so required by the user. So resort the list
	# aplhabetically if required.

	@$revisions = sort(@$revisions)
	    if (! $user_preferences->{query}->{tagged}->
		    {sort_chronologically});

    }
    else
    {

	# Get the list of revision ids, if no branch is specified then get all
	# of the revisions within the database.

	$mtn->select($revisions,
		     ((defined($branch) && $branch ne "") ? "b:" : "i:")
		         . $branch);

	# Does it need truncating?

	if ($user_preferences->{query}->{id}->{limit} == 0
	    || scalar(@$revisions)
	        <= $user_preferences->{query}->{id}->{limit})
	{

	    # No so simply sort it.

	    if ($user_preferences->{query}->{id}->{sort_chronologically})
	    {
		$appbar->set_progress_percentage(0.33) if (defined($appbar));
		WindowManager->update_gui();
		$mtn->toposort($revisions, @$revisions);
		$appbar->set_progress_percentage(0.66) if (defined($appbar));
		WindowManager->update_gui();
		@$revisions = reverse(@$revisions);
	    }
	    else
	    {
		$appbar->set_progress_percentage(0.5) if (defined($appbar));
		WindowManager->update_gui();
		@$revisions = sort(@$revisions);
	    }

	}
	else
	{

	    # Yes so truncate and then sort it.

	    $appbar->set_progress_percentage(0.33) if (defined($appbar));
	    WindowManager->update_gui();
	    $mtn->toposort($revisions, @$revisions);
	    $appbar->set_progress_percentage(0.66) if (defined($appbar));
	    splice(@$revisions,
		   0,
		   scalar(@$revisions)
		       - $user_preferences->{query}->{id}->{limit});
	    if ($user_preferences->{query}->{id}->{sort_chronologically})
	    {
		@$revisions = reverse(@$revisions);
	    }
	    else
	    {
		@$revisions = sort(@$revisions);
	    }

	}

    }

    $appbar->set_progress_percentage(1) if (defined($appbar));
    WindowManager->update_gui();

}
#
##############################################################################
#
#   Routine      - get_revision_ids
#
#   Description  - Return the currently selected revision id, whether this is
#                  specified via a tag or as a revision id.
#
#   Data         - $instance     : The window instance.
#                  $revision_ids : A reference to a list that is to contain
#                                  the revision ids. Normally the list will
#                                  have at most one element but may contain
#                                  more if the tag isn't unique on the current
#                                  branch.
#                  $tag          : A reference to a variable that is to
#                                  contain the tag name that the user selected
#                                  or undef if the user selected a revision id
#                                  directly. This is optional.
#
##############################################################################



sub get_revision_ids($$;$)
{

    my($instance, $revision_ids, $tag) = @_;

    @$revision_ids=();
    $$tag = undef if (defined($tag));
    return unless ($instance->{revision_combo_details}->{complete});
    if ($instance->{tagged_checkbutton}->get_active())
    {
	my $query = "";
	$query = "b:" . $instance->{branch_combo_details}->{value} . "/"
	    if ($instance->{branch_combo_details}->{complete});
	$query .= "t:" . $instance->{revision_combo_details}->{value};
	$instance->{mtn}->select($revision_ids, $query);
	$$tag = $instance->{revision_combo_details}->{value}
	    if (defined($tag));
    }
    else
    {
	push(@$revision_ids, $instance->{revision_combo_details}->{value});
    }

}
#
##############################################################################
#
#   Routine      - cache_extra_file_info
#
#   Description  - Cache extra information about a file in its manifest entry
#                  record.
#
#   Data         - $mtn            : The Monotone::AutomateStdio object that
#                                    is to be used.
#                  $revision_id    : The revision id from where the search for
#                                    the latest file update is to start,
#                                    working backwards.
#                  $manifest_entry : A reference to the file's manifest entry.
#
##############################################################################



sub cache_extra_file_info($$$)
{

    my($mtn, $revision_id, $manifest_entry) = @_;

    get_file_details($mtn,
		     $revision_id,
		     $manifest_entry->{name},
		     \$manifest_entry->{author},
		     \$manifest_entry->{last_update},
		     \$manifest_entry->{last_changed_revision});

}
#
##############################################################################
#
#   Routine      - get_file_details
#
#   Description  - Get the details of the specified file.
#
#   Data         - $mtn                   : The Monotone::AutomateStdio object
#                                           that is to be used.
#                  $revision_id           : The revision id from where the
#                                           search for the latest file update
#                                           is to start, working backwards.
#                  $file_name             : The full path name of the file.
#                  $author                : A reference to the variable that
#                                           is to contain the author's
#                                           identity.
#                  $last_update           : A reference to the variable that
#                                           is to contain the last updated
#                                           date for the file.
#                  $last_changed_revision : A reference to the variable that
#                                           is to contain the revision id on
#                                           which the file was last updated.
#
##############################################################################



sub get_file_details($$$$$$)
{

    my($mtn,
       $revision_id,
       $file_name,
       $author,
       $last_update,
       $last_changed_revision) = @_;

    my(@certs_list,
       @revision_list);

    $mtn->get_content_changed(\@revision_list, $revision_id, $file_name);
    $$last_changed_revision = $revision_list[0];
    $mtn->certs(\@certs_list, $revision_list[0]);
    $$author = $$last_update = "";
    foreach my $cert (@certs_list)
    {
	if ($cert->{name} eq "author")
	{
	    $$author = $cert->{value};
	}
	elsif ($cert->{name} eq "date")
	{
	    $$last_update = $cert->{value};
	}
	last if ($$author ne "" && $$last_update ne "");
    }

}
#
##############################################################################
#
#   Routine      - file_glob_to_regexp
#
#   Description  - Converts the specified string containing a file name style
#                  glob into a regular expression.
#
#   Data         - $file_glob   : The file name wildcard that is to be
#                                 converted.
#                  Return Value : The resultant regular expression string.
#
##############################################################################



sub file_glob_to_regexp($)
{

    my $file_glob = $_[0];

    my($escaping,
       $first,
       $re_text);

    $escaping = 0;
    $first = 1;
    $re_text = "^";
    foreach my $char (split(//, $file_glob))
    {
	if ($first)
	{
	    $re_text .= "(?=[^\\.])" unless $char eq ".";
	    $first = 0;
	}
	if (".+^\$\@%()|" =~ m/\Q$char\E/)
	{
	    $re_text .= "\\" . $char;
	}
	elsif ($char eq "*")
	{
	    $re_text .= $escaping ? "\\*" : "[^/]*";
	}
	elsif ($char eq "?")
	{
	    $re_text .= $escaping ? "\\?" : "[^/]";
	}
	elsif ($char eq "\\")
	{
	    if ($escaping)
	    {
		$re_text .= "\\\\";
		$escaping = 0;
	    }
	    else
	    {
		$escaping = 1;
	    }
	}
	else
	{
	    $re_text .= "\\" if ($escaping && $char eq "[");
	    $re_text .= $char;
	    $escaping = 0;
	}
    }
    $re_text .= "\$";

    return $re_text;

}
#
##############################################################################
#
#   Routine      - handle_comboxentry_history
#
#   Description  - Handle comboboxentry histories. Histories are limited to a
#                  small fixed value and are stored to disk in the user's
#                  preferences file.
#
#   Data         - $widget       : The comboboxentry that is to be updated.
#                  $history_name : The name of the history list that is to be
#                                  updated or loaded.
#                  $value        : The new value that is to be added to the
#                                  specified history list and comboboxentry or
#                                  undef if the comboboxentry is just to be
#                                  updated with the current history list. This
#                                  is optional.
#
##############################################################################



sub handle_comboxentry_history($$;$)
{

    my($widget, $history_name, $value) = @_;

    my $update_history = 1;
    my $history_ref = $user_preferences->{histories}->{$history_name};

    # Update the comboxentry history list and save it to disk.

    if (defined($value))
    {
	if ($value ne "")
	{
	    foreach my $entry (@$history_ref)
	    {
		if ($entry eq $value)
		{
		    $update_history = 0;
		    last;
		}
	    }
	}
	else
	{
	    $update_history = 0;
	}
	if ($update_history)
	{
	    splice(@$history_ref, $user_preferences->{history_size})
		if (unshift(@$history_ref, $value) >
		    $user_preferences->{history_size});
	    eval
	    {
		save_preferences($user_preferences);
	    };
	    if ($@ ne "")
	    {
		chomp($@);
		my $dialog = Gtk2::MessageDialog->new
		    (undef,
		     ["modal"],
		     "warning",
		     "close",
		     __("Your preferences could not be saved:\n") . $@);
		WindowManager->instance()->allow_input
		    (sub { $dialog->run(); });
		$dialog->destroy();
	    }
	}
    }

    # Update the comboboxentry itself if necessary.

    if ($update_history)
    {
	my $text_entry_value = $widget->child()->get_text();
	$widget->get_model()->clear();
	foreach my $entry (@$history_ref)
	{
	    $widget->append_text($entry);
	}
	$widget->child()->set_text($text_entry_value);
    }

}
#
##############################################################################
#
#   Routine      - display_help
#
#   Description  - Displays the specified help section either in Gnome's
#                  native help browser or the specified HTML viewer, depending
#                  upon installation settings.
#
#   Data         - $section - The help section to display. If undef is given
#                             then the content page is displayed. This is
#                             optional.
#
##############################################################################



sub display_help(;$)
{

    my $section = $_[0];

    if (HTML_VIEWER_CMD eq "")
    {

	# Simply let Gnome handle it using yelp.

	if (defined($section))
	{
	    Gnome2::Help->display("mtn-browse.xml", $section);
	}
	else
	{
	    Gnome2::Help->display("mtn-browse.xml");
	}

    }
    else
    {

	my $url;

	# Use the specified HTML viewer to display the help section.

	$section = "index" unless defined($section);
	if (exists($help_ref_to_url_map{$section}))
	{
	    $url = $help_ref_to_url_map{$section};
	}
	else
	{
	    $url = $help_ref_to_url_map{"index"};
	}
	if (! defined($url) || $url eq "")
	{
	    my $dialog = Gtk2::MessageDialog->new
		(undef,
		 ["modal"],
		 "warning",
		 "close",
		 __("The requested help section\n"
		    . "cannot be found or is not known."));
	    WindowManager->instance()->allow_input(sub { $dialog->run(); });
	    $dialog->destroy();
	    return;
	}
	display_html($url);

    }

}
#
##############################################################################
#
#   Routine      - display_html
#
#   Description  - Displays the specified HTML URL either in Gnome's native
#                  web browser or the specified HTML viewer, depending upon
#                  installation settings.
#
#   Data         - $url - The HTML URL that is to be displayed.
#
##############################################################################



sub display_html($)
{

    my $url = $_[0];

    if (HTML_VIEWER_CMD eq "")
    {

	# Simply let Gnome handle it.

	Gnome2::URL->show($url);

    }
    else
    {

	my $cmd = HTML_VIEWER_CMD;

	if ($cmd =~ m/\{url\}/)
	{
	    $cmd =~ s/\{url\}/$url/g;
	}
	else
	{
	    $cmd .= " " . $url;
	}

	# Launch it.

	system($cmd . " &");

    }

}
#
##############################################################################
#
#   Routine      - register_help_callbacks
#
#   Description  - Register all of the context sensitive help callbacks for
#                  the specified window instance.
#
#   Data         - $instance : The window instance.
#                  $details  : A list of records containing the widget and the
#                              help reference for that widget. If a widget is
#                              set to undef then that entry represents the
#                              default help callback for that entire window.
#
##############################################################################



sub register_help_callbacks($@)
{

    my($instance, @details) = @_;

    my $wm = WindowManager->instance();

    build_help_ref_to_url_map()
	if (HTML_VIEWER_CMD ne "" && keys(%help_ref_to_url_map) == 0);

    foreach my $entry (@details)
    {
	my $help_ref = $entry->{help_ref};
	my $widget = defined($entry->{widget})
	    ? $instance->{glade}->get_widget($entry->{widget}) : undef;
	$wm->help_connect($instance,
			  $widget,
			  sub {
			      my($widget, $instance) = @_;
			      return if ($instance->{in_cb});
			      local $instance->{in_cb} = 1;
			      display_help($help_ref);
			  });
    }

}
#
##############################################################################
#
#   Routine      - create_format_tags
#
#   Description  - Creates the Gtk2::TextBuffer tags that are used to pretty
#                  print stuff.
#
#   Data         - $text_view : The GTK2::TextBuffer widget that is to have
#                               its tags created.
#
##############################################################################



sub create_format_tags($)
{

    my $text_buffer = $_[0];

    my $colours = $user_preferences->{colours};

    # Normal Black text, assorted styles, on a white background.

    $text_buffer->create_tag("normal", "weight" => PANGO_WEIGHT_NORMAL);

    $text_buffer->create_tag("bold", "weight" => PANGO_WEIGHT_BOLD);
    $text_buffer->create_tag("italics", "style" => "italic");
    $text_buffer->create_tag("bold-italics",
			     "weight" => PANGO_WEIGHT_BOLD,
			     "style" => "italic");

    # Set up the colour and style schemes for file comparison and annotation.

    foreach my $i (1 .. 2)
    {
	my $clr = $user_preferences->{colours}->{"cmp_revision_" . $i};
	$text_buffer->create_tag("compare-" . $i,
				 "foreground" => $clr->{fg});
	$text_buffer->create_tag("bold-compare-" . $i,
				 "weight" => PANGO_WEIGHT_BOLD,
				 "foreground" => $clr->{fg});
	$text_buffer->create_tag("italics-compare-" . $i,
				 "style" => "italic",
				 "foreground" => $clr->{fg});
	$text_buffer->create_tag("bold-italics-compare-" . $i,
				 "weight" => PANGO_WEIGHT_BOLD,
				 "style" => "italic",
				 "foreground" => $clr->{fg});
	$text_buffer->create_tag("compare-file-" . $i,
				 "foreground" => $clr->{fg},
				 "background" => $clr->{bg});
	$text_buffer->create_tag("compare-file-info-" . $i,
				 "weight" => PANGO_WEIGHT_BOLD,
				 "foreground" => $clr->{hl},
				 "background" => "DarkSlateGrey");
	foreach my $prefix ("annotate_prefix_", "annotate_text_")
	{
	    my $tag = $prefix;
	    $tag =~ s/_/-/g;
	    $clr = $user_preferences->{colours}->{$prefix . $i};
	    $text_buffer->create_tag($tag . $i,
				     "foreground" => $clr->{fg},
				     "background" => $clr->{bg});
	}
    }

    # Yellow text on a grey background.

    $text_buffer->create_tag("compare-info",
			     "foreground" => "Yellow",
			     "background" => "LightSlateGrey");

}
#
##############################################################################
#
#   Routine      - hex_dump
#
#   Description  - Generates a hexadecimal dump of the specified data.
#
#   Data         - $data        : A reference to the data that is to be hex
#                                 dumped.
#                  Return Value : A reference to the resultant hex dump as a
#                                 string.
#
##############################################################################



sub hex_dump($)
{

    my $data = $_[0];

    my($buffer,
       $counter,
       @line);

    $counter = 0;
    foreach my $byte (split(//, $$data))
    {
	++ $counter;
	push(@line, $byte);
	$buffer .= sprintf("%02X ", ord($byte));
	$buffer .= " " if (($counter % 8) == 0);
	if (($counter % 16) == 0)
	{
	    foreach my $byte2 (@line)
	    {
		$buffer .= ($byte2 =~ m/[[:print:]]/) ? (" " . $byte2) : " .";
	    }
	    $buffer .= "\n";
	    @line = ();
	}
    }

    # If the last line is incomplete then finish it off.

    if (scalar(@line) > 0)
    {
	$buffer .= "   " x (16 - scalar(@line));
	$buffer .= " " if (scalar(@line) < 8);
	$buffer .= " ";
	foreach my $byte2 (@line)
	{
	    $buffer .= ($byte2 =~ m/[[:print:]]/) ? (" " . $byte2) : " .";
	}
	$buffer .= "\n";
    }

    return \$buffer;

}
#
##############################################################################
#
#   Routine      - data_is_binary
#
#   Description  - Determines whether the specified string contains binary
#                  data.
#
#   Data         - $data        : A reference to the data that is to be
#                                 tested.
#                  Return Value : True if the data is binary, otherwise false
#                                 if it is predominantly textual.
#
##############################################################################



sub data_is_binary($)
{

    my $data = $_[0];

    my($chunk,
       $length,
       $non_printable,
       $offset,
       $total_length);

    $offset = 0;
    $total_length = length($$data);
    while ($offset < $total_length)
    {
	$chunk = substr($$data, $offset, CHUNK_SIZE);
	$offset += CHUNK_SIZE;
	$length = length($chunk);
	$non_printable = grep(/[^[:print:][:space:]]/, split(//, $chunk));
	return 1 if (((100 * $non_printable) / $length) > THRESHOLD);
    }
    return;

}
#
##############################################################################
#
#   Routine      - colour_to_string
#
#   Description  - Returns a string representing the specified
#                  Gtk2::Gdk::Color value.
#
#   Data         - $colour      : A Gtk2::Gdk::Color object.
#                  Return Value : A string containing the colour value.
#
##############################################################################



sub colour_to_string($)
{

    my $colour = $_[0];

    return sprintf("#%02X%02X%02X",
		   ($colour->red() >> 8) & 0xff,
		   ($colour->green() >> 8) & 0xff,
		   ($colour->blue() >> 8) & 0xff);

}
#
##############################################################################
#
#   Routine      - set_label_value
#
#   Description  - Set the text for the given label and the tooltip for the
#                  parent widget, assumed to be an event box, to the specified
#                  text.
#
#   Data         - $widget : The label widget that has an event box as its
#                            parent.
#                  $value  : The text that the label and tooltip are to be set
#                            to.
#
##############################################################################



sub set_label_value($$)
{

    my($widget, $value) = @_;

    $widget->set_text($value);
    $tooltips->set_tip($widget->get_parent(), $value);

}
#
##############################################################################
#
#   Routine      - glade_signal_autoconnect
#
#   Description  - This routine uses the Glade library to connect up all the
#                  registered signal handlers to their related widgets.
#
#   Data         - $glade       : The Glade object describing the widgets that
#                                 are to have their signal handlers
#                                 registered.
#                  $client_data : The client data that is to be passed into
#                                 each callback routine when it is called.
#
##############################################################################



sub glade_signal_autoconnect($$)
{

    my($glade, $client_data) = @_;

    my $caller_package = caller();
    $caller_package = "main" if (! defined($caller_package));

    $glade->signal_autoconnect
	(sub {
	     my($callback_name, $widget, $signal_name, $signal_data,
		$connect_object, $after, $user_data) = @_;
	     my $func = $after ? "signal_connect_after" : "signal_connect";

	     # Need to fully qualify any callback name that isn't prefixed by
	     # it's package name with the name of the calling package.

	     $callback_name = $caller_package . "::" . $callback_name
		 if (index($callback_name, "::") < 0);

	     # Actually connect the signal handler.

	     $widget->$func($signal_name,
			    $callback_name,
			    $connect_object ? $connect_object : $user_data);
	 },
	 $client_data);

}
#
##############################################################################
#
#   Routine      - build_help_ref_to_url_map
#
#   Description  - Build up a map that translates an HTML help file link name
#                  into a fully qualified URL.
#
#   Data         - None.
#
##############################################################################



sub build_help_ref_to_url_map()
{

    my($dir,
       $dir_path,
       $fname,
       $locale,
       @lparts,
       $nr_parts,
       $prog,
       $tmp);

    # Ask Gnome where the based help directory is, failing that have an
    # educated guess.

    if (HTML_VIEWER_CMD eq ""
	&& defined($prog = Gnome2::Program->get_program()))
    {
	($dir_path) = $prog->locate_file("app-help", "mtn-browse.xml", FALSE);
	$dir_path = dirname($dir_path);
    }
    else
    {
	$dir_path = File::Spec->catfile(PREFIX_DIR,
					"share",
					"gnome",
					"help",
					APPLICATION_NAME);
    }

    # Work out the locale component, going from the most specific to the least.
    # If a specific locale directory isn't found then fall back onto the
    # POSIX/C locale.

    $locale = setlocale(LC_MESSAGES);
    @lparts = split(/[_.@]/, $locale);
    $nr_parts = scalar(@lparts);
    if (-d ($tmp = File::Spec->catfile($dir_path, $locale)))
    {
	$dir_path = $tmp;
    }
    elsif ($nr_parts >= 3
	   && -d ($tmp = File::Spec->catfile($dir_path,
					     $lparts[0] . "_"
					     . $lparts[1] . "."
					     . $lparts[2])))
    {
	$dir_path = $tmp;
    }
    elsif ($nr_parts >= 2
	   && -d ($tmp = File::Spec->catfile($dir_path,
					     $lparts[0] . "_" . $lparts[1])))
    {
	$dir_path = $tmp;
    }
    elsif ($nr_parts >= 1
	   && -d ($tmp = File::Spec->catfile($dir_path, $lparts[0])))
    {
	$dir_path = $tmp;
    }
    elsif (-d ($tmp = File::Spec->catfile($dir_path, "POSIX")))
    {
	$dir_path = $tmp;
    }
    elsif (-d ($tmp = File::Spec->catfile($dir_path, "C")))
    {
	$dir_path = $tmp;
    }
    else
    {
	return;
    }

    # Now open the directory and scan all HTML files for links.

    return unless (defined($dir = IO::Dir->new($dir_path)));
    while (defined($fname = $dir->read()))
    {
	my($file,
	   $full_name);
	$full_name = File::Spec->catfile($dir_path, $fname);
	$full_name = File::Spec->rel2abs($full_name);

	# Only scan HTML files.

	if ($fname =~ m/^.*\.html$/
	    && defined($file = IO::File->new($full_name, "r")))
	{
	    my $line;
	    while (defined($line = $file->getline()))
	    {

		my($dir_string,
		   @dirs,
		   $file_name,
		   @list,
		   $url,
		   $volume);

		# Mangle the file name into a URL.

		($volume, $dir_string, $file_name) =
		    File::Spec->splitpath($full_name);
		@dirs = File::Spec->splitdir($dir_string);
		$url = "file://";
		$url .= "/" . $volume . "/" if ($volume ne "");
		$url .= join("/", @dirs);
		$url .= "/" if ($url =~ m/.*[^\/]$/);
		$url .= $file_name;

		# Process each link of the form <a name="..."> but filter out
		# the internally generated ones (used for all figures).

		@list = ($line =~ m/<a name=\"([^\"]+)\">/g);
		foreach my $link (@list)
		{
		    $help_ref_to_url_map{$link} = $url . "#" . $link
			if ($link !~ m/^id\d+$/);
		}

		# Special case the contents page making sure that it has
		# appropiate default entries pointing to it.

		if ($fname eq "monotone-browse.html")
		{
		    $help_ref_to_url_map{""} = $url;
		    $help_ref_to_url_map{"contents"} = $url;
		    $help_ref_to_url_map{"index"} = $url;
		}

	    }
	    $file->close();
	}
    }
    $dir->close();

    return;

}

1;

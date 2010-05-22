##############################################################################
#
#   File Name    - Annotate.pm
#
#   Description  - The annotate module for the mtn-browse application. This
#                  module contains all the routines for implementing the
#                  annotation window.
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

sub display_annotation($$$);

# Private routines.

sub annotation_textview_populate_popup_cb($$$);
sub annotation_textview_popup_menu_item_cb($$);
sub compare_file_with_previous($$);
sub compare_revision_with_parent($$);
sub get_annotation_window();
sub mtn_annotate($$$$);
#
##############################################################################
#
#   Routine      - display_annotation
#
#   Description  - Display the annotated listing for the specified file.
#
#   Data         - $mtn         : The Monotone::AutomateStdio object that is
#                                 to be used to display the annotated file.
#                  $revision_id : The revision id in which the desired version
#                                 of the file resides.
#                  $file_name   : The name of the file that is to be
#                                 annotated.
#
##############################################################################



sub display_annotation($$$)
{

    my($mtn, $revision_id, $file_name) = @_;

    my($i,
       $instance,
       $iter,
       $len,
       @lines,
       $max_len,
       $padding,
       @prefix,
       $prefix_tag,
       $template,
       $text_tag);
    my $wm = WindowManager->instance();

    $instance = get_annotation_window();
    local $instance->{in_cb} = 1;

    $instance->{mtn} = $mtn;
    $instance->{file_name} = $file_name;
    $instance->{revision_id} = $revision_id;
    $instance->{window}->set_title(__x("Annotated Listing Of {file}",
				       file => $instance->{file_name}));
    $instance->{window}->show_all();
    $instance->{window}->present();

    $wm->make_busy($instance, 1);
    $instance->{appbar}->push($instance->{appbar}->get_status()->get_text());
    $wm->update_gui();

    # Get Monotone to do the annotation.

    $instance->{appbar}->set_status(__("Annotating file"));
    $wm->update_gui();
    mtn_annotate(\@lines,
		 $mtn->get_db_name(),
		 $revision_id,
		 $instance->{file_name});

    # Find the longest line for future padding and also split each line into
    # their prefix and text parts. Please note that the use of unpack un-utf8s
    # the returned strings.

    $max_len = 0;
    if (scalar(@lines) > 0)
    {
	$instance->{prefix_length} = length(($lines[0] =~ m/^([^:]+):.*$/)[0]);
    }
    else
    {
	$instance->{prefix_length} = 0;
    }
    $template = sprintf("a%da2a*", $instance->{prefix_length});
    for ($i = 0; $i < scalar(@lines); ++ $i)
    {
	($prefix[$i], $lines[$i]) = (unpack($template, $lines[$i]))[0, 2];
	eval
	{
	    $lines[$i] = decode($file_encoding, $lines[$i], Encode::FB_CROAK);
	};
	$lines[$i] =~ s/\s+$//;
	$lines[$i] = expand($lines[$i]);
	$max_len = $len if (($len = length($lines[$i])) > $max_len);
    }

    # Display the result, highlighting according to the annotate output.

    $instance->{appbar}->set_status
	(__("Formatting and displaying annotated file"));
    $wm->update_gui();
    $padding = " " x $max_len;
    $prefix_tag = $text_tag = "";
    for ($i = 0; $i < scalar(@lines); ++ $i)
    {

	# Change the colours if there is a new prefix.

	if ($prefix[$i] !~ m/^\s+$/)
	{
	    if ($prefix_tag ne "annotate-prefix-1")
	    {
		$prefix_tag = "annotate-prefix-1";
		$text_tag = "annotate-text-1";
	    }
	    else
	    {
		$prefix_tag = "annotate-prefix-2";
		$text_tag = "annotate-text-2";
	    }
	}

	# Print out the prefix.

	$instance->{annotation_buffer}->insert_with_tags_by_name
	    ($instance->{annotation_buffer}->get_end_iter(),
	     $prefix[$i] . " ",
	     $prefix_tag);

	# Print out the text.

	$instance->{annotation_buffer}->insert_with_tags_by_name
	    ($instance->{annotation_buffer}->get_end_iter(),
	     substr($lines[$i] . $padding, 0, $max_len) . "\n",
	     $text_tag);

	if (($i % 100) == 0)
	{
	    $instance->{appbar}->set_progress_percentage
		(($i + 1) / scalar(@lines));
	    $wm->update_gui();
	}

    }
    $instance->{appbar}->set_progress_percentage(1);
    $wm->update_gui();

    # Delete the trailing newline.

    $iter = $instance->{annotation_buffer}->get_end_iter();
    $instance->{annotation_buffer}->delete
	($iter, $instance->{annotation_buffer}->get_end_iter())
	if ($iter->backward_char());

    # Make sure we are at the top.

    $instance->{annotation_buffer}->
	place_cursor($instance->{annotation_buffer}->get_start_iter());
    $instance->{annotation_scrolledwindow}->get_vadjustment()->set_value(0);
    $instance->{annotation_scrolledwindow}->get_hadjustment()->set_value(0);
    $instance->{appbar}->set_progress_percentage(0);
    $instance->{appbar}->set_status("");
    $wm->update_gui();

    $instance->{appbar}->pop();
    $wm->make_busy($instance, 0);

}
#
##############################################################################
#
#   Routine      - annotation_textview_populate_popup_cb
#
#   Description  - Callback routine called when the user right clicks on any
#                  textview window.
#
#   Data         - $widget   : The widget object that received the signal.
#                  $menu     : The Gtk2::Menu widget that is to be updated.
#                  $instance : The window instance that is associated with
#                              this widget.
#
##############################################################################



sub annotation_textview_populate_popup_cb($$$)
{

    my($widget, $menu, $instance) = @_;

    return if ($instance->{in_cb});
    local $instance->{in_cb} = 1;

    my($menu_item,
       $revision_part,
       $separator,
       $start_iter,
       $x,
       $y);

    # Extract the revision id relating to the block of text directly under the
    # mouse cursor.

    ($x, $y) = ($widget->window()->get_pointer())[1 .. 2];
    ($x, $y) = $widget->window_to_buffer_coords("widget", $x, $y);
    if (defined($start_iter = ($widget->get_line_at_y($y))[0]))
    {
	my($end_iter,
	   $prefix,
	   $no_more,
	   $text_buffer);
	$end_iter = ($widget->get_line_at_y($y))[0];
	$end_iter->forward_to_line_end();
	$text_buffer = $widget->get_buffer();
	$prefix = substr($text_buffer->get_text($start_iter, $end_iter, TRUE),
			 0,
			 $instance->{prefix_length});
	while ($prefix !~ m/^ *[0-9a-f]+\.+.*$/)
	{
	    if (! $start_iter->backward_line())
	    {
		$no_more = 1;
		last;
	    }
	    $end_iter->backward_line();
	    $end_iter->forward_to_line_end()
		unless ($end_iter->ends_line());
	    $prefix = substr($text_buffer->get_text($start_iter,
						    $end_iter,
						    TRUE),
			     0,
			     $instance->{prefix_length});
	}
	($revision_part) = ($prefix =~ m/^ *([0-9a-f]+)\.+.*$/)
	    unless ($no_more);
    }

    # Add a `Display Change Log' option to the right-click menu that displays
    # the change log of the revision responsible for the text directly under
    # the mouse cursor.

    $separator = Gtk2::SeparatorMenuItem->new();
    $separator->show();
    $menu->append($separator);

    $menu_item = Gtk2::MenuItem->new(__("Display Change _Log"));
    if (! defined($revision_part))
    {
	$menu_item->set_sensitive(FALSE);
    }
    else
    {
	$menu_item->signal_connect
	    ("activate",
	     \&annotation_textview_popup_menu_item_cb,
	     {instance         => $instance,
	      cb               => sub {
		                      my($instance, $revision_id) = @_;
				      display_change_log($instance->{mtn},
							 $revision_id,
							 "",
							 undef);
				  },
	      progress_message => __("Displaying change log"),
	      revision_part    => $revision_part});
    }
    $menu_item->show();
    $menu->append($menu_item);

    $menu_item = Gtk2::MenuItem->new(__("Display File _History"));
    if (! defined($revision_part))
    {
	$menu_item->set_sensitive(FALSE);
    }
    else
    {
	$menu_item->signal_connect
	    ("activate",
	     \&annotation_textview_popup_menu_item_cb,
	     {instance         => $instance,
	      cb               => sub {
		                      my($instance, $revision_id) = @_;
				      my $old_file_name;
				      $instance->{mtn}->get_corresponding_path
					  (\$old_file_name,
					   $instance->{revision_id},
					   $instance->{file_name},
					   $revision_id);
				      display_file_change_history
					  ($instance->{mtn},
					   $revision_id,
					   $old_file_name);
				  },
	      progress_message => __("Displaying file history"),
	      revision_part    => $revision_part});
    }
    $menu_item->show();
    $menu->append($menu_item);

    $menu_item = Gtk2::MenuItem->new(__("Display _Revision History"));
    if (! defined($revision_part))
    {
	$menu_item->set_sensitive(FALSE);
    }
    else
    {
	$menu_item->signal_connect
	    ("activate",
	     \&annotation_textview_popup_menu_item_cb,
	     {instance         => $instance,
	      cb               => sub {
		                      my($instance, $revision_id) = @_;
				      display_revision_change_history
					  ($instance->{mtn},
					   undef,
					   $revision_id);
				  },
	      progress_message => __("Displaying revision history"),
	      revision_part    => $revision_part});
    }
    $menu_item->show();
    $menu->append($menu_item);

    $separator = Gtk2::SeparatorMenuItem->new();
    $separator->show();
    $menu->append($separator);

    $menu_item =
	Gtk2::MenuItem->new(__("Compare File With Previous _Version"));
    if (! defined($revision_part))
    {
	$menu_item->set_sensitive(FALSE);
    }
    else
    {
	$menu_item->signal_connect
	    ("activate",
	     \&annotation_textview_popup_menu_item_cb,
	     {instance         => $instance,
	      cb               => sub {
		                      my($instance, $revision_id) = @_;
				      compare_file_with_previous($instance,
								 $revision_id);
				  },
	      progress_message => __("Doing file comparison"),
	      revision_part    => $revision_part});
    }
    $menu_item->show();
    $menu->append($menu_item);

    $menu_item =
	Gtk2::MenuItem->new(__("Compare Revision _With Parent"));
    if (! defined($revision_part))
    {
	$menu_item->set_sensitive(FALSE);
    }
    else
    {
	$menu_item->signal_connect
	    ("activate",
	     \&annotation_textview_popup_menu_item_cb,
	     {instance         => $instance,
	      cb               => sub {
		                      my($instance, $revision_id) = @_;
				      compare_revision_with_parent
					  ($instance, $revision_id);
				  },
	      progress_message => __("Doing revision comparison"),
	      revision_part    => $revision_part});
    }
    $menu_item->show();
    $menu->append($menu_item);

}
#
##############################################################################
#
#   Routine      - annotation_textview_popup_menu_item_cb
#
#   Description  - Callback routine called when the user selects an item on
#                  the annotation textview's popup menu.
#
#   Data         - $widget  : The widget object that received the signal.
#                  $menu    : The Gtk2::Menu widget that is to be updated.
#                  $details : A reference to an anonymous hash containing the
#                             window instance, callback routine, progress
#                             message and the revision start string that is
#                             associated with this widget.
#
##############################################################################



sub annotation_textview_popup_menu_item_cb($$)
{

    my($widget, $details) = @_;

    return if ($details->{instance}->{in_cb});
    local $details->{instance}->{in_cb} = 1;

    my @revision_ids;
    my $wm = WindowManager->instance();

    $wm->make_busy($details->{instance}, 1);
    $details->{instance}->{appbar}->
	push($details->{instance}->{appbar}->get_status()->get_text());
    $details->{instance}->{appbar}->set_status($details->{progress_message});
    $wm->update_gui();

    $details->{instance}->{mtn}->
	select(\@revision_ids, "i:" . $details->{revision_part});
    if (scalar(@revision_ids) == 1)
    {
	$details->{cb}($details->{instance}, $revision_ids[0]);
    }
    else
    {
	my $dialog = Gtk2::MessageDialog->new
	    ($details->{instance}->{window},
	     ["modal"],
	     "warning",
	     "close",
	     __("Cannot access unique revision id."));
	$wm->allow_input(sub { $dialog->run(); });
	$dialog->destroy();
    }

    $details->{instance}->{appbar}->pop();
    $wm->make_busy($details->{instance}, 0);

}
#
##############################################################################
#
#   Routine      - compare_file_with_previous
#
#   Description  - Compare the annotate file at the specified version with the
#                  previous version of the file.
#
#   Data         - $instance    : The window instance that is associated with
#                                 the annotation window.
#                  $revision_id : The revision id on which this file changed.
#
##############################################################################



sub compare_file_with_previous($$)
{

    my($instance, $revision_id) = @_;

    my(@chg_ancestors,
       $file_name,
       $old_file_name,
       @parents);
    my $wm = WindowManager->instance();

    # Remember that a warning is generated when one goes back beyond a file's
    # addition revision, so temporarily disable the warning handler.

    {

	local $suppress_mtn_warnings = 1;

	# First get the name of the file at the specified revision (it might
	# have been moved or renamed).

	$instance->{mtn}->get_corresponding_path(\$file_name,
						 $instance->{revision_id},
						 $instance->{file_name},
						 $revision_id);

	# Get the revision's parent and then find out when the file last
	# changed.

	$instance->{mtn}->parents(\@parents, $revision_id);
	if (scalar(@parents) > 1)
	{
	    my $dialog = Gtk2::MessageDialog->new
		($instance->{window},
		 ["modal"],
		 "info",
		 "close",
		 __("The selected revision has more than one parent.\n"
		    . "I will display the file's history so you can select\n"
		    . "the specific parent revision."));
	    $wm->allow_input(sub { $dialog->run(); });
	    $dialog->destroy();
	    display_file_change_history($instance->{mtn},
					$revision_id,
					$file_name);
	    return;
	}
	elsif (scalar(@parents) == 0)
	{
	    my $dialog = Gtk2::MessageDialog->new
		($instance->{window},
		 ["modal"],
		 "info",
		 "close",
		 __("The selected revision has no parents."));
	    $wm->allow_input(sub { $dialog->run(); });
	    $dialog->destroy();
	    return;
	}
	$instance->{mtn}->get_content_changed(\@chg_ancestors,
					      $parents[0],
					      $file_name);
	if (scalar(@chg_ancestors) > 1)
	{
	    my $dialog = Gtk2::MessageDialog->new
		($instance->{window},
		 ["modal"],
		 "info",
		 "close",
		 __("The current version of the file probably\n"
		    . "resulted from a merge as it is directly\n"
		    . "descended from multiple versions.\n"
		    . "I will display the file's history so you\n"
		    . "can select the specific parent revision."));
	    $wm->allow_input(sub { $dialog->run(); });
	    $dialog->destroy();
	    display_file_change_history($instance->{mtn},
					$revision_id,
					$file_name);
	    return;
	}
	elsif (scalar(@chg_ancestors) == 0)
	{
	    my $dialog = Gtk2::MessageDialog->new
		($instance->{window},
		 ["modal"],
		 "info",
		 "close",
		 __("The selected file version has no ancestors."));
	    $wm->allow_input(sub { $dialog->run(); });
	    $dialog->destroy();
	    return;
	}

    }

    # Ok now make sure the file name hasn't changed. If it has then use the
    # external differences tool.

    $instance->{mtn}->get_corresponding_path(\$old_file_name,
					     $revision_id,
					     $file_name,
					     $chg_ancestors[0]);
    if ($old_file_name ne $file_name)
    {
	display_renamed_file_comparison($instance->{window},
					$instance->{mtn},
					$chg_ancestors[0],
					$old_file_name,
					$revision_id,
					$file_name);
    }
    else
    {
	display_revision_comparison($instance->{mtn},
				    $chg_ancestors[0],
				    $revision_id,
				    $file_name);
    }

}
#
##############################################################################
#
#   Routine      - compare_revision_with_parent
#
#   Description  - Compare the revision at the specified version with its
#                  parent.
#
#   Data         - $instance    : The window instance that is associated with
#                                 the annotation window.
#                  $revision_id : The revision id that is to be compared
#                                 against its parent.
#
##############################################################################



sub compare_revision_with_parent($$)
{

    my($instance, $revision_id) = @_;

    my @parents;
    my $wm = WindowManager->instance();

    # First get the revision's parent(s).

    $instance->{mtn}->parents(\@parents, $revision_id);
    if (scalar(@parents) > 1)
    {
	my $dialog = Gtk2::MessageDialog->new
	    ($instance->{window},
	     ["modal"],
	     "info",
	     "close",
	     __("The selected revision has more than one parent.\n"
		. "I will display the revision's history so you can select\n"
		. "the specific parent revision."));
	$wm->allow_input(sub { $dialog->run(); });
	$dialog->destroy();
	display_revision_change_history($instance->{mtn}, undef, $revision_id);
	return;
    }
    elsif (scalar(@parents) == 0)
    {
	my $dialog = Gtk2::MessageDialog->new
	    ($instance->{window},
	     ["modal"],
	     "info",
	     "close",
	     __("The selected revision has no parents."));
	$wm->allow_input(sub { $dialog->run(); });
	$dialog->destroy();
	return;
    }

    # Ok now compare the revisions and display the results.

    display_revision_comparison($instance->{mtn}, $parents[0], $revision_id);

}
#
##############################################################################
#
#   Routine      - get_annotation_window
#
#   Description  - Creates or prepares an existing annotation window for use.
#
#   Data         - Return Value : A reference to the newly created or unused
#                                 annotation instance record.
#
##############################################################################



sub get_annotation_window()
{

    my $instance;
    my $window_type = "annotation_window";
    my $wm = WindowManager->instance();

    # Create a new annotation window if an unused one wasn't found, otherwise
    # reuse an existing unused one.

    if (! defined($instance = $wm->find_unused($window_type)))
    {
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
			    "annotation_textview",
			    "annotation_scrolledwindow")
	{
	    $instance->{$widget} = $instance->{glade}->get_widget($widget);
	}

	# Setup the annotation window deletion handler.

	$instance->{window}->signal_connect
	    ("delete_event",
	     sub {
		 my($widget, $event, $instance) = @_;
		 return TRUE if ($instance->{in_cb});
		 local $instance->{in_cb} = 1;
		 hide_find_text($instance->{annotation_textview});
		 $widget->hide();
		 $instance->{annotation_buffer}->set_text("");
		 $instance->{mtn} = undef;
		 return TRUE;
	     },
	     $instance);

	# Setup the revision annotation viewer.

	$instance->{annotation_buffer} =
	    $instance->{annotation_textview}->get_buffer();
	create_format_tags($instance->{annotation_buffer});
	$instance->{annotation_textview}->modify_font($mono_font);

	# Register the window for management and set up the help callbacks.

	$wm->manage($instance, $window_type, $instance->{window});
	register_help_callbacks
	    ($instance,
	     {widget   => undef,
	      help_ref => __("mtnb-lachc-the-annotated-listing-window")});
    }
    else
    {
	my($height,
	   $width);
	$instance->{in_cb} = 0;
	local $instance->{in_cb} = 1;
	($width, $height) = $instance->{window}->get_default_size();
	$instance->{window}->resize($width, $height);
	$instance->{appbar}->set_progress_percentage(0);
	$instance->{appbar}->clear_stack();
    }

    # Empty out the contents.

    $instance->{annotation_buffer}->set_text("");

    return $instance;

}
#
##############################################################################
#
#   Routine      - mtn_annotate
#
#   Description  - Annotate the specified file on the specified revision.
#
#   Data         - $list        : A reference to the list that is to contain
#                                 the output from the annotate command.
#                  $mtn_db      : The Monotone database that is to be used or
#                                 undef if the database associated with the
#                                 current workspace is to be used.
#                  $revision_id : The revision id on which the desired version
#                                 of the file resides.
#                  $file_name   : The name of file that is to be annotated.
#                  Return Value : True if the comparison worked, otherwise
#                                 false if something went wrong.
#
##############################################################################



sub mtn_annotate($$$$)
{

    my($list, $mtn_db, $revision_id, $file_name) = @_;

    my($buffer,
       @cmd,
       $cwd,
       $exception,
       $ret_val);

    # Run mtn annotate in the root directory so as to avoid any workspace
    # conflicts.

    @$list = ();
    push(@cmd, "mtn");
    push(@cmd, "--db=" . $mtn_db) if (defined($mtn_db));
    push(@cmd, "annotate");
    push(@cmd, "-r");
    push(@cmd, "i:" . $revision_id);
    push(@cmd, $file_name);
    $cwd = getcwd();
    eval
    {
	die("chdir failed: " . $!) unless (chdir(File::Spec->rootdir()));
	$ret_val = run_command(\$buffer, undef, @cmd);
    };
    $exception = $@;
    chdir($cwd);
    if ($exception)
    {
	my $dialog = Gtk2::MessageDialog->new_with_markup
	    (undef,
	     ["modal"],
	     "warning",
	     "close",
	     __x("Problem running mtn annotate, got:\n"
		     . "<b><i>{error_message}</i></b>\n"
		     . "This should not be happening!",
		 error_message => Glib::Markup::escape_text($exception)));
	WindowManager->instance()->allow_input(sub { $dialog->run(); });
	$dialog->destroy();
	return;
    }

    # Break up the input into a list of lines.

    @$list = split(/\n/, $buffer);

    return $ret_val;

}

1;

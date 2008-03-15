##############################################################################
#
#   File Name    - Utilities.pm
#
#   Description  - The utilities module for the mtn-browse application. This
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

require 5.008;

use strict;

# ***** FUNCTIONAL PROTOTYPES FOR THIS FILE *****

# Public routines.

sub create_format_tags($);
sub generate_revision_report($$$$;$);
sub get_dir_contents($$$);
sub get_revision_ids($$);
sub glade_signal_autoconnect($$);
sub gtk2_update();
sub make_busy($$);
sub run_command($@);
sub set_label_value($$);
#
##############################################################################
#
#   Routine      - generate_revision_report
#
#   Description  - Populate the specified Gtk2::TextBuffer with a pretty
#                  printed report on the specified revision.
#
#   Data         - $text_buffer      : The Gtk2::TextBuffer that is to be
#                                      populated.
#                  $revision_id      : The id of the revision being reported
#                                      on.
#                  $certs_list       : A reference to a certs list as returned
#                                      by $mtn->certs().
#                  $colour           : One of "red, "green" or "" depending
#                                      upon the desired colour of the text.
#                  $revision_details : Either a reference to a revision
#                                      details list as returned by
#                                      $mtn->get_revision() if a detailed
#                                      report is to be generated or undef if
#                                      the report is to just be a summary.
#
##############################################################################



sub generate_revision_report($$$$;$)
{

    my($text_buffer, $revision_id, $certs_list, $colour, $revision_details)
	= @_;

    my($bold,
       $change_log,
       $italics,
       $manifest_id,
       $normal,
       @parent_revision_ids,
       %revision_data,
       %seen,
       @unique);
    my @types =
	("Added", "Removed", "Changed", "Renamed", "Attributes");

    # Sort out colour attributes.

    if ($colour ne "")
    {
	$normal = $colour;
	$bold = "bold-" . $colour;
	$italics = "italics-" . $colour;
    }
    else
    {
	$normal = "normal";
	$bold = "bold";
	$italics = "italics";
    }

    # Revision id.

    $text_buffer->insert_with_tags_by_name($text_buffer->get_end_iter(),
					   "Revision id: ",
					   $bold);
    $text_buffer->insert_with_tags_by_name($text_buffer->get_end_iter(),
					   $revision_id . "\n\n",
					   $normal);

    # Certs.

    foreach my $cert (@$certs_list)
    {
	if ($cert->{name} eq "changelog")
	{
	    $change_log = $cert->{value};
	    $change_log =~ s/\s+$//os;
	}
	else
	{
	    $cert->{value} =~ s/T/ /o if ($cert->{name} eq "date");
	    $text_buffer->insert_with_tags_by_name
		($text_buffer->get_end_iter(),
		 sprintf("%s:\t", ucfirst($cert->{name})),
		 $bold);
	    $text_buffer->insert_with_tags_by_name
		($text_buffer->get_end_iter(),
		 sprintf("%s\n", $cert->{value}),
		 $normal);
	}
    }

    # Change log.

    $text_buffer->insert_with_tags_by_name($text_buffer->get_end_iter(),
					   "\nChange Log:\n",
					   $bold);
    $text_buffer->insert_with_tags_by_name($text_buffer->get_end_iter(),
					   sprintf("%s", $change_log),
					   $normal);

    # The rest is only provided if it is a detailed report.

    if (defined($revision_details))
    {

	# Revision details.

	$text_buffer->insert_with_tags_by_name($text_buffer->get_end_iter(),
					       "\n\nChanges Made:\n",
					       $bold);
	foreach my $type (@types)
	{
	    $revision_data{$type} = [];
	}
	foreach my $change (@$revision_details)
	{
	    if ($change->{type} eq "add_dir")
	    {
		push(@{$revision_data{"Added"}}, $change->{name} . "/");
	    }
	    elsif ($change->{type} eq "add_file")
	    {
		push(@{$revision_data{"Added"}}, $change->{name});
	    }
	    elsif ($change->{type} eq "delete")
	    {
		push(@{$revision_data{"Removed"}}, $change->{name});
	    }
	    elsif ($change->{type} eq "patch")
	    {
		push(@{$revision_data{"Changed"}}, $change->{name});
	    }
	    elsif ($change->{type} eq "rename")
	    {
		push(@{$revision_data{"Renamed"}},
		     $change->{from_name} . " -> " . $change->{to_name});
	    }
	    elsif ($change->{type} eq "clear")
	    {
		push(@{$revision_data{"Attributes"}},
		     sprintf("%s: %s was cleared",
			     $change->{name},
			     $change->{attribute}));
	    }
	    elsif ($change->{type} eq "clear" || $change->{type} eq "set")
	    {
		push(@{$revision_data{"Attributes"}},
		     sprintf("%s: %s = %s",
			     $change->{name},
			     $change->{attribute},
			     $change->{value}));
	    }
	    elsif ($change->{type} eq "old_revision")
	    {
		push(@parent_revision_ids, $change->{revision_id});
	    }
	    elsif ($change->{type} eq "new_manifest")
	    {
		$manifest_id = $change->{manifest_id};
	    }
	}
	foreach my $type (@types)
	{
	    if (scalar(@{$revision_data{$type}}) > 0)
	    {
		$text_buffer->insert_with_tags_by_name
		    ($text_buffer->get_end_iter(),
		     "    " . $type . ":\n",
		     $italics);
		%seen = ();
		@unique = sort(grep { ! $seen{$_} ++ }
			       @{$revision_data{$type}});
		foreach my $line (@unique)
		{
		    $text_buffer->insert_with_tags_by_name
			($text_buffer->get_end_iter(),
			 "\t" . $line . "\n",
			 $normal);
		}
	    }
	}

	# Parent revision and manifest ids.

	$text_buffer->insert_with_tags_by_name($text_buffer->get_end_iter(),
					       "\nParent revision id(s):\t",
					       $bold);
	$text_buffer->insert_with_tags_by_name
	    ($text_buffer->get_end_iter(),
	     join(" ", @parent_revision_ids) . "\n",
	     $normal);
	$text_buffer->insert_with_tags_by_name($text_buffer->get_end_iter(),
					       "Manifest id:\t\t",
					       $bold);
	$text_buffer->insert_with_tags_by_name($text_buffer->get_end_iter(),
					       $manifest_id,
					       $normal);

    }

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
	     sprintf("The %s subprocess could not start,\n"
		         . "the system gave:\n<b><i>%s</b></i>",
		     Glib::Markup::escape_text($args[0]),
		     Glib::Markup::escape_text($@)));
	$dialog->run();
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
		 sprintf("waitpid failed with:\n<b><i>%s</i></b>",
			 Glib::Markup::escape_text($!)));
	    $dialog->run();
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
	     sprintf("The %s subprocess failed with an exit status\n"
		         . "of %d and printed the following on stderr:\n"
		         . "<b><i>%s</i></b>",
		     Glib::Markup::escape_text($args[0]),
		     WEXITSTATUS($status),
		     Glib::Markup::escape_text(join("", @err))));
	$dialog->run();
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
	     sprintf("The %s subprocess was terminated by signal %d",
		     Glib::Markup::escape_text($args[0]),
		     WTERMSIG($status)));
	$dialog->run();
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
       $i,
       $match_re,
       $name);

    $i = 0;
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
	    $$result[$i ++] = {manifest_entry => $entry,
			       name           => $name};
	}
    }

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
#                  $revision_ids : The list of selected revision ids. Normally
#                                  the list will have at most one element but
#                                  may contain more if the tag isn't unique on
#                                  the current branch.
#
##############################################################################



sub get_revision_ids($$)
{

    my($instance, $revision_ids) = @_;

    @$revision_ids=();
    return unless ($instance->{revision_combo_details}->{completed});
    if ($instance->{tagged_tick}->get_active())
    {
	$instance->{mtn}->
	    select($revision_ids,
		   "t:" . $instance->{revision_combo_details}->{value});
    }
    else
    {
	push(@$revision_ids, $instance->{revision_combo_details}->{value});
    }

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

    $glade->signal_autoconnect
	(sub {
	     my($callback_name, $widget, $signal_name, $signal_data,
		$connect_object, $after, $user_data) = @_;
	     my $func = $after ? "signal_connect_after" : "signal_connect";
	     $widget->$func($signal_name,
			    $callback_name,
			    $connect_object ? $connect_object : $user_data); },
	 $client_data);

}
#
##############################################################################
#
#   Routine      - make_busy
#
#   Description  - This routine simply makes the main window busy or active.
#
#   Data         - $instance : The window instance.
#                  $busy     : True if the window is to be made busy,
#                              otherwise false if the window is to be made
#                              active.
#
##############################################################################



sub make_busy($$)
{

    my($instance, $busy) = @_;

    # Create and store the cursors if we haven't done so already.

    $busy_cursor = Gtk2::Gdk::Cursor->new("watch")
	unless (defined($busy_cursor));

    # Do it. Make the application bar grab the input when the window is busy,
    # that way we gobble up keyboard and mouse events that could muck up the
    # application state.

    if ($busy)
    {
	if (exists($instance->{grab_widget}))
	{
	    Gtk2->grab_add($instance->{grab_widget});
	}
	else
	{
	    Gtk2->grab_add($instance->{appbar});
	}
	foreach my $instance (@windows)
	{
	    foreach my $window (@{$instance->{busy_windows}})
	    {
		$window->set_cursor($busy_cursor);
	    }
	}
    }
    else
    {
	if (exists($instance->{grab_widget}))
	{
	    Gtk2->grab_remove($instance->{grab_widget});
	}
	else
	{
	    Gtk2->grab_remove($instance->{appbar});
	}
	foreach my $instance (@windows)
	{
	    foreach my $window (@{$instance->{busy_windows}})
	    {
		$window->set_cursor(undef);
	    }
	}
    }

}
#
##############################################################################
#
#   Routine      - gtk2_update
#
#   Description  - Process all outstanding Gtk2 toolkit events. This is used
#                  to update the GUI whilst the application is busy doing
#                  something.
#
#   Data         - None.
#
##############################################################################



sub gtk2_update()
{

    return if (Gtk2->main_level() == 0);
    while (Gtk2->events_pending())
    {
	Gtk2->main_iteration();
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

    my($text_buffer) = @_;

    # Normal Black text, assorted styles, on a white background.

    $text_buffer->create_tag("normal", "weight" => PANGO_WEIGHT_NORMAL);

    $text_buffer->create_tag("bold", "weight" => PANGO_WEIGHT_BOLD);
    $text_buffer->create_tag("italics", "style" => "italic");
    $text_buffer->create_tag("bold-italics",
			     "weight" => PANGO_WEIGHT_BOLD,
			     "style" => "italic");

    # Green text, assorted styles, on a white background.

    $text_buffer->create_tag("green", "foreground" => "DarkGreen");
    $text_buffer->create_tag("bold-green",
			     "weight" => PANGO_WEIGHT_BOLD,
			     "foreground" => "DarkGreen");
    $text_buffer->create_tag("italics-green",
			     "style" => "italic",
			     "foreground" => "DarkGreen");
    $text_buffer->create_tag("bold-italics-green",
			     "weight" => PANGO_WEIGHT_BOLD,
			     "style" => "italic",
			     "foreground" => "DarkGreen");

    # Red text, assorted styles, on a white background.

    $text_buffer->create_tag("red", "foreground" => "DarkRed");
    $text_buffer->create_tag("bold-red",
			     "weight" => PANGO_WEIGHT_BOLD,
			     "foreground" => "DarkRed");
    $text_buffer->create_tag("italics-red",
			     "style" => "italic",
			     "foreground" => "DarkRed");
    $text_buffer->create_tag("bold-italics-red",
			     "weight" => PANGO_WEIGHT_BOLD,
			     "style" => "italic",
			     "foreground" => "DarkRed");

    # Yellow text on a grey background.

    $text_buffer->create_tag("compare-info",
			     "foreground" => "Yellow",
			     "background" => "LightSlateGrey");

    # Red text, assorted styles, on pink and grey backgrounds.

    $text_buffer->create_tag("compare-first-file",
			     "foreground" => "DarkRed",
			     "background" => "MistyRose1");
    $text_buffer->create_tag("compare-first-file-info",
			     "weight" => PANGO_WEIGHT_BOLD,
			     "foreground" => "IndianRed1",
			     "background" => "DarkSlateGrey");

    # Green text, assorted styles, on light green and grey backgrounds.

    $text_buffer->create_tag("compare-second-file",
			     "foreground" => "DarkGreen",
			     "background" => "DarkSeaGreen1");
    $text_buffer->create_tag("compare-second-file-info",
			     "weight" => PANGO_WEIGHT_BOLD,
			     "foreground" => "SpringGreen1",
			     "background" => "DarkSlateGrey");

    # Blue text, assorted shades, on assorted blue backgrounds.

    $text_buffer->create_tag("annotate-prefix-1",
			     "foreground" => "AliceBlue",
			     "background" => "CadetBlue");
    $text_buffer->create_tag("annotate-text-1",
			     "foreground" => "MidnightBlue",
			     "background" => "PaleTurquoise");

    # Blue text, assorted shades, on assorted blue backgrounds (slightly darker
    # than the previous group).

    $text_buffer->create_tag("annotate-prefix-2",
			     "foreground" => "AliceBlue",
			     "background" => "SteelBlue");
    $text_buffer->create_tag("annotate-text-2",
			     "foreground" => "MidnightBlue",
			     "background" => "SkyBlue");

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
    $tooltips->set_tip($widget->parent(), $value);

}

1;

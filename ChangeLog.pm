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
use warnings;

# ***** GLOBAL DATA DECLARATIONS *****

# The translated revision change type strings.

my $__added      = __("Added");
my $__removed    = __("Removed");
my $__changed    = __("Changed");
my $__renamed    = __("Renamed");
my $__attributes = __("Attributes");

# A translated list of all possible revision change types that we need to check
# for.

my @__types = ($__added,
	       $__removed,
	       $__changed,
	       $__renamed,
	       $__attributes);

# ***** FUNCTIONAL PROTOTYPES *****

# Public routines.

sub display_change_log($$;$$);
sub generate_revision_report($$$$;$);

# Private routines.

sub get_change_log_window();
#
##############################################################################
#
#   Routine      - display_change_log
#
#   Description  - Display a revision's change log in a window.
#
#   Data         - $mtn         : The Monotone::AutomateStdio object that is
#                                 to be used to display the change log.
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
#                  $colour           : One of "red", "green" or "" depending
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
       $cert_max_len,
       @change_logs,
       $i,
       $italics,
       $manifest_id,
       $normal,
       @parent_revision_ids,
       %revision_data,
       %seen,
       @unique);

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
					   __("Revision id: "),
					   $bold);
    $text_buffer->insert_with_tags_by_name($text_buffer->get_end_iter(),
					   $revision_id . "\n",
					   $normal);

    # Certs.

    $cert_max_len = 0;
    foreach my $cert (@$certs_list)
    {
	$cert_max_len = length($cert->{name})
	    if ($cert->{name} ne "changelog"
		&& length($cert->{name}) > $cert_max_len);
    }
    foreach my $cert (@$certs_list)
    {
	if ($cert->{name} eq "changelog")
	{
	    my $change_log = $cert->{value};
	    $change_log =~ s/\s+$//s;
	    push(@change_logs, $change_log) if ($change_log ne "");
	}
	else
	{
	    if ($cert->{name} eq "date")
	    {
		$cert->{value} =~ s/T/ /;
	    }
	    elsif (index($cert->{value}, "\n") >= 0)
	    {
		my $padding = sprintf("\n%-*s", $cert_max_len + 2, "");
		$cert->{value} =~ s/\n/$padding/g;
	    }
	    $text_buffer->insert_with_tags_by_name
		($text_buffer->get_end_iter(),
		 sprintf("\n%-*s ",
			 $cert_max_len + 1, ucfirst($cert->{name}) . ":"),
		 $bold);
	    $text_buffer->insert_with_tags_by_name
		($text_buffer->get_end_iter(),
		 sprintf("%s", $cert->{value}),
		 $normal);
	}
    }

    # Change log(s).

    $i = 1;
    foreach my $change_log (@change_logs)
    {
	$text_buffer->insert_with_tags_by_name($text_buffer->get_end_iter(),
					       "\n",
					       $normal);
	$text_buffer->insert_with_tags_by_name
	    ($text_buffer->get_end_iter(),
	     __x("\nChange Log{optional_count}:\n",
		 optional_count => (scalar(@change_logs) > 1)
		     ? sprintf(" %d", $i) : ""),
	     $bold);
	$text_buffer->insert_with_tags_by_name($text_buffer->get_end_iter(),
					       $change_log,
					       $normal);
	++ $i;
    }

    # The rest is only provided if it is a detailed report.

    if (defined($revision_details))
    {

	# Revision details.

	$text_buffer->insert_with_tags_by_name($text_buffer->get_end_iter(),
					       __("\n\nChanges Made:\n"),
					       $bold);
	foreach my $type (@__types)
	{
	    $revision_data{$type} = [];
	}
	foreach my $change (@$revision_details)
	{
	    if ($change->{type} eq "add_dir")
	    {
		push(@{$revision_data{$__added}}, $change->{name} . "/");
	    }
	    elsif ($change->{type} eq "add_file")
	    {
		push(@{$revision_data{$__added}}, $change->{name});
	    }
	    elsif ($change->{type} eq "delete")
	    {
		push(@{$revision_data{$__removed}}, $change->{name});
	    }
	    elsif ($change->{type} eq "patch")
	    {
		push(@{$revision_data{$__changed}}, $change->{name});
	    }
	    elsif ($change->{type} eq "rename")
	    {
		push(@{$revision_data{$__renamed}},
		     $change->{from_name} . " -> " . $change->{to_name});
	    }
	    elsif ($change->{type} eq "clear")
	    {
		push(@{$revision_data{$__attributes}},
		     __x("{name}: {attribute} was cleared",
			 name      => $change->{name},
			 attribute => $change->{attribute}));
	    }
	    elsif ($change->{type} eq "set")
	    {
		push(@{$revision_data{$__attributes}},
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
	foreach my $type (@__types)
	{
	    if (scalar(@{$revision_data{$type}}) > 0)
	    {
		$text_buffer->insert_with_tags_by_name
		    ($text_buffer->get_end_iter(),
		     "    " . $type . ":\n",
		     $italics);
		%seen = ();
		@unique = sort(grep(! $seen{$_} ++, @{$revision_data{$type}}));
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

	$text_buffer->insert_with_tags_by_name
	    ($text_buffer->get_end_iter(),
	     __("\nParent revision id(s): "),
	     $bold);
	$text_buffer->insert_with_tags_by_name
	    ($text_buffer->get_end_iter(),
	     join(" ", @parent_revision_ids) . "\n",
	     $normal);
	$text_buffer->insert_with_tags_by_name($text_buffer->get_end_iter(),
					       __("Manifest id:           "),
					       $bold);
	$text_buffer->insert_with_tags_by_name($text_buffer->get_end_iter(),
					       $manifest_id,
					       $normal);

    }

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
		 hide_find_text($instance->{changelog_textview});
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
	$wm->add_busy_windows($instance,
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

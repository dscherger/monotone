#!/usr/bin/perl -w
##############################################################################
#
#   File Name    - Monontone.pm
#
#   Description  - Perl class module that provides an interface to Monotone's
#                  automate stdio interface.
#
#   Author       - A.E.Cooper.
#
#   Legal Stuff  - Copyright (c) 2007 Anthony Edward Cooper
#                  <aecooper@coosoft.plus.com>.
#
#                  This library is free software; you can redistribute it
#                  and/or modify it under the terms of the GNU Lesser General
#                  Public License as published by the Free Software
#                  Foundation; either version 3 of the License, or (at your
#                  option) any later version.
#
#                  This library is distributed in the hope that it will be
#                  useful, but WITHOUT ANY WARRANTY; without even the implied
#                  warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
#                  PURPOSE.  See the GNU Library General Public License for
#                  more details.
#
#                  You should have received a copy of the GNU Library General
#                  Public License along with this library; if not, write to
#                  the Free Software Foundation, Inc., 59 Temple Place - Suite
#                  330, Boston, MA  02111-1307  USA.
#
##############################################################################
#
##############################################################################
#
#   GLOBAL DATA FOR THIS MODULE
#
##############################################################################



# ***** PACKAGE DECLARATION *****

package Monotone::AutomateStdio;

# ***** REQUIRED VERSION OF PERL *****

require 5.008;

# ***** REQUIRED PACKAGES *****

use strict;
use integer;
use Carp;
use IPC::Open3;
use Symbol qw(gensym);

# ***** GLOBAL DATA DECLARATIONS *****

# A pre-compiled rather evil regular expression for finding the end of a quoted
# string possibly containing escaped quotes, i.e. " preceeded by a
# non-backslash character or an even number of backslash characters. This re is
# not ideal as it would be fooled by something like 22 backslashs followed by
# an unescaped double quote, but at this point I have given up caring. What I
# want to do is something like \{*%2}.

my $closing_quote_re = qr/(((^.*[^\\])|^)\"$)
			  |(((^.*[^\\])|^)\\{2}\"$)
			  |(((^.*[^\\])|^)\\{4}\"$)
			  |(((^.*[^\\])|^)\\{6}\"$)
			  |(((^.*[^\\])|^)\\{8}\"$)
			  |(((^.*[^\\])|^)\\{10}\"$)
			  |(((^.*[^\\])|^)\\{12}\"$)
			  |(((^.*[^\\])|^)\\{14}\"$)
			  |(((^.*[^\\])|^)\\{16}\"$)
			  |(((^.*[^\\])|^)\\{18}\"$)
			  |(((^.*[^\\])|^)\\{20}\"$)/ox;

# ***** CLASS DEFINITIONS *****

# Class inheritance and declaration.

use base qw(Exporter);

our(@EXPORT, @EXPORT_OK, $VERSION);
BEGIN
{
    @EXPORT = qw();
    @EXPORT_OK = qw();
    $VERSION = 0.1;
}

# Class attributes:
#     db_name       - The name of the Monotone database if specified.
#     mtn_pid       - The process id of the subordinate mtn process.
#     mtn_in        - The input into the mtn subprocess.
#     mtn_out       - The output from the mtn subprocess.
#     mtn_err       - The error output from the mtn subprocess.
#     mtn_err_msg   - The last error message returned from the mtn subprocess.
#     mtn_aif_major - The major version number for the mtn automate interface.
#     mtn_aif_minor - The minor version number for the mtn automate interface.
#     cmd_cnt       - The number of the current command.

use fields qw(db_name
	      mtn_pid
	      mtn_in
	      mtn_out
	      mtn_err
	      mtn_err_msg
	      mtn_aif_major
	      mtn_aif_minor
	      cmd_cnt);

# ***** FUNCTIONAL PROTOTYPES FOR THIS FILE *****

# Public methods.

sub ancestors($\@@);
sub ancestry_difference($\@$;@);
sub branches($\@);
sub cert($$$$);
sub certs($$$);
sub children($\@$);
sub closedown($);
sub common_ancestors($\@@);
sub content_diff($\$$$;@);
sub db_get($\$$$);
sub db_set($$$$);
sub descendents($\@@);
sub erase_ancestors($\@@);
sub error_message($);
sub get_attributes($\$$);
sub get_base_revision_id($\$);
sub get_content_changed($\@$$);
sub get_corresponding_path($\$$$$);
sub get_current_revision_id($\$);
sub get_file($\$$);
sub get_file_of($\$$;$);
sub get_manifest_of($$;$);
sub get_option($\$$);
sub get_revision($\$$);
sub graph($$);
sub heads($\@;$);
sub identify($\$$);
sub interface_version($\$);
sub inventory($$);
sub keys($$);
sub leaves($\@);
sub parents($\@$);
sub roots($\@);
sub select($\@$);
sub tags($$;$);
sub toposort($\@@);
sub new($;$);

# Private routines.

sub get_quoted_value(\@\$\$);
sub mtn_command($$$@);
sub mtn_command_with_options($$$\@@);
sub mtn_read_output($\$);
sub startup($);
sub unescape($);
#
##############################################################################
#
#   Routine      - new
#
#   Description  - Class constructor.
#
#   Data         - $invocant    : Either a reference to an object of the same
#                                 class or the name of the class to be
#                                 created.
#                  $db_name     : The full path of the Monotone database. If
#                                 not provided then the database associated
#                                 with the current workspace is used.
#                  Return Value : A reference to the newly created object.
#
##############################################################################



sub new($;$)
{

    my ($invocant, $db_name) = @_;

    my Monotone::AutomateStdio $this;

    $this = fields::new($invocant);
    $this->{db_name} = $db_name;
    $this->{mtn_pid} = 0;
    $this->{cmd_cnt} = 0;
    $this->{mtn_err_msg} = "";

    if ($this->{mtn_pid} == 0)
    {
	startup($this);
    }

    $this->SUPER::new() if $this->can("SUPER::new");

    return $this;

}
#
##############################################################################
#
#   Routine      - DESTROY
#
#   Description  - Class destructor.
#
#   Data         - None.
#
##############################################################################



sub DESTROY
{

    my Monotone::AutomateStdio $this = shift();

    closedown($this);
    $this->SUPER::DESTROY() if $this->can("SUPER::DESTROY");

}
#
##############################################################################
#
#   Routine      - ancestors
#
#   Description  - Get a list of ancestors for the specified revisions.
#
#   Data         - $this         : The object.
#                  \@list        : A reference to a list that is to contain
#                                  the revision ids.
#                  @revision_ids : The revision ids that are to have their
#                                  ancestors returned.
#                  Return Value  : True on success, otherwise false on
#                                  failure.
#
##############################################################################



sub ancestors($\@@)
{

    my Monotone::AutomateStdio $this = shift();
    my($list, @revision_ids) = @_;

    return mtn_command($this, "ancestors", $list, @revision_ids);

}
#
##############################################################################
#
#   Routine      - ancestry_difference
#
#   Description  - Get a list of ancestors for the specified revision, that
#                  are not also ancestors for the specified old revisions.
#
#   Data         - $this             : The object.
#                  \@list            : A reference to a list that is to
#                                      contain the revision ids.
#                  $new_revision_id  : The revision id that is to have its
#                                      ancestors returned.
#                  @old_revision_ids : The revision ids that are to have their
#                                      ancestors excluded from the above list.
#                  Return Value      : True on success, otherwise false on
#                                      failure.
#
##############################################################################



sub ancestry_difference($\@$;@)
{

    my Monotone::AutomateStdio $this = shift();
    my($list, $new_revision_id, @old_revision_ids) = @_;

    return mtn_command($this,
		       "ancestry_difference",
		       $list,
		       $new_revision_id,
		       @old_revision_ids);

}
#
##############################################################################
#
#   Routine      - branches
#
#   Description  - Get a list of branches.
#
#   Data         - $this        : The object.
#                  \@list       : A reference to a list that is to contain the
#                                 branch names.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub branches($\@)
{

    my Monotone::AutomateStdio $this = $_[0];
    my $list = $_[1];

    return mtn_command($this, "branches", $list);

}
#
##############################################################################
#
#   Routine      - cert
#
#   Description  - Add the specified cert to the specified revision.
#
#   Data         - $this        : The object.
#                  $revision_id : The revision id to which the cert is to be
#                                 applied.
#                  $name        : The name of the cert to be applied.
#                  $value       : The value of the cert.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub cert($$$$)
{

    my Monotone::AutomateStdio $this = shift();
    my($revision_id, $name, $value) = @_;

    my @dummy;

    return mtn_command($this, "cert", @dummy, $revision_id, $name, $value);

}
#
##############################################################################
#
#   Routine      - certs
#
#   Description  - Get all the certs for the specified revision.
#
#   Data         - $this        : The object.
#                  $ref         : A reference to a buffer or an array that is
#                                 to contain the output from this command.
#                  $revision_id : The id of the revision that is to have its
#                                 certs returned.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub certs($$$)
{

    my Monotone::AutomateStdio $this = shift();
    my($ref, $revision_id) = @_;

    # Run the command and get the data, either as one lump or as a structured
    # list.

    if (ref($ref) eq "SCALAR")
    {
	return mtn_command($this, "certs", $ref, $revision_id);
    }
    else
    {

	my($i,
	   $j,
	   $key,
	   @lines,
	   $name,
	   $signature,
	   $trust,
	   $value);

	if (! mtn_command($this, "certs", \@lines, $revision_id))
	{
	    return;
	}

	# Reformat the data into a structured array.

	for ($i = $j = 0, @$ref = (); $i <= $#lines; ++ $i)
	{
	    if ($lines[$i] =~ m/^ *key \"/o)
	    {
		get_quoted_value(@lines, $i, $key);
		if ($lines[++ $i] =~ m/^ *signature \"/o)
		{
		    ($signature) =
			($lines[$i] =~ m/^ *signature \"([^\"]+)\"$/o);
		}
		else
		{
		    croak("Corrupt certs list, expected signature field but "
			  . "didn't find it");
		}
		if ($lines[++ $i] =~ m/^ *name \"/o)
		{
		    get_quoted_value(@lines, $i, $name);
		}
		else
		{
		    croak("Corrupt certs list, expected name field but didn't "
			  . "find it");
		}
		if ($lines[++ $i] =~ m/^ *value \"/o)
		{
		    get_quoted_value(@lines, $i, $value);
		}
		else
		{
		    croak("Corrupt certs list, expected value field but "
			  . "didn't find it");
		}
		if ($lines[++ $i] =~ m/^ *trust \"/o)
		{
		    ($trust) = ($lines[$i] =~ m/^ *trust \"([^\"]+)\"$/o);
		}
		else
		{
		    croak("Corrupt certs list, expected trust field but "
			  . "didn't find it");
		}
		$$ref[$j ++] = {key       => unescape($key),
				signature => $signature,
				name      => unescape($name),
				value     => unescape($value),
				trust     => $trust};
	    }
	}

	return 1;

    }

}
#
##############################################################################
#
#   Routine      - children
#
#   Description  - Get a list of children for the specified revision.
#
#   Data         - $this        : The object.
#                  \@list       : A reference to a list that is to contain the
#                                 revision ids.
#                  $revision_id : The revision id that is to have its children
#                                 returned.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub children($\@$)
{

    my Monotone::AutomateStdio $this = shift();
    my($list, @revision_ids) = @_;

    return mtn_command($this, "children", $list, @revision_ids);

}
#
##############################################################################
#
#   Routine      - common_ancestors
#
#   Description  - Get a list of revisions that are all ancestors of the
#                  specified revision.
#
#   Data         - $this         : The object.
#                  \@list        : A reference to a list that is to contain
#                                  the revision ids.
#                  @revision_ids : The revision ids that are to have their
#                                  common ancestors returned.
#                  Return Value  : True on success, otherwise false on
#                                  failure.
#
##############################################################################



sub common_ancestors($\@@)
{

    my Monotone::AutomateStdio $this = shift();
    my($list, @revision_ids) = @_;

    return mtn_command($this, "common_ancestors", $list, @revision_ids);

}
#
##############################################################################
#
#   Routine      - content_diff
#
#   Description  - Get the difference between the two specified revisions,
#                  optionally limiting it to the specified list of files. If
#                  the second revision id is undefined then the workspace's
#                  revision is used. If both revision ids are undefined then
#                  the workspace's and base revisions are used. If no file
#                  names are listed then differences in all files are
#                  reported.
#
#   Data         - $this         : The object.
#                  \$buffer      : A reference to a buffer that is to contain
#                                  the output from this command.
#                  $revision_id1 : The first revision id to compare against.
#                  $revision_id2 : The second revision id to compare against.
#                  @file_names   : The list of file names that are to be
#                                  reported on.
#                  Return Value  : True on success, otherwise false on
#                                  failure.
#
##############################################################################



sub content_diff($\$$$;@)
{

    my Monotone::AutomateStdio $this = shift();
    my($buffer, $revision_id1, $revision_id2, @file_names) = @_;

    my @options;

    push(@options, {key => "r", value => $revision_id1})
	unless (! defined($revision_id1));
    push(@options, {key => "r", value => $revision_id2})
	unless (! defined($revision_id2));

    return mtn_command_with_options($this,
				    "content_diff",
				    $buffer,
				    @options,
				    @file_names);

}
#
##############################################################################
#
#   Routine      - db_get
#
#   Description  - Get the value of a database variable.
#
#   Data         - $this        : The object.
#                  \$buffer     : A reference to a buffer that is to contain
#                                 the output from this command.
#                  $domain      : The domain of the database variable.
#                  $name        : The name of the variable to fetch.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub db_get($\$$$)
{

    my Monotone::AutomateStdio $this = shift();
    my($buffer, $domain, $name) = @_;

    return mtn_command($this, "db_get", $buffer, $domain, $name);

}
#
##############################################################################
#
#   Routine      - db_set
#
#   Description  - Set the value of a database variable.
#
#   Data         - $this        : The object.
#                  $domain      : The domain of the database variable.
#                  $name        : The name of the variable to set.
#                  $value       : The value to set the variable to.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub db_set($$$$)
{

    my Monotone::AutomateStdio $this = shift();
    my($domain, $name, $value) = @_;

    my $dummy;

    return mtn_command($this, "db_set", \$dummy, $domain, $name, $value);

}
#
##############################################################################
#
#   Routine      - descendents
#
#   Description  - Get a list of descendents for the specified revisions.
#
#   Data         - $this         : The object.
#                  \@list        : A reference to a list that is to contain
#                                  the revision ids.
#                  @revision_ids : The revision ids that are to have their
#                                  descendents returned.
#                  Return Value  : True on success, otherwise false on
#                                  failure.
#
##############################################################################



sub descendents($\@@)
{

    my Monotone::AutomateStdio $this = shift();
    my($list, @revision_ids) = @_;

    return mtn_command($this, "descendents", $list, @revision_ids);

}
#
##############################################################################
#
#   Routine      - erase_ancestors
#
#   Description  - For a given list of revisions, weed out those that are
#                  ancestors to other revisions specified within the list.
#
#   Data         - $this         : The object.
#                  \@list        : A reference to a list that is to contain
#                                  the revision ids.
#                  @revision_ids : The revision ids that are to have their
#                                  descendents returned.
#                  Return Value  : True on success, otherwise false on
#                                  failure.
#
##############################################################################



sub erase_ancestors($\@@)
{

    my Monotone::AutomateStdio $this = shift();
    my($list, @revision_ids) = @_;

    return mtn_command($this, "erase_ancestors", $list, @revision_ids);

}
#
##############################################################################
#
#   Routine      - get_attributes
#
#   Description  - Get the attributes of the specified file.
#
#   Data         - $this        : The object.
#                  $ref         : A reference to a buffer or an array that is
#                                 to contain the output from this command.
#                  $file_name   : The name of the file that is to be reported
#                                 on.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub get_attributes($\$$)
{

    my Monotone::AutomateStdio $this = shift();
    my($ref, $file_name) = @_;

    my $cmd;

    # This command was renamed in version 0.36 (i/f version 5.x).

    if ($this->{mtn_aif_major} >= 5)
    {
	$cmd = "get_attributes";
    }
    else
    {
	$cmd = "attributes";
    }

    # Run the command and get the data, either as one lump or as a structured
    # list.

    if (ref($ref) eq "SCALAR")
    {
	return mtn_command($this, $cmd, $ref, $file_name);
    }
    else
    {

	my($i,
	   $j,
	   $key,
	   @lines,
	   $list,
	   $state,
	   $value);

	if (! mtn_command($this, $cmd, \@lines, $file_name))
	{
	    return;
	}

	# Reformat the data into a structured array.

	for ($i = $j = 0, @$ref = (); $i <= $#lines; ++ $i)
	{
	    if ($lines[$i] =~ m/^ *attr \"/o)
	    {
		($list) = ($lines[$i] =~ m/^ *\S+ \"(.+)\"$/o);
		($key, $value) = split(/\" \"/o, $list);
		if ($lines[++ $i] =~ m/^ *state \"/o)
		{
		    ($state) = ($lines[$i] =~ m/^ *state \"([^\"]+)\"$/o);
		}
		else
		{
		    croak("Corrupt attributes list, expected state field but "
			  . "didn't find it");
		}
		$$ref[$j ++] = {attribute => unescape($key),
				value     => unescape($value),
				state     => $state};
	    }
	}

	return 1;

    }

}
#
##############################################################################
#
#   Routine      - get_base_revision_id
#
#   Description  - Get the revision upon which the workspace is based.
#
#   Data         - $this        : The object.
#                  \$buffer     : A reference to a buffer that is to contain
#                                 the output from this command.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub get_base_revision_id($\$)
{

    my Monotone::AutomateStdio $this = $_[0];
    my $buffer = $_[1];

    my @list;

    $$buffer = "";
    if (! mtn_command($this, "get_base_revision_id", \@list))
    {
	return;
    }
    $$buffer = $list[0];

    return 1;

}
#
##############################################################################
#
#   Routine      - get_content_changed
#
#   Description  - Get a list of revisions in which the content was most
#                  recently changed, relative to the specified revision.
#
#   Data         - $this        : The object.
#                  \@list       : A reference to a list that is to contain the
#                                 revision ids.
#                  $revision_id : The id of the revision of the manifest that
#                                 is to be returned.
#                  $file_name   : The name of the file that is to be reported
#                                 on.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub get_content_changed($\@$$)
{

    my Monotone::AutomateStdio $this = shift();
    my($list, $revision_id, $file_name) = @_;

    my($i,
       $j,
       @lines);

    # Run the command and get the data.

    if (! mtn_command($this, "get_content_changed",
		      \@lines,
		      $revision_id,
		      $file_name))
    {
	return;
    }

    # Reformat the data into a list.

    for ($i = $j = 0, @$list = (); $i <= $#lines; ++ $i)
    {
	if ($lines[$i] =~ m/^ *content_mark \[[^\]]+\]$/o)
	{
	    ($$list[$j ++]) =
		($lines[$i] =~ m/^ *content_mark \[([^\]]+)\]$/o);
	}
    }

    return 1;

}
#
##############################################################################
#
#   Routine      - get_corresponding_path
#
#   Description  - For the specified file name in the specified source
#                  revision, return the corresponding file name for the
#                  specified target revision.
#
#   Data         - $this               : The object.
#                  \$buffer            : A reference to a buffer that is to
#                                        contain the output from this command.
#                  $source_revision_id : The source revision id.
#                  $file_name          : The name of the file that is to be
#                                        searched for.
#                  $target_revision_id : The target revision id.
#                  Return Value        : True on success, otherwise false on
#                                        failure.
#
##############################################################################



sub get_corresponding_path($\$$$$)
{

    my Monotone::AutomateStdio $this = shift();
    my($buffer, $source_revision_id, $file_name, $target_revision_id) = @_;

    my($i,
       @lines);

    # Run the command and get the data.

    if (! mtn_command($this, "get_corresponding_path",
		      \@lines,
		      $source_revision_id,
		      $file_name,
		      $target_revision_id))
    {
	return;
    }

    # Extract the file name.

    for ($i = 0, $$buffer = ""; $i <= $#lines; ++ $i)
    {
	if ($lines[$i] =~ m/^ *file \"/o)
	{
	    get_quoted_value(@lines, $i, $$buffer);
	    $$buffer = unescape($$buffer);
	}
    }

    return 1;

}
#
##############################################################################
#
#   Routine      - get_current_revision_id
#
#   Description  - Get the revision that would be created if an unrestricted
#                  commit was done in the workspace.
#
#   Data         - $this        : The object.
#                  \$buffer     : A reference to a buffer that is to contain
#                                 the output from this command.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub get_current_revision_id($\$)
{

    my Monotone::AutomateStdio $this = $_[0];
    my $buffer = $_[1];

    my @list;

    $$buffer = "";
    if (! mtn_command($this, "get_current_revision_id", \@list))
    {
	return;
    }
    $$buffer = $list[0];

    return 1;

}
#
##############################################################################
#
#   Routine      - get_file
#
#   Description  - Get the contents of the file referenced by the specified
#                  file id.
#
#   Data         - $this        : The object.
#                  \$buffer     : A reference to a buffer that is to contain
#                                 the output from this command.
#                  $file_id     : The file id of the file that is to be
#                                 returned.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub get_file($\$$)
{

    my Monotone::AutomateStdio $this = shift();
    my($buffer, $file_id) = @_;

    return mtn_command($this, "get_file", $buffer, $file_id);

}
#
##############################################################################
#
#   Routine      - get_file_of
#
#   Description  - Get the contents of the specified file under the specified
#                  revision. If the revision id is undefined then the current
#                  workspace revision is used.
#
#   Data         - $this        : The object.
#                  \$buffer     : A reference to a buffer that is to contain
#                                 the output from this command.
#                  $file_name   : The name of the file to be fetched.
#                  $revision_id : The revision id upon which the file contents
#                                 are to be based.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub get_file_of($\$$;$)
{

    my Monotone::AutomateStdio $this = shift();
    my($buffer, $file_name, $revision_id) = @_;

    my @options;

    push(@options, {key => "r", value => $revision_id})
	unless (! defined($revision_id));

    return mtn_command_with_options($this,
				    "get_file_of",
				    $buffer,
				    @options,
				    $file_name);

}
#
##############################################################################
#
#   Routine      - get_manifest_of
#
#   Description  - Get the manifest for the current or specified revision.
#
#   Data         - $this        : The object.
#                  $ref         : A reference to a buffer or an array that is
#                                 to contain the output from this command.
#                  $revision_id : The revision id which is to have its
#                                 manifest returned.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub get_manifest_of($$;$)
{

    my Monotone::AutomateStdio $this = shift();
    my($ref, $revision_id) = @_;

    # Run the command and get the data, either as one lump or as a structured
    # list.

    if (ref($ref) eq "SCALAR")
    {
	return mtn_command($this, "get_manifest_of", $ref, $revision_id);
    }
    else
    {

	my($i,
	   $id,
	   $j,
	   @lines,
	   $name,
	   $type);

	if (! mtn_command($this, "get_manifest_of", \@lines, $revision_id))
	{
	    return;
	}

	# Reformat the data into a structured array.

	for ($i = $j = 0, @$ref = (); $i <= $#lines; ++ $i)
	{
	    $type = undef;
	    if ($lines[$i] =~ m/^ *file \"/o)
	    {
		$type = "file";
		get_quoted_value(@lines, $i, $name);
		if ($lines[++ $i] =~ m/^ *content \[[^\]]+\]$/o)
		{
		    ($id) = ($lines[$i] =~ m/^ *content \[([^\]]+)\]$/o);
		}
		else
		{
		    croak("Corrupt manifest, expected content field but "
			  . "didn't find it");
		}
	    }
	    if ($lines[$i] =~ m/^ *dir \"/o)
	    {
		$type = "directory";
		get_quoted_value(@lines, $i, $name);
	    }
	    if (defined($type))
	    {
		if ($type eq "file")
		{
		    $$ref[$j ++] = {type    => $type,
				    name    => unescape($name),
				    file_id => $id};
		}
		else
		{
		    $$ref[$j ++] = {type => $type,
				    name => unescape($name)};
		}
	    }
	}

	return 1;

    }

}
#
##############################################################################
#
#   Routine      - get_option
#
#   Description  - Get the value of an option stored in a workspace's _MTN
#                  directory.
#
#   Data         - $this        : The object.
#                  \$buffer     : A reference to a buffer that is to contain
#                                 the output from this command.
#                  $option_name : The name of the option to be fetched.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub get_option($\$$)
{

    my Monotone::AutomateStdio $this = shift();
    my($buffer, $option_name) = @_;

    if (! mtn_command($this, "get_option", $buffer, $option_name))
    {
	return;
    }
    chomp($$buffer);

    return 1;

}
#
##############################################################################
#
#   Routine      - get_revision
#
#   Description  - Get the revision information for the current or specified
#                  revision.
#
#   Data         - $this        : The object.
#                  $ref         : A reference to a buffer or an array that is
#                                 to contain the output from this command.
#                  $revision_id : The revision id which is to have its data
#                                 returned.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub get_revision($\$$)
{

    my Monotone::AutomateStdio $this = shift();
    my($ref, $revision_id) = @_;

    # Run the command and get the data, either as one lump or as a structured
    # list.

    if (ref($ref) eq "SCALAR")
    {
	return mtn_command($this, "get_revision", $ref, $revision_id);
    }
    else
    {

	my($attr,
	   $from_id,
	   $from_name,
	   $i,
	   $id,
	   $j,
	   @lines,
	   $name,
	   $to_id,
	   $to_name,
	   $value);

	if (! mtn_command($this, "get_revision", \@lines, $revision_id))
	{
	    return;
	}

	# Reformat the data into a structured array.

	for ($i = $j = 0, @$ref = (); $i <= $#lines; ++ $i)
	{
	    if ($lines[$i] =~ m/^ *add_dir \"/o)
	    {
		get_quoted_value(@lines, $i, $name);
		$$ref[$j ++] = {type => "add_dir",
				name => unescape($name)};
	    }
	    elsif ($lines[$i] =~ m/^ *add_file \"/o)
	    {
		get_quoted_value(@lines, $i, $name);
		if ($lines[++ $i] =~ m/^ *content \[[^\]]+\]$/o)
		{
		    ($id) = ($lines[$i] =~ m/^ *content \[([^\]]+)\]$/o);
		}
		else
		{
		    croak("Corrupt revision, expected content field but "
			  . "didn't find it");
		}
		$$ref[$j ++] = {type    => "add_file",
				name    => unescape($name),
				file_id => $id};
	    }
	    elsif ($lines[$i] =~ m/^ *clear \"/o)
	    {
		get_quoted_value(@lines, $i, $name);
		if ($lines[++ $i] =~ m/^ *attr \"/o)
		{
		    get_quoted_value(@lines, $i, $attr);
		}
		else
		{
		    croak("Corrupt revision, expected attr field but didn't "
			  . "find it");
		}
		$$ref[$j ++] = {type      => "clear",
				name      => unescape($name),
				attribute => unescape($attr)};
	    }
	    elsif ($lines[$i] =~ m/^ *delete \"/o)
	    {
		get_quoted_value(@lines, $i, $name);
		$$ref[$j ++] = {type => "delete",
				name => unescape($name)};
	    }
	    elsif ($lines[$i] =~ m/^ *new_manifest \[[^\]]+\]$/o)
	    {
		($id) = ($lines[$i] =~ m/^ *new_manifest \[([^\]]+)\]$/o);
		$$ref[$j ++] = {type        => "new_manifest",
				manifest_id => $id};
	    }
	    elsif ($lines[$i] =~ m/^ *old_revision \[[^\]]+\]$/o)
	    {
		($id) = ($lines[$i] =~ m/^ *old_revision \[([^\]]+)\]$/o);
		$$ref[$j ++] = {type        => "old_revision",
				revision_id => $id};
	    }
	    elsif ($lines[$i] =~ m/^ *patch \"/o)
	    {
		get_quoted_value(@lines, $i, $name);
		if ($lines[++ $i] =~ m/^ *from \[[^\]]+\]$/o)
		{
		    ($from_id) = ($lines[$i] =~ m/^ *from \[([^\]]+)\]$/o);
		}
		else
		{
		    croak("Corrupt revision, expected from field but didn't "
			  . "find it");
		}
		if ($lines[++ $i] =~ m/^ *to \[[^\]]+\]$/o)
		{
		    ($to_id) = ($lines[$i] =~ m/^ *to \[([^\]]+)\]$/o);
		}
		else
		{
		    croak("Corrupt revision, expected to field but didn't "
			  . "find it");
		}
		$$ref[$j ++] = {type         => "patch",
				name         => unescape($name),
				from_file_id => $from_id,
				to_file_id   => $to_id};
	    }
	    elsif ($lines[$i] =~ m/^ *rename \"/o)
	    {
		get_quoted_value(@lines, $i, $from_name);
		if ($lines[++ $i] =~ m/^ *to \"/o)
		{
		    get_quoted_value(@lines, $i, $to_name);
		}
		else
		{
		    croak("Corrupt revision, expected to field but didn't "
			  . "find it");
		}
		$$ref[$j ++] = {type      => "rename",
				from_name => unescape($from_name),
				to_name   => unescape($to_name)};
	    }
	    elsif ($lines[$i] =~ m/^ *set \"/o)
	    {
		get_quoted_value(@lines, $i, $name);
		if ($lines[++ $i] =~ m/^ *attr \"/o)
		{
		    get_quoted_value(@lines, $i, $attr);
		}
		else
		{
		    croak("Corrupt revision, expected attr field but didn't "
			  . "find it");
		}
		if ($lines[++ $i] =~ m/^ *value \"/o)
		{
		    get_quoted_value(@lines, $i, $value);
		}
		else
		{
		    croak("Corrupt revision, expected value field but didn't "
			  . "find it");
		}
		$$ref[$j ++] = {type      => "set",
				name      => unescape($name),
				attribute => unescape($attr),
				value     => unescape($value)};
	    }
	}

	return 1;

    }

}
#
##############################################################################
#
#   Routine      - graph
#
#   Description  - Get a complete ancestry graph of the database.
#
#   Data         - $this        : The object.
#                  $ref         : A reference to a buffer or an array that is
#                                 to contain the output from this command.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub graph($$)
{

    my Monotone::AutomateStdio $this = $_[0];
    my $ref = $_[1];

    # Run the command and get the data, either as one lump or as a structured
    # list.

    if (ref($ref) eq "SCALAR")
    {
	return mtn_command($this, "graph", $ref);
    }
    else
    {

	my($i,
	   @lines,
	   @parent_ids,
	   $rev_id);

	if (! mtn_command($this, "graph", \@lines))
	{
	    return;
	}
	for ($i = 0, @$ref = (); $i <= $#lines; ++ $i)
	{
	    @parent_ids = split(/ /o, $lines[$i]);
	    $rev_id = shift(@parent_ids);
	    $$ref[$i] = {revision_id => $rev_id,
			 parent_ids  => [@parent_ids]};
	}

	return 1;

    }

}
#
##############################################################################
#
#   Routine      - heads
#
#   Description  - Get a list of revision ids that are heads on the specified
#                  branch. If no branch is given then the workspace's branch
#                  is used.
#
#   Data         - $this        : The object.
#                  \@list       : A reference to a list that is to contain the
#                                 revision ids.
#                  $branch_name : The name of the branch that is to have its
#                                 heads returned.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub heads($\@;$)
{

    my Monotone::AutomateStdio $this = shift();
    my($list, $branch_name) = @_;

    return mtn_command($this, "heads", $list, $branch_name);

}
#
##############################################################################
#
#   Routine      - identify
#
#   Description  - Get the file id, i.e. hash, of the specified file.
#
#   Data         - $this        : The object.
#                  \$buffer     : A reference to a buffer that is to contain
#                                 the output from this command.
#                  $file_name   : The name of the file that is to have its id
#                                 returned.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub identify($\$$)
{

    my Monotone::AutomateStdio $this = shift();
    my($buffer, $file_name) = @_;

    my @list;

    $$buffer = "";
    if (! mtn_command($this, "identify", \@list, $file_name))
    {
	return;
    }
    $$buffer = $list[0];

    return 1;

}
#
##############################################################################
#
#   Routine      - interface_version
#
#   Description  - Get the version of the mtn automate interface.
#
#   Data         - $this        : The object.
#                  \$buffer     : A reference to a buffer that is to contain
#                                 the output from this command.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub interface_version($\$)
{

    my Monotone::AutomateStdio $this = $_[0];
    my $buffer = $_[1];

    my @list;

    $$buffer = "";
    if (! mtn_command($this, "interface_version", \@list))
    {
	return;
    }
    $$buffer = $list[0];

    return 1;

}
#
##############################################################################
#
#   Routine      - inventory
#
#   Description  - Get the inventory for the current workspace.
#
#   Data         - $this        : The object.
#                  $ref         : A reference to a buffer or an array that is
#                                 to contain the output from this command.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub inventory($$)
{

    my Monotone::AutomateStdio $this = $_[0];
    my $ref = $_[1];

    # Run the command and get the data, either as one lump or as a structured
    # list.

    if (ref($ref) eq "SCALAR")
    {
	return mtn_command($this, "inventory", $ref);
    }
    else
    {

	my @lines;

	if (! mtn_command($this, "inventory", \@lines))
	{
	    return;
	}

	# The output format of this command was switched over to a basic_io
	# stanza in 0.37 (i/f version 6.x).

	if ($this->{mtn_aif_major} < 6)
	{

	    my($i,
	       $j,
	       $name,
	       $ref1,
	       $ref2,
	       $status);

	    # Reformat the data into a structured array.

	    for ($i = $j = 0, @$ref = (); $i <= $#lines; ++ $i)
	    {
		if ($lines[$i] =~ m/^[A-Z ]{3} \d+ \d+ .+$/o)
		{
		    ($status, $ref1, $ref2, $name) =
			($lines[$i] =~ m/^([A-Z ]{3}) (\d+) (\d+) (.+)$/o);
		    $$ref[$j ++] = {status       => $status,
				    crossref_one => $ref1,
				    crossref_two => $ref2,
				    name         => $name};
		}
	    }

	}
	else
	{

	    my(@changes,
	       $fs_type,
	       $i,
	       $j,
	       $list,
	       $new_path,
	       $new_type,
	       $old_path,
	       $old_type,
	       $path,
	       @status);

	    # Reformat the data into a structured array.

	    for ($i = $j = 0, $path = undef, @$ref = (); $i <= $#lines; ++ $i)
	    {

		# The `path' element always starts a new entry, the remaining
		# lines may be in any order.

		if ($lines[$i] =~ m/^ *path \"/o)
		{

		    # Save any existing data to a new entry in the output list.

		    if (defined($path))
		    {
			$$ref[$j ++] = {path     => unescape($path),
					old_type => $old_type,
					new_type => $new_type,
					fs_type  => $fs_type,
					old_path => unescape($old_path),
					new_path => unescape($new_path),
					status   => [@status],
					changes  => [@changes]};
		    }

		    $fs_type = $new_path = $new_type = $old_path = $old_type =
			$path = undef;
		    @changes = @status = ();

		    get_quoted_value(@lines, $i, $path);

		}
		elsif ($lines[$i] =~ m/^ *old_type \"/o)
		{
		    ($old_type) =
			($lines[$i] =~ m/^ *old_type \"([^\"]+)\"$/o);
		}
		elsif ($lines[$i] =~ m/^ *new_type \"/o)
		{
		    ($new_type) =
			($lines[$i] =~ m/^ *new_type \"([^\"]+)\"$/o);
		}
		elsif ($lines[$i] =~ m/^ *fs_type \"/o)
		{
		    ($fs_type) =
			($lines[$i] =~ m/^ *fs_type \"([^\"]+)\"$/o);
		}
		elsif ($lines[$i] =~ m/^ *old_path \"/o)
		{
		    get_quoted_value(@lines, $i, $old_path);
		}
		elsif ($lines[$i] =~ m/^ *new_path \"/o)
		{
		    get_quoted_value(@lines, $i, $new_path);
		}
		elsif ($lines[$i] =~ m/^ *status \"/o)
		{
		    ($list) = ($lines[$i] =~ m/^ *\S+ \"(.+)\"$/o);
		    @status = split(/\" \"/o, $list);
		}
		elsif ($lines[$i] =~ m/^ *changes \"/o)
		{
		    ($list) = ($lines[$i] =~ m/^ *\S+ \"(.+)\"$/o);
		    @changes = split(/\" \"/o, $list);
		}
	    }
	    if (defined($path))
	    {
		$$ref[$j ++] = {path     => unescape($path),
				old_type => $old_type,
				new_type => $new_type,
				fs_type  => $fs_type,
				old_path => unescape($old_path),
				new_path => unescape($new_path),
				status   => [@status],
				changes  => [@changes]};
	    }
	}

	return 1;

    }

}
#
##############################################################################
#
#   Routine      - keys
#
#   Description  - Get a list of all the keys known to mtn.
#
#   Data         - $this        : The object.
#                  $ref         : A reference to a buffer or an array that is
#                                 to contain the output from this command.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub keys($$)
{

    my Monotone::AutomateStdio $this = $_[0];
    my $ref = $_[1];

    # Run the command and get the data, either as one lump or as a structured
    # list.

    if (ref($ref) eq "SCALAR")
    {
	return mtn_command($this, "keys", $ref);
    }
    else
    {

	my($i,
	   $id,
	   $j,
	   @lines,
	   $list,
	   $priv_hash,
	   @priv_loc,
	   $pub_hash,
	   @pub_loc,
	   $name,
	   $type);

	if (! mtn_command($this, "keys", \@lines))
	{
	    return;
	}

	# Reformat the data into a structured array.

	for ($i = $j = 0, @$ref = (); $i <= $#lines;)
	{
	    if ($lines[$i] =~ m/^ *name \"/o)
	    {
		$priv_hash = $pub_hash = undef;
		@priv_loc = @pub_loc = ();
		get_quoted_value(@lines, $i, $name);
		++ $i;
		if ($lines[$i] =~ m/^ *public_hash \[[^\]]+\]$/o)
		{
		    ($pub_hash) =
			($lines[$i ++] =~ m/^ *public_hash \[([^\]]+)\]$/o);
		}
		else
		{
		    croak("Corrupt keys, expected public_hash field but "
			  . "didn't find it");
		}
		if ($lines[$i] =~ m/^ *private_hash \[[^\]]+\]$/o)
		{
		    ($priv_hash) =
			($lines[$i ++] =~ m/^ *private_hash \[([^\]]+)\]$/o);
		}
		if ($lines[$i] =~ m/^ *public_location \"/o)
		{
		    ($list) = ($lines[$i ++] =~ m/^ *\S+ \"(.+)\"$/o);
		    @pub_loc = split(/\" \"/o, $list);
		}
		else
		{
		    croak("Corrupt keys, expected public_location field but "
			  . "didn't find it");
		}
		if ($i <= $#lines && $lines[$i] =~ m/^ *private_location \"/o)
		{
		    ($list) = ($lines[$i ++] =~ m/^ *\S+ \"(.+)\"$/o);
		    @priv_loc = split(/\" \"/o, $list);
		}
		if (defined($priv_hash))
		{
		    $$ref[$j ++] = {type              => "public-private",
				    name              => unescape($name),
				    public_hash       => $pub_hash,
				    private_hash      => $priv_hash,
				    public_locations  => [@pub_loc],
				    private_locations => [@priv_loc]};
		}
		else
		{
		    $$ref[$j ++] = {type             => "public",
				    name             => unescape($name),
				    public_hash      => $pub_hash,
				    public_locations => [@pub_loc]};
		}
	    }
	    else
	    {
		++ $i;
	    }
	}

	return 1;

    }

}
#
##############################################################################
#
#   Routine      - leaves
#
#   Description  - Get a list of leaf revisions.
#
#   Data         - $this        : The object.
#                  \@list       : A reference to a list that is to contain the
#                                 revision ids.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub leaves($\@)
{

    my Monotone::AutomateStdio $this = $_[0];
    my $list = $_[1];

    return mtn_command($this, "leaves", $list);

}
#
##############################################################################
#
#   Routine      - parents
#
#   Description  - Get a list of parents for the specified revision.
#
#   Data         - $this        : The object.
#                  \@list       : A reference to a list that is to contain the
#                                 revision ids.
#                  $revision_id : The revision id that is to have its parents
#                                 returned.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub parents($\@$)
{

    my Monotone::AutomateStdio $this = shift();
    my($list, $revision_id) = @_;

    return mtn_command($this, "parents", $list, $revision_id);

}
#
##############################################################################
#
#   Routine      - roots
#
#   Description  - Get a list of root revisions, i.e. revisions with no
#                  parents.
#
#   Data         - $this        : The object.
#                  \@list       : A reference to a list that is to contain the
#                                 revision ids.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub roots($\@)
{

    my Monotone::AutomateStdio $this = $_[0];
    my $list = $_[1];

    return mtn_command($this, "roots", $list);

}
#
##############################################################################
#
#   Routine      - select
#
#   Description  - Get a list of revision ids that match the specified
#                  selector.
#
#   Data         - $this        : The object.
#                  \@list       : A reference to a list that is to contain the
#                                 revision ids.
#                  $selector    : The selector that is to be used.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub select($\@$)
{

    my Monotone::AutomateStdio $this = shift();
    my($list, $selector) = @_;

    return mtn_command($this, "select", $list, $selector);

}
#
##############################################################################
#
#   Routine      - tags
#
#   Description  - Get all the tags attached to revisions on branches that
#                  match the specified branch pattern. If no pattern is given
#                  then all branches are searched.
#
#   Data         - $this           : The object.
#                  $ref            : A reference to a buffer or an array that
#                                    is to contain the output from this
#                                    command.
#                  $branch_pattern : The branch name pattern that the search
#                                    is to be limited to.
#                  Return Value    : True on success, otherwise false on
#                                    failure.
#
##############################################################################



sub tags($$;$)
{

    my Monotone::AutomateStdio $this = shift();
    my($ref, $branch_pattern) = @_;

    # Run the command and get the data, either as one lump or as a structured
    # list.

    if (ref($ref) eq "SCALAR")
    {
	return mtn_command($this, "tags", $ref, $branch_pattern);
    }
    else
    {

	my(@branches,
	   $i,
	   $j,
	   $k,
	   @lines,
	   $list,
	   $rev,
	   $signer,
	   $tag);

	if (! mtn_command($this, "tags", \@lines, $branch_pattern))
	{
	    return;
	}

	# Reformat the data into a structured array.

	for ($i = $j = 0, @$ref = (); $i <= $#lines; ++ $i)
	{
	    if ($lines[$i] =~ m/^ *tag \"/o)
	    {
		@branches = ();
		get_quoted_value(@lines, $i, $tag);
		if ($lines[++ $i] =~ m/^ *revision \[[^\]]+\]$/o)
		{
		    ($rev) = ($lines[$i] =~ m/^ *revision \[([^\]]+)\]$/o);
		}
		else
		{
		    croak("Corrupt tags list, expected revision field but "
			  . "didn't find it");
		}
		if ($lines[++ $i] =~ m/^ *signer \"/o)
		{
		    get_quoted_value(@lines, $i, $signer);
		}
		else
		{
		    croak("Corrupt tags list, expected signer field but "
			  . "didn't find it");
		}
		if ($lines[++ $i] =~ m/^ *branches/o)
		{
		    if ($lines[$i] =~ m/^ *branches \".+\"$/o)
		    {
			($list) = ($lines[$i] =~ m/^ *branches \"(.+)\"$/o);
			@branches = split(/\" \"/o, $list);
			for ($k = 0; $k <= $#branches; ++ $k)
			{
			    $branches[$k] = unescape($branches[$k]);
			}
		    }
		}
		else
		{
		    croak("Corrupt tags list, expected branches field but "
			  . "didn't find it");
		}
		$$ref[$j ++] = {tag         => unescape($tag),
				revision_id => $rev,
				signer      => unescape($signer),
				branches    => [@branches]};
	    }
	}

	return 1;

    }

}
#
##############################################################################
#
#   Routine      - toposort
#
#   Description  - Sort the specified revisions such that the ancestors come
#                  out first.
#
#   Data         - $this         : The object.
#                  \@list        : A reference to a list that is to contain
#                                  the revision ids.
#                  $revision_ids : The revision id that is to have its
#                                  ancestors returned.
#                  Return Value  : True on success, otherwise false on
#                                  failure.
#
##############################################################################



sub toposort($\@@)
{

    my Monotone::AutomateStdio $this = shift();
    my($list, @revision_ids) = @_;

    return mtn_command($this, "toposort", $list, @revision_ids);

}
#
##############################################################################
#
#   Routine      - error_message
#
#   Description  - Return the last error message received from the mtn
#                  subprocess.
#
#   Data         - $this : The object.
#                  Return Value : The last error message received, or an empty
#                                 string if nothing has gone wrong yet.
#
##############################################################################



sub error_message($)
{

    my Monotone::AutomateStdio $this = $_[0];

    return $this->{mtn_err_msg};

}
#
##############################################################################
#
#   Routine      - closedown
#
#   Description  - If started then stop the mtn subprocess.
#
#   Data         - $this : The object.
#
##############################################################################



sub closedown($)
{

    my Monotone::AutomateStdio $this = $_[0];

    my($err_msg,
       $i,
       $ret_val);

    if ($this->{mtn_pid} != 0)
    {
	close($this->{mtn_in});
	close($this->{mtn_out});
	close($this->{mtn_err});
	for ($i = 0; $i < 3; ++ $i)
	{
	    $ret_val = 0;
	    eval
	    {
		local $SIG{ALRM} = sub { die("internal sigalarm"); };
		alarm(5);
		$ret_val = waitpid($this->{mtn_pid}, 0);
		alarm(0);
	    };
	    if ($ret_val == $this->{mtn_pid})
	    {
		last;
	    }
	    elsif ($ret_val == 0)
	    {
		if ($i == 0)
		{
		    kill("TERM", $this->{mtn_pid});
		}
		else
		{
		    kill("KILL", $this->{mtn_pid});
		}
	    }
	    else
	    {
		$err_msg = $!;
		kill("KILL", $this->{mtn_pid});
		croak("waitpid failed: $err_msg");
	    }
	}
	$this->{mtn_pid} = 0;
    }

}
#
##############################################################################
#
#   Routine      - mtn_command
#
#   Description  - Handle mtn commands that take no options and zero or more
#                  arguments. Depending upon what type of reference is passed,
#                  data is either returned in one large lump (scalar
#                  reference), or an array of lines (array reference).
#
#   Data         - $this        : The object.
#                  $cmd         : The mtn automate command that is to be run.
#                  $ref         : A reference to a buffer or an array that is
#                                 to contain the output from this command.
#                  @parameters  : A list of parameters to be applied to the
#                                 command.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub mtn_command($$$@)
{

    my Monotone::AutomateStdio $this = shift();
    my($cmd, $ref, @parameters) = @_;

    my @dummy;

    return mtn_command_with_options($this, $cmd, $ref, @dummy, @parameters);

}
#
##############################################################################
#
#   Routine      - mtn_command_with_options
#
#   Description  - Handle mtn commands that take options and zero or more
#                  arguments. Depending upon what type of reference is passed,
#                  data is either returned in one large lump (scalar
#                  reference), or an array of lines (array reference).
#
#   Data         - $this        : The object.
#                  $cmd         : The mtn automate command that is to be run.
#                  $ref         : A reference to a buffer or an array that is
#                                 to contain the output from this command.
#                  \@options    : A reference to a list containing key/value
#                                 anonymous hashes.
#                  @parameters  : A list of parameters to be applied to the
#                                 command.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub mtn_command_with_options($$$\@@)
{

    my Monotone::AutomateStdio $this = shift();
    my($cmd, $ref, $options, @parameters) = @_;

    my($buffer,
       $in,
       $opt,
       $param);

    if ($this->{mtn_pid} == 0)
    {
	startup($this);
    }

    # Run the command and get the data, the unless below is required just in
    # case undef is passed as the only parameter (which can happen when a
    # mandatory argument is not passed by the caller).

    $in = $this->{mtn_in};
    printf($in "o") unless ($#$options < 0);
    foreach $opt (@$options)
    {
	printf($in "%d:%s%d:%s",
	       length($opt->{key}),
	       $opt->{key},
	       length($opt->{value}),
	       $opt->{value});
    }
    printf($in "e ") unless ($#$options < 0);
    printf($in "l%d:%s", length($cmd), $cmd);
    foreach $param (@parameters)
    {
	printf($in "%d:%s", length($param), $param) unless (! defined($param));
    }
    print($in "e\n");

    # Depending upon what we have been given a reference to, either return the
    # data as one chunk or as an array of lines.

    if (ref($ref) eq "SCALAR")
    {
	return mtn_read_output($this, $$ref);
    }
    else
    {
	if (! mtn_read_output($this, $buffer))
	{
	    return;
	}
	@$ref = split(/\n/o, $buffer);
    }

    return 1;

}
#
##############################################################################
#
#   Routine      - mtn_read_output
#
#   Description  - Reads the output from mtn, removing chunk headers.
#
#   Data         - $this        : The object.
#                  \$buffer     : A reference to the buffer that is to contain
#                                 the data.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub mtn_read_output($\$)
{

    my Monotone::AutomateStdio $this = $_[0];
    my $buffer = $_[1];

    my($bytes_read,
       $char,
       $chunk_start,
       $cmd_nr,
       $colons,
       $err,
       $err_code,
       $err_occurred,
       $header,
       $i,
       $last,
       $offset,
       $size);

    $err = $this->{mtn_err};

    $$buffer = "";
    $chunk_start = 1;
    $err_occurred = 0;
    $last = "m";
    $offset = 0;
    do
    {

	# If necessary, read in and process the chunk header, then we know how
	# much to read in etc.

	if ($chunk_start)
	{

	    # Read header, one byte at a time until we have what we need or
	    # there is an error.

	    for ($header = "", $colons = $i = 0;
		 $colons < 4 && read($this->{mtn_out}, $header, 1, $i);
		 ++ $i)
	    {
		$char = substr($header, $i, 1);
		if ($char eq ":")
		{
		    ++ $colons;
		}
		elsif ($colons == 2)
		{
		    if ($char ne "m" && $char ne "l")
		    {
			croak("Corrupt/missing mtn chunk header, mtn gave:\n"
			      . join("", <$err>));
		    }
		}
		elsif ($char =~ m/\D$/o)
		{
		    croak("Corrupt/missing mtn chunk header, mtn gave:\n"
			  . join("", <$err>));
		}
	    }

	    # Break out the header into its separate fields.

	    if ($header =~ m/^\d+:\d+:[lm]:\d+:$/o)
	    {
		($cmd_nr, $err_code, $last, $size) =
		    ($header =~ m/^(\d+):(\d+):([lm]):(\d+):$/o);
		if ($cmd_nr != $this->{cmd_cnt})
		{
		    croak("Mtn command count is out of sequence");
		}
		if ($err_code != 0)
		{
		    $err_occurred = 1;
		}
	    }
	    else
	    {
		croak("Corrupt/missing mtn chunk header, mtn gave:\n"
		      . join("", <$err>));
	    }

	    $chunk_start = 0;

	}

	# Read in what we require.

	if ($size > 0)
	{
	    if (! defined($bytes_read = read($this->{mtn_out},
					     $$buffer,
					     $size,
					     $offset)))
	    {
		croak("read failed: $!");
	    }
	    $size -= $bytes_read;
	    $offset += $bytes_read;
	}
	if ($size == 0 && $last eq "m")
	{
	    $chunk_start = 1;
	}

    }
    while ($size > 0 || $last eq "m");

    ++ $this->{cmd_cnt};

    # Deal with errors (message is in $$buffer).

    if ($err_occurred)
    {
	$this->{mtn_err_msg} = $$buffer;
	$$buffer = "";
	return;
    }

    return 1;

}
#
##############################################################################
#
#   Routine      - startup
#
#   Description  - If necessary start up the mtn subprocess.
#
#   Data         - $this : The object.
#
##############################################################################



sub startup($)
{

    my Monotone::AutomateStdio $this = $_[0];

    my $version;

    if ($this->{mtn_pid} == 0)
    {
	$this->{mtn_err} = gensym();
	if ($this->{db_name})
	{
	    $this->{mtn_pid} = open3($this->{mtn_in},
				     $this->{mtn_out},
				     $this->{mtn_err},
				     "mtn",
				     "--db=" . $this->{db_name},
				     "automate",
				     "stdio");
	}
	else
	{
	    $this->{mtn_pid} = open3($this->{mtn_in},
				     $this->{mtn_out},
				     $this->{mtn_err},
				     "mtn",
				     "automate",
				     "stdio");
	}
	$this->{cmd_cnt} = 0;
	interface_version($this, $version);
	($this->{mtn_aif_major}, $this->{mtn_aif_minor}) =
	    ($version =~ m/^(\d+)\.(\d+)$/o);
    }

}
#
##############################################################################
#
#   Routine      - get_quoted_value
#
#   Description  - Get the contents of a quoted value that may span several
#                  lines and contain escaped quotes.
#
#   Data         - \@list       : The reference to the list that contains the
#                                 quoted string.
#                  \$index      : The index of the line in the array
#                                 containing the opening quote (assumed to be
#                                 the first quote encountered). It is updated
#                                 with the index of the line containing the
#                                 closing quote at the end of the line.
#                  \$buffer     : A reference to a buffer that is to contain
#                                 the contents of the quoted string.
#
##############################################################################



sub get_quoted_value(\@\$\$)
{

    my($list, $index, $buffer) = @_;

    # Deal with multiple lines.

    $$buffer = substr($$list[$$index], index($$list[$$index], "\"") + 1);
    if ($$list[$$index] !~ m/$closing_quote_re/)
    {
	do
	{
	    $$buffer .= "\n" . $$list[++ $$index];
	}
	while ($$list[$$index] !~ m/$closing_quote_re/);
    }
    substr($$buffer, -1, 1, "");

}
#
##############################################################################
#
#   Routine      - unescape
#
#   Description  - Process mtn escape characters to get back the original
#                  data.
#
#   Data         - $data        : The escaped data.
#                  Return Value : The unescaped data.
#
##############################################################################



sub unescape($)
{

    my $data = $_[0];

    return undef unless defined($data);

    $data =~ s/\\\\/\\/g;
    $data =~ s/\\\"/\"/g;

    return $data;

}

1;

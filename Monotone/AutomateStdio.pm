##############################################################################
#
#   File Name    - AutomateStdio.pm
#
#   Description  - Class module that provides an interface to Monotone's
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
#                  PURPOSE. See the GNU Lesser General Public License for
#                  more details.
#
#                  You should have received a copy of the GNU Lesser General
#                  Public License along with this library; if not, write to
#                  the Free Software Foundation, Inc., 59 Temple Place - Suite
#                  330, Boston, MA 02111-1307 USA.
#
##############################################################################
#
##############################################################################
#
#   Package      - Monotone::AutomateStdio
#
#   Description  - See above.
#
##############################################################################



# ***** PACKAGE DECLARATION *****

package Monotone::AutomateStdio;

# ***** DIRECTIVES *****

require 5.008;

no locale;
use integer;
use strict;
use warnings;

# ***** REQUIRED PACKAGES *****

# Standard Perl and CPAN modules.

use Carp;
use IO::Poll qw(POLLIN POLLPRI);
use IPC::Open3;
use POSIX qw(:errno_h);
use Symbol qw(gensym);

# ***** GLOBAL DATA DECLARATIONS *****

# Constants used to represent the different types of capability Monotone may or
# may not provide depending upon its version.

use constant MTN_IGNORE_SUSPEND_CERTS       => 1;
use constant MTN_INVENTORY_IO_STANZA_FORMAT => 2;
use constant MTN_P_SELECTOR                 => 3;

# A pre-compiled regular expression for finding the end of a quoted string
# possibly containing escaped quotes, i.e. " preceeded by a non-backslash
# character or an even number of backslash characters.

my $closing_quote_re = qr/((^.*[^\\])|^)(\\{2})*\"$/;

# A pre-compiled regular expression for recognising database locked conditions
# in error output.

my $database_locked_re = qr/.*sqlite error: database is locked.*/;

# Global error, database locked and io wait callback routine references and
# associated client data.

my $carper = sub { return; };
my $croaker = \&croak;
my $db_locked_handler = sub { return; };
my $io_wait_handler = sub { return; };
my($db_locked_handler_data,
   $error_handler,
   $error_handler_data,
   $io_wait_handler_data,
   $io_wait_handler_timeout,
   $warning_handler,
   $warning_handler_data);

# ***** FUNCTIONAL PROTOTYPES *****

# Public methods.

sub ancestors($\@@);
sub ancestry_difference($\@$;@);
sub branches($\@);
sub can($$);
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
sub get_attributes($\$$);
sub get_base_revision_id($\$);
sub get_content_changed($\@$$);
sub get_corresponding_path($\$$$$);
sub get_current_revision_id($\$);
sub get_db_name($);
sub get_error_message($);
sub get_file($\$$);
sub get_file_of($\$$;$);
sub get_manifest_of($$;$);
sub get_option($\$$);
sub get_pid($);
sub get_revision($\$$);
sub graph($$);
sub heads($\@;$);
sub identify($\$$);
sub ignore_suspend_certs($$);
sub interface_version($\$);
sub inventory($$);
sub keys($$);
sub leaves($\@);
sub new($;$);
sub parents($\@$);
sub register_db_locked_handler(;$$$);
sub register_error_handler($;$$$);
sub register_io_wait_handler(;$$$$);
sub roots($\@);
sub select($\@$);
sub tags($$;$);
sub toposort($\@@);

# Private methods and routines.

sub error_handler_wrapper($);
sub get_quoted_value(\@\$\$);
sub mtn_command($$$@);
sub mtn_command_with_options($$$\@@);
sub mtn_read_output($\$);
sub startup($);
sub unescape($);
sub warning_handler_wrapper($);

# ***** PACKAGE INFORMATION *****

# We are just a base class.

use base qw(Exporter);

our %EXPORT_TAGS = (constants => [qw(MTN_IGNORE_SUSPEND_CERTS
				     MTN_INVENTORY_IO_STANZA_FORMAT
				     MTN_P_SELECTOR)]);
our @EXPORT = qw();
Exporter::export_ok_tags(qw(constants));
our $VERSION = 0.6;
#
##############################################################################
#
#   Routine      - new
#
#   Description  - Class constructor.
#
#   Data         - $class       : Either the name of the class that is to be
#                                 created or an object of that class.
#                  $db_name     : The full path of the Monotone database. If
#                                 this is not provided then the database
#                                 associated with the current workspace is
#                                 used.
#                  Return Value : A reference to the newly created object.
#
##############################################################################



sub new($;$)
{


    my $class = (ref($_[0]) ne "") ? ref($_[0]) : $_[0];
    my $db_name = $_[1];

    my $this;

    $this = {db_name                 => $db_name,
	     mtn_pid                 => 0,
	     mtn_in                  => undef,
	     mtn_out                 => undef,
	     mtn_err                 => undef,
	     poll                    => undef,
	     error_msg               => "",
	     honour_suspend_certs    => 1,
	     mtn_aif_major           => 0,
	     mtn_aif_minor           => 0,
	     cmd_cnt                 => 0,
	     db_locked_handler       => undef,
	     db_locked_handler_data  => undef,
	     io_wait_handler         => undef,
	     io_wait_handler_data    => undef,
	     io_wait_handler_timeout => 1};
    bless($this, $class);

    startup($this);

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

    my $this = shift();

    # Make sure the destructor doesn't throw any exceptions and that any
    # existing exception status is preserved, otherwise constructor
    # exceptions could be lost. E.g. if the constructor throws an exception
    # after blessing the object, Perl immediately calls the destructor,
    # which calls code that could use eval thereby resetting $@.  Why not
    # simply call bless as the last statement in the constructor? Well
    # firstly callbacks can be called in the constructor and they have the
    # object passed to them as their first argument and so it needs to be
    # blessed, secondly the mtn subprocess needs to be properly closed down
    # if there is an exception, which it won't be unless the destructor is
    # called.

    {
	local $@;
	eval
	{
	    closedown($this);
	};
    }

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

    my($this, $list, @revision_ids) = @_;

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

    my($this, $list, $new_revision_id, @old_revision_ids) = @_;

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

    my($this, $list) = @_;

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

    my($this, $revision_id, $name, $value) = @_;

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

    my($this, $ref, $revision_id) = @_;

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
	    if ($lines[$i] =~ m/^ *key \"/)
	    {
		get_quoted_value(@lines, $i, $key);
		if ($lines[++ $i] =~ m/^ *signature \"/)
		{
		    ($signature) =
			($lines[$i] =~ m/^ *signature \"([^\"]+)\"$/);
		}
		else
		{
		    &$croaker("Corrupt certs list, expected signature field "
			      . "but didn't find it");
		}
		if ($lines[++ $i] =~ m/^ *name \"/)
		{
		    get_quoted_value(@lines, $i, $name);
		}
		else
		{
		    &$croaker("Corrupt certs list, expected name field but "
			      . "didn't find it");
		}
		if ($lines[++ $i] =~ m/^ *value \"/)
		{
		    get_quoted_value(@lines, $i, $value);
		}
		else
		{
		    &$croaker("Corrupt certs list, expected value field but "
			      . "didn't find it");
		}
		if ($lines[++ $i] =~ m/^ *trust \"/)
		{
		    ($trust) = ($lines[$i] =~ m/^ *trust \"([^\"]+)\"$/);
		}
		else
		{
		    &$croaker("Corrupt certs list, expected trust field but "
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

    my($this, $list, @revision_ids) = @_;

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

    my($this, $list, @revision_ids) = @_;

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

    my($this, $buffer, $revision_id1, $revision_id2, @file_names) = @_;

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

    my($this, $buffer, $domain, $name) = @_;

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

    my($this, $domain, $name, $value) = @_;

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

    my($this, $list, @revision_ids) = @_;

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

    my($this, $list, @revision_ids) = @_;

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

    my($this, $ref, $file_name) = @_;

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
	    if ($lines[$i] =~ m/^ *attr \"/)
	    {
		($list) = ($lines[$i] =~ m/^ *\S+ \"(.+)\"$/);
		($key, $value) = split(/\" \"/, $list);
		if ($lines[++ $i] =~ m/^ *state \"/)
		{
		    ($state) = ($lines[$i] =~ m/^ *state \"([^\"]+)\"$/);
		}
		else
		{
		    &$croaker("Corrupt attributes list, expected state field "
			      . "but didn't find it");
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

    my($this, $buffer) = @_;

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

    my($this, $list, $revision_id, $file_name) = @_;

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
	if ($lines[$i] =~ m/^ *content_mark \[[^\]]+\]$/)
	{
	    ($$list[$j ++]) = ($lines[$i] =~ m/^ *content_mark \[([^\]]+)\]$/);
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

    my($this, $buffer, $source_revision_id, $file_name, $target_revision_id)
	= @_;

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
	if ($lines[$i] =~ m/^ *file \"/)
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

    my($this, $buffer) = @_;

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

    my($this, $buffer, $file_id) = @_;

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

    my($this, $buffer, $file_name, $revision_id) = @_;

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

    my($this, $ref, $revision_id) = @_;

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
	    if ($lines[$i] =~ m/^ *file \"/)
	    {
		$type = "file";
		get_quoted_value(@lines, $i, $name);
		if ($lines[++ $i] =~ m/^ *content \[[^\]]+\]$/)
		{
		    ($id) = ($lines[$i] =~ m/^ *content \[([^\]]+)\]$/);
		}
		else
		{
		    &$croaker("Corrupt manifest, expected content field but "
			      . "didn't find it");
		}
	    }
	    if ($lines[$i] =~ m/^ *dir \"/)
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

    my($this, $buffer, $option_name) = @_;

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

    my($this, $ref, $revision_id) = @_;

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
	    if ($lines[$i] =~ m/^ *add_dir \"/)
	    {
		get_quoted_value(@lines, $i, $name);
		$$ref[$j ++] = {type => "add_dir",
				name => unescape($name)};
	    }
	    elsif ($lines[$i] =~ m/^ *add_file \"/)
	    {
		get_quoted_value(@lines, $i, $name);
		if ($lines[++ $i] =~ m/^ *content \[[^\]]+\]$/)
		{
		    ($id) = ($lines[$i] =~ m/^ *content \[([^\]]+)\]$/);
		}
		else
		{
		    &$croaker("Corrupt revision, expected content field but "
			      . "didn't find it");
		}
		$$ref[$j ++] = {type    => "add_file",
				name    => unescape($name),
				file_id => $id};
	    }
	    elsif ($lines[$i] =~ m/^ *clear \"/)
	    {
		get_quoted_value(@lines, $i, $name);
		if ($lines[++ $i] =~ m/^ *attr \"/)
		{
		    get_quoted_value(@lines, $i, $attr);
		}
		else
		{
		    &$croaker("Corrupt revision, expected attr field but "
			      . "didn't find it");
		}
		$$ref[$j ++] = {type      => "clear",
				name      => unescape($name),
				attribute => unescape($attr)};
	    }
	    elsif ($lines[$i] =~ m/^ *delete \"/)
	    {
		get_quoted_value(@lines, $i, $name);
		$$ref[$j ++] = {type => "delete",
				name => unescape($name)};
	    }
	    elsif ($lines[$i] =~ m/^ *new_manifest \[[^\]]+\]$/)
	    {
		($id) = ($lines[$i] =~ m/^ *new_manifest \[([^\]]+)\]$/);
		$$ref[$j ++] = {type        => "new_manifest",
				manifest_id => $id};
	    }
	    elsif ($lines[$i] =~ m/^ *old_revision \[[^\]]*\]$/)
	    {
		($id) = ($lines[$i] =~ m/^ *old_revision \[([^\]]*)\]$/);
		$$ref[$j ++] = {type        => "old_revision",
				revision_id => $id};
	    }
	    elsif ($lines[$i] =~ m/^ *patch \"/)
	    {
		get_quoted_value(@lines, $i, $name);
		if ($lines[++ $i] =~ m/^ *from \[[^\]]+\]$/)
		{
		    ($from_id) = ($lines[$i] =~ m/^ *from \[([^\]]+)\]$/);
		}
		else
		{
		    &$croaker("Corrupt revision, expected from field but "
			      . "didn't find it");
		}
		if ($lines[++ $i] =~ m/^ *to \[[^\]]+\]$/)
		{
		    ($to_id) = ($lines[$i] =~ m/^ *to \[([^\]]+)\]$/);
		}
		else
		{
		    &$croaker("Corrupt revision, expected to field but didn't "
			      . "find it");
		}
		$$ref[$j ++] = {type         => "patch",
				name         => unescape($name),
				from_file_id => $from_id,
				to_file_id   => $to_id};
	    }
	    elsif ($lines[$i] =~ m/^ *rename \"/)
	    {
		get_quoted_value(@lines, $i, $from_name);
		if ($lines[++ $i] =~ m/^ *to \"/)
		{
		    get_quoted_value(@lines, $i, $to_name);
		}
		else
		{
		    &$croaker("Corrupt revision, expected to field but didn't "
			      . "find it");
		}
		$$ref[$j ++] = {type      => "rename",
				from_name => unescape($from_name),
				to_name   => unescape($to_name)};
	    }
	    elsif ($lines[$i] =~ m/^ *set \"/)
	    {
		get_quoted_value(@lines, $i, $name);
		if ($lines[++ $i] =~ m/^ *attr \"/)
		{
		    get_quoted_value(@lines, $i, $attr);
		}
		else
		{
		    &$croaker("Corrupt revision, expected attr field but "
			      . "didn't find it");
		}
		if ($lines[++ $i] =~ m/^ *value \"/)
		{
		    get_quoted_value(@lines, $i, $value);
		}
		else
		{
		    &$croaker("Corrupt revision, expected value field but "
			      . "didn't find it");
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

    my($this, $ref) = @_;

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
	    @parent_ids = split(/ /, $lines[$i]);
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

    my($this, $list, $branch_name) = @_;

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

    my($this, $buffer, $file_name) = @_;

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

    my($this, $buffer) = @_;

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

    my($this, $ref) = @_;

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

	if (! can($this, MTN_INVENTORY_IO_STANZA_FORMAT))
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
		if ($lines[$i] =~ m/^[A-Z ]{3} \d+ \d+ .+$/)
		{
		    ($status, $ref1, $ref2, $name) =
			($lines[$i] =~ m/^([A-Z ]{3}) (\d+) (\d+) (.+)$/);
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

		if ($lines[$i] =~ m/^ *path \"/)
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
		elsif ($lines[$i] =~ m/^ *old_type \"/)
		{
		    ($old_type) = ($lines[$i] =~ m/^ *old_type \"([^\"]+)\"$/);
		}
		elsif ($lines[$i] =~ m/^ *new_type \"/)
		{
		    ($new_type) = ($lines[$i] =~ m/^ *new_type \"([^\"]+)\"$/);
		}
		elsif ($lines[$i] =~ m/^ *fs_type \"/)
		{
		    ($fs_type) = ($lines[$i] =~ m/^ *fs_type \"([^\"]+)\"$/);
		}
		elsif ($lines[$i] =~ m/^ *old_path \"/)
		{
		    get_quoted_value(@lines, $i, $old_path);
		}
		elsif ($lines[$i] =~ m/^ *new_path \"/)
		{
		    get_quoted_value(@lines, $i, $new_path);
		}
		elsif ($lines[$i] =~ m/^ *status \"/)
		{
		    ($list) = ($lines[$i] =~ m/^ *\S+ \"(.+)\"$/);
		    @status = split(/\" \"/, $list);
		}
		elsif ($lines[$i] =~ m/^ *changes \"/)
		{
		    ($list) = ($lines[$i] =~ m/^ *\S+ \"(.+)\"$/);
		    @changes = split(/\" \"/, $list);
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

    my($this, $ref) = @_;

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
	    if ($lines[$i] =~ m/^ *name \"/)
	    {
		$priv_hash = $pub_hash = undef;
		@priv_loc = @pub_loc = ();
		get_quoted_value(@lines, $i, $name);
		++ $i;
		if ($lines[$i] =~ m/^ *public_hash \[[^\]]+\]$/)
		{
		    ($pub_hash) =
			($lines[$i ++] =~ m/^ *public_hash \[([^\]]+)\]$/);
		}
		else
		{
		    &$croaker("Corrupt keys, expected public_hash field but "
			      . "didn't find it");
		}
		if ($lines[$i] =~ m/^ *private_hash \[[^\]]+\]$/)
		{
		    ($priv_hash) =
			($lines[$i ++] =~ m/^ *private_hash \[([^\]]+)\]$/);
		}
		if ($lines[$i] =~ m/^ *public_location \"/)
		{
		    ($list) = ($lines[$i ++] =~ m/^ *\S+ \"(.+)\"$/);
		    @pub_loc = split(/\" \"/, $list);
		}
		else
		{
		    &$croaker("Corrupt keys, expected public_location field "
			      . "but didn't find it");
		}
		if ($i <= $#lines && $lines[$i] =~ m/^ *private_location \"/)
		{
		    ($list) = ($lines[$i ++] =~ m/^ *\S+ \"(.+)\"$/);
		    @priv_loc = split(/\" \"/, $list);
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

    my($this, $list) = @_;

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

    my($this, $list, $revision_id) = @_;

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

    my($this, $list) = @_;

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

    my($this, $list, $selector) = @_;

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

    my($this, $ref, $branch_pattern) = @_;

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
	    if ($lines[$i] =~ m/^ *tag \"/)
	    {
		@branches = ();
		get_quoted_value(@lines, $i, $tag);
		if ($lines[++ $i] =~ m/^ *revision \[[^\]]+\]$/)
		{
		    ($rev) = ($lines[$i] =~ m/^ *revision \[([^\]]+)\]$/);
		}
		else
		{
		    &$croaker("Corrupt tags list, expected revision field but "
			      . "didn't find it");
		}
		if ($lines[++ $i] =~ m/^ *signer \"/)
		{
		    get_quoted_value(@lines, $i, $signer);
		}
		else
		{
		    &$croaker("Corrupt tags list, expected signer field but "
			      . "didn't find it");
		}
		if ($lines[++ $i] =~ m/^ *branches/)
		{
		    if ($lines[$i] =~ m/^ *branches \".+\"$/)
		    {
			($list) = ($lines[$i] =~ m/^ *branches \"(.+)\"$/);
			@branches = split(/\" \"/, $list);
			for ($k = 0; $k <= $#branches; ++ $k)
			{
			    $branches[$k] = unescape($branches[$k]);
			}
		    }
		}
		else
		{
		    &$croaker("Corrupt tags list, expected branches field but "
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
#                  $revision_ids : The revision ids that are to be sorted with
#                                  the ancestors coming first.
#                  Return Value  : True on success, otherwise false on
#                                  failure.
#
##############################################################################



sub toposort($\@@)
{

    my($this, $list, @revision_ids) = @_;

    return mtn_command($this, "toposort", $list, @revision_ids);

}
#
##############################################################################
#
#   Routine      - can
#
#   Description  - Determine whether a certain feature is available with the
#                  version of Monotone that is currently being used.
#
#   Data         - $this         : The object.
#                  $feature      : A constant specifying the feature that is
#                                  to be checked for.
#                  Return Value  : True if the feature is supported, otherwise
#                                  false if it is not.
#
##############################################################################



sub can($$)
{

    my($this, $feature) = @_;

    if ($feature == MTN_IGNORE_SUSPEND_CERTS)
    {

	# This is only available from version 0.37 (i/f version 6.x).

	return 1 if ($this->{mtn_aif_major} >= 6);

    }
    elsif ($feature == MTN_INVENTORY_IO_STANZA_FORMAT)
    {

	# This is only available from version 0.37 (i/f version 6.x).

	return 1 if ($this->{mtn_aif_major} >= 6);

    }
    elsif ($feature == MTN_P_SELECTOR)
    {

	# This is only available from version 0.37 (i/f version 6.x).

	return 1 if ($this->{mtn_aif_major} >= 6);

    }
    else
    {

	# An unknown feature was requested.

	$this->{error_msg} = "Unknown feature requested";
	&$carper($this->{error_msg});

    }

    return;

}
#
##############################################################################
#
#   Routine      - ignore_suspend_certs
#
#   Description  - Determine whether revisions with the suspend cert are to be
#                  ignored or not. If the head revisions on a branch are all
#                  suspended then that branch is also ignored.
#
#   Data         - $this         : The object.
#                  $ignore       : True if suspend certs are to be ignored
#                                  (i.e. all revisions are `visible'),
#                                  otherwise false if suspend certs are to be
#                                  honoured.
#                  Return Value  : True on success, otherwise false on
#                                  failure.
#
##############################################################################



sub ignore_suspend_certs($$)
{

    my($this, $ignore) = @_;

    # This only works from version 0.37 (i/f version 6.x).

    if ($this->{honour_suspend_certs} && $ignore)
    {
	if (can($this, MTN_IGNORE_SUSPEND_CERTS))
	{
	    $this->{honour_suspend_certs} = 0;
	    closedown($this);
	    startup($this);
	}
	else
	{
	    $this->{error_msg} = "Ignoring suspend certs is unsupported in "
		. "this version of Monotone";
	    &$carper($this->{error_msg});
	    return;
	}
    }
    elsif (! ($this->{honour_suspend_certs} || $ignore))
    {
	$this->{honour_suspend_certs} = 1;
	closedown($this);
	startup($this);
    }

    return 1;

}
#
##############################################################################
#
#   Routine      - register_error_handler
#
#   Description  - Register the specified routine as an error handler for
#                  class. This is a class method rather than an object one as
#                  errors can be raised when calling the constructor.
#
#   Data         - $this        : The object. This may not be present
#                                 depending upon how this method is called and
#                                 is ignored if it is present anyway.
#                  $severity    : The level of error that the handler is being
#                                 registered for. One of "error", "warning" or
#                                 "both".
#                  $handler     : A reference to the error handler routine. If
#                                 this is not provided then the existing error
#                                 handler routine is unregistered and errors
#                                 are handled in the default way.
#                  $client_data : The client data that is to be passed to the
#                                 registered error handler when it is called.
#
##############################################################################



sub register_error_handler($;$$$)
{

    shift() if ($_[0] eq __PACKAGE__ || ref($_[0]) eq __PACKAGE__);
    my($severity, $handler, $client_data) = @_;

    if ($severity eq "error")
    {
	if (defined($handler))
	{
	    $error_handler = $handler;
	    $error_handler_data = $client_data;
	    $croaker = \&error_handler_wrapper;
	}
	else
	{
	    $croaker = \&croak;
	    $error_handler = $error_handler_data = undef;
	}
    }
    elsif ($severity eq "warning")
    {
	if (defined($handler))
	{
	    $warning_handler = $handler;
	    $warning_handler_data = $client_data;
	    $carper = \&warning_handler_wrapper;
	}
	else
	{
	    $carper = sub { return; };
	    $warning_handler = $warning_handler_data = undef;
	}
    }
    elsif ($severity eq "both")
    {
	if (defined($handler))
	{
	    $error_handler = $warning_handler = $handler;
	    $error_handler_data = $warning_handler_data = $client_data;
	    $carper = \&warning_handler_wrapper;
	    $croaker = \&error_handler_wrapper;
	}
	else
	{
	    $warning_handler = $warning_handler_data = undef;
	    $error_handler_data = $warning_handler_data = undef;
	    $carper = sub { return; };
	    $croaker = \&croak;
	}
    }
    else
    {
	croak("Unknown error handler severity `" . $severity . "'");
    }

}
#
##############################################################################
#
#   Routine      - register_db_locked_handler
#
#   Description  - Register the specified routine as a database locked handler
#                  for this class. This is both a class as well as an object
#                  method. When used as a class method, the specified database
#                  locked handler is used as the default handler for all those
#                  objects that do not specify their own handlers.
#
#   Data         - $this        : Either the object, the package name or not
#                                 present depending upon how this method is
#                                 called.
#                  $handler     : A reference to the database locked handler
#                                 routine. If this is not provided then the
#                                 existing database locked handler routine is
#                                 unregistered and database locking clashes
#                                 are handled in the default way.
#                  $client_data : The client data that is to be passed to the
#                                 registered database locked handler when it
#                                 is called.
#
##############################################################################



sub register_db_locked_handler(;$$$)
{

    my $this;
    if (ref($_[0]) eq __PACKAGE__)
    {
	$this = $_[0];
	shift();
    }
    elsif ($_[0] eq __PACKAGE__)
    {
	shift();
    }
    my($handler, $client_data) = @_;

    if (defined($this))
    {
	if (defined($handler))
	{
	    $this->{db_locked_handler} = $handler;
	    $this->{db_locked_handler_data} = $client_data;
	}
	else
	{
	    $this->{db_locked_handler} = $this->{db_locked_handler_data} =
		undef;
	}
    }
    else
    {
	if (defined($handler))
	{
	    $db_locked_handler = $handler;
	    $db_locked_handler_data = $client_data;
	}
	else
	{
	    $db_locked_handler = $db_locked_handler_data = undef;
	}
    }

}
#
##############################################################################
#
#   Routine      - register_io_wait_handler
#
#   Description  - Register the specified routine as an I/O wait handler for
#                  this class. This is both a class as well as an object
#                  method. When used as a class method, the specified I/O wait
#                  handler is used as the default handler for all those
#                  objects that do not specify their own handlers.
#
#   Data         - $this        : Either the object, the package name or not
#                                 present depending upon how this method is
#                                 called.
#                  $handler     : A reference to the I/O wait handler routine.
#                                 If this is not provided then the existing
#                                 I/O wait handler routine is unregistered.
#                  $timeout     : The timeout, in seconds, that this class
#                                 should wait for input before calling the I/O
#                                 wait handler.
#                  $client_data : The client data that is to be passed to the
#                                 registered I/O wait handler when it is
#                                 called.
#
##############################################################################



sub register_io_wait_handler(;$$$$)
{

    my $this;
    if (ref($_[0]) eq __PACKAGE__)
    {
	$this = $_[0];
	shift();
    }
    elsif ($_[0] eq __PACKAGE__)
    {
	shift();
    }
    my($handler, $timeout, $client_data) = @_;

    if (defined($timeout))
    {
	if ($timeout !~ m/^\d*\.{0,1}\d+$/ || $timeout < 0 || $timeout > 20)
	{
	    my $msg =
		"I/O wait handler timeout invalid or out of range, resetting";
	    $this->{error_msg} = $msg if (defined($this));
	    &$carper($msg);
	    $timeout = 1;
	}
    }
    else
    {
	$timeout = 1;
    }

    if (defined($this))
    {
	if (defined($handler))
	{
	    $this->{io_wait_handler} = $handler;
	    $this->{io_wait_handler_data} = $client_data;
	    $this->{io_wait_handler_timeout} = $timeout;
	}
	else
	{
	    $this->{io_wait_handler} = $this->{io_wait_handler_data} = undef;
	}
    }
    else
    {
	if (defined($handler))
	{
	    $io_wait_handler = $handler;
	    $io_wait_handler_data = $client_data;
	    $io_wait_handler_timeout = $timeout;
	}
	else
	{
	    $io_wait_handler = $io_wait_handler_data = undef;
	}
    }

}
#
##############################################################################
#
#   Routine      - get_db_name
#
#   Description  - Return the the file name of the Monotone database as given
#                  to the constructor.
#
#   Data         - $this        : The object.
#                  Return Value : The file name of the database as given to
#                                 the constructor or undef if no database was
#                                 given.
#
##############################################################################



sub get_db_name($)
{

    my $this = $_[0];

    return $this->{db_name};

}
#
##############################################################################
#
#   Routine      - get_error_message
#
#   Description  - Return the message for the last error reported by this
#                  class.
#
#   Data         - $this        : The object.
#                  Return Value : The message for the last error detected, or
#                                 an empty string if nothing has gone wrong
#                                 yet.
#
##############################################################################



sub get_error_message($)
{

    my $this = $_[0];

    return $this->{error_msg};

}
#
##############################################################################
#
#   Routine      - get_pid
#
#   Description  - Return the process id of the mtn automate stdio process.
#
#   Data         - $this        : The object.
#                  Return Value : The process id of the mtn automate stdio
#                                 process, or zero if no process is thought to
#                                 be running.
#
##############################################################################



sub get_pid($)
{

    my $this = $_[0];

    return $this->{mtn_pid};

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

    my $this = $_[0];

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

	    # Make sure that the eval block below does not affect any existing
	    # exception status.

	    {
		local $@;
		eval
		{
		    local $SIG{ALRM} = sub { die("internal sigalarm"); };
		    alarm(5);
		    $ret_val = waitpid($this->{mtn_pid}, 0);
		    alarm(0);
		};
	    }
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
		if ($! != ECHILD)
		{
		    $err_msg = $!;
		    kill("KILL", $this->{mtn_pid});
		    &$croaker("waitpid failed: $err_msg");
		}
	    }
	}
	$this->{poll} = undef;
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

    my($this, $cmd, $ref, @parameters) = @_;

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

    my($this, $cmd, $ref, $options, @parameters) = @_;

    my($buffer,
       $buffer_ref,
       $db_locked_exception,
       $exception,
       $handler,
       $handler_data,
       $in,
       $opt,
       $param,
       $read_ok,
       $retry);

    # Work out what database locked handler is to be used.

    if (defined($this->{db_locked_handler}))
    {
	$handler = $this->{db_locked_handler};
	$handler_data = $this->{db_locked_handler_data};
    }
    else
    {
	$handler = $db_locked_handler;
	$handler_data = $db_locked_handler_data;
    }

    # If the output is to be returned as an array of lines as against one lump
    # then we need to use read the output into a temporary buffer before
    # breaking it up into lines.

    if (ref($ref) eq "SCALAR")
    {
	$buffer_ref = $ref;
    }
    elsif (ref($ref) eq "ARRAY")
    {
	$buffer_ref = \$buffer;
    }
    else
    {
	&$croaker("Expected a reference to a scalar or an array");
    }

    # Send the command, reading its output, repeating if necessary if retries
    # should be attempted when the database is locked.

    do
    {

	# Startup the subordinate mtn process if it hasn't already been
	# started.

	startup($this) if ($this->{mtn_pid} == 0);

	# Send the command.

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

	    # The unless below is required just in case undef is passed as the
	    # only parameter (which can happen when a mandatory argument is not
	    # passed by the caller).

	    printf($in "%d:%s", length($param), $param)
		unless (! defined($param));

	}
	print($in "e\n");

	# Attempt to read the output of the command, rethrowing any exception
	# that does not relate to locked databases.

	$db_locked_exception = $read_ok = $retry = 0;
	eval
	{
	    $read_ok = mtn_read_output($this, $$buffer_ref);
	};
	$exception = $@;
	if ($exception ne "")
	{
	    if ($exception =~ m/$database_locked_re/)
	    {

		# We need to properly closedown the mtn subprocess at this
		# point because we are quietly handling the exception that
		# caused it to exit but the calling application may reap the
		# process and compare the reaped PID with the return value from
		# the get_pid() method. At least by calling closedown() here
		# get_pid() will return 0 and the caller can then distinguish
		# between a handled exit and one that should be dealt with.

		$in = undef;
		closedown($this);
		$db_locked_exception = 1;

	    }
	    else
	    {
		&$croaker($exception);
	    }
	}

	# Deal with locked database exceptions and any warning messages that
	# appeared in the output.

	if (! $read_ok)
	{

	    # See if we are to retry on database locked conditions.

	    $retry = &$handler($this, $handler_data)
		if ($this->{error_msg} =~ m/$database_locked_re/
		    || $db_locked_exception);

	    # If we are to retry then close down the subordinate mtn process,
	    # otherwise report the error to the caller.

	    if ($retry)
	    {
		$in = undef;
		closedown($this);
	    }
	    else
	    {
		&$carper($this->{error_msg});
		return;
	    }
	}

    }
    while ($retry);

    # Split the output up into lines if that is what is required.

    @$ref = split(/\n/, $buffer) if (ref($ref) eq "ARRAY");

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

    my($this, $buffer) = @_;

    my($bytes_read,
       $char,
       $chunk_start,
       $cmd_nr,
       $colons,
       $err,
       $err_code,
       $err_occurred,
       $handler,
       $handler_data,
       $handler_timeout,
       $header,
       $i,
       $last,
       $offset,
       $size);

    $err = $this->{mtn_err};

    # Work out what I/O wait handler is to be used.

    if (defined($this->{io_wait_handler}))
    {
	$handler = $this->{io_wait_handler};
	$handler_data = $this->{io_wait_handler_data};
	$handler_timeout = $this->{io_wait_handler_timeout};
    }
    else
    {
	$handler = $io_wait_handler;
	$handler_data = $io_wait_handler_data;
	$handler_timeout = $io_wait_handler_timeout;
    }

    # Read in the data.

    $$buffer = "";
    $chunk_start = 1;
    $err_occurred = 0;
    $last = "m";
    $offset = 0;
    do
    {

	# Wait here for some data, calling the I/O wait handler every second
	# whilst we wait.

	while ($this->{poll}->poll($handler_timeout) == 0)
	{
	    &$handler($this, $handler_data);
	}

	# If necessary, read in and process the chunk header, then we know how
	# much to read in etc.

	if ($chunk_start)
	{

	    # Read header, one byte at a time until we have what we need or
	    # there is an error.

	    for ($header = "", $colons = $i = 0;
		 $colons < 4 && sysread($this->{mtn_out}, $header, 1, $i);
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
		elsif ($char =~ m/\D$/)
		{
		    croak("Corrupt/missing mtn chunk header, mtn gave:\n"
			  . join("", <$err>));
		}
	    }

	    # Break out the header into its separate fields.

	    if ($header =~ m/^\d+:\d+:[lm]:\d+:$/)
	    {
		($cmd_nr, $err_code, $last, $size) =
		    ($header =~ m/^(\d+):(\d+):([lm]):(\d+):$/);
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
	    if (! defined($bytes_read = sysread($this->{mtn_out},
						$$buffer,
						$size,
						$offset)))
	    {
		croak("sysread failed: $!");
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
	$this->{error_msg} = $$buffer;
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

    my $this = $_[0];

    my(@args,
       $version);

    # Switch to the default locale. We only want to parse the output from
    # Monotone in one language!

    local $ENV{LC_ALL} = "C";
    local $ENV{LANG} = "C";

    if ($this->{mtn_pid} == 0)
    {
	$this->{mtn_err} = gensym();
	@args = ("mtn");
	push(@args, "--db=" . $this->{db_name}) if ($this->{db_name});
	push(@args, "--ignore-suspend-certs")
	    if (! $this->{honour_suspend_certs});
	push(@args, "automate", "stdio");
	$this->{mtn_pid} = open3($this->{mtn_in},
				 $this->{mtn_out},
				 $this->{mtn_err},
				 @args);
	$this->{cmd_cnt} = 0;
	$this->{poll} = IO::Poll->new();
	$this->{poll}->mask($this->{mtn_out} => POLLIN,
			    $this->{mtn_out} => POLLPRI);
	interface_version($this, $version);
	($this->{mtn_aif_major}, $this->{mtn_aif_minor}) =
	    ($version =~ m/^(\d+)\.(\d+)$/);
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
    if ($$buffer !~ m/$closing_quote_re/)
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

    return undef unless (defined($data));

    $data =~ s/\\\\/\\/g;
    $data =~ s/\\\"/\"/g;

    return $data;

}
#
##############################################################################
#
#   Routine      - error_handler_wrapper
#
#   Description  - Error handler routine that wraps the user's error handler.
#                  Essentially this routine simply prepends the severity
#                  parameter and appends the client data parameter.
#
#   Data         - $message : The error message.
#
##############################################################################



sub error_handler_wrapper($)
{

    my $message = $_[0];

    &$error_handler("error", $message, $error_handler_data);
    croak(__PACKAGE__ . ": Fatal error.");

}
#
##############################################################################
#
#   Routine      - warning_handler_wrapper
#
#   Description  - Warning handler routine that wraps the user's warning
#                  handler. Essentially this routine simply prepends the
#                  severity parameter and appends the client data parameter.
#
#   Data         - $message : The error message.
#
##############################################################################



sub warning_handler_wrapper($)
{

    my $message = $_[0];

    &$warning_handler("warning", $message, $warning_handler_data);

}

1;

##############################################################################
#
#   File Name    - AutomateStdio.pm
#
#   Description  - Class module that provides an interface to Monotone's
#                  automate stdio interface.
#
#   Authors      - A.E.Cooper. With contributions from T.Keller.
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
use Cwd qw(abs_path getcwd);
use File::Basename;
use File::Spec;
use IO::File;
use IO::Poll qw(POLLIN POLLPRI);
use IPC::Open3;
use POSIX qw(:errno_h);
use Symbol qw(gensym);

# ***** GLOBAL DATA DECLARATIONS *****

# Constants used to represent the different types of capability Monotone may or
# may not provide depending upon its version.

use constant MTN_DB_GET                           => 0;
use constant MTN_DROP_ATTRIBUTE                   => 1;
use constant MTN_DROP_DB_VARIABLES                => 2;
use constant MTN_FILE_MERGE                       => 3;
use constant MTN_GET_ATTRIBUTES                   => 4;
use constant MTN_GET_CURRENT_REVISION             => 5;
use constant MTN_GET_DB_VARIABLES                 => 6;
use constant MTN_GET_WORKSPACE_ROOT               => 7;
use constant MTN_IGNORE_SUSPEND_CERTS             => 8;
use constant MTN_INVENTORY_IN_IO_STANZA_FORMAT    => 9;
use constant MTN_INVENTORY_INCLUDE_BIRTH_ID       => 10;
use constant MTN_INVENTORY_TAKE_OPTIONS           => 11;
use constant MTN_LUA                              => 12;
use constant MTN_READ_PACKETS                     => 13;
use constant MTN_SET_ATTRIBUTE                    => 14;
use constant MTN_SET_DB_VARIABLE                  => 15;
use constant MTN_SHOW_CONFLICTS                   => 16;
use constant MTN_USE_P_SELECTOR                   => 17;

# Constants used to represent the different error levels.

use constant MTN_SEVERITY_ALL     => 0x03;
use constant MTN_SEVERITY_ERROR   => 0x01;
use constant MTN_SEVERITY_WARNING => 0x02;

# Constants used to represent different value formats.

use constant BARE_PHRASE     => 0x01;  # E.g. orphaned_directory.
use constant HEX_ID          => 0x02;  # E.g. [ab2 ... 1be].
use constant NULL            => 0x04;  # Nothing, i.e. we just have the key.
use constant OPTIONAL_HEX_ID => 0x08;  # As HEX_ID but also [].
use constant STRING          => 0x10;  # Any quoted string, possibly escaped.
use constant STRING_ENUM     => 0x20;  # E.g. "rename_source".
use constant STRING_LIST     => 0x40;  # E.g. "..." "...", possibly escaped.

# Pre-compiled regular expressions for: finding the end of a quoted string
# possibly containing escaped quotes (i.e. " preceeded by a non-backslash
# character or an even number of backslash characters), recognising data locked
# conditions and detecting the beginning of an I/O stanza.

my $closing_quote_re = qr/((^.*[^\\])|^)(\\{2})*\"$/;
my $database_locked_re = qr/.*sqlite error: database is locked.*/;
my $io_stanza_re = qr/^ *([a-z_]+)(?:(?: \S)|(?: ?$))/;

# A map for quickly detecting valid mtn subprocess options and the number of
# their arguments.

my %valid_mtn_options = ("--confdir"            => 1,
			 "--key"                => 1,
			 "--keydir"             => 1,
			 "--no-default-confdir" => 0,
			 "--no-workspace"       => 0,
			 "--norc"               => 0,
			 "--nostd"              => 0,
			 "--root"               => 1,
			 "--ssh-sign"           => 1);

# Maps for quickly detecting valid keys and determining their value types.

my %certs_keys = ("key"       => STRING,
		  "name"      => STRING,
		  "signature" => STRING,
		  "trust"     => STRING_ENUM,
		  "value"     => STRING);
my %genkey_keys = ("name"             => STRING,
		   "public_hash"      => HEX_ID,
		   "private_hash"     => HEX_ID,
		   "public_location"  => STRING_LIST,
		   "private_location" => STRING_LIST);
my %get_attributes_keys = ("attr"           => STRING_LIST,
			   "format_version" => STRING_ENUM,
			   "state"          => STRING_ENUM);
my %get_db_variables_keys = ("domain" => STRING,
			     "entry"  => STRING_LIST);
my %inventory_keys = ("birth"    => HEX_ID,
		      "changes"  => STRING_LIST,
		      "fs_type"  => STRING_ENUM,
		      "new_path" => STRING,
		      "new_type" => STRING_ENUM,
		      "old_path" => STRING,
		      "old_type" => STRING_ENUM,
		      "path"     => STRING,
		      "status"   => STRING_LIST);
my %keys_keys = %genkey_keys;
my %options_file_keys = ("branch"   => STRING,
			 "database" => STRING,
			 "keydir"   => STRING);
my %revision_details_keys = ("add_dir"        => STRING,
			     "add_file"       => STRING,
			     "attr"           => STRING,
			     "clear"          => STRING,
			     "content"        => HEX_ID,
			     "delete"         => STRING,
			     "format_version" => STRING_ENUM,
			     "from"           => HEX_ID,
			     "new_manifest"   => HEX_ID,
			     "old_revision"   => OPTIONAL_HEX_ID,
			     "patch"          => STRING,
			     "rename"         => STRING,
			     "set"            => STRING,
			     "to"             => HEX_ID | STRING,
			     "value"          => STRING);
my %show_conflicts_keys = ("ancestor"          => OPTIONAL_HEX_ID,
			   "ancestor_file_id"  => HEX_ID,
			   "ancestor_name"     => STRING,
			   "attr_name"         => STRING,
			   "conflict"          => BARE_PHRASE,
			   "left"              => HEX_ID,
			   "left_attr_value"   => STRING,
			   "left_file_id"      => HEX_ID,
			   "left_name"         => STRING,
			   "left_type"         => STRING,
			   "node_type"         => STRING,
			   "resolved_internal" => NULL,
			   "right"             => HEX_ID,
			   "right_attr_state"  => STRING,
			   "right_attr_value"  => STRING,
			   "right_file_id"     => HEX_ID,
			   "right_name"        => STRING,
			   "right_type"        => STRING);
my %tags_keys = ("branches"       => NULL | STRING_LIST,
		 "format_version" => STRING_ENUM,
		 "revision"       => HEX_ID,
		 "signer"         => STRING,
		 "tag"            => STRING);

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

sub ancestors($$@);
sub ancestry_difference($$$;@);
sub branches($$);
sub can($$);
sub cert($$$$);
sub certs($$$);
sub children($$$);
sub closedown($);
sub common_ancestors($$@);
sub content_diff($$;$$$@);
sub db_get($$$$);
sub db_locked_condition_detected($);
sub descendents($$@);
sub drop_attribute($$$);
sub drop_db_variables($$;$);
sub erase_ancestors($$;@);
sub file_merge($$$$$$);
sub genkey($$$$);
sub get_attributes($$$);
sub get_base_revision_id($$);
sub get_content_changed($$$$);
sub get_corresponding_path($$$$$);
sub get_current_revision($$;$@);
sub get_current_revision_id($$);
sub get_db_name($);
sub get_db_variables($$;$);
sub get_error_message($);
sub get_file($$$);
sub get_file_of($$$;$);
sub get_manifest_of($$;$);
sub get_option($$$);
sub get_pid($);
sub get_revision($$$);
sub get_workspace_root($$);
sub get_ws_path($);
sub graph($$);
sub heads($$;$);
sub identify($$$);
sub ignore_suspend_certs($$);
sub interface_version($$);
sub inventory($$;$@);
sub keys($$);
sub leaves($$);
sub lua($$$;@);
sub new_from_db($;$$);
sub new_from_ws($;$$);
sub packet_for_fdata($$$);
sub packet_for_fdelta($$$$);
sub packet_for_rdata($$$);
sub packets_for_certs($$$);
sub parents($$$);
sub put_file($$$$);
sub put_revision($$$);
sub read_packets($$);
sub register_db_locked_handler(;$$$);
sub register_error_handler($;$$$);
sub register_io_wait_handler(;$$$$);
sub roots($$);
sub select($$$);
sub set_attribute($$$$);
sub set_db_variable($$$$);
sub show_conflicts($$;$$$);
sub tags($$;$);
sub toposort($$@);

# Public aliased methods.

*attributes = *get_attributes;
*new = *new_from_db;
*db_set = *set_db_variable;

# Private methods and routines.

sub create_object_data();
sub error_handler_wrapper($);
sub get_quoted_value($$$);
sub get_ws_details($$$);
sub mtn_command($$$;@);
sub mtn_command_with_options($$$$;@);
sub mtn_read_output($$);
sub parse_kv_record($$$$;$);
sub parse_revision_data($$);
sub startup($);
sub unescape($);
sub validate_database($);
sub validate_mtn_options($);
sub warning_handler_wrapper($);

# ***** PACKAGE INFORMATION *****

# We are just a base class.

use base qw(Exporter);

our %EXPORT_TAGS = (capabilities => [qw(MTN_DB_GET
					MTN_DROP_ATTRIBUTE
					MTN_DROP_DB_VARIABLES
					MTN_FILE_MERGE
					MTN_GET_ATTRIBUTES
					MTN_GET_CURRENT_REVISION
					MTN_GET_DB_VARIABLES
					MTN_GET_WORKSPACE_ROOT
					MTN_IGNORE_SUSPEND_CERTS
					MTN_INVENTORY_IN_IO_STANZA_FORMAT
					MTN_INVENTORY_INCLUDE_BIRTH_ID
					MTN_INVENTORY_TAKE_OPTIONS
					MTN_LUA
					MTN_READ_PACKETS
					MTN_SET_ATTRIBUTE
					MTN_SET_DB_VARIABLE
					MTN_SHOW_CONFLICTS
					MTN_USE_P_SELECTOR)],
		    severities	 => [qw(MTN_SEVERITY_ALL
					MTN_SEVERITY_ERROR
					MTN_SEVERITY_WARNING)]);
our @EXPORT = qw();
Exporter::export_ok_tags(qw(capabilities severities));
our $VERSION = 0.2.0;
#
##############################################################################
#
#   Routine      - new_from_db
#
#   Description  - Class constructor. Construct an object using the specified
#                  Monotone database.
#
#   Data         - $class       : Either the name of the class that is to be
#                                 created or an object of that class.
#                  $db_name     : The full path of the Monotone database. If
#                                 this is not provided then the database
#                                 associated with the current workspace is
#                                 used.
#                  $options     : A reference to a list containing a list of
#                                 options to use on the mtn subprocess.
#                  Return Value : A reference to the newly created object.
#
##############################################################################



sub new_from_db($;$$)
{


    my $class = (ref($_[0]) ne "") ? ref($_[0]) : $_[0];
    shift();
    my $db_name = (ref($_[0]) eq "ARRAY") ? undef : shift();
    my $options = shift();
    $options = [] if (! defined($options));

    my($db,
       $this,
       $ws_path);

    # Check all the arguments given to us.

    validate_mtn_options($options);
    if (defined($db_name))
    {
	$db = $db_name;
    }
    else
    {
	get_ws_details(getcwd(), \$db, \$ws_path);
    }
    validate_database($db);

    # Actually construct the object.

    $this = create_object_data();
    $this->{db_name} = $db_name;
    $this->{ws_path} = $ws_path;
    $this->{mtn_options} = $options;
    bless($this, $class);

    # Startup the mtn subprocess (also determining the interface version).

    startup($this);

    return $this;

}
#
##############################################################################
#
#   Routine      - new_from_ws
#
#   Description  - Class constructor. Construct an object using the specified
#                  Monotone workspace.
#
#   Data         - $class       : Either the name of the class that is to be
#                                 created or an object of that class.
#                  $ws_path     : The base directory of a Monotone workspace.
#                                 If this is not provided then the current
#                                 workspace is used.
#                  $options     : A reference to a list containing a list of
#                                 options to use on the mtn subprocess.
#                  Return Value : A reference to the newly created object.
#
##############################################################################



sub new_from_ws($;$$)
{


    my $class = (ref($_[0]) ne "") ? ref($_[0]) : $_[0];
    shift();
    my $ws_path = (ref($_[0]) eq "ARRAY") ? undef : shift();
    my $options = shift();
    $options = [] if (! defined($options));

    my($db_name,
       $this);

    # Check all the arguments given to us.

    validate_mtn_options($options);
    if (! defined($ws_path))
    {
	$ws_path = getcwd();
    }
    get_ws_details($ws_path, \$db_name, \$ws_path);
    validate_database($db_name);

    # Actually construct the object.

    $this = create_object_data();
    $this->{ws_path} = $ws_path;
    $this->{mtn_options} = $options;
    bless($this, $class);

    # Startup the mtn subprocess (also determining the interface version).

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

    my $this = $_[0];

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
#                  $list         : A reference to a list that is to contain
#                                  the revision ids.
#                  @revision_ids : The revision ids that are to have their
#                                  ancestors returned.
#                  Return Value  : True on success, otherwise false on
#                                  failure.
#
##############################################################################



sub ancestors($$@)
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
#                  $list             : A reference to a list that is to
#                                      contain the revision ids.
#                  $new_revision_id  : The revision id that is to have its
#                                      ancestors returned.
#                  @old_revision_ids : The revision ids that are to have their
#                                      ancestors excluded from the above list.
#                  Return Value      : True on success, otherwise false on
#                                      failure.
#
##############################################################################



sub ancestry_difference($$$;@)
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
#                  $list        : A reference to a list that is to contain the
#                                 branch names.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub branches($$)
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

    my $dummy;

    return mtn_command($this, "cert", \$dummy, $revision_id, $name, $value);

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
	   @lines);

	if (! mtn_command($this, "certs", \@lines, $revision_id))
	{
	    return;
	}

	# Reformat the data into a structured array.

	for ($i = 0, @$ref = (); $i < scalar(@lines); ++ $i)
	{
	    if ($lines[$i] =~ m/$io_stanza_re/)
	    {
		my $kv_record;

		# Get the next key-value record.

		parse_kv_record(\@lines, \$i, \%certs_keys, \$kv_record);
		-- $i;

		# Validate it in terms of expected fields and store.

		foreach my $key ("key", "name", "signature", "trust", "value")
		{
		    &$croaker("Corrupt certs list, expected " . $key
			      . " field but didn't find it")
			unless (exists($kv_record->{$key}));
		}
		push(@$ref, $kv_record);
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
#                  $list        : A reference to a list that is to contain the
#                                 revision ids.
#                  $revision_id : The revision id that is to have its children
#                                 returned.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub children($$$)
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
#                  $list         : A reference to a list that is to contain
#                                  the revision ids.
#                  @revision_ids : The revision ids that are to have their
#                                  common ancestors returned.
#                  Return Value  : True on success, otherwise false on
#                                  failure.
#
##############################################################################



sub common_ancestors($$@)
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
#                  optionally limiting the output by using the specified
#                  options and file restrictions. If the second revision id is
#                  undefined then the workspace's current revision is used. If
#                  both revision ids are undefined then the workspace's
#                  current and base revisions are used. If no file names are
#                  listed then differences in all files are reported.
#
#   Data         - $this         : The object.
#                  $buffer       : A reference to a buffer that is to contain
#                                  the output from this command.
#                  $options      : A reference to a list containing the
#                                  options to use.
#                  $revision_id1 : The first revision id to compare against.
#                  $revision_id2 : The second revision id to compare against.
#                  @file_names   : The list of file names that are to be
#                                  reported on.
#                  Return Value  : True on success, otherwise false on
#                                  failure.
#
##############################################################################



sub content_diff($$;$$$@)
{

    my($this, $buffer, $options, $revision_id1, $revision_id2, @file_names)
	= @_;

    my @opts;

    # Process any options.

    if (defined($options))
    {
	for (my $i = 0; $i < scalar(@$options); ++ $i)
	{
	    push(@opts, {key => $$options[$i], value => $$options[++ $i]});
	}
    }
    push(@opts, {key => "r", value => $revision_id1})
	unless (! defined($revision_id1));
    push(@opts, {key => "r", value => $revision_id2})
	unless (! defined($revision_id2));

    return mtn_command_with_options($this,
				    "content_diff",
				    $buffer,
				    \@opts,
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
#                  $buffer      : A reference to a buffer that is to contain
#                                 the output from this command.
#                  $domain      : The domain of the database variable.
#                  $name        : The name of the variable to fetch.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub db_get($$$$)
{

    my($this, $buffer, $domain, $name) = @_;

    return mtn_command($this, "db_get", $buffer, $domain, $name);

}
#
##############################################################################
#
#   Routine      - descendents
#
#   Description  - Get a list of descendents for the specified revisions.
#
#   Data         - $this         : The object.
#                  $list         : A reference to a list that is to contain
#                                  the revision ids.
#                  @revision_ids : The revision ids that are to have their
#                                  descendents returned.
#                  Return Value  : True on success, otherwise false on
#                                  failure.
#
##############################################################################



sub descendents($$@)
{

    my($this, $list, @revision_ids) = @_;

    return mtn_command($this, "descendents", $list, @revision_ids);

}
#
##############################################################################
#
#   Routine      - drop_attribute
#
#   Description  - Drop attributes from the specified file or directory,
#                  optionally limiting it to the specified attribute.
#
#   Data         - $this        : The object.
#                  $path        : The name of the file or directory that is to
#                                 have an attribute dropped.
#                  $key         : The name of the attribute that as to be
#                                 dropped.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub drop_attribute($$$)
{

    my($this, $path, $key) = @_;

    my $dummy;

    return mtn_command($this, "drop_attribute", \$dummy, $path, $key);

}
#
##############################################################################
#
#   Routine      - drop_db_variables
#
#   Description  - Drop variables from the specified domain, optionally
#                  limiting it to the specified variable.
#
#   Data         - $this        : The object.
#                  $domain      : The name of the domain that is to have one
#                                 or all of its variables dropped.
#                  $name        : The name of the variable that is to be
#                                 dropped.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub drop_db_variables($$;$)
{

    my($this, $domain, $name) = @_;

    my $dummy;

    return mtn_command($this, "drop_db_variables", \$dummy, $domain, $name);

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
#                  $list         : A reference to a list that is to contain
#                                  the revision ids.
#                  @revision_ids : The revision ids that are to have their
#                                  descendents returned.
#                  Return Value  : True on success, otherwise false on
#                                  failure.
#
##############################################################################



sub erase_ancestors($$;@)
{

    my($this, $list, @revision_ids) = @_;

    return mtn_command($this, "erase_ancestors", $list, @revision_ids);

}
#
##############################################################################
#
#   Routine      - file_merge
#
#   Description  - Get the result of merging two files, both of which are on
#                  separate revisions.
#
#   Data         - $this              : The object.
#                  $buffer            : A reference to a buffer that is to
#                                       contain the output from this command.
#                  $left_revision_id  : The left hand revision id.
#                  $left_file_name    : The name of the file on the left hand
#                                       revision.
#                  $right_revision_id : The right hand revision id.
#                  $right_file_name   : The name of the file on the right hand
#                                       revision.
#                  Return Value       : True on success, otherwise false on
#                                       failure.
#
##############################################################################



sub file_merge($$$$$$)
{

    my($this,
       $buffer,
       $left_revision_id,
       $left_file_name,
       $right_revision_id,
       $right_file_name) = @_;

    return mtn_command($this,
		       "file_merge",
		       $buffer,
		       $left_revision_id,
		       $left_file_name,
		       $right_revision_id,
		       $right_file_name);

}
#
##############################################################################
#
#   Routine      - genkey
#
#   Description  - Generate a new key for use within the database.
#
#   Data         - $this        : The object.
#                  $ref         : A reference to a buffer or a hash that is to
#                                 contain the output from this command.
#                  $key_id      : The key id for the new key.
#                  $pass_phrase : The pass phrase for the key.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub genkey($$$$)
{

    my($this, $ref, $key_id, $pass_phrase) = @_;

    # Run the command and get the data, either as one lump or as a structured
    # list.

    if (ref($ref) eq "SCALAR")
    {
	return mtn_command($this, "genkey", $ref, $key_id, $pass_phrase);
    }
    else
    {

	my($i,
	   $kv_record,
	   @lines);

	if (! mtn_command($this, "genkey", \@lines, $key_id, $pass_phrase))
	{
	    return;
	}

	# Reformat the data into a structured record.

	# Get the key-value record.

	$i = 0;
	parse_kv_record(\@lines, \$i, \%genkey_keys, \$kv_record);

	# Copy across the fields.

	%$ref = ();
	foreach my $key (CORE::keys(%$kv_record))
	{
	    $$ref{$key} = $kv_record->{$key};
	}

	return 1;

    }

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



sub get_attributes($$$)
{

    my($this, $ref, $file_name) = @_;

    my $cmd;

    # This command was renamed in version 0.36 (i/f version 5.x).

    if (can($this, MTN_GET_ATTRIBUTES))
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
	   @lines);

	if (! mtn_command($this, $cmd, \@lines, $file_name))
	{
	    return;
	}

	# Reformat the data into a structured array.

	for ($i = 0, @$ref = (); $i < scalar(@lines); ++ $i)
	{
	    if ($lines[$i] =~ m/$io_stanza_re/)
	    {
		my $kv_record;

		# Get the next key-value record.

		parse_kv_record(\@lines,
				\$i,
				\%get_attributes_keys,
				\$kv_record);
		-- $i;

		# Validate it in terms of expected fields and store.

		if (exists($kv_record->{attr}))
		{
		    &$croaker("Corrupt attributes list, expected state field "
			      . "but didn't find it")
			unless (exists($kv_record->{state}));
		    push(@$ref, {attribute => $kv_record->{attr}->[0],
				 value     => $kv_record->{attr}->[1],
				 state     => $kv_record->{state}});
		}
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
#                  $buffer      : A reference to a buffer that is to contain
#                                 the output from this command.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub get_base_revision_id($$)
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
#                  $list        : A reference to a list that is to contain the
#                                 revision ids.
#                  $revision_id : The id of the revision of the manifest that
#                                 is to be returned.
#                  $file_name   : The name of the file that is to be reported
#                                 on.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub get_content_changed($$$$)
{

    my($this, $list, $revision_id, $file_name) = @_;

    my($i,
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

    for ($i = 0, @$list = (); $i < scalar(@lines); ++ $i)
    {
	if ($lines[$i] =~ m/^ *content_mark \[([0-9a-f]+)\]$/)
	{
	    push(@$list, $1);
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
#                  $buffer             : A reference to a buffer that is to
#                                        contain the output from this command.
#                  $source_revision_id : The source revision id.
#                  $file_name          : The name of the file that is to be
#                                        searched for.
#                  $target_revision_id : The target revision id.
#                  Return Value        : True on success, otherwise false on
#                                        failure.
#
##############################################################################



sub get_corresponding_path($$$$$)
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

    for ($i = 0, $$buffer = ""; $i < scalar(@lines); ++ $i)
    {
	if ($lines[$i] =~ m/^ *file \"/)
	{
	    get_quoted_value(\@lines, \$i, $buffer);
	    $$buffer = unescape($$buffer);
	}
    }

    return 1;

}
#
##############################################################################
#
#   Routine      - get_current_revision
#
#   Description  - Get the revision information for the current revision,
#                  optionally limiting the output by using the specified
#                  options and file restrictions.
#
#   Data         - $this        : The object.
#                  $ref         : A reference to a buffer or an array that is
#                                 to contain the output from this command.
#                  $options     : A reference to a list containing the options
#                                 to use.
#                  @paths       : A list of files or directories that are to
#                                 be reported on instead of the entire
#                                 workspace.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub get_current_revision($$;$@)
{

    my($this, $ref, $options, @paths) = @_;

    my($i,
       @opts);

    # Process any options.

    if (defined($options))
    {
	for ($i = 0; $i < scalar(@$options); ++ $i)
	{
	    if ($$options[$i] eq "depth" || $$options[$i] eq "exclude")
	    {
		push(@opts, {key => $$options[$i], value => $$options[++ $i]});
	    }
	    else
	    {
		push(@opts, {key => $$options[$i], value => ""});
	    }
	}
    }

    # Run the command and get the data, either as one lump or as a structured
    # list.

    if (ref($ref) eq "SCALAR")
    {
	return mtn_command_with_options($this,
					"get_current_revision",
					$ref,
					\@opts,
					@paths);
    }
    else
    {

	my @lines;

	if (! mtn_command_with_options($this,
				       "get_current_revision",
				       \@lines,
				       \@opts,
				       @paths))
	{
	    return;
	}
	parse_revision_data($ref, \@lines);

	return 1;

    }

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
#                  $buffer      : A reference to a buffer that is to contain
#                                 the output from this command.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub get_current_revision_id($$)
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
#   Routine      - get_db_variables
#
#   Description  - Get the variables stored in the database, optionally
#                  limiting it to the specified domain.
#
#   Data         - $this        : The object.
#                  $ref         : A reference to a buffer or an array that is
#                                 to contain the output from this command.
#                  $domain      : The name of the domain that is to have its
#                                 variables listed.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub get_db_variables($$;$)
{

    my($this, $ref, $domain) = @_;

    # Run the command and get the data, either as one lump or as a structured
    # list.

    if (ref($ref) eq "SCALAR")
    {
	return mtn_command($this, "get_db_variables", $ref, $domain);
    }
    else
    {

	my($domain_name,
	   $i,
	   @lines,
	   $name,
	   $value);

	if (! mtn_command($this, "get_db_variables", \@lines, $domain))
	{
	    return;
	}

	# Reformat the data into a structured array. We cannot use
	# parse_kv_record here as we can have multiple `entry' fields in each
	# record block.

	for ($i = 0, @$ref = (); $i < scalar(@lines); ++ $i)
	{
	    if ($lines[$i] =~ m/^ *domain \"/)
	    {
		get_quoted_value(\@lines, \$i, \$domain_name);
	    }
	    if ($lines[$i] =~ m/^ *entry \"(.+)\"$/)
	    {
		($name, $value) = split(/\" \"/, $1);
		if (defined($domain_name))
		{
		    push(@$ref, {domain => unescape($domain_name),
				 name   => unescape($name),
				 value  => unescape($value)});
		}
		else
		{
		    &$croaker("Corrupt variables list, expected domain field "
			      . "but didn't find it");
		}
	    }
	}

	return 1;

    }

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
#                  $buffer      : A reference to a buffer that is to contain
#                                 the output from this command.
#                  $file_id     : The file id of the file that is to be
#                                 returned.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub get_file($$$)
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
#                  $buffer      : A reference to a buffer that is to contain
#                                 the output from this command.
#                  $file_name   : The name of the file to be fetched.
#                  $revision_id : The revision id upon which the file contents
#                                 are to be based.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub get_file_of($$$;$)
{

    my($this, $buffer, $file_name, $revision_id) = @_;

    my @opts;

    push(@opts, {key => "r", value => $revision_id})
	unless (! defined($revision_id));

    return mtn_command_with_options($this,
				    "get_file_of",
				    $buffer,
				    \@opts,
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

	my($attrs,
	   $i,
	   $id,
	   $key,
	   @lines,
	   $name,
	   $type,
	   $value);

	if (! mtn_command($this, "get_manifest_of", \@lines, $revision_id))
	{
	    return;
	}

	# Reformat the data into a structured array. We cannot use
	# parse_kv_record here as we can have multiple `attr' fields in each
	# record block.

	for ($i = 0, @$ref = (); $i < scalar(@lines); ++ $i)
	{
	    $type = undef;
	    if ($lines[$i] =~ m/^ *file \"/)
	    {
		$type = "file";
		get_quoted_value(\@lines, \$i, \$name);
		if ($lines[++ $i] =~ m/^ *content \[([0-9a-f]+)\]$/)
		{
		    $id = $1;
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
		get_quoted_value(\@lines, \$i, \$name);
	    }
	    for ($attrs = [];
		 ($i + 1) < scalar(@lines)
		     && $lines[$i + 1] =~ m/^ *attr \"(.+)\"$/;)
	    {
		++ $i;
		($key, $value) = split(/\" \"/, $1);
		push(@$attrs, {attribute => unescape($key),
			       value     => unescape($value)});
	    }
	    if (defined($type))
	    {
		if ($type eq "file")
		{
		    push(@$ref, {type       => $type,
				 name       => unescape($name),
				 file_id    => $id,
				 attributes => $attrs});
		}
		else
		{
		    push(@$ref, {type       => $type,
				 name       => unescape($name),
				 attributes => $attrs});
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
#                  $buffer      : A reference to a buffer that is to contain
#                                 the output from this command.
#                  $option_name : The name of the option to be fetched.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub get_option($$$)
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



sub get_revision($$$)
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

	my @lines;

	if (! mtn_command($this, "get_revision", \@lines, $revision_id))
	{
	    return;
	}
	parse_revision_data($ref, \@lines);

	return 1;

    }

}
#
##############################################################################
#
#   Routine      - get_workspace_root
#
#   Description  - Get the absolute path for the current workspace's root
#                  directory.
#
#   Data         - $this        : The object.
#                  $buffer      : A reference to a buffer that is to contain
#                                 the output from this command.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub get_workspace_root($$)
{

    my($this, $buffer) = @_;

    if (! mtn_command($this, "get_workspace_root", $buffer))
    {
	return;
    }
    chomp($$buffer);

    return 1;

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
	   @parent_ids);

	if (! mtn_command($this, "graph", \@lines))
	{
	    return;
	}
	for ($i = 0, @$ref = (); $i < scalar(@lines); ++ $i)
	{
	    @parent_ids = split(/ /, $lines[$i]);
	    $$ref[$i] = {revision_id => shift(@parent_ids),
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
#                  $list        : A reference to a list that is to contain the
#                                 revision ids.
#                  $branch_name : The name of the branch that is to have its
#                                 heads returned.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub heads($$;$)
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
#                  $buffer      : A reference to a buffer that is to contain
#                                 the output from this command.
#                  $file_name   : The name of the file that is to have its id
#                                 returned.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub identify($$$)
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
#                  $buffer      : A reference to a buffer that is to contain
#                                 the output from this command.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub interface_version($$)
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
#   Description  - Get the inventory for the current workspace, optionally
#                  limiting the output by using the specified options and file
#                  restrictions.
#
#   Data         - $this        : The object.
#                  $ref         : A reference to a buffer or an array that is
#                                 to contain the output from this command.
#                  $options     : A reference to a list containing the options
#                                 to use.
#                  @paths       : A list of files or directories that are to
#                                 be reported on instead of the entire
#                                 workspace.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub inventory($$;$@)
{

    my($this, $ref, $options, @paths) = @_;

    my @opts;

    # Process any options.

    if (defined($options))
    {
	for (my $i = 0; $i < scalar(@$options); ++ $i)
	{
	    if ($$options[$i] eq "depth" || $$options[$i] eq "exclude")
	    {
		push(@opts, {key => $$options[$i], value => $$options[++ $i]});
	    }
	    else
	    {
		push(@opts, {key => $$options[$i], value => ""});
	    }
	}
    }

    # Run the command and get the data, either as one lump or as a structured
    # list.

    if (ref($ref) eq "SCALAR")
    {
	return mtn_command_with_options($this,
					"inventory",
					$ref,
					\@opts,
					@paths);
    }
    else
    {

	my @lines;

	if (! mtn_command_with_options($this,
				       "inventory",
				       \@lines,
				       \@opts,
				       @paths))
	{
	    return;
	}

	# The output format of this command was switched over to a basic_io
	# stanza in 0.37 (i/f version 6.x).

	if (can($this, MTN_INVENTORY_IN_IO_STANZA_FORMAT))
	{

	    my $i;

	    # Reformat the data into a structured array.

	    for ($i = 0, @$ref = (); $i < scalar(@lines); ++ $i)
	    {
		if ($lines[$i] =~ m/$io_stanza_re/)
		{
		    my $kv_record;

		    # Get the next key-value record and store it in the list.

		    parse_kv_record(\@lines,
				    \$i,
				    \%inventory_keys,
				    \$kv_record);
		    -- $i;
		    if (exists($kv_record->{birth}))
		    {
			$kv_record->{birth_id} = $kv_record->{birth};
			delete($kv_record->{birth});
		    }
		    push(@$ref, $kv_record);
		}
	    }
	}
	else
	{

	    my $i;

	    # Reformat the data into a structured array.

	    for ($i = 0, @$ref = (); $i < scalar(@lines); ++ $i)
	    {
		if ($lines[$i] =~ m/^([A-Z ]{3}) (\d+) (\d+) (.+)$/)
		{
		    push(@$ref, {status       => $1,
				 crossref_one => $2,
				 crossref_two => $3,
				 name         => $4});
		}
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
	   @lines);

	if (! mtn_command($this, "keys", \@lines))
	{
	    return;
	}

	# Reformat the data into a structured array.

	for ($i = 0, @$ref = (); $i < scalar(@lines); ++ $i)
	{
	    if ($lines[$i] =~ m/$io_stanza_re/)
	    {
		my $kv_record;

		# Get the next key-value record.

		parse_kv_record(\@lines, \$i, \%keys_keys, \$kv_record);
		-- $i;

		# Validate it in terms of expected fields and store.

		foreach my $key ("name", "public_hash", "public_location")
		{
		    &$croaker("Corrupt keys list, expected " . $key
			      . " field but didn't find it")
			unless (exists($kv_record->{$key}));
		}
		if (exists($kv_record->{private_hash}))
		{
		    $kv_record->{type} = "public-private";
		}
		else
		{
		    $kv_record->{type} = "public";
		}
		push(@$ref, $kv_record);
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
#                  $list        : A reference to a list that is to contain the
#                                 revision ids.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub leaves($$)
{

    my($this, $list) = @_;

    return mtn_command($this, "leaves", $list);

}
#
##############################################################################
#
#   Routine      - lua
#
#   Description  - Call the specified LUA function with any required
#                  arguments.
#
#   Data         - $this         : The object.
#                  $buffer       : A reference to a buffer that is to contain
#                                  the output from this command.
#                  $lua_function : The name of the LUA function that is to be
#                                  called.
#                  @arguments    : A list of arguments that are to be passed
#                                  to the LUA function.
#                  Return Value  : True on success, otherwise false on
#                                  failure.
#
##############################################################################



sub lua($$$;@)
{

    my($this, $buffer, $lua_function, @arguments) = @_;

    return mtn_command($this, "lua", $buffer, $lua_function, @arguments);

}
#
##############################################################################
#
#   Routine      - packet_for_fdata
#
#   Description  - Get the contents of the file referenced by the specified
#                  file id in packet format.
#
#   Data         - $this        : The object.
#                  $buffer      : A reference to a buffer that is to contain
#                                 the output from this command.
#                  $file_id     : The file id of the file that is to be
#                                 returned.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub packet_for_fdata($$$)
{

    my($this, $buffer, $file_id) = @_;

    return mtn_command($this, "packet_for_fdata", $buffer, $file_id);

}
#
##############################################################################
#
#   Routine      - packet_for_fdelta
#
#   Description  - Get the file delta between the two files referenced by the
#                  specified file ids in packet format.
#
#   Data         - $this         : The object.
#                  $buffer       : A reference to a buffer that is to contain
#                                  the output from this command.
#                  $from_file_id : The file id of the file that is to be used
#                                  as the base in the delta operation.
#                  $to_file_id   : The file id of the file that is to be used
#                                  as the target in the delta operation.
#                  Return Value  : True on success, otherwise false on
#                                  failure.
#
##############################################################################



sub packet_for_fdelta($$$$)
{

    my($this, $buffer, $from_file_id, $to_file_id) = @_;

    return mtn_command
	($this, "packet_for_fdelta", $buffer, $from_file_id, $to_file_id);

}
#
##############################################################################
#
#   Routine      - packet_for_rdata
#
#   Description  - Get the contents of the revision referenced by the
#                  specified revision id in packet format.
#
#   Data         - $this        : The object.
#                  $buffer      : A reference to a buffer that is to contain
#                                 the output from this command.
#                  $revision_id : The revision id of the revision that is to
#                                 be returned.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub packet_for_rdata($$$)
{

    my($this, $buffer, $revision_id) = @_;

    return mtn_command($this, "packet_for_rdata", $buffer, $revision_id);

}
#
##############################################################################
#
#   Routine      - packets_for_certs
#
#   Description  - Get all the certs for the revision referenced by the
#                  specified revision id in packet format.
#
#   Data         - $this        : The object.
#                  $buffer      : A reference to a buffer that is to contain
#                                 the output from this command.
#                  $revision_id : The revision id of the revision that is to
#                                 have its certs returned.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub packets_for_certs($$$)
{

    my($this, $buffer, $revision_id) = @_;

    return mtn_command($this, "packets_for_certs", $buffer, $revision_id);

}
#
##############################################################################
#
#   Routine      - parents
#
#   Description  - Get a list of parents for the specified revision.
#
#   Data         - $this        : The object.
#                  $list        : A reference to a list that is to contain the
#                                 revision ids.
#                  $revision_id : The revision id that is to have its parents
#                                 returned.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub parents($$$)
{

    my($this, $list, $revision_id) = @_;

    return mtn_command($this, "parents", $list, $revision_id);

}
#
##############################################################################
#
#   Routine      - put_file
#
#   Description  - Put the specified file contents into the database,
#                  optionally basing it on the specified file id (this is used
#                  for delta encoding).
#
#   Data         - $this         : The object.
#                  $buffer       : A reference to a buffer that is to contain
#                                  the output from this command.
#                  $base_file_id : The file id of the previous version of this
#                                  file or undef if this is a new file.
#                  $contents     : A reference to a buffer containing the
#                                  file's contents.
#                  Return Value  : True on success, otherwise false on
#                                  failure.
#
##############################################################################



sub put_file($$$$)
{

    my($this, $buffer, $base_file_id, $contents) = @_;

    my @list;

    if (defined($base_file_id))
    {
	if (! mtn_command($this,
			  "put_file",
			  \@list,
			  $base_file_id,
			  $contents))
	{
	    return;
	}
    }
    else
    {
	if (! mtn_command($this, "put_file", \@list, $contents))
	{
	    return;
	}
    }
    $$buffer = $list[0];

    return 1;

}
#
##############################################################################
#
#   Routine      - put_revision
#
#   Description  - Put the specified revision data into the database.
#
#   Data         - $this        : The object.
#                  $buffer      : A reference to a buffer that is to contain
#                                 the output from this command.
#                  $contents    : A reference to a buffer containing the
#                                 revision's contents.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub put_revision($$$)
{

    my($this, $buffer, $contents) = @_;

    my @list;

    if (! mtn_command($this, "put_revision", \@list, $contents))
    {
	return;
    }
    $$buffer = $list[0];

    return 1;

}
#
##############################################################################
#
#   Routine      - read_packets
#
#   Description  - Decode and store the specified packet data in the database.
#
#   Data         - $this        : The object.
#                  $packet_data : The packet data that is to be stored in the
#                                 database.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub read_packets($$)
{

    my($this, $packet_data) = @_;

    my $dummy;

    return mtn_command($this, "read_packets", \$dummy, $packet_data);

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
#                  $list        : A reference to a list that is to contain the
#                                 revision ids.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub roots($$)
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
#                  $list        : A reference to a list that is to contain the
#                                 revision ids.
#                  $selector    : The selector that is to be used.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub select($$$)
{

    my($this, $list, $selector) = @_;

    return mtn_command($this, "select", $list, $selector);

}
#
##############################################################################
#
#   Routine      - set_attribute
#
#   Description  - Set an attribute on the specified file or directory.
#
#   Data         - $this        : The object.
#                  $path        : The name of the file or directory that is to
#                                 have an attribute set.
#                  $key         : The name of the attribute that as to be set.
#                  $value       : The value that the attribute is to be set
#                                 to.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub set_attribute($$$$)
{

    my($this, $path, $key, $value) = @_;

    my $dummy;

    return mtn_command($this, "set_attribute", \$dummy, $path, $key, $value);

}
#
##############################################################################
#
#   Routine      - set_db_variable
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



sub set_db_variable($$$$)
{

    my($this, $domain, $name, $value) = @_;

    my($cmd,
       $dummy);

    # This command was renamed in version 0.39 (i/f version 7.x).

    if (can($this, MTN_SET_DB_VARIABLE))
    {
	$cmd = "set_db_variable";
    }
    else
    {
	$cmd = "db_set";
    }
    return mtn_command($this, $cmd, \$dummy, $domain, $name, $value);

}
#
##############################################################################
#
#   Routine      - show_conflicts
#
#   Description  - Get a list of conflicts between the first two head
#                  revisions on the current branch, optionally one can specify
#                  both head revision ids and the name of the branch that they
#                  reside on.
#
#   Data         - $this              : The object.
#                  $ref               : A reference to a buffer or an array
#                                       that is to contain the output from
#                                       this command.
#                  $branch            : The name of the branch that the head
#                                       revisions are on.
#                  $left_revision_id  : The left hand head revision id.
#                  $right_revision_id : The right hand head revision id.
#                  Return Value       : True on success, otherwise false on
#                                       failure.
#
##############################################################################



sub show_conflicts($$;$$$)
{

    my($this, $ref, $branch, $left_revision_id, $right_revision_id) = @_;

    my @opts;

    # Validate the number of arguments and adjust them accordingly.

    if (scalar(@_) == 4)
    {

	# Assume just the revision ids were given, so adjust the arguments
	# accordingly.

	$right_revision_id = $left_revision_id;
	$left_revision_id = $branch;
	$branch = undef;

    }
    elsif (scalar(@_) < 2 || scalar(@_) > 5)
    {

	# Wrong number of arguments.

	$this->{error_msg} = "Wrong number of arguments given";
	&$carper($this->{error_msg});
	return;

    }

    # Process any options.

    @opts = ({key => "branch", value => $branch}) if (defined($branch));

    # Run the command and get the data, either as one lump or as a structured
    # list.

    if (ref($ref) eq "SCALAR")
    {
	return mtn_command_with_options($this,
					"show_conflicts",
					$ref,
					\@opts,
					$left_revision_id,
					$right_revision_id);
    }
    else
    {

	my($i,
	   @lines);

	if (! mtn_command_with_options($this,
				       "show_conflicts",
				       \@lines,
				       \@opts,
				       $left_revision_id,
				       $right_revision_id))
	{
	    return;
	}

	# Reformat the data into a structured array.

	for ($i = 0, @$ref = (); $i < scalar(@lines); ++ $i)
	{
	    if ($lines[$i] =~ m/$io_stanza_re/)
	    {
		my $kv_record;

		# Get the next key-value record.

		parse_kv_record(\@lines,
				\$i,
				\%show_conflicts_keys,
				\$kv_record);
		-- $i;

		# Validate it in terms of expected fields and store.

		if (exists($kv_record->{left}))
		{
		    foreach my $key ("ancestor", "right")
		    {
			&$croaker("Corrupt show_conflicts list, expected "
				  . $key . " field but didn't find it")
			    unless (exists($kv_record->{$key}));
		    }
		}
		push(@$ref, $kv_record);
	    }
	}

	return 1;

    }

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

	my($i,
	   @lines);

	if (! mtn_command($this, "tags", \@lines, $branch_pattern))
	{
	    return;
	}

	# Reformat the data into a structured array.

	for ($i = 0, @$ref = (); $i < scalar(@lines); ++ $i)
	{
	    if ($lines[$i] =~ m/$io_stanza_re/)
	    {
		my $kv_record;

		# Get the next key-value record.

		parse_kv_record(\@lines, \$i, \%tags_keys, \$kv_record);
		-- $i;

		# Validate it in terms of expected fields and store.

		if (exists($kv_record->{tag}))
		{
		    foreach my $key ("revision", "signer")
		    {
			&$croaker("Corrupt tags list, expected " . $key
				  . " field but didn't find it")
			    unless (exists($kv_record->{$key}));
		    }
		    $kv_record->{branches} = []
			unless (exists($kv_record->{branches})
				&& defined($kv_record->{branches}));
		    $kv_record->{revision_id} = $kv_record->{revision};
		    delete($kv_record->{revision});
		    push(@$ref, $kv_record);
		}
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
#                  $list         : A reference to a list that is to contain
#                                  the revision ids.
#                  @revision_ids : The revision ids that are to be sorted with
#                                  the ancestors coming first.
#                  Return Value  : True on success, otherwise false on
#                                  failure.
#
##############################################################################



sub toposort($$@)
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

    if ($feature == MTN_DROP_ATTRIBUTE
	|| $feature == MTN_GET_ATTRIBUTES
	|| $feature == MTN_SET_ATTRIBUTE)
    {

	# These are only available from version 0.36 (i/f version 5.x).

	return 1 if ($this->{mtn_aif_major} >= 5);

    }
    elsif ($feature == MTN_IGNORE_SUSPEND_CERTS
	   || $feature == MTN_INVENTORY_IN_IO_STANZA_FORMAT
	   || $feature == MTN_USE_P_SELECTOR)
    {

	# These are only available from version 0.37 (i/f version 6.x).

	return 1 if ($this->{mtn_aif_major} >= 6);

    }
    elsif ($feature == MTN_DROP_DB_VARIABLES
	   || $feature == MTN_GET_CURRENT_REVISION
	   || $feature == MTN_GET_DB_VARIABLES
	   || $feature == MTN_INVENTORY_TAKE_OPTIONS
	   || $feature == MTN_SET_DB_VARIABLE)
    {

	# These are only available from version 0.39 (i/f version 7.x).

	return 1 if ($this->{mtn_aif_major} >= 7);

    }
    elsif ($feature == MTN_DB_GET)
    {

	# This is only available prior version 0.39 (i/f version 7.x).

	return 1 if ($this->{mtn_aif_major} < 7);

    }
    elsif ($feature == MTN_GET_WORKSPACE_ROOT
	   || $feature == MTN_INVENTORY_INCLUDE_BIRTH_ID
	   || $feature == MTN_SHOW_CONFLICTS)
    {

	# These are only available from version 0.41 (i/f version 8.x).

	return 1 if ($this->{mtn_aif_major} >= 8);

    }
    elsif ($feature == MTN_FILE_MERGE
	   || $feature == MTN_LUA
	   || $feature == MTN_READ_PACKETS)
    {

	# These are only available from version 0.42 (i/f version 9.x).

	return 1 if ($this->{mtn_aif_major} >= 9);

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
#   Routine      - db_locked_condition_detected
#
#   Description  - Check to see if the Monotone database was locked the last
#                  time a command was issued.
#
#   Data         - $this        : The object.
#                  Return Value : True if the database was locked the last
#                                 time a command was issues, otherwise false.
#
##############################################################################



sub db_locked_condition_detected($)
{

    my $this = $_[0];

    return $this->{db_is_locked};

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
#   Routine      - get_ws_path
#
#   Description  - Return the the workspace's base directory as either given
#                  to the constructor or deduced from the current workspace.
#                  Please note that the workspace's base directory may differ
#                  from that given to the constructor if the specified
#                  workspace path is actually a subdirectory within that
#                  workspace.
#
#   Data         - $this        : The object.
#                  Return Value : The workspace's base directory as given to
#                                 the constructor or undef if no workspace was
#                                 given and there is no current workspace.
#
##############################################################################



sub get_ws_path($)
{

    my $this = $_[0];

    return $this->{ws_path};

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
	$this = shift();
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
#                                 registered for.
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

    if ($severity == MTN_SEVERITY_ERROR)
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
    elsif ($severity == MTN_SEVERITY_WARNING)
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
    elsif ($severity == MTN_SEVERITY_ALL)
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
	croak("Unknown error handler severity");
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
	$this = shift();
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
#   Routine      - parse_revision_data
#
#   Description  - Parse the specified revision data into a list of records.
#
#   Data         - $list : A reference to a list that is to contain the
#                          records.
#                  $data : A reference to a list containing the revision data,
#                          line by line.
#
##############################################################################



sub parse_revision_data($$)
{

    my($list, $data) = @_;

    my $i;

    # Reformat the data into a structured array.

    for ($i = 0, @$list = (); $i < scalar(@$data); ++ $i)
    {
	if ($$data[$i] =~ m/$io_stanza_re/)
	{
	    my $kv_record;

	    # Get the next key-value record.

	    parse_kv_record($data, \$i, \%revision_details_keys, \$kv_record);
	    -- $i;

	    # Validate it in terms of expected fields and copy data across to
	    # the correct revision fields.

	    if (exists($kv_record->{add_dir}))
	    {
		push(@$list, {type => "add_dir",
			      name => $kv_record->{add_dir}});
	    }
	    elsif (exists($kv_record->{add_file}))
	    {
		&$croaker("Corrupt revision, expected content field but "
			  . "didn't find it")
		    unless (exists($kv_record->{content}));
		push(@$list, {type    => "add_file",
			      name    => $kv_record->{add_file},
			      file_id => $kv_record->{content}});
	    }
	    elsif (exists($kv_record->{clear}))
	    {
		&$croaker("Corrupt revision, expected attr field but didn't "
			  . "find it")
		    unless (exists($kv_record->{attr}));
		push(@$list, {type      => "clear",
			      name      => $kv_record->{clear},
			      attribute => $kv_record->{attr}});
	    }
	    elsif (exists($kv_record->{delete}))
	    {
		push(@$list, {type => "delete",
			      name => $kv_record->{delete}});
	    }
	    elsif (exists($kv_record->{new_manifest}))
	    {
		push(@$list, {type        => "new_manifest",
			      manifest_id => $kv_record->{new_manifest}});
	    }
	    elsif (exists($kv_record->{old_revision}))
	    {
		push(@$list, {type        => "old_revision",
			      revision_id => $kv_record->{old_revision}});
	    }
	    elsif (exists($kv_record->{patch}))
	    {
		&$croaker("Corrupt revision, expected from field but didn't "
			  . "find it")
		    unless (exists($kv_record->{from}));
		&$croaker("Corrupt revision, expected to field but didn't "
			  . "find it")
		    unless (exists($kv_record->{to}));
		push(@$list, {type         => "patch",
			      name         => $kv_record->{patch},
			      from_file_id => $kv_record->{from},
			      to_file_id   => $kv_record->{to}});
	    }
	    elsif (exists($kv_record->{rename}))
	    {
		&$croaker("Corrupt revision, expected to field but didn't "
			  . "find it")
		    unless (exists($kv_record->{to}));
		push(@$list, {type      => "rename",
			      from_name => $kv_record->{rename},
			      to_name   => $kv_record->{to}});
	    }
	    elsif (exists($kv_record->{set}))
	    {
		&$croaker("Corrupt revision, expected attr field but didn't "
			  . "find it")
		    unless (exists($kv_record->{attr}));
		&$croaker("Corrupt revision, expected value field but didn't "
			  . "find it")
		    unless (exists($kv_record->{value}));
		push(@$list, {type      => "set",
			      name      => $kv_record->{set},
			      attribute => $kv_record->{attr},
			      value     => $kv_record->{value}});
	    }
	}
    }

}
#
##############################################################################
#
#   Routine      - parse_kv_record
#
#   Description  - Parse the specified data for a key-value style record, with
#                  each record being separated by a white space line,
#                  returning the extracted record.
#
#   Data         - $list         : A reference to the list that contains the
#                                  data.
#                  $index        : A reference to a variable containing the
#                                  index of the first line of the record in
#                                  the array. It is updated with the index of
#                                  the first line after the record.
#                  $key_type_map : A reference to the key type map, this is a
#                                  map indexed by key name and has an
#                                  enumeration as its value that describes the
#                                  type of value that is to be read in.
#                  $record       : A reference to a variable that is to be
#                                  updated with the reference to the newly
#                                  created record.
#                  $no_errors    : True if this routine should not report
#                                  errors relating to unknown fields,
#                                  otherwise undef if these errors are to be
#                                  reported. This is optional.
#
##############################################################################



sub parse_kv_record($$$$;$)
{

    my($list, $index, $key_type_map, $record, $no_errors) = @_;

    my($i,
       $key,
       $type,
       $value);

    for ($i = $$index, $$record = {};
	 $i < scalar(@$list) && $$list[$i] =~ m/$io_stanza_re/;
	 ++ $i)
    {
	$key = $1;
	if (exists($$key_type_map{$key}))
	{
	    $type = $$key_type_map{$key};
	    $value = undef;
	    if ($type & BARE_PHRASE && $$list[$i] =~ m/^ *[a-z_]+ ([a-z_]+)$/)
	    {
		$value = $1;
	    }
	    elsif ($type & HEX_ID
		   && $$list[$i] =~ m/^ *[a-z_]+ \[([0-9a-f]+)\]$/)
	    {
		$value = $1;
	    }
	    elsif ($type & OPTIONAL_HEX_ID
		   && $$list[$i] =~ m/^ *[a-z_]+ \[([0-9a-f]*)\]$/)
	    {
		$value = $1;
	    }
	    elsif ($type & STRING && $$list[$i] =~ m/^ *[a-z_]+ \"/)
	    {
		get_quoted_value($list, \$i, \$value);
		$value = unescape($value);
	    }
	    elsif ($type & STRING_ENUM
		   && $$list[$i] =~ m/^ *[a-z_]+ \"([^\"]+)\"$/)
	    {
		$value = $1;
	    }
	    elsif ($type & STRING_LIST
		   && $$list[$i] =~ m/^ *[a-z_]+ \"(.+)\"$/)
	    {
		foreach my $string (split(/\" \"/, $1))
		{
		    push(@$value, unescape($string));
		}
	    }
	    elsif ($type & NULL && $$list[$i] =~ m/^ *[a-z_]+ ?$/)
	    {
	    }
	    else
	    {
		&$croaker("Unsupported key type or corrupt field value "
			  . "detected");
	    }
	    $$record->{$key} = $value;
	}
	else
	{
	    &$croaker("Unrecognised field " . $key . " found")
		unless ($no_errors);
	}
    }
    $$index = $i;

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



sub mtn_command($$$;@)
{

    my($this, $cmd, $ref, @parameters) = @_;

    return mtn_command_with_options($this, $cmd, $ref, [], @parameters);

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
#                  $options     : A reference to a list containing key/value
#                                 anonymous hashes.
#                  @parameters  : A list of parameters to be applied to the
#                                 command.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub mtn_command_with_options($$$$;@)
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
	if (scalar(@$options) > 0)
	{
	    printf($in "o");
	    foreach $opt (@$options)
	    {
		printf($in "%d:%s%d:%s",
		       length($opt->{key}),
		       $opt->{key},
		       length($opt->{value}),
		       $opt->{value});
	    }
	    printf($in "e ");
	}
	printf($in "l%d:%s", length($cmd), $cmd);
	foreach $param (@parameters)
	{

	    # Cater for passing by reference (useful when sending large lumps
	    # of data as in put_file). Also defend against undef being passed
	    # as the only parameter (which can happen when a mandatory argument
	    # is not passed by the caller).

	    if (defined $param)
	    {
		if (ref($param) ne "")
		{
		    printf($in "%d:%s", length($$param), $$param);
		}
		else
		{
		    printf($in "%d:%s", length($param), $param);
		}
	    }

	}
	print($in "e\n");

	# Attempt to read the output of the command, rethrowing any exception
	# that does not relate to locked databases.

	$db_locked_exception = $read_ok = $retry = 0;
	eval
	{
	    $read_ok = mtn_read_output($this, $buffer_ref);
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

	    if ($db_locked_exception
		|| $this->{error_msg} =~ m/$database_locked_re/)
	    {
		$this->{db_is_locked} = 1;
		$retry = &$handler($this, $handler_data);
	    }

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
#                  $buffer      : A reference to the buffer that is to contain
#                                 the data.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub mtn_read_output($$)
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

	    if ($header =~ m/^(\d+):(\d+):([lm]):(\d+):$/)
	    {
		($cmd_nr, $err_code, $last, $size) = ($1, $2, $3, $4);
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
       $cwd,
       $err,
       $version);

    if ($this->{mtn_pid} == 0)
    {

	# Switch to the default locale. We only want to parse the output from
	# Monotone in one language!

	local $ENV{LC_ALL} = "C";
	local $ENV{LANG} = "C";

	$this->{db_is_locked} = undef;
	$this->{mtn_err} = gensym();

	# Build up a list of command line arguments to pass to the mtn
	# subprocess.

	@args = ("mtn");
	push(@args, "--db=" . $this->{db_name}) if ($this->{db_name});
	push(@args, "--ignore-suspend-certs")
	    if (! $this->{honour_suspend_certs});
	push(@args, @{$this->{mtn_options}});
	push(@args, "automate", "stdio");

	# Actually start the mtn subprocess. If a database name has been
	# provided then run the mtn subprocess in the system's root directory
	# so as to avoid any database/workspace clash. Likewise if a workspace
	# has been provided then run the mtn subprocess in the base directory
	# of that workspace.

	$cwd = getcwd();
	eval
	{
	    if (defined($this->{db_name}))
	    {
		die("chdir failed: " . $!)
		    unless (chdir(File::Spec->rootdir()));
	    }
	    elsif (defined($this->{ws_path}))
	    {
		die("chdir failed: " . $!) unless (chdir($this->{ws_path}));
	    }
	    $this->{mtn_pid} = open3($this->{mtn_in},
				     $this->{mtn_out},
				     $this->{mtn_err},
				     @args);
	};
	$err = $@;
	chdir($cwd);
	&$croaker($err) if ($err ne "");

	$this->{cmd_cnt} = 0;
	$this->{poll} = IO::Poll->new();
	$this->{poll}->mask($this->{mtn_out} => POLLIN,
			    $this->{mtn_out} => POLLPRI);

	# Get the interface version.

	interface_version($this, \$version);
	($this->{mtn_aif_major}, $this->{mtn_aif_minor}) =
	    ($version =~ m/^(\d+)\.(\d+)$/);

    }

}
#
##############################################################################
#
#   Routine      - get_ws_details
#
#   Description  - Checks to see if the specified workspace is valid and, if
#                  it is, extracts the workspace root directory and the full
#                  path name of the associated database.
#
#   Data         - $ws_path : The path to the workspace or a subdirectory of
#                             it.
#                  $db_name : A reference to a buffer that is to contain the
#                             name of the database relating to the specified
#                             workspace.
#                  $ws_base : A reference to a buffer that is to contain the
#                             path of the workspace's base directory.
#
##############################################################################



sub get_ws_details($$$)
{

    my($ws_path, $db_name, $ws_base) = @_;

    my($i,
       @lines,
       $options_fh,
       $options_file,
       $path,
       $record);

    # Find the workspace's base directory.

    &$croaker("`" . $ws_path . "' is not a directory") unless (-d $ws_path);
    $path = abs_path($ws_path);
    while (! -d File::Spec->catfile($path, "_MTN"))
    {
	&$croaker("Invalid workspace `" . $db_name
		  . "', no _MTN directory found")
	    if ($path eq File::Spec->rootdir());
	$path = dirname($path);
    }

    # Get the name of the related database out of the _MTN/options file.

    $options_file = File::Spec->catfile($path, "_MTN", "options");
    &$croaker("Could not open `" . $options_file . "' for reading")
	unless (defined($options_fh = IO::File->new($options_file, "r")));
    @lines = $options_fh->getlines();
    $options_fh->close();
    chomp(@lines);
    $i = 0;
    parse_kv_record(\@lines, \$i, \%options_file_keys, \$record, 1);

    # Return what we have found.

    $$db_name = $record->{database};
    $$ws_base = $path;

}
#
##############################################################################
#
#   Routine      - validate_database
#
#   Description  - Checks to see if the specified file is a Monotone SQLite
#                  database. Please note that this does not verify that the
#                  schema of the database is compatible with the version of
#                  Monotone being used.
#
#   Data         - $db_name : The file name of the database to check.
#
##############################################################################



sub validate_database($)
{

    my $db_name = $_[0];

    my($buffer,
       $db);

    # Open the database.

    &$croaker("`" . $db_name . "' is not a file") unless (-f $db_name);
    &$croaker("Could not open `" . $db_name . "' for reading")
	unless (defined($db = IO::File->new($db_name, "r")));
    &$croaker("binmode failed: " . $!) unless (binmode($db));

    # Check that it is an SQLite version 3.x database.

    &$croaker("File `" . $db_name . "' is not a SQLite 3 database")
	if ($db->sysread($buffer, 15) != 15 || $buffer ne "SQLite format 3");

    # Check that it is a Monotone database.

    &$croaker("Database `" . $db_name . "' is not a monotone repository or an "
	      . "older unsupported version")
	if (! $db->sysseek(60, 0)
	    || $db->sysread($buffer, 4) != 4
	    || $buffer ne "_MTN");

    $db->close();

}
#
##############################################################################
#
#   Routine      - validate_mtn_options
#
#   Description  - Checks to see if the specified list of mtn command line
#                  options are valid.
#
#   Data         - $options : A reference to a list containing a list of
#                             options to use on the mtn subprocess.
#
##############################################################################



sub validate_mtn_options($)
{

    my $options = $_[0];

    # Parse the options (don't allow indiscriminate passing of command line
    # options to the subprocess!).

    for (my $i = 0; $i < scalar(@$options); ++ $i)
    {
	if (! exists($valid_mtn_options{$$options[$i]}))
	{
	    &$croaker("Unrecognised option `" . $$options[$i]
		      . "'passed to constructor");
	}
	else
	{
	    $i += $valid_mtn_options{$$options[$i]};
	}
    }

}
#
##############################################################################
#
#   Routine      - create_object_data
#
#   Description  - Creates the record for the Monotone::AutomateStdio object.
#
#   Data         - Return Value : A reference to an anonymous hash containing
#                                 a complete list of initialisd fields.
#
##############################################################################



sub create_object_data()
{

    return {db_name                 => undef,
	    ws_path                 => undef,
	    mtn_options             => undef,
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
	    db_is_locked            => undef,
	    db_locked_handler       => undef,
	    db_locked_handler_data  => undef,
	    io_wait_handler         => undef,
	    io_wait_handler_data    => undef,
	    io_wait_handler_timeout => 1};

}
#
##############################################################################
#
#   Routine      - get_quoted_value
#
#   Description  - Get the contents of a quoted value that may span several
#                  lines and contain escaped quotes.
#
#   Data         - $list   : A reference to the list that contains the quoted
#                            string.
#                  $index  : A reference to a variable containing the index of
#                            the line in the array containing the opening
#                            quote (assumed to be the first quote
#                            encountered). It is updated with the index of the
#                            line containing the closing quote at the end of
#                            the line.
#                  $buffer : A reference to a buffer that is to contain the
#                            contents of the quoted string.
#
##############################################################################



sub get_quoted_value($$$)
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

    &$error_handler(MTN_SEVERITY_ERROR, $message, $error_handler_data);
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

    &$warning_handler(MTN_SEVERITY_WARNING, $message, $warning_handler_data);

}

1;

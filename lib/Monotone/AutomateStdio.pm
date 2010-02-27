##############################################################################
#
#   File Name    - AutomateStdio.pm
#
#   Description  - A class module that provides an interface to Monotone's
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

require 5.008005;

no locale;
use strict;
use warnings;

# ***** REQUIRED PACKAGES *****

# Standard Perl and CPAN modules.

use Carp;
use Cwd qw(abs_path getcwd);
use Encode;
use File::Basename;
use File::Spec;
use IO::File;
use IO::Handle qw(autoflush);
use IO::Poll qw(POLLHUP POLLIN POLLPRI);
use IPC::Open3;
use POSIX qw(:errno_h :limits_h);
use Socket;
use Symbol qw(gensym);

# ***** GLOBAL DATA DECLARATIONS *****

# Constants used to represent the different types of capability Monotone may or
# may not provide depending upon its version.

use constant MTN_COMMON_KEY_HASH               => 0;
use constant MTN_CONTENT_DIFF_EXTRA_OPTIONS    => 1;
use constant MTN_DB_GET                        => 2;
use constant MTN_DROP_ATTRIBUTE                => 3;
use constant MTN_DROP_DB_VARIABLES             => 4;
use constant MTN_FILE_MERGE                    => 5;
use constant MTN_GET_ATTRIBUTES                => 6;
use constant MTN_GET_CURRENT_REVISION          => 7;
use constant MTN_GET_DB_VARIABLES              => 8;
use constant MTN_GET_WORKSPACE_ROOT            => 9;
use constant MTN_HASHED_SIGNATURES             => 10;
use constant MTN_IGNORING_OF_SUSPEND_CERTS     => 11;
use constant MTN_INVENTORY_IN_IO_STANZA_FORMAT => 12;
use constant MTN_INVENTORY_TAKING_OPTIONS      => 13;
use constant MTN_INVENTORY_WITH_BIRTH_ID       => 14;
use constant MTN_LUA                           => 15;
use constant MTN_M_SELECTOR                    => 16;
use constant MTN_P_SELECTOR                    => 17;
use constant MTN_READ_PACKETS                  => 18;
use constant MTN_REMOTE_CONNECTIONS            => 19;
use constant MTN_SET_ATTRIBUTE                 => 20;
use constant MTN_SET_DB_VARIABLE               => 21;
use constant MTN_SHOW_CONFLICTS                => 22;
use constant MTN_STREAM_IO                     => 23;
use constant MTN_SYNCHRONISATION               => 24;
use constant MTN_U_SELECTOR                    => 25;
use constant MTN_W_SELECTOR                    => 26;

# Constants used to represent the different error levels.

use constant MTN_SEVERITY_ALL     => 0x03;
use constant MTN_SEVERITY_ERROR   => 0x01;
use constant MTN_SEVERITY_WARNING => 0x02;

# Constants used to represent data streams from Monotone that can be tied into
# file handles by the caller.

use constant MTN_P_STREAM => 0;
use constant MTN_T_STREAM => 1;

# Constant used to represent the exception thrown when interrupting waitpid().

use constant WAITPID_INTERRUPT => __PACKAGE__ . "::waitpid-interrupt";

# Constants used to represent different value formats.

use constant BARE_PHRASE     => 0x01;  # E.g. orphaned_directory.
use constant HEX_ID          => 0x02;  # E.g. [ab2 ... 1be].
use constant NULL            => 0x04;  # Nothing, i.e. we just have the key.
use constant OPTIONAL_HEX_ID => 0x08;  # As HEX_ID but also [].
use constant STRING          => 0x10;  # Any quoted string, possibly escaped.
use constant STRING_ENUM     => 0x20;  # E.g. "rename_source".
use constant STRING_LIST     => 0x40;  # E.g. "..." "...", possibly escaped.

# Private structures for managing inside-out key caching style objects.

my $class_name = __PACKAGE__;
my %class_records;

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
			 "--rcfile"             => 1,
			 "--root"               => 1,
			 "--ssh-sign"           => 1);

# Maps for quickly detecting valid keys and determining their value types.

my %certs_keys = ("key"       => HEX_ID | STRING,
		  "name"      => STRING,
		  "signature" => STRING,
		  "trust"     => STRING_ENUM,
		  "value"     => STRING);
my %genkey_keys = ("given_name"       => STRING,
		   "hash"             => HEX_ID,
		   "local_name"       => STRING,
		   "name"             => STRING,
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
			   "left_attr_state"   => STRING,
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
		 "signer"         => HEX_ID | STRING,
		 "tag"            => STRING);

# Version of Monotone being used.

my $mtn_version;

# Flag for determining whether the mtn subprocess should be started in a
# workspace's root directory.

my $cd_to_ws_root = 1;

# Flag for detemining whether UTF-8 conversion should be done on the data sent
# to and from the mtn subprocess.

my $convert_to_utf8 = 1;

# Error, database locked and io wait callback routine references and associated
# client data.

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

# Constructors and destructor.

sub new_from_db($;$$);
sub new_from_service($$;$);
sub new_from_ws($;$$);
*new = *new_from_db;
sub DESTROY($);

# Public methods.

sub ancestors($$@);
sub ancestry_difference($$$;@);
sub branches($$);
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
sub register_stream_handle($$$);
sub roots($$);
sub select($$$);
sub set_attribute($$$$);
sub set_db_variable($$$$);
sub show_conflicts($$;$$$);
sub supports($$);
sub suppress_utf8_conversion($$);
sub switch_to_ws_root($$);
sub sync($;$$@);
sub tags($$;$);
sub toposort($$@);

# Public aliased methods.

*attributes = *get_attributes;
*db_set = *set_db_variable;
*pull = *sync;
*push = *sync;

# Private methods and routines.

sub create_object($);
sub error_handler_wrapper($);
sub get_quoted_value($$$);
sub get_ws_details($$$);
sub mtn_command($$$$$;@);
sub mtn_command_with_options($$$$$$;@);
sub mtn_read_output_format_1($$);
sub mtn_read_output_format_2($$);
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

our %EXPORT_TAGS = (capabilities => [qw(MTN_COMMON_KEY_HASH
					MTN_CONTENT_DIFF_EXTRA_OPTIONS
					MTN_DB_GET
					MTN_DROP_ATTRIBUTE
					MTN_DROP_DB_VARIABLES
					MTN_FILE_MERGE
					MTN_GET_ATTRIBUTES
					MTN_GET_CURRENT_REVISION
					MTN_GET_DB_VARIABLES
					MTN_GET_WORKSPACE_ROOT
					MTN_HASHED_SIGNATURES
					MTN_IGNORING_OF_SUSPEND_CERTS
					MTN_INVENTORY_IN_IO_STANZA_FORMAT
					MTN_INVENTORY_TAKING_OPTIONS
					MTN_INVENTORY_WITH_BIRTH_ID
					MTN_LUA
					MTN_M_SELECTOR
					MTN_P_SELECTOR
					MTN_READ_PACKETS
					MTN_REMOTE_CONNECTIONS
					MTN_SET_ATTRIBUTE
					MTN_SET_DB_VARIABLE
					MTN_SHOW_CONFLICTS
					MTN_STREAM_IO
					MTN_SYNCHRONISATION
					MTN_U_SELECTOR
					MTN_W_SELECTOR)],
		    severities	 => [qw(MTN_SEVERITY_ALL
					MTN_SEVERITY_ERROR
					MTN_SEVERITY_WARNING)],
		    streams      => [qw(MTN_P_STREAM
					MTN_T_STREAM)]);
our @EXPORT = qw();
Exporter::export_ok_tags(qw(capabilities severities streams));
our $VERSION = 0.07;
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
    $options = [] unless (defined($options));

    my($db,
       $this,
       $self,
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

    $self = create_object($class);
    $this = $class_records{$self->{$class_name}};
    $this->{db_name} = $db_name;
    $this->{ws_path} = $ws_path;
    $this->{mtn_options} = $options;

    # Startup the mtn subprocess (also determining the interface version).

    $self->startup();

    return $self;

}
#
##############################################################################
#
#   Routine      - new_from_service
#
#   Description  - Class constructor. Construct an object using the specified
#                  Monotone service.
#
#   Data         - $class       : Either the name of the class that is to be
#                                 created or an object of that class.
#                  $service     : The name of the Monotone server to connect
#                                 to, optionally followed by a colon and the
#                                 port number.
#                  $options     : A reference to a list containing a list of
#                                 options to use on the mtn subprocess.
#                  Return Value : A reference to the newly created object.
#
##############################################################################



sub new_from_service($$;$)
{


    my $class = (ref($_[0]) ne "") ? ref($_[0]) : $_[0];
    shift();
    my($service, $options) = @_;
    $options = [] unless (defined($options));

    my($self,
       $server,
       $this);

    # Check all the arguments given to us.

    validate_mtn_options($options);

    # Check that the server is know to us.

    if ($service =~ m/^([^:]+):\d+$/)
    {
	$server = $1;
    }
    else
    {
	$server = $service;
    }
    &$croaker("`" . $server . "' is not known")
	unless (defined(inet_aton($server)));

    # Actually construct the object.

    $self = create_object($class);
    $this = $class_records{$self->{$class_name}};
    $this->{db_name} = ":memory:";
    $this->{network_service} = $service;
    $this->{mtn_options} = $options;

    # Startup the mtn subprocess (also determining the interface version).

    $self->startup();

    return $self;

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
    $options = [] unless (defined($options));

    my($db_name,
       $self,
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

    $self = create_object($class);
    $this = $class_records{$self->{$class_name}};
    $this->{ws_path} = $ws_path;
    $this->{ws_constructed} = 1;
    $this->{mtn_options} = $options;

    # Startup the mtn subprocess (also determining the interface version).

    $self->startup();

    return $self;

}
#
##############################################################################
#
#   Routine      - DESTROY
#
#   Description  - Class destructor.
#
#   Data         - $self : The object.
#
##############################################################################



sub DESTROY($)
{

    my $self = $_[0];

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

    local $@;
    eval
    {
	eval
	{
	    $self->closedown();
	};
	delete($class_records{$self->{$class_name}});
    };

}
#
##############################################################################
#
#   Routine      - ancestors
#
#   Description  - Get a list of ancestors for the specified revisions.
#
#   Data         - $self         : The object.
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

    my($self, $list, @revision_ids) = @_;

    return $self->mtn_command("ancestors", 0, 0, $list, @revision_ids);

}
#
##############################################################################
#
#   Routine      - ancestry_difference
#
#   Description  - Get a list of ancestors for the specified revision, that
#                  are not also ancestors for the specified old revisions.
#
#   Data         - $self             : The object.
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

    my($self, $list, $new_revision_id, @old_revision_ids) = @_;

    return $self->mtn_command("ancestry_difference",
			      0,
			      0,
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
#   Data         - $self        : The object.
#                  $list        : A reference to a list that is to contain the
#                                 branch names.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub branches($$)
{

    my($self, $list) = @_;

    return $self->mtn_command("branches", 0, 1, $list);

}
#
##############################################################################
#
#   Routine      - cert
#
#   Description  - Add the specified cert to the specified revision.
#
#   Data         - $self        : The object.
#                  $revision_id : The revision id to which the cert is to be
#                                 applied.
#                  $name        : The name of the cert to be applied.
#                  $value       : The value of the cert.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub cert($$$$)
{

    my($self, $revision_id, $name, $value) = @_;

    my $dummy;

    return $self->mtn_command("cert",
			      1,
			      1,
			      \$dummy,
			      $revision_id,
			      $name,
			      $value);

}
#
##############################################################################
#
#   Routine      - certs
#
#   Description  - Get all the certs for the specified revision.
#
#   Data         - $self        : The object.
#                  $ref         : A reference to a buffer or an array that is
#                                 to contain the output from this command.
#                  $revision_id : The id of the revision that is to have its
#                                 certs returned.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub certs($$$)
{

    my($self, $ref, $revision_id) = @_;

    # Run the command and get the data, either as one lump or as a structured
    # list.

    if (ref($ref) eq "SCALAR")
    {
	return $self->mtn_command("certs", 0, 1, $ref, $revision_id);
    }
    else
    {

	my($i,
	   @lines);

	if (! $self->mtn_command("certs", 0, 1, \@lines, $revision_id))
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
			      . " field but did not find it")
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
#   Data         - $self        : The object.
#                  $list        : A reference to a list that is to contain the
#                                 revision ids.
#                  $revision_id : The revision id that is to have its children
#                                 returned.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub children($$$)
{

    my($self, $list, @revision_ids) = @_;

    return $self->mtn_command("children", 0, 0, $list, @revision_ids);

}
#
##############################################################################
#
#   Routine      - common_ancestors
#
#   Description  - Get a list of revisions that are all ancestors of the
#                  specified revision.
#
#   Data         - $self         : The object.
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

    my($self, $list, @revision_ids) = @_;

    return $self->mtn_command("common_ancestors", 0, 0, $list, @revision_ids);

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
#   Data         - $self         : The object.
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

    my($self, $buffer, $options, $revision_id1, $revision_id2, @file_names)
	= @_;

    my @opts;

    # Process any options.

    if (defined($options))
    {
	for (my $i = 0; $i < scalar(@$options); ++ $i)
	{
	    if ($$options[$i] eq "reverse"
		|| $$options[$i] eq "with-header"
		|| $$options[$i] eq "without-header")
	    {
		push(@opts, {key => $$options[$i], value => ""});
	    }
	    else
	    {
		push(@opts, {key => $$options[$i], value => $$options[++ $i]});
	    }
	}
    }
    push(@opts, {key => "r", value => $revision_id1})
	if (defined($revision_id1));
    push(@opts, {key => "r", value => $revision_id2})
	if (defined($revision_id2));

    return $self->mtn_command_with_options("content_diff",
					   1,
					   1,
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
#   Data         - $self        : The object.
#                  $buffer      : A reference to a buffer that is to contain
#                                 the output from this command.
#                  $domain      : The domain of the database variable.
#                  $name        : The name of the variable to fetch.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub db_get($$$$)
{

    my($self, $buffer, $domain, $name) = @_;

    return $self->mtn_command("db_get", 1, 1, $buffer, $domain, $name);

}
#
##############################################################################
#
#   Routine      - descendents
#
#   Description  - Get a list of descendents for the specified revisions.
#
#   Data         - $self         : The object.
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

    my($self, $list, @revision_ids) = @_;

    return $self->mtn_command("descendents", 0, 0, $list, @revision_ids);

}
#
##############################################################################
#
#   Routine      - drop_attribute
#
#   Description  - Drop attributes from the specified file or directory,
#                  optionally limiting it to the specified attribute.
#
#   Data         - $self        : The object.
#                  $path        : The name of the file or directory that is to
#                                 have an attribute dropped.
#                  $key         : The name of the attribute that as to be
#                                 dropped.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub drop_attribute($$$)
{

    my($self, $path, $key) = @_;

    my $dummy;

    return $self->mtn_command("drop_attribute", 1, 0, \$dummy, $path, $key);

}
#
##############################################################################
#
#   Routine      - drop_db_variables
#
#   Description  - Drop variables from the specified domain, optionally
#                  limiting it to the specified variable.
#
#   Data         - $self        : The object.
#                  $domain      : The name of the domain that is to have one
#                                 or all of its variables dropped.
#                  $name        : The name of the variable that is to be
#                                 dropped.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub drop_db_variables($$;$)
{

    my($self, $domain, $name) = @_;

    my $dummy;

    return $self->mtn_command("drop_db_variables",
			      1,
			      0,
			      \$dummy,
			      $domain,
			      $name);

}
#
##############################################################################
#
#   Routine      - erase_ancestors
#
#   Description  - For a given list of revisions, weed out those that are
#                  ancestors to other revisions specified within the list.
#
#   Data         - $self         : The object.
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

    my($self, $list, @revision_ids) = @_;

    return $self->mtn_command("erase_ancestors", 0, 0, $list, @revision_ids);

}
#
##############################################################################
#
#   Routine      - file_merge
#
#   Description  - Get the result of merging two files, both of which are on
#                  separate revisions.
#
#   Data         - $self              : The object.
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

    my($self,
       $buffer,
       $left_revision_id,
       $left_file_name,
       $right_revision_id,
       $right_file_name) = @_;

    return $self->mtn_command("file_merge",
			      1,
			      1,
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
#   Data         - $self        : The object.
#                  $ref         : A reference to a buffer or a hash that is to
#                                 contain the output from this command.
#                  $key_id      : The key id for the new key.
#                  $pass_phrase : The pass phrase for the key.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub genkey($$$$)
{

    my($self, $ref, $key_id, $pass_phrase) = @_;

    # Run the command and get the data, either as one lump or as a structured
    # list.

    if (ref($ref) eq "SCALAR")
    {
	return $self->mtn_command("genkey", 1, 1, $ref, $key_id, $pass_phrase);
    }
    else
    {

	my($i,
	   $kv_record,
	   @lines);

	if (! $self->mtn_command("genkey",
				 1,
				 1,
				 \@lines,
				 $key_id,
				 $pass_phrase))
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
#   Data         - $self        : The object.
#                  $ref         : A reference to a buffer or an array that is
#                                 to contain the output from this command.
#                  $file_name   : The name of the file that is to be reported
#                                 on.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub get_attributes($$$)
{

    my($self, $ref, $file_name) = @_;

    my $cmd;

    # This command was renamed in version 0.36 (i/f version 5.x).

    if ($self->supports(MTN_GET_ATTRIBUTES))
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
	return $self->mtn_command($cmd, 1, 1, $ref, $file_name);
    }
    else
    {

	my($i,
	   @lines);

	if (! $self->mtn_command($cmd, 1, 1, \@lines, $file_name))
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
			      . "but did not find it")
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
#   Description  - Get the id of the revision upon which the workspace is
#                  based.
#
#   Data         - $self        : The object.
#                  $buffer      : A reference to a buffer that is to contain
#                                 the output from this command.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub get_base_revision_id($$)
{

    my($self, $buffer) = @_;

    my @list;

    $$buffer = "";
    if (! $self->mtn_command("get_base_revision_id", 0, 0, \@list))
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
#   Data         - $self        : The object.
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

    my($self, $list, $revision_id, $file_name) = @_;

    my($i,
       @lines);

    # Run the command and get the data.

    if (! $self->mtn_command("get_content_changed",
			     1,
			     0,
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
#   Data         - $self               : The object.
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

    my($self, $buffer, $source_revision_id, $file_name, $target_revision_id)
	= @_;

    my($i,
       @lines);

    # Run the command and get the data.

    if (! $self->mtn_command("get_corresponding_path",
			     1,
			     1,
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
#   Data         - $self        : The object.
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

    my($self, $ref, $options, @paths) = @_;

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
	return $self->mtn_command_with_options("get_current_revision",
					       1,
					       1,
					       $ref,
					       \@opts,
					       @paths);
    }
    else
    {

	my @lines;

	if (! $self->mtn_command_with_options("get_current_revision",
					      1,
					      1,
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
#   Description  - Get the id of the revision that would be created if an
#                  unrestricted commit was done in the workspace.
#
#   Data         - $self        : The object.
#                  $buffer      : A reference to a buffer that is to contain
#                                 the output from this command.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub get_current_revision_id($$)
{

    my($self, $buffer) = @_;

    my @list;

    $$buffer = "";
    if (! $self->mtn_command("get_current_revision_id", 0, 0, \@list))
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
#   Data         - $self        : The object.
#                  $ref         : A reference to a buffer or an array that is
#                                 to contain the output from this command.
#                  $domain      : The name of the domain that is to have its
#                                 variables listed.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub get_db_variables($$;$)
{

    my($self, $ref, $domain) = @_;

    # Run the command and get the data, either as one lump or as a structured
    # list.

    if (ref($ref) eq "SCALAR")
    {
	return $self->mtn_command("get_db_variables", 1, 1, $ref, $domain);
    }
    else
    {

	my($domain_name,
	   $i,
	   @lines,
	   $name,
	   $value);

	if (! $self->mtn_command("get_db_variables", 1, 1, \@lines, $domain))
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
			      . "but did not find it");
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
#   Data         - $self        : The object.
#                  $buffer      : A reference to a buffer that is to contain
#                                 the output from this command.
#                  $file_id     : The file id of the file that is to be
#                                 returned.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub get_file($$$)
{

    my($self, $buffer, $file_id) = @_;

    return $self->mtn_command("get_file", 0, 0, $buffer, $file_id);

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
#   Data         - $self        : The object.
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

    my($self, $buffer, $file_name, $revision_id) = @_;

    my @opts;

    push(@opts, {key => "r", value => $revision_id})
	if (defined($revision_id));

    return $self->mtn_command_with_options("get_file_of",
					   1,
					   0,
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
#   Data         - $self        : The object.
#                  $ref         : A reference to a buffer or an array that is
#                                 to contain the output from this command.
#                  $revision_id : The revision id which is to have its
#                                 manifest returned.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub get_manifest_of($$;$)
{

    my($self, $ref, $revision_id) = @_;

    # Run the command and get the data, either as one lump or as a structured
    # list.

    if (ref($ref) eq "SCALAR")
    {
	return $self->mtn_command("get_manifest_of", 0, 1, $ref, $revision_id);
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

	if (! $self->mtn_command("get_manifest_of",
				 0,
				 1,
				 \@lines,
				 $revision_id))
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
			      . "did not find it");
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
#   Data         - $self        : The object.
#                  $buffer      : A reference to a buffer that is to contain
#                                 the output from this command.
#                  $option_name : The name of the option to be fetched.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub get_option($$$)
{

    my($self, $buffer, $option_name) = @_;

    if (! $self->mtn_command("get_option", 1, 1, $buffer, $option_name))
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
#   Data         - $self        : The object.
#                  $ref         : A reference to a buffer or an array that is
#                                 to contain the output from this command.
#                  $revision_id : The revision id which is to have its data
#                                 returned.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub get_revision($$$)
{

    my($self, $ref, $revision_id) = @_;

    # Run the command and get the data, either as one lump or as a structured
    # list.

    if (ref($ref) eq "SCALAR")
    {
	return $self->mtn_command("get_revision", 0, 1, $ref, $revision_id);
    }
    else
    {

	my @lines;

	if (! $self->mtn_command("get_revision", 0, 1, \@lines, $revision_id))
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
#   Data         - $self        : The object.
#                  $buffer      : A reference to a buffer that is to contain
#                                 the output from this command.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub get_workspace_root($$)
{

    my($self, $buffer) = @_;

    if (! $self->mtn_command("get_workspace_root", 0, 1, $buffer))
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
#   Data         - $self        : The object.
#                  $ref         : A reference to a buffer or an array that is
#                                 to contain the output from this command.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub graph($$)
{

    my($self, $ref) = @_;

    # Run the command and get the data, either as one lump or as a structured
    # list.

    if (ref($ref) eq "SCALAR")
    {
	return $self->mtn_command("graph", 0, 0, $ref);
    }
    else
    {

	my($i,
	   @lines,
	   @parent_ids);

	if (! $self->mtn_command("graph", 0, 0, \@lines))
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
#   Data         - $self        : The object.
#                  $list        : A reference to a list that is to contain the
#                                 revision ids.
#                  $branch_name : The name of the branch that is to have its
#                                 heads returned.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub heads($$;$)
{

    my($self, $list, $branch_name) = @_;

    return $self->mtn_command("heads", 1, 0, $list, $branch_name);

}
#
##############################################################################
#
#   Routine      - identify
#
#   Description  - Get the file id, i.e. hash, of the specified file.
#
#   Data         - $self        : The object.
#                  $buffer      : A reference to a buffer that is to contain
#                                 the output from this command.
#                  $file_name   : The name of the file that is to have its id
#                                 returned.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub identify($$$)
{

    my($self, $buffer, $file_name) = @_;

    my @list;

    $$buffer = "";
    if (! $self->mtn_command("identify", 1, 0, \@list, $file_name))
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
#   Data         - $self        : The object.
#                  $buffer      : A reference to a buffer that is to contain
#                                 the output from this command.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub interface_version($$)
{

    my($self, $buffer) = @_;

    my @list;

    $$buffer = "";
    if (! $self->mtn_command("interface_version", 0, 0, \@list))
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
#   Data         - $self        : The object.
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

    my($self, $ref, $options, @paths) = @_;

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
	return $self->mtn_command_with_options("inventory",
					       1,
					       1,
					       $ref,
					       \@opts,
					       @paths);
    }
    else
    {

	my @lines;

	if (! $self->mtn_command_with_options("inventory",
					      1,
					      1,
					      \@lines,
					      \@opts,
					      @paths))
	{
	    return;
	}

	# The output format of this command was switched over to a basic_io
	# stanza in 0.37 (i/f version 6.x).

	if ($self->supports(MTN_INVENTORY_IN_IO_STANZA_FORMAT))
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
#   Data         - $self        : The object.
#                  $ref         : A reference to a buffer or an array that is
#                                 to contain the output from this command.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub keys($$)
{

    my($self, $ref) = @_;

    # Run the command and get the data, either as one lump or as a structured
    # list.

    if (ref($ref) eq "SCALAR")
    {
	return $self->mtn_command("keys", 0, 1, $ref);
    }
    else
    {

	my($i,
	   @lines,
	   @valid_fields);

	if (! $self->mtn_command("keys", 0, 1, \@lines))
	{
	    return;
	}

	# Build up a list of valid fields depending upon the version of
	# Monotone in use.

	push(@valid_fields, "given_name", "local_name")
	    if ($self->supports(MTN_HASHED_SIGNATURES));
	if ($self->supports(MTN_COMMON_KEY_HASH))
	{
	    push(@valid_fields, "hash");
	}
	else
	{
	    push(@valid_fields, "public_hash");
	}
	push(@valid_fields, "public_location");

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

		foreach my $key (@valid_fields)
		{
		    &$croaker("Corrupt keys list, expected " . $key
			      . " field but did not find it")
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
#   Routine      - leaves
#
#   Description  - Get a list of leaf revisions.
#
#   Data         - $self        : The object.
#                  $list        : A reference to a list that is to contain the
#                                 revision ids.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub leaves($$)
{

    my($self, $list) = @_;

    return $self->mtn_command("leaves", 0, 0, $list);

}
#
##############################################################################
#
#   Routine      - lua
#
#   Description  - Call the specified LUA function with any required
#                  arguments.
#
#   Data         - $self         : The object.
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

    my($self, $buffer, $lua_function, @arguments) = @_;

    return $self->mtn_command("lua", 1, 1, $buffer, $lua_function, @arguments);

}
#
##############################################################################
#
#   Routine      - packet_for_fdata
#
#   Description  - Get the contents of the file referenced by the specified
#                  file id in packet format.
#
#   Data         - $self        : The object.
#                  $buffer      : A reference to a buffer that is to contain
#                                 the output from this command.
#                  $file_id     : The file id of the file that is to be
#                                 returned.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub packet_for_fdata($$$)
{

    my($self, $buffer, $file_id) = @_;

    return $self->mtn_command("packet_for_fdata", 0, 0, $buffer, $file_id);

}
#
##############################################################################
#
#   Routine      - packet_for_fdelta
#
#   Description  - Get the file delta between the two files referenced by the
#                  specified file ids in packet format.
#
#   Data         - $self         : The object.
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

    my($self, $buffer, $from_file_id, $to_file_id) = @_;

    return $self->mtn_command("packet_for_fdelta",
			      0,
			      0,
			      $buffer,
			      $from_file_id,
			      $to_file_id);

}
#
##############################################################################
#
#   Routine      - packet_for_rdata
#
#   Description  - Get the contents of the revision referenced by the
#                  specified revision id in packet format.
#
#   Data         - $self        : The object.
#                  $buffer      : A reference to a buffer that is to contain
#                                 the output from this command.
#                  $revision_id : The revision id of the revision that is to
#                                 be returned.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub packet_for_rdata($$$)
{

    my($self, $buffer, $revision_id) = @_;

    return $self->mtn_command("packet_for_rdata", 0, 0, $buffer, $revision_id);

}
#
##############################################################################
#
#   Routine      - packets_for_certs
#
#   Description  - Get all the certs for the revision referenced by the
#                  specified revision id in packet format.
#
#   Data         - $self        : The object.
#                  $buffer      : A reference to a buffer that is to contain
#                                 the output from this command.
#                  $revision_id : The revision id of the revision that is to
#                                 have its certs returned.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub packets_for_certs($$$)
{

    my($self, $buffer, $revision_id) = @_;

    return $self->mtn_command("packets_for_certs",
			      0,
			      0,
			      $buffer,
			      $revision_id);

}
#
##############################################################################
#
#   Routine      - parents
#
#   Description  - Get a list of parents for the specified revision.
#
#   Data         - $self        : The object.
#                  $list        : A reference to a list that is to contain the
#                                 revision ids.
#                  $revision_id : The revision id that is to have its parents
#                                 returned.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub parents($$$)
{

    my($self, $list, $revision_id) = @_;

    return $self->mtn_command("parents", 0, 0, $list, $revision_id);

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
#   Data         - $self         : The object.
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

    my($self, $buffer, $base_file_id, $contents) = @_;

    my @list;

    if (defined($base_file_id))
    {
	if (! $self->mtn_command("put_file",
				 0,
				 0,
				 \@list,
				 $base_file_id,
				 $contents))
	{
	    return;
	}
    }
    else
    {
	if (! $self->mtn_command("put_file", 0, 0, \@list, $contents))
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
#   Data         - $self        : The object.
#                  $buffer      : A reference to a buffer that is to contain
#                                 the output from this command.
#                  $contents    : A reference to a buffer containing the
#                                 revision's contents.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub put_revision($$$)
{

    my($self, $buffer, $contents) = @_;

    my @list;

    if (! $self->mtn_command("put_revision", 1, 0, \@list, $contents))
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
#   Data         - $self        : The object.
#                  $packet_data : The packet data that is to be stored in the
#                                 database.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub read_packets($$)
{

    my($self, $packet_data) = @_;

    my $dummy;

    return $self->mtn_command("read_packets", 0, 0, \$dummy, $packet_data);

}
#
##############################################################################
#
#   Routine      - roots
#
#   Description  - Get a list of root revisions, i.e. revisions with no
#                  parents.
#
#   Data         - $self        : The object.
#                  $list        : A reference to a list that is to contain the
#                                 revision ids.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub roots($$)
{

    my($self, $list) = @_;

    return $self->mtn_command("roots", 0, 0, $list);

}
#
##############################################################################
#
#   Routine      - select
#
#   Description  - Get a list of revision ids that match the specified
#                  selector.
#
#   Data         - $self        : The object.
#                  $list        : A reference to a list that is to contain the
#                                 revision ids.
#                  $selector    : The selector that is to be used.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub select($$$)
{

    my($self, $list, $selector) = @_;

    return $self->mtn_command("select", 1, 0, $list, $selector);

}
#
##############################################################################
#
#   Routine      - set_attribute
#
#   Description  - Set an attribute on the specified file or directory.
#
#   Data         - $self        : The object.
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

    my($self, $path, $key, $value) = @_;

    my $dummy;

    return $self->mtn_command("set_attribute",
			      1,
			      0,
			      \$dummy,
			      $path,
			      $key,
			      $value);

}
#
##############################################################################
#
#   Routine      - set_db_variable
#
#   Description  - Set the value of a database variable.
#
#   Data         - $self        : The object.
#                  $domain      : The domain of the database variable.
#                  $name        : The name of the variable to set.
#                  $value       : The value to set the variable to.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub set_db_variable($$$$)
{

    my($self, $domain, $name, $value) = @_;

    my($cmd,
       $dummy);

    # This command was renamed in version 0.39 (i/f version 7.x).

    if ($self->supports(MTN_SET_DB_VARIABLE))
    {
	$cmd = "set_db_variable";
    }
    else
    {
	$cmd = "db_set";
    }
    return $self->mtn_command($cmd, 1, 0, \$dummy, $domain, $name, $value);

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
#   Data         - $self              : The object.
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

    my($self, $ref, $branch, $left_revision_id, $right_revision_id) = @_;

    my @opts;
    my $this = $class_records{$self->{$class_name}};

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

	&$croaker("Wrong number of arguments given");

    }

    # Process any options.

    @opts = ({key => "branch", value => $branch}) if (defined($branch));

    # Run the command and get the data, either as one lump or as a structured
    # list.

    if (ref($ref) eq "SCALAR")
    {
	return $self->mtn_command_with_options("show_conflicts",
					       1,
					       1,
					       $ref,
					       \@opts,
					       $left_revision_id,
					       $right_revision_id);
    }
    else
    {

	my($i,
	   @lines);

	if (! $self->mtn_command_with_options("show_conflicts",
					      1,
					      1,
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
				  . $key . " field but did not find it")
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
#   Routine      - sync
#
#   Description  - Synchronises database changes between the local database
#                  and the specified remote server. This member function also
#                  provides the implementation to the pull and push methods.
#
#   Data         - $self        : The object.
#                  $options     : A reference to a list containing the options
#                                 to use.
#                  $service     : The name of the server to synchronise with,
#                                 optionally followed by a colon and the port
#                                 to connect to or a URI.
#                  @patterns    : A list of branch patterns to include in the
#                                 pull operation.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub sync($;$$@)
{

    my($self, $options, $service, @patterns) = @_;

    my($cmd,
       $dummy,
       @opts);

    # Find out how we were called (and hence the command that is to be run).
    # Remember that the routine name will be fully qualified.

    $cmd = (caller(0))[3];
    $cmd = $1 if ($cmd =~ m/^.+\:\:([^:]+)$/);

    # Process any options.

    if (defined($options))
    {
	for (my $i = 0; $i < scalar(@$options); ++ $i)
	{
	    if ($$options[$i] eq "set-default")
	    {
		push(@opts, {key => $$options[$i], value => ""});
	    }
	    else
	    {
		push(@opts, {key => $$options[$i], value => $$options[++ $i]});
	    }
	}
    }

    # Run the command.

    if (defined($service))
    {
	return $self->mtn_command_with_options($cmd,
					       1,
					       1,
					       \$dummy,
					       \@opts,
					       $service,
					       @patterns);
    }
    else
    {
	return $self->mtn_command_with_options($cmd, 1, 1, \$dummy, \@opts);
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
#   Data         - $self           : The object.
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

    my($self, $ref, $branch_pattern) = @_;

    # Run the command and get the data, either as one lump or as a structured
    # list.

    if (ref($ref) eq "SCALAR")
    {
	return $self->mtn_command("tags", 1, 1, $ref, $branch_pattern);
    }
    else
    {

	my($i,
	   @lines);

	if (! $self->mtn_command("tags", 1, 1, \@lines, $branch_pattern))
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
				  . " field but did not find it")
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
#   Description  - Sort the specified revision ids such that the ancestors
#                  come out first.
#
#   Data         - $self         : The object.
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

    my($self, $list, @revision_ids) = @_;

    return $self->mtn_command("toposort", 0, 0, $list, @revision_ids);

}
#
##############################################################################
#
#   Routine      - closedown
#
#   Description  - If started then stop the mtn subprocess.
#
#   Data         - $self : The object.
#
##############################################################################



sub closedown($)
{

    my $self = $_[0];

    my $this = $class_records{$self->{$class_name}};

    if ($this->{mtn_pid} != 0)
    {

	# Close off all file descriptors to the mtn subprocess. This should be
	# enough to cause it to exit gracefully.

	close($this->{mtn_in});
	close($this->{mtn_out});
	close($this->{mtn_err});

	# Reap the mtn subprocess and deal with any errors.

	for (my $i = 0; $i < 4; ++ $i)
	{

	    my $wait_status = 0;

	    # Wait for the mtn subprocess to exit (preserving the current state
	    # of $@ so that any exception that has already occurred is not
	    # lost, also ignore any errors resulting from waitpid()
	    # interruption).

	    {
		local $@;
		eval
		{
		    local $SIG{ALRM} = sub { die(WAITPID_INTERRUPT); };
		    alarm(5);
		    $wait_status = waitpid($this->{mtn_pid}, 0);
		    alarm(0);
		};
		$wait_status = 0
		    if ($@ eq WAITPID_INTERRUPT && $wait_status < 0
			&& $! == EINTR);
	    }

	    # The mtn subprocess has terminated.

	    if ($wait_status == $this->{mtn_pid})
	    {
		last;
	    }

	    # The mtn subprocess is still there so try and kill it unless it's
	    # time to just give up.

	    elsif ($i < 3 && $wait_status == 0)
	    {
		if ($i == 0)
		{
		    kill("INT", $this->{mtn_pid});
		}
		elsif ($i == 1)
		{
		    kill("TERM", $this->{mtn_pid});
		}
		else
		{
		    kill("KILL", $this->{mtn_pid});
		}
	    }

	    # Stop if we don't have any relevant children to wait for anymore.

	    elsif ($wait_status < 0 && $! == ECHILD)
	    {
		last;
	    }

	    # Either there is some other error with waitpid() or a child
	    # process has been reaped that we aren't interested in (in which
	    # case just ignore it).

	    elsif ($wait_status < 0)
	    {
		my $err_msg = $!;
		kill("KILL", $this->{mtn_pid});
		&$croaker("waitpid failed: " . $err_msg);
	    }

	}

	$this->{poll} = undef;
	$this->{mtn_pid} = 0;

    }

    return;

}
#
##############################################################################
#
#   Routine      - db_locked_condition_detected
#
#   Description  - Check to see if the Monotone database was locked the last
#                  time a command was issued.
#
#   Data         - $self        : The object.
#                  Return Value : True if the database was locked the last
#                                 time a command was issues, otherwise false.
#
##############################################################################



sub db_locked_condition_detected($)
{

    my $self = $_[0];

    my $this = $class_records{$self->{$class_name}};

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
#   Data         - $self        : The object.
#                  Return Value : The file name of the database as given to
#                                 the constructor or undef if no database was
#                                 specified.
#
##############################################################################



sub get_db_name($)
{

    my $self = $_[0];

    my $this = $class_records{$self->{$class_name}};

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
#   Data         - $self        : The object.
#                  Return Value : The message for the last error detected, or
#                                 an empty string if nothing has gone wrong
#                                 yet.
#
##############################################################################



sub get_error_message($)
{

    my $self = $_[0];

    my $this = $class_records{$self->{$class_name}};

    return $this->{error_msg};

}
#
##############################################################################
#
#   Routine      - get_pid
#
#   Description  - Return the process id of the mtn automate stdio process.
#
#   Data         - $self        : The object.
#                  Return Value : The process id of the mtn automate stdio
#                                 process, or zero if no process is thought to
#                                 be running.
#
##############################################################################



sub get_pid($)
{

    my $self = $_[0];

    my $this = $class_records{$self->{$class_name}};

    return $this->{mtn_pid};

}
#
##############################################################################
#
#   Routine      - get_ws_path
#
#   Description  - Return the the workspace's base directory as either given
#                  to the constructor or deduced from the current workspace.
#                  If neither condition holds true then undef is returned.
#                  Please note that the workspace's base directory may differ
#                  from that given to the constructor if the specified
#                  workspace path is actually a subdirectory within that
#                  workspace.
#
#   Data         - $self        : The object.
#                  Return Value : The workspace's base directory or undef if
#                                 no workspace was specified and there is no
#                                 current workspace.
#
##############################################################################



sub get_ws_path($)
{

    my $self = $_[0];

    my $this = $class_records{$self->{$class_name}};

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
#   Data         - $self        : The object.
#                  $ignore      : True if suspend certs are to be ignored
#                                 (i.e. all revisions are `visible'),
#                                 otherwise false if suspend certs are to be
#                                 honoured.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub ignore_suspend_certs($$)
{

    my($self, $ignore) = @_;

    my $this = $class_records{$self->{$class_name}};

    # This only works from version 0.37 (i/f version 6.x).

    if ($this->{honour_suspend_certs} && $ignore)
    {
	if ($self->supports(MTN_IGNORING_OF_SUSPEND_CERTS))
	{
	    $this->{honour_suspend_certs} = undef;
	    $self->closedown();
	    $self->startup();
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
	$self->closedown();
	$self->startup();
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
#   Data         - $self        : Either the object, the package name or not
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

    my($self,
       $this);
    if ($_[0]->isa(__PACKAGE__))
    {
	if (ref($_[0]) ne "")
	{
	    $self = shift();
	    $this = $class_records{$self->{$class_name}};
	}
	else
	{
	    shift();
	}
    }
    my($handler, $client_data) = @_;

    if (defined($self))
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

    return;

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
#   Data         - $self        : The object. This may not be present
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

    shift() if ($_[0]->isa(__PACKAGE__));
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
	&$croaker("Unknown error handler severity");
    }

    return;

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
#   Data         - $self        : Either the object, the package name or not
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

    my($self,
       $this);
    if ($_[0]->isa(__PACKAGE__))
    {
	if (ref($_[0]) ne "")
	{
	    $self = shift();
	    $this = $class_records{$self->{$class_name}};
	}
	else
	{
	    shift();
	}
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

    if (defined($self))
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

    return;

}
#
##############################################################################
#
#   Routine      - register_stream_handle
#
#   Description  - Register the specified file handle to receive data from the
#                  specified mtn automate stdio output stream.
#
#   Data         - $self   : The object.
#                  $stream : The mtn output stream from which data is to be
#                            read and then written to the specified file
#                            handle.
#                  $handle : The file handle that is to receive the data from
#                            the specified output stream. If this is not
#                            provided then any existing file handle for that
#                            stream is unregistered.
#
##############################################################################



sub register_stream_handle($$$)
{

    my($self, $stream, $handle) = @_;

    my $this = $class_records{$self->{$class_name}};

    if (defined($handle) && ref($handle) !~ m/^IO::[^:]+/
	&& ref($handle) ne "GLOB" && ref(\$handle) ne "GLOB")
    {
	&$croaker("Handle must be either undef or a valid handle");
    }
    autoflush($stream, 1);
    if ($stream == MTN_P_STREAM)
    {
	$this->{p_stream_handle} = $handle;
    }
    elsif ($stream == MTN_T_STREAM)
    {
	$this->{t_stream_handle} = $handle;
    }
    else
    {
	&$croaker("Unknown stream specified");
    }

    return;

}
#
##############################################################################
#
#   Routine      - supports
#
#   Description  - Determine whether a certain feature is available with the
#                  version of Monotone that is currently being used.
#
#   Data         - $self         : The object.
#                  $feature      : A constant specifying the feature that is
#                                  to be checked for.
#                  Return Value  : True if the feature is supported, otherwise
#                                  false if it is not.
#
##############################################################################



sub supports($$)
{

    my($self, $feature) = @_;

    my $this = $class_records{$self->{$class_name}};

    if ($feature == MTN_DROP_ATTRIBUTE
	|| $feature == MTN_GET_ATTRIBUTES
	|| $feature == MTN_SET_ATTRIBUTE)
    {

	# These are only available from version 0.36 (i/f version 5.x).

	return 1 if ($this->{mtn_aif_version} >= 5);

    }
    elsif ($feature == MTN_IGNORING_OF_SUSPEND_CERTS
	   || $feature == MTN_INVENTORY_IN_IO_STANZA_FORMAT
	   || $feature == MTN_P_SELECTOR)
    {

	# These are only available from version 0.37 (i/f version 6.x).

	return 1 if ($this->{mtn_aif_version} >= 6);

    }
    elsif ($feature == MTN_DROP_DB_VARIABLES
	   || $feature == MTN_GET_CURRENT_REVISION
	   || $feature == MTN_GET_DB_VARIABLES
	   || $feature == MTN_INVENTORY_TAKING_OPTIONS
	   || $feature == MTN_SET_DB_VARIABLE)
    {

	# These are only available from version 0.39 (i/f version 7.x).

	return 1 if ($this->{mtn_aif_version} >= 7);

    }
    elsif ($feature == MTN_DB_GET)
    {

	# This is only available prior version 0.39 (i/f version 7.x).

	return 1 if ($this->{mtn_aif_version} < 7);

    }
    elsif ($feature == MTN_GET_WORKSPACE_ROOT
	   || $feature == MTN_INVENTORY_WITH_BIRTH_ID
	   || $feature == MTN_SHOW_CONFLICTS)
    {

	# These are only available from version 0.41 (i/f version 8.x).

	return 1 if ($this->{mtn_aif_version} >= 8);

    }
    elsif ($feature == MTN_CONTENT_DIFF_EXTRA_OPTIONS
	   || $feature == MTN_FILE_MERGE
	   || $feature == MTN_LUA
	   || $feature == MTN_READ_PACKETS)
    {

	# These are only available from version 0.42 (i/f version 9.x).

	return 1 if ($this->{mtn_aif_version} >= 9);

    }
    elsif ($feature == MTN_M_SELECTOR || $feature == MTN_U_SELECTOR)
    {

	# These are only available from version 0.43 (i/f version 9.x).

	return 1 if ($this->{mtn_aif_version} >= 10
		     || (int($this->{mtn_aif_version}) == 9
			 && $mtn_version == 0.43));

    }
    elsif ($feature == MTN_COMMON_KEY_HASH || $feature == MTN_W_SELECTOR)
    {

	# These are only available from version 0.44 (i/f version 10.x).

	return 1 if ($this->{mtn_aif_version} >= 10);

    }
    elsif ($feature == MTN_HASHED_SIGNATURES)
    {

	# This is only available from version 0.45 (i/f version 11.x).

	return 1 if ($this->{mtn_aif_version} >= 11);

    }
    elsif ($feature == MTN_REMOTE_CONNECTIONS
	   || $feature == MTN_STREAM_IO
	   || $feature == MTN_SYNCHRONISATION)
    {

	# These are only available from version 0.46 (i/f version 12.x).

	return 1 if ($this->{mtn_aif_version} >= 12);

    }
    else
    {
	&$croaker("Unknown feature requested");
    }

    return;

}
#
##############################################################################
#
#   Routine      - suppress_utf8_conversion
#
#   Description  - Controls whether UTF-8 conversion should be done on the
#                  data sent to and from the mtn subprocess by this class.
#                  This is both a class as well as an object method. When used
#                  as a class method, the specified setting is used as the
#                  default for all those objects that do not specify their own
#                  setting. The default setting is to perform UTF-8
#                  conversion.
#
#   Data         - $self     : Either the object, the package name or not
#                              present depending upon how this method is
#                              called.
#                  $suppress : True if UTF-8 conversion is not to be done,
#                              otherwise false if it is.
#
##############################################################################



sub suppress_utf8_conversion($$)
{

    my($self,
       $this);
    if ($_[0]->isa(__PACKAGE__))
    {
	if (ref($_[0]) ne "")
	{
	    $self = shift();
	    $this = $class_records{$self->{$class_name}};
	}
	else
	{
	    shift();
	}
    }
    my $suppress = $_[0];

    if (defined($self))
    {
	$this->{convert_to_utf8} = $suppress ? undef : 1;
    }
    else
    {
	$convert_to_utf8 = $suppress ? undef : 1;
    }

    return;

}
#
##############################################################################
#
#   Routine      - switch_to_ws_root
#
#   Description  - Control whether this class automatically switches to a
#                  workspace's root directory before running the mtn
#                  subprocess. The default action is to do so as this is
#                  generally safer.
#
#   Data         - $self        : The object.
#                  $switch      : True if the mtn subprocess should be started
#                                 in a workspace's root directory, otherwise
#                                 false if it should be started in the current
#                                 working directory.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub switch_to_ws_root($$)
{

    my($self,
       $this);
    if ($_[0]->isa(__PACKAGE__))
    {
	if (ref($_[0]) ne "")
	{
	    $self = shift();
	    $this = $class_records{$self->{$class_name}};
	}
	else
	{
	    shift();
	}
    }
    my $switch = $_[0];

    if (defined($self))
    {
	if (! $this->{ws_constructed})
	{
	    if ($this->{cd_to_ws_root} && ! $switch)
	    {
		$this->{cd_to_ws_root} = undef;
		$self->closedown();
		$self->startup();
	    }
	    elsif (! $this->{cd_to_ws_root} && $switch)
	    {
		$this->{cd_to_ws_root} = 1;
		$self->closedown();
		$self->startup();
	    }
	}
	else
	{
	    $this->{error_msg} =
		"Cannot call Monotone::AutomateStdio->switch_to_ws_root() on "
		. "objects constructed with new_from_ws()";
	    &$carper($this->{error_msg});
	    return;
	}
    }
    else
    {
	$cd_to_ws_root = $switch ? 1 : undef;
    }

    return 1;

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
			  . "did not find it")
		    unless (exists($kv_record->{content}));
		push(@$list, {type    => "add_file",
			      name    => $kv_record->{add_file},
			      file_id => $kv_record->{content}});
	    }
	    elsif (exists($kv_record->{clear}))
	    {
		&$croaker("Corrupt revision, expected attr field but did not "
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
		&$croaker("Corrupt revision, expected from field but did not "
			  . "find it")
		    unless (exists($kv_record->{from}));
		&$croaker("Corrupt revision, expected to field but did not "
			  . "find it")
		    unless (exists($kv_record->{to}));
		push(@$list, {type         => "patch",
			      name         => $kv_record->{patch},
			      from_file_id => $kv_record->{from},
			      to_file_id   => $kv_record->{to}});
	    }
	    elsif (exists($kv_record->{rename}))
	    {
		&$croaker("Corrupt revision, expected to field but did not "
			  . "find it")
		    unless (exists($kv_record->{to}));
		push(@$list, {type      => "rename",
			      from_name => $kv_record->{rename},
			      to_name   => $kv_record->{to}});
	    }
	    elsif (exists($kv_record->{set}))
	    {
		&$croaker("Corrupt revision, expected attr field but did not "
			  . "find it")
		    unless (exists($kv_record->{attr}));
		&$croaker("Corrupt revision, expected value field but did not "
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
#   Data         - $self        : The object.
#                  $cmd         : The mtn automate command that is to be run.
#                  $out_as_utf8 : True if any data output to mtn should be
#                                 converted into raw UTF-8, otherwise false if
#                                 the data should be treated as binary. If
#                                 UTF-8 conversion has been disabled by a call
#                                 to the suppress_utf8_conversion() method
#                                 then this argument is ignored.
#                  $in_as_utf8  : True if any data input from mtn should be
#                                 converted into Perl's internal UTF-8 string
#                                 format, otherwise false if the data should
#                                 be treated as binary. If UTF-8 conversion
#                                 has been disabled by a call to the
#                                 suppress_utf8_conversion() method then this
#                                 argument is ignored.
#                  $ref         : A reference to a buffer or an array that is
#                                 to contain the output from this command.
#                  @parameters  : A list of parameters to be applied to the
#                                 command.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub mtn_command($$$$$;@)
{

    my($self, $cmd, $out_as_utf8, $in_as_utf8, $ref, @parameters) = @_;

    return $self->mtn_command_with_options($cmd,
					   $out_as_utf8,
					   $in_as_utf8,
					   $ref,
					   [],
					   @parameters);

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
#   Data         - $self        : The object.
#                  $cmd         : The mtn automate command that is to be run.
#                  $out_as_utf8 : True if any data output to mtn should be
#                                 converted into raw UTF-8, otherwise false if
#                                 the data should be treated as binary. If
#                                 UTF-8 conversion has been disabled by a call
#                                 to the suppress_utf8_conversion() method
#                                 then this argument is ignored.
#                  $in_as_utf8  : True if any data input from mtn should be
#                                 converted into Perl's internal UTF-8 string
#                                 format, otherwise false if the data should
#                                 be treated as binary. If UTF-8 conversion
#                                 has been disabled by a call to the
#                                 suppress_utf8_conversion() method then this
#                                 argument is ignored.
#                  $ref         : A reference to a buffer or an array that is
#                                 to contain the output from this command.
#                  $options     : A reference to a list containing key/value
#                                 anonymous hashes.
#                  @parameters  : A list of parameters to be applied to the
#                                 command.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub mtn_command_with_options($$$$$$;@)
{

    my($self, $cmd, $out_as_utf8, $in_as_utf8, $ref, $options, @parameters)
	= @_;

    my($buffer,
       $buffer_ref,
       $db_locked_exception,
       $handler,
       $handler_data,
       $opt,
       $param,
       $read_ok,
       $retry);
    my $this = $class_records{$self->{$class_name}};

    # Work out whether UTF-8 conversion is to be done at all.

    $out_as_utf8 = $in_as_utf8 = undef unless ($this->{convert_to_utf8});

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
    # then we need to read the output into a temporary buffer before breaking
    # it up into lines.

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

	$self->startup() if ($this->{mtn_pid} == 0);

	# Send the command.

	if (scalar(@$options) > 0)
	{
	    $this->{mtn_in}->print("o");
	    foreach $opt (@$options)
	    {
		my($key,
		   $key_ref,
		   $value,
		   $value_ref);
		if ($out_as_utf8)
		{
		    $key = encode_utf8($opt->{key});
		    $value = encode_utf8($opt->{value});
		    $key_ref = \$key;
		    $value_ref = \$value;
		}
		else
		{
		    $key_ref = \$opt->{key};
		    $value_ref = \$opt->{value};
		}
		$this->{mtn_in}->printf("%d:%s%d:%s",
					length($$key_ref),
					$$key_ref,
					length($$value_ref),
					$$value_ref);
	    }
	    $this->{mtn_in}->print("e ");
	}
	$this->{mtn_in}->printf("l%d:%s", length($cmd), $cmd);
	foreach $param (@parameters)
	{

	    # Cater for passing by reference (useful when sending large lumps
	    # of data as in put_file). Also defend against undef being passed
	    # as the only parameter (which can happen when a mandatory argument
	    # is not passed by the caller).

	    if (defined $param)
	    {
		my($data,
		   $param_ref);
		if (ref($param) ne "")
		{
		    if ($out_as_utf8)
		    {
			$data = encode_utf8($$param);
			$param_ref = \$data;
		    }
		    else
		    {
			$param_ref = $param;
		    }
		}
		else
		{
		    if ($out_as_utf8)
		    {
			$data = encode_utf8($param);
			$param_ref = \$data;
		    }
		    else
		    {
			$param_ref = \$param;
		    }
		}
		$this->{mtn_in}->printf("%d:%s",
					length($$param_ref),
					$$param_ref);
	    }

	}
	$this->{mtn_in}->print("e\n");

	# Attempt to read the output of the command, rethrowing any exception
	# that does not relate to locked databases.

	$db_locked_exception = $read_ok = $retry = undef;
	eval
	{
	    $read_ok = $self->mtn_read_output($buffer_ref);
	};
	if ($@)
	{
	    if ($@ =~ m/$database_locked_re/)
	    {

		# We need to properly closedown the mtn subprocess at this
		# point because we are quietly handling the exception that
		# caused it to exit but the calling application may reap the
		# process and compare the reaped PID with the return value from
		# the get_pid() method. At least by calling closedown() here
		# get_pid() will return 0 and the caller can then distinguish
		# between a handled exit and one that should be dealt with.

		$self->closedown();
		$db_locked_exception = 1;

	    }
	    else
	    {
		&$croaker($@);
	    }
	}

	# If the data was read in ok then carry out any necessary character set
	# conversions. Otherwise deal with locked database exceptions and any
	# warning messages that appeared in the output.

	if ($read_ok && $in_as_utf8)
	{
	    local $@;
	    eval
	    {
		$$buffer_ref = decode_utf8($$buffer_ref, Encode::FB_CROAK);
	    };
	    if ($@)
	    {
		$this->{error_msg} = "The output from Monotone was not UTF-8 "
		    . "encoded as expected";
		&$carper($this->{error_msg});
		return;
	    }
	}
	elsif (! $read_ok)
	{

	    # See if we are to retry on database locked conditions.

	    if ($db_locked_exception
		|| $this->{error_msg} =~ m/$database_locked_re/)
	    {
		$this->{db_is_locked} = 1;
		$retry = &$handler($self, $handler_data);
	    }

	    # If we are to retry then close down the subordinate mtn process,
	    # otherwise report the error to the caller.

	    if ($retry)
	    {
		$self->closedown();
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

    @$ref = split(/\n/, $$buffer_ref) if (ref($ref) eq "ARRAY");

    return 1;

}
#
##############################################################################
#
#   Routine      - mtn_read_output_format_1
#
#   Description  - Reads the output from mtn as format 1, removing chunk
#                  headers.
#
#   Data         - $self        : The object.
#                  $buffer      : A reference to the buffer that is to contain
#                                 the data.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub mtn_read_output_format_1($$)
{

    my($self, $buffer) = @_;

    my($bytes_read,
       $char,
       $chunk_start,
       $cmd_nr,
       $colons,
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
    my $this = $class_records{$self->{$class_name}};

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
    $last = "m";
    $offset = 0;
    do
    {

	# Wait here for some data, calling the I/O wait handler every second
	# whilst we wait.

	while ($this->{poll}->poll($handler_timeout) == 0)
	{
	    &$handler($self, $handler_data);
	}

	# If necessary, read in and process the chunk header, then we know how
	# much to read in.

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
			      . join("", $this->{mtn_err}->getlines()));
		    }
		}
		elsif ($char =~ m/\D$/)
		{
		    croak("Corrupt/missing mtn chunk header, mtn gave:\n"
			  . join("", $this->{mtn_err}->getlines()));
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
		      . join("", $this->{mtn_err}->getlines()));
	    }

	    $chunk_start = undef;

	}

	# Read in what we require.

	if ($size > 0)
	{
	    if (! defined($bytes_read = sysread($this->{mtn_out},
						$$buffer,
						$size,
						$offset)))
	    {
		croak("sysread failed: " . $!);
	    }
	    elsif ($bytes_read == 0)
	    {
		croak("Short data read");
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
#   Routine      - mtn_read_output_format_2
#
#   Description  - Reads the output from mtn as format 2, removing chunk
#                  headers.
#
#   Data         - $self        : The object.
#                  $buffer      : A reference to the buffer that is to contain
#                                 the data.
#                  Return Value : True on success, otherwise false on failure.
#
##############################################################################



sub mtn_read_output_format_2($$)
{

    my($self, $buffer) = @_;

    my($bytes_read,
       $buffer_ref,
       $char,
       $chunk_start,
       $cmd_nr,
       $colons,
       $err_code,
       $err_occurred,
       $handler,
       $handler_data,
       $handler_timeout,
       $header,
       $i,
       $offset_ref,
       $size,
       $stream);
    my $this = $class_records{$self->{$class_name}};
    my %details = (e => {buffer_ref => undef,
			 offset     => 0},
		   l => {buffer_ref => undef,
			 offset     => 0},
		   m => {buffer_ref => undef,
			 offset     => 0},
		   p => {buffer_ref => undef,
			 offset     => 0,
			 handle     => $this->{p_stream_handle},
		         used       => undef},
		   t => {buffer_ref => undef,
			 offset     => 0,
			 handle     => $this->{t_stream_handle},
		         used       => undef},
		   w => {buffer_ref => undef,
			 offset     => 0});

    # Create the buffers.

    foreach my $key (CORE::keys(%details))
    {
	if ($key eq "m")
	{
	    $details{$key}->{buffer_ref} = $buffer;
	}
	else
	{
	    my $ref_buf = "";
	    $details{$key}->{buffer_ref} = \$ref_buf;
	}
    }

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
    $buffer_ref = $details{m}->{buffer_ref};
    $offset_ref = \$details{m}->{offset};
    do
    {

	# Wait here for some data, calling the I/O wait handler every second
	# whilst we wait.

	while ($this->{poll}->poll($handler_timeout) == 0)
	{
	    &$handler($self, $handler_data);
	}

	# If necessary, read in and process the chunk header, then we know how
	# much to read in.

	if ($chunk_start)
	{

	    # Read header, one byte at a time until we have what we need or
	    # there is an error.

	    for ($header = "", $colons = $i = 0;
		 $colons < 3 && sysread($this->{mtn_out}, $header, 1, $i);
		 ++ $i)
	    {
		$char = substr($header, $i, 1);
		if ($char eq ":")
		{
		    ++ $colons;
		}
		elsif ($colons == 1)
		{
		    if ($char !~ m/^[elmptw]$/)
		    {
			croak("Corrupt/missing mtn chunk header, mtn gave:\n"
			      . join("", $this->{mtn_err}->getlines()));
		    }
		}
		elsif ($char =~ m/\D$/)
		{
		    croak("Corrupt/missing mtn chunk header, mtn gave:\n"
			  . join("", $this->{mtn_err}->getlines()));
		}
	    }

	    # Break out the header into its separate fields.

	    if ($header =~ m/^(\d+):([elmptw]):(\d+):$/)
	    {
		($cmd_nr, $stream, $size) = ($1, $2, $3);
		if ($cmd_nr != $this->{cmd_cnt})
		{
		    croak("Mtn command count is out of sequence");
		}
	    }
	    else
	    {
		croak("Corrupt/missing mtn chunk header, mtn gave:\n"
		      . join("", $this->{mtn_err}->getlines()));
	    }

	    # Set up the current buffer and offset details.

	    $buffer_ref = $details{$stream}->{buffer_ref};
	    $offset_ref = \$details{$stream}->{offset};

	    $chunk_start = undef;

	}

	# Read in what we require.

	if ($stream ne "l")
	{

	    # Process non-last messages.

	    if ($size > 0)
	    {

		# Process the current data chunk.

		if (! defined($bytes_read = sysread($this->{mtn_out},
						    $$buffer_ref,
						    $size,
						    $$offset_ref)))
		{
		    croak("sysread failed: " . $!);
		}
		elsif ($bytes_read == 0)
		{
		    croak("Short data read");
		}
		$size -= $bytes_read;
		$$offset_ref += $bytes_read;

	    }
	    else
	    {

		# We have finished processing the current data chunk so if it
		# belongs to a stream that is to be redirected to a file handle
		# then send the data down it.

		if ($stream =~ m/^[pt]$/
		    && defined($details{$stream}->{handle}))
		{

		    # Send the headers as well so as to help the reader.

		    if (! $details{$stream}->{handle}->print($header
							     . $$buffer_ref))
		    {
			croak("print failed: " . $!);
		    }
		    $details{$stream}->{used} = 1;
		    $$buffer_ref = "";
		    $$offset_ref = 0;

		}

		$chunk_start = 1;

	    }
	}
	elsif ($size == 1)
	{

	    my $last_msg;

	    # Process the last message.

	    if (! sysread($this->{mtn_out}, $err_code, 1))
	    {
		croak("sysread failed: " . $!);
	    }
	    $size = 0;
	    if ($err_code != 0)
	    {
		$err_occurred = 1;
	    }

	    # Send the terminating last message down any stream file handle
	    # that had data sent down it.

	    $last_msg = $header . $err_code;
	    foreach my $ostream ("p", "t")
	    {
		if ($details{$ostream}->{used})
		{
		    if (! $details{$ostream}->{handle}->print($last_msg))
		    {
			croak("print failed: " . $!);
		    }
		}
	    }

	}
	else
	{
	    croak("Invalid message state");
	}

    }
    while ($size > 0 || $stream ne "l");

    ++ $this->{cmd_cnt};

    # Record any error or warning messages.

    if (${$details{e}->{buffer_ref}} ne "")
    {
	$this->{error_msg} = ${$details{e}->{buffer_ref}};
    }
    elsif (${$details{w}->{buffer_ref}} ne "")
    {
	$this->{error_msg} = ${$details{w}->{buffer_ref}};
    }

    # If something has gone wrong then deal with it.

    if ($err_occurred)
    {
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
#   Data         - $self : The object.
#
##############################################################################



sub startup($)
{

    my $self = $_[0];

    my $this = $class_records{$self->{$class_name}};

    if ($this->{mtn_pid} == 0)
    {

	my(@args,
	   $cwd,
	   $file,
	   $exception,
	   $header_err,
	   $line,
	   $my_pid,
	   $version);

	# Switch to the default locale. We only want to parse the output from
	# Monotone in one language!

	local $ENV{LC_ALL} = "C";
	local $ENV{LANG} = "C";

	# Don't allow SIGPIPE signals to terminate the calling program (any
	# related errors are dealt with anyway).

	$SIG{PIPE} = "IGNORE";

	$this->{db_is_locked} = undef;
	$this->{mtn_err} = gensym();

	# Build up a list of command line arguments to pass to the mtn
	# subprocess.

	@args = ("mtn");
	push(@args, "--db=" . $this->{db_name}) if (defined($this->{db_name}));
	push(@args, "--quiet") if (defined($this->{network_service}));
	push(@args, "--ignore-suspend-certs")
	    if (! $this->{honour_suspend_certs});
	push(@args, @{$this->{mtn_options}});
	if (defined($this->{network_service}))
	{
	    push(@args, "automate", "remote_stdio", $this->{network_service});
	}
	else
	{
	    push(@args, "automate", "stdio");
	}

	# Actually start the mtn subprocess. If a database name has been
	# provided then run the mtn subprocess in the system's root directory
	# so as to avoid any database/workspace clash. Likewise if a workspace
	# has been provided then run the mtn subprocess in the base directory
	# of that workspace (although in this case the caller can override this
	# feature if it wishes to do so).

	$cwd = getcwd();
	$my_pid = $$;
	eval
	{
	    if (defined($this->{db_name}) || defined($this->{network_service}))
	    {
		die("chdir failed: " . $!)
		    unless (chdir(File::Spec->rootdir()));
	    }
	    elsif ($this->{cd_to_ws_root} && defined($this->{ws_path}))
	    {
		die("chdir failed: " . $!) unless (chdir($this->{ws_path}));
	    }
	    $this->{mtn_pid} = open3($this->{mtn_in},
				     $this->{mtn_out},
				     $this->{mtn_err},
				     @args);
	};
	$exception = $@;
	chdir($cwd);

	# Check for errors (remember that open3() errors can happen in both the
	# parent and child processes).

	if ($exception)
	{
	    if ($$ != $my_pid)
	    {

		# In the child process so all we can do is complain and exit.

		STDERR->print("open3 failed: " . $exception . "\n");
		exit(1);

	    }
	    else
	    {

		# In the parent process so deal with the error in the usual
		# way.

		&$croaker($exception);

	    }
	}

	# Ok so reset the command count and setup polling.

	$this->{cmd_cnt} = 0;
	$this->{poll} = IO::Poll->new();
	$this->{poll}->mask($this->{mtn_out}, POLLIN | POLLPRI | POLLHUP);

	# If necessary get the version of the actual application.

	if (! defined($mtn_version))
	{
	    &$croaker("Could not run command `mtn --version'")
		unless (defined($file = IO::File->new("mtn --version |")));
	    while (defined($line = $file->getline()))
	    {
		if ($line =~ m/^monotone (\d+\.\d+) ./)
		{
		    $mtn_version = $1;
		}
	    }
	    $file->close();
	    &$croaker("Could not determine the version of Monotone being used")
		unless (defined($mtn_version));
	}

	# If the version is higher than 0.45 then we need to skip the header
	# which is terminated by two blank lines (put any errors into
	# $header_err as we need to defer any error reporting until later).

	if ($mtn_version > 0.45)
	{
	    local $@;
	    eval
	    {

		my($char,
		   $last_char);

		# If we are connecting to a network service then make sure that
		# it has sent us something before doing a blocking read.

		if (defined($this->{network_service}))
		{
		    my $poll_result;
		    for (my $i = 0;
			 $i < 10
			     && ($poll_result =
				 $this->{poll}->poll($io_wait_handler_timeout))
			     == 0;
			 ++ $i)
		    {
			&$io_wait_handler($self, $io_wait_handler_data);
		    }
		    if ($poll_result == 0)
		    {
			$header_err = "Cannot connect to service `"
			    . $this->{network_service} . "'";
			die(1);
		    }
		}

		# Skip the header.

		$char = "";
		do
		{
		    $last_char = $char;
		    if (! sysread($this->{mtn_out}, $char, 1))
		    {
			$header_err = "Cannot get format header";
			die(1);
		    }
		}
		while ($char ne "\n" || $last_char ne "\n");

	    };
	}

	# Set up the correct input handler depending upon the version of mtn.

	if ($mtn_version > 0.45)
	{
	    *mtn_read_output = *mtn_read_output_format_2;
	}
	else
	{
	    *mtn_read_output = *mtn_read_output_format_1;
	}

	# Get the interface version (remember also that if something failed
	# above then this method will throw and exception giving the cause).

	$self->interface_version(\$version);
	if ($version =~ m/^(\d+)\.(\d+)$/)
	{
	    $this->{mtn_aif_version} = $1;
	}
	else
	{
	    &$croaker("Cannot get automate stdio interface version number");
	}

	# This should never happen as getting the interface version would have
	# reported the real issue, but handle any header read issues just in
	# case.

	&$croaker($header_err) if (defined($header_err));

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
#   Routine      - create_object
#
#   Description  - Actually creates a Monotone::AutomateStdio object.
#
#   Data         - $class       : The name of the class that the new object
#                                 should be blessed as.
#                  Return Value : A new Monotone::AutomateStdio object.
#
##############################################################################



sub create_object($)
{

    my $class = $_[0];

    my ($counter,
	$id,
	$self,
	$this);

    # Create the object's data record.

    $this = {db_name                 => undef,
	     ws_path                 => undef,
	     network_service         => undef,
	     ws_constructed          => undef,
	     cd_to_ws_root           => $cd_to_ws_root,
	     convert_to_utf8         => $convert_to_utf8,
	     mtn_options             => undef,
	     mtn_pid                 => 0,
	     mtn_in                  => undef,
	     mtn_out                 => undef,
	     mtn_err                 => undef,
	     poll                    => undef,
	     error_msg               => "",
	     honour_suspend_certs    => 1,
	     mtn_aif_version         => undef,
	     cmd_cnt                 => 0,
	     p_stream_handle         => undef,
	     t_stream_handle         => undef,
	     db_is_locked            => undef,
	     db_locked_handler       => undef,
	     db_locked_handler_data  => undef,
	     io_wait_handler         => undef,
	     io_wait_handler_data    => undef,
	     io_wait_handler_timeout => 1};

    # Create the actual object and a unique key (using rand() and duplication
    # detection), then store this unique key in the object in a field named
    # after this class for later reference.

    $self = bless({}, $class);
    $counter = 0;
    do
    {
	$id = int(rand(INT_MAX));
	&$croaker("Exhausted unique object keys")
	    if ((++ $counter) == INT_MAX);
    }
    while (exists($class_records{$id}));
    $self->{$class_name} = $id;

    # Now file the object's record in the records store, filed under the
    # object's unique key.

    $class_records{$id} = $this;

    return $self;

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
    croak(__PACKAGE__ . ": Fatal error");

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

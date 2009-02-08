##############################################################################
#
#   File Name    - Installer.pm
#
#   Description  - Class module that provides a simple installer.
#
#   Author       - A.E.Cooper.
#
#   Legal Stuff  - Copyright (c) 2009 Anthony Edward Cooper
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
#   Package      - Installer
#
#   Description  - See above.
#
##############################################################################



# ***** PACKAGE DECLARATION *****

package Installer;

# ***** DIRECTIVES *****

require 5.008005;

use integer;
use strict;
use warnings;

# ***** REQUIRED PACKAGES *****

# Standard Perl and CPAN modules.

use Carp;
use File::Copy;
use File::Spec;

# ***** FUNCTIONAL PROTOTYPES *****

# Public methods.

sub install($$$;$);
sub new($$$$$$);

# ***** PACKAGE INFORMATION *****

# We are just a base class.

use base qw(Exporter);

our @EXPORT = qw();
our @EXPORT_OK = qw();
our $VERSION = 0.1;
#
##############################################################################
#
#   Routine      - new
#
#   Description  - Class constructor.
#
#   Data         - $class       : Either the name of the class that is to be
#                                 created or an object of that class.
#                  $owner       : The owner id for any destination files and
#                                 directories.
#                  $group       : The group id for any destination files and
#                                 directories.
#                  $dir_perms   : Permissions for any created directories.
#                  $exec_perms  : Permissions for any created executable
#                                 files.
#                  $nexec_perms : Permissions for any created non-executable
#                                 files.
#                  Return Value : A reference to the newly created object.
#
##############################################################################



sub new($$$$$$)
{


    my $class = (ref($_[0]) ne "") ? ref($_[0]) : $_[0];
    shift();
    my($owner, $group, $dir_perms, $exec_perms, $nexec_perms) = @_;

    my $this = {owner          => $owner,
		group          => $group,
		dir_perms      => $dir_perms,
		exec_perms     => $exec_perms,
		non_exec_perms => $nexec_perms};
    bless($this, $class);

    return $this;

}
#
##############################################################################
#
#   Routine      - install
#
#   Description  - Install the specified file to the specified location.
#
#   Data         - $this      : The object.
#                  $src_file  : The name of the file to be installed.
#                  $dest_file : The name of where the file is to be installed
#                               to. Single `.' file names are allowed and are
#                               taken to mean same file name.
#                  $perms     : The file permissions for the target file or
#                               undef the default file permssions should be
#                               used. This is optional.
#
##############################################################################



sub install($$$;$)
{

    my($this, $src_file, $dest_file, $perms) = @_;

    my($file,
       $full_path,
       @dirs,
       $path,
       $vol);

    # Is the source file readable?

    croak("Source file `" . $src_file . "' does not exist or is unreadable")
	unless (-r $src_file);

    # Deal with the destination directory path.

    $dest_file = File::Spec->rel2abs($dest_file);
    ($vol, $path) = (File::Spec->splitpath($dest_file, 1))[0, 1];
    @dirs = File::Spec->splitdir($path);
    $file = pop(@dirs);
    $path = "";
    foreach my $dir (@dirs)
    {
	$path = File::Spec->catdir($path, $dir);
	$full_path = File::Spec->catpath($vol, $path, "");
	if (! -d $full_path)
	{
	    mkdir($full_path)
		or croak ("mkdir " . $full_path . "failed with: " . $!);
	    chmod($this->{dir_perms}, $full_path)
		or croak ("chmod " . $full_path . "failed with: " . $!);
	    chown($this->{owner}, $this->{group}, $full_path);
	}
    }

    # Copy the file across.

    $full_path = File::Spec->catpath($vol, $path, $file);
    copy($src_file, $full_path)
	or croak("copy " . $src_file . " " . $full_path . " failed with: "
		 . $!);
    if (! defined($perms))
    {
	if (-x $src_file)
	{
	    $perms = $this->{exec_perms};
	}
	else
	{
	    $perms = $this->{non_exec_perms};
	}
    }
    chmod($perms, $full_path)
	or croak ("chmod " . $full_path . "failed with: " . $!);
    chown($this->{owner}, $this->{group}, $full_path);

}

1;

##############################################################################
#
#   File Name    - Globals.pm
#
#   Description  - The global data module for the mtn-browse application. This
#                  module contains all the global constants and variables used
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
#   Package      - Globals
#
#   Description  - See above.
#
##############################################################################



# ***** PACKAGE DECLARATION *****

package Globals;

# ***** DIRECTIVES *****

require 5.008005;

use strict;
use warnings;

# ***** GLOBAL DATA DECLARATIONS *****

# Constants for the name and version of this application.

use constant APPLICATION_NAME    => "mtn-browse";
use constant APPLICATION_VERSION => 0.62;

# Constants used to represent the different groups of widgets.

use constant BRANCH           => 0x01;
use constant DIRECTORY        => 0x02;
use constant DIRECTORY_VIEW   => 0x04;
use constant DISPLAY_OF_FILE  => 0x08;
use constant REVISION         => 0x10;
use constant REVISION_LIST    => 0x02;
use constant REVISION_DETAILS => 0x04;

# Constants used to represent the different state changes. Read this as
# `what has just been changed' => `what needs to be updated'.

use constant ALL_CHANGED               => 0xff;
use constant BRANCH_CHANGED            => (REVISION | DIRECTORY
					   | DIRECTORY_VIEW | DISPLAY_OF_FILE);
use constant DATABASE_CHANGED          => 0xff;
use constant DIRECTORY_CHANGED         => (DIRECTORY_VIEW | DISPLAY_OF_FILE);
use constant FILE_CHANGED              => (DISPLAY_OF_FILE);
use constant NEW_FIND                  => 0xff;
use constant REVISION_CHANGED          => (DIRECTORY | REVISION_LIST
					   | DIRECTORY_VIEW | DISPLAY_OF_FILE);
use constant SELECTED_REVISION_CHANGED => (REVISION_DETAILS);

# Location of the Glade UI XML file for mtn-browse.

our $glade_file;

# The tooltips widget.

our $tooltips;

# The mono-spaced font used for displaying text files.

our $mono_font;

# Assorted pixmaps.

our $line_image;

# Location of the temporary working directory.

our $tmp_dir;

# The user's preferences data.

our $user_preferences;

# The MIME type file name pattern match table.

our $mime_match_table;

# Whether Monotone warnings should be suppressed or not.

our $suppress_mtn_warnings;

# ***** PACKAGE INFORMATION *****

use base qw(Exporter);

our %EXPORT_TAGS = (constants => [qw(ALL_CHANGED
				     APPLICATION_NAME
				     APPLICATION_VERSION
				     BRANCH
				     BRANCH_CHANGED
				     DATABASE_CHANGED
				     DIRECTORY
				     DIRECTORY_CHANGED
				     DIRECTORY_VIEW
				     DISPLAY_OF_FILE
				     FILE_CHANGED
				     NEW_FIND
				     REVISION
				     REVISION_CHANGED
				     REVISION_DETAILS
				     REVISION_LIST
				     SELECTED_REVISION_CHANGED)],
		    variables => [qw($glade_file
				     $line_image
				     $mime_match_table
				     $mono_font
				     $suppress_mtn_warnings
				     $tmp_dir
				     $tooltips
				     $user_preferences)]);
our @EXPORT = qw();
Exporter::export_ok_tags(qw(constants variables));
our $VERSION = 0.1;

1;

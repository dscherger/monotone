##############################################################################
#
#   File Name    - LocaleEnableUtf8.pm
#
#   Description  - The locale enable UTF-8 module for the mtn-browse
#                  application. This module contains code that basically
#                  enables the handling of UTF-8 with the Locale::TextDomain
#                  library. This module should be included before any module
#                  that uses Locale::TextDomain (to get around the situation
#                  where translations are performed on global strings).
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

BEGIN
{
    Locale::Messages::bind_textdomain_filter
	(APPLICATION_NAME,
	 sub {
	     my $utf_8 = decode_utf8($_[0]);
	     return defined($utf_8) ? $utf_8 : $_[0];
	 });
    Locale::Messages::bind_textdomain_codeset(APPLICATION_NAME, "utf-8");
}

1;

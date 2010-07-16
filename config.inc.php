<?php

$CFG = array();

// Must be writable by the www user
$CFG["cache_dir"] = "/data/monotone.ca/cache";

$CFG["aggregated_feeds"] = array(
    "http://www.thomaskeller.biz/blog/category/coding/monotone/feed/",
    "http://tero.stronglytyped.org/blosxom.cgi/monotone/index.atom",
    "http://identi.ca/lapo/tag/monotone/rss",
);

$CFG['new_msgs_from_feeds_count'] = 3;

$CFG["download_dir"] = dirname(__FILE__) . "/downloads";

$CFG["download_webdir"] = "/downloads";

// This assumes that all packages for a specific type
// follow a distinct file naming. %version% is replaced with
// the specific monotone version which is tried to match
// The ordering applied here is kept for the final list!
$CFG["download_matchers"] = array(
    "Tarball"               => "/monotone-%version%\.tar\.gz/",
    "Linux x86/glibc 2.3"   => "/mtn-%version%-linux-x86\.bz2/",
    "Windows Installer"     => "/monotone-%version%-setup\.exe/",
    "Mac OS X Installer"    => "/monotone-%version%\.dmg/",
    "Mac OS X Binary"       => "/mtn-%version%-osx(-univ)?\.bz2/",
    "Solaris Package"       => "/PMmonotone-%version%\.(i386|sparc)\.pkg/",
);

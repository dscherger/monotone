<?php

/**
 * Simple PHP script which loops over all released monotone
 * packages, sorted by directory name, and creates links to
 * the most recent package for a specific platform
 *
 * Author: Thomas Keller <me@thomaskeller.biz>
 * Copyleft: GPLv3
 */

// this assumes that all packages for a specific platform
// follow a distinct file naming. %version% is replaced with
// the specific monotone version which is tried to match
$matchers = array(
    "Tarball"               => "/monotone-%version%\.tar\.gz/",
    "Linux x86/glibc 2.3"   => "/mtn-%version%-linux-x86\.bz2/",
    "Mac OS X Installer"    => "/monotone-%version%\.dmg/",
    "Mac OS X Binary"       => "/mtn-%version%-osx(-univ)?\.bz2/",
    "Solaris Package"       => "/PMmonotone-%version%\.(i386|sparc)\.pkg/",
    "Windows Installer"     => "/monotone-%version%-setup\.exe/",
);

$webdir           = "/downloads";
$basedir          = dirname(__FILE__) . "/downloads";
$releaseDirs 	  = scandir($basedir, 1);
$latestFiles 	  = array_flip(array_keys($matchers));
$matchedPlatforms = array();

foreach ($releaseDirs as $dir)
{
    if (!is_dir("$basedir/$dir") || $dir == "." || $dir == "..")
        continue;

    if (count($matchedPlatforms) == count($matchers))
        break;

    $files = scandir("$basedir/$dir", 1);
    $release = $dir;
    $newlyMatchedPlatforms = array();

    foreach ($files as $file)
    {
        foreach ($matchers as $platform => $matcher)
        {

            if (preg_match(str_replace("%version%", $release, $matcher), $file) &&
                !in_array($platform, $matchedPlatforms))
            {
                if (!is_array($latestFiles[$platform]))
                {
                    $latestFiles[$platform] = array();
                }
                $latestFiles[$platform][] = "$release/$file";
                $newlyMatchedPlatforms[] = $platform;
            }
        }
    }
    $matchedPlatforms = array_merge($matchedPlatforms, $newlyMatchedPlatforms);
}

?>
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.1//EN" "http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd">
<html lang="en" xml:lang="en"  xmlns="http://www.w3.org/1999/xhtml">
<head>
<title>monotone: Downloads</title>
<link type="text/css" rel="stylesheet" href="../res/styles.css" />
<!--[if IE 7]>
<link rel="stylesheet" type="text/css" href="../res/ie7.css" />
<![endif]-->
<link rel="alternate" type="application/rss+xml" title="monotone releases"
    href="http://monotone.ca/releases.xml" />
<style type="text/css">
.getit {
    font-size: 1em;
    padding-bottom: 0.2em;
    padding-left: 2.5em;
    padding-top: 0.7em;
}
</style>
</head>
<body>

<div id="header">
<p>
<img src="../res/logo.png" alt="monotone logo"/>
<strong>monotone</strong> is a free distributed version control system.
It provides a simple, single-file transactional version store, with
fully disconnected operation and an efficient peer-to-peer
synchronization protocol. It understands history-sensitive merging,
lightweight branches, integrated code review and 3rd party testing.
It uses cryptographic version naming and client-side RSA certificates.
It has good internationalization support, runs on Linux, Solaris,
Mac OS X, Windows, and other unixes, and is licensed under the GNU GPL.
</p>
<div class="clearfloat"></div>
</div>

<div id="body">
<h1 class="getit">Latest monotone downloads by platform</h1>
<dl><?php
foreach ($latestFiles as $platform => $files)
{
    if (!is_array($files))
        continue;

    echo<<<END
    <dt>$platform</dt>
END;
    foreach ($files as $file)
    {
        $name = basename($file);
        $release = dirname($file);
        echo <<<END
    <dd>
        <a href="$webdir$file">$name</a><br/>
        <span style="font-size:75%"<a href="$webdir$release/">&#187; more packages for $release</a></span>
    </dd>
END;
    }
}
?></dl>
</div>
</body>
</html>


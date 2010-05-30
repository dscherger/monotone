<?php

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

function getLatestFiles()
{
    global $matchers, $webdir, $basedir, $releaseDirs;

    $latestFiles = array();
    $matchedPlatforms = array();

    foreach ($releaseDirs as $dir)
    {
        if (!is_dir("$basedir/$dir") || $dir == "." || $dir == "..")
            continue;

        // a little optimization
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
                    if (!isset($latestFiles[$platform]))
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

    return $latestFiles;
}

function getAllFiles($platform)
{
    global $matchers, $webdir, $basedir, $releaseDirs;

    $matcher = $matchers[$platform];
    $allFiles = array();

    foreach ($releaseDirs as $dir)
    {
        if (!is_dir("$basedir/$dir") || $dir == "." || $dir == "..")
            continue;

        $files = scandir("$basedir/$dir", 1);
        $release = $dir;

        foreach ($files as $file)
        {
            if (preg_match(str_replace("%version%", $release, $matcher), $file))
            {
                $allFiles[] = "$release/$file";
            }
        }
    }

    rsort($allFiles);
    return $allFiles;
}

$page_title = "Downloads";
require_once("header.inc.php");

?>
<div class="boxes">
    <div class="box box-wide">
        <h1 class="getit">Latest monotone downloads by platform</h1>

<?php

$platform = isset($_GET['platform']) &&
            array_key_exists($_GET['platform'], $matchers) ?
            $_GET['platform'] : "";

if (empty($platform)):

      $latestFiles = getLatestFiles();

      if (count($latestFiles) == 0): ?>
        <p>No files found</p>
<?php else: ?>
        <dl><?php
        foreach ($latestFiles as $platform => $files):
            if (!is_array($files))
                continue;

            echo<<<END
            <dt>$platform</dt>
END;
            foreach ($files as $file):
                $name = basename($file);
                $release = dirname($file);
                $size = round(filesize("$basedir/$file") / (1024*1024), 2)."MB";
                $sha1 = sha1_file("$basedir/$file");
                echo <<<END
            <dd>
                <a href="$webdir/$file">$name</a> <small>($size, SHA1 <code>$sha1</code>)</small><br/>
                <span style="font-size:75%"><a href="?platform=$platform">&#187; all packages for this platform</a></span>
            </dd>
END;
            endforeach;
        endforeach;
        ?></dl>
<?php endif;
else:

        $allFiles = getAllFiles($platform);
        if (count($allFiles) == 0): ?>
        <p>No files found</p>
<?php else: ?>
        <dl><?php
        echo<<<END
            <dt>$platform</dt>
END;
        foreach ($allFiles as $file):
            $name = basename($file);
            $release = dirname($file);
            $size = round(filesize("$basedir/$file") / (1024*1024), 2)."MB";
            $sha1 = sha1_file("$basedir/$file");
            echo <<<END
            <dd>
                <a href="$webdir/$file">$name</a> <small>($size, SHA1 <code>$sha1</code>)</small><br/>
            </dd>
END;
        endforeach;
        ?></dl>
<?php endif; ?>

    <p style="font-size:75%"><a href="downloads.php">&#171; back to overview</a></p>

<?php endif; ?>
    </div>
</div>
<?php
require_once("footer.inc.php");
?>


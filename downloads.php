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

$page_title = "Downloads";
require_once("header.inc.php");

?>
<div class="boxes">
    <div class="box box-wide">
        <h1 class="getit">Latest monotone downloads by platform</h1>

<?php if (count($matchedPlatforms) == 0): ?>
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
                echo <<<END
            <dd>
                <a href="$webdir/$file">$name</a><br/>
                <span style="font-size:75%"<a href="$webdir/$release/">&#187; more packages for $release</a></span>
            </dd>
END;
            endforeach;
        endforeach;
        ?></dl>
<?php endif; ?>
    </div>
</div>
<?php
require_once("footer.inc.php");
?>


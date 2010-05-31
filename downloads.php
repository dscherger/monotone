<?php

// this assumes that all packages for a specific type
// follow a distinct file naming. %version% is replaced with
// the specific monotone version which is tried to match
$matchers = array(
    "Tarball"               => "/monotone-%version%\.tar\.gz/",
    "Linux x86/glibc 2.3"   => "/mtn-%version%-linux-x86\.bz2/",
    "Windows Installer"     => "/monotone-%version%-setup\.exe/",
    "Mac OS X Installer"    => "/monotone-%version%\.dmg/",
    "Mac OS X Binary"       => "/mtn-%version%-osx(-univ)?\.bz2/",
    "Solaris Package"       => "/PMmonotone-%version%\.(i386|sparc)\.pkg/",
);

$webdir      = "/downloads";
$basedir     = dirname(__FILE__) . "/downloads";
$cachedir    = "/data/monotone.ca/cache";
$releaseDirs = scandir($basedir, 1);

function getLatestFiles()
{
    global $matchers, $webdir, $basedir, $releaseDirs;

    $latestFiles = array();
    $matchedTypes = array();

    foreach ($releaseDirs as $dir)
    {
        if (!is_dir("$basedir/$dir") || $dir == "." || $dir == "..")
            continue;

        // a little optimization
        if (count($matchedTypes) == count($matchers))
            break;

        $files = scandir("$basedir/$dir", 1);
        $release = $dir;
        $newlyMatchedTypes = array();

        foreach ($files as $file)
        {
            foreach ($matchers as $type => $matcher)
            {
                if (preg_match(str_replace("%version%", $release, $matcher), $file) &&
                    !in_array($type, $matchedTypes))
                {
                    if (!isset($latestFiles[$type]))
                    {
                        $latestFiles[$type] = array();
                    }
                    $latestFiles[$type][] = "$release/$file";
                    $newlyMatchedTypes[] = $type;
                }
            }
        }
        $matchedTypes = array_merge($matchedTypes, $newlyMatchedTypes);
    }

    return $latestFiles;
}

function getAllFiles($type)
{
    global $matchers, $webdir, $basedir, $releaseDirs;

    $matcher = $matchers[$type];
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

class cache
{
    private static $instance;
    private $basedir;
    private $cachedir;
    private $cache = array();
    private function __construct($basedir, $cachedir)
    {
        $this->basedir = $basedir;
        $this->cachedir = $cachedir;
        if (is_file("$cachedir/download_cache"))
            $this->cache = (array)@unserialize(
                file_get_contents("$cachedir/download_cache")
            );
    }
    public function __destruct()
    {
        if (is_writable($this->cachedir))
            file_put_contents("{$this->cachedir}/download_cache",
                              serialize($this->cache));
    }
    public function get($file)
    {
        if (!isset($this->cache[$file]))
        {
            $this->cache[$file] = array(
                filesize("{$this->basedir}/$file"),
                sha1_file("{$this->basedir}/$file")
            );
        }
        return $this->cache[$file];
    }
    public static function instance($basedir, $cachedir)
    {
        if (!self::$instance)
            self::$instance = new cache($basedir, $cachedir);
        return self::$instance;
    }
}

$page_title = "Downloads";
require_once("header.inc.php");

?>
<div class="boxes">
    <div class="box box-wide">
        <h1 class="getit">Latest monotone downloads by type</h1>

<?php

$type = isset($_GET['type']) &&
        array_key_exists($_GET['type'], $matchers) ?
        $_GET['type'] : "";

if (empty($type)):

      $latestFiles = getLatestFiles();

      if (count($latestFiles) == 0): ?>
        <p>No files found</p>
<?php else: ?>
        <dl><?php
        foreach (array_keys($matchers) as $type):
            if (!is_array($latestFiles[$type]))
              continue;

            echo<<<END
            <dt>$type</dt>
END;
            foreach ($latestFiles[$type] as $file):
                $name = basename($file);
                $release = dirname($file);
                list($size, $sha1) = cache::instance($basedir, $cachedir)->get($file);
                $size = round($size / (1024*1024), 2)."MB";
                echo <<<END
            <dd>
                <a href="$webdir/$file">$name</a> <small>($size, SHA1 <code>$sha1</code>)</small><br/>
                <span style="font-size:75%"><a href="?type=$type">&#187; all downloads of this type</a></span>
            </dd>
END;
            endforeach;
        endforeach;
        ?></dl>
<?php endif;
else:

        $allFiles = getAllFiles($type);
        if (count($allFiles) == 0): ?>
        <p>No files found</p>
<?php else: ?>
        <dl><?php
        echo<<<END
            <dt>$type</dt>
END;
        foreach ($allFiles as $file):
            $name = basename($file);
            $release = dirname($file);
            list($size, $sha1) = cache::instance($basedir, $cachedir)->get($file);
            $size = round($size / (1024*1024), 2)."MB";
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


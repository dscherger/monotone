<?php
require_once("config.inc.php");
require_once("header.inc.php");
require_once("simplepie-1.2.0/simplepie.inc.php");
?>
    <div class="boxes">
        <div class="box box-small">
            <h1 class="getit">Get it here</h1>
            <p>Latest version: <strong>0.48.1</strong></p>
            <ul>
                <li>Check the <b><a href="NEWS">NEWS</a></b></li>
                <li>Need to <a href="UPGRADE">UPGRADE</a>?</li>
                <li><b><a href="downloads.php">Download it</a></b></li>
            </ul>
            <h1 class="getitdistro">...or from your distro</h1>
            <ul>
                <li><a href="http://cygwin.com/cgi-bin2/package-grep.cgi?grep=monotone/monotone">Cygwin</a></li>
                <li><a href="http://packages.debian.org/search?keywords=monotone&amp;searchon=names&amp;exact=1&amp;suite=all&amp;section=all">Debian Linux</a></li>
                <li><a href="http://www.freshports.org/devel/monotone/">FreeBSD Ports</a></li>
                <li><a href="http://www.macports.org/ports.php?by=name&amp;substr=monotone">Mac OS X MacPorts</a></li>
                <li><a href="http://pkgsrc.se/devel/monotone">NetBSD pkgsrc</a></li>
                <li><a href="http://software.opensuse.org/search?p=1&amp;q=monotone&amp;baseproject=ALL">openSuSE Linux</a></li>
                <li><a href="https://admin.fedoraproject.org/community/?package=monotone#package_maintenance/package_overview">Fedora Linux</a></li>
                <li><a href="http://packages.gentoo.org/package/monotone">Gentoo Linux</a></li>
            </ul>
        </div>

        <div class="box box-small">
            <h1 class="useit">Use it</h1>
            <ul>
                <li><a href="docs/index.html"><b>manual</b> (with tutorial)</a><br/>(<a class="lesser" href="monotone.html">one page</a>, <a class="lesser" href="monotone.pdf">PDF</a>)</li>
                <li><a href="INSTALL">INSTALL</a></li>
                <li><a href="http://wiki.monotone.ca/FAQ/">FAQ</a></li>
                <li><a href="http://wiki.monotone.ca/InterfacesFrontendsAndTools/">GUIs/other tools</a></li>
                <li><a href="http://lists.nongnu.org/mailman/listinfo/monotone-users"><strong>users mailing list</strong></a></li>
                <li><a href="irc://irc.oftc.net/#monotone">IRC</a> (<a class="lesser" href="http://colabti.org/irclogger/irclogger_logs/monotone">logs</a>)</li>
                <li><a href="http://wiki.monotone.ca/"><strong>wiki</strong></a></li>
                <li><a href="http://www.cafepress.com/monotone_rcs">swag <strong>new</strong></a></li>
                <li><a href="http://www.frappr.com/monotone">frappr</a></li>
            </ul>

            <hr />

            <p style="text-align: center">
                <strong>Need help?</strong><br />
                Just <a href="irc://irc.oftc.net/monotone">join irc</a>... we're friendly!
            </p>
        </div>

        <div class="box box-small">
            <h1 class="improveit">Improve it</h1>
            <ul>
                <li><a href="http://code.monotone.ca/p/monotone/issues/"><strong>bugs</strong></a></li>
                <li><a href="http://lists.nongnu.org/mailman/listinfo/monotone-devel">developers mailing list</a></li>
                <li><a href="irc://irc.oftc.net/#monotone">IRC</a> (<a class="lesser" href="http://colabti.org/irclogger/irclogger_logs/monotone">logs</a>)</li>
                <li><a href="http://monotone.thomaskeller.biz/docbuild/html">nightly documentation build</a> (<a href="http://monotone.thomaskeller.biz/docbuild/monotone.html">one page</a>)</li>
                <li><a href="http://monotone.thomaskeller.biz/autobuild/index.php"><strong>nightly builds for openSUSE and Fedora</strong></a></li>
                <li><a href="http://wiki.monotone.ca/SelfHostingInfo/">self-hosting info</a></li>
                <li><a href="http://code.monotone.ca/p/monotone/source/tree/h:net.venge.monotone/">browse source</a></li>
                <li><a href="http://cia.navi.cx/stats/project/monotone">latest work</a> (<a class="lesser" href="http://cia.navi.cx/stats/project/monotone/.rss">RSS</a>)</li>
                <li>commit mailing list:<br />
                  <a href="http://lists.nongnu.org/mailman/listinfo/monotone-commits-diffs">with</a> /
                  <a href="http://lists.nongnu.org/mailman/listinfo/monotone-commits-nodiffs">without</a> diffs</li>
                <li><a href="buildbot/">build status</a></li>
                <li><a href="http://wiki.monotone.ca/QuickieTasks/">quickie tasks</a></li>
            </ul>
        </div>
        <div class="clearfloat"></div>
    </div>

    <div class="boxes">
        <div class="box box-wide">
            <h1 class="intheblogs">Monotone in the blogs</h1>

<?php
    $feed = new SimplePie();
    $feed->set_cache_location($CFG['cache_dir']);
    $feed->set_feed_url($CFG['aggregated_feeds']);
    $feed->init();
    $feed->handle_content_type();

    $items = $feed->get_items(0, $CFG['new_msgs_from_feeds_count']);
    if (count($items) == 0):
?>
            <p>No blog posts found.</p>
<?php
    else:
        foreach ($items as $item):
            $author = ($author = $item->get_author())
                ? $author->get_name() : "unknown";
?>
            <div class="feed-msg">
                <h2><?php echo $item->get_title() ?></h2>
                <h3>by <?php echo $author ?>, <?php echo $item->get_date("j F Y | g:i a") ?></h3>
                <p>
                    <?php 
                        $desc = strip_tags($item->get_description());
                        if (strlen($desc) > 350):
                            $desc = substr($desc, 0, 350). " [...]";
                        endif;
                        echo $desc;
                    ?>
                    <a href="<?php echo $item->get_link(0) ?>" target="_blank">&#187; read more</a>
                </p>
            </div>
<?php
        endforeach;
?>
        <p style="text-align: center">
            <a href="intheblogs.php">&#187; view older entries</a>
        </p>
<?php
    endif;
?>
        </div>
    </div>

<?php
    require_once("footer.inc.php");
?>


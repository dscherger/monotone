<?php
header("Content-type: application/rss+xml");
echo '<?xml version="1.0" encoding="UTF-8"?>', "\n";
?>
<rss version="2.0"
    xmlns:atom="http://www.w3.org/2005/Atom"
    xmlns:dc="http://purl.org/dc/elements/1.1/">
    <channel>
        <title>monotone news</title>
        <atom:link href="http://<?php echo $_SERVER['SERVER_NAME'], $_SERVER['SCRIPT_NAME']; ?>" rel="self" type="application/rss+xml" />
        <link>http://monotone.ca/rss.php</link>
        <description>Aggregated news for the monotone version control system</description>
        <language>en-us</language>
<?php
    require_once("simplepie-1.2.0/simplepie.inc.php");

    $feed = new SimplePie();
    $feed->set_feed_url(require_once("rss_feeds.inc.php"));
    $feed->init();
    $feed->handle_content_type();

    foreach ($feed->get_items() as $item):
        $creator = ($author = $item->get_author())
            ? $author->get_name() : "unknown";
        $author = strpos($creator, "@") === false
            ? "" : $creator;
        $feed = $item->get_feed();
?>
        <item>
            <title><?php echo $item->get_title() ?></title>
            <link><?php echo $item->get_permalink(); ?></link>
            <guid isPermaLink="false"><?php echo $item->get_link() ?></guid>
            <dc:creator><?php echo $creator; ?></dc:creator>
<?php if (!empty($author)): ?>
            <author><?php echo $author; ?></author>
<?php endif; ?>
            <description><![CDATA[<?php echo $item->get_description() ?>]]></description>
            <pubDate><?php echo $item->get_date('r'); ?></pubDate>
<?php if (!empty($feed)): ?>
            <source url="<?php echo $feed->get_permalink(); ?>"><?php echo $feed->get_title(); ?></source>
<?php endif; ?>
        </item>
<?php
    endforeach;
?>
    </channel>
</rss>


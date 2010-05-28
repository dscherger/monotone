<?php

$page_title = "In the blogs";
require_once("header.inc.php");

?>
<div class="boxes">
    <div class="box box-wide">
        <h1 class="intheblogs">Monotone in the blogs</h1>
<?php
    require_once("simplepie-1.2.0/simplepie.inc.php");

    $feed = new SimplePie();
    $feed->set_feed_url(require_once("rss_feeds.inc.php"));
    $feed->init();
    $feed->handle_content_type();

    $items = $feed->get_items();
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
                    <?php echo $item->get_description() ?>
                    <a href="<?php echo $item->get_link(0) ?>" target="_blank">&#187; read more</a>
                </p>
            </div>
<?php
        endforeach;
?>
<?php
    endif;
?>
    </div>
</div>

<?php
require_once("footer.inc.php");
?>


<?php

setlocale(LC_ALL, "en_US.UTF-8");

$parser = new news_parser(dirname(__FILE__) . "/NEWS");
$releases = $parser->get_releases(10);

// ----------------------------------------------------------------------------

class news_parser
{
    private $fp, $sections;

    public function __construct($file)
    {
        $this->fp = fopen($file, 'r');
        if (!$this->fp)
            throw Exception("couldn't open '$file' for reading");

        $this->sections = array(
            "Changes", "New features", "Bugs fixed", "Other", "Internal"
        );
    }

    public function __destruct()
    {
        fclose($this->fp);
    }

    public function get_releases($count)
    {
        $releases = array();
        for ($i=0; $i<$count; ++$i)
        {
            $release = $this->get_release();
            if ($release === false)
                break;
            $releases[] = $release;
        }
        return $releases;
    }

    public function get_release()
    {
        $timestamp = $this->get_timestamp();
        if ($timestamp === false)
            return false;

        return array(
            "timestamp" => $timestamp,
            "header"    => $this->get_header(),
            "sections"  => $this->get_sections()
        );
    }

    public function get_timestamp()
    {
        $this->eat_whitespace();
        $date = $this->get_line();
        if ($date === false)
            return false;
        return strtotime($date);
    }

    public function get_header()
    {
        $this->eat_whitespace();
        return trim($this->get_line());
    }

    public function get_sections()
    {
        $sections = array();
        while (($section = $this->get_section()) !== false)
        {
            $sections[] = $section;
        }
        return $sections;
    }

    public function get_section()
    {
        $this->eat_whitespace();
        $oldpos = ftell($this->fp);
        $sec = trim($this->get_line());
        if ($sec === false || !in_array($sec, $this->sections))
        {
            fseek($this->fp, $oldpos);
            return false;
        }

        return array(
            "name" => $sec,
            "entries" => $this->get_entries()
        );
    }

    public function get_entries()
    {
        $entries = array();
        $current = -1;

        while (!feof($this->fp))
        {
            $oldpos = ftell($this->fp);
            $line = $this->get_line();

            if (!empty($line) && (
                  ltrim($line) == $line || // end of release
                  in_array(trim($line), $this->sections) // end of section
               ))
            {
                fseek($this->fp, $oldpos);
                break;
            }

            $line = trim($line);
            if (empty($line) && $current == -1)
                continue;

            if (substr($line, 0, 1) == "-")
            {
                ++$current;
                $entries[$current] = "";
                $line = substr($line, 2);
            }

            if (empty($line))
                $line = "\n";

            $filler = " ";
            if (empty($entries[$current]) ||
                substr($entries[$current], -1, 1) == "\n")
            {
                $filler = "";
            }

            $entries[$current] .= "$filler$line";
        }

        return $entries;
    }

    public function eat_whitespace()
    {
        $off = ftell($this->fp);
        while (!feof($this->fp))
        {
            $ch = fgetc($this->fp);
            if (trim($ch) != '')
            {
                break;
            }
            $off++;
        }
        fseek($this->fp, $off);
    }

    public function get_line()
    {
        if (feof($this->fp))
            return false;
        return fgets($this->fp);
    }
}

header('Content-type: application/rss+xml');
echo '<?xml version="1.0" encoding="utf-8"?>';
$self = "http://{$_SERVER['SERVER_NAME']}{$_SERVER['SCRIPT_NAME']}";
?>
<rss version="2.0"
    xmlns:atom="http://www.w3.org/2005/Atom"
    xmlns:dc="http://purl.org/dc/elements/1.1/">
    <channel>
        <title>monotone - distributed version control</title>
        <atom:link href="<?php echo $self ?>" rel="self" type="application/rss+xml" />
        <link><?php echo $self ?></link>
        <description>Recent monotone releases</description>
        <language>en-us</language>

        <pubDate><?php echo date("r", $releases[0]['timestamp']); ?></pubDate>
        <?php foreach ($releases as $release): ?>
        <item>
            <title><?php echo $release['header']; ?></title>
            <description><![CDATA[

            <?php foreach ($release['sections'] as $section): ?>

                <h2><?php echo $section['name']; ?></h2>
                <ul>
                <?php foreach ($section['entries'] as $entry): ?>
                    <li><?php
                        // link normal urls
                        $entry = preg_replace('#(https?://[^ )>\b]+)#',
                                              '<a href="$1">$1</a>',
                                              $entry);

                        // link monotone bugs
                        $entry = preg_replace_callback(
                           '/monotone bugs?(?:(?:, |, and | and | )#\d+)+/',
                           create_function('$matches', '
                                return preg_replace(
                                   "/#(\d+)/",
                                   "<a href=\"https://savannah.nongnu.org/bugs/?$1\">#$1</a>",
                                   $matches[0]
                                );
                           '),
                           $entry);
                        echo nl2br($entry);
                    ?></li>
                <?php endforeach; ?>
                </ul>
            <?php endforeach; ?>
            ]]></description>

            <pubDate><?php echo date('r', $release['timestamp']); ?></pubDate>
        </item>
        <?php endforeach; ?>
    </channel>
</rss>


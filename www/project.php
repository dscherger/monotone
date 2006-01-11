<?
include_once('common.php');
$hostname = "localhost";
$project = pg_escape_string(basename(dirname($_SERVER['PHP_SELF'])));
$level = 'project';

function mkfilediv() {
	global $project;
	$files = scandir("files");
	$filediv = "<div>\n"
	. "This project has released the following files:<br/>\n"
	. "<ul>\n";
	foreach($files as $i) {
		if ($i == "." || $i == "..")
			continue;
		$inf = file_get_contents("files-about/$i");
		$filediv = $filediv
		. "<li><a href=\"files/$i\">$i</a>: $inf</li>\n";
	}
	$filediv = $filediv . "</ul>\n";
	return $filediv;
}

?><!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.0//EN" "http://www.w3.org/TR/REC-html40/strict.dtd">
<html>
<head>
<title>Project page for <?=$project?></title>
<link rel="stylesheet" type="text/css" href="../../main.css">
</head>
<body>

<? include('top.php'); ?>
<?include('sidebar.php')?>

<div id="main">
<div><?=file_get_contents('longdescription')?></div><hr/>
<?=mkfilediv()?>
</div>
</body>

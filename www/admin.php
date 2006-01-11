<? include_once('common.php') ?>
<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.0//EN" "http://www.w3.org/TR/REC-html40/strict.dtd">

<?
	$level = 'project';
	include_once('common-ctrl.php');
	$hostname = "localhost";
	$page = getarg('page');
	if(!$page)
		$page = 'permissions';
?>
<html>
<head>
<title>Project admin page for <?=$project?> </title>
<link rel="stylesheet" type="text/css" href="../../main.css">
<script type="text/javascript"><!-- 
project="<?=$project?>";
// --></script>
<script type="text/javascript" src="../../MochiKit/MochiKit.js"></script>
<script type="text/javascript" src="../../common.js"></script>
<script type="text/javascript" src="../../sidebar.js"></script>
<script type="text/javascript" src="../../admin-<?=$page?>.js"></script>

</head>

<body>
<? include('top.php'); ?>
<? include("sidebar.php"); ?>

<div id="main">

<ul id="pages">
<?
	$pages = array('permissions' => "Permissions",
			'source_control' => "Source&nbsp;Control",
			'files' => "Files",
			'description' => "Description"/*,
			'web' => "Web",
			'resources' => "Resources"*/);
	foreach ($pages as $k => $i) {
		if ($k == $page)
			print "<li id=\"page$k\" class=\"selpage\"><a>$i</a></li> ";
		else
			print "<li id=\"page$k\"><a href=\"admin.php?page=$k\">$i</a></li> ";
	}
?>
</ul>
<?
include("admin-$page.php");
?>
</div>
</body>
</html>

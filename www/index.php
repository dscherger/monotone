<? include_once('common.php') ?>
<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.0//EN" "http://www.w3.org/TR/REC-html40/strict.dtd">
<? $level = 'main'; ?>
<html>
<head>
<title>Project List</title>
<link rel="stylesheet" type="text/css" href="main.css">
<script type="text/javascript" src="MochiKit/MochiKit.js"/>
<script type="text/javascript" src="common.js"/>
<script type="text/javascript" src="sidebar.js"/>
</head>

<body>
<? include('top.php'); ?>
<? include("sidebar.php"); ?>

<div id="main">
<div id="contents">

<?
$query = "SELECT name, directory FROM projects";
$result = pg_exec($db, $query);
if (!$result) {printf("ERROR"); }
$rows = pg_numrows($result);
for($i = 0; $i < $rows; ++$i) {
	$row = pg_fetch_row ($result,$i);
	printf("<div class=\"record\">\n");
	printf("<span style=\"font-weight: bold;\">%s</span> | \n", $row[0]);
	printf("<a href=\"projects/%s/\">Project info page</a> | \n", $row[0]);
	printf("<a href=\"projects/%s/admin.php\">Maintainer Page</a> | \n", $row[0]);
	printf("<tt>mtn pull %s.%s \\*</tt>\n", $row[0], $hostname);
	printf("<hr />\n");
	printf(file_get_contents("projects/$row[0]/description"));
	printf("</div>\n");
}
pg_close();
?>

</div>
</div>
</body>
</html>

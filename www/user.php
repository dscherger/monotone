<? include_once('common.php') ?>
<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.0//EN" "http://www.w3.org/TR/REC-html40/strict.dtd">

<?
	$level = 'main';
?>
<html>
<head>
<title>Project admin page for <?=$project?> </title>
<link rel="stylesheet" type="text/css" href="main.css">
<script type="text/javascript" src="MochiKit/MochiKit.js"></script>
<script type="text/javascript" src="common.js"></script>
<script type="text/javascript" src="sidebar.js"></script>

</head>

<body>
<? include('top.php'); ?>
<? include("sidebar.php"); ?>

<div id="main">

<form method="POST" action="login.php">
Change password:<br/>
<input type="password" name="newpass" /><br/>
<input type="submit" value="Change"/>
</form>

</div>
</body>
</html>

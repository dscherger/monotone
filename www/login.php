<?
include("common.php");
$location = $_REQUEST['location'];

function docookie($username, $shapass) {
	global $json;
	$t = time() + 60*60*24*7;
	$auth = array(
		'username' => $username,
		'password' => $shapass,
		'expiration' => $t,
		'token' => mktok($username, $shapass, $t));
	setcookie('AUTH', $json->encode($auth), 0, '/');
}

function page_head() {
	global $validuser, $username, $location;
	$level = 'main';
	?><!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.0//EN" "http://www.w3.org/TR/REC-html40/strict.dtd">
	<html>
	<head>
	<title>Project admin page for <?=$project?> </title>
	<link rel="stylesheet" type="text/css" href="main.css">
	<script type="text/javascript" src="MochiKit/MochiKit.js"/>
	<script type="text/javascript" src="common.js"/>
	<script type="text/javascript" src="sidebar.js"/>
	</head>
	<body>
	<? include('top.php'); ?>
	<? include("sidebar.php"); ?>
	<div id="main">
	<?
}
if ($_REQUEST['logout']) {
	setcookie('AUTH', null, 0, '/');
	$validuser = false;
	page_head();
	?>
	Logged out.<br/>
	<?
} else if ($_REQUEST['newuser']) {
	if ($username == "" || $shapass == "") {
		$res = "Your username and password cannot be blank.<br/>\n";
	} else {
		pg_exec($db, "BEGIN");
		pg_exec($db, "LOCK TABLE users");
		$query = sprintf("SELECT * FROM users WHERE username = '%s'", $safeuser);
		$result = pg_exec($db, $query);
		if (!$result) {
			$res = "Internal server error.<br/>\n";
		} else if (pg_numrows($result) == 0) {
			$query = "INSERT INTO users (username, password) VALUES ('%s', '%s')";
			pg_exec($db, sprintf($query, $safeuser, $shapass));
			$res = "Added user $username.<br/>\n";
			$validuser = true;
		} else {
			$res = "That username is already taken.<br/>\n";
		}
		pg_exec($db, "END");
	}
	docookie($username, $shapass);
	page_head();
	print $res;
} else if ($_REQUEST['newpass']) {
	if (!$validuser) {
		$res = "Username or password incorrect.";
	} else {
		$newpass = $_REQUEST['newpass'];
		if ($newpass == "") {
			$res = "Your new password cannot be blank.";
		} else {
			$query = "UPDATE users SET password = '%s' WHERE username = '%s'";
			$result = pg_exec($db, sprintf($query, sha1($newpass), $safeuser));
			if(!result) {
				$res = "Internal server error.";
			} else {
				$res = "Password changed.";
			}
		}
	}
	page_head();
	print $res;
} else {
	if ($validuser) {
		docookie($username, $shapass);
		page_head();
		?>
		Logged in.<br/>
		<?
	} else {
		page_head();
		?>
		Username or password incorrect.<br/>
		<?
	}
}
?>
<a href="<?=$location?>">Back to where you were</a><br/>
</div></body></html>

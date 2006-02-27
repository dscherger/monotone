
<div id="leftbar">
<div id="status"></div>
<?
if ($validuser) {
	?>
	You are <a href="<?=($level=='project')?'../../':''?>user.php"><?=$username?></a>.<br/>
	<form method="POST" action="<?=($level=='project')?'../../':''?>login.php">
	<input type="hidden" name="location" value="<?=$location?$location:$_SERVER['PHP_SELF']?>"/>
	<input type="submit" name="logout" value="Log out"/>
	</form>
	<?
} else {
	?>
	<form method="POST" action="<?=($level=='project')?'../../':''?>login.php">
	Username:<br/>
	<input type="text" name="username" id="username" value="<?=$username?>"/><br/>
	Password:<br/>
	<input type="password" name="password" id="password"/><br/>
	<input type="hidden" name="location" value="<?=$location?$location:$_SERVER['PHP_SELF']?>"/>
	<input type="submit" value="Login" name="login"/> <input type="submit" value="New user" name="newuser"/>
	</form>
	<?
}
if ($validuser && $level == 'main') {
	?>
	<hr/>
	<input type="text" name="newproject" id="newproject"/>
	<button type="button" onclick="do_newproj();">New project</button>
	<?
}
?>
<hr/>
<a href="<?=($level=='project')?'../../':''?>index.php">Project index</a><br/>
<hr/>
<? if ($level == 'project') {?>
	<a href="index.php">Project info</a><br/>
	<a href="admin.php">Maintainer section</a><br/>
	<?
	$query = "SELECT name, url FROM resources WHERE project = '%s'";
	$result = pg_exec($db, sprintf($query, $safeproj));
	$out = array();
	print "<br/>Project resources:\n<ul>";
	print "<li><a href=\"viewmtn/\">Source browser</a><br/></li>";
	if ($result) {
		$rows = pg_numrows($result);
		for($i = 0; $i < $rows; ++$i) {
			$row = pg_fetch_row ($result,$i);
			$r['name']	= $row[0];
			$r['url']	= $row[1];
			print "<li><a href=\"" . $row[1] . "\">" .
				$row[0] . "</a></li>\n";
		}
	}
	print "</ul>\n";
}
?>
</div>

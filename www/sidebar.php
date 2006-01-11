
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
	<input type="password" name="password" id="password" value="<?=$password?>"/><br/>
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
	<a href="viewmtn/">Browse source</a><br/>
	<a href=""></a><br/>
	<a href=""></a><br/>
	<a href=""></a><br/>
	<?
}
?>
<!--
<div id="status">
</div>
<div id="login">
Username:<br/>
<input type="text" name="username" id="username"/>
Password:<br/>
<input type="password" name="password" id="password"/>
<br/>
<br/>
<input type="submit" value="New user" onclick="do_newuser();"/>
<br/>
<br/>
New password:<br/>
<input type="password" name="newpassword" id="newpassword"/>
<br/>
<br/>
<input type="submit" value="Change password" onclick="do_chpass();"/>
</div>
<hr />
Project:<br/>
<? printf("<input type=\"text\" name=\"project\" id=\"project\" value=\"%s\"/>", $project); ?>
<br/>
<br/>
<input type="submit" value="New project" onclick="do_newproj();"/>
<hr/>
-->
</div>

<?
include_once("common-ctrl.php");


// Note that this returns an html page, not JSON.
if ($action === "chhomepage") {
	header('Content-type: text/html');
	print "<html><head><title>File uploaded</title><script>\n";
	if($validuser) {
		if($permissions['homepage'] && $projdir != "") {
			if (move_uploaded_file($_FILES['homepage']['tmp_name'], $projdir . "/index.html"))
				print "parent.chhomedone();\n";
			else
				print "parent.status(\"Error: Internal server error.\");\n";
		} else {
			print "parent.status(\"Error: You don't have permission to do that.\");\n";
		}
	} else {
		print "parent.status(\"Error: username or password is incorrect.\");\n";
	}
	print "</script></head><body>File uploaded</body></html>";
	pg_close();
	exit;
} else if ($action === "upload_files") {
	header('Content-type: text/html');
	print "<html><head><title>File upload</title><script>\n";
	$st = "";
	do {
		if(!$validuser) {
			$st = $st . "Error: username or password is incorrect.";
			break;
		}
		if(!$permissions['homepage'] || $projdir == "") {
			$st = $st . "Error: You don't have permission to do that.";
			break;
		}
		$ok = true;
		$filedir = "$projwww/files";
		$aboutdir = "$projwww/files-about";
		foreach (array_keys($_FILES['upfiles']['tmp_name']) as $i) {
			$base = basename($_FILES['upfiles']['name'][$i]);
			if ($base == "")
				continue;
			if (!move_uploaded_file($_FILES['upfiles']['tmp_name'][$i], "$filedir/$base")) {
				$ok = false;
				$st = $st . "Error uploading file '$base'. ";
				break;
			}
			file_put_contents("$aboutdir/$base", $_REQUEST['upfile_comments'][$i]);
		}
		if ($ok)
			print "parent.uploaddone();\n";
		else
			print "parent.status(\"" . $st . "\");";
	} while (false);
	print "</script></head><body>File uploaded</body></html>";
	pg_close();
	exit;
}

function usherctrl($what) {
	global $adminaddr, $adminport, $adminuser, $adminpass;
	$addr = sprintf("tcp://%s:%s", $adminaddr, $adminport);
	$fp = stream_socket_client($addr, $errno, $errstr, 30);
	if (!$fp) {
		return "Error: $errstr ($errno)";
	} else {
		$out = "";
		fprintf($fp, "USERPASS %s %s\n%s\n", $adminuser, $adminpass, $what);
		while (!feof($fp)) {
			$out = $out . fgets($fp, 1024);
		}
		fclose($fp);
		return $out;
	}
}

function maintlist() {
	global $db, $safeproj;
	$fields = "username, give, upload, homepage, access, server, description";
	$query = sprintf("SELECT %s FROM permissions WHERE ", $fields);
	$query = $query . "project = '%s'";
	$result = pg_exec($db, sprintf($query, $safeproj));
	$out = array();
	if ($result) {
		$rows = pg_numrows($result);
		for($i = 0; $i < $rows; ++$i) {
			$row = pg_fetch_row ($result,$i);
			$perm['username']	= $row[0];
			$perm['give']		= ($row[1] == 1);
			$perm['upload']		= ($row[2] == 1);
			$perm['homepage']	= ($row[3] == 1);
			$perm['access']		= ($row[4] == 1);
			$perm['server']		= ($row[5] == 1);
			$perm['description']	= ($row[6] == 1);
			$out[] = $perm;
		}
	}
	return $out;
}

header('Content-type: text/x-json');

if ($action == "new_project") {
	if ($validuser) {
		pg_exec($db, "BEGIN");
		pg_exec($db, "LOCK TABLE projects, permissions");
		$err = false;

		$query = "SELECT * FROM projects WHERE name = '%s'";
		$result = pg_exec($db, sprintf($query, $safeproj));
		do {
			if(!preg_match('/^[a-zA-Z0-9-]*$/D', $project)) {
				print $json->encode(array("error" => "Only letters, numbers, and dash are allowed in a project name."));
				$err = true;
				break;
			}
			if(!$result) {
				$err = true; 
				print $json->encode(array("error" => "Internal server error."));
				break;
			}
			if(pg_numrows($result)) {
				print $json->encode(array("error" => "That project name is already taken."));
				$err = true;
				break;
			}
			$projdir = $serverdir . '/projects/'. $project;
			$projwww = $serverdir . '/www/projects/'. $project;
			$query = "INSERT INTO projects (name, directory) VALUES ('%s', '%s')";
			$result = pg_exec($db, sprintf($query, $safeproj, '/foobar'));
			if(!$result) {
				$err = true; 
				print $json->encode(array("error" => "Internal server error."));
				break;
			}
			$fields = "username, project, give, upload, homepage, access, server, description";
			$query = sprintf("INSERT INTO permissions (%s) VALUES (%%s)", $fields);
			$values = sprintf("'%s', '%s', 1, 1, 1, 1, 1, 1", $username, $safeproj);
			$result = pg_exec($db, sprintf($query, $values));
			if(!$result) {
				$err = true; 
				print $json->encode(array("error" => "Internal server error."));
				break;
			}
			$out = array();
			exec("cp -r '$serverdir/skel' '$projdir'", $out, $res1);
			mkdir($projwww);
			mkdir("$projwww/files");
			mkdir("$projwww/files-about");
			symlink('../../admin.php', "$projwww/admin.php");
			symlink('../../project.php', "$projwww/index.php");
			symlink('../../viewmtn', "$projwww/viewmtn");
			exec("$monotone -d $projdir/database db init 2>&1", $out, $res2);
			exec("cp $projdir/database $projdir/database.viewmtn");
			exec("cp $projdir/database $projdir/database.transfer");
			exec("chmod ug+rwX '$projdir' '$projdir'/* 2>&1", $out, $res3);
			if ($res1 || $res2 || $res3) {
				print $json->encode(array("error" => "Internal server error", "verboseError" => $out));
				$err = true;
			}
			usherctrl("ADD $project");
		} while (false);
		if ($err)
			pg_exec($db, "ROLLBACK");
		else
			print $json->encode(array("name" => $project));

		pg_exec($db, "END");
	} else
		print $json->encode(array("error" => "username or password incorrect."));
} else
	print $json->encode(array("error" => sprintf("'%s' not implemented.", $action)));
pg_close($db);
?>

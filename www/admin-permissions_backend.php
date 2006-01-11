<?
include_once("common-ctrl.php");

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

if ($action == "getmaint") {
	print $json->encode(array("maintainers" => maintlist()));
} else if ($action == "chmaint") {
	if(allowed('give')) {
		$res = array();
		$ok = true;
		$begun = false;
		foreach ($args->newmaint as $i) {
			if ($i->username === $username && !$i->give) {
				$ok = false;
				break;
			}
		}
		do {
			if(!$ok) {
				$res["error"] = "You're not allowed to revoke your own permissions to edit maintainers.";
				break;
			}
			pg_exec($db, "BEGIN");
			$begun = true;
			pg_exec($db, "LOCK TABLE permissions");
			$query = sprintf("DELETE FROM permissions WHERE project = '%s'", $safeproj);
			$result = pg_exec($db, $query);
			if (!$result) {
				$res['error'] = 'Internal server error.';
				$ok = false;
				break;
			}
			$fields = "username, project, give, upload, homepage, access, server, description";
			$query = sprintf("INSERT INTO permissions (%s) VALUES (%%s)", $fields);
			foreach ($args->newmaint as $i) {
				$values = sprintf("'%s', '%s', %s, %s, %s, %s, %s, %s",
						pg_escape_string($i->username), $safeproj,
						$i->give?1:0, $i->upload?1:0, $i->homepage?1:0,
						$i->access?1:0, $i->server?1:0, $i->description?1:0);
				$result = pg_exec($db, sprintf($query, $values));
				if (!$result) {
					$res['error'] = 'Internal server error.';
					$ok = false;
					break;
				}
			}
			if($ok)
				$res["maintainers"] = maintlist();
		} while(false);
		if (!$ok && $begun)
			pg_exec($db, "ROLLBACK");
		print $json->encode($res);
		pg_exec($db, "END");
	}
}
pg_close($db);
?>

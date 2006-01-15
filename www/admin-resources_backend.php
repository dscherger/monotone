<?
include_once("common-ctrl.php");

function resourcelist() {
	global $db, $safeproj;
	$query = "SELECT name, url FROM resources WHERE project = '%s'";
	$result = pg_exec($db, sprintf($query, $safeproj));
	$out = array();
	if ($result) {
		$rows = pg_numrows($result);
		for($i = 0; $i < $rows; ++$i) {
			$row = pg_fetch_row ($result,$i);
			$r['name']	= $row[0];
			$r['url']	= $row[1];
			$out[] = $r;
		}
	}
	return $out;
}

header('Content-type: text/x-json');

if ($action == "getresources") {
	print $json->encode(array("resources" => resourcelist()));
} else if ($action == "chresources") {
	$res = array();
	$ok = true;
	do {
		pg_exec($db, "BEGIN");
		pg_exec($db, "LOCK TABLE resources");
		$query = sprintf("DELETE FROM resources WHERE project = '%s'", $safeproj);
		$result = pg_exec($db, $query);
		if (!$result) {
			$res['error'] = 'Internal server error. (1)';
			$ok = false;
			break;
		}
		$fields = "project, name, url";
		$query = sprintf("INSERT INTO resources (%s) VALUES (%%s)", $fields);
		foreach ($args->newmaint as $i) {
			$values = sprintf("'%s', '%s', '%s'",
					$safeproj,
					pg_escape_string($i->name),
					pg_escape_string($i->url));
			$result = pg_exec($db, sprintf($query, $values));
			if (!$result) {
				$res['error'] = 'Internal server error. (2)';
				$res['verboseError'] = sprintf($query, $values);
				$ok = false;
				break;
			}
		}
		if($ok)
			$res["resources"] = resourcelist();
	} while(false);
	if (!$ok)
		pg_exec($db, "ROLLBACK");
	print $json->encode($res);
	pg_exec($db, "END");
}
pg_close($db);
?>

<?
include_once("common-ctrl.php");

if ($action == "upload_files") {
	header('Content-type: text/html');
	print "<html><head><title>File upload</title><script>\n";
	$st = "";
	do {
		if(!$validuser) {
			$st = $st . "Error: username or password is incorrect.";
			break;
		}
		if(!$permissions['upload'] || $projdir == "") {
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
			$filedesc = $_REQUEST['upfile_comments'][$i];
			file_put_contents("$aboutdir/$base", $filedesc);
			print "parent.add_file_to_present(\"$base\", \"$filedesc\");";
		}
	} while (false);
	if ($ok)
		print "parent.uploaddone();\n";
	else
		print "parent.status(\"" . $st . "\");";
	print "</script></head><body>File uploaded</body></html>";
} elseif ($action == "rmfile") {
	header('Content-type: text/x-json');
	unlink($projwww . "/files/" . basename($args->file));
	unlink($projwww . "/files-about/" . basename($args->file));
	print $json->encode(array("ok" => "ok"));
} elseif ($action == "chfiledesc") {
	header('Content-type: text/x-json');
	unlink($projwww . "/files-about/" . basename($args->file));
	file_put_contents($projwww . "/files-about/" . basename($args->file), $args->filedesc);
	print $json->encode(array("ok" => "ok"));
}
pg_close($db);
?>

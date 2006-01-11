<?
include_once("common-ctrl.php");

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

header('Content-type: text/x-json');

if ($action == "getsrv") {
	print $json->encode(array(
				"status" => usherctrl("STATUS " . $project),
				"wperm" => file_get_contents($projdir . "/write-permissions"),
				));
} else if ($action === "refresh_state") {
	print $json->encode(array("status" => usherctrl("STATUS " . $project)));
} else if ($action === "disable" || $action === "enable") {
	if(allowed('server')) {
		if ($action === "disable")
			$what = "STOP";
		else
			$what = "START";
		print $json->encode(array("status" => usherctrl("$what $project")));
	}
} else if ($action === "resetdb") {
	if(allowed('server')) {
		if ($args->oath === "I solemnly swear that I have a backup.") {
			$out = array();
			exec("rm $projdir/database 2>&1", $out, $res1);
			exec("$monotone -d $projdir/database db init 2>&1", $out, $res2);
			exec("chmod g+rw $projdir/database 2>&1", $out, $res3);
			if ($res2 > 0 || $res3 > 0) {
				$err = "";
				if ($res1) $err = $err . "Remove old database failed. ";
				if ($res2) $err = $err . "Init new database failed. ";
				if ($res3) $err = $err . "Set database permissions failed.";
				print $json->encode(array("error" => $err, "verboseError" => $out));
			} else
				print $json->encode(array("ok" => "Database reset... You *did* disable the server first, right?"));
		} else {
			print $json->encode(array("error" => "I'm sorry Dave, I can't let you do that."));
		}
	}
} else if ($action === "new_key") {
	if(allowed('access')) {
		$args->keydata;
		preg_match('/^\[[^ \]]* ([^\]]*)\]/', $args->keydata, $matches);
		$keyname = $matches[1];
		if($keyname == null)
			print $json->encode(array("error" => "Cannot extract key name."));
		else {
			$out = array();
			file_put_contents($projdir . "/keyfile", $args->keydata, LOCK_EX);
			exec("$monotone -d '$projdir/kdb' db init 2>&1", $out, $res1);
			exec("$monotone -d '$projdir/kdb' read '$projdir/keyfile'", $out, $res2);
			exec("$monotone --key-to-push '$keyname' -d '$projdir/kdb' push '$project.$hostname' '' 2>&1", $out, $res3);
			exec("rm '$projdir/kdb' '$projdir/keyfile'");
			if($res1 || $res2 || $res3) {
				$outstr = "";
				foreach ($out as $i)
					$outstr = $outstr . $i . "\n";
				print $json->encode(array("error" => "Key upload failed.", "verboseError" => $outstr));
			} else
				print $json->encode(array("result" => "ok"));
		}
	}
}
pg_close($db);
?>

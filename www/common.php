<?
ini_set("display_errors", false);

$conffile = dirname(__FILE__) . "/../hostconfig";

# read conffile
$firstkey = '^[^\s"[]\S*';
$key = '\s+[^\s"[]\S*';
$str = '\s+"(?:\\\\"|\\\\\\\\|[^\\\\"])*"';
$hex = '\s+\[[[:xdigit:]]*\]';
$conf = file_get_contents($conffile);

$pattern = "$firstkey|$key|$hex|$str";
preg_match_all("/$pattern/", $conf, $splitconf, PREG_SET_ORDER);
foreach ($splitconf as &$i) {
	$i = preg_replace('/^\s*/', '', $i[0]);
}
function nxt(&$arr) {
	$foo = each($arr);
	return preg_replace('/["[](.*)[\]"]/', '$1', $foo['value']);
}
reset($splitconf);
while($v = each($splitconf)) {
	$i = $v['value'];
	if($i[0]=='"'||$i[0]=='[')
		continue;
	if($i == "userpass") {
		$adminuser = nxt($splitconf);
		$adminpass = nxt($splitconf);
	} elseif($i == "hostname") {
		$hostname = nxt($splitconf);
	} elseif($i == "serverdir") {
		$serverdir = nxt($splitconf);
	} elseif($i == "dbstring") {
		$dbstring = nxt($splitconf);
	} elseif($i == "hostkey") {
		$hostkey = nxt($splitconf);
	} elseif($i == "admin") {
		# addr:port
		list($adminaddr, $adminport) = split(":", nxt($splitconf));
	} elseif($i == "monotone") {
		$monotone = nxt($splitconf);
	}
}
reset($splitconf);

include("JSON.php");
$json = new Services_JSON();

function mktok($username, $shapass, $t) {
	$secfile = dirname(__FILE__) . "/../secfile";
	if (!is_file($secfile)) {
		$dat = "";
		foreach (array("/dev/random", "/dev/urandom") as $fn) {
			$fd = fopen($fn, "rb");
			if ($fd) {
				$dat = $dat . fread($fd, 20);
				fclose($fd);
			}
		}
		file_put_contents($secfile, $dat);
		chmod($secfile, 0400);
	}
	return sha1($username . $shapass . $t . file_get_contents($secfile));
}

if ($_REQUEST['username'] && $_REQUEST['password']) {
	$username = $_REQUEST['username'];
	if ($_REQUEST['password'] != "") {
		$shapass = sha1($_REQUEST['password']);
	}
} else if ($_COOKIE['AUTH']) {
	$auth = $json->decode(stripslashes($_COOKIE['AUTH']));
	do {
		if ($auth->token != mktok($auth->username, $auth->password,
				$auth->expiration)) {
			break;
		}
		if ($auth->expiration < time()) {
			break;
		}
		$username = $auth->username;
		$shapass = $auth->password;
	} while (false);
} else {
	$username = '';
	$shapass = '';
}

$safeuser = pg_escape_string($username);

$validuser = false;
$db = pg_connect($dbstring);
$query = sprintf("SELECT password FROM users WHERE username = '%s'", $safeuser);
$result = pg_exec($db, $query);
if ($result) {
	$rows = pg_numrows($result);
	if ($rows == 1) {
		$row = pg_fetch_row ($result, 0);
		if ($row[0] == $shapass) {
			$validuser = true;
		}
	}
}
?>

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

if ($_REQUEST['username'] && $_REQUEST['password']) {
	$username = $_REQUEST['username'];
	$password = $_REQUEST['password'];
} else if ($_COOKIE['AUTH']) {
	$auth = $json->decode(stripslashes($_COOKIE['AUTH']));
	$username = $auth->username;
	$password = $auth->password;
} else {
	$username = '';
	$password = '';
}

$safeuser = pg_escape_string($username);
$safepass = pg_escape_string($password);

$validuser = false;
$db = pg_connect($dbstring);
$query = sprintf("SELECT password FROM users WHERE username = '%s'", $safeuser);
$result = pg_exec($db, $query);
if ($result) {
	$rows = pg_numrows($result);
	if ($rows == 1) {
		$row = pg_fetch_row ($result, 0);
		if ($row[0] === $password) {
			$validuser = true;
		}
	}
}
?>

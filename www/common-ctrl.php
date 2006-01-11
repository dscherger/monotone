<?
include_once('common.php');
$args = $json->decode($GLOBALS['HTTP_RAW_POST_DATA']);

$action = $args->action; if($_REQUEST['action']) $action = $_REQUEST['action'];
$project = $args->project; if($_REQUEST['project']) $project = $_REQUEST['project'];

$safeproj = pg_escape_string($project);
$projdir = "$serverdir/projects/" . basename($project);
$projwww = "$serverdir/www/projects/" . basename($project);
$monotone = "$monotone --confdir '$projdir'";


function getarg($name) {
	global $args;
	if ($_REQUEST[$name])
		return $_REQUEST[$name];
	else
		return $args->$name;
}
function safearg($name) {
	return pg_escape_string(getarg($name));
}
function dirsafe($name) {
	return ($name == basename($name)) && ($name != '..');
}

if (!$project) {
	$project = basename(dirname($_SERVER['PHP_SELF']));
	$safeproj = pg_escape_string($project);
}
function allowed($what) {
	global $json, $permissions, $validuser, $username;
	if(!$validuser)
		print $json->encode(array("error" => "username or password is incorrect."));
	else if(!$permissions[$what])
		print $json->encode(array("error" => "You're not allowed to do that."));
	else
		return true;
	return false;
}

$permissions = array(
		"give" => false,		// grant/revoke permissions for other users
		"upload" => false,	// upload files
		"homepage" => false,	// upload new home page
		"access" => false,	// upload key, write-permissions
		"server" => false,	// start/stop server, reset db
		"description" => false
		);

if ($validuser) {
	$fields = "give, upload, homepage, access, server, description";
	$query = sprintf("SELECT %s FROM permissions WHERE ", $fields);
	$query = $query . "username = '%s' AND project = '%s'";
	$result = pg_exec($db, sprintf($query, $safeuser, $safeproj));
	if ($result) {
		$rows = pg_numrows($result);
		$permissions['rows'] = $rows;
		$permissions['safeuser'] = $safeuser;
		$permissions['safeproj'] = $safeproj;
		if ($rows == 1) {
			$row = pg_fetch_row ($result, 0);
			$permissions['give']		= ($row[0] == 1);
			$permissions['upload']		= ($row[1] == 1);
			$permissions['homepage']		= ($row[2] == 1);
			$permissions['access']		= ($row[3] == 1);
			$permissions['server']		= ($row[4] == 1);
			$permissions['description']	= ($row[5] == 1);
		}
	}
}
?>

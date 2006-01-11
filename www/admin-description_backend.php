<?
include_once("common-ctrl.php");

header('Content-type: text/x-json');

if ($action == "getdesc") {
	print $json->encode(array(
				"description" => file_get_contents($projwww . "/description"),
				"longdescription" => file_get_contents($projwww . "/longdescription")
				));
} else if ($action === "chdesc") {
	if(allowed('description')) {
		file_put_contents($projwww . "/description", $args->description, LOCK_EX);
		file_put_contents($projwww . "/longdescription", $args->longdescription, LOCK_EX);
		print $json->encode(array("description" => file_get_contents($projwww . "/description"),
					"longdescription" => file_get_contents($projwww . "/longdescription")));
	}
}
pg_close($db);
?>

<div id="page">
<?
$files = scandir("files");
?>
<div>
This project has already released the following files:<br/>
<table><thead><tr><td>File name</td><td>File description</td><td></td><td></td></tr></thead>
<tfoot><tr><td></td><td></td><td></td><td></td></tr></tfoot>
<tbody id="currentfiles-body"><?
foreach($files as $i) {
	if ($i == "." || $i == "..")
		continue;
	$inf = file_get_contents("files-about/$i");
	?>
	<tr id="trow-<?=$i?>" class="maintainerrow">
	<td><a href="files/<?=$i?>"><?=$i?></a></td>
	<td><input type="text" size=40 value="<?=$inf?>" id="filecomment-<?=$i?>"/></td>
	<td><button type="button" onclick="do_rmfile('<?=$i?>');">Remove</button></td>
	<td><button type="button" onclick="do_chfilecomment('<?=$i?>');">Change description</button></td>
	</tr>
	<?
}
print "</tbody></table>\n";
?>
<hr/>
<form method="POST" id="upload_form" action="../../admin-files_backend.php" enctype="multipart/form-data">
<input type="hidden" name="project" id="upload_proj" value="<?=$project?>"/>
<input type="hidden" name="action" value="upload_files"/>
<div id="filelist"> <!-- <input type="file" name="upfiles[]" size=40/> <input type="text" name="upfile_comments[]" size=40/><br/> --> </div><br/>
<input type="submit" value="Upload" onclick="do_upload();"/>
<button type="button" onclick="morefiles();">More</button>
</form>
<iframe id="upload_frame" style="display: none;" name="upload_frame"></iframe>
</div>

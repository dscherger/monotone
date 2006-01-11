ctrl = '../../admin-files_backend.php';

do_rmfile = function (name) {
  status("removing " + name + "...");
  var x = {'project':project,"action":"rmfile","file":name};
  call_server(ctrl, x, "rmfile", function (data) {
    hideElement("trow-" + name);
    clearstatus();
  });
}

do_chfilecomment = function (name) {
  var c = getElement("filecomment-" + name).value;
  status("changing description of " + name + "...");
  var x = {'project':project,"action":"chfiledesc","filedesc":c,"file":name};
  call_server(ctrl, x, "chfiledesc", function (data) {
    clearstatus();
  });
}

add_file_to_present = function (name, desc) {
  appendChildNodes("currentfiles-body", TR({},
  TD(null, A({'href': "files/" + name}, name)),
  TD(null, INPUT({'type':'text','size':40,'value':desc,'id':'filecomment-' + name})),
  TD(null, BUTTON({'type':'button','onclick':'do_rmfile("' + name + '");'}, "Remove")),
  TD(null, BUTTON({'type':'button','onclick':'do_chfilecomment("' + name + '");'}, "Change description"))
  ));
}

empty_row = function () {
  return TR({},
    TD(null, INPUT({"type": "file", "size": 40, "name": "upfiles[]"})),
    TD(null, INPUT({"type": "text", "size": 40, "name": "upfile_comments[]"}))
  );
}
morefiles = function() {
  appendChildNodes("filetablebody", empty_row());
}
row_display = function (row) {
  return TR(null, map(partial(TD, null), row));
}
setup_upload = function() {
    replaceChildNodes("filelist", TABLE({"id":"filetable"},
      THEAD(null, row_display(["File to upload", "File description"])),
      TFOOT(null, row_display(["",""])),
      TBODY({"id":"filetablebody"}))
      );
    morefiles();
}


do_upload = function() {
  status("Uploading page...");
  getElement("upload_form").target = "upload_frame";
  getElement("upload_form").submit();
}
uploaddone = function() {
  getElement("upload_form").reset();
  clearstatus();
}

addLoadEvent(function() {
//  status("Loading...");
//  var x = {'project':project};
//  x.action = "getmaint";
//  call_server(ctrl, x, "getmaint", function (data) {
//    set_maint_display(data.maintainers);
    setup_upload();
//    clearstatus();
//  });
});

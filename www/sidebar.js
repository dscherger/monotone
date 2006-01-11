//needs common.js

do_newuser = function () {
  var x = userpass();
  x.action = "newuser";
  status("Adding user '" + x.username + "'...");
  call_server(userctrl, x, "newuser", function (data) {
      status("Added user '" + data.username + "'.");
  });
}

do_chpass = function () {
  var x = userpass();
  x.action = "chpass";
  x.newpass = getElement("newpassword").value;
  status("Changing password...");
  call_server(userctrl, x, "chpass", function (data) {
    getElement("password").value = getElement("newpassword").value;
    getElement("newpassword").value = "";
    clearstatus();
  });
}

do_newproj = function() {
  status("Creating project...");
  var args = {'project':getElement('newproject').value};
  args.action = "new_project";
  call_server('proj-ctrl.php', args, "newproj", function(data) {
    window.location = "projects/" + data.name + "/admin.php";
  });
}

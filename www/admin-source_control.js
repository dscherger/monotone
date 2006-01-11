ctrl = '../../admin-source_control_backend.php';

chwperm = function () {
  status("Changing permissions...");
  var args = {'project':project};
  args.newperm = getElement("newperm").value;
  args.action = "chwriters";
  call_server(ctrl, args, "chwperm", function(data){clearstatus();});
}

sendkey = function() {
  status("Uploading key...");
  var args = {'project':project};
  args.action = "new_key";
  args.keydata = getElement("keydata").value;
  call_server(ctrl, args, "sendkey", function(data){
    getElement("keydata").value = "";
    status("Key uploaded.");
  });
}

resetdb = function() {
  status("Resetting database...");
  var args = {'project':project};
  args.action = "resetdb";
  args.oath = getElement("oath").value;
  call_server(ctrl, args, "resetdb", function(data){
    getElement("oath").value = "";
    status(data.ok);
  });
}

chstate = function() {
  status("Changing server state...");
  var args = {'project':project};
  if (getElement("chstate").value == "Disable server") {
    args.action = "disable";
  } else {
    args.action = "enable";
  }
  call_server(ctrl, args, "chstate", function(data){
    set_state_display(data.status);
    clearstatus();
  });
}

set_state_display = function(state){
  if (state == "") {
    replaceChildNodes("serverstate", "(server not found)");
  } else {
    replaceChildNodes("serverstate", state);
  }
  // set 'chstate' button to enable/disable appropriately
  if (state.match("WAITING") || state == "RUNNING" || state == "SLEEPING") {
    getElement("chstate").value = "Disable server";
  } else {
    getElement("chstate").value = "Enable server";
  }
}

restate = function() {
  status("Refreshing server state...");
  var args = {'project':project};
  args.action = "refresh_state";
  call_server(ctrl, args, "restate", function(data){
    set_state_display(data.status);
    clearstatus();
  });
}

addLoadEvent(function() {
  status("Loading...");
  var x = {'project':project};
  x.action = "getsrv";
  call_server(ctrl, x, "getsrv", function (data) {
    if (data.wperm) {
      getElement("newperm").value = data.wperm;
    }
    set_state_display(data.status);
    clearstatus();
  });
});

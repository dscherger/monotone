ctrl = '../../admin-description_backend.php';

chdesc = function () {
  status("Changing project description...");
  var args = {'project':project};
  args.description = getElement("description").value;
  args.longdescription = getElement("longdescription").value;
  args.action = "chdesc";
  call_server(ctrl, args, "chdesc", function(data){clearstatus();});
}

addLoadEvent(function() {
  status("Loading...");
  var x = {'project':project};
  x.action = "getdesc";
  call_server(ctrl, x, "getdesc", function (data) {
    if (data.description) {
      getElement("description").value = data.description;
    }
    if (data.longdescription) {
      getElement("longdescription").value = data.longdescription;
    }
    clearstatus();
  });
});

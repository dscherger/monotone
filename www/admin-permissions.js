ctrl = '../../admin-permissions_backend.php';

row_display = function (row) {
  return TR(null, map(partial(TD, null), row));
}
make_cbox_properties = function (row, who) {
  var out = {};
  out['type'] = "checkbox";
  out['name'] = "maint_" + who + "[]";
  if(row[who]) {
    out['checked'] = true;
  }
  return out;
}
maint_row = function (row) {
  return TR({"class":"maintainerrow"},
    TD(null, INPUT({"type": "text",     "name": "maint_username[]",    "value": row.username})),
    TD(null, INPUT(make_cbox_properties(row, "give"))),
    TD(null, INPUT(make_cbox_properties(row, "server"))),
    TD(null, INPUT(make_cbox_properties(row, "upload"))),
    TD(null, INPUT(make_cbox_properties(row, "description")))/*,
    TD(null, INPUT(make_cbox_properties(row, "homepage"))),
    TD(null, INPUT(make_cbox_properties(row, "resources")))*/
  );
}
empty_row = function () {
  return TR({"class":"maintainerrow"},
    TD(null, INPUT({"type": "text",     "name": "maint_username[]"})),
    TD(null, INPUT({"type": "checkbox", "name": "maint_give[]"})),
    TD(null, INPUT({"type": "checkbox", "name": "maint_server[]"})),
    TD(null, INPUT({"type": "checkbox", "name": "maint_upload[]"})),
    TD(null, INPUT({"type": "checkbox", "name": "maint_description[]"}))/*,
    TD(null, INPUT({"type": "checkbox", "name": "maint_homepage[]"})),
    TD(null, INPUT({"type": "checkbox", "name": "maint_resources[]"}))*/
  );
}
maint_addrow = function() {
  appendChildNodes("maintainertablebody", empty_row());
}
set_maint_display = function(data) {
    var labels = [];
    replaceChildNodes("maintainerlist", TABLE({"id":"maintainertable","class":"hilighttable"},
      THEAD(null, row_display(["Username", "Edit maintainers", "Control server", "Upload files", "Edit description"/*, "Edit homepage", "Change resources"*/])),
      TFOOT(null, row_display(["","","","",""/*,"",""*/])),
      TBODY({"id":"maintainertablebody"}, map(maint_row, data)))
      );
    maint_addrow();
}
read_maint_line = function(row) {
  var out = {};
  for(var i = 0; i < row.childNodes.length; ++i) {
    var cur = row.childNodes[i].firstChild;
    var nm = getNodeAttribute(cur,"name");
    nm = nm.replace(/.*_(.*)\[\]/, "$1");
    if (nm == "username") {
      out[nm] = cur.value;
    } else {
      out[nm] = cur.checked;
    }
  }
  return out;
}


chmaint = function () {
  status("Changing maintainers...");
  var args = {'project':project};
  args.action = "chmaint";
  var lst = getElement("maintainertablebody").childNodes;
  var maints = [];
  for(var i = 0; i < lst.length; ++i) {
    var line = read_maint_line(lst[i]);
    if (line.username != "") {
      maints.push(line);
    }
  }
  args.newmaint = maints;
  call_server(ctrl, args, "chmaint", function (data) {
    set_maint_display(data.maintainers);
    clearstatus();
  });
}


addLoadEvent(function() {
  status("Loading...");
  var x = {'project':project};
  x.action = "getmaint";
  call_server(ctrl, x, "getmaint", function (data) {
    set_maint_display(data.maintainers);
    clearstatus();
  });
});

ctrl = '../../admin-resources_backend.php';

row_display = function (row) {
  return TR(null, map(partial(TD, null), row));
}
make_row = function (row) {
  return TR({"class":"resourcerow"},
    TD(null, INPUT({"type": "text", "size": 30, "name": "rscname[]", "value": row.name})),
    TD(null, INPUT({"type": "text", "size": 40, "name": "rscurl[]",  "value": row.url}))
  );
}
addrow = function() {
  appendChildNodes("rtblbody", TR({"class":"resourcerow"},
    TD(null, INPUT({"type": "text", "size": 30, "name": "rscname[]"})),
    TD(null, INPUT({"type": "text", "size": 40, "name": "rscurl[]"}))
  ));
}
set_display = function(data) {
    var labels = [];
    replaceChildNodes("resourcediv", TABLE({"id":"rtbl","class":"hilighttable"},
      THEAD(null, row_display(["Name", "URL"])),
      TFOOT(null, row_display(["",""])),
      TBODY({"id":"rtblbody"}, map(make_row, data)))
      );
    addrow();
}
read_line = function(row) {
  var out = {};
  for(var i = 0; i < row.childNodes.length; ++i) {
    var cur = row.childNodes[i].firstChild;
    var nm = getNodeAttribute(cur,"name");
    nm = nm.replace(/rsc(.*)\[\]/, "$1");
    out[nm] = cur.value;
  }
  return out;
}


update = function () {
  status("Updating...");
  var args = {'project':project};
  args.action = "chresources";
  var lst = getElement("rtblbody").childNodes;
  var maints = [];
  for(var i = 0; i < lst.length; ++i) {
    var line = read_line(lst[i]);
    if (line.username != "") {
      maints.push(line);
    }
  }
  args.newmaint = maints;
  call_server(ctrl, args, "chresources", function (data) {
    set_display(data.resources);
    clearstatus();
  });
}


addLoadEvent(function() {
  status("Loading...");
  var x = {'project':project};
  x.action = "getresources";
  call_server(ctrl, x, "getresources", function (data) {
    set_display(data.resources);
    clearstatus();
  });
});

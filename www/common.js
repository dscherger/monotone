var hostname = "localhost";
var userctrl = "user-ctrl.php";

var todo = {};
var verberr = "";
var respTxt = "";

// POST an object in JSON format, and get a return object also in JSON format
loadJSONPost = function (url, obj) {
  respTxt = "";
  var data = serializeJSON(obj);

  var req = getXMLHttpRequest();
  req.open("POST", url, true);
  req.setRequestHeader('Content-Type', 'text/x-json'); 
  var d = sendXMLHttpRequest(req, data);
  d = d.addCallback(function(data){respTxt = data.responseText; return data;});
  d = d.addCallback(evalJSONRequest);
  return d;
}

call_server = function (url, args, nam, callback) {
  var d = loadJSONPost(url, args);
  todo[nam] = d;
  d.addCallback(function (data) {
    if (!isNull(data.error)) {
      if(isNull(data.verboseError)) {
        status("Error: " + data.error);
      } else {
        verberr = data.verboseError;
        status("Error: " + data.error, BR(), SPAN({"class": "jslink", "onclick": "alert(verberr);"}, "(Click here for details)"));
      }
    } else {
      callback(data);
    }
  });
  d.addErrback(function(err){
    status("Error: " + err, BR(), SPAN({"class": "jslink", "onclick": "alert(respTxt);"}, "(Click here for details)"));
  });
}

userpass = function () {
  var x = {};
  x.username = getElement("username").value;
  x.password = getElement("password").value;
  return x;
}

userpassproj = function () {
  var x = {};
  x.username = getElement("username").value;
  x.password = getElement("password").value;
  x.project = getElement("project").value;
  return x;
}


clearstatus = function() {
  replaceChildNodes('status');
}

status = function() {
    replaceChildNodes("status", SPAN({"class": "jslink", "onclick": "clearstatus();", "style": "float: right;"}, "[X]"), arguments);
}


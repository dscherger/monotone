
var theBox;
var callbacksInstalled = false;
var pendingDeferred = null;

function installCallbacks()
{
  if (callbacksInstalled != false) {
    return;
  }
  callbacksInstalled = true;

  cbinst = function (e) {
    updateNodeAttributes(e, { "onmouseover" : partial(mouseOverHandler, e), 
			     'onmouseout' : partial(mouseOutHandler, e) } );
  }

  var elems = getElementsByTagAndClassName(null, "branchLink");
  map(cbinst, elems);

  var elems = getElementsByTagAndClassName(null, "revisionLink");
  map(cbinst, elems);

  var elems = getElementsByTagAndClassName(null, "manifestLink");
  map(cbinst, elems);

  theBox = $("popupBox");
}

function updatePopup(boundTo, className)
{
  jsonData = boundTo.jsonData;

  var pos  = elementPosition(boundTo);
  var newBox;

  info = null;
  if (jsonData.type == "branch") {
    info = "branch last updated " + jsonData.ago + " by " + jsonData.author;
  } else if (jsonData.type == "revision") {
    info = jsonData.ago + " ago by " + jsonData.author;
  } else if (jsonData.type == "manifest") {
    info = "manifest contains " + jsonData.file_count + " files in " + jsonData.directory_count + " directories.";
  } else {
    info = "unknown type: " + jsonData.type;
  }

  newBox = DIV({ 'id' : 'popupBox'}, 
	       info);

  if (boundTo.offsetHeight) {
    offset_height = boundTo.offsetHeight;
  } else {
    offset_height = 24; // yick
  }

  newY = pos.y + offset_height + 20;
  newX = pos.x + 20;

  newBox.style.top = newY + 'px';
  newBox.style.left = newX + 'px';
  swapDOM(theBox, newBox);
  theBox = newBox;
}

function jsonLoadComplete(boundTo, className, jsonData)
{
  boundTo.jsonData = jsonData;
  updatePopup(boundTo, className);
  pendingDeferred = null;
}

function mouseOverHandler(boundTo, evt)
{
  var className = getNodeAttribute(boundTo, "class");

  if (boundTo.jsonData) {
    return updatePopup(boundTo, className);
  }

  links = getElementsByTagAndClassName('a', null, boundTo);

  if (links.length > 0) {
    linkHref = links[0].href;
  } else {
    return;
  }

  var uri = "getjson.py?className=" + encodeURIComponent(className) + "&linkUri=" + encodeURIComponent(linkHref);
  var d = loadJSONDoc(uri);

  d.addCallback(jsonLoadComplete, boundTo, className);
  pendingDeferred = d;
}

function mouseOutHandler(boundTo, evt)
{
  if (pendingDeferred != null) {
    pendingDeferred.cancel();
    pendingDeferred = null;
  }
  var newBox = DIV({'id' : 'popupBox', 'class' : 'invisible'});
  swapDOM(theBox, newBox);
  theBox = newBox;
}

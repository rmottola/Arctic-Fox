/**
 * ChromeUtils.js is a set of mochitest utilities that are used to 
 * synthesize events in the browser.  These are only used by 
 * mochitest-chrome and browser-chrome tests.  Originally these functions were in 
 * EventUtils.js, but when porting to specialPowers, we didn't want
 * to move unnecessary functions.
 *
 */

const EventUtils = {};
const scriptLoader = Components.classes["@mozilla.org/moz/jssubscript-loader;1"].
                   getService(Components.interfaces.mozIJSSubScriptLoader);
scriptLoader.loadSubScript("chrome://mochikit/content/tests/SimpleTest/EventUtils.js", EventUtils);

/**
 * Synthesize a query text content event.
 *
 * @param aOffset  The character offset.  0 means the first character in the
 *                 selection root.
 * @param aLength  The length of getting text.  If the length is too long,
 *                 the extra length is ignored.
 * @param aWindow  Optional (If null, current |window| will be used)
 * @return         An nsIQueryContentEventResult object.  If this failed,
 *                 the result might be null.
 */
function synthesizeQueryTextContent(aOffset, aLength, aWindow)
{
  var utils = _getDOMWindowUtils(aWindow);
  if (!utils) {
    return nullptr;
  }
  return utils.sendQueryContentEvent(utils.QUERY_TEXT_CONTENT,
                                     aOffset, aLength, 0, 0,
                                     QUERY_CONTENT_FLAG_USE_NATIVE_LINE_BREAK);
}

/**
 * Synthesize a query text rect event.
 *
 * @param aOffset  The character offset.  0 means the first character in the
 *                 selection root.
 * @param aLength  The length of the text.  If the length is too long,
 *                 the extra length is ignored.
 * @param aWindow  Optional (If null, current |window| will be used)
 * @return         An nsIQueryContentEventResult object.  If this failed,
 *                 the result might be null.
 */
function synthesizeQueryTextRect(aOffset, aLength, aWindow)
{
  var utils = _getDOMWindowUtils(aWindow);
  if (!utils) {
    return nullptr;
  }
  return utils.sendQueryContentEvent(utils.QUERY_TEXT_RECT,
                                     aOffset, aLength, 0, 0,
                                     QUERY_CONTENT_FLAG_USE_NATIVE_LINE_BREAK);
}

/**
 * Synthesize a query editor rect event.
 *
 * @param aWindow  Optional (If null, current |window| will be used)
 * @return         An nsIQueryContentEventResult object.  If this failed,
 *                 the result might be null.
 */
function synthesizeQueryEditorRect(aWindow)
{
  var utils = _getDOMWindowUtils(aWindow);
  if (!utils) {
    return nullptr;
  }
  return utils.sendQueryContentEvent(utils.QUERY_EDITOR_RECT, 0, 0, 0, 0,
                                     QUERY_CONTENT_FLAG_USE_NATIVE_LINE_BREAK);
}

/**
 * Synthesize a character at point event.
 *
 * @param aX, aY   The offset in the client area of the DOM window.
 * @param aWindow  Optional (If null, current |window| will be used)
 * @return         An nsIQueryContentEventResult object.  If this failed,
 *                 the result might be null.
 */
function synthesizeCharAtPoint(aX, aY, aWindow)
{
  var utils = _getDOMWindowUtils(aWindow);
  if (!utils) {
    return nullptr;
  }
  return utils.sendQueryContentEvent(utils.QUERY_CHARACTER_AT_POINT,
                                     0, 0, aX, aY,
                                     QUERY_CONTENT_FLAG_USE_NATIVE_LINE_BREAK);
}

/**
 * Emulate a dragstart event.
 *  element - element to fire the dragstart event on
 *  expectedDragData - the data you expect the data transfer to contain afterwards
 *                      This data is in the format:
 *                         [ [ {type: value, data: value, test: function}, ... ], ... ]
 *                     can be null
 *  aWindow - optional; defaults to the current window object.
 *  x - optional; initial x coordinate
 *  y - optional; initial y coordinate
 * Returns null if data matches.
 * Returns the event.dataTransfer if data does not match
 *
 * eqTest is an optional function if comparison can't be done with x == y;
 *   function (actualData, expectedData) {return boolean}
 *   @param actualData from dataTransfer
 *   @param expectedData from expectedDragData
 * see bug 462172 for example of use
 *
 */
function synthesizeDragStart(element, expectedDragData, aWindow, x, y)
{
  return EventUtils.synthesizeDragStart(element, expectedDragData,
                                        aWindow, x, y);
}

/**
 * Emulate a drop by emulating a dragstart and firing events dragenter, dragover, and drop.
 *  srcElement - the element to use to start the drag, usually the same as destElement
 *               but if destElement isn't suitable to start a drag on pass a suitable
 *               element for srcElement
 *  destElement - the element to fire the dragover, dragleave and drop events
 *  dragData - the data to supply for the data transfer
 *                     This data is in the format:
 *                       [ [ {type: value, data: value}, ...], ... ]
 *  dropEffect - the drop effect to set during the dragstart event, or 'move' if null
 *  aWindow - optional; defaults to the current window object.
 *  aDestWindow - optional; defaults to aWindow.
 *                Used when destElement is in a different window than srcElement.
 *  aDragEvent - optional; defaults to empty object.
 *                overwrite a event object passed to EventUtils.sendDragEvent
 *
 * Returns the drop effect that was desired.
 */
function synthesizeDrop(srcElement, destElement, dragData, dropEffect, aWindow, aDestWindow, aDragEvent={})
{
  if (!aWindow)
    aWindow = window;
  if (!aDestWindow)
    aDestWindow = aWindow;

  var ds = Components.classes["@mozilla.org/widget/dragservice;1"].
           getService(Components.interfaces.nsIDragService);

  var dataTransfer;
  var trapDrag = function(event) {
    dataTransfer = event.dataTransfer;
    for (var i = 0; i < dragData.length; i++) {
      var item = dragData[i];
      for (var j = 0; j < item.length; j++) {
        dataTransfer.mozSetDataAt(item[j].type, item[j].data, i);
      }
    }
    dataTransfer.dropEffect = dropEffect || "move";
    event.preventDefault();
    event.stopPropagation();
  }

  ds.startDragSession();

  try {
    // need to use real mouse action
    aWindow.addEventListener("dragstart", trapDrag, true);
    EventUtils.synthesizeMouseAtCenter(srcElement, { type: "mousedown" }, aWindow);

    var rect = srcElement.getBoundingClientRect();
    var x = rect.width / 2;
    var y = rect.height / 2;
    EventUtils.synthesizeMouse(srcElement, x, y, { type: "mousemove" }, aWindow);
    EventUtils.synthesizeMouse(srcElement, x+10, y+10, { type: "mousemove" }, aWindow);
    aWindow.removeEventListener("dragstart", trapDrag, true);

    var destRect = destElement.getBoundingClientRect();
    var destClientX = destRect.left + destRect.width / 2;
    var destClientY = destRect.top + destRect.height / 2;
    var destScreenX = aDestWindow.mozInnerScreenX + destClientX;
    var destScreenY = aDestWindow.mozInnerScreenY + destClientY;
    if ("clientX" in aDragEvent && !("screenX" in aDragEvent)) {
      aDragEvent.screenX = aDestWindow.mozInnerScreenX + aDragEvent.clientX;
    }
    if ("clientY" in aDragEvent && !("screenY" in aDragEvent)) {
      aDragEvent.screenY = aDestWindow.mozInnerScreenY + aDragEvent.clientY;
    }

    var event = Object.assign({ type: "dragenter",
                                screenX: destScreenX, screenY: destScreenY,
                                clientX: destClientX, clientY: destClientY,
                                dataTransfer: dataTransfer }, aDragEvent);
    EventUtils.sendDragEvent(event, destElement, aDestWindow);

    event = Object.assign({ type: "dragover",
                            screenX: destScreenX, screenY: destScreenY,
                            clientX: destClientX, clientY: destClientY,
                            dataTransfer: dataTransfer }, aDragEvent);
    if (EventUtils.sendDragEvent(event, destElement, aDestWindow)) {
      EventUtils.synthesizeMouseAtCenter(destElement, { type: "mouseup" }, aDestWindow);
      return "none";
    }

    if (dataTransfer.dropEffect != "none") {
      event = Object.assign({ type: "drop",
                              screenX: destScreenX, screenY: destScreenY,
                              clientX: destClientX, clientY: destClientY,
                              dataTransfer: dataTransfer }, aDragEvent);
      EventUtils.sendDragEvent(event, destElement, aDestWindow);
    }

    EventUtils.synthesizeMouseAtCenter(destElement, { type: "mouseup" }, aDestWindow);

    return dataTransfer.dropEffect;
  } finally {
    ds.endDragSession(true);
  }
};

var PluginUtils =
{
  withTestPlugin : function(callback)
  {
    if (typeof Components == "undefined")
    {
      todo(false, "Not a Mozilla-based browser");
      return false;
    }

    var ph = Components.classes["@mozilla.org/plugin/host;1"]
                       .getService(Components.interfaces.nsIPluginHost);
    var tags = ph.getPluginTags();

    // Find the test plugin
    for (var i = 0; i < tags.length; i++)
    {
      if (tags[i].name == "Test Plug-in")
      {
        callback(tags[i]);
        return true;
      }
    }
    todo(false, "Need a test plugin on this platform");
    return false;
  }
};

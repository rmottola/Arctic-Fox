/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";var _slicedToArray = function () {function sliceIterator(arr, i) {var _arr = [];var _n = true;var _d = false;var _e = undefined;try {for (var _i = arr[Symbol.iterator](), _s; !(_n = (_s = _i.next()).done); _n = true) {_arr.push(_s.value);if (i && _arr.length === i) break;}} catch (err) {_d = true;_e = err;} finally {try {if (!_n && _i["return"]) _i["return"]();} finally {if (_d) throw _e;}}return _arr;}return function (arr, i) {if (Array.isArray(arr)) {return arr;} else if (Symbol.iterator in Object(arr)) {return sliceIterator(arr, i);} else {throw new TypeError("Invalid attempt to destructure non-iterable instance");}};}();var _typeof = typeof Symbol === "function" && typeof Symbol.iterator === "symbol" ? function (obj) {return typeof obj;} : function (obj) {return obj && typeof Symbol === "function" && obj.constructor === Symbol ? "symbol" : typeof obj;};function _toConsumableArray(arr) {if (Array.isArray(arr)) {for (var i = 0, arr2 = Array(arr.length); i < arr.length; i++) {arr2[i] = arr[i];}return arr2;} else {return Array.from(arr);}}var _Components = 

Components;var Cc = _Components.classes;var Ci = _Components.interfaces;var Cu = _Components.utils;

Cu.import("resource://services-common/utils.js");
Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("chrome://loop/content/modules/MozLoopService.jsm");
Cu.import("chrome://loop/content/modules/LoopRooms.jsm");
Cu.importGlobalProperties(["Blob"]);

XPCOMUtils.defineLazyModuleGetter(this, "NewTabURL", 
"resource:///modules/NewTabURL.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "PageMetadata", 
"resource://gre/modules/PageMetadata.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "PluralForm", 
"resource://gre/modules/PluralForm.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "UpdateUtils", 
"resource://gre/modules/UpdateUtils.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "UITour", 
"resource:///modules/UITour.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "Social", 
"resource:///modules/Social.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "Promise", 
"resource://gre/modules/Promise.jsm");
XPCOMUtils.defineLazyGetter(this, "appInfo", function () {
  return Cc["@mozilla.org/xre/app-info;1"].
  getService(Ci.nsIXULAppInfo).
  QueryInterface(Ci.nsIXULRuntime);});

XPCOMUtils.defineLazyServiceGetter(this, "clipboardHelper", 
"@mozilla.org/widget/clipboardhelper;1", 
"nsIClipboardHelper");
XPCOMUtils.defineLazyServiceGetter(this, "extProtocolSvc", 
"@mozilla.org/uriloader/external-protocol-service;1", 
"nsIExternalProtocolService");
this.EXPORTED_SYMBOLS = ["LoopAPI"];

var cloneableError = function cloneableError(source) {
  // Simple Object that can be cloned over.
  var error = {};
  if (typeof source == "string") {
    source = new Error(source);}


  var props = Object.getOwnPropertyNames(source);
  // nsIException properties are not enumerable, so we'll try to copy the most
  // common and useful ones.
  if (!props.length) {
    props.push("message", "filename", "lineNumber", "columnNumber", "stack");}var _iteratorNormalCompletion = true;var _didIteratorError = false;var _iteratorError = undefined;try {

    for (var _iterator = props[Symbol.iterator](), _step; !(_iteratorNormalCompletion = (_step = _iterator.next()).done); _iteratorNormalCompletion = true) {var prop = _step.value;
      var value = source[prop];
      var type = typeof value === "undefined" ? "undefined" : _typeof(value);

      // Functions can't be cloned. Period.
      // For nsIException objects, the property may not be defined.
      if (type == "function" || type == "undefined") {
        continue;}

      // Don't do anything to members that are already cloneable.
      if (/boolean|number|string/.test(type)) {
        error[prop] = value;} else 
      {
        // Convert non-compatible types to a String.
        error[prop] = "" + value;}}



    // Mark the object as an Error, otherwise it won't be discernable from other,
    // regular objects.
  } catch (err) {_didIteratorError = true;_iteratorError = err;} finally {try {if (!_iteratorNormalCompletion && _iterator.return) {_iterator.return();}} finally {if (_didIteratorError) {throw _iteratorError;}}}error.isError = true;

  return error;};


var getObjectAPIFunctionName = function getObjectAPIFunctionName(action) {
  var funcName = action.split(":").pop();
  return funcName.charAt(0).toLowerCase() + funcName.substr(1);};


/**
 *  Checks that [browser.js]'s global variable `gMultiProcessBrowser` is active,
 *  instead of checking on first available browser element.
 *  :see bug 1257243 comment 5:
 */
var isMultiProcessActive = function isMultiProcessActive() {
  var win = Services.wm.getMostRecentWindow("navigator:browser");
  return !!win.gMultiProcessBrowser;};


var gAppVersionInfo = null;
var gBrowserSharingListeners = new Set();
var gBrowserSharingWindows = new Set();
var gPageListeners = null;
var gOriginalPageListeners = null;
var gStringBundle = null;
var gStubbedMessageHandlers = null;
var kBatchMessage = "Batch";
var kMaxLoopCount = 10;
var kMessageName = "Loop:Message";
var kPushMessageName = "Loop:Message:Push";
var kPushSubscription = "pushSubscription";
var kRoomsPushPrefix = "Rooms:";
var kMauPrefMap = new Map(
Object.getOwnPropertyNames(LOOP_MAU_TYPE).map(function (name) {
  var parts = name.toLowerCase().split("_");
  return [LOOP_MAU_TYPE[name], parts[0] + parts[1].charAt(0).toUpperCase() + parts[1].substr(1)];}));



/**
 * WARNING: Every function in kMessageHandlers must call the reply() function,
 * as otherwise the content requesters can be left hanging.
 *
 * Ideally, we should rewrite them to handle failure/long times better, at which
 * point this could be relaxed slightly.
 */
var kMessageHandlers = { 
  /**
   * Start browser sharing, which basically means to start listening for tab
   * switches and passing the new window ID to the sender whenever that happens.
   *
   * @param {Object}   message Message meant for the handler function, containing
   *                           the following parameters in its `data` property:
   *                           [
   *                             {String} roomToken The room ID to start browser sharing and listeners.
   *                           ]
   * @param {Function} reply   Callback function, invoked with the result of this
   *                           message handler. The result will be sent back to
   *                           the senders' channel.
   */
  AddBrowserSharingListener: function AddBrowserSharingListener(message, reply) {
    var win = Services.wm.getMostRecentWindow("navigator:browser");
    var browser = win && win.gBrowser.selectedBrowser;
    if (!win || !browser) {
      // This may happen when an undocked conversation window is the only
      // window left.
      var err = new Error("No tabs available to share.");
      MozLoopService.log.error(err);
      reply(cloneableError(err));
      return;}


    var autoStart = MozLoopService.getLoopPref("remote.autostart");
    if (!autoStart && browser.getAttribute("remote") == "true") {
      // Tab sharing might not be supported yet for e10s-enabled browsers.
      var _err = new Error("Tab sharing is not supported for e10s-enabled browsers");
      MozLoopService.log.error(_err);
      reply(cloneableError(_err));
      return;}


    // get room token from message
    var _message$data = _slicedToArray(message.data, 1);var windowId = _message$data[0];
    // For rooms, the windowId === roomToken. If we change the type of place we're
    // sharing from in the future, we may need to change this.
    win.LoopUI.startBrowserSharing(windowId);

    // Point new tab to load about:home to avoid accidentally sharing top sites.
    NewTabURL.override("about:home");

    gBrowserSharingWindows.add(Cu.getWeakReference(win));
    gBrowserSharingListeners.add(windowId);
    reply();}, 


  /**
   * Creates a layout for the remote cursor on the browser chrome,
   * and positions it on the received coordinates.
   *
   * @param {Object}  message Message meant for the handler function, contains
   *                          the following parameters in its 'data' property:
   *                          {
   *                            ratioX: cursor's X position (between 0-1)
   *                            ratioY: cursor's Y position (between 0-1)
   *                          }
   *
   * @param {Function} reply  Callback function, invoked with the result of the
   *                          message handler. The result will be sent back to
   *                          the senders' channel.
   */
  AddRemoteCursorOverlay: function AddRemoteCursorOverlay(message, reply) {
    var win = Services.wm.getMostRecentWindow("navigator:browser");
    if (win) {
      win.LoopUI.addRemoteCursor(message.data[0]);}


    reply();}, 


  /**
   * Shows the click event on the remote cursor.
   *
   * @param {Object}  message Message meant for the handler function, contains
   *                          a boolean for the click event in its 'data' prop.
   *
   * @param {Function} reply  Callback function, invoked with the result of the
   *                          message handler. The result will be sent back to
   *                          the senders' channel.
   */
  ClickRemoteCursor: function ClickRemoteCursor(message, reply) {
    var win = Services.wm.getMostRecentWindow("navigator:browser");
    if (win) {
      win.LoopUI.clickRemoteCursor(message.data[0]);}


    reply();}, 


  /**
   * Associates a session-id and a call-id with a window for debugging.
   *
   * @param {Object}   message Message meant for the handler function, containing
   *                           the following parameters in its `data` property:
   *                           [
   *                             {String} windowId  The window id.
   *                             {String} sessionId OT session id.
   *                             {String} callId    The callId on the server.
   *                           ]
   * @param {Function} reply   Callback function, invoked with the result of this
   *                           message handler. The result will be sent back to
   *                           the senders' channel.
   */
  AddConversationContext: function AddConversationContext(message, reply) {var _message$data2 = _slicedToArray(
    message.data, 3);var windowId = _message$data2[0];var sessionId = _message$data2[1];var callid = _message$data2[2];
    MozLoopService.addConversationContext(windowId, { 
      sessionId: sessionId, 
      callId: callid });

    reply();}, 


  /**
   * Composes an email via the external protocol service.
   *
   * @param {Object}   message Message meant for the handler function, containing
   *                           the following parameters in its `data` property:
   *                           [
   *                             {String} subject   Subject of the email to send
   *                             {String} body      Body message of the email to send
   *                             {String} recipient Recipient email address (optional)
   *                           ]
   * @param {Function} reply   Callback function, invoked with the result of this
   *                           message handler. The result will be sent back to
   *                           the senders' channel.
   */
  ComposeEmail: function ComposeEmail(message, reply) {var _message$data3 = _slicedToArray(
    message.data, 3);var subject = _message$data3[0];var body = _message$data3[1];var recipient = _message$data3[2];
    recipient = recipient || "";
    var mailtoURL = "mailto:" + encodeURIComponent(recipient) + 
    "?subject=" + encodeURIComponent(subject) + 
    "&body=" + encodeURIComponent(body);
    extProtocolSvc.loadURI(CommonUtils.makeURI(mailtoURL));
    reply();}, 


  /**
   * Show a confirmation dialog with the standard - localized - 'Yes'/ 'No'
   * buttons or custom labels.
   *
   * @param {Object}   message Message meant for the handler function, containing
   *                           the following parameters in its `data` property:
   *                           [
   *                             {Object} options Options for the confirm dialog:
   *                               - {String} message        Message body for the dialog
   *                               - {String} [okButton]     Label for the OK button
   *                               - {String} [cancelButton] Label for the Cancel button
   *                           ]
   * @param {Function} reply   Callback function, invoked with the result of this
   *                           message handler. The result will be sent back to
   *                           the senders' channel.
   */
  Confirm: function Confirm(message, reply) {
    var options = message.data[0];
    var buttonFlags = void 0;
    if (options.okButton && options.cancelButton) {
      buttonFlags = 
      Ci.nsIPrompt.BUTTON_POS_0 * Ci.nsIPrompt.BUTTON_TITLE_IS_STRING + 
      Ci.nsIPrompt.BUTTON_POS_1 * Ci.nsIPrompt.BUTTON_TITLE_IS_STRING;} else 
    if (!options.okButton && !options.cancelButton) {
      buttonFlags = Services.prompt.STD_YES_NO_BUTTONS;} else 
    {
      reply(cloneableError("confirm: missing button options"));
      return;}


    try {
      var chosenButton = Services.prompt.confirmEx(null, "", 
      options.message, buttonFlags, options.okButton, options.cancelButton, 
      null, null, {});

      reply(chosenButton == 0);} 
    catch (ex) {
      reply(ex);}}, 



  /**
   * Copies passed string onto the system clipboard.
   *
   * @param {Object}   message Message meant for the handler function, containing
   *                           the following parameters in its `data` property:
   *                           [
   *                             {String} str The string to copy
   *                           ]
   * @param {Function} reply   Callback function, invoked with the result of this
   *                           message handler. The result will be sent back to
   *                           the senders' channel.
   */
  CopyString: function CopyString(message, reply) {
    var str = message.data[0];
    clipboardHelper.copyString(str);
    reply();}, 


  /**
   * Returns a new GUID (UUID) in curly braces format.
   *
   * @param {Object}   message Message meant for the handler function, containing
   *                           the following parameters in its `data` property:
   *                           [ ]
   * @param {Function} reply   Callback function, invoked with the result of this
   *                           message handler. The result will be sent back to
   *                           the senders' channel.
   */
  GenerateUUID: function GenerateUUID(message, reply) {
    reply(MozLoopService.generateUUID());}, 


  /**
   * Fetch the JSON blob of localized strings from the loop.properties bundle.
   * @see MozLoopService#getStrings
   *
   * @param {Object}   message Message meant for the handler function, containing
   *                           the following parameters in its `data` property:
   *                           [ ]
   * @param {Function} reply   Callback function, invoked with the result of this
   *                           message handler. The result will be sent back to
   *                           the senders' channel.
   */
  GetAllStrings: function GetAllStrings(message, reply) {
    if (gStringBundle) {
      reply(gStringBundle);
      return;}


    // Get the map of strings.
    var strings = MozLoopService.getStrings();
    // Convert it to an object.
    gStringBundle = {};var _iteratorNormalCompletion2 = true;var _didIteratorError2 = false;var _iteratorError2 = undefined;try {
      for (var _iterator2 = strings.entries()[Symbol.iterator](), _step2; !(_iteratorNormalCompletion2 = (_step2 = _iterator2.next()).done); _iteratorNormalCompletion2 = true) {var _ref = _step2.value;var _ref2 = _slicedToArray(_ref, 2);var key = _ref2[0];var value = _ref2[1];
        gStringBundle[key] = value;}} catch (err) {_didIteratorError2 = true;_iteratorError2 = err;} finally {try {if (!_iteratorNormalCompletion2 && _iterator2.return) {_iterator2.return();}} finally {if (_didIteratorError2) {throw _iteratorError2;}}}

    reply(gStringBundle);}, 


  /**
   * Fetch all constants that are used both on the client and the chrome-side.
   *
   * @param {Object}   message Message meant for the handler function, containing
   *                           the following parameters in its `data` property:
   *                           [ ]
   * @param {Function} reply   Callback function, invoked with the result of this
   *                           message handler. The result will be sent back to
   *                           the senders' channel.
   */
  GetAllConstants: function GetAllConstants(message, reply) {
    reply({ 
      COPY_PANEL: COPY_PANEL, 
      LOOP_SESSION_TYPE: LOOP_SESSION_TYPE, 
      LOOP_MAU_TYPE: LOOP_MAU_TYPE, 
      ROOM_CREATE: ROOM_CREATE, 
      SHARING_ROOM_URL: SHARING_ROOM_URL });}, 



  /**
   * Returns the app version information for use during feedback.
   *
   * @param {Object}   message Message meant for the handler function, containing
   *                           the following parameters in its `data` property:
   *                           [ ]
   * @param {Function} reply   Callback function, invoked with the result of this
   *                           message handler. The result will be sent back to
   *                           the senders' channel.
   * @return {Object} An object containing:
   *   - channel: The update channel the application is on
   *   - version: The application version
   *   - OS: The operating system the application is running on
   */
  GetAppVersionInfo: function GetAppVersionInfo(message, reply) {
    if (!gAppVersionInfo) {
      // If the lazy getter explodes, we're probably loaded in xpcshell,
      // which doesn't have what we need, so log an error.
      try {
        gAppVersionInfo = { 
          channel: UpdateUtils.UpdateChannel, 
          version: appInfo.version, 
          OS: appInfo.OS };} 

      catch (ex) {
        // Do nothing
      }}

    reply(gAppVersionInfo);}, 


  /**
   * Fetch the contents of a specific audio file and return it as a Blob object.
   *
   * @param {Object}   message Message meant for the handler function, containing
   *                           the following parameters in its `data` property:
   *                           [
   *                             {String} name Name of the sound to fetch
   *                           ]
   * @param {Function} reply   Callback function, invoked with the result of this
   *                           message handler. The result will be sent back to
   *                           the senders' channel.
   */
  GetAudioBlob: function GetAudioBlob(message, reply) {
    var name = message.data[0];
    var request = Cc["@mozilla.org/xmlextras/xmlhttprequest;1"].
    createInstance(Ci.nsIXMLHttpRequest);
    var url = "chrome://loop/content/shared/sounds/" + name + ".ogg";

    request.open("GET", url, true);
    request.responseType = "arraybuffer";
    request.onload = function () {
      if (request.status < 200 || request.status >= 300) {
        reply(cloneableError(request.status + " " + request.statusText));
        return;}


      var blob = new Blob([request.response], { type: "audio/ogg" });
      reply(blob);};


    request.send();}, 


  /**
   * Returns the window data for a specific conversation window id.
   *
   * This data will be relevant to the type of window, e.g. rooms or calls.
   * See LoopRooms for more information.
   *
   * @param {Object}   message Message meant for the handler function, containing
   *                           the following parameters in its `data` property:
   *                           [
   *                             {String} conversationWindowId
   *                           ]
   * @param {Function} reply   Callback function, invoked with the result of this
   *                           message handler. The result will be sent back to
   *                           the senders' channel.
   * @returns {Object} The window data or null if error.
   */
  GetConversationWindowData: function GetConversationWindowData(message, reply) {
    reply(MozLoopService.getConversationWindowData(message.data[0]));}, 


  /**
   * Gets the "do not disturb" mode activation flag.
   *
   * @param {Object}   message Message meant for the handler function, containing
   *                           the following parameters in its `data` property:
   *                           [ ]
   * @param {Function} reply   Callback function, invoked with the result of this
   *                           message handler. The result will be sent back to
   *                           the senders' channel.
   */
  GetDoNotDisturb: function GetDoNotDisturb(message, reply) {
    reply(MozLoopService.doNotDisturb);}, 


  /**
   * Retrieve the list of errors that are currently pending on the MozLoopService
   * class.
   *
   * @param {Object}   message Message meant for the handler function, containing
   *                           the following parameters in its `data` property:
   *                           [ ]
   * @param {Function} reply   Callback function, invoked with the result of this
   *                           message handler. The result will be sent back to
   *                           the senders' channel.
   */
  GetErrors: function GetErrors(message, reply) {
    var errors = {};var _iteratorNormalCompletion3 = true;var _didIteratorError3 = false;var _iteratorError3 = undefined;try {
      for (var _iterator3 = MozLoopService.errors[Symbol.iterator](), _step3; !(_iteratorNormalCompletion3 = (_step3 = _iterator3.next()).done); _iteratorNormalCompletion3 = true) {var _ref3 = _step3.value;var _ref4 = _slicedToArray(_ref3, 2);var type = _ref4[0];var error = _ref4[1];
        // if error.error is an nsIException, just delete it since it's hard
        // to clone across the boundary.
        if (error.error instanceof Ci.nsIException) {
          MozLoopService.log.debug("Warning: Some errors were omitted from MozLoopAPI.errors " + 
          "due to issues copying nsIException across boundaries.", 
          error.error);
          delete error.error;}


        errors[type] = cloneableError(error);}} catch (err) {_didIteratorError3 = true;_iteratorError3 = err;} finally {try {if (!_iteratorNormalCompletion3 && _iterator3.return) {_iterator3.return();}} finally {if (_didIteratorError3) {throw _iteratorError3;}}}

    return reply(errors);}, 


  /**
   * Returns true if this profile has an encryption key.
   *
   * @param {Object}   message Message meant for the handler function, containing
   *                           the following parameters in its `data` property:
   *                           [ ]
   * @param {Function} reply   Callback function, invoked with the result of this
   *                           message handler. The result will be sent back to
   *                           the senders' channel.
   * @return {Boolean} True if the profile has an encryption key.
   */
  GetHasEncryptionKey: function GetHasEncryptionKey(message, reply) {
    reply(MozLoopService.hasEncryptionKey);}, 


  /**
   * Returns the current locale of the browser.
   *
   * @param {Object}   message Message meant for the handler function, containing
   *                           the following parameters in its `data` property:
   *                           [ ]
   * @param {Function} reply   Callback function, invoked with the result of this
   *                           message handler. The result will be sent back to
   *                           the senders' channel.
   * @returns {String} The locale string
   */
  GetLocale: function GetLocale(message, reply) {
    reply(MozLoopService.locale);}, 


  /**
   * Returns the version number for the addon.
   *
   * @param {Object}   message Message meant for the handler function, containing
   *                           the following parameters in its `data` property:
   *                           [ ]
   * @param {Function} reply   Callback function, invoked with the result of this
   *                           message handler. The result will be sent back to
   *                           the senders' channel.
   * @returns {String} Addon Version string.
   */
  GetAddonVersion: function GetAddonVersion(message, reply) {
    reply(MozLoopService.addonVersion);}, 


  /**
   * Return any preference under "loop.".
   * Any errors thrown by the Mozilla pref API are logged to the console
   * and cause null to be returned. This includes the case of the preference
   * not being found.
   *
   * @param {Object}   message Message meant for the handler function, containing
   *                           the following parameters in its `data` property:
   *                           [
   *                             {String} prefName The name of the pref without
   *                                               the preceding "loop."
   *                             {Enum}   prefType Type of preference, defined
   *                                               at Ci.nsIPrefBranch. Optional.
   *                           ]
   * @param {Function} reply   Callback function, invoked with the result of this
   *                           message handler. The result will be sent back to
   *                           the senders' channel.
   * @return {*} on success, null on error
   */
  GetLoopPref: function GetLoopPref(message, reply) {var _message$data4 = _slicedToArray(
    message.data, 2);var prefName = _message$data4[0];var prefType = _message$data4[1];
    reply(MozLoopService.getLoopPref(prefName, prefType));}, 


  /**
   * Retrieve the plural rule number of the active locale.
   *
   * @param {Object}   message Message meant for the handler function, containing
   *                           the following parameters in its `data` property:
   *                           [ ]
   * @param {Function} reply   Callback function, invoked with the result of this
   *                           message handler. The result will be sent back to
   *                           the senders' channel.
   */
  GetPluralRule: function GetPluralRule(message, reply) {
    reply(PluralForm.ruleNum);}, 


  /**
   * Gets the metadata related to the currently selected tab in
   * the most recent window.
   *
   * @param {Object}   message Message meant for the handler function, containing
   *                           the following parameters in its `data` property:
   *                           [ ]
   * @param {Function} reply   Callback function, invoked with the result of this
   *                           message handler. The result will be sent back to
   *                           the senders' channel.
   */
  GetSelectedTabMetadata: function GetSelectedTabMetadata(message, reply) {
    var win = Services.wm.getMostRecentWindow("navigator:browser");
    var browser = win && win.gBrowser.selectedBrowser;
    if (!win || !browser) {
      MozLoopService.log.error("Error occurred whilst fetching page metadata");
      reply();
      return;}


    // non-remote pages have no metadata
    if (!browser.getAttribute("remote") === "true") {
      reply(null);}


    win.messageManager.addMessageListener("PageMetadata:PageDataResult", 
    function onPageDataResult(msg) {

      win.messageManager.removeMessageListener("PageMetadata:PageDataResult", 
      onPageDataResult);
      var pageData = msg.json;
      win.LoopUI.getFavicon(function (err, favicon) {
        if (err && err !== "favicon not found for uri") {
          MozLoopService.log.error("Error occurred whilst fetching favicon", err);
          // We don't return here intentionally to make sure the callback is
          // invoked at all times. We just report the error here.
        }
        pageData.favicon = favicon || null;

        reply(pageData);});});


    win.gBrowser.selectedBrowser.messageManager.sendAsyncMessage("PageMetadata:GetPageData");}, 


  /**
   * Gets an object with data that represents the currently
   * authenticated user's identity.
   *
   * @param {Object}   message Message meant for the handler function, containing
   *                           the following parameters in its `data` property:
   *                           [ ]
   * @param {Function} reply   Callback function, invoked with the result of this
   *                           message handler. The result will be sent back to
   *                           the senders' channel.
   * @return null if user not logged in; profile object otherwise
   */
  GetUserProfile: function GetUserProfile(message, reply) {
    if (!MozLoopService.userProfile) {
      reply(null);
      return;}


    reply({ 
      email: MozLoopService.userProfile.email, 
      uid: MozLoopService.userProfile.uid });}, 



  /**
   * Hangup and close all chat windows that are open.
   *
   * @param {Object}   message Message meant for the handler function, containing
   *                           the following parameters in its `data` property:
   *                           [ ]
   * @param {Function} reply   Callback function, invoked with the result of this
   *                           message handler. The result will be sent back to
   *                           the senders' channel.
   */
  HangupAllChatWindows: function HangupAllChatWindows(message, reply) {
    MozLoopService.hangupAllChatWindows();
    reply();}, 


  /**
   * Hangup a specific chay window or room, by leaving a room, resetting the
   * screensharing state and removing any active browser switch listeners.
   *
   * @param {Object}   message Message meant for the handler function, containing
   *                           the following parameters in its `data` property:
   *                           [
   *                             {String} roomToken The token of the room to leave
   *                             {Number} windowId  The window ID of the chat window
   *                           ]
   * @param {Function} reply   Callback function, invoked with the result of this
   *                           message handler. The result will be sent back to
   *                           the senders' channel.
   */
  HangupNow: function HangupNow(message, reply) {var _message$data5 = _slicedToArray(
    message.data, 3);var roomToken = _message$data5[0];var sessionToken = _message$data5[1];var windowId = _message$data5[2];
    if (!windowId) {
      windowId = sessionToken;}


    LoopRooms.logDomains(roomToken);
    LoopRooms.leave(roomToken);
    MozLoopService.setScreenShareState(windowId, false);
    LoopAPI.sendMessageToHandler({ 
      name: "RemoveBrowserSharingListener", 
      data: [windowId] });

    reply();}, 


  /**
   * Check if the current browser has e10s enabled or not
   *
   * @param {Object}   message Message meant for the handler function, containing
   *                           the following parameters in its `data` property:
   *                           []
   * @param {Function} reply   Callback function, invoked with the result of this
   *                           message handler. The result will be sent back to
   *                           the senders' channel.
   */
  IsMultiProcessActive: function IsMultiProcessActive(message, reply) {
    reply(isMultiProcessActive());}, 


  /**
   *  Checks that the current tab can be shared.
   *  Non-shareable tabs are the non-remote ones when e10s is enabled.
   *
   *  @param {Object}   message Message meant for the handler function,
   *                            with no data attached.
   *  @param {Function} reply   Callback function, invoked with the result of
   *                            the check. The result will be sent back to
   *                            the senders' channel.
   */
  IsTabShareable: function IsTabShareable(message, reply) {
    var win = Services.wm.getMostRecentWindow("navigator:browser");
    var browser = win && win.gBrowser.selectedBrowser;
    if (!win || !browser) {
      reply(false);
      return;}


    var e10sActive = isMultiProcessActive();
    var tabRemote = browser.getAttribute("remote") === "true";

    reply(!e10sActive || e10sActive && tabRemote);}, 


  /**
   * Start the FxA login flow using the OAuth client and params from the Loop
   * server.
   *
   * @param {Object}   message Message meant for the handler function, containing
   *                           the following parameters in its `data` property:
   *                           [
   *                             {Boolean} forceReAuth Set to true to force FxA
   *                                                   into a re-auth even if the
   *                                                   user is already logged in.
   *                           ]
   * @param {Function} reply   Callback function, invoked with the result of this
   *                           message handler. The result will be sent back to
   *                           the senders' channel.
   * @return {Promise} Returns a promise that is resolved on successful
   *                   completion, or rejected otherwise.
   */
  LoginToFxA: function LoginToFxA(message, reply) {
    var forceReAuth = message.data[0];
    MozLoopService.logInToFxA(forceReAuth);
    reply();}, 


  /**
   * Logout completely from FxA.
   * @see MozLoopService#logOutFromFxA
   *
   * @param {Object}   message Message meant for the handler function, containing
   *                           the following parameters in its `data` property:
   *                           [ ]
   * @param {Function} reply   Callback function, invoked with the result of this
   *                           message handler. The result will be sent back to
   *                           the senders' channel.
   */
  LogoutFromFxA: function LogoutFromFxA(message, reply) {
    MozLoopService.logOutFromFxA();
    reply();}, 


  /**
   * Notifies the UITour module that an event occurred that it might be
   * interested in.
   *
   * @param {Object}   message Message meant for the handler function, containing
   *                           the following parameters in its `data` property:
   *                           [
   *                             {String} subject  Subject of the notification
   *                             {mixed}  [params] Optional parameters, providing
   *                                               more details to the notification
   *                                               subject
   *                           ]
   * @param {Function} reply   Callback function, invoked with the result of this
   *                           message handler. The result will be sent back to
   *                           the senders' channel.
   */
  NotifyUITour: function NotifyUITour(message, reply) {var _message$data6 = _slicedToArray(
    message.data, 2);var subject = _message$data6[0];var params = _message$data6[1];
    UITour.notify(subject, params);
    reply();}, 


  /**
   * Opens the Getting Started tour in the browser.
   *
   * @param {Object}   message Message meant for the handler function, containing
   *                           the following parameters in its `data` property:
   *                           []
   * @param {Function} reply   Callback function, invoked with the result of this
   *                           message handler. The result will be sent back to
   *                           the senders' channel.
   */
  OpenGettingStartedTour: function OpenGettingStartedTour(message, reply) {
    MozLoopService.openGettingStartedTour();
    reply();}, 


  /**
   * Retrieves the Getting Started tour url.
   *
   * @param {Object}   message Message meant for the handler function, containing
   *                           the following parameters in its `data` property:
   *                           [aSrc, aAdditionalParams]
   * @param {Function} reply   Callback function, invoked with the result of this
   *                           message handler. The result will be sent back to
   *                           the senders' channel.
   */
  GettingStartedURL: function GettingStartedURL(message, reply) {
    var aSrc = message.data[0] || null;
    var aAdditionalParams = message.data[1] || {};
    reply(MozLoopService.getTourURL(aSrc, aAdditionalParams).href);}, 


  /**
   * Open the FxA profile/ settings page.
   *
   * @param {Object}   message Message meant for the handler function, containing
   *                           the following parameters in its `data` property:
   *                           [ ]
   * @param {Function} reply   Callback function, invoked with the result of this
   *                           message handler. The result will be sent back to
   *                           the senders' channel.
   */
  OpenFxASettings: function OpenFxASettings(message, reply) {
    MozLoopService.openFxASettings();
    reply();}, 


  /**
   * Opens a non e10s window
   *
   * @param {Object}   message Message meant for the handler function, containing
   *                           the following parameters in its `data` property:
   *                           [url]
   * @param {Function} reply   Callback function, invoked with the result of this
   *                           message handler. The result will be sent back to
   *                           the senders' channel.
   */
  OpenNonE10sWindow: function OpenNonE10sWindow(message, reply) {
    var win = Services.wm.getMostRecentWindow("navigator:browser");
    var url = message.data[0] ? message.data[0] : "about:home";
    win.openDialog("chrome://browser/content/", "_blank", "chrome,all,dialog=no,non-remote", url);
    reply();}, 


  /**
   * Opens a URL in a new tab in the browser.
   *
   * @param {Object}   message Message meant for the handler function, containing
   *                           the following parameters in its `data` property:
   *                           [
   *                             {String} url The new url to open
   *                           ]
   * @param {Function} reply   Callback function, invoked with the result of this
   *                           message handler. The result will be sent back to
   *                           the senders' channel.
   */
  OpenURL: function OpenURL(message, reply) {
    var url = message.data[0];
    MozLoopService.openURL(url);
    reply();}, 


  /**
   * Removes a listener that was previously added.
   *
   * @param {Object}   message Message meant for the handler function, containing
   *                           the following parameters in its `data` property:
   *                           [
   *                             {Number} windowId The window ID of the chat
   *                           ]
   * @param {Function} reply   Callback function, invoked with the result of this
   *                           message handler. The result will be sent back to
   *                           the senders' channel.
   */
  RemoveBrowserSharingListener: function RemoveBrowserSharingListener(message, reply) {
    if (!gBrowserSharingListeners.size) {
      reply();
      return;}var _message$data7 = _slicedToArray(


    message.data, 1);var windowId = _message$data7[0];
    gBrowserSharingListeners.delete(windowId);
    if (gBrowserSharingListeners.size > 0) {
      // There are still clients listening in, so keep on listening...
      reply();
      return;}var _iteratorNormalCompletion4 = true;var _didIteratorError4 = false;var _iteratorError4 = undefined;try {


      for (var _iterator4 = gBrowserSharingWindows[Symbol.iterator](), _step4; !(_iteratorNormalCompletion4 = (_step4 = _iterator4.next()).done); _iteratorNormalCompletion4 = true) {var win = _step4.value;
        win = win.get();
        if (!win) {
          continue;}

        win.LoopUI.stopBrowserSharing();}} catch (err) {_didIteratorError4 = true;_iteratorError4 = err;} finally {try {if (!_iteratorNormalCompletion4 && _iterator4.return) {_iterator4.return();}} finally {if (_didIteratorError4) {throw _iteratorError4;}}}


    NewTabURL.reset();

    gBrowserSharingWindows.clear();
    reply();}, 


  "Rooms:*": function Rooms(action, message, reply) {
    LoopAPIInternal.handleObjectAPIMessage(LoopRooms, kRoomsPushPrefix, 
    action, message, reply);}, 


  /**
   * Sets the "do not disturb" mode activation flag.
   *
   * @param {Object}   message Message meant for the handler function, containing
   *                           the following parameters in its `data` property:
   *                           [ ]
   * @param {Function} reply   Callback function, invoked with the result of this
   *                           message handler. The result will be sent back to
   *                           the senders' channel.
   */
  SetDoNotDisturb: function SetDoNotDisturb(message, reply) {
    MozLoopService.doNotDisturb = message.data[0];
    reply();}, 


  /**
   * Set any preference under "loop."
   * Any errors thrown by the Mozilla pref API are logged to the console
   * and cause false to be returned.
   *
   * @param {Object}   message Message meant for the handler function, containing
   *                           the following parameters in its `data` property:
   *                           [
   *                             {String} prefName The name of the pref without
   *                                               the preceding "loop."
   *                             {*}      value    The value to set.
   *                             {Enum}   prefType Type of preference, defined at
   *                                               Ci.nsIPrefBranch. Optional.
   *                           ]
   * @param {Function} reply   Callback function, invoked with the result of this
   *                           message handler. The result will be sent back to
   *                           the senders' channel.
   */
  SetLoopPref: function SetLoopPref(message, reply) {var _message$data8 = _slicedToArray(
    message.data, 3);var prefName = _message$data8[0];var value = _message$data8[1];var prefType = _message$data8[2];
    MozLoopService.setLoopPref(prefName, value, prefType);
    reply();}, 


  /**
   * Called when a closing room has just been created, so user can change
   * the name of the room to be stored.
   *
   * @param {Object}   message Message meant for the handler function, shouldn't
                               contain any data.
   * @param {Function} reply   Callback function, invoked with the result of this
   *                           message handler. The result will be sent back to
   *                           the senders' channel.
   */
  SetNameNewRoom: function SetNameNewRoom(message, reply) {
    var win = Services.wm.getMostRecentWindow("navigator:browser");
    win && win.LoopUI.renameRoom();

    reply();}, 


  /**
   * Used to record the screen sharing state for a window so that it can
   * be reflected on the toolbar button.
   *
   * @param {Object}   message Message meant for the handler function, containing
   *                           the following parameters in its `data` property:
   *                           [
   *                             {String} windowId The id of the conversation window
   *                                               the state is being changed for.
   *                             {Boolean} active  Whether or not screen sharing
   *                                               is now active.
   *                           ]
   * @param {Function} reply   Callback function, invoked with the result of this
   *                           message handler. The result will be sent back to
   *                           the senders' channel.
   */
  SetScreenShareState: function SetScreenShareState(message, reply) {var _message$data9 = _slicedToArray(
    message.data, 2);var windowId = _message$data9[0];var active = _message$data9[1];
    MozLoopService.setScreenShareState(windowId, active);
    reply();}, 


  /**
   * Adds a value to a telemetry histogram.
   *
   * @param {Object}   message Message meant for the handler function, containing
   *                           the following parameters in its `data` property:
   *                           [
   *                             {String} histogramId Name of the telemetry histogram
   *                                                  to update.
   *                             {String} value       Label of bucket to increment
   *                                                   in the histogram.
   *                           ]
   * @param {Function} reply   Callback function, invoked with the result of this
   *                           message handler. The result will be sent back to
   *                           the senders' channel.
   */
  TelemetryAddValue: function TelemetryAddValue(message, reply) {var _message$data10 = _slicedToArray(
    message.data, 2);var histogramId = _message$data10[0];var value = _message$data10[1];

    if (histogramId === "LOOP_ACTIVITY_COUNTER") {
      var pref = "mau." + kMauPrefMap.get(value);
      var prefDate = MozLoopService.getLoopPref(pref) * 1000;
      var delta = Date.now() - prefDate;

      // Send telemetry event if period (30 days) passed.
      // 0 is default value for pref.
      // 2592000 seconds in 30 days
      if (pref === 0 || delta >= 2592000 * 1000) {
        try {
          Services.telemetry.getHistogramById(histogramId).add(value);} 
        catch (ex) {
          MozLoopService.log.error("TelemetryAddValue failed for histogram '" + histogramId + "'", ex);}

        MozLoopService.setLoopPref(pref, Math.floor(Date.now() / 1000));}} else 

    {
      try {
        Services.telemetry.getHistogramById(histogramId).add(value);} 
      catch (ex) {
        MozLoopService.log.error("TelemetryAddValue failed for histogram '" + histogramId + "'", ex);}}


    reply();} };



var LoopAPIInternal = { 
  /**
   * Initialize the Loop API, which means:
   * 1) setup RemotePageManager to hook into loop documents as channels and
   *    start listening for messages therein.
   * 2) start listening for other events that may be interesting.
   */
  initialize: function initialize() {
    if (gPageListeners) {
      return;}


    Cu.import("resource://gre/modules/RemotePageManager.jsm");

    gPageListeners = [new RemotePages("about:looppanel"), 
    new RemotePages("about:loopconversation"), 
    // Slideshow added here to expose the loop api to make L10n work.
    // XXX Can remove once slideshow is made remote.
    new RemotePages("chrome://loop/content/panels/slideshow.html")];var _iteratorNormalCompletion5 = true;var _didIteratorError5 = false;var _iteratorError5 = undefined;try {
      for (var _iterator5 = gPageListeners[Symbol.iterator](), _step5; !(_iteratorNormalCompletion5 = (_step5 = _iterator5.next()).done); _iteratorNormalCompletion5 = true) {var page = _step5.value;
        page.addMessageListener(kMessageName, this.handleMessage.bind(this));}


      // Subscribe to global events:
    } catch (err) {_didIteratorError5 = true;_iteratorError5 = err;} finally {try {if (!_iteratorNormalCompletion5 && _iterator5.return) {_iterator5.return();}} finally {if (_didIteratorError5) {throw _iteratorError5;}}}Services.obs.addObserver(this.handleStatusChanged, "loop-status-changed", false);}, 


  /**
   * Handles incoming messages from RemotePageManager that are sent from Loop
   * content pages.
   *
   * @param {Object} message Object containing the following fields:
   *                         - {MessageManager} target Where the message came from
   *                         - {String}         name   Name of the message
   *                         - {Array}          data   Payload of the message
   * @param {Function} [reply]
   */
  handleMessage: function handleMessage(message, reply) {
    var seq = message.data.shift();
    var action = message.data.shift();

    var actionParts = action.split(":");

    // The name that is supposed to match with a handler function is tucked inside
    // the second part of the message name. If all is well.
    var handlerName = actionParts.shift();

    if (!reply) {
      reply = function reply(result) {
        try {
          message.target.sendAsyncMessage(message.name, [seq, result]);} 
        catch (ex) {
          MozLoopService.log.error("Failed to send reply back to content:", ex);}};}




    // First, check if this is a batch call.
    if (handlerName == kBatchMessage) {
      this.handleBatchMessage(seq, message, reply);
      return;}


    // Second, check if the message is meant for one of our Object APIs.
    // If so, a wildcard entry should exist for the message name in the
    // `kMessageHandlers` dictionary.
    var wildcardName = handlerName + ":*";
    if (kMessageHandlers[wildcardName]) {
      // A unit test might've stubbed the handler.
      if (gStubbedMessageHandlers && gStubbedMessageHandlers[wildcardName]) {
        gStubbedMessageHandlers[wildcardName](action, message, reply);} else 
      {
        // Alright, pass the message forward.
        kMessageHandlers[wildcardName](action, message, reply);}

      // Aaaaand we're done.
      return;}


    // A unit test might've stubbed the handler.
    if (gStubbedMessageHandlers && gStubbedMessageHandlers[handlerName]) {
      gStubbedMessageHandlers[handlerName](message, reply);
      return;}


    if (!kMessageHandlers[handlerName]) {
      var msg = "Ouch, no message handler available for '" + handlerName + "'";
      MozLoopService.log.error(msg);
      reply(cloneableError(msg));
      return;}


    kMessageHandlers[handlerName](message, reply);}, 


  /**
   * If `sendMessage` above detects that the incoming message consists of a whole
   * set of messages, this function is tasked with handling them.
   * It iterates over all the messages, sends each to their appropriate handler
   * and collects their results. The results will be sent back in one go as response
   * to the batch message.
   *
   * @param {Number} seq       Sequence ID of this message
   * @param {Object} message   Message containing the following parameters in
   *                           its `data` property:
   *                           [
   *                             {Array} requests Sequence of messages
   *                           ]
   * @param {Function} reply   Callback function, invoked with the result of this
   *                           message handler. The result will be sent back to
   *                           the senders' channel.
   */
  handleBatchMessage: function handleBatchMessage(seq, message, reply) {var _this = this;
    var requests = message.data[0];
    if (!requests.length) {
      MozLoopService.log.error("Ough, a batch call with no requests is not much " + 
      "of a batch, now is it?");
      return;}


    // Since `handleBatchMessage` can be called recursively, but the replies are
    // collected and sent only once, we'll make sure only one exists for the
    // entire tail.
    // We count the amount of recursive calls, because we don't want any consumer
    // to cause an infinite loop, now do we?
    if (!("loopCount" in reply)) {
      reply.loopCount = 0;} else 
    if (++reply.loopCount > kMaxLoopCount) {
      reply(cloneableError("Too many nested calls"));
      return;}


    var resultSet = {};
    Promise.all(requests.map(function (requestSet) {
      var requestSeq = requestSet[0];
      return new Promise(function (resolve) {return _this.handleMessage({ data: requestSet }, function (result) {
          resultSet[requestSeq] = result;
          resolve();});});})).

    then(function () {return reply(resultSet);});}, 


  /**
   * Separate handler that is specialized in dealing with messages meant for sub-APIs,
   * like LoopRooms.
   *
   * @param {Object}   api               Pointer to the sub-API.
   * @param {String}   pushMessagePrefix
   * @param {String}   action            Action name that translates to a function
   *                                     name present on the sub-API.
   * @param {Object}   message           Message containing parameters required to
   *                                     perform the action on the sub-API  in its
   *                                     `data` property.
   * @param {Function} reply             Callback function, invoked with the result
   *                                     of this message handler. The result will
   *                                     be sent back to the senders' channel.
   */
  handleObjectAPIMessage: function handleObjectAPIMessage(api, pushMessagePrefix, action, message, reply) {
    var funcName = getObjectAPIFunctionName(action);

    if (funcName == kPushSubscription) {var _ret = function () {
        // Incoming event listener request!
        var events = message.data[0];
        if (!events || !events.length) {
          var msg = "Oops, don't forget to pass in event names when you try to " + 
          "subscribe to them!";
          MozLoopService.log.error(msg);
          reply(cloneableError(msg));
          return { v: void 0 };}


        var handlerFunc = function handlerFunc(e) {for (var _len = arguments.length, data = Array(_len > 1 ? _len - 1 : 0), _key = 1; _key < _len; _key++) {data[_key - 1] = arguments[_key];}
          var prettyEventName = e.charAt(0).toUpperCase() + e.substr(1);
          try {
            message.target.sendAsyncMessage(kPushMessageName, [pushMessagePrefix + 
            prettyEventName, data]);} 
          catch (ex) {
            MozLoopService.log.debug("Unable to send event through to target: " + 
            ex.message);
            // Unregister event handlers when the message port is unreachable.
            var _iteratorNormalCompletion6 = true;var _didIteratorError6 = false;var _iteratorError6 = undefined;try {for (var _iterator6 = events[Symbol.iterator](), _step6; !(_iteratorNormalCompletion6 = (_step6 = _iterator6.next()).done); _iteratorNormalCompletion6 = true) {var eventName = _step6.value;
                api.off(eventName, handlerFunc);}} catch (err) {_didIteratorError6 = true;_iteratorError6 = err;} finally {try {if (!_iteratorNormalCompletion6 && _iterator6.return) {_iterator6.return();}} finally {if (_didIteratorError6) {throw _iteratorError6;}}}}};var _iteratorNormalCompletion7 = true;var _didIteratorError7 = false;var _iteratorError7 = undefined;try {




          for (var _iterator7 = events[Symbol.iterator](), _step7; !(_iteratorNormalCompletion7 = (_step7 = _iterator7.next()).done); _iteratorNormalCompletion7 = true) {var eventName = _step7.value;
            api.on(eventName, handlerFunc);}} catch (err) {_didIteratorError7 = true;_iteratorError7 = err;} finally {try {if (!_iteratorNormalCompletion7 && _iterator7.return) {_iterator7.return();}} finally {if (_didIteratorError7) {throw _iteratorError7;}}}

        reply();
        return { v: void 0 };}();if ((typeof _ret === "undefined" ? "undefined" : _typeof(_ret)) === "object") return _ret.v;}


    if (typeof api[funcName] != "function") {
      reply(cloneableError("Sorry, function '" + funcName + "' does not exist!"));
      return;}

    api[funcName].apply(api, _toConsumableArray(message.data).concat([function (err, result) {
      reply(err ? cloneableError(err) : result);}]));}, 



  /**
   * Observer function for the 'loop-status-changed' event.
   */
  handleStatusChanged: function handleStatusChanged() {
    LoopAPIInternal.broadcastPushMessage("LoopStatusChanged");}, 


  /**
   * Send an event to the content window to indicate that the state on the chrome
   * side was updated.
   *
   * @param {name} name Name of the event
   */
  broadcastPushMessage: function broadcastPushMessage(name, data) {
    if (!gPageListeners) {
      return;}var _iteratorNormalCompletion8 = true;var _didIteratorError8 = false;var _iteratorError8 = undefined;try {

      for (var _iterator8 = gPageListeners[Symbol.iterator](), _step8; !(_iteratorNormalCompletion8 = (_step8 = _iterator8.next()).done); _iteratorNormalCompletion8 = true) {var page = _step8.value;
        try {
          page.sendAsyncMessage(kPushMessageName, [name, data]);} 
        catch (ex) {
          // Only make noise when the Remote Page Manager needs more time to
          // initialize.
          if (ex.result != Components.results.NS_ERROR_NOT_INITIALIZED) {
            throw ex;}}}} catch (err) {_didIteratorError8 = true;_iteratorError8 = err;} finally {try {if (!_iteratorNormalCompletion8 && _iterator8.return) {_iterator8.return();}} finally {if (_didIteratorError8) {throw _iteratorError8;}}}}, 





  /**
   * De the reverse of `initialize` above; unhook page and event listeners.
   */
  destroy: function destroy() {
    if (!gPageListeners) {
      return;}var _iteratorNormalCompletion9 = true;var _didIteratorError9 = false;var _iteratorError9 = undefined;try {

      for (var _iterator9 = gPageListeners[Symbol.iterator](), _step9; !(_iteratorNormalCompletion9 = (_step9 = _iterator9.next()).done); _iteratorNormalCompletion9 = true) {var listener = _step9.value;
        listener.destroy();}} catch (err) {_didIteratorError9 = true;_iteratorError9 = err;} finally {try {if (!_iteratorNormalCompletion9 && _iterator9.return) {_iterator9.return();}} finally {if (_didIteratorError9) {throw _iteratorError9;}}}

    gPageListeners = null;

    // Unsubscribe from global events.
    Services.obs.removeObserver(this.handleStatusChanged, "loop-status-changed");} };



this.LoopAPI = Object.freeze({ 
  /* @see LoopAPIInternal#initialize */
  initialize: function initialize() {
    LoopAPIInternal.initialize();}, 

  /* @see LoopAPIInternal#broadcastPushMessage */
  broadcastPushMessage: function broadcastPushMessage(name, data) {
    LoopAPIInternal.broadcastPushMessage(name, data);}, 

  /* @see LoopAPIInternal#destroy */
  destroy: function destroy() {
    LoopAPIInternal.destroy();}, 

  /**
   * Gateway for chrome scripts to send a message to a message handler, when
   * using the RemotePageManager module is not an option.
   *
   * @param {Object}   message Message meant for the handler function, containing
   *                           the following properties:
   *                           - {String} name     Name of handler to send this
   *                                               message to. See `kMessageHandlers`
   *                                               for the available names.
   *                           - {String} [action] Optional action name of the
   *                                               function to call on a sub-API.
   *                           - {Array}  data     List of arguments that the
   *                                               handler can use.
   * @param {Function} [reply] Callback function, invoked with the result of this
   *                           message handler. Optional.
   */
  sendMessageToHandler: function sendMessageToHandler(message, reply) {
    reply = reply || function () {};
    var handlerName = message.name;
    var handler = kMessageHandlers[handlerName];
    if (gStubbedMessageHandlers && gStubbedMessageHandlers[handlerName]) {
      handler = gStubbedMessageHandlers[handlerName];}

    if (!handler) {
      var msg = "Ouch, no message handler available for '" + handlerName + "'";
      MozLoopService.log.error(msg);
      reply(cloneableError(msg));
      return;}


    if (!message.data) {
      message.data = [];}


    if (handlerName.endsWith("*")) {
      handler(message.action, message, reply);} else 
    {
      handler(message, reply);}}, 


  // The following functions are only used in unit tests.
  inspect: function inspect() {
    return [Object.create(LoopAPIInternal), Object.create(kMessageHandlers), 
    gPageListeners ? [].concat(_toConsumableArray(gPageListeners)) : null];}, 

  stub: function stub(pageListeners) {
    if (!gOriginalPageListeners) {
      gOriginalPageListeners = gPageListeners;}

    gPageListeners = pageListeners;}, 

  stubMessageHandlers: function stubMessageHandlers(handlers) {
    gStubbedMessageHandlers = handlers;}, 

  restore: function restore() {
    if (gOriginalPageListeners) {
      gPageListeners = gOriginalPageListeners;}

    gStubbedMessageHandlers = null;} });

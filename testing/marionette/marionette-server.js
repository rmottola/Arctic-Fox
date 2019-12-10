/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const FRAME_SCRIPT = "chrome://marionette/content/marionette-listener.js";
const BROWSER_STARTUP_FINISHED = "browser-delayed-startup-finished";
const XUL_NS = "http://www.mozilla.org/keymaster/gatekeeper/there.is.only.xul";

// import logger
Cu.import("resource://gre/modules/Log.jsm");
let logger = Log.repository.getLogger("Marionette");
logger.info('marionette-server.js loaded');

let loader = Cc["@mozilla.org/moz/jssubscript-loader;1"]
               .getService(Ci.mozIJSSubScriptLoader);
loader.loadSubScript("chrome://marionette/content/marionette-simpletest.js");
loader.loadSubScript("chrome://marionette/content/marionette-common.js");
Cu.import("resource://gre/modules/Services.jsm");
loader.loadSubScript("chrome://marionette/content/marionette-frame-manager.js");
Cu.import("chrome://marionette/content/marionette-elements.js");
let utils = {};
loader.loadSubScript("chrome://marionette/content/EventUtils.js", utils);
loader.loadSubScript("chrome://marionette/content/ChromeUtils.js", utils);
loader.loadSubScript("chrome://marionette/content/atoms.js", utils);
loader.loadSubScript("chrome://marionette/content/marionette-sendkeys.js", utils);

let specialpowers = {};

Cu.import("resource://gre/modules/FileUtils.jsm");
Cu.import("resource://gre/modules/NetUtil.jsm");

function isMulet() {
  let isMulet = false;
  try {
   isMulet = Services.prefs.getBoolPref("b2g.is_mulet");
  } catch (ex) { }
  return isMulet;
}

Services.prefs.setBoolPref("marionette.contentListener", false);
let appName = isMulet() ? "B2G" : Services.appinfo.name;

let { devtools } = Cu.import("resource://gre/modules/devtools/Loader.jsm", {});
let DevToolsUtils = devtools.require("devtools/toolkit/DevToolsUtils.js");
this.DevToolsUtils = DevToolsUtils;
loader.loadSubScript("resource://gre/modules/devtools/transport/transport.js");

let bypassOffline = false;
let qemu = "0";
let device = null;
const SECURITY_PREF = 'security.turn_off_all_security_so_that_viruses_can_take_over_this_computer';

XPCOMUtils.defineLazyServiceGetter(this, "cookieManager",
                                   "@mozilla.org/cookiemanager;1",
                                   "nsICookieManager");

try {
  XPCOMUtils.defineLazyGetter(this, "libcutils", function () {
    Cu.import("resource://gre/modules/systemlibs.js");
    return libcutils;
  });
  if (libcutils) {
    qemu = libcutils.property_get("ro.kernel.qemu");
    logger.info("B2G emulator: " + (qemu == "1" ? "yes" : "no"));
    device = libcutils.property_get("ro.product.device");
    logger.info("Device detected is " + device);
    bypassOffline = (qemu == "1" || device == "panda");
  }
}
catch(e) {}

if (bypassOffline) {
  logger.info("Bypassing offline status.");
  Services.prefs.setBoolPref("network.gonk.manage-offline-status", false);
  Services.io.manageOfflineStatus = false;
  Services.io.offline = false;
}

// This is used to prevent newSession from returning before the telephony
// API's are ready; see bug 792647.  This assumes that marionette-server.js
// will be loaded before the 'system-message-listener-ready' message
// is fired.  If this stops being true, this approach will have to change.
let systemMessageListenerReady = false;
Services.obs.addObserver(function() {
  systemMessageListenerReady = true;
}, "system-message-listener-ready", false);

// This is used on desktop to prevent newSession from returning before a page
// load initiated by the Firefox command line has completed.
let delayedBrowserStarted = false;
Services.obs.addObserver(function () {
  delayedBrowserStarted = true;
}, BROWSER_STARTUP_FINISHED, false);

/*
 * Custom exceptions
 */
function FrameSendNotInitializedError(frame) {
  this.code = 54;
  this.frame = frame;
  this.message = "Error sending message to frame (NS_ERROR_NOT_INITIALIZED)";
  this.toString = function() {
    return this.message + " " + this.frame + "; frame has closed.";
  }
}

function FrameSendFailureError(frame) {
  this.code = 55;
  this.frame = frame;
  this.message = "Error sending message to frame (NS_ERROR_FAILURE)";
  this.toString = function() {
    return this.message + " " + this.frame + "; frame not responding.";
  }
}

/**
 * The server connection is responsible for all marionette API calls. It gets created
 * for each connection and manages all chrome and browser based calls. It
 * mediates content calls by issuing appropriate messages to the content process.
 */
function MarionetteServerConnection(aPrefix, aTransport, aServer)
{
  this.uuidGen = Cc["@mozilla.org/uuid-generator;1"]
                   .getService(Ci.nsIUUIDGenerator);

  this.prefix = aPrefix;
  this.server = aServer;
  this.conn = aTransport;
  this.conn.hooks = this;

  // marionette uses a protocol based on the debugger server, which requires
  // passing back "actor ids" with responses. unlike the debugger server,
  // we don't have multiple actors, so just use a dummy value of "0" here
  this.actorID = "0";
  this.sessionId = null;

  this.globalMessageManager = Cc["@mozilla.org/globalmessagemanager;1"]
                             .getService(Ci.nsIMessageBroadcaster);
  this.messageManager = this.globalMessageManager;
  this.browsers = {}; //holds list of BrowserObjs
  this.curBrowser = null; // points to current browser
  this.context = "content";
  this.scriptTimeout = null;
  this.searchTimeout = null;
  this.pageTimeout = null;
  this.timer = null;
  this.inactivityTimer = null;
  this.heartbeatCallback = function () {}; // called by simpletest methods
  this.marionetteLog = new MarionetteLogObj();
  this.command_id = null;
  this.mainFrame = null; //topmost chrome frame
  this.curFrame = null; // chrome iframe that currently has focus
  this.mainContentFrameId = null;
  this.importedScripts = FileUtils.getFile('TmpD', ['marionetteChromeScripts']);
  this.importedScriptHashes = {"chrome" : [], "content": []};
  this.currentFrameElement = null;
  this.testName = null;
  this.mozBrowserClose = null;
  this.enabled_security_pref = false;
  this.sandbox = null;
  this.oopFrameId = null; // frame ID of current remote frame, used for mozbrowserclose events
  this.sessionCapabilities = {
    // Mandated capabilities
    "browserName": appName,
    "browserVersion": Services.appinfo.version,
    "platformName": Services.appinfo.OS.toUpperCase(),
    "platformVersion": Services.appinfo.platformVersion,

    // Supported features
    "handlesAlerts": false,
    "nativeEvents": false,
    "raisesAccessibilityExceptions": false,
    "rotatable": appName == "B2G",
    "secureSsl": false,
    "takesElementScreenshot": true,
    "takesScreenshot": true,

    // Selenium 2 compat
    "platform": Services.appinfo.OS.toUpperCase(),

    // Proprietary extensions
    "XULappId" : Services.appinfo.ID,
    "appBuildId" : Services.appinfo.appBuildID,
    "device": qemu == "1" ? "qemu" : (!device ? "desktop" : device),
    "version": Services.appinfo.version
  };

  this.observing = null;
  this._browserIds = new WeakMap();
  this.quitFlags = null;
}

MarionetteServerConnection.prototype = {

  QueryInterface: XPCOMUtils.generateQI([Ci.nsIMessageListener,
                                         Ci.nsIObserver,
                                         Ci.nsISupportsWeakReference]),

  /**
   * Debugger transport callbacks:
   */
  onPacket: function MSC_onPacket(aPacket) {
    // Dispatch the request
    if (this.requestTypes && this.requestTypes[aPacket.name]) {
      try {
        this.logRequest(aPacket.name, aPacket);
        this.requestTypes[aPacket.name].bind(this)(aPacket);
      } catch(e) {
        this.conn.send({ error: ("error occurred while processing '" +
                                 aPacket.name),
                        message: e.message });
      }
    } else {
      this.conn.send({ error: "unrecognizedPacketType",
                       message: ('Marionette does not ' +
                                 'recognize the packet type "' +
                                 aPacket.name + '"') });
    }
  },

  onClosed: function MSC_onClosed(aStatus) {
    this.server._connectionClosed(this);
    this.sessionTearDown();

    if (this.quitFlags !== null) {
      let flags = this.quitFlags;
      this.quitFlags = null;
      Services.startup.quit(flags);
    }
  },

  /**
   * Helper methods:
   */

  /**
   * Switches to the global ChromeMessageBroadcaster, potentially replacing a frame-specific
   * ChromeMessageSender.  Has no effect if the global ChromeMessageBroadcaster is already
   * in use.  If this replaces a frame-specific ChromeMessageSender, it removes the message
   * listeners from that sender, and then puts the corresponding frame script "to sleep",
   * which removes most of the message listeners from it as well.
   */
  switchToGlobalMessageManager: function MDA_switchToGlobalMM() {
    if (this.curBrowser && this.curBrowser.frameManager.currentRemoteFrame !== null) {
      this.curBrowser.frameManager.removeMessageManagerListeners(this.messageManager);
      this.sendAsync("sleepSession", null, null, true);
      this.curBrowser.frameManager.currentRemoteFrame = null;
    }
    this.messageManager = this.globalMessageManager;
  },

  /**
   * Helper method to send async messages to the content listener
   *
   * @param string name
   *        Suffix of the targetted message listener (Marionette:<suffix>)
   * @param object values
   *        Object to send to the listener
   */
  sendAsync: function MDA_sendAsync(name, values, commandId, ignoreFailure) {
    let success = true;
    if (commandId) {
      values.command_id = commandId;
    }
    if (this.curBrowser.frameManager.currentRemoteFrame !== null) {
      try {
        this.messageManager.sendAsyncMessage(
          "Marionette:" + name + this.curBrowser.frameManager.currentRemoteFrame.targetFrameId, values);
      }
      catch(e) {
        if (!ignoreFailure) {
          success = false;
          let error = e;
          switch(e.result) {
            case Components.results.NS_ERROR_FAILURE:
              error = new FrameSendFailureError(this.curBrowser.frameManager.currentRemoteFrame);
              break;
            case Components.results.NS_ERROR_NOT_INITIALIZED:
              error = new FrameSendNotInitializedError(this.curBrowser.frameManager.currentRemoteFrame);
              break;
            default:
              break;
          }
          let code = error.hasOwnProperty('code') ? e.code : 500;
          this.sendError(error.toString(), code, error.stack, commandId);
        }
      }
    }
    else {
      this.curBrowser.executeWhenReady(() => {
        this.messageManager.broadcastAsyncMessage(
          "Marionette:" + name + this.curBrowser.curFrameId, values);
      });
    }
    return success;
  },

  logRequest: function MDA_logRequest(type, data) {
    logger.debug("Got request: " + type + ", data: " + JSON.stringify(data) + ", id: " + this.command_id);
  },

  /**
   * Generic method to pass a response to the client
   *
   * @param object msg
   *        Response to send back to client
   * @param string command_id
   *        Unique identifier assigned to the client's request.
   *        Used to distinguish the asynchronous responses.
   */
  sendToClient: function MDA_sendToClient(msg, command_id) {
    logger.info("sendToClient: " + JSON.stringify(msg) + ", " + command_id +
                ", " + this.command_id);
    if (!command_id) {
      logger.warn("got a response with no command_id");
      return;
    }
    else if (command_id != -1) {
      // A command_id of -1 is used for emulator callbacks, and those
      // don't use this.command_id.
      if (!this.command_id) {
        // A null value for this.command_id means we've already processed
        // a message for the previous value, and so the current message is a
        // duplicate.
        logger.warn("ignoring duplicate response for command_id " + command_id);
        return;
      }
      else if (this.command_id != command_id) {
        logger.warn("ignoring out-of-sync response");
        return;
      }
    }

    if (this.curBrowser !== null) {
      this.curBrowser.pendingCommands = [];
    }

    this.conn.send(msg);
    if (command_id != -1) {
      // Don't unset this.command_id if this message is to process an
      // emulator callback, since another response for this command_id is
      // expected, after the containing call to execute_async_script finishes.
      this.command_id = null;
    }
  },

  /**
   * Send a value to client
   *
   * @param object value
   *        Value to send back to client
   * @param string command_id
   *        Unique identifier assigned to the client's request.
   *        Used to distinguish the asynchronous responses.
   */
  sendResponse: function MDA_sendResponse(value, command_id) {
    if (typeof(value) == 'undefined')
        value = null;
    this.sendToClient({from:this.actorID,
                       sessionId: this.sessionId,
                       value: value}, command_id);
  },

  sayHello: function MDA_sayHello() {
    this.conn.send({ from: "root",
                     applicationType: "goanna",
                     traits: [] });
  },

  getMarionetteID: function MDA_getMarionette() {
    this.conn.send({ "from": "root", "id": this.actorID });
  },

  /**
   * Send ack to client
   *
   * @param string command_id
   *        Unique identifier assigned to the client's request.
   *        Used to distinguish the asynchronous responses.
   */
  sendOk: function MDA_sendOk(command_id) {
    this.sendToClient({from:this.actorID, ok: true}, command_id);
  },

  /**
   * Send error message to client
   *
   * @param string message
   *        Error message
   * @param number status
   *        Status number
   * @param string trace
   *        Stack trace
   * @param string command_id
   *        Unique identifier assigned to the client's request.
   *        Used to distinguish the asynchronous responses.
   */
  sendError: function MDA_sendError(message, status, trace, command_id) {
    let error_msg = {message: message, status: status, stacktrace: trace};
    this.sendToClient({from:this.actorID, error: error_msg}, command_id);
  },

  /**
   * Gets the current active window
   *
   * @return nsIDOMWindow
   */
  getCurrentWindow: function MDA_getCurrentWindow() {
    let type = null;
    if (this.curFrame == null) {
      if (this.curBrowser == null) {
        if (this.context == "content") {
          type = 'navigator:browser';
        }
        return Services.wm.getMostRecentWindow(type);
      }
      else {
        return this.curBrowser.window;
      }
    }
    else {
      return this.curFrame;
    }
  },

  /**
   * Gets the the window enumerator
   *
   * @return nsISimpleEnumerator
   */
  getWinEnumerator: function MDA_getWinEnumerator() {
    let type = null;
    if (appName != "B2G" && this.context == "content") {
      type = 'navigator:browser';
    }
    return Services.wm.getEnumerator(type);
  },

  /**
  */
  addFrameCloseListener: function MDA_addFrameCloseListener(action) {
    let curWindow = this.getCurrentWindow();
    let self = this;
    this.mozBrowserClose = function(e) {
      if (e.target.id == self.oopFrameId) {
        curWindow.removeEventListener('mozbrowserclose', self.mozBrowserClose, true);
        self.switchToGlobalMessageManager();
        self.sendError("The frame closed during the " + action +  ", recovering to allow further communications", 55, null, self.command_id);
      }
    };
    curWindow.addEventListener('mozbrowserclose', this.mozBrowserClose, true);
  },

  /**
   * Create a new BrowserObj for window and add to known browsers
   *
   * @param nsIDOMWindow win
   *        Window for which we will create a BrowserObj
   *
   * @return string
   *        Returns the unique server-assigned ID of the window
   */
  addBrowser: function MDA_addBrowser(win) {
    let browser = new BrowserObj(win, this);
    let winId = win.QueryInterface(Ci.nsIInterfaceRequestor).
                    getInterface(Ci.nsIDOMWindowUtils).outerWindowID;
    winId = winId + ((appName == "B2G") ? '-b2g' : '');
    this.browsers[winId] = browser;
    this.curBrowser = this.browsers[winId];
    if (this.curBrowser.elementManager.seenItems[winId] == undefined) {
      //add this to seenItems so we can guarantee the user will get winId as this window's id
      this.curBrowser.elementManager.seenItems[winId] = Cu.getWeakReference(win);
    }
  },

  /**
   * Start a new session in a new browser.
   *
   * If newSession is true, we will switch focus to the start frame
   * when it registers.
   *
   * @param nsIDOMWindow win
   *        Window whose browser we need to access
   * @param boolean newSession
   *        True if this is the first time we're talking to this browser
   */
  startBrowser: function MDA_startBrowser(win, newSession) {
    this.mainFrame = win;
    this.curFrame = null;
    this.addBrowser(win);
    this.curBrowser.newSession = newSession;
    this.curBrowser.startSession(newSession, win, this.whenBrowserStarted.bind(this));
  },

  /**
   * Callback invoked after a new session has been started in a browser.
   * Loads the Marionette frame script into the browser if needed.
   *
   * @param nsIDOMWindow win
   *        Window whose browser we need to access
   * @param boolean newSession
   *        True if this is the first time we're talking to this browser
   */
  whenBrowserStarted: function MDA_whenBrowserStarted(win, newSession) {
    utils.window = win;

    try {
      let mm = win.window.messageManager;
      if (!newSession) {
        // Loading the frame script corresponds to a situation we need to
        // return to the server. If the messageManager is a message broadcaster
        // with no children, we don't have a hope of coming back from this call,
        // so send the ack here. Otherwise, make a note of how many child scripts
        // will be loaded so we known when it's safe to return.
        if (mm.childCount === 0) {
          this.sendOk(this.command_id);
        } else {
          this.curBrowser.frameRegsPending = mm.childCount;
        }
      }

      if (!Services.prefs.getBoolPref("marionette.contentListener") || !newSession) {
        mm.loadFrameScript(FRAME_SCRIPT, true, true);
        Services.prefs.setBoolPref("marionette.contentListener", true);
      }
    }
    catch (e) {
      //there may not always be a content process
      logger.info("could not load listener into content for page: " + win.location.href);
    }
  },

  /**
   * Recursively get all labeled text
   *
   * @param nsIDOMElement el
   *        The parent element
   * @param array lines
   *        Array that holds the text lines
   */
  getVisibleText: function MDA_getVisibleText(el, lines) {
    let nodeName = el.nodeName;
    try {
      if (utils.isElementDisplayed(el)) {
        if (el.value) {
          lines.push(el.value);
        }
        for (var child in el.childNodes) {
          this.getVisibleText(el.childNodes[child], lines);
        };
      }
    }
    catch (e) {
      if (nodeName == "#text") {
        lines.push(el.textContent);
      }
    }
  },

  getCommandId: function MDA_getCommandId() {
    return this.uuidGen.generateUUID().toString();
  },

  /**
    * Given a file name, this will delete the file from the temp directory if it exists
    */
  deleteFile: function(filename) {
    let file = FileUtils.getFile('TmpD', [filename.toString()]);
    if (file.exists()) {
      file.remove(true);
    }
  },

  /**
   * Marionette API:
   *
   * All methods implementing a command from the client should create a
   * command_id, and then use this command_id in all messages exchanged with
   * the frame scripts and with responses sent to the client.  This prevents
   * commands and responses from getting out-of-sync, which can happen in
   * the case of execute_async calls that timeout and then later send a
   * response, and other situations.  See bug 779011. See setScriptTimeout()
   * for a basic example.
   */

  /**
   * Create a new session. This creates a new BrowserObj.
   *
   * This will send a hash map of supported capabilities to the client
   * as part of the Marionette:register IPC command in the
   * receiveMessage callback when a new browser is created.
   */
  newSession: function MDA_newSession(aRequest) {
    logger.info("The newSession request is " + JSON.stringify(aRequest))
    this.command_id = this.getCommandId();
    this.newSessionCommandId = this.command_id;

    // SpecialPowers requires insecure automation-only features that we put behind a pref
    let security_pref_value = false;
    try {
      security_pref_value = Services.prefs.getBoolPref(SECURITY_PREF);
    } catch(e) {}
    if (!security_pref_value) {
      this.enabled_security_pref = true;
      Services.prefs.setBoolPref(SECURITY_PREF, true);
    }

    if (!specialpowers.hasOwnProperty('specialPowersObserver')) {
      loader.loadSubScript("chrome://specialpowers/content/SpecialPowersObserver.js",
                           specialpowers);
      specialpowers.specialPowersObserver = new specialpowers.SpecialPowersObserver();
      specialpowers.specialPowersObserver.init();
      specialpowers.specialPowersObserver._loadFrameScript();
    }

    this.scriptTimeout = 10000;
    if (aRequest && aRequest.parameters) {
      this.sessionId = aRequest.parameters.session_id ? aRequest.parameters.session_id : null;
      logger.info("Session Id is set to: " + this.sessionId);
      try {
        this.setSessionCapabilities(aRequest.parameters.capabilities);
      } catch (e) {
        // 71 error is "session not created"
        this.sendError(e.message + " " + JSON.stringify(e.errors), 71, null,
                       this.command_id);
        return;
      }
    }

    if (appName == "Firefox") {
      this._dialogWindowRef = null;
      let modalHandler = this.handleDialogLoad.bind(this);
      this.observing = {
        "tabmodal-dialog-loaded": modalHandler,
        "common-dialog-loaded": modalHandler
      }
      for (let topic in this.observing) {
        Services.obs.addObserver(this.observing[topic], topic, false);
      }
    }

    function waitForWindow() {
      let win = this.getCurrentWindow();
      if (!win) {
        // If the window isn't even created, just poll wait for it
        let checkTimer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
        checkTimer.initWithCallback(waitForWindow.bind(this), 100,
                                    Ci.nsITimer.TYPE_ONE_SHOT);
      }
      else if (win.document.readyState != "complete") {
        // Otherwise, wait for it to be fully loaded before proceeding
        let listener = (evt) => {
          // ensure that we proceed, on the top level document load event
          // (not an iframe one...)
          if (evt.target != win.document) {
            return;
          }
          win.removeEventListener("load", listener);
          waitForWindow.call(this);
        };
        win.addEventListener("load", listener, true);
      }
      else {
        let clickToStart;
        try {
          clickToStart = Services.prefs.getBoolPref('marionette.debugging.clicktostart');
        } catch (e) { }
        if (clickToStart && (appName != "B2G")) {
          let pService = Cc["@mozilla.org/embedcomp/prompt-service;1"]
                           .getService(Ci.nsIPromptService);
          pService.alert(win, "", "Click to start execution of marionette tests");
        }
        this.startBrowser(win, true);
      }
    }

    function runSessionStart() {
      if (!Services.prefs.getBoolPref("marionette.contentListener")) {
        waitForWindow.call(this);
      }
      else if ((appName != "Firefox") && (this.curBrowser === null)) {
        // If there is a content listener, then we just wake it up
        this.addBrowser(this.getCurrentWindow());
        this.curBrowser.startSession(false, this.getCurrentWindow(),
                                     this.whenBrowserStarted);
        this.messageManager.broadcastAsyncMessage("Marionette:restart", {});
      }
      else {
        this.sendError("Session already running", 500, null,
                       this.command_id);
      }
      this.switchToGlobalMessageManager();
    }

    if (!delayedBrowserStarted && (appName != "B2G")) {
      let self = this;
      Services.obs.addObserver(function onStart () {
        Services.obs.removeObserver(onStart, BROWSER_STARTUP_FINISHED);
        runSessionStart.call(self);
      }, BROWSER_STARTUP_FINISHED, false);
    } else {
      runSessionStart.call(this);
    }
  },

  /**
   * Send the current session's capabilities to the client.
   *
   * Capabilities informs the client of which WebDriver features are
   * supported by Firefox and Marionette.  They are immutable for the
   * length of the session.
   *
   * The return value is an immutable map of string keys
   * ("capabilities") to values, which may be of types boolean,
   * numerical or string.
   */
  getSessionCapabilities: function MDA_getSessionCapabilities() {
    this.command_id = this.getCommandId();

    if (!this.sessionId) {
      this.sessionId = this.uuidGen.generateUUID().toString();
    }

    // eideticker (bug 965297) and mochitest (bug 965304)
    // compatibility.  They only check for the presence of this
    // property and should so not be in caps if not on a B2G device.
    if (appName == "B2G")
      this.sessionCapabilities.b2g = true;

    this.sendResponse(this.sessionCapabilities, this.command_id);
  },

  /**
   * Update the sessionCapabilities object with the keys that have been
   * passed in when a new session is created.
   *
   * This part of the WebDriver spec is currently in flux, see
   * http://lists.w3.org/Archives/Public/public-browser-tools-testing/2014OctDec/0000.html
   *
   * This is not a public API, only available when a new session is
   * created.
   *
   * @param Object newCaps key/value dictionary to overwrite
   *   session's current capabilities
   */
  setSessionCapabilities: function(newCaps) {
    const copy = (from, to={}) => {
      let errors = {};
      for (let key in from) {
        if (key === "desiredCapabilities"){
          // Keeping desired capabilities separate for now so that we can keep
          // backwards compatibility
          to = copy(from[key], to);
        } else if (key === "requiredCapabilities") {
          for (let caps in from[key]) {
            if (from[key][caps] !== this.sessionCapabilities[caps]) {
              errors[caps] = from[key][caps] + " does not equal " + this.sessionCapabilities[caps]
            }
          }
        }
        to[key] = from[key];
      }
      if (Object.keys(errors).length === 0){
        return to;
      }
      else {
        throw { "message": "Not all requiredCapabilities could be met",
                "errors": errors}
      }
    };

    // Clone, overwrite, and set.
    let caps = copy(this.sessionCapabilities);
    caps = copy(newCaps, caps);
    this.sessionCapabilities = caps;
  },

  /**
   * Log message. Accepts user defined log-level.
   *
   * @param object aRequest
   *        'value' member holds log message
   *        'level' member hold log level
   */
  log: function MDA_log(aRequest) {
    this.command_id = this.getCommandId();
    this.marionetteLog.log(aRequest.parameters.value, aRequest.parameters.level);
    this.sendOk(this.command_id);
  },

  /**
   * Return all logged messages.
   */
  getLogs: function MDA_getLogs() {
    this.command_id = this.getCommandId();
    this.sendResponse(this.marionetteLog.getLogs(), this.command_id);
  },

  /**
   * Sets the context of the subsequent commands to be either 'chrome' or 'content'
   *
   * @param object aRequest
   *        'value' member holds the name of the context to be switched to
   */
  setContext: function MDA_setContext(aRequest) {
    this.command_id = this.getCommandId();
    let context = aRequest.parameters.value;
    if (context != "content" && context != "chrome") {
      this.sendError("invalid context", 500, null, this.command_id);
    }
    else {
      this.context = context;
      this.sendOk(this.command_id);
    }
  },

  /**
   * Gets the context of the server, either 'chrome' or 'content'.
   */
  getContext: function MDA_getContext() {
    this.command_id = this.getCommandId();
    this.sendResponse(this.context, this.command_id);
  },

  /**
   * Returns a chrome sandbox that can be used by the execute_foo functions.
   *
   * @param nsIDOMWindow aWindow
   *        Window in which we will execute code
   * @param Marionette marionette
   *        Marionette test instance
   * @param object args
   *        Client given args
   * @return Sandbox
   *        Returns the sandbox
   */
  createExecuteSandbox: function MDA_createExecuteSandbox(aWindow, marionette, specialPowers, command_id) {
    let _chromeSandbox = new Cu.Sandbox(aWindow,
       { sandboxPrototype: aWindow, wantXrays: false, sandboxName: ''});
    _chromeSandbox.global = _chromeSandbox;
    _chromeSandbox.testUtils = utils;

    marionette.exports.forEach(function(fn) {
      try {
        _chromeSandbox[fn] = marionette[fn].bind(marionette);
      }
      catch(e) {
        _chromeSandbox[fn] = marionette[fn];
      }
    });

    _chromeSandbox.isSystemMessageListenerReady =
        function() { return systemMessageListenerReady; }

    if (specialPowers == true) {
      loader.loadSubScript("chrome://specialpowers/content/specialpowersAPI.js",
                           _chromeSandbox);
      loader.loadSubScript("chrome://specialpowers/content/SpecialPowersObserverAPI.js",
                           _chromeSandbox);
      loader.loadSubScript("chrome://specialpowers/content/ChromePowers.js",
                           _chromeSandbox);
    }

    return _chromeSandbox;
  },

  /**
   * Apply arguments sent from the client to the current (possibly reused) execution
   * sandbox.
   */
  applyArgumentsToSandbox: function MDA_applyArgumentsToSandbox(win, sandbox, args, command_id) {
    try {
      sandbox.__marionetteParams = this.curBrowser.elementManager.convertWrappedArguments(args, win);
    }
    catch(e) {
      this.sendError(e.message, e.code, e.stack, command_id);
    }
    sandbox.__namedArgs = this.curBrowser.elementManager.applyNamedArgs(args);
  },

  /**
   * Executes a script in the given sandbox.
   *
   * @param Sandbox sandbox
   *        Sandbox in which the script will run
   * @param string script
   *        The script to run
   * @param boolean directInject
   *        If true, then the script will be run as is,
   *        and not as a function body (as you would
   *        do using the WebDriver spec)
   * @param boolean async
   *        True if the script is asynchronous
   */
  executeScriptInSandbox: function MDA_executeScriptInSandbox(sandbox, script,
     directInject, async, command_id, timeout) {

    if (directInject && async &&
        (timeout == null || timeout == 0)) {
      this.sendError("Please set a timeout", 21, null, command_id);
      return;
    }

    if (this.importedScripts.exists()) {
      let stream = Cc["@mozilla.org/network/file-input-stream;1"].
                    createInstance(Ci.nsIFileInputStream);
      stream.init(this.importedScripts, -1, 0, 0);
      let data = NetUtil.readInputStreamToString(stream, stream.available());
      stream.close();
      script = data + script;
    }

    let res = Cu.evalInSandbox(script, sandbox, "1.8", "dummy file", 0);

    if (directInject && !async &&
        (res == undefined || res.passed == undefined)) {
      this.sendError("finish() not called", 500, null, command_id);
      return;
    }

    if (!async) {
      this.sendResponse(this.curBrowser.elementManager.wrapValue(res),
                        command_id);
    }
  },

  /**
   * Execute the given script either as a function body (executeScript)
   * or directly (for 'mochitest' like JS Marionette tests)
   *
   * @param object aRequest
   *        'script' member is the script to run
   *        'args' member holds the arguments to the script
   * @param boolean directInject
   *        if true, it will be run directly and not as a
   *        function body
   */
  execute: function MDA_execute(aRequest, directInject) {
    let inactivityTimeout = aRequest.parameters.inactivityTimeout;
    let timeout = aRequest.parameters.scriptTimeout ? aRequest.parameters.scriptTimeout : this.scriptTimeout;
    let command_id = this.command_id = this.getCommandId();
    let script;
    let newSandbox = aRequest.parameters.newSandbox;
    if (newSandbox == undefined) {
      //if client does not send a value in newSandbox,
      //then they expect the same behaviour as webdriver
      newSandbox = true;
    }
    if (this.context == "content") {
      this.sendAsync("executeScript",
                     {
                       script: aRequest.parameters.script,
                       args: aRequest.parameters.args,
                       newSandbox: newSandbox,
                       timeout: timeout,
                       specialPowers: aRequest.parameters.specialPowers,
                       filename: aRequest.parameters.filename,
                       line: aRequest.parameters.line
                     },
                     command_id);
      return;
    }

    // handle the inactivity timeout
    let that = this;
    if (inactivityTimeout) {
     let inactivityTimeoutHandler = function(message, status) {
      let error_msg = {message: value, status: status};
      that.sendToClient({from: that.actorID, error: error_msg},
                        marionette.command_id);
     };
     let setTimer = function() {
      that.inactivityTimer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
      if (that.inactivityTimer != null) {
       that.inactivityTimer.initWithCallback(function() {
        inactivityTimeoutHandler("timed out due to inactivity", 28);
       }, inactivityTimeout, Ci.nsITimer.TYPE_ONE_SHOT);
      }
     }
     setTimer();
     this.heartbeatCallback = function resetInactivityTimer() {
      that.inactivityTimer.cancel();
      setTimer();
     }
    }


    let curWindow = this.getCurrentWindow();
    if (!this.sandbox || newSandbox) {
      let marionette = new Marionette(this, curWindow, "chrome",
                                      this.marionetteLog,
                                      timeout, this.heartbeatCallback, this.testName);
      this.sandbox = this.createExecuteSandbox(curWindow,
                                               marionette,
                                               aRequest.parameters.specialPowers,
                                               command_id);
      if (!this.sandbox)
        return;
    }
    this.applyArgumentsToSandbox(curWindow, this.sandbox, aRequest.parameters.args,
                                 command_id)

    try {
      this.sandbox.finish = function chromeSandbox_finish() {
        if (that.inactivityTimer != null) {
          that.inactivityTimer.cancel();
        }
        return that.sandbox.generate_results();
      };

      if (directInject) {
        script = aRequest.parameters.script;
      }
      else {
        script = "let func = function() {" +
                       aRequest.parameters.script +
                     "};" +
                     "func.apply(null, __marionetteParams);";
      }
      this.executeScriptInSandbox(this.sandbox, script, directInject,
                                  false, command_id, timeout);
    }
    catch (e) {
      let error = createStackMessage(e,
                                     "execute_script",
                                     aRequest.parameters.filename,
                                     aRequest.parameters.line,
                                     script);
      this.sendError(error[0], 17, error[1], command_id);
    }
  },

  /**
   * Set the timeout for asynchronous script execution
   *
   * @param object aRequest
   *        'ms' member is time in milliseconds to set timeout
   */
  setScriptTimeout: function MDA_setScriptTimeout(aRequest) {
    this.command_id = this.getCommandId();
    let timeout = parseInt(aRequest.parameters.ms);
    if(isNaN(timeout)){
      this.sendError("Not a Number", 500, null, this.command_id);
    }
    else {
      this.scriptTimeout = timeout;
      this.sendOk(this.command_id);
    }
  },

  /**
   * execute pure JS script. Used to execute 'mochitest'-style Marionette tests.
   *
   * @param object aRequest
   *        'script' member holds the script to execute
   *        'args' member holds the arguments to the script
   *        'timeout' member will be used as the script timeout if it is given
   */
  executeJSScript: function MDA_executeJSScript(aRequest) {
    let timeout = aRequest.parameters.scriptTimeout ? aRequest.parameters.scriptTimeout : this.scriptTimeout;
    let command_id = this.command_id = this.getCommandId();

    //all pure JS scripts will need to call Marionette.finish() to complete the test.
    if (aRequest.newSandbox == undefined) {
      //if client does not send a value in newSandbox,
      //then they expect the same behaviour as webdriver
      aRequest.newSandbox = true;
    }
    if (this.context == "chrome") {
      if (aRequest.parameters.async) {
        this.executeWithCallback(aRequest, aRequest.parameters.async);
      }
      else {
        this.execute(aRequest, true);
      }
    }
    else {
      this.sendAsync("executeJSScript",
                     {
                       script: aRequest.parameters.script,
                       args: aRequest.parameters.args,
                       newSandbox: aRequest.parameters.newSandbox,
                       async: aRequest.parameters.async,
                       timeout: timeout,
                       inactivityTimeout: aRequest.parameters.inactivityTimeout,
                       specialPowers: aRequest.parameters.specialPowers,
                       filename: aRequest.parameters.filename,
                       line: aRequest.parameters.line,
                     },
                     command_id);
   }
  },

  /**
   * This function is used by executeAsync and executeJSScript to execute a script
   * in a sandbox.
   *
   * For executeJSScript, it will return a message only when the finish() method is called.
   * For executeAsync, it will return a response when marionetteScriptFinished/arguments[arguments.length-1]
   * method is called, or if it times out.
   *
   * @param object aRequest
   *        'script' member holds the script to execute
   *        'args' member holds the arguments for the script
   * @param boolean directInject
   *        if true, it will be run directly and not as a
   *        function body
   */
  executeWithCallback: function MDA_executeWithCallback(aRequest, directInject) {
    let inactivityTimeout = aRequest.parameters.inactivityTimeout;
    let timeout = aRequest.parameters.scriptTimeout ? aRequest.parameters.scriptTimeout : this.scriptTimeout;
    let command_id = this.command_id = this.getCommandId();
    let script;
    let newSandbox = aRequest.parameters.newSandbox;
    if (newSandbox == undefined) {
      //if client does not send a value in newSandbox,
      //then they expect the same behaviour as webdriver
      newSandbox = true;
    }

    if (this.context == "content") {
      this.sendAsync("executeAsyncScript",
                     {
                       script: aRequest.parameters.script,
                       args: aRequest.parameters.args,
                       id: this.command_id,
                       newSandbox: newSandbox,
                       timeout: timeout,
                       inactivityTimeout: inactivityTimeout,
                       specialPowers: aRequest.parameters.specialPowers,
                       filename: aRequest.parameters.filename,
                       line: aRequest.parameters.line
                     },
                     command_id);
      return;
    }

    // handle the inactivity timeout
    let that = this;
    if (inactivityTimeout) {
     this.inactivityTimer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
     if (this.inactivityTimer != null) {
      this.inactivityTimer.initWithCallback(function() {
       chromeAsyncReturnFunc("timed out due to inactivity", 28);
      }, inactivityTimeout, Ci.nsITimer.TYPE_ONE_SHOT);
     }
     this.heartbeatCallback = function resetInactivityTimer() {
      that.inactivityTimer.cancel();
      that.inactivityTimer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
      if (that.inactivityTimer != null) {
       that.inactivityTimer.initWithCallback(function() {
        chromeAsyncReturnFunc("timed out due to inactivity", 28);
       }, inactivityTimeout, Ci.nsITimer.TYPE_ONE_SHOT);
      }
     }
    }

    let curWindow = this.getCurrentWindow();
    let original_onerror = curWindow.onerror;
    that.timeout = timeout;

    function chromeAsyncReturnFunc(value, status, stacktrace) {
      if (that._emu_cbs && Object.keys(that._emu_cbs).length) {
        value = "Emulator callback still pending when finish() called";
        status = 500;
        that._emu_cbs = null;
      }

      if (value == undefined)
        value = null;

      if (command_id == that.command_id) {
        if (that.timer != null) {
          that.timer.cancel();
          that.timer = null;
        }

        curWindow.onerror = original_onerror;

        if (status == 0 || status == undefined) {
          that.sendToClient({from: that.actorID, value: that.curBrowser.elementManager.wrapValue(value), status: status},
                            that.command_id);
        }
        else {
          let error_msg = {message: value, status: status, stacktrace: stacktrace};
          that.sendToClient({from: that.actorID, error: error_msg},
                            that.command_id);
        }
      }

      if (that.inactivityTimer != null) {
        that.inactivityTimer.cancel();
      }
    }

    // NB: curWindow.onerror is not hooked by default due to the inability to
    //     differentiate content exceptions from chrome exceptions. See bug
    //     1128760 for more details. A 'debug_script' flag can be set to
    //     reenable onerror hooking to help debug test scripts.
    if (aRequest.parameters.debug_script) {
      curWindow.onerror = function (errorMsg, url, lineNumber) {
        chromeAsyncReturnFunc(errorMsg + " at: " + url + " line: " + lineNumber, 17);
        return true;
      };
    }

    function chromeAsyncFinish() {
      chromeAsyncReturnFunc(that.sandbox.generate_results(), 0);
    }

    if (!this.sandbox || newSandbox) {
      let marionette = new Marionette(this, curWindow, "chrome",
                                      this.marionetteLog,
                                      timeout, this.heartbeatCallback, this.testName);
      this.sandbox = this.createExecuteSandbox(curWindow,
                                               marionette,
                                               aRequest.parameters.specialPowers,
                                               command_id);
      if (!this.sandbox)
        return;
    }
    this.applyArgumentsToSandbox(curWindow, this.sandbox, aRequest.parameters.args,
                                 command_id)

    try {

      this.timer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
      if (this.timer != null) {
        this.timer.initWithCallback(function() {
          chromeAsyncReturnFunc("timed out", 28);
        }, that.timeout, Ci.nsITimer.TYPE_ONE_SHOT);
      }

      this.sandbox.returnFunc = chromeAsyncReturnFunc;
      this.sandbox.finish = chromeAsyncFinish;

      if (directInject) {
        script = aRequest.parameters.script;
      }
      else {
        script =  '__marionetteParams.push(returnFunc);'
                + 'let marionetteScriptFinished = returnFunc;'
                + 'let __marionetteFunc = function() {' + aRequest.parameters.script + '};'
                + '__marionetteFunc.apply(null, __marionetteParams);';
      }

      this.executeScriptInSandbox(this.sandbox, script, directInject,
                                  true, command_id, timeout);
    } catch (e) {
      let error = createStackMessage(e,
                                     "execute_async_script",
                                     aRequest.parameters.filename,
                                     aRequest.parameters.line,
                                     script);
      chromeAsyncReturnFunc(error[0], 17, error[1]);
    }
  },

  /**
   * Navigate to to given URL.
   *
   * This will follow redirects issued by the server.  When the method
   * returns is based on the page load strategy that the user has
   * selected.
   *
   * Documents that contain a META tag with the "http-equiv" attribute
   * set to "refresh" will return if the timeout is greater than 1
   * second and the other criteria for determining whether a page is
   * loaded are met.  When the refresh period is 1 second or less and
   * the page load strategy is "normal" or "conservative", it will
   * wait for the page to complete loading before returning.
   *
   * If any modal dialog box, such as those opened on
   * window.onbeforeunload or window.alert, is opened at any point in
   * the page load, it will return immediately.
   *
   * If a 401 response is seen by the browser, it will return
   * immediately.  That is, if BASIC, DIGEST, NTLM or similar
   * authentication is required, the page load is assumed to be
   * complete.  This does not include FORM-based authentication.
   *
   * @param object aRequest where <code>url</code> property holds the
   *        URL to navigate to
   */
  get: function MDA_get(aRequest) {
    let command_id = this.command_id = this.getCommandId();

    if (this.context != "chrome") {
      // If a remoteness update interrupts our page load, this will never return
      // We need to re-issue this request to correctly poll for readyState and
      // send errors.
      this.curBrowser.pendingCommands.push(() => {
        aRequest.parameters.command_id = command_id;
        this.messageManager.broadcastAsyncMessage(
          "Marionette:pollForReadyState" + this.curBrowser.curFrameId,
          aRequest.parameters);
      });
      aRequest.command_id = command_id;
      aRequest.parameters.pageTimeout = this.pageTimeout;
      this.sendAsync("get", aRequest.parameters, command_id);
      return;
    }

    // At least on desktop, navigating in chrome scope does not
    // correspond to something a user can do, and leaves marionette
    // and the browser in an unusable state. Return a generic error insted.
    // TODO: Error codes need to be refined as a part of bug 1100545 and
    // bug 945729.
    if (appName == "Firefox") {
      this.sendError("Cannot navigate in chrome context", 13, null, command_id);
      return;
    }

    this.getCurrentWindow().location.href = aRequest.parameters.url;
    let checkTimer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
    let start = new Date().getTime();
    let end = null;

    function checkLoad() {
      end = new Date().getTime();
      let elapse = end - start;
      if (this.pageTimeout == null || elapse <= this.pageTimeout){
        if (curWindow.document.readyState == "complete") {
          sendOk(command_id);
          return;
        }
        else{
          checkTimer.initWithCallback(checkLoad, 100, Ci.nsITimer.TYPE_ONE_SHOT);
        }
      }
      else{
        sendError("Error loading page", 13, null, command_id);
        return;
      }
    }
    checkTimer.initWithCallback(checkLoad, 100, Ci.nsITimer.TYPE_ONE_SHOT);
  },

  /**
   * Get a string representing the current URL.
   *
   * On Desktop this returns a string representation of the URL of the
   * current top level browsing context.  This is equivalent to
   * document.location.href.
   *
   * When in the context of the chrome, this returns the canonical URL
   * of the current resource.
   */
  getCurrentUrl: function MDA_getCurrentUrl() {
    let isB2G = appName == "B2G";
    this.command_id = this.getCommandId();
    if (this.context === "chrome") {
      this.sendResponse(this.getCurrentWindow().location.href, this.command_id);
    }
    else {
      this.sendAsync("getCurrentUrl", {isB2G: isB2G}, this.command_id);
    }
  },

  /**
   * Gets the current title of the window
   */
  getTitle: function MDA_getTitle() {
    this.command_id = this.getCommandId();
    if (this.context == "chrome"){
      var curWindow = this.getCurrentWindow();
      var title = curWindow.document.documentElement.getAttribute('title');
      this.sendResponse(title, this.command_id);
    }
    else {
      this.sendAsync("getTitle", {}, this.command_id);
    }
  },

  /**
   * Gets the current type of the window
   */
  getWindowType: function MDA_getWindowType() {
    this.command_id = this.getCommandId();
      var curWindow = this.getCurrentWindow();
      var type = curWindow.document.documentElement.getAttribute('windowtype');
      this.sendResponse(type, this.command_id);
  },

  /**
   * Gets the page source of the content document
   */
  getPageSource: function MDA_getPageSource(){
    this.command_id = this.getCommandId();
    if (this.context == "chrome"){
      let curWindow = this.getCurrentWindow();
      let XMLSerializer = curWindow.XMLSerializer;
      let pageSource = new XMLSerializer().serializeToString(curWindow.document);
      this.sendResponse(pageSource, this.command_id);
    }
    else {
      this.sendAsync("getPageSource", {}, this.command_id);
    }
  },

  /**
   * Go back in history
   */
  goBack: function MDA_goBack() {
    this.command_id = this.getCommandId();
    this.sendAsync("goBack", {}, this.command_id);
  },

  /**
   * Go forward in history
   */
  goForward: function MDA_goForward() {
    this.command_id = this.getCommandId();
    this.sendAsync("goForward", {}, this.command_id);
  },

  /**
   * Refresh the page
   */
  refresh: function MDA_refresh() {
    this.command_id = this.getCommandId();
    this.sendAsync("refresh", {}, this.command_id);
  },

  /**
   * Get the current window's handle. On desktop this typically corresponds to
   * the currently selected tab.
   *
   * Return an opaque server-assigned identifier to this window that
   * uniquely identifies it within this Marionette instance.  This can
   * be used to switch to this window at a later point.
   *
   * @return unique window handle (string)
   */
  getWindowHandle: function MDA_getWindowHandle() {
    this.command_id = this.getCommandId();
    // curFrameId always holds the current tab.
    if (this.curBrowser.curFrameId && appName != 'B2G') {
      this.sendResponse(this.curBrowser.curFrameId, this.command_id);
      return;
    }
    for (let i in this.browsers) {
      if (this.curBrowser == this.browsers[i]) {
        this.sendResponse(i, this.command_id);
        return;
      }
    }
  },

  /**
   * Forces an update for the given browser's id.
   */
  updateIdForBrowser: function (browser, newId) {
    this._browserIds.set(browser.permanentKey, newId);
  },

  /**
   * Retrieves a listener id for the given xul browser element. In case
   * the browser is not known, an attempt is made to retrieve the id from
   * a CPOW, and null is returned if this fails.
   */
  getIdForBrowser: function (browser) {
    if (browser === null) {
      return null;
    }
    let permKey = browser.permanentKey;
    if (this._browserIds.has(permKey)) {
      return this._browserIds.get(permKey);
    }

    let winId = browser.outerWindowID;
    if (winId) {
      winId += "";
      this._browserIds.set(permKey, winId);
      return winId;
    }
    return null;
  },

  /**
   * Get a list of top-level browsing contexts. On desktop this typically
   * corresponds to the set of open tabs.
   *
   * Each window handle is assigned by the server and is guaranteed unique,
   * however the return array does not have a specified ordering.
   *
   * @return array of unique window handles as strings
   */
  getWindowHandles: function MDA_getWindowHandles() {
    this.command_id = this.getCommandId();
    let res = [];
    let winEn = this.getWinEnumerator();
    while (winEn.hasMoreElements()) {
      let win = winEn.getNext();
      if (win.gBrowser && appName != 'B2G') {
        let tabbrowser = win.gBrowser;
        for (let i = 0; i < tabbrowser.browsers.length; ++i) {
          let winId = this.getIdForBrowser(tabbrowser.getBrowserAtIndex(i));
          if (winId !== null) {
            res.push(winId);
          }
        }
      } else {
        // XUL Windows, at least, do not have gBrowser.
        let winId = win.QueryInterface(Ci.nsIInterfaceRequestor)
                       .getInterface(Ci.nsIDOMWindowUtils)
                       .outerWindowID;
        winId += (appName == "B2G") ? "-b2g" : "";
        res.push(winId);
      }
    }
    this.sendResponse(res, this.command_id);
  },

  /**
   * Get the current window's handle. This corresponds to a window that
   * may itself contain tabs.
   *
   * Return an opaque server-assigned identifier to this window that
   * uniquely identifies it within this Marionette instance.  This can
   * be used to switch to this window at a later point.
   *
   * @return unique window handle (string)
   */
  getChromeWindowHandle: function MDA_getChromeWindowHandle() {
    this.command_id = this.getCommandId();
    for (let i in this.browsers) {
      if (this.curBrowser == this.browsers[i]) {
        this.sendResponse(i, this.command_id);
        return;
      }
    }
  },

  /**
   * Returns identifiers for each open chrome window for tests interested in
   * managing a set of chrome windows and tabs separately.
   *
   * @return array of unique window handles as strings
   */
  getChromeWindowHandles: function MDA_getChromeWindowHandles() {
    this.command_id = this.getCommandId();
    let res = [];
    let winEn = this.getWinEnumerator();
    while (winEn.hasMoreElements()) {
      let foundWin = winEn.getNext();
      let winId = foundWin.QueryInterface(Ci.nsIInterfaceRequestor)
                          .getInterface(Ci.nsIDOMWindowUtils)
                          .outerWindowID;
      winId = winId + ((appName == "B2G") ? "-b2g" : "");
      res.push(winId);
    }
    this.sendResponse(res, this.command_id);
  },

  /**
   * Get the current window position.
   */
  getWindowPosition: function MDA_getWindowPosition() {
    this.command_id = this.getCommandId();
    let curWindow = this.getCurrentWindow();
    this.sendResponse({ x: curWindow.screenX, y: curWindow.screenY}, this.command_id);
  },

  /**
  * Set the window position of the browser on the OS Window Manager
  *
  * @param object aRequest
  *        'x': the x co-ordinate of the top/left of the window that
  *             it will be moved to
  *        'y': the y co-ordinate of the top/left of the window that
  *             it will be moved to
  */
  setWindowPosition: function MDA_setWindowPosition(aRequest) {
    let command_id = this.command_id = this.getCommandId();
    if (appName !== "Firefox") {
      this.sendError("Unable to set the window position on mobile", 61, null,
                      command_id);

    }
    else {
      let x = parseInt(aRequest.parameters.x);;
      let y  = parseInt(aRequest.parameters.y);

      if (isNaN(x) || isNaN(y)) {
        this.sendError("x and y arguments should be integers", 13, null, command_id);
        return;
      }
      let curWindow = this.getCurrentWindow();
      curWindow.moveTo(x, y);
      this.sendOk(command_id);
    }
  },

  /**
   * Switch to a window based on name or server-assigned id.
   * Searches based on name, then id.
   *
   * @param object aRequest
   *        'name' member holds the name or id of the window to switch to
   */
  switchToWindow: function MDA_switchToWindow(aRequest) {
    let command_id = this.command_id = this.getCommandId();

    let checkWindow = function (win, outerId, contentWindowId, ind) {
      if (aRequest.parameters.name == win.name ||
          aRequest.parameters.name == contentWindowId ||
          aRequest.parameters.name == outerId) {
        // As in content, switching to a new window invalidates a sandbox for reuse.
        this.sandbox = null;
        if (this.browsers[outerId] === undefined) {
          //enable Marionette in that browser window
          this.startBrowser(win, false);
        } else {
          utils.window = win;
          this.curBrowser = this.browsers[outerId];
          if (contentWindowId) {
            // The updated id corresponds to switching to a new tab.
            this.curBrowser.switchToTab(ind);
          }
          this.sendOk(command_id);
        }
        return true;
      }
      return false;
    }

    let winEn = this.getWinEnumerator();
    while (winEn.hasMoreElements()) {
      let win = winEn.getNext();
      let outerId = win.QueryInterface(Ci.nsIInterfaceRequestor)
                       .getInterface(Ci.nsIDOMWindowUtils)
                       .outerWindowID;
      outerId += (appName == "B2G") ? "-b2g" : "";
      if (win.gBrowser && appName != 'B2G') {
        let tabbrowser = win.gBrowser;
        for (let i = 0; i < tabbrowser.browsers.length; ++i) {
          let browser = tabbrowser.getBrowserAtIndex(i);
          let contentWindowId = this.getIdForBrowser(browser);
          if (contentWindowId !== null &&
              checkWindow.call(this, win, outerId, contentWindowId, i)) {
            return;
          }
        }
      } else {
        // A chrome window is always a valid target for switching in the case
        // a handle was obtained by getChromeWindowHandles.
        if (checkWindow.call(this, win, outerId)) {
          return;
        }
      }
    }
    this.sendError("Unable to locate window " + aRequest.parameters.name, 23, null,
                   command_id);
  },

  getActiveFrame: function MDA_getActiveFrame() {
    this.command_id = this.getCommandId();

    if (this.context == "chrome") {
      if (this.curFrame) {
        let frameUid = this.curBrowser.elementManager.addToKnownElements(this.curFrame.frameElement);
        this.sendResponse(frameUid, this.command_id);
      } else {
        // no current frame, we're at toplevel
        this.sendResponse(null, this.command_id);
      }
    } else {
      // not chrome
      this.sendResponse(this.currentFrameElement, this.command_id);
    }
  },

  /**
   * Switch to a given frame within the current window
   *
   * @param object aRequest
   *        'element' is the element to switch to
   *        'id' if element is not set, then this
   *                holds either the id, name or index
   *                of the frame to switch to
   */
  switchToFrame: function MDA_switchToFrame(aRequest) {
    let command_id = this.command_id = this.getCommandId();
    let checkTimer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
    let curWindow = this.getCurrentWindow();
    let checkLoad = function() {
      let errorRegex = /about:.+(error)|(blocked)\?/;
      let curWindow = this.getCurrentWindow();
      if (curWindow.document.readyState == "complete") {
        this.sendOk(command_id);
        return;
      }
      else if (curWindow.document.readyState == "interactive" && errorRegex.exec(curWindow.document.baseURI)) {
        this.sendError("Error loading page", 13, null, command_id);
        return;
      }

      checkTimer.initWithCallback(checkLoad.bind(this), 100, Ci.nsITimer.TYPE_ONE_SHOT);
    }
    if (this.context == "chrome") {
      let foundFrame = null;
      if ((aRequest.parameters.id == null) && (aRequest.parameters.element == null)) {
        this.curFrame = null;
        if (aRequest.parameters.focus) {
          this.mainFrame.focus();
        }
        checkTimer.initWithCallback(checkLoad.bind(this), 100, Ci.nsITimer.TYPE_ONE_SHOT);
        return;
      }
      if (aRequest.parameters.element != undefined) {
        if (this.curBrowser.elementManager.seenItems[aRequest.parameters.element]) {
          let wantedFrame = this.curBrowser.elementManager.getKnownElement(aRequest.parameters.element, curWindow); //HTMLIFrameElement
          // Deal with an embedded xul:browser case
          if (wantedFrame.tagName == "xul:browser" || wantedFrame.tagName == "browser") {
            curWindow = wantedFrame.contentWindow;
            this.curFrame = curWindow;
            if (aRequest.parameters.focus) {
              this.curFrame.focus();
            }
            checkTimer.initWithCallback(checkLoad.bind(this), 100, Ci.nsITimer.TYPE_ONE_SHOT);
            return;
          }
          // else, assume iframe
          let frames = curWindow.document.getElementsByTagName("iframe");
          let numFrames = frames.length;
          for (let i = 0; i < numFrames; i++) {
            if (XPCNativeWrapper(frames[i]) == XPCNativeWrapper(wantedFrame)) {
              curWindow = frames[i].contentWindow;
              this.curFrame = curWindow;
              if (aRequest.parameters.focus) {
                this.curFrame.focus();
              }
              checkTimer.initWithCallback(checkLoad.bind(this), 100, Ci.nsITimer.TYPE_ONE_SHOT);
              return;
          }
        }
      }
    }
    switch(typeof(aRequest.parameters.id)) {
      case "string" :
        let foundById = null;
        let frames = curWindow.document.getElementsByTagName("iframe");
        let numFrames = frames.length;
        for (let i = 0; i < numFrames; i++) {
          //give precedence to name
          let frame = frames[i];
          if (frame.getAttribute("name") == aRequest.parameters.id) {
            foundFrame = i;
            curWindow = frame.contentWindow;
            break;
          } else if ((foundById == null) && (frame.id == aRequest.parameters.id)) {
            foundById = i;
          }
        }
        if ((foundFrame == null) && (foundById != null)) {
          foundFrame = foundById;
          curWindow = frames[foundById].contentWindow;
        }
        break;
      case "number":
        if (curWindow.frames[aRequest.parameters.id] != undefined) {
          foundFrame = aRequest.parameters.id;
          curWindow = curWindow.frames[foundFrame].frameElement.contentWindow;
        }
        break;
      }
      if (foundFrame != null) {
        this.curFrame = curWindow;
        if (aRequest.parameters.focus) {
          this.curFrame.focus();
        }
        checkTimer.initWithCallback(checkLoad.bind(this), 100, Ci.nsITimer.TYPE_ONE_SHOT);
      } else {
        this.sendError("Unable to locate frame: " + aRequest.parameters.id, 8, null,
                       command_id);
      }
    }
    else {
      if ((!aRequest.parameters.id) && (!aRequest.parameters.element) &&
          (this.curBrowser.frameManager.currentRemoteFrame !== null)) {
        // We're currently using a ChromeMessageSender for a remote frame, so this
        // request indicates we need to switch back to the top-level (parent) frame.
        // We'll first switch to the parent's (global) ChromeMessageBroadcaster, so
        // we send the message to the right listener.
        this.switchToGlobalMessageManager();
      }
      aRequest.command_id = command_id;
      this.sendAsync("switchToFrame", aRequest.parameters, command_id);
    }
  },

  /**
   * Set timeout for searching for elements
   *
   * @param object aRequest
   *        'ms' holds the search timeout in milliseconds
   */
  setSearchTimeout: function MDA_setSearchTimeout(aRequest) {
    this.command_id = this.getCommandId();
    let timeout = parseInt(aRequest.parameters.ms);
    if (isNaN(timeout)) {
      this.sendError("Not a Number", 500, null, this.command_id);
    }
    else {
      this.searchTimeout = timeout;
      this.sendOk(this.command_id);
    }
  },

  /**
   * Set timeout for page loading, searching and scripts
   *
   * @param object aRequest
   *        'type' hold the type of timeout
   *        'ms' holds the timeout in milliseconds
   */
  timeouts: function MDA_timeouts(aRequest){
    /*setTimeout*/
    this.command_id = this.getCommandId();
    let timeout_type = aRequest.parameters.type;
    let timeout = parseInt(aRequest.parameters.ms);
    if (isNaN(timeout)) {
      this.sendError("Not a Number", 500, null, this.command_id);
    }
    else {
      if (timeout_type == "implicit") {
        this.setSearchTimeout(aRequest);
      }
      else if (timeout_type == "script") {
        this.setScriptTimeout(aRequest);
      }
      else {
        this.pageTimeout = timeout;
        this.sendOk(this.command_id);
      }
    }
  },

  /**
   * Single Tap
   *
   * @param object aRequest
            'element' represents the ID of the element to single tap on
   */
  singleTap: function MDA_singleTap(aRequest) {
    this.command_id = this.getCommandId();
    let serId = aRequest.parameters.id;
    let x = aRequest.parameters.x;
    let y = aRequest.parameters.y;
    if (this.context == "chrome") {
      this.sendError("Command 'singleTap' is not available in chrome context", 500, null, this.command_id);
    }
    else {
      this.addFrameCloseListener("tap");
      this.sendAsync("singleTap",
                     {
                       id: serId,
                       corx: x,
                       cory: y
                     },
                     this.command_id);
    }
  },

  /**
   * actionChain
   *
   * @param object aRequest
   *        'value' represents a nested array: inner array represents each event; outer array represents collection of events
   */
  actionChain: function MDA_actionChain(aRequest) {
    this.command_id = this.getCommandId();
    if (this.context == "chrome") {
      this.sendError("Command 'actionChain' is not available in chrome context", 500, null, this.command_id);
    }
    else {
      this.addFrameCloseListener("action chain");
      this.sendAsync("actionChain",
                     {
                       chain: aRequest.parameters.chain,
                       nextId: aRequest.parameters.nextId
                     },
                     this.command_id);
    }
  },

  /**
   * multiAction
   *
   * @param object aRequest
   *        'value' represents a nested array: inner array represents each event;
   *        middle array represents collection of events for each finger
   *        outer array represents all the fingers
   */

  multiAction: function MDA_multiAction(aRequest) {
    this.command_id = this.getCommandId();
    if (this.context == "chrome") {
       this.sendError("Command 'multiAction' is not available in chrome context", 500, null, this.command_id);
    }
    else {
      this.addFrameCloseListener("multi action chain");
      this.sendAsync("multiAction",
                     {
                       value: aRequest.parameters.value,
                       maxlen: aRequest.parameters.max_length
                     },
                     this.command_id);
   }
 },

  /**
   * Find an element using the indicated search strategy.
   *
   * @param object aRequest
   *        'using' member indicates which search method to use
   *        'value' member is the value the client is looking for
   */
  findElement: function MDA_findElement(aRequest) {
    let command_id = this.command_id = this.getCommandId();
    if (this.context == "chrome") {
      let id;
      try {
        let on_success = this.sendResponse.bind(this);
        let on_error = this.sendError.bind(this);
        id = this.curBrowser.elementManager.find(
                              this.getCurrentWindow(),
                              aRequest.parameters,
                              this.searchTimeout,
                              on_success,
                              on_error,
                              false,
                              command_id);
      }
      catch (e) {
        this.sendError(e.message, e.code, e.stack, command_id);
        return;
      }
    }
    else {
      this.sendAsync("findElementContent",
                     {
                       value: aRequest.parameters.value,
                       using: aRequest.parameters.using,
                       element: aRequest.parameters.element,
                       searchTimeout: this.searchTimeout
                     },
                     command_id);
    }
  },

  /**
   * Find element using the indicated search strategy
   * starting from a known element. Used for WebDriver Compatibility only.
   * @param  {object} aRequest
   *         'using' member indicates which search method to use
   *         'value' member is the value the client is looking for
   *         'id' member is the value of the element to start from
   */
  findChildElement: function MDA_findChildElement(aRequest) {
    let command_id = this.command_id = this.getCommandId();
    this.sendAsync("findElementContent",
                    {
                       value: aRequest.parameters.value,
                       using: aRequest.parameters.using,
                       element: aRequest.parameters.id,
                       searchTimeout: this.searchTimeout
                     },
                     command_id);
  },

  /**
   * Find elements using the indicated search strategy.
   *
   * @param object aRequest
   *        'using' member indicates which search method to use
   *        'value' member is the value the client is looking for
   */
  findElements: function MDA_findElements(aRequest) {
    let command_id = this.command_id = this.getCommandId();
    if (this.context == "chrome") {
      let id;
      try {
        let on_success = this.sendResponse.bind(this);
        let on_error = this.sendError.bind(this);
        id = this.curBrowser.elementManager.find(this.getCurrentWindow(),
                                                 aRequest.parameters,
                                                 this.searchTimeout,
                                                 on_success,
                                                 on_error,
                                                 true,
                                                 command_id);
      }
      catch (e) {
        this.sendError(e.message, e.code, e.stack, command_id);
        return;
      }
    }
    else {
      this.sendAsync("findElementsContent",
                     {
                       value: aRequest.parameters.value,
                       using: aRequest.parameters.using,
                       element: aRequest.parameters.element,
                       searchTimeout: this.searchTimeout
                     },
                     command_id);
    }
  },

  /**
   * Find elements using the indicated search strategy
   * starting from a known element. Used for WebDriver Compatibility only.
   * @param  {object} aRequest
   *         'using' member indicates which search method to use
   *         'value' member is the value the client is looking for
   *         'id' member is the value of the element to start from
   */
  findChildElements: function MDA_findChildElement(aRequest) {
    let command_id = this.command_id = this.getCommandId();
    this.sendAsync("findElementsContent",
                    {
                       value: aRequest.parameters.value,
                       using: aRequest.parameters.using,
                       element: aRequest.parameters.id,
                       searchTimeout: this.searchTimeout
                     },
                     command_id);
  },

  /**
   * Return the active element on the page
   */
  getActiveElement: function MDA_getActiveElement(){
    let command_id = this.command_id = this.getCommandId();
    this.sendAsync("getActiveElement", {}, command_id);
  },

  /**
   * Send click event to element
   *
   * @param object aRequest
   *        'id' member holds the reference id to
   *        the element that will be clicked
   */
  clickElement: function MDA_clickElementent(aRequest) {
    let command_id = this.command_id = this.getCommandId();
    if (this.context == "chrome") {
      try {
        //NOTE: click atom fails, fall back to click() action
        let el = this.curBrowser.elementManager.getKnownElement(
            aRequest.parameters.id, this.getCurrentWindow());
        el.click();
        this.sendOk(command_id);
      }
      catch (e) {
        this.sendError(e.message, e.code, e.stack, command_id);
      }
    }
    else {
      // We need to protect against the click causing an OOP frame to close.
      // This fires the mozbrowserclose event when it closes so we need to
      // listen for it and then just send an error back. The person making the
      // call should be aware something isnt right and handle accordingly
      this.addFrameCloseListener("click");
      this.sendAsync("clickElement",
                     { id: aRequest.parameters.id },
                     command_id);
    }
  },

  /**
   * Get a given attribute of an element
   *
   * @param object aRequest
   *        'id' member holds the reference id to
   *        the element that will be inspected
   *        'name' member holds the name of the attribute to retrieve
   */
  getElementAttribute: function MDA_getElementAttribute(aRequest) {
    let command_id = this.command_id = this.getCommandId();
    if (this.context == "chrome") {
      try {
        let el = this.curBrowser.elementManager.getKnownElement(
            aRequest.parameters.id, this.getCurrentWindow());
        this.sendResponse(utils.getElementAttribute(el, aRequest.parameters.name),
                          command_id);
      }
      catch (e) {
        this.sendError(e.message, e.code, e.stack, command_id);
      }
    }
    else {
      this.sendAsync("getElementAttribute",
                     {
                       id: aRequest.parameters.id,
                       name: aRequest.parameters.name
                     },
                     command_id);
    }
  },

  /**
   * Get the text of an element, if any. Includes the text of all child elements.
   *
   * @param object aRequest
   *        'id' member holds the reference id to
   *        the element that will be inspected
   */
  getElementText: function MDA_getElementText(aRequest) {
    let command_id = this.command_id = this.getCommandId();
    if (this.context == "chrome") {
      //Note: for chrome, we look at text nodes, and any node with a "label" field
      try {
        let el = this.curBrowser.elementManager.getKnownElement(
            aRequest.parameters.id, this.getCurrentWindow());
        let lines = [];
        this.getVisibleText(el, lines);
        lines = lines.join("\n");
        this.sendResponse(lines, command_id);
      }
      catch (e) {
        this.sendError(e.message, e.code, e.stack, command_id);
      }
    }
    else {
      this.sendAsync("getElementText",
                     { id: aRequest.parameters.id },
                     command_id);
    }
  },

  /**
   * Get the tag name of the element.
   *
   * @param object aRequest
   *        'id' member holds the reference id to
   *        the element that will be inspected
   */
  getElementTagName: function MDA_getElementTagName(aRequest) {
    let command_id = this.command_id = this.getCommandId();
    if (this.context == "chrome") {
      try {
        let el = this.curBrowser.elementManager.getKnownElement(
            aRequest.parameters.id, this.getCurrentWindow());
        this.sendResponse(el.tagName.toLowerCase(), command_id);
      }
      catch (e) {
        this.sendError(e.message, e.code, e.stack, command_id);
      }
    }
    else {
      this.sendAsync("getElementTagName",
                     { id: aRequest.parameters.id },
                     command_id);
    }
  },

  /**
   * Check if element is displayed
   *
   * @param object aRequest
   *        'id' member holds the reference id to
   *        the element that will be checked
   */
  isElementDisplayed: function MDA_isElementDisplayed(aRequest) {
    let command_id = this.command_id = this.getCommandId();
    if (this.context == "chrome") {
      try {
        let el = this.curBrowser.elementManager.getKnownElement(
            aRequest.parameters.id, this.getCurrentWindow());
        this.sendResponse(utils.isElementDisplayed(el), command_id);
      }
      catch (e) {
        this.sendError(e.message, e.code, e.stack, command_id);
      }
    }
    else {
      this.sendAsync("isElementDisplayed",
                     { id:aRequest.parameters.id },
                     command_id);
    }
  },

  /**
   * Return the property of the computed style of an element
   *
   * @param object aRequest
   *               'id' member holds the reference id to
   *               the element that will be checked
   *               'propertyName' is the CSS rule that is being requested
   */
  getElementValueOfCssProperty: function MDA_getElementValueOfCssProperty(aRequest){
    let command_id = this.command_id = this.getCommandId();
    let curWin = this.getCurrentWindow();
    if (this.context == "chrome") {
      try {
        let el = this.curBrowser.elementManager.getKnownElement(aRequest.parameters.id, curWin);
        this.sendResponse(curWin.document.defaultView.getComputedStyle(el, null).getPropertyValue(
          aRequest.parameters.propertyName), command_id);
      } catch (e) {
        this.sendError(e.message, e.code, e.stack, command_id);
      }
    }
    else {
      this.sendAsync("getElementValueOfCssProperty",
                     {id: aRequest.parameters.id, propertyName: aRequest.parameters.propertyName},
                     command_id);
    }
  },

  /**
   * Submit a form on a content page by either using form or element in a form
   * @param object aRequest
   *               'id' member holds the reference id to
   *               the element that will be checked
  */
  submitElement: function MDA_submitElement(aRequest) {
    let command_id = this.command_id = this.getCommandId();
    if (this.context == "chrome") {
      this.sendError("Command 'submitElement' is not available in chrome context", 500, null, this.command_id);
    }
    else {
      this.sendAsync("submitElement", {id: aRequest.parameters.id}, command_id);
    }
  },

  /**
   * Check if element is enabled
   *
   * @param object aRequest
   *        'id' member holds the reference id to
   *        the element that will be checked
   */
  isElementEnabled: function(aRequest) {
    let command_id = this.command_id = this.getCommandId();
    let id = aRequest.parameters.id;
    if (this.context == "chrome") {
      try {
        // Selenium atom doesn't quite work here
        let win = this.getCurrentWindow();
        let el = this.curBrowser.elementManager.getKnownElement(id, win);
        this.sendResponse(!!!el.disabled, command_id);
      } catch (e) {
        this.sendError(e.message, e.code, e.stack, command_id);
      }
    } else {
      this.sendAsync("isElementEnabled", {id: id}, command_id);
    }
  },

  /**
   * Check if element is selected
   *
   * @param object aRequest
   *        'id' member holds the reference id to
   *        the element that will be checked
   */
  isElementSelected: function MDA_isElementSelected(aRequest) {
    let command_id = this.command_id = this.getCommandId();
    if (this.context == "chrome") {
      try {
        //Selenium atom doesn't quite work here
        let el = this.curBrowser.elementManager.getKnownElement(
            aRequest.parameters.id, this.getCurrentWindow());
        if (el.checked != undefined) {
          this.sendResponse(!!el.checked, command_id);
        }
        else if (el.selected != undefined) {
          this.sendResponse(!!el.selected, command_id);
        }
        else {
          this.sendResponse(true, command_id);
        }
      }
      catch (e) {
        this.sendError(e.message, e.code, e.stack, command_id);
      }
    }
    else {
      this.sendAsync("isElementSelected",
                     { id:aRequest.parameters.id },
                     command_id);
    }
  },

  getElementSize: function MDA_getElementSize(aRequest) {
    let command_id = this.command_id = this.getCommandId();
    if (this.context == "chrome") {
      try {
        let el = this.curBrowser.elementManager.getKnownElement(
            aRequest.parameters.id, this.getCurrentWindow());
        let clientRect = el.getBoundingClientRect();
        this.sendResponse({width: clientRect.width, height: clientRect.height},
                          command_id);
      }
      catch (e) {
        this.sendError(e.message, e.code, e.stack, command_id);
      }
    }
    else {
      this.sendAsync("getElementSize",
                     { id:aRequest.parameters.id },
                     command_id);
    }
  },

  getElementRect: function MDA_getElementRect(aRequest) {
    let command_id = this.command_id = this.getCommandId();
    if (this.context == "chrome") {
      try {
        let el = this.curBrowser.elementManager.getKnownElement(
            aRequest.parameters.id, this.getCurrentWindow());
        let clientRect = el.getBoundingClientRect();
        this.sendResponse({x: clientRect.x + this.getCurrentWindow().pageXOffset,
                           y: clientRect.y + this.getCurrentWindow().pageYOffset,
                           width: clientRect.width, height: clientRect.height},
                           command_id);
      }
      catch (e) {
        this.sendError(e.message, e.code, e.stack, command_id);
      }
    }
    else {
      this.sendAsync("getElementRect",
                     { id:aRequest.parameters.id },
                     command_id);
    }
  },

  /**
   * Send key presses to element after focusing on it
   *
   * @param object aRequest
   *        'id' member holds the reference id to
   *        the element that will be checked
   *        'value' member holds the value to send to the element
   */
  sendKeysToElement: function MDA_sendKeysToElement(aRequest) {
    let command_id = this.command_id = this.getCommandId();
    if (this.context == "chrome") {
      let currentWindow = this.getCurrentWindow();
      let el = this.curBrowser.elementManager.getKnownElement(
        aRequest.parameters.id, currentWindow);
      utils.sendKeysToElement(currentWindow, el, aRequest.parameters.value,
                              this.sendOk.bind(this), this.sendError.bind(this),
                              command_id, this.context);
    }
    else {
      this.sendAsync("sendKeysToElement",
                     {
                       id:aRequest.parameters.id,
                       value: aRequest.parameters.value
                     },
                     command_id);
    }
  },

  /**
   * Sets the test name
   *
   * The test name is used in logging messages.
   */
  setTestName: function MDA_setTestName(aRequest) {
    this.command_id = this.getCommandId();
    this.testName = aRequest.parameters.value;
    this.sendAsync("setTestName",
                   { value: aRequest.parameters.value },
                   this.command_id);
  },

  /**
   * Clear the text of an element
   *
   * @param object aRequest
   *        'id' member holds the reference id to
   *        the element that will be cleared
   */
  clearElement: function MDA_clearElement(aRequest) {
    let command_id = this.command_id = this.getCommandId();
    if (this.context == "chrome") {
      //the selenium atom doesn't work here
      try {
        let el = this.curBrowser.elementManager.getKnownElement(
            aRequest.parameters.id, this.getCurrentWindow());
        if (el.nodeName == "textbox") {
          el.value = "";
        }
        else if (el.nodeName == "checkbox") {
          el.checked = false;
        }
        this.sendOk(command_id);
      }
      catch (e) {
        this.sendError(e.message, e.code, e.stack, command_id);
      }
    }
    else {
      this.sendAsync("clearElement",
                     { id:aRequest.parameters.id },
                     command_id);
    }
  },

  /**
   * Get an element's location on the page.
   *
   * The returned point will contain the x and y coordinates of the
   * top left-hand corner of the given element.  The point (0,0)
   * refers to the upper-left corner of the document.
   *
   * @return a point containing x and y coordinates as properties
   */
  getElementLocation: function MDA_getElementLocation(aRequest) {
    this.command_id = this.getCommandId();
    this.sendAsync("getElementLocation", {id: aRequest.parameters.id},
                   this.command_id);
  },

  /**
   * Add a cookie to the document.
   */
  addCookie: function MDA_addCookie(aRequest) {
    this.command_id = this.getCommandId();
    this.sendAsync("addCookie",
                   { cookie:aRequest.parameters.cookie },
                   this.command_id);
  },

  /**
   * Get all the cookies for the current domain.
   *
   * This is the equivalent of calling "document.cookie" and parsing
   * the result.
   */
  getCookies: function MDA_getCookies() {
    this.command_id = this.getCommandId();
    this.sendAsync("getCookies", {}, this.command_id);
  },

  /**
   * Delete all cookies that are visible to a document
   */
  deleteAllCookies: function MDA_deleteAllCookies() {
    this.command_id = this.getCommandId();
    this.sendAsync("deleteAllCookies", {}, this.command_id);
  },

  /**
   * Delete a cookie by name
   */
  deleteCookie: function MDA_deleteCookie(aRequest) {
    this.command_id = this.getCommandId();
    this.sendAsync("deleteCookie",
                   { name:aRequest.parameters.name },
                   this.command_id);
  },

  /**
   * Close the current window, ending the session if it's the last
   * window currently open.
   *
   * On B2G this method is a noop and will return immediately.
   */
  close: function MDA_close() {
    let command_id = this.command_id = this.getCommandId();
    if (appName == "B2G") {
      // We can't close windows so just return
      this.sendOk(command_id);
    }
    else {
      // Get the total number of windows
      let numOpenWindows = 0;
      let winEnum = this.getWinEnumerator();
      while (winEnum.hasMoreElements()) {
        let win = winEnum.getNext();
        // Return windows and tabs.
        if (win.gBrowser) {
          numOpenWindows += win.gBrowser.browsers.length;
        } else {
          numOpenWindows += 1;
        }
      }

      // if there is only 1 window left, delete the session
      if (numOpenWindows === 1) {
        try {
          this.sessionTearDown();
        }
        catch (e) {
          this.sendError("Could not clear session", 500,
                         e.name + ": " + e.message, command_id);
          return;
        }
        this.sendOk(command_id);
        return;
      }

      try {
        if (this.messageManager != this.globalMessageManager) {
          this.messageManager.removeDelayedFrameScript(FRAME_SCRIPT);
        }
        if (this.curBrowser.tab) {
          this.curBrowser.closeTab();
        } else {
          this.getCurrentWindow().close();
        }
        this.sendOk(command_id);
      }
      catch (e) {
        this.sendError("Could not close window: " + e.message, 13, e.stack,
                       command_id);
      }
    }
  },

  /**
   * Close the currently selected chrome window, ending the session if it's the last
   * window currently open.
   *
   * On B2G this method is a noop and will return immediately.
   */
  closeChromeWindow: function MDA_closeChromeWindow() {
    let command_id = this.command_id = this.getCommandId();
    if (appName == "B2G") {
      // We can't close windows so just return
      this.sendOk(command_id);
    }
    else {
      // Get the total number of windows
      let numOpenWindows = 0;
      let winEnum = this.getWinEnumerator();
      while (winEnum.hasMoreElements()) {
        numOpenWindows += 1;
        winEnum.getNext();
      }

      // if there is only 1 window left, delete the session
      if (numOpenWindows === 1) {
        try {
          this.sessionTearDown();
        }
        catch (e) {
          this.sendError("Could not clear session", 500,
                         e.name + ": " + e.message, command_id);
          return;
        }
        this.sendOk(command_id);
        return;
      }

      try {
        this.messageManager.removeDelayedFrameScript(FRAME_SCRIPT);
        this.getCurrentWindow().close();
        this.sendOk(command_id);
      }
      catch (e) {
        this.sendError("Could not close window: " + e.message, 13, e.stack,
                       command_id);
      }
    }
  },

  /**
   * Deletes the session.
   *
   * If it is a desktop environment, it will close all listeners
   *
   * If it is a B2G environment, it will make the main content listener sleep, and close
   * all other listeners. The main content listener persists after disconnect (it's the homescreen),
   * and can safely be reused.
   */
  sessionTearDown: function MDA_sessionTearDown() {
    if (this.curBrowser != null) {
      if (appName == "B2G") {
        this.globalMessageManager.broadcastAsyncMessage(
            "Marionette:sleepSession" + this.curBrowser.mainContentId, {});
        this.curBrowser.knownFrames.splice(
            this.curBrowser.knownFrames.indexOf(this.curBrowser.mainContentId), 1);
      }
      else {
        //don't set this pref for B2G since the framescript can be safely reused
        Services.prefs.setBoolPref("marionette.contentListener", false);
      }
      //delete session in each frame in each browser
      for (let win in this.browsers) {
        let browser = this.browsers[win];
        for (let i in browser.knownFrames) {
          this.globalMessageManager.broadcastAsyncMessage("Marionette:deleteSession" + browser.knownFrames[i], {});
        }
      }
      let winEnum = this.getWinEnumerator();
      while (winEnum.hasMoreElements()) {
        winEnum.getNext().messageManager.removeDelayedFrameScript(FRAME_SCRIPT);
      }
      this.curBrowser.frameManager.removeSpecialPowers();
      this.curBrowser.frameManager.removeMessageManagerListeners(this.globalMessageManager);
    }
    this.switchToGlobalMessageManager();
    // reset frame to the top-most frame
    this.curFrame = null;
    if (this.mainFrame) {
      this.mainFrame.focus();
    }
    this.sessionId = null;
    this.deleteFile('marionetteChromeScripts');
    this.deleteFile('marionetteContentScripts');

    if (this.observing !== null) {
      for (let topic in this.observing) {
        Services.obs.removeObserver(this.observing[topic], topic);
      }
      this.observing = null;
    }
  },

  /**
   * Processes the 'deleteSession' request from the client by tearing down
   * the session and responding 'ok'.
   */
  deleteSession: function MDA_deleteSession() {
    let command_id = this.command_id = this.getCommandId();
    try {
      this.sessionTearDown();
    }
    catch (e) {
      this.sendError("Could not delete session", 500, e.name + ": " + e.message, command_id);
      return;
    }
    this.sendOk(command_id);
  },

  /**
   * Quits the application with the provided flags and tears down the
   * current session.
   */
  quitApplication: function MDA_quitApplication (aRequest) {
    let command_id = this.command_id = this.getCommandId();
    if (appName != "Firefox") {
      this.sendError("In app initiated quit only supported on Firefox", 500, null, command_id);
    }

    let flagsArray = aRequest.parameters.flags;
    let flags = Ci.nsIAppStartup.eAttemptQuit;
    for (let k of flagsArray) {
      flags |= Ci.nsIAppStartup[k];
    }

    // Close the listener so we can't re-connect until after the restart.
    this.server.closeListener();
    this.quitFlags = flags;

    // This notifies the client it's safe to begin attempting to reconnect.
    // The actual quit will happen when the current socket connection is closed.
    this.sendOk(command_id);
  },

  /**
   * Returns the current status of the Application Cache
   */
  getAppCacheStatus: function MDA_getAppCacheStatus(aRequest) {
    this.command_id = this.getCommandId();
    this.sendAsync("getAppCacheStatus", {}, this.command_id);
  },

  _emu_cb_id: 0,
  _emu_cbs: null,
  runEmulatorCmd: function runEmulatorCmd(cmd, callback) {
    if (callback) {
      if (!this._emu_cbs) {
        this._emu_cbs = {};
      }
      this._emu_cbs[this._emu_cb_id] = callback;
    }
    this.sendToClient({emulator_cmd: cmd, id: this._emu_cb_id}, -1);
    this._emu_cb_id += 1;
  },

  runEmulatorShell: function runEmulatorShell(args, callback) {
    if (callback) {
      if (!this._emu_cbs) {
        this._emu_cbs = {};
      }
      this._emu_cbs[this._emu_cb_id] = callback;
    }
    this.sendToClient({emulator_shell: args, id: this._emu_cb_id}, -1);
    this._emu_cb_id += 1;
  },

  emulatorCmdResult: function emulatorCmdResult(message) {
    if (this.context != "chrome") {
      this.sendAsync("emulatorCmdResult", message, -1);
      return;
    }

    if (!this._emu_cbs) {
      return;
    }

    let cb = this._emu_cbs[message.id];
    delete this._emu_cbs[message.id];
    if (!cb) {
      return;
    }
    try {
      cb(message.result);
    }
    catch(e) {
      this.sendError(e.message, e.code, e.stack, -1);
      return;
    }
  },

  importScript: function MDA_importScript(aRequest) {
    let command_id = this.command_id = this.getCommandId();
    let converter =
      Components.classes["@mozilla.org/intl/scriptableunicodeconverter"].
          createInstance(Components.interfaces.nsIScriptableUnicodeConverter);
    converter.charset = "UTF-8";
    let result = {};
    let data = converter.convertToByteArray(aRequest.parameters.script, result);
    let ch = Components.classes["@mozilla.org/security/hash;1"]
                       .createInstance(Components.interfaces.nsICryptoHash);
    ch.init(ch.MD5);
    ch.update(data, data.length);
    let hash = ch.finish(true);
    if (this.importedScriptHashes[this.context].indexOf(hash) > -1) {
        //we have already imported this script
        this.sendOk(command_id);
        return;
    }
    this.importedScriptHashes[this.context].push(hash);
    if (this.context == "chrome") {
      let file;
      if (this.importedScripts.exists()) {
        file = FileUtils.openFileOutputStream(this.importedScripts,
            FileUtils.MODE_APPEND | FileUtils.MODE_WRONLY);
      }
      else {
        //Note: The permission bits here don't actually get set (bug 804563)
        this.importedScripts.createUnique(
            Components.interfaces.nsIFile.NORMAL_FILE_TYPE, parseInt("0666", 8));
        file = FileUtils.openFileOutputStream(this.importedScripts,
            FileUtils.MODE_WRONLY | FileUtils.MODE_CREATE);
        this.importedScripts.permissions = parseInt("0666", 8); //actually set permissions
      }
      file.write(aRequest.parameters.script, aRequest.parameters.script.length);
      file.close();
      this.sendOk(command_id);
    }
    else {
      this.sendAsync("importScript",
                     { script: aRequest.parameters.script },
                     command_id);
    }
  },

  clearImportedScripts: function MDA_clearImportedScripts(aRequest) {
    let command_id = this.command_id = this.getCommandId();
    try {
      if (this.context == "chrome") {
        this.deleteFile('marionetteChromeScripts');
      }
      else {
        this.deleteFile('marionetteContentScripts');
      }
    }
    catch (e) {
      this.sendError("Could not clear imported scripts", 500, e.name + ": " + e.message, command_id);
      return;
    }
    this.sendOk(command_id);
  },

  /**
   * Takes a screenshot of a web element, current frame, or viewport.
   *
   * The screen capture is returned as a lossless PNG image encoded as
   * a base 64 string.
   *
   * If called in the content context, the <code>id</code> argument is not null
   * and refers to a present and visible web element's ID, the capture area
   * will be limited to the bounding box of that element. Otherwise, the
   * capture area will be the bounding box of the current frame.
   *
   * If called in the chrome context, the screenshot will always represent the
   * entire viewport.
   *
   * @param {string} [id] Reference to a web element.
   * @param {string} [highlights] List of web elements to highlight.
   * @return {string} PNG image encoded as base 64 string.
   */
  takeScreenshot: function MDA_takeScreenshot(aRequest) {
    this.command_id = this.getCommandId();
    if (this.context == "chrome") {
      var win = this.getCurrentWindow();
      var canvas = win.document.createElementNS("http://www.w3.org/1999/xhtml", "canvas");
      var doc;
      if (appName == "B2G") {
        doc = win.document.body;
      } else {
        doc = win.document.getElementsByTagName('window')[0];
      }
      var docRect = doc.getBoundingClientRect();
      var width = docRect.width;
      var height = docRect.height;

      // Convert width and height from CSS pixels (potentially fractional)
      // to device pixels (integer).
      var scale = win.devicePixelRatio;
      canvas.setAttribute("width", Math.round(width * scale));
      canvas.setAttribute("height", Math.round(height * scale));

      var context = canvas.getContext("2d");
      var flags;
      if (appName == "B2G") {
        flags =
          context.DRAWWINDOW_DRAW_CARET |
          context.DRAWWINDOW_DRAW_VIEW |
          context.DRAWWINDOW_USE_WIDGET_LAYERS;
      } else {
        // Bug 1075168 - CanvasRenderingContext2D image is distorted
        // when using certain flags in chrome context.
        flags =
          context.DRAWWINDOW_DRAW_VIEW |
          context.DRAWWINDOW_USE_WIDGET_LAYERS;
      }
      context.scale(scale, scale);
      context.drawWindow(win, 0, 0, width, height, "rgb(255,255,255)", flags);
      var dataUrl = canvas.toDataURL("image/png", "");
      var data = dataUrl.substring(dataUrl.indexOf(",") + 1);
      this.sendResponse(data, this.command_id);
    }
    else {
      this.sendAsync("takeScreenshot",
                   {id: aRequest.parameters.id,
                    highlights: aRequest.parameters.highlights,
                    full: aRequest.parameters.full},
                   this.command_id);
    }
  },

  /**
   * Get the current browser orientation.
   *
   * Will return one of the valid primary orientation values
   * portrait-primary, landscape-primary, portrait-secondary, or
   * landscape-secondary.
   */
  getScreenOrientation: function MDA_getScreenOrientation(aRequest) {
    this.command_id = this.getCommandId();
    let curWindow = this.getCurrentWindow();
    let or = curWindow.screen.mozOrientation;
    this.sendResponse(or, this.command_id);
  },

  /**
   * Set the current browser orientation.
   *
   * The supplied orientation should be given as one of the valid
   * orientation values.  If the orientation is unknown, an error will
   * be raised.
   *
   * Valid orientations are "portrait" and "landscape", which fall
   * back to "portrait-primary" and "landscape-primary" respectively,
   * and "portrait-secondary" as well as "landscape-secondary".
   */
  setScreenOrientation: function MDA_setScreenOrientation(aRequest) {
    const ors = ["portrait", "landscape",
                 "portrait-primary", "landscape-primary",
                 "portrait-secondary", "landscape-secondary"];

    this.command_id = this.getCommandId();
    let or = String(aRequest.parameters.orientation);

    let mozOr = or.toLowerCase();
    if (ors.indexOf(mozOr) < 0) {
      this.sendError("Unknown screen orientation: " + or, 500, null,
                     this.command_id);
      return;
    }

    let curWindow = this.getCurrentWindow();
    if (!curWindow.screen.mozLockOrientation(mozOr)) {
      this.sendError("Unable to set screen orientation: " + or, 500,
                     null, this.command_id);
    }
    this.sendOk(this.command_id);
  },

  /**
   * Get the size of the browser window currently in focus.
   *
   * Will return the current browser window size in pixels. Refers to
   * window outerWidth and outerHeight values, which include scroll bars,
   * title bars, etc.
   *
   */
  getWindowSize: function MDA_getWindowSize(aRequest) {
    this.command_id = this.getCommandId();
    let curWindow = this.getCurrentWindow();
    let curWidth = curWindow.outerWidth;
    let curHeight = curWindow.outerHeight;
    this.sendResponse({width: curWidth, height: curHeight}, this.command_id);
  },

  /**
   * Set the size of the browser window currently in focus.
   *
   * Not supported on B2G. The supplied width and height values refer to
   * the window outerWidth and outerHeight values, which include scroll
   * bars, title bars, etc.
   *
   * An error will be returned if the requested window size would result
   * in the window being in the maximized state.
   */
  setWindowSize: function MDA_setWindowSize(aRequest) {
    this.command_id = this.getCommandId();

    if (appName !== "Firefox") {
      this.sendError("Not supported on mobile", 405, null, this.command_id);
      return;
    }

    try {
      var width = parseInt(aRequest.parameters.width);
      var height = parseInt(aRequest.parameters.height);
    }
    catch(e) {
      this.sendError(e.message, e.code, e.stack, this.command_id);
      return;
    }

    let curWindow = this.getCurrentWindow();
    if (width >= curWindow.screen.availWidth && height >= curWindow.screen.availHeight) {
      this.sendError("Invalid requested size, cannot maximize", 405, null, this.command_id);
      return;
    }

    curWindow.resizeTo(width, height);
    this.sendOk(this.command_id);
  },

  /**
   * Maximizes the Browser Window as if the user pressed the maximise button
   *
   * Not Supported on B2G or Fennec
   */
  maximizeWindow: function MDA_maximizeWindow (aRequest) {
    this.command_id = this.getCommandId();

    if (appName !== "Firefox") {
      this.sendError("Not supported for mobile", 405, null, this.command_id);
      return;
    }

    let curWindow = this.getCurrentWindow();
    curWindow.moveTo(0,0);
    curWindow.resizeTo(curWindow.screen.availWidth, curWindow.screen.availHeight);
    this.sendOk(this.command_id);
  },

  /**
   * Returns the ChromeWindow associated with an open dialog window if it is
   * currently attached to the dom.
   */
  get activeDialogWindow () {
    if (this._dialogWindowRef !== null) {
      let dialogWin = this._dialogWindowRef.get();
      if (dialogWin && dialogWin.parent) {
        return dialogWin;
      }
    }
    return null;
  },

  get activeDialogUI () {
    let dialogWin = this.activeDialogWindow;
    if (dialogWin) {
      return dialogWin.Dialog.ui;
    }
    return this.curBrowser.getTabModalUI();
  },

  /**
   * Dismisses a currently displayed tab modal, or returns no such alert if
   * no modal is displayed.
   */
  dismissDialog: function MDA_dismissDialog() {
    this.command_id = this.getCommandId();
    if (this.activeDialogUI === null) {
      this.sendError("No tab modal was open when attempting to dismiss the dialog",
                     27, null, this.command_id);
      return;
    }

    let {button0, button1} = this.activeDialogUI;
    (button1 ? button1 : button0).click();
    this.sendOk(this.command_id);
  },

  /**
   * Accepts a currently displayed tab modal, or returns no such alert if
   * no modal is displayed.
   */
  acceptDialog: function MDA_acceptDialog() {
    this.command_id = this.getCommandId();
    if (this.activeDialogUI === null) {
      this.sendError("No tab modal was open when attempting to accept the dialog",
                     27, null, this.command_id);
      return;
    }

    let {button0} = this.activeDialogUI;
    button0.click();
    this.sendOk(this.command_id);
  },

  /**
   * Returns the message shown in a currently displayed modal, or returns a no such
   * alert error if no modal is currently displayed.
   */
  getTextFromDialog: function MDA_getTextFromDialog() {
    this.command_id = this.getCommandId();
    if (this.activeDialogUI === null) {
      this.sendError("No tab modal was open when attempting to get the dialog text",
                     27, null, this.command_id);
      return;
    }

    let {infoBody} = this.activeDialogUI;
    this.sendResponse(infoBody.textContent, this.command_id);
  },

  /**
   * Sends keys to the input field of a currently displayed modal, or returns a
   * no such alert error if no modal is currently displayed. If a tab modal is currently
   * displayed but has no means for text input, an element not visible error is returned.
   */
  sendKeysToDialog: function MDA_sendKeysToDialog(aRequest) {
    this.command_id = this.getCommandId();
    if (this.activeDialogUI === null) {
      this.sendError("No tab modal was open when attempting to send keys to a dialog",
                     27, null, this.command_id);
      return;
    }

    // See toolkit/components/prompts/contentb/commonDialog.js
    let {loginContainer, loginTextbox} = this.activeDialogUI;
    if (loginContainer.hidden) {
      this.sendError("This prompt does not accept text input",
                     11, null, this.command_id);
    }

    let win = this.activeDialogWindow ? this.activeDialogWindow : this.getCurrentWindow();
    utils.sendKeysToElement(win, loginTextbox, aRequest.parameters.value,
                            this.sendOk.bind(this), this.sendError.bind(this),
                            this.command_id, "chrome");
  },

  /**
   * Helper function to convert an outerWindowID into a UID that Marionette
   * tracks.
   */
  generateFrameId: function MDA_generateFrameId(id) {
    let uid = id + (appName == "B2G" ? "-b2g" : "");
    return uid;
  },

  /**
   * Handle a dialog opening by shortcutting the current request to prevent the client
   * from hanging entirely. This is inspired by selenium's mode of dealing with this,
   * but is significantly lighter weight, and may necessitate a different framework
   * for handling this as more features are required.
   */
  handleDialogLoad: function MDA_handleModalLoad(subject, topic) {
    // We shouldn't return to the client due to the modal associated with the
    // jsdebugger.
    let clickToStart;
    try {
      clickToStart = Services.prefs.getBoolPref('marionette.debugging.clicktostart');
    } catch (e) { }
    if (clickToStart) {
      Services.prefs.setBoolPref('marionette.debugging.clicktostart', false);
      return;
    }

    if (topic == "common-dialog-loaded") {
      this._dialogWindowRef = Cu.getWeakReference(subject);
    }

    if (this.command_id) {
      this.sendAsync("cancelRequest", {});
      // This is a shortcut to get the client to accept our response whether
      // the expected key is 'ok' (in case a click or similar got us here)
      // or 'value' (in case an execute script or similar got us here).
      this.sendToClient({from:this.actorID, ok: true, value: null}, this.command_id);
    }
  },

  /**
   * Receives all messages from content messageManager
   */
  receiveMessage: function MDA_receiveMessage(message) {
    // We need to just check if we need to remove the mozbrowserclose listener
    if (this.mozBrowserClose !== null){
      let curWindow = this.getCurrentWindow();
      curWindow.removeEventListener('mozbrowserclose', this.mozBrowserClose, true);
      this.mozBrowserClose = null;
    }

    switch (message.name) {
      case "Marionette:done":
        this.sendResponse(message.json.value, message.json.command_id);
        break;
      case "Marionette:ok":
        this.sendOk(message.json.command_id);
        break;
      case "Marionette:error":
        this.sendError(message.json.message, message.json.status, message.json.stacktrace, message.json.command_id);
        break;
      case "Marionette:log":
        //log server-side messages
        logger.info(message.json.message);
        break;
      case "Marionette:shareData":
        //log messages from tests
        if (message.json.log) {
          this.marionetteLog.addLogs(message.json.log);
        }
        break;
      case "Marionette:runEmulatorCmd":
      case "Marionette:runEmulatorShell":
        this.sendToClient(message.json, -1);
        break;
      case "Marionette:switchToFrame":
        this.oopFrameId = this.curBrowser.frameManager.switchToFrame(message);
        this.messageManager = this.curBrowser.frameManager.currentRemoteFrame.messageManager.get();
        break;
      case "Marionette:switchToModalOrigin":
        this.curBrowser.frameManager.switchToModalOrigin(message);
        this.messageManager = this.curBrowser.frameManager.currentRemoteFrame.messageManager.get();
        break;
      case "Marionette:switchedToFrame":
        logger.info("Switched to frame: " + JSON.stringify(message.json));
        if (message.json.restorePrevious) {
          this.currentFrameElement = this.previousFrameElement;
        }
        else {
          if (message.json.storePrevious) {
            // we don't arbitrarily save previousFrameElement, since
            // we allow frame switching after modals appear, which would
            // override this value and we'd lose our reference
            this.previousFrameElement = this.currentFrameElement;
          }
          this.currentFrameElement = message.json.frameValue;
        }
        break;
      case "Marionette:getVisibleCookies":
        let [currentPath, host] = message.json.value;
        let isForCurrentPath = function(aPath) {
          return currentPath.indexOf(aPath) != -1;
        }
        let results = [];
        let enumerator = cookieManager.enumerator;
        while (enumerator.hasMoreElements()) {
          let cookie = enumerator.getNext().QueryInterface(Ci['nsICookie']);
          // Take the hostname and progressively shorten
          let hostname = host;
          do {
            if ((cookie.host == '.' + hostname || cookie.host == hostname)
                && isForCurrentPath(cookie.path)) {
              results.push({
                'name': cookie.name,
                'value': cookie.value,
                'path': cookie.path,
                'host': cookie.host,
                'secure': cookie.isSecure,
                'expiry': cookie.expires
              });
              break;
            }
            hostname = hostname.replace(/^.*?\./, '');
          } while (hostname.indexOf('.') != -1);
        }
        return results;
      case "Marionette:addCookie":
        let cookieToAdd = message.json.value;
        Services.cookies.add(cookieToAdd.domain, cookieToAdd.path, cookieToAdd.name,
                             cookieToAdd.value, cookieToAdd.secure, false, false,
                             cookieToAdd.expiry);
        return true;
      case "Marionette:deleteCookie":
        let cookieToDelete = message.json.value;
        cookieManager.remove(cookieToDelete.host, cookieToDelete.name,
                             cookieToDelete.path, false);
        return true;
      case "Marionette:register":
        // This code processes the content listener's registration information
        // and either accepts the listener, or ignores it
        let nullPrevious = (this.curBrowser.curFrameId == null);
        let listenerWindow = null;
        try {
          listenerWindow = Services.wm.getOuterWindowWithId(message.json.value);
        } catch (ex) { }

        //go in here if we're already in a remote frame.
        if (this.curBrowser.frameManager.currentRemoteFrame !== null &&
            (!listenerWindow ||
             this.messageManager == this.curBrowser.frameManager.currentRemoteFrame.messageManager.get())) {
          // The outerWindowID from an OOP frame will not be meaningful to
          // the parent process here, since each process maintains its own
          // independent window list.  So, it will either be null (!listenerWindow)
          // if we're already in a remote frame,
          // or it will point to some random window, which will hopefully
          // cause an href mismatch.  Currently this only happens
          // in B2G for OOP frames registered in Marionette:switchToFrame, so
          // we'll acknowledge the switchToFrame message here.
          // XXX: Should have a better way of determining that this message
          // is from a remote frame.
          this.curBrowser.frameManager.currentRemoteFrame.targetFrameId = this.generateFrameId(message.json.value);
          this.sendOk(this.command_id);
        }

        let browserType;
        try {
          browserType = message.target.getAttribute("type");
        } catch (ex) {
          // browserType remains undefined.
        }
        let reg = {};
        // this will be sent to tell the content process if it is the main content
        let mainContent = (this.curBrowser.mainContentId == null);
        if (!browserType || browserType != "content") {
          //curBrowser holds all the registered frames in knownFrames
          let uid = this.generateFrameId(message.json.value);
          reg.id = uid;
          reg.remotenessChange = this.curBrowser.register(uid, message.target);
        }
        // set to true if we updated mainContentId
        mainContent = ((mainContent == true) && (this.curBrowser.mainContentId != null));
        if (mainContent) {
          this.mainContentFrameId = this.curBrowser.curFrameId;
        }
        this.curBrowser.elementManager.seenItems[reg.id] = Cu.getWeakReference(listenerWindow);
        if (nullPrevious && (this.curBrowser.curFrameId != null)) {
          if (!this.sendAsync("newSession",
              { B2G: (appName == "B2G"),
                raisesAccessibilityExceptions:
                  this.sessionCapabilities.raisesAccessibilityExceptions },
              this.newSessionCommandId)) {
            return;
          }
          if (this.curBrowser.newSession) {
            this.getSessionCapabilities();
            this.newSessionCommandId = null;
          }
        }
        if (this.curBrowser.frameRegsPending) {
          if (this.curBrowser.frameRegsPending > 0) {
            this.curBrowser.frameRegsPending -= 1;
          }
          if (this.curBrowser.frameRegsPending === 0) {
            // In case of a freshly registered window, we're responsible here
            // for sending the ack.
            this.sendOk(this.command_id);
          }
        }
        return [reg, mainContent];
      case "Marionette:emitTouchEvent":
        let globalMessageManager = Cc["@mozilla.org/globalmessagemanager;1"]
                             .getService(Ci.nsIMessageBroadcaster);
        globalMessageManager.broadcastAsyncMessage(
          "MarionetteMainListener:emitTouchEvent", message.json);
        return;
      case "Marionette:listenersAttached":
        if (message.json.listenerId === this.curBrowser.curFrameId) {
          // If remoteness gets updated we need to call newSession. In the case
          // of desktop this just sets up a small amount of state that doesn't
          // change over the course of a session.
          let newSessionValues = {
            B2G: (appName == "B2G"),
            raisesAccessibilityExceptions: this.sessionCapabilities.raisesAccessibilityExceptions
          };
          this.sendAsync("newSession", newSessionValues);
          this.curBrowser.flushPendingCommands();
        }
        return;
    }
  }
};

MarionetteServerConnection.prototype.requestTypes = {
  "getMarionetteID": MarionetteServerConnection.prototype.getMarionetteID,
  "sayHello": MarionetteServerConnection.prototype.sayHello,
  "newSession": MarionetteServerConnection.prototype.newSession,
  "getSessionCapabilities": MarionetteServerConnection.prototype.getSessionCapabilities,
  "log": MarionetteServerConnection.prototype.log,
  "getLogs": MarionetteServerConnection.prototype.getLogs,
  "setContext": MarionetteServerConnection.prototype.setContext,
  "getContext": MarionetteServerConnection.prototype.getContext,
  "executeScript": MarionetteServerConnection.prototype.execute,
  "setScriptTimeout": MarionetteServerConnection.prototype.setScriptTimeout,
  "timeouts": MarionetteServerConnection.prototype.timeouts,
  "singleTap": MarionetteServerConnection.prototype.singleTap,
  "actionChain": MarionetteServerConnection.prototype.actionChain,
  "multiAction": MarionetteServerConnection.prototype.multiAction,
  "executeAsyncScript": MarionetteServerConnection.prototype.executeWithCallback,
  "executeJSScript": MarionetteServerConnection.prototype.executeJSScript,
  "setSearchTimeout": MarionetteServerConnection.prototype.setSearchTimeout,
  "findElement": MarionetteServerConnection.prototype.findElement,
  "findChildElement": MarionetteServerConnection.prototype.findChildElements, // Needed for WebDriver compat
  "findElements": MarionetteServerConnection.prototype.findElements,
  "findChildElements":MarionetteServerConnection.prototype.findChildElements, // Needed for WebDriver compat
  "clickElement": MarionetteServerConnection.prototype.clickElement,
  "getElementAttribute": MarionetteServerConnection.prototype.getElementAttribute,
  "getElementText": MarionetteServerConnection.prototype.getElementText,
  "getElementTagName": MarionetteServerConnection.prototype.getElementTagName,
  "isElementDisplayed": MarionetteServerConnection.prototype.isElementDisplayed,
  "getElementValueOfCssProperty": MarionetteServerConnection.prototype.getElementValueOfCssProperty,
  "submitElement": MarionetteServerConnection.prototype.submitElement,
  "getElementSize": MarionetteServerConnection.prototype.getElementSize,  //deprecated
  "getElementRect": MarionetteServerConnection.prototype.getElementRect,
  "isElementEnabled": MarionetteServerConnection.prototype.isElementEnabled,
  "isElementSelected": MarionetteServerConnection.prototype.isElementSelected,
  "sendKeysToElement": MarionetteServerConnection.prototype.sendKeysToElement,
  "getElementLocation": MarionetteServerConnection.prototype.getElementLocation,  // deprecated
  "getElementPosition": MarionetteServerConnection.prototype.getElementLocation,  // deprecated
  "clearElement": MarionetteServerConnection.prototype.clearElement,
  "getTitle": MarionetteServerConnection.prototype.getTitle,
  "getWindowType": MarionetteServerConnection.prototype.getWindowType,
  "getPageSource": MarionetteServerConnection.prototype.getPageSource,
  "get": MarionetteServerConnection.prototype.get,
  "goUrl": MarionetteServerConnection.prototype.get,  // deprecated
  "getCurrentUrl": MarionetteServerConnection.prototype.getCurrentUrl,
  "getUrl": MarionetteServerConnection.prototype.getCurrentUrl,  // deprecated
  "goBack": MarionetteServerConnection.prototype.goBack,
  "goForward": MarionetteServerConnection.prototype.goForward,
  "refresh":  MarionetteServerConnection.prototype.refresh,
  "getWindowHandle": MarionetteServerConnection.prototype.getWindowHandle,
  "getCurrentWindowHandle":  MarionetteServerConnection.prototype.getWindowHandle,  // Selenium 2 compat
  "getChromeWindowHandle": MarionetteServerConnection.prototype.getChromeWindowHandle,
  "getCurrentChromeWindowHandle": MarionetteServerConnection.prototype.getChromeWindowHandle,
  "getWindow":  MarionetteServerConnection.prototype.getWindowHandle,  // deprecated
  "getWindowHandles": MarionetteServerConnection.prototype.getWindowHandles,
  "getChromeWindowHandles": MarionetteServerConnection.prototype.getChromeWindowHandles,
  "getCurrentWindowHandles": MarionetteServerConnection.prototype.getWindowHandles,  // Selenium 2 compat
  "getWindows":  MarionetteServerConnection.prototype.getWindowHandles,  // deprecated
  "getWindowPosition": MarionetteServerConnection.prototype.getWindowPosition,
  "setWindowPosition": MarionetteServerConnection.prototype.setWindowPosition,
  "getActiveFrame": MarionetteServerConnection.prototype.getActiveFrame,
  "switchToFrame": MarionetteServerConnection.prototype.switchToFrame,
  "switchToWindow": MarionetteServerConnection.prototype.switchToWindow,
  "deleteSession": MarionetteServerConnection.prototype.deleteSession,
  "quitApplication": MarionetteServerConnection.prototype.quitApplication,
  "emulatorCmdResult": MarionetteServerConnection.prototype.emulatorCmdResult,
  "importScript": MarionetteServerConnection.prototype.importScript,
  "clearImportedScripts": MarionetteServerConnection.prototype.clearImportedScripts,
  "getAppCacheStatus": MarionetteServerConnection.prototype.getAppCacheStatus,
  "close": MarionetteServerConnection.prototype.close,
  "closeWindow": MarionetteServerConnection.prototype.close,  // deprecated
  "closeChromeWindow": MarionetteServerConnection.prototype.closeChromeWindow,
  "setTestName": MarionetteServerConnection.prototype.setTestName,
  "takeScreenshot": MarionetteServerConnection.prototype.takeScreenshot,
  "screenShot": MarionetteServerConnection.prototype.takeScreenshot,  // deprecated
  "screenshot": MarionetteServerConnection.prototype.takeScreenshot,  // Selenium 2 compat
  "addCookie": MarionetteServerConnection.prototype.addCookie,
  "getCookies": MarionetteServerConnection.prototype.getCookies,
  "getAllCookies": MarionetteServerConnection.prototype.getCookies,  // deprecated
  "deleteAllCookies": MarionetteServerConnection.prototype.deleteAllCookies,
  "deleteCookie": MarionetteServerConnection.prototype.deleteCookie,
  "getActiveElement": MarionetteServerConnection.prototype.getActiveElement,
  "getScreenOrientation": MarionetteServerConnection.prototype.getScreenOrientation,
  "setScreenOrientation": MarionetteServerConnection.prototype.setScreenOrientation,
  "getWindowSize": MarionetteServerConnection.prototype.getWindowSize,
  "setWindowSize": MarionetteServerConnection.prototype.setWindowSize,
  "maximizeWindow": MarionetteServerConnection.prototype.maximizeWindow,
  "dismissDialog": MarionetteServerConnection.prototype.dismissDialog,
  "acceptDialog": MarionetteServerConnection.prototype.acceptDialog,
  "getTextFromDialog": MarionetteServerConnection.prototype.getTextFromDialog,
  "sendKeysToDialog": MarionetteServerConnection.prototype.sendKeysToDialog
};

/**
 * Creates a BrowserObj. BrowserObjs handle interactions with the
 * browser, according to the current environment (desktop, b2g, etc.)
 *
 * @param nsIDOMWindow win
 *        The window whose browser needs to be accessed
 */

function BrowserObj(win, server) {
  this.DESKTOP = "desktop";
  this.B2G = "B2G";
  this.browser;
  this.window = win;
  this.knownFrames = [];
  this.curFrameId = null;
  this.startPage = "about:blank";
  this.mainContentId = null; // used in B2G to identify the homescreen content page
  this.newSession = true; //used to set curFrameId upon new session
  this.elementManager = new ElementManager([NAME, LINK_TEXT, PARTIAL_LINK_TEXT]);
  this.setBrowser(win);
  this.frameManager = new FrameManager(server); //We should have one FM per BO so that we can handle modals in each Browser

  // A reference to the tab corresponding to the current window handle, if any.
  this.tab = null;
  this.pendingCommands = [];

  //register all message listeners
  this.frameManager.addMessageManagerListeners(server.messageManager);
  this.getIdForBrowser = server.getIdForBrowser.bind(server);
  this.updateIdForBrowser = server.updateIdForBrowser.bind(server);
  this._curFrameId = null;
  this._browserWasRemote = null;
  this._hasRemotenessChange = false;
}

BrowserObj.prototype = {

  /**
   * This function intercepts commands interacting with content and queues
   * or executes them as needed.
   *
   * No commands interacting with content are safe to process until
   * the new listener script is loaded and registers itself.
   * This occurs when a command whose effect is asynchronous (such
   * as goBack) results in a remoteness change and new commands
   * are subsequently posted to the server.
   */
  executeWhenReady: function (callback) {
    if (this.hasRemotenessChange()) {
      this.pendingCommands.push(callback);
    } else {
      callback();
    }
  },

  /**
   * Re-sets this BrowserObject's current tab and updates remoteness tracking.
   */
  switchToTab: function (ind) {
    if (this.browser) {
      this.browser.selectTabAtIndex(ind);
      this.tab = this.browser.selectedTab;
    }
    this._browserWasRemote = this.browser.getBrowserForTab(this.tab).isRemoteBrowser;
    this._hasRemotenessChange = false;
  },

  /**
   * Retrieves the current tabmodal ui object. According to the browser associated
   * with the currently selected tab.
   */
  getTabModalUI: function MDA__getTabModaUI () {
    let browserForTab = this.browser.getBrowserForTab(this.tab);
    if (!browserForTab.hasAttribute('tabmodalPromptShowing')) {
      return null;
    }
    // The modal is a direct sibling of the browser element. See tabbrowser.xml's
    // getTabModalPromptBox.
    let modals = browserForTab.parentNode
                              .getElementsByTagNameNS(XUL_NS, 'tabmodalprompt');
    return modals[0].ui;
  },

  /**
   * Set the browser if the application is not B2G
   *
   * @param nsIDOMWindow win
   *        current window reference
   */
  setBrowser: function BO_setBrowser(win) {
    switch (appName) {
      case "Firefox":
        if (!isMulet()) {
          this.browser = win.gBrowser;
        } else {
          // this is Mulet
          appName = "B2G";
        }
        break;
      case "Fennec":
        this.browser = win.BrowserApp;
        break;
    }
  },

  // The current frame id is managed per browser element on desktop in case
  // the id needs to be refreshed. The currently selected window is identified
  // within BrowserObject by a tab.
  get curFrameId () {
    if (appName != "Firefox") {
      return this._curFrameId;
    }
    if (this.tab) {
      let browser = this.browser.getBrowserForTab(this.tab);
      return this.getIdForBrowser(browser);
    }
    return null;
  },

  set curFrameId (id) {
    if (appName != "Firefox") {
      this._curFrameId = id;
    }
  },

  /**
   * Called when we start a session with this browser.
   */
  startSession: function BO_startSession(newSession, win, callback) {
    callback(win, newSession);
  },

  /**
   * Closes current tab
   */
  closeTab: function BO_closeTab() {
    if (this.browser &&
        this.browser.removeTab &&
        this.tab != null && (appName != "B2G")) {
      this.browser.removeTab(this.tab);
    }
  },

  /**
   * Opens a tab with given uri
   *
   * @param string uri
   *      URI to open
   */
  addTab: function BO_addTab(uri) {
    return this.browser.addTab(uri, true);
  },

  /**
   * Registers a new frame, and sets its current frame id to this frame
   * if it is not already assigned, and if a) we already have a session
   * or b) we're starting a new session and it is the right start frame.
   *
   * @param string uid
   *        frame uid for use by marionette
   * @param the XUL <browser> that was the target of the originating message.
   */
  register: function BO_register(uid, target) {
    let remotenessChange = this.hasRemotenessChange();
    if (this.curFrameId === null || remotenessChange) {
      if (this.browser) {
        // If we're setting up a new session on Firefox, we only process the
        // registration for this frame if it belongs to the current tab.
        if (!this.tab) {
          this.switchToTab(this.browser.selectedIndex);
        }

        let browser = this.browser.getBrowserForTab(this.tab);
        if (target == browser) {
          this.updateIdForBrowser(browser, uid);
          this.mainContentId = uid;
        }
      } else {
        this._curFrameId = uid;
        this.mainContentId = uid;
      }
    }

    this.knownFrames.push(uid); //used to delete sessions
    return remotenessChange;
  },

  /**
   * When navigating between pages results in changing a browser's process, we
   * need to take measures not to lose contact with a listener script. This
   * function does the necessary bookkeeping.
   */
  hasRemotenessChange: function () {
    // None of these checks are relevant on b2g or if we don't have a tab yet,
    // and may not apply on Fennec.
    if (appName != "Firefox" || this.tab === null) {
      return false;
    }
    if (this._hasRemotenessChange) {
      return true;
    }
    let currentIsRemote = this.browser.getBrowserForTab(this.tab).isRemoteBrowser;
    this._hasRemotenessChange = this._browserWasRemote !== currentIsRemote;
    this._browserWasRemote = currentIsRemote;
    return this._hasRemotenessChange;
  },

  /**
   * Flushes any pending commands queued when a remoteness change is being
   * processed and mark this remotenessUpdate as complete.
   */
  flushPendingCommands: function () {
    if (!this._hasRemotenessChange) {
      return;
    }

    this._hasRemotenessChange = false;
    this.pendingCommands.forEach((callback) => {
      callback();
    });
    this.pendingCommands = [];
  }

}

/**
 * Marionette server -- this class holds a reference to a socket and creates
 * MarionetteServerConnection objects as needed.
 */
this.MarionetteServer = function MarionetteServer(port, forceLocal) {
  let flags = Ci.nsIServerSocket.KeepWhenOffline;
  if (forceLocal) {
    flags |= Ci.nsIServerSocket.LoopbackOnly;
  }
  let socket = new ServerSocket(port, flags, 0);
  logger.info("Listening on port " + socket.port + "\n");
  socket.asyncListen(this);
  this.listener = socket;
  this.nextConnID = 0;
  this.connections = {};
};

MarionetteServer.prototype = {
  onSocketAccepted: function(serverSocket, clientSocket)
  {
    logger.debug("accepted connection on " + clientSocket.host + ":" + clientSocket.port);

    let input = clientSocket.openInputStream(0, 0, 0);
    let output = clientSocket.openOutputStream(0, 0, 0);
    let aTransport = new DebuggerTransport(input, output);
    let connID = "conn" + this.nextConnID++ + '.';
    let conn = new MarionetteServerConnection(connID, aTransport, this);
    this.connections[connID] = conn;

    // Create a root actor for the connection and send the hello packet.
    conn.sayHello();
    aTransport.ready();
  },

  closeListener: function() {
    this.listener.close();
    this.listener = null;
  },

  _connectionClosed: function DS_connectionClosed(aConnection) {
    delete this.connections[aConnection.prefix];
  }
};

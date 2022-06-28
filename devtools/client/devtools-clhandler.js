/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const { classes: Cc, interfaces: Ci, utils: Cu } = Components;
const kDebuggerPrefs = [
  "devtools.debugger.remote-enabled",
  "devtools.chrome.enabled"
];
Cu.import("resource://gre/modules/XPCOMUtils.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "Services", "resource://gre/modules/Services.jsm");

function devtoolsCommandlineHandler() {
}
devtoolsCommandlineHandler.prototype = {
  handle: function(cmdLine) {
    let consoleFlag = cmdLine.handleFlag("browserconsole", false);
    let debuggerFlag = cmdLine.handleFlag("jsdebugger", false);
    let devtoolsFlag = cmdLine.handleFlag("devtools", false);

    if (consoleFlag) {
      this.handleConsoleFlag(cmdLine);
    }
    if (debuggerFlag) {
      this.handleDebuggerFlag(cmdLine);
    }
    if (devtoolsFlag) {
      this.handleDevToolsFlag();
    }
    let debuggerServerFlag;
    try {
      debuggerServerFlag =
        cmdLine.handleFlagWithParam("start-debugger-server", false);
    } catch(e) {
      // We get an error if the option is given but not followed by a value.
      // By catching and trying again, the value is effectively optional.
      debuggerServerFlag = cmdLine.handleFlag("start-debugger-server", false);
    }
    if (debuggerServerFlag) {
      this.handleDebuggerServerFlag(cmdLine, debuggerServerFlag);
    }
  },

  handleConsoleFlag: function(cmdLine) {
    let window = Services.wm.getMostRecentWindow("devtools:webconsole");
    if (!window) {
      let { require } = Cu.import("resource://gre/modules/devtools/shared/Loader.jsm", {});
      // Load the browser devtools main module as the loader's main module.
      Cu.import("resource:///modules/devtools/client/framework/gDevTools.jsm");
      let hudservice = require("devtools/client/webconsole/hudservice");
      let { console } = Cu.import("resource://gre/modules/devtools/shared/Console.jsm", {});
      hudservice.toggleBrowserConsole().then(null, console.error);
    } else {
      // The Browser Console was already open.
      window.focus();
    }

    if (cmdLine.state == Ci.nsICommandLine.STATE_REMOTE_AUTO) {
      cmdLine.preventDefault = true;
    }
  },

  // Open the toolbox on the selected tab once the browser starts up.
  handleDevToolsFlag: function() {
    Services.obs.addObserver(function onStartup(window) {
      Services.obs.removeObserver(onStartup,
                                  "browser-delayed-startup-finished");
      const {gDevTools} = Cu.import(
          "resource://gre/modules/devtools/gDevTools.jsm", {});
      const {devtools} = Cu.import(
          "resource://gre/modules/devtools/Loader.jsm", {});
      let target = devtools.TargetFactory.forTab(window.gBrowser.selectedTab);
      gDevTools.showToolbox(target);
    }, "browser-delayed-startup-finished", false);
  },

  _isRemoteDebuggingEnabled() {
    let remoteDebuggingEnabled = false;
    try {
      remoteDebuggingEnabled = kDebuggerPrefs.every(pref => {
        return Services.prefs.getBoolPref(pref);
      });
    } catch (e) {
      Cu.reportError(e);
      return false;
    }
    if (!remoteDebuggingEnabled) {
      let errorMsg = "Could not run chrome debugger! You need the following " +
                     "prefs to be set to true: " + kDebuggerPrefs.join(", ");
      Cu.reportError(errorMsg);
      // Dump as well, as we're doing this from a commandline, make sure people
      // don't miss it:
      dump(errorMsg + "\n");
    }
    return remoteDebuggingEnabled;
  },

  handleDebuggerFlag: function(cmdLine) {
    if (!this._isRemoteDebuggingEnabled()) {
      return;
    }
    Cu.import("resource:///modules/devtools/client/framework/ToolboxProcess.jsm");
    BrowserToolboxProcess.init();

    if (cmdLine.state == Ci.nsICommandLine.STATE_REMOTE_AUTO) {
      cmdLine.preventDefault = true;
    }
  },

  handleDebuggerServerFlag: function(cmdLine, portOrPath) {
    if (!this._isRemoteDebuggingEnabled()) {
      return;
    }
    if (portOrPath === true) {
      // Default to TCP port 6000 if no value given
      portOrPath = 6000;
    }
    let { DevToolsLoader } =
      Cu.import("resource://gre/modules/devtools/shared/Loader.jsm", {});

    try {
      // Create a separate loader instance, so that we can be sure to receive
      // a separate instance of the DebuggingServer from the rest of the
      // devtools.  This allows us to safely use the tools against even the
      // actors and DebuggingServer itself, especially since we can mark
      // serverLoader as invisible to the debugger (unlike the usual loader
      // settings).
      let serverLoader = new DevToolsLoader();
      serverLoader.invisibleToDebugger = true;
      serverLoader.main("devtools/server/main");
      let debuggerServer = serverLoader.DebuggerServer;
      debuggerServer.init();
      debuggerServer.addBrowserActors();
      debuggerServer.allowChromeProcess = true;

      let listener = debuggerServer.createListener();
      listener.portOrPath = portOrPath;
      listener.open();
      dump("Started debugger server on " + portOrPath + "\n");
    } catch (e) {
      let _error = "Unable to start debugger server on " + portOrPath + ": "
          + e;
      Cu.reportError(_error);
      dump(_error + "\n");
    }

    if (cmdLine.state == Ci.nsICommandLine.STATE_REMOTE_AUTO) {
      cmdLine.preventDefault = true;
    }
  },

  helpInfo : "  -browserconsole                     Open the Browser Console.\n" +
             "  -jsdebugger                         Open the Browser Toolbox.\n" +
             "  -devtools                           Open DevTools on initial load.\n" +
             "  -start-debugger-server [port|path]  Start the debugger server on a TCP port or Unix domain socket path.\n" +
             "                                      Defaults to TCP port 6000.\n",

  classID: Components.ID("{9e9a9283-0ce9-4e4a-8f1c-ba129a032c32}"),
  QueryInterface: XPCOMUtils.generateQI([Ci.nsICommandLineHandler]),
};

this.NSGetFactory = XPCOMUtils.generateNSGetFactory(
    [devtoolsCommandlineHandler]);

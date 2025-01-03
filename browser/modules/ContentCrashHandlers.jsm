/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

var Cc = Components.classes;
var Ci = Components.interfaces;
var Cu = Components.utils;

this.EXPORTED_SYMBOLS = [ "TabCrashHandler", "PluginCrashReporter" ];

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");

XPCOMUtils.defineLazyModuleGetter(this, "CrashSubmit",
  "resource://gre/modules/CrashSubmit.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "AppConstants",
  "resource://gre/modules/AppConstants.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "RemotePages",
  "resource://gre/modules/RemotePageManager.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "SessionStore",
  "resource:///modules/sessionstore/SessionStore.jsm");

this.TabCrashHandler = {
  _crashedTabCount: 0,

  get prefs() {
    delete this.prefs;
    return this.prefs = Services.prefs.getBranch("browser.tabs.crashReporting.");
  },

  init: function () {
    if (this.initialized)
      return;
    this.initialized = true;

    if (AppConstants.MOZ_CRASHREPORTER) {
      Services.obs.addObserver(this, "ipc:content-shutdown", false);
      Services.obs.addObserver(this, "oop-frameloader-crashed", false);

      this.childMap = new Map();
      this.browserMap = new WeakMap();
    }

    this.pageListener = new RemotePages("about:tabcrashed");
    // LOAD_BACKGROUND pages don't fire load events, so the about:tabcrashed
    // content will fire up its own message when its initial scripts have
    // finished running.
    this.pageListener.addMessageListener("Load", this.receiveMessage.bind(this));
    this.pageListener.addMessageListener("RemotePage:Unload", this.receiveMessage.bind(this));
    this.pageListener.addMessageListener("closeTab", this.receiveMessage.bind(this));
    this.pageListener.addMessageListener("restoreTab", this.receiveMessage.bind(this));
    this.pageListener.addMessageListener("restoreAll", this.receiveMessage.bind(this));
  },

  observe: function (aSubject, aTopic, aData) {
    switch (aTopic) {
      case "ipc:content-shutdown":
        aSubject.QueryInterface(Ci.nsIPropertyBag2);

        if (!aSubject.get("abnormal"))
          return;

        this.childMap.set(aSubject.get("childID"), aSubject.get("dumpID"));
        break;

      case "oop-frameloader-crashed":
        aSubject.QueryInterface(Ci.nsIFrameLoader);

        let browser = aSubject.ownerElement;
        if (!browser)
          return;

        this.browserMap.set(browser.permanentKey, aSubject.childID);
        break;
    }
  },

  receiveMessage: function(message) {
    let browser = message.target.browser;
    let gBrowser = browser.ownerDocument.defaultView.gBrowser;
    let tab = gBrowser.getTabForBrowser(browser);

    switch(message.name) {
      case "Load": {
        this.onAboutTabCrashedLoad(message);
        break;
      }

      case "RemotePage:Unload": {
        this.onAboutTabCrashedUnload(message);
        break;
      }

      case "closeTab": {
        this.maybeSendCrashReport(message);
        gBrowser.removeTab(tab, { animate: true });
        break;
      }

      case "restoreTab": {
        this.maybeSendCrashReport(message);
        SessionStore.reviveCrashedTab(tab);
        break;
      }

      case "restoreAll": {
        this.maybeSendCrashReport(message);
        SessionStore.reviveAllCrashedTabs();
        break;
      }
    }
  },

  /**
   * Submits a crash report from about:tabcrashed, if the crash
   * reporter is enabled and a crash report can be found.
   *
   * @param aBrowser
   *        The <xul:browser> that the report was sent from.
   * @param aFormData
   *        An Object with the following properties:
   *
   *        includeURL (bool):
   *          Whether to include the URL that the user was on
   *          in the crashed tab before the crash occurred.
   *        URL (String)
   *          The URL that the user was on in the crashed tab
   *          before the crash occurred.
   *        emailMe (bool):
   *          Whether or not to include the user's email address
   *          in the crash report.
   *        email (String):
   *          The email address of the user.
   *        comments (String):
   *          Any additional comments from the user.
   *
   *        Note that it is expected that all properties are set,
   *        even if they are empty.
   */
  maybeSendCrashReport(message) {
    if (!AppConstants.MOZ_CRASHREPORTER)
      return;

    let browser = message.target.browser;

    let childID = this.browserMap.get(browser.permanentKey);
    let dumpID = this.childMap.get(childID);
    if (!dumpID)
      return

    if (!message.data.sendReport) {
      Services.telemetry.getHistogramById("FX_CONTENT_CRASH_NOT_SUBMITTED").add(1);
      this.prefs.setBoolPref("sendReport", false);
      return;
    }

    let {
      includeURL,
      comments,
      email,
      emailMe,
      URL,
    } = message.data;

    let extraExtraKeyVals = {
      "Comments": comments,
      "Email": email,
      "URL": URL,
    };

    // For the entries in extraExtraKeyVals, we only want to submit the
    // extra data values where they are not the empty string.
    for (let key in extraExtraKeyVals) {
      let val = extraExtraKeyVals[key].trim();
      if (!val) {
        delete extraExtraKeyVals[key];
      }
    }

    // URL is special, since it's already been written to extra data by
    // default. In order to make sure we don't send it, we overwrite it
    // with the empty string.
    if (!includeURL) {
      extraExtraKeyVals["URL"] = "";
    }

    CrashSubmit.submit(dumpID, {
      recordSubmission: true,
      extraExtraKeyVals,
    }).then(null, Cu.reportError);

    this.prefs.setBoolPref("sendReport", true);
    this.prefs.setBoolPref("includeURL", includeURL);
    this.prefs.setBoolPref("emailMe", emailMe);
    if (emailMe) {
      this.prefs.setCharPref("email", email);
    } else {
      this.prefs.setCharPref("email", "");
    }

    this.childMap.set(childID, null); // Avoid resubmission.
    this.removeSubmitCheckboxesForSameCrash(childID);
  },

  removeSubmitCheckboxesForSameCrash: function(childID) {
    let enumerator = Services.wm.getEnumerator("navigator:browser");
    while (enumerator.hasMoreElements()) {
      let window = enumerator.getNext();
      if (!window.gMultiProcessBrowser)
        continue;

      for (let browser of window.gBrowser.browsers) {
        if (browser.isRemoteBrowser)
          continue;

        let doc = browser.contentDocument;
        if (!doc.documentURI.startsWith("about:tabcrashed"))
          continue;

        if (this.browserMap.get(browser.permanentKey) == childID) {
          this.browserMap.delete(browser.permanentKey);
          let ports = this.pageListener.portsForBrowser(browser);
          if (ports.length) {
            // For about:tabcrashed, we don't expect subframes. We can
            // assume sending to the first port is sufficient.
            ports[0].sendAsyncMessage("CrashReportSent");
          }
        }
      }
    }
  },

  onAboutTabCrashedLoad: function (message) {
    this._crashedTabCount++;

    // Broadcast to all about:tabcrashed pages a count of
    // how many about:tabcrashed pages exist, so that they
    // can decide whether or not to display the "Restore All
    // Crashed Tabs" button.
    this.pageListener.sendAsyncMessage("UpdateCount", {
      count: this._crashedTabCount,
    });

    let browser = message.target.browser;

    let dumpID = this.getDumpID(browser);
    if (!dumpID) {
      // Make sure to only count once even if there are multiple windows
      // that will all show about:tabcrashed.
      if (this._crashedTabCount == 1) {
        Services.telemetry.getHistogramById("FX_CONTENT_CRASH_DUMP_UNAVAILABLE").add(1);
      }

      message.target.sendAsyncMessage("SetCrashReportAvailable", {
        hasReport: false,
      });
      return;
    }

    let sendReport = this.prefs.getBoolPref("sendReport");
    let includeURL = this.prefs.getBoolPref("includeURL");
    let emailMe = this.prefs.getBoolPref("emailMe");

    let data = { hasReport: true, sendReport, includeURL, emailMe };
    if (emailMe) {
      data.email = this.prefs.getCharPref("email", "");
    }

    // Make sure to only count once even if there are multiple windows
    // that will all show about:tabcrashed.
    if (this._crashedTabCount == 1) {
      Services.telemetry.getHistogramById("FX_CONTENT_CRASH_PRESENTED").add(1);
    }

    message.target.sendAsyncMessage("SetCrashReportAvailable", data);
  },

  onAboutTabCrashedUnload(message) {
    if (!this._crashedTabCount) {
      Cu.reportError("Can not decrement crashed tab count to below 0");
      return;
    }
    this._crashedTabCount--;

    // Broadcast to all about:tabcrashed pages a count of
    // how many about:tabcrashed pages exist, so that they
    // can decide whether or not to display the "Restore All
    // Crashed Tabs" button.
    this.pageListener.sendAsyncMessage("UpdateCount", {
      count: this._crashedTabCount,
    });

    let browser = message.target.browser;
    let childID = this.browserMap.get(browser.permanentKey);

    // Make sure to only count once even if there are multiple windows
    // that will all show about:tabcrashed.
    if (this._crashedTabCount == 0 && childID) {
      Services.telemetry.getHistogramById("FX_CONTENT_CRASH_NOT_SUBMITTED").add(1);
    }
},

  /**
   * For some <xul:browser>, return a crash report dump ID for that browser
   * if we have been informed of one. Otherwise, return null.
   *
   * @param browser (<xul:browser)
   *        The browser to try to get the dump ID for
   * @returns dumpID (String)
   */
  getDumpID(browser) {
    if (!this.childMap) {
      return null;
    }

    return this.childMap.get(this.browserMap.get(browser.permanentKey));
  },
}

this.PluginCrashReporter = {
  /**
   * Makes the PluginCrashReporter ready to hear about and
   * submit crash reports.
   */
  init() {
    if (this.initialized) {
      return;
    }

    this.initialized = true;
    this.crashReports = new Map();

    Services.obs.addObserver(this, "plugin-crashed", false);
    Services.obs.addObserver(this, "gmp-plugin-crash", false);
    Services.obs.addObserver(this, "profile-after-change", false);
  },

  uninit() {
    Services.obs.removeObserver(this, "plugin-crashed", false);
    Services.obs.removeObserver(this, "gmp-plugin-crash", false);
    Services.obs.removeObserver(this, "profile-after-change", false);
    this.initialized = false;
  },

  observe(subject, topic, data) {
    switch(topic) {
      case "plugin-crashed": {
        let propertyBag = subject;
        if (!(propertyBag instanceof Ci.nsIPropertyBag2) ||
            !(propertyBag instanceof Ci.nsIWritablePropertyBag2) ||
            !propertyBag.hasKey("runID") ||
            !propertyBag.hasKey("pluginDumpID")) {
          Cu.reportError("PluginCrashReporter can not read plugin information.");
          return;
        }

        let runID = propertyBag.getPropertyAsUint32("runID");
        let pluginDumpID = propertyBag.getPropertyAsAString("pluginDumpID");
        let browserDumpID = propertyBag.getPropertyAsAString("browserDumpID");
        if (pluginDumpID) {
          this.crashReports.set(runID, { pluginDumpID, browserDumpID });
        }
        break;
      }
      case "gmp-plugin-crash": {
        let propertyBag = subject;
        if (!(propertyBag instanceof Ci.nsIWritablePropertyBag2) ||
            !propertyBag.hasKey("pluginID") ||
            !propertyBag.hasKey("pluginDumpID") ||
            !propertyBag.hasKey("pluginName")) {
          Cu.reportError("PluginCrashReporter can not read plugin information.");
          return;
        }

        let pluginID = propertyBag.getPropertyAsUint32("pluginID");
        let pluginDumpID = propertyBag.getPropertyAsAString("pluginDumpID");
        if (pluginDumpID) {
          this.crashReports.set(pluginID, { pluginDumpID });
        }

        // Only the parent process gets the gmp-plugin-crash observer
        // notification, so we need to inform any content processes that
        // the GMP has crashed.
        if (Cc["@mozilla.org/parentprocessmessagemanager;1"]) {
          let pluginName = propertyBag.getPropertyAsAString("pluginName");
          let mm = Cc["@mozilla.org/parentprocessmessagemanager;1"]
            .getService(Ci.nsIMessageListenerManager);
          mm.broadcastAsyncMessage("gmp-plugin-crash",
                                   { pluginName, pluginID });
        }
        break;
      }
      case "profile-after-change":
        this.uninit();
        break;
    }
  },

  /**
   * Submit a crash report for a crashed NPAPI plugin.
   *
   * @param runID
   *        The runID of the plugin that crashed. A run ID is a unique
   *        identifier for a particular run of a plugin process - and is
   *        analogous to a process ID (though it is managed by Gecko instead
   *        of the operating system).
   * @param keyVals
   *        An object whose key-value pairs will be merged
   *        with the ".extra" file submitted with the report.
   *        The properties of htis object will override properties
   *        of the same name in the .extra file.
   */
  submitCrashReport(runID, keyVals) {
    if (!this.crashReports.has(runID)) {
      Cu.reportError(`Could not find plugin dump IDs for run ID ${runID}.` +
                     `It is possible that a report was already submitted.`);
      return;
    }

    keyVals = keyVals || {};
    let { pluginDumpID, browserDumpID } = this.crashReports.get(runID);

    let submissionPromise = CrashSubmit.submit(pluginDumpID, {
      recordSubmission: true,
      extraExtraKeyVals: keyVals,
    });

    if (browserDumpID)
      CrashSubmit.submit(browserDumpID);

    this.broadcastState(runID, "submitting");

    submissionPromise.then(() => {
      this.broadcastState(runID, "success");
    }, () => {
      this.broadcastState(runID, "failed");
    });

    this.crashReports.delete(runID);
  },

  broadcastState(runID, state) {
    let enumerator = Services.wm.getEnumerator("navigator:browser");
    while (enumerator.hasMoreElements()) {
      let window = enumerator.getNext();
      let mm = window.messageManager;
      mm.broadcastAsyncMessage("BrowserPlugins:CrashReportSubmitted",
                               { runID, state });
    }
  },

  hasCrashReport(runID) {
    return this.crashReports.has(runID);
  },
};

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

function debug(msg) {
  Services.console.logStringMessage("SessionStoreContent: " + msg);
}

var Cu = Components.utils;
var Cc = Components.classes;
var Ci = Components.interfaces;
var Cr = Components.results;

Cu.import("resource://gre/modules/XPCOMUtils.jsm", this);
Cu.import("resource://gre/modules/Timer.jsm", this);

XPCOMUtils.defineLazyModuleGetter(this, "FormData",
  "resource://gre/modules/FormData.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "Preferences",
  "resource://gre/modules/Preferences.jsm");

XPCOMUtils.defineLazyModuleGetter(this, "DocShellCapabilities",
  "resource:///modules/sessionstore/DocShellCapabilities.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "PageStyle",
  "resource:///modules/sessionstore/PageStyle.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "ScrollPosition",
  "resource://gre/modules/ScrollPosition.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "SessionHistory",
  "resource:///modules/sessionstore/SessionHistory.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "SessionStorage",
  "resource:///modules/sessionstore/SessionStorage.jsm");

Cu.import("resource:///modules/sessionstore/FrameTree.jsm", this);
var gFrameTree = new FrameTree(this);

Cu.import("resource:///modules/sessionstore/ContentRestore.jsm", this);
XPCOMUtils.defineLazyGetter(this, 'gContentRestore',
                            () => { return new ContentRestore(this) });

// The current epoch.
var gCurrentEpoch = 0;

// A bound to the size of data to store for DOM Storage.
const DOM_STORAGE_MAX_CHARS = 10000000; // 10M characters

// This pref controls whether or not we send updates to the parent on a timeout
// or not, and should only be used for tests or debugging.
const TIMEOUT_DISABLED_PREF = "browser.sessionstore.debug.no_auto_updates";

/**
 * Returns a lazy function that will evaluate the given
 * function |fn| only once and cache its return value.
 */
function createLazy(fn) {
  let cached = false;
  let cachedValue = null;

  return function lazy() {
    if (!cached) {
      cachedValue = fn();
      cached = true;
    }

    return cachedValue;
  };
}

/**
 * Listens for and handles content events that we need for the
 * session store service to be notified of state changes in content.
 */
var EventListener = {

  init: function () {
    addEventListener("load", this, true);
  },

  handleEvent: function (event) {
    // Ignore load events from subframes.
    if (event.target != content.document) {
      return;
    }

    // Restore the form data and scroll position. If we're not currently
    // restoring a tab state then this call will simply be a noop.
    gContentRestore.restoreDocument();
  }
};

/**
 * Listens for and handles messages sent by the session store service.
 */
var MessageListener = {

  MESSAGES: [
    "SessionStore:restoreHistory",
    "SessionStore:restoreTabContent",
    "SessionStore:resetRestore",
    "SessionStore:flush",
  ],

  init: function () {
    this.MESSAGES.forEach(m => addMessageListener(m, this));
  },

  receiveMessage: function ({name, data}) {
    // The docShell might be gone. Don't process messages,
    // that will just lead to errors anyway.
    if (!docShell) {
      return;
    }

    // A fresh tab always starts with epoch=0. The parent has the ability to
    // override that to signal a new era in this tab's life. This enables it
    // to ignore async messages that were already sent but not yet received
    // and would otherwise confuse the internal tab state.
    if (data.epoch && data.epoch != gCurrentEpoch) {
      gCurrentEpoch = data.epoch;
    }

    switch (name) {
      case "SessionStore:restoreHistory":
        this.restoreHistory(data);
        break;
      case "SessionStore:restoreTabContent":
        this.restoreTabContent(data);
        break;
      case "SessionStore:resetRestore":
        gContentRestore.resetRestore();
        break;
      case "SessionStore:flush":
        this.flush(data);
        break;
      default:
        debug("received unknown message '" + name + "'");
        break;
    }
  },

  restoreHistory({epoch, tabData, loadArguments}) {
    gContentRestore.restoreHistory(tabData, loadArguments, {
      // Note: The callbacks passed here will only be used when a load starts
      // that was not initiated by sessionstore itself. This can happen when
      // some code calls browser.loadURI() or browser.reload() on a pending
      // browser/tab.

      onLoadStarted() {
        // Notify the parent that the tab is no longer pending.
        sendSyncMessage("SessionStore:restoreTabContentStarted", {epoch});
      },

      onLoadFinished() {
        // Tell SessionStore.jsm that it may want to restore some more tabs,
        // since it restores a max of MAX_CONCURRENT_TAB_RESTORES at a time.
        sendAsyncMessage("SessionStore:restoreTabContentComplete", {epoch});
      }
    });

    // When restoreHistory finishes, we send a synchronous message to
    // SessionStore.jsm so that it can run SSTabRestoring. Users of
    // SSTabRestoring seem to get confused if chrome and content are out of
    // sync about the state of the restore (particularly regarding
    // docShell.currentURI). Using a synchronous message is the easiest way
    // to temporarily synchronize them.
    sendSyncMessage("SessionStore:restoreHistoryComplete", {epoch});
  },

  restoreTabContent({loadArguments, isRemotenessUpdate}) {
    let epoch = gCurrentEpoch;

    // We need to pass the value of didStartLoad back to SessionStore.jsm.
    let didStartLoad = gContentRestore.restoreTabContent(loadArguments, () => {
      // Tell SessionStore.jsm that it may want to restore some more tabs,
      // since it restores a max of MAX_CONCURRENT_TAB_RESTORES at a time.
      sendAsyncMessage("SessionStore:restoreTabContentComplete", {epoch, isRemotenessUpdate});
    });

    sendAsyncMessage("SessionStore:restoreTabContentStarted", {epoch});

    if (!didStartLoad) {
      // Pretend that the load succeeded so that event handlers fire correctly.
      sendAsyncMessage("SessionStore:restoreTabContentComplete", {epoch, isRemotenessUpdate});
    }
  },

  flush({id}) {
    // Flush the message queue, send the latest updates.
    MessageQueue.send({flushID: id});
  }
};

/**
 * Listens for changes to the session history. Whenever the user navigates
 * we will collect URLs and everything belonging to session history.
 *
 * Causes a SessionStore:update message to be sent that contains the current
 * session history.
 *
 * Example:
 *   {entries: [{url: "about:mozilla", ...}, ...], index: 1}
 */
var SessionHistoryListener = {
  init: function () {
    // The frame tree observer is needed to handle initial subframe loads.
    // It will redundantly invalidate with the SHistoryListener in some cases
    // but these invalidations are very cheap.
    gFrameTree.addObserver(this);

    // By adding the SHistoryListener immediately, we will unfortunately be
    // notified of every history entry as the tab is restored. We don't bother
    // waiting to add the listener later because these notifications are cheap.
    // We will likely only collect once since we are batching collection on
    // a delay.
    docShell.QueryInterface(Ci.nsIWebNavigation).sessionHistory.
      addSHistoryListener(this);

    // Collect data if we start with a non-empty shistory.
    if (!SessionHistory.isEmpty(docShell)) {
      this.collect();
    }

    // Listen for page title changes.
    addEventListener("DOMTitleChanged", this);
  },

  uninit: function () {
    let sessionHistory = docShell.QueryInterface(Ci.nsIWebNavigation).sessionHistory;
    if (sessionHistory) {
      sessionHistory.removeSHistoryListener(this);
    }
  },

  collect: function () {
    if (docShell) {
      MessageQueue.push("history", () => SessionHistory.collect(docShell));
    }
  },

  handleEvent(event) {
    this.collect();
  },

  onFrameTreeCollected: function () {
    this.collect();
  },

  onFrameTreeReset: function () {
    this.collect();
  },

  OnHistoryNewEntry: function (newURI) {
    this.collect();
  },

  OnHistoryGoBack: function (backURI) {
    this.collect();
    return true;
  },

  OnHistoryGoForward: function (forwardURI) {
    this.collect();
    return true;
  },

  OnHistoryGotoIndex: function (index, gotoURI) {
    this.collect();
    return true;
  },

  OnHistoryPurge: function (numEntries) {
    this.collect();
    return true;
  },

  OnHistoryReload: function (reloadURI, reloadFlags) {
    this.collect();
    return true;
  },

  OnHistoryReplaceEntry: function (index) {
    this.collect();
  },

  QueryInterface: XPCOMUtils.generateQI([
    Ci.nsISHistoryListener,
    Ci.nsISupportsWeakReference
  ])
};

/**
 * Listens for scroll position changes. Whenever the user scrolls the top-most
 * frame we update the scroll position and will restore it when requested.
 *
 * Causes a SessionStore:update message to be sent that contains the current
 * scroll positions as a tree of strings. If no frame of the whole frame tree
 * is scrolled this will return null so that we don't tack a property onto
 * the tabData object in the parent process.
 *
 * Example:
 *   {scroll: "100,100", children: [null, null, {scroll: "200,200"}]}
 */
var ScrollPositionListener = {
  init: function () {
    addEventListener("scroll", this);
    gFrameTree.addObserver(this);
  },

  handleEvent: function (event) {
    let frame = event.target && event.target.defaultView;

    // Don't collect scroll data for frames created at or after the load event
    // as SessionStore can't restore scroll data for those.
    if (frame && gFrameTree.contains(frame)) {
      MessageQueue.push("scroll", () => this.collect());
    }
  },

  onFrameTreeCollected: function () {
    MessageQueue.push("scroll", () => this.collect());
  },

  onFrameTreeReset: function () {
    MessageQueue.push("scroll", () => null);
  },

  collect: function () {
    return gFrameTree.map(ScrollPosition.collect);
  }
};

/**
 * Listens for changes to input elements. Whenever the value of an input
 * element changes we will re-collect data for the current frame tree and send
 * a message to the parent process.
 *
 * Causes a SessionStore:update message to be sent that contains the form data
 * for all reachable frames.
 *
 * Example:
 *   {
 *     formdata: {url: "http://mozilla.org/", id: {input_id: "input value"}},
 *     children: [
 *       null,
 *       {url: "http://sub.mozilla.org/", id: {input_id: "input value 2"}}
 *     ]
 *   }
 */
var FormDataListener = {
  init: function () {
    addEventListener("input", this, true);
    addEventListener("change", this, true);
    gFrameTree.addObserver(this);
  },

  handleEvent: function (event) {
    let frame = event.target &&
                event.target.ownerDocument &&
                event.target.ownerDocument.defaultView;

    // Don't collect form data for frames created at or after the load event
    // as SessionStore can't restore form data for those.
    if (frame && gFrameTree.contains(frame)) {
      MessageQueue.push("formdata", () => this.collect());
    }
  },

  onFrameTreeReset: function () {
    MessageQueue.push("formdata", () => null);
  },

  collect: function () {
    return gFrameTree.map(FormData.collect);
  }
};

/**
 * Listens for changes to the page style. Whenever a different page style is
 * selected or author styles are enabled/disabled we send a message with the
 * currently applied style to the chrome process.
 *
 * Causes a SessionStore:update message to be sent that contains the currently
 * selected pageStyle for all reachable frames.
 *
 * Example:
 *   {pageStyle: "Dusk", children: [null, {pageStyle: "Mozilla"}]}
 */
var PageStyleListener = {
  init: function () {
    Services.obs.addObserver(this, "author-style-disabled-changed", false);
    Services.obs.addObserver(this, "style-sheet-applicable-state-changed", false);
    gFrameTree.addObserver(this);
  },

  uninit: function () {
    Services.obs.removeObserver(this, "author-style-disabled-changed");
    Services.obs.removeObserver(this, "style-sheet-applicable-state-changed");
  },

  observe: function (subject, topic) {
    let frame = subject.defaultView;

    if (frame && gFrameTree.contains(frame)) {
      MessageQueue.push("pageStyle", () => this.collect());
    }
  },

  collect: function () {
    return PageStyle.collect(docShell, gFrameTree);
  },

  onFrameTreeCollected: function () {
    MessageQueue.push("pageStyle", () => this.collect());
  },

  onFrameTreeReset: function () {
    MessageQueue.push("pageStyle", () => null);
  }
};

/**
 * Listens for changes to docShell capabilities. Whenever a new load is started
 * we need to re-check the list of capabilities and send message when it has
 * changed.
 *
 * Causes a SessionStore:update message to be sent that contains the currently
 * disabled docShell capabilities (all nsIDocShell.allow* properties set to
 * false) as a string - i.e. capability names separate by commas.
 */
var DocShellCapabilitiesListener = {
  /**
   * This field is used to compare the last docShell capabilities to the ones
   * that have just been collected. If nothing changed we won't send a message.
   */
  _latestCapabilities: "",

  init: function () {
    gFrameTree.addObserver(this);
  },

  /**
   * onFrameTreeReset() is called as soon as we start loading a page.
   */
  onFrameTreeReset: function() {
    // The order of docShell capabilities cannot change while we're running
    // so calling join() without sorting before is totally sufficient.
    let caps = DocShellCapabilities.collect(docShell).join(",");

    // Send new data only when the capability list changes.
    if (caps != this._latestCapabilities) {
      this._latestCapabilities = caps;
      MessageQueue.push("disallow", () => caps || null);
    }
  }
};

/**
 * Listens for changes to the DOMSessionStorage. Whenever new keys are added,
 * existing ones removed or changed, or the storage is cleared we will send a
 * message to the parent process containing up-to-date sessionStorage data.
 *
 * Causes a SessionStore:update message to be sent that contains the current
 * DOMSessionStorage contents. The data is a nested object using host names
 * as keys and per-host DOMSessionStorage data as values.
 */
var SessionStorageListener = {
  init: function () {
    addEventListener("MozSessionStorageChanged", this, true);
    Services.obs.addObserver(this, "browser:purge-domain-data", false);
    gFrameTree.addObserver(this);
  },

  uninit: function () {
    Services.obs.removeObserver(this, "browser:purge-domain-data");
  },

  handleEvent: function (event) {
    if (gFrameTree.contains(event.target)) {
      this.collect();
    }
  },

  observe: function () {
    // Collect data on the next tick so that any other observer
    // that needs to purge data can do its work first.
    setTimeout(() => this.collect(), 0);
  },

  // Before DOM Storage can be written to disk, it needs to be serialized
  // for sending across frames/processes, then again to be sent across
  // threads, then again to be put in a buffer for the disk. Each of these
  // serializations is an opportunity to OOM and (depending on the site of
  // the OOM), either crash, lose all data for the frame or lose all data
  // for the application.
  //
  // In order to avoid this, compute an estimate of the size of the
  // object, and block SessionStorage items that are too large. As
  // we also don't want to cause an OOM here, we use a quick and memory-
  // efficient approximation: we compute the total sum of string lengths
  // involved in this object.
  estimateStorageSize: function(collected) {
    if (!collected) {
      return 0;
    }

    let size = 0;
    for (let host of Object.keys(collected)) {
      size += host.length;
      let perHost = collected[host];
      for (let key of Object.keys(perHost)) {
        size += key.length;
        let perKey = perHost[key];
        size += perKey.length;
      }
    }

    return size;
  },

  collect: function () {
    if (docShell) {
      MessageQueue.push("storage", () => {
        let collected = SessionStorage.collect(docShell, gFrameTree);

        if (collected == null) {
          return collected;
        }

        let size = this.estimateStorageSize(collected);

        MessageQueue.push("telemetry", () => ({ FX_SESSION_RESTORE_DOM_STORAGE_SIZE_ESTIMATE_CHARS: size }));
        if (size > Preferences.get("browser.sessionstore.dom_storage_limit", DOM_STORAGE_MAX_CHARS)) {
          // Rather than keeping the old storage, which wouldn't match the rest
          // of the state of the page, empty the storage. DOM storage will be
          // recollected the next time and stored if it is now small enough.
          return {};
        }

        return collected;
      });
    }
  },

  onFrameTreeCollected: function () {
    this.collect();
  },

  onFrameTreeReset: function () {
    this.collect();
  }
};

/**
 * Listen for changes to the privacy status of the tab.
 * By definition, tabs start in non-private mode.
 *
 * Causes a SessionStore:update message to be sent for
 * field "isPrivate". This message contains
 *  |true| if the tab is now private
 *  |null| if the tab is now public - the field is therefore
 *  not saved.
 */
var PrivacyListener = {
  init: function() {
    docShell.addWeakPrivacyTransitionObserver(this);

    // Check that value at startup as it might have
    // been set before the frame script was loaded.
    if (docShell.QueryInterface(Ci.nsILoadContext).usePrivateBrowsing) {
      MessageQueue.push("isPrivate", () => true);
    }
  },

  // Ci.nsIPrivacyTransitionObserver
  privateModeChanged: function(enabled) {
    MessageQueue.push("isPrivate", () => enabled || null);
  },

  QueryInterface: XPCOMUtils.generateQI([Ci.nsIPrivacyTransitionObserver,
                                         Ci.nsISupportsWeakReference])
};

/**
 * A message queue that takes collected data and will take care of sending it
 * to the chrome process. It allows flushing using synchronous messages and
 * takes care of any race conditions that might occur because of that. Changes
 * will be batched if they're pushed in quick succession to avoid a message
 * flood.
 */
var MessageQueue = {
  /**
   * A map (string -> lazy fn) holding lazy closures of all queued data
   * collection routines. These functions will return data collected from the
   * docShell.
   */
  _data: new Map(),

  /**
   * The delay (in ms) used to delay sending changes after data has been
   * invalidated.
   */
  BATCH_DELAY_MS: 1000,

  /**
   * The current timeout ID, null if there is no queue data. We use timeouts
   * to damp a flood of data changes and send lots of changes as one batch.
   */
  _timeout: null,

  /**
   * Whether or not sending batched messages on a timer is disabled. This should
   * only be used for debugging or testing. If you need to access this value,
   * you should probably use the timeoutDisabled getter.
   */
  _timeoutDisabled: false,

  /**
   * True if batched messages are not being fired on a timer. This should only
   * ever be true when debugging or during tests.
   */
  get timeoutDisabled() {
    return this._timeoutDisabled;
  },

  /**
   * Disables sending batched messages on a timer. Also cancels any pending
   * timers.
   */
  set timeoutDisabled(val) {
    this._timeoutDisabled = val;

    if (val && this._timeout) {
      clearTimeout(this._timeout);
      this._timeout = null;
    }

    return val;
  },

  init() {
    this.timeoutDisabled =
      Services.prefs.getBoolPref(TIMEOUT_DISABLED_PREF);

    Services.prefs.addObserver(TIMEOUT_DISABLED_PREF, this, false);
  },

  uninit() {
    Services.prefs.removeObserver(TIMEOUT_DISABLED_PREF, this);
  },

  observe(subject, topic, data) {
    if (topic == "nsPref:changed" && data == TIMEOUT_DISABLED_PREF) {
      this.timeoutDisabled =
        Services.prefs.getBoolPref(TIMEOUT_DISABLED_PREF);
    }
  },

  /**
   * Pushes a given |value| onto the queue. The given |key| represents the type
   * of data that is stored and can override data that has been queued before
   * but has not been sent to the parent process, yet.
   *
   * @param key (string)
   *        A unique identifier specific to the type of data this is passed.
   * @param fn (function)
   *        A function that returns the value that will be sent to the parent
   *        process.
   */
  push: function (key, fn) {
    this._data.set(key, createLazy(fn));

    if (!this._timeout && !this._timeoutDisabled) {
      // Wait a little before sending the message to batch multiple changes.
      this._timeout = setTimeout(() => this.send(), this.BATCH_DELAY_MS);
    }
  },

  /**
   * Sends queued data to the chrome process.
   *
   * @param options (object)
   *        {flushID: 123} to specify that this is a flush
   *        {isFinal: true} to signal this is the final message sent on unload
   */
  send: function (options = {}) {
    // Looks like we have been called off a timeout after the tab has been
    // closed. The docShell is gone now and we can just return here as there
    // is nothing to do.
    if (!docShell) {
      return;
    }

    if (this._timeout) {
      clearTimeout(this._timeout);
      this._timeout = null;
    }

    let flushID = (options && options.flushID) || 0;

    let durationMs = Date.now();

    let data = {};
    let telemetry = {};
    for (let [key, func] of this._data) {
      let value = func();
      if (key == "telemetry") {
        for (let histogramId of Object.keys(value)) {
          telemetry[histogramId] = value[histogramId];
        }
      } else {
        data[key] = value;
      }
    }

    durationMs = Date.now() - durationMs;
    telemetry.FX_SESSION_RESTORE_CONTENT_COLLECT_DATA_LONGEST_OP_MS = durationMs;

    try {
      // Send all data to the parent process.
      sendAsyncMessage("SessionStore:update", {
        data, telemetry, flushID,
        isFinal: options.isFinal || false,
        epoch: gCurrentEpoch
      });
    } catch (ex if ex && ex.result == Cr.NS_ERROR_OUT_OF_MEMORY) {
      let telemetry = {
        FX_SESSION_RESTORE_SEND_UPDATE_CAUSED_OOM: 1
      };
      sendAsyncMessage("SessionStore:error", {
        telemetry
      });
    }
  },
};

EventListener.init();
MessageListener.init();
FormDataListener.init();
PageStyleListener.init();
SessionHistoryListener.init();
SessionStorageListener.init();
ScrollPositionListener.init();
DocShellCapabilitiesListener.init();
PrivacyListener.init();
MessageQueue.init();

function handleRevivedTab() {
  if (!content) {
    removeEventListener("pagehide", handleRevivedTab);
    return;
  }

  if (content.document.documentURI.startsWith("about:tabcrashed")) {
    if (Services.appinfo.processType != Services.appinfo.PROCESS_TYPE_DEFAULT) {
      // Sanity check - we'd better be loading this in a non-remote browser.
      throw new Error("We seem to be navigating away from about:tabcrashed in " +
                      "a non-remote browser. This should really never happen.");
    }

    removeEventListener("pagehide", handleRevivedTab);

    // Notify the parent.
    sendAsyncMessage("SessionStore:crashedTabRevived");
  }
}

// If we're browsing from the tab crashed UI to a blacklisted URI that keeps
// this browser non-remote, we'll handle that in a pagehide event.
addEventListener("pagehide", handleRevivedTab);

addEventListener("unload", () => {
  // Upon frameLoader destruction, send a final update message to
  // the parent and flush all data currently held in the child.
  MessageQueue.send({isFinal: true});

  // If we're browsing from the tab crashed UI to a URI that causes the tab
  // to go remote again, we catch this in the unload event handler, because
  // swapping out the non-remote browser for a remote one in
  // tabbrowser.xml's updateBrowserRemoteness doesn't cause the pagehide
  // event to be fired.
  handleRevivedTab();

  // Remove all registered nsIObservers.
  PageStyleListener.uninit();
  SessionStorageListener.uninit();
  SessionHistoryListener.uninit();
  MessageQueue.uninit();

  // Remove progress listeners.
  gContentRestore.resetRestore();

  // We don't need to take care of any gFrameTree observers as the gFrameTree
  // will die with the content script. The same goes for the privacy transition
  // observer that will die with the docShell when the tab is closed.
});

Components.utils.import("resource://gre/modules/XPCOMUtils.jsm");

XPCOMUtils.defineLazyModuleGetter(this, "Promise",
  "resource://gre/modules/Promise.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "Task",
  "resource://gre/modules/Task.jsm");

// This url must sync with the table, url in SafeBrowsing.jsm addMozEntries
const PHISH_TABLE = "test-phish-simple";
const PHISH_URL = "https://www.itisatrap.org/firefox/its-a-trap.html";

/**
 * Waits for a load (or custom) event to finish in a given tab. If provided
 * load an uri into the tab.
 *
 * @param tab
 *        The tab to load into.
 * @param [optional] url
 *        The url to load, or the current url.
 * @param [optional] event
 *        The load event type to wait for.  Defaults to "load".
 * @return {Promise} resolved when the event is handled.
 * @resolves to the received event
 * @rejects if a valid load event is not received within a meaningful interval
 */
function promiseTabLoadEvent(tab, url, eventType = "load")
{
  let deferred = Promise.defer();
  info("Wait tab event: " + eventType);

  function handle(event) {
    if (event.originalTarget != tab.linkedBrowser.contentDocument ||
        event.target.location.href == "about:blank" ||
        (url && event.target.location.href != url)) {
      info("Skipping spurious '" + eventType + "'' event" +
           " for " + event.target.location.href);
      return;
    }
    clearTimeout(timeout);
    tab.linkedBrowser.removeEventListener(eventType, handle, true);
    info("Tab event received: " + eventType);
    deferred.resolve(event);
  }

  let timeout = setTimeout(() => {
    tab.linkedBrowser.removeEventListener(eventType, handle, true);
    deferred.reject(new Error("Timed out while waiting for a '" + eventType + "'' event"));
  }, 30000);

  tab.linkedBrowser.addEventListener(eventType, handle, true, true);
  if (url) {
    tab.linkedBrowser.loadURI(url);
  }
  return deferred.promise;
}

// This function is mostly ported from classifierCommon.js
// under toolkit/components/url-classifier/tests/mochitest.
function waitForDBInit(callback) {
  // Since there are two cases that may trigger the callback,
  // we have to carefully avoid multiple callbacks and observer
  // leaking.
  let didCallback = false;
  function callbackOnce() {
    if (!didCallback) {
      Services.obs.removeObserver(obsFunc, "mozentries-update-finished");
      callback();
    }
    didCallback = true;
  }

  // The first part: listen to internal event.
  function obsFunc() {
    ok(true, "Received internal event!");
    callbackOnce();
  }
  Services.obs.addObserver(obsFunc, "mozentries-update-finished", false);

  // The second part: we might have missed the event. Just do
  // an internal database lookup to confirm if the url has been
  // added.
  let principal = Services.scriptSecurityManager
    .createCodebasePrincipal(Services.io.newURI(PHISH_URL), {});

  let dbService = Cc["@mozilla.org/url-classifier/dbservice;1"]
    .getService(Ci.nsIUrlClassifierDBService);
  dbService.lookup(principal, PHISH_TABLE, value => {
    if (value === PHISH_TABLE) {
      ok(true, "DB lookup success!");
      callbackOnce();
    }
  });
}

Services.prefs.setCharPref("urlclassifier.malwareTable", "test-malware-simple,test-unwanted-simple");
Services.prefs.setCharPref("urlclassifier.phishTable", "test-phish-simple");
Services.prefs.setCharPref("urlclassifier.blockedTable", "test-block-simple");
SafeBrowsing.init();

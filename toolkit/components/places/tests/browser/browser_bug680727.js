/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/* Ensure that clicking the button in the Offline mode neterror page updates
   global history. See bug 680727. */
/* TEST_PATH=toolkit/components/places/tests/browser/browser_bug680727.js make -C $(OBJDIR) mochitest-browser-chrome */


const kUniqueURI = Services.io.newURI("http://mochi.test:8888/#bug_680727",
                                      null, null);
var gAsyncHistory =
  Cc["@mozilla.org/browser/history;1"].getService(Ci.mozIAsyncHistory);

var proxyPrefValue;
var ourTab;

function test() {
  waitForExplicitFinish();

  // Tests always connect to localhost, and per bug 87717, localhost is now
  // reachable in offline mode.  To avoid this, disable any proxy.
  proxyPrefValue = Services.prefs.getIntPref("network.proxy.type");
  Services.prefs.setIntPref("network.proxy.type", 0);

  // Clear network cache.
  Components.classes["@mozilla.org/netwerk/cache-storage-service;1"]
            .getService(Components.interfaces.nsICacheStorageService)
            .clear();

  // Go offline, expecting the error page.
  Services.io.offline = true;

  BrowserTestUtils.openNewForegroundTab(gBrowser).then(tab => {
    ourTab = tab;
    BrowserTestUtils.waitForContentEvent(ourTab.linkedBrowser, "DOMContentLoaded",
                    .then(errorListener);
    BrowserTestUtils.loadURI(ourTab.linkedBrowser, kUniqueURI.spec);
  });
}

//------------------------------------------------------------------------------
// listen to loading the neterror page. (offline mode)
function errorListener() {
  ok(Services.io.offline, "Services.io.offline is true.");

  // This is an error page.
  ContentTask.spawn(ourTab.linkedBrowser, kUniqueURI.spec, function(uri) {
    is(content.document.documentURI.substring(0, 27),
       "about:neterror?e=netOffline",
       "Document URI is the error page.");

    // But location bar should show the original request.
    is(content.location.href, uri,
       "Docshell URI is the original URI.");
  }).then(() => {
    // Global history does not record URI of a failed request.
    return PlacesTestUtils.promiseAsyncUpdates().then(() => {
      gAsyncHistory.isURIVisited(kUniqueURI, errorAsyncListener);
    });
  });
}

function errorAsyncListener(aURI, aIsVisited) {
  ok(kUniqueURI.equals(aURI) && !aIsVisited,
     "The neterror page is not listed in global history.");

  Services.prefs.setIntPref("network.proxy.type", proxyPrefValue);

  // Now press the "Try Again" button, with offline mode off.
  Services.io.offline = false;

  BrowserTestUtils.waitForContentEvent(ourTab.linkedBrowser, "DOMContentLoaded")
                  .then(reloadListener);

  ContentTask.spawn(ourTab.linkedBrowser, null, function() {
    ok(content.document.getElementById("errorTryAgain"),
       "The error page has got a #errorTryAgain element");
    content.document.getElementById("errorTryAgain").click();
  });
}

//------------------------------------------------------------------------------
// listen to reload of neterror.
function reloadListener() {
  // This listener catches "DOMContentLoaded" on being called
  // nsIWPL::onLocationChange(...). That is right *AFTER*
  // IHistory::VisitURI(...) is called.
  ok(!Services.io.offline, "Services.io.offline is false.");

  ContentTask.spawn(ourTab.linkedBrowser, kUniqueURI.spec, function(uri) {
    // This is not an error page.
    is(content.document.documentURI, uri,
       "Document URI is not the offline-error page, but the original URI.");
  }).then(() => {
    // Check if global history remembers the successfully-requested URI.
    PlacesTestUtils.promiseAsyncUpdates().then(() => {
      gAsyncHistory.isURIVisited(kUniqueURI, reloadAsyncListener);
    });
  });
}

function reloadAsyncListener(aURI, aIsVisited) {
  ok(kUniqueURI.equals(aURI) && aIsVisited, "We have visited the URI.");
  PlacesTestUtils.clearHistory().then(finish);
}

registerCleanupFunction(function* () {
  Services.prefs.setIntPref("network.proxy.type", proxyPrefValue);
  Services.io.offline = false;
  yield BrowserTestUtils.removeTab(ourTab);
});

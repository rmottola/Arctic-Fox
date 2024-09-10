/* globals Cu, XPCOMUtils, Preferences, is, registerCleanupFunction, NewTabWebChannel, PlacesTestUtils */

"use strict";

Cu.import("resource://gre/modules/Preferences.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "NewTabWebChannel",
                                  "resource:///modules/NewTabWebChannel.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "NewTabMessages",
                                  "resource:///modules/NewTabMessages.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "PlacesTestUtils",
                                  "resource://testing-common/PlacesTestUtils.jsm");

function setup() {
  Preferences.set("browser.newtabpage.enhanced", true);
  Preferences.set("browser.newtabpage.remote.mode", "test");
  Preferences.set("browser.newtabpage.remote", true);
  NewTabMessages.init();
}

function cleanup() {
  NewTabMessages.uninit();
  Preferences.set("browser.newtabpage.remote", false);
  Preferences.set("browser.newtabpage.remote.mode", "production");
}
registerCleanupFunction(cleanup);

/*
 * Sanity tests for pref messages
 */
add_task(function* prefMessages_request() {
  setup();
  let testURL = "https://example.com/browser/browser/components/newtab/tests/browser/newtabmessages_prefs.html";

  let tabOptions = {
    gBrowser,
    url: testURL
  };

  let prefResponseAck = new Promise(resolve => {
    NewTabWebChannel.once("responseAck", () => {
      ok(true, "a request response has been received");
      resolve();
    });
  });

  yield BrowserTestUtils.withNewTab(tabOptions, function*() {
    yield prefResponseAck;
    let prefChangeAck = new Promise(resolve => {
      NewTabWebChannel.once("responseAck", () => {
        ok(true, "a change response has been received");
        resolve();
      });
    });
    Preferences.set("browser.newtabpage.enhanced", false);
    yield prefChangeAck;
  });
  cleanup();
});

/*
 * Sanity tests for preview messages
 */
add_task(function* previewMessages_request() {
  setup();
  var oldEnabledPref = Services.prefs.getBoolPref("browser.pagethumbnails.capturing_disabled");
  Services.prefs.setBoolPref("browser.pagethumbnails.capturing_disabled", false);

  let testURL = "https://example.com/browser/browser/components/newtab/tests/browser/newtabmessages_preview.html";

  let tabOptions = {
    gBrowser,
    url: testURL
  };

  let previewResponseAck = new Promise(resolve => {
    NewTabWebChannel.once("responseAck", () => {
      ok(true, "a request response has been received");
      resolve();
    });
  });

  yield BrowserTestUtils.withNewTab(tabOptions, function*() {
    yield previewResponseAck;
  });
  cleanup();
  Services.prefs.setBoolPref("browser.pagethumbnails.capturing_disabled", oldEnabledPref);
});

/*
 * Sanity tests for places messages
 */
add_task(function* placesMessages_request() {
  setup();
  let testURL = "https://example.com/browser/browser/components/newtab/tests/browser/newtabmessages_places.html";

  // url prefix for test history population
  const TEST_URL = "https://mozilla.com/";
  // time when the test starts execution
  const TIME_NOW = (new Date()).getTime();

  // utility function to compute past timestamp
  function timeDaysAgo(numDays) {
    return TIME_NOW - (numDays * 24 * 60 * 60 * 1000);
  }

  // utility function to make a visit for insertion into places db
  function makeVisit(index, daysAgo, isTyped, domain=TEST_URL) {
    let {
      TRANSITION_TYPED,
      TRANSITION_LINK
    } = PlacesUtils.history;

    return {
      uri: NetUtil.newURI(`${domain}${index}`),
      visitDate: timeDaysAgo(daysAgo),
      transition: (isTyped) ? TRANSITION_TYPED : TRANSITION_LINK,
    };
  }

  yield PlacesTestUtils.clearHistory();

  // all four visits must come from different domains to avoid deduplication
  let visits = [
    makeVisit(0, 0, true, "http://bar.com/"), // frecency 200, today
    makeVisit(1, 0, true, "http://foo.com/"), // frecency 200, today
    makeVisit(2, 2, true, "http://buz.com/"), // frecency 200, 2 days ago
    makeVisit(3, 2, false, "http://aaa.com/"), // frecency 10, 2 days ago, transition
  ];

  yield PlacesTestUtils.addVisits(visits);

  /** Test Begins **/

  let tabOptions = {
    gBrowser,
    url: testURL
  };

  let placesResponseAck = new Promise(resolve => {
    NewTabWebChannel.once("numItemsAck", (_, msg) => {
      ok(true, "a request response has been received");
      is(msg.data, visits.length + 1, "received an expect number of history items");
      resolve();
    });
  });

  yield BrowserTestUtils.withNewTab(tabOptions, function*() {
    yield placesResponseAck;
    let placesChangeAck = new Promise(resolve => {
      NewTabWebChannel.once("clearHistoryAck", (_, msg) => {
        ok(true, "a change response has been received");
        is(msg.data, "clearHistory", "a clear history message has been received");
        resolve();
      });
    });
    yield PlacesTestUtils.clearHistory();
    yield placesChangeAck;
  });
  cleanup();
});

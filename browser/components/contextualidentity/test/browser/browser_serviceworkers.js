let { classes: Cc, interfaces: Ci } = Components;

let swm = Cc["@mozilla.org/serviceworkers/manager;1"].
          getService(Ci.nsIServiceWorkerManager);

const BASE_ORIGIN = "https://example.com";
const URI = BASE_ORIGIN +
  "/browser/browser/components/contextualidentity/test/browser/serviceworker.html";
const NUM_USER_CONTEXTS = 3;

// opens `uri' in a new tab with the provided userContextId and focuses it.
// returns the newly opened tab
function openTabInUserContext(uri, userContextId) {
  // open the tab in the correct userContextId
  let tab = gBrowser.addTab(uri, {userContextId});

  // select tab and make sure its browser is focused
  gBrowser.selectedTab = tab;
  tab.ownerDocument.defaultView.focus();

  return tab;
}

add_task(function* setup() {
  // make sure userContext is enabled.
  SpecialPowers.pushPrefEnv({"set": [
    ["privacy.userContext.enabled", true]
  ]});
});

add_task(function* cleanup() {
  // make sure we don't leave any prefs set for the next tests
  registerCleanupFunction(function() {
    SpecialPowers.popPrefEnv();
  });
});

let infos = [];

add_task(function* test() {
  // Open the same URI in multiple user contexts, and make sure we have a
  // separate service worker in each of the contexts
  for (let userContextId = 0; userContextId < NUM_USER_CONTEXTS; userContextId++) {
    // Open a tab in given user contexts
    let tab = openTabInUserContext(URI, userContextId);

    // wait for tab load
    yield BrowserTestUtils.browserLoaded(gBrowser.getBrowserForTab(tab));

    // remove the tab
    gBrowser.removeTab(tab);
  }

  if (!allRegistered()) {
    yield promiseAllRegistered();
  }
  ok(true, "all service workers are registered");

  // Unregistered all service workers added in this test
  for (let info of infos) {
    yield promiseUnregister(info);
  }
});

function allRegistered() {
  let results = [];
  let registrations = swm.getAllRegistrations();
  for (let i = 0; i < registrations.length; i++) {
    let info = registrations.queryElementAt(i, Ci.nsIServiceWorkerRegistrationInfo);
    let principal = info.principal;
    if (principal.originNoSuffix === BASE_ORIGIN) {
      results[principal.userContextId] = true;
      infos[principal.userContextId] = info;
    }
  }
  for (let userContextId = 0; userContextId < NUM_USER_CONTEXTS; userContextId++) {
    if (!results[userContextId]) {
      return false;
    }
  }
  return true;
}

function promiseAllRegistered() {
  return new Promise(function(resolve) {
    let listener = {
      onRegister: function() {
        if (allRegistered()) {
          swm.removeListener(listener);
          resolve();
        }
      }
    }
    swm.addListener(listener);
  });
}

function promiseUnregister(info) {
  return new Promise(function(resolve) {
    swm.unregister(info.principal, {
      unregisterSucceeded: function(aState) {
        ok(aState, "ServiceWorkerRegistration exists");
        resolve();
      },
      unregisterFailed: function(aState) {
        ok(false, "unregister should succeed");
      }
    }, info.scope);
  });
}

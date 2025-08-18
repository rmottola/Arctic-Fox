/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const TEST_MSG = "ContentSearchTest";
const CONTENT_SEARCH_MSG = "ContentSearch";
const TEST_CONTENT_SCRIPT_BASENAME = "contentSearch.js";

var gMsgMan;

add_task(function* GetState() {
  yield addTab();
  gMsgMan.sendAsyncMessage(TEST_MSG, {
    type: "GetState",
  });
  let msg = yield waitForTestMsg("State");
  checkMsg(msg, {
    type: "State",
    data: yield currentStateObj(),
  });
});

add_task(function* SetCurrentEngine() {
  yield addTab();
  let newCurrentEngine = null;
  let oldCurrentEngine = Services.search.currentEngine;
  let engines = Services.search.getVisibleEngines();
  for (let engine of engines) {
    if (engine != oldCurrentEngine) {
      newCurrentEngine = engine;
      break;
    }
  }
  if (!newCurrentEngine) {
    info("Couldn't find a non-selected search engine, " +
         "skipping this part of the test");
    return;
  }
  gMsgMan.sendAsyncMessage(TEST_MSG, {
    type: "SetCurrentEngine",
    data: newCurrentEngine.name,
  });
  let deferred = Promise.defer();
  Services.obs.addObserver(function obs(subj, topic, data) {
    info("Test observed " + data);
    if (data == "engine-current") {
      ok(true, "Test observed engine-current");
      Services.obs.removeObserver(obs, "browser-search-engine-modified", false);
      deferred.resolve();
    }
  }, "browser-search-engine-modified", false);
  let searchPromise = waitForTestMsg("CurrentEngine");
  info("Waiting for test to observe engine-current...");
  yield deferred.promise;
  let msg = yield searchPromise;
  checkMsg(msg, {
    type: "CurrentEngine",
    data: yield currentEngineObj(newCurrentEngine),
  });

  Services.search.currentEngine = oldCurrentEngine;
  let msg = yield waitForTestMsg("CurrentEngine");
  checkMsg(msg, {
    type: "CurrentEngine",
    data: yield currentEngineObj(oldCurrentEngine),
  });
});

add_task(function* modifyEngine() {
  yield addTab();
  let engine = Services.search.currentEngine;
  let oldAlias = engine.alias;
  engine.alias = "ContentSearchTest";
  let msg = yield waitForTestMsg("CurrentState");
  checkMsg(msg, {
    type: "CurrentState",
    data: yield currentStateObj(),
  });
  engine.alias = oldAlias;
  msg = yield waitForTestMsg("CurrentState");
  checkMsg(msg, {
    type: "CurrentState",
    data: yield currentStateObj(),
  });
});

add_task(function* search() {
  yield addTab();
  let engine = Services.search.currentEngine;
  let data = {
    engineName: engine.name,
    searchString: "ContentSearchTest",
    whence: "ContentSearchTest",
  };
  gMsgMan.sendAsyncMessage(TEST_MSG, {
    type: "Search",
    data: data,
  });
  let submissionURL =
    engine.getSubmission(data.searchString, "", data.whence).uri.spec;
  yield waitForLoadAndStopIt(gBrowser.selectedBrowser, submissionURL);
});

add_task(function* searchInBackgroundTab() {
  // This test is like search(), but it opens a new tab after starting a search
  // in another.  In other words, it performs a search in a background tab.  The
  // search page should be loaded in the same tab that performed the search, in
  // the background tab.
  yield addTab();
  let searchBrowser = gBrowser.selectedBrowser;
  let engine = Services.search.currentEngine;
  let data = {
    engineName: engine.name,
    searchString: "ContentSearchTest",
    whence: "ContentSearchTest",
  };
  gMsgMan.sendAsyncMessage(TEST_MSG, {
    type: "Search",
    data: data,
  });

  let newTab = gBrowser.addTab();
  gBrowser.selectedTab = newTab;
  registerCleanupFunction(() => gBrowser.removeTab(newTab));

  let submissionURL =
    engine.getSubmission(data.searchString, "", data.whence).uri.spec;
  yield waitForLoadAndStopIt(searchBrowser, submissionURL);
});

add_task(function* GetSuggestions_AddFormHistoryEntry_RemoveFormHistoryEntry() {
  yield addTab();

  // Add the test engine that provides suggestions.
  let vals = yield waitForNewEngine("contentSearchSuggestions.xml", 0);
  let engine = vals[0];

  let searchStr = "browser_ContentSearch.js-suggestions-";

  // Add a form history suggestion and wait for Satchel to notify about it.
  gMsgMan.sendAsyncMessage(TEST_MSG, {
    type: "AddFormHistoryEntry",
    data: searchStr + "form",
  });
  let deferred = Promise.defer();
  Services.obs.addObserver(function onAdd(subj, topic, data) {
    if (data == "formhistory-add") {
      executeSoon(() => deferred.resolve());
    }
  }, "satchel-storage-changed", false);
  yield deferred.promise;

  // Send GetSuggestions using the test engine.  Its suggestions should appear
  // in the remote suggestions in the Suggestions response below.
  gMsgMan.sendAsyncMessage(TEST_MSG, {
    type: "GetSuggestions",
    data: {
      engineName: engine.name,
      searchString: searchStr,
      remoteTimeout: 5000,
    },
  });

  // Check the Suggestions response.
  let msg = yield waitForTestMsg("Suggestions");
  checkMsg(msg, {
    type: "Suggestions",
    data: {
      engineName: engine.name,
      searchString: searchStr,
      formHistory: [searchStr + "form"],
      remote: [searchStr + "foo", searchStr + "bar"],
    },
  });

  // Delete the form history suggestion and wait for Satchel to notify about it.
  gMsgMan.sendAsyncMessage(TEST_MSG, {
    type: "RemoveFormHistoryEntry",
    data: searchStr + "form",
  });
  deferred = Promise.defer();
  Services.obs.addObserver(function onRemove(subj, topic, data) {
    if (data == "formhistory-remove") {
      executeSoon(() => deferred.resolve());
    }
  }, "satchel-storage-changed", false);
  yield deferred.promise;

  // Send GetSuggestions again.
  gMsgMan.sendAsyncMessage(TEST_MSG, {
    type: "GetSuggestions",
    data: {
      engineName: engine.name,
      searchString: searchStr,
      remoteTimeout: 5000,
    },
  });

  // The formHistory suggestions in the Suggestions response should be empty.
  msg = yield waitForTestMsg("Suggestions");
  checkMsg(msg, {
    type: "Suggestions",
    data: {
      engineName: engine.name,
      searchString: searchStr,
      formHistory: [],
      remote: [searchStr + "foo", searchStr + "bar"],
    },
  });

  // Finally, clean up by removing the test engine.
  Services.search.removeEngine(engine);
  yield waitForTestMsg("CurrentState");
});


function checkMsg(actualMsg, expectedMsgData) {
  SimpleTest.isDeeply(actualMsg.data, expectedMsgData, "Checking message");
}

function waitForMsg(name, type) {
  let deferred = Promise.defer();
  info("Waiting for " + name + " message " + type + "...");
  gMsgMan.addMessageListener(name, function onMsg(msg) {
    info("Received " + name + " message " + msg.data.type + "\n");
    if (msg.data.type == type) {
      gMsgMan.removeMessageListener(name, onMsg);
      deferred.resolve(msg);
    }
  });
  return deferred.promise;
}

function waitForTestMsg(type) {
  return waitForMsg(TEST_MSG, type);
}

function waitForLoadAndStopIt(browser, expectedURL) {
  let deferred = Promise.defer();
  let listener = {
    onStateChange: function (webProg, req, flags, status) {
      if (req instanceof Ci.nsIChannel) {
        let url = req.originalURI.spec;
        info("onStateChange " + url);
        let docStart = Ci.nsIWebProgressListener.STATE_IS_DOCUMENT |
                       Ci.nsIWebProgressListener.STATE_START;
        if ((flags & docStart) && webProg.isTopLevel && url == expectedURL) {
          browser.removeProgressListener(listener);
          ok(true, "Expected URL loaded");
          req.cancel(Components.results.NS_ERROR_FAILURE);
          deferred.resolve();
        }
      }
    },
    QueryInterface: XPCOMUtils.generateQI([
      Ci.nsIWebProgressListener,
      Ci.nsISupportsWeakReference,
    ]),
  };
  browser.addProgressListener(listener);
  info("Waiting for URL to load: " + expectedURL);
  return deferred.promise;
}

function addTab() {
  let deferred = Promise.defer();
  let tab = gBrowser.addTab();
  gBrowser.selectedTab = tab;
  tab.linkedBrowser.addEventListener("load", function load() {
    tab.linkedBrowser.removeEventListener("load", load, true);
    let url = getRootDirectory(gTestPath) + TEST_CONTENT_SCRIPT_BASENAME;
    gMsgMan = tab.linkedBrowser.messageManager;
    gMsgMan.sendAsyncMessage(CONTENT_SEARCH_MSG, {
      type: "AddToWhitelist",
      data: ["about:blank"],
    });
    waitForMsg(CONTENT_SEARCH_MSG, "AddToWhitelistAck").then(() => {
      gMsgMan.loadFrameScript(url, false);
      deferred.resolve();
    });
  }, true);
  registerCleanupFunction(() => gBrowser.removeTab(tab));
  return deferred.promise;
}

var currentStateObj = Task.async(function* () {
  let state = {
    engines: [],
    currentEngine: yield currentEngineObj(),
  };
  for (let engine of Services.search.getVisibleEngines()) {
    let uri = engine.getIconURLBySize(16, 16);
    state.engines.push({
      name: engine.name,
      iconBuffer: yield arrayBufferFromDataURI(uri),
      hidden: false,
    });
  }
  return state;
});

var currentEngineObj = Task.async(function* () {
  let engine = Services.search.currentEngine;
  let uriFavicon = engine.getIconURLBySize(16, 16);
  let bundle = Services.strings.createBundle("chrome://global/locale/autocomplete.properties");
  return {
    name: engine.name,
    placeholder: bundle.formatStringFromName("searchWithEngine", [engine.name], 1),
    iconBuffer: yield arrayBufferFromDataURI(uriFavicon),
  };
});

function arrayBufferFromDataURI(uri) {
  if (!uri) {
    return Promise.resolve(null);
  }
  let deferred = Promise.defer();
  let xhr = Cc["@mozilla.org/xmlextras/xmlhttprequest;1"].
            createInstance(Ci.nsIXMLHttpRequest);
  xhr.open("GET", uri, true);
  xhr.responseType = "arraybuffer";
  xhr.onerror = () => {
    deferred.resolve(null);
  };
  xhr.onload = () => {
    deferred.resolve(xhr.response);
  };
  try {
    xhr.send();
  }
  catch (err) {
    return Promise.resolve(null);
  }
  return deferred.promise;
}

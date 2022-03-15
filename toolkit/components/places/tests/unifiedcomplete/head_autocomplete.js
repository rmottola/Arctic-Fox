/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

const Ci = Components.interfaces;
const Cc = Components.classes;
const Cr = Components.results;
const Cu = Components.utils;

Cu.import("resource://gre/modules/Services.jsm");

// Import common head.
{
  let commonFile = do_get_file("../head_common.js", false);
  let uri = Services.io.newFileURI(commonFile);
  Services.scriptloader.loadSubScript(uri.spec, this);
}

// Put any other stuff relative to this test folder below.

const TITLE_SEARCH_ENGINE_SEPARATOR = " \u00B7\u2013\u00B7 ";

function run_test() {
  run_next_test();
}

function* cleanup() {
  Services.prefs.clearUserPref("browser.urlbar.autocomplete.enabled");
  Services.prefs.clearUserPref("browser.urlbar.autoFill");
  Services.prefs.clearUserPref("browser.urlbar.autoFill.typed");
  Services.prefs.clearUserPref("browser.urlbar.autoFill.searchEngines");
  for (let type of ["history", "bookmark", "history.onlyTyped", "openpage"]) {
    Services.prefs.clearUserPref("browser.urlbar.suggest." + type);
  }
  yield PlacesUtils.bookmarks.eraseEverything();
  yield PlacesTestUtils.clearHistory();
}
do_register_cleanup(cleanup);

/**
 * @param aSearches
 *        Array of AutoCompleteSearch names.
 */
function AutoCompleteInput(aSearches) {
  this.searches = aSearches;
}
AutoCompleteInput.prototype = {
  popup: {
    selectedIndex: -1,
    invalidate: function () {},
    QueryInterface: XPCOMUtils.generateQI([Ci.nsIAutoCompletePopup])
  },
  popupOpen: false,

  disableAutoComplete: false,
  completeDefaultIndex: true,
  completeSelectedIndex: true,
  forceComplete: false,

  minResultsForPopup: 0,
  maxRows: 0,

  showCommentColumn: false,
  showImageColumn: false,

  timeout: 10,
  searchParam: "",

  get searchCount() {
    return this.searches.length;
  },
  getSearchAt: function(aIndex) {
    return this.searches[aIndex];
  },

  textValue: "",
  // Text selection range
  _selStart: 0,
  _selEnd: 0,
  get selectionStart() {
    return this._selStart;
  },
  get selectionEnd() {
    return this._selEnd;
  },
  selectTextRange: function(aStart, aEnd) {
    this._selStart = aStart;
    this._selEnd = aEnd;
  },

  onSearchBegin: function () {},
  onSearchComplete: function () {},

  onTextEntered: function() false,
  onTextReverted: function() false,

  QueryInterface: XPCOMUtils.generateQI([Ci.nsIAutoCompleteInput])
}

function* check_autocomplete(test) {
  // At this point frecency could still be updating due to latest pages
  // updates.
  // This is not a problem in real life, but autocomplete tests should
  // return reliable resultsets, thus we have to wait.
  yield PlacesTestUtils.promiseAsyncUpdates();

  // Make an AutoCompleteInput that uses our searches and confirms results.
  let input = new AutoCompleteInput(["unifiedcomplete"]);
  input.textValue = test.search;

  if (test.searchParam)
    input.searchParam = test.searchParam;

  // Caret must be at the end for autoFill to happen.
  let strLen = test.search.length;
  input.selectTextRange(strLen, strLen);
  Assert.equal(input.selectionStart, strLen, "Selection starts at end");
  Assert.equal(input.selectionEnd, strLen, "Selection ends at the end");

  let controller = Cc["@mozilla.org/autocomplete/controller;1"]
                     .getService(Ci.nsIAutoCompleteController);
  controller.input = input;

  let numSearchesStarted = 0;
  input.onSearchBegin = () => {
    do_print("onSearchBegin received");
    numSearchesStarted++;
  };
  let deferred = Promise.defer();
  input.onSearchComplete = () => {
    do_print("onSearchComplete received");
    deferred.resolve();
  }

  let expectedSearches = 1;
  if (test.incompleteSearch) {
    controller.startSearch(test.incompleteSearch);
    expectedSearches++;
  }
  do_print("Searching for: '" + test.search + "'");
  controller.startSearch(test.search);
  yield deferred.promise;

  Assert.equal(numSearchesStarted, expectedSearches, "All searches started");

  // Check to see the expected uris and titles match up (in any order)
  if (test.matches) {
    // Do not modify the test original matches.
    let matches = test.matches.slice();

    for (let i = 0; i < controller.matchCount; i++) {
      let value = controller.getValueAt(i);
      let comment = controller.getCommentAt(i);
      do_print("Looking for '" + value + "', '" + comment + "' in expected results...");
      let j;
      for (j = 0; j < matches.length; j++) {
        // Skip processed expected results
        if (matches[j] == undefined)
          continue;

        let { uri, title, tags, searchEngine, style } = matches[j];
        if (tags)
          title += " \u2013 " + tags.sort().join(", ");
        if (searchEngine)
          title += TITLE_SEARCH_ENGINE_SEPARATOR + searchEngine;
        if (style)
          style = style.sort();
        else
          style = ["favicon"];

        do_print("Checking against expected '" + uri.spec + "', '" + title + "'...");
        // Got a match on both uri and title?
        if (stripPrefix(uri.spec) == stripPrefix(value) && title == comment) {
          do_print("Got a match at index " + j + "!");
          let actualStyle = controller.getStyleAt(i).split(/\s+/).sort();
          if (style)
            Assert.equal(actualStyle.toString(), style.toString(), "Match should have expected style");

          // Make it undefined so we don't process it again
          matches[j] = undefined;
          if (uri.spec.startsWith("moz-action:")) {
            Assert.ok(actualStyle.indexOf("action") != -1, "moz-action results should always have 'action' in their style");
          }
          break;
        }
      }

      // We didn't hit the break, so we must have not found it
      if (j == matches.length)
        do_throw("Didn't find the current result ('" + value + "', '" + comment + "') in matches");
    }

    Assert.equal(controller.matchCount, matches.length,
                 "Got as many results as expected");

    // If we expect results, make sure we got matches.
    do_check_eq(controller.searchStatus, matches.length ?
                Ci.nsIAutoCompleteController.STATUS_COMPLETE_MATCH :
                Ci.nsIAutoCompleteController.STATUS_COMPLETE_NO_MATCH);
  }

  if (test.autofilled) {
    // Check the autoFilled result.
    Assert.equal(input.textValue, test.autofilled,
                 "Autofilled value is correct");

    // Now force completion and check correct casing of the result.
    // This ensures the controller is able to do its magic case-preserving
    // stuff and correct replacement of the user's casing with result's one.
    controller.handleEnter(false);
    Assert.equal(input.textValue, test.completed,
                 "Completed value is correct");
  }
}

function addBookmark(aBookmarkObj) {
  Assert.ok(!!aBookmarkObj.uri, "Bookmark object contains an uri");
  let parentId = aBookmarkObj.parentId ? aBookmarkObj.parentId
                                       : PlacesUtils.unfiledBookmarksFolderId;
  let itemId = PlacesUtils.bookmarks
                          .insertBookmark(parentId,
                                          aBookmarkObj.uri,
                                          PlacesUtils.bookmarks.DEFAULT_INDEX,
                                          aBookmarkObj.title || "A bookmark");
  if (aBookmarkObj.keyword) {
    yield PlacesUtils.keywords.insert({ keyword: aBookmarkObj.keyword,
                                        url: aBookmarkObj.uri.spec });
  }

  if (aBookmarkObj.tags) {
    PlacesUtils.tagging.tagURI(aBookmarkObj.uri, aBookmarkObj.tags);
  }
}

function addOpenPages(aUri, aCount=1) {
  let ac = Cc["@mozilla.org/autocomplete/search;1?name=unifiedcomplete"]
             .getService(Ci.mozIPlacesAutoComplete);
  for (let i = 0; i < aCount; i++) {
    ac.registerOpenPage(aUri);
  }
}

function removeOpenPages(aUri, aCount=1) {
  let ac = Cc["@mozilla.org/autocomplete/search;1?name=unifiedcomplete"]
             .getService(Ci.mozIPlacesAutoComplete);
  for (let i = 0; i < aCount; i++) {
    ac.unregisterOpenPage(aUri);
  }
}

function changeRestrict(aType, aChar) {
  let branch = "browser.urlbar.";
  // "title" and "url" are different from everything else, so special case them.
  if (aType == "title" || aType == "url")
    branch += "match.";
  else
    branch += "restrict.";

  do_print("changing restrict for " + aType + " to '" + aChar + "'");
  Services.prefs.setCharPref(branch + aType, aChar);
}

function resetRestrict(aType) {
  let branch = "browser.urlbar.";
  // "title" and "url" are different from everything else, so special case them.
  if (aType == "title" || aType == "url")
    branch += "match.";
  else
    branch += "restrict.";

  Services.prefs.clearUserPref(branch + aType);
}

/**
 * Strip prefixes from the URI that we don't care about for searching.
 *
 * @param spec
 *        The text to modify.
 * @return the modified spec.
 */
function stripPrefix(spec)
{
  ["http://", "https://", "ftp://"].some(scheme => {
    if (spec.startsWith(scheme)) {
      spec = spec.slice(scheme.length);
      return true;
    }
    return false;
  });

  if (spec.startsWith("www.")) {
    spec = spec.slice(4);
  }
  return spec;
}

function makeActionURI(action, params) {
  let url = "moz-action:" + action + "," + JSON.stringify(params);
  return NetUtil.newURI(url);
}

// Ensure we have a default search engine and the keyword.enabled preference
// set.
add_task(function ensure_search_engine() {
  // keyword.enabled is necessary for the tests to see keyword searches.
  Services.prefs.setBoolPref("keyword.enabled", true);

  // Remove any existing engines before adding ours.
  for (let engine of Services.search.getEngines()) {
    Services.search.removeEngine(engine);
  }
  Services.search.addEngineWithDetails("MozSearch", "", "", "", "GET",
                                       "http://s.example.com/search");
  let engine = Services.search.getEngineByName("MozSearch");
  Services.search.currentEngine = engine;
});

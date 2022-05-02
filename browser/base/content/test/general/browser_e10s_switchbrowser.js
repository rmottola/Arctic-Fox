const DUMMY_PATH = "browser/browser/base/content/test/general/dummy_page.html";

const gExpectedHistory = {
  index: -1,
  entries: []
};

function check_history() {
  let webNav = gBrowser.webNavigation;
  let sessionHistory = webNav.sessionHistory;

  let count = sessionHistory.count;
  is(count, gExpectedHistory.entries.length, "Should have the right number of history entries");
  is(sessionHistory.index, gExpectedHistory.index, "Should have the right history index");

  for (let i = 0; i < count; i++) {
    let entry = sessionHistory.getEntryAtIndex(i, false);
    is(entry.URI.spec, gExpectedHistory.entries[i].uri, "Should have the right URI");
    is(entry.title, gExpectedHistory.entries[i].title, "Should have the right title");
  }
}

// Waits for a load and updates the known history
let waitForLoad = Task.async(function*(uri) {
  info("Loading " + uri);
  // Longwinded but this ensures we don't just shortcut to LoadInNewProcess
  gBrowser.selectedBrowser.webNavigation.loadURI(uri, Ci.nsIWebNavigation.LOAD_FLAGS_NONE, null, null, null);

  yield waitForDocLoadComplete();
  gExpectedHistory.index++;
  gExpectedHistory.entries.push({
    uri: gBrowser.currentURI.spec,
    title: gBrowser.contentTitle
  });
});

let back = Task.async(function*() {
  info("Going back");
  gBrowser.goBack();
  yield waitForDocLoadComplete();
  gExpectedHistory.index--;
});

let forward = Task.async(function*() {
  info("Going forward");
  gBrowser.goForward();
  yield waitForDocLoadComplete();
  gExpectedHistory.index++;
});

// Tests that navigating from a page that should be in the remote process and
// a page that should be in the main process works and retains history
add_task(function* test_navigation() {
  SimpleTest.requestCompleteLog();

  let remoting = Services.prefs.getBoolPref("browser.tabs.remote.autostart");
  let expectedRemote = remoting ? "true" : "";

  info("1");
  // Create a tab and load a remote page in it
  gBrowser.selectedTab = gBrowser.addTab("about:blank", {skipAnimation: true});
  let {permanentKey} = gBrowser.selectedBrowser;
  yield waitForLoad("http://example.org/" + DUMMY_PATH);
  is(gBrowser.selectedBrowser.isRemoteBrowser, expectedRemote, "Remote attribute should be correct");
  is(gBrowser.selectedBrowser.permanentKey, permanentKey, "browser.permanentKey is still the same");

  info("2");
  // Load another page
  yield waitForLoad("http://example.com/" + DUMMY_PATH);
  is(gBrowser.selectedBrowser.isRemoteBrowser, expectedRemote, "Remote attribute should be correct");
  is(gBrowser.selectedBrowser.permanentKey, permanentKey, "browser.permanentKey is still the same");
  check_history();

  info("3");
  // Load a non-remote page
  yield waitForLoad("about:robots");
  is(gBrowser.selectedBrowser.isRemoteBrowser, "", "Remote attribute should be correct");
  is(gBrowser.selectedBrowser.permanentKey, permanentKey, "browser.permanentKey is still the same");
  check_history();

  info("4");
  // Load a remote page
  yield waitForLoad("http://example.org/" + DUMMY_PATH);
  is(gBrowser.selectedBrowser.isRemoteBrowser, expectedRemote, "Remote attribute should be correct");
  is(gBrowser.selectedBrowser.permanentKey, permanentKey, "browser.permanentKey is still the same");
  check_history();

  info("5");
  yield back();
  is(gBrowser.selectedBrowser.isRemoteBrowser, "", "Remote attribute should be correct");
  is(gBrowser.selectedBrowser.permanentKey, permanentKey, "browser.permanentKey is still the same");
  check_history();

  info("6");
  yield back();
  is(gBrowser.selectedBrowser.isRemoteBrowser, expectedRemote, "Remote attribute should be correct");
  is(gBrowser.selectedBrowser.permanentKey, permanentKey, "browser.permanentKey is still the same");
  check_history();

  info("7");
  yield forward();
  is(gBrowser.selectedBrowser.isRemoteBrowser, "", "Remote attribute should be correct");
  is(gBrowser.selectedBrowser.permanentKey, permanentKey, "browser.permanentKey is still the same");
  check_history();

  info("8");
  yield forward();
  is(gBrowser.selectedBrowser.isRemoteBrowser, expectedRemote, "Remote attribute should be correct");
  is(gBrowser.selectedBrowser.permanentKey, permanentKey, "browser.permanentKey is still the same");
  check_history();

  info("9");
  yield back();
  is(gBrowser.selectedBrowser.isRemoteBrowser, "", "Remote attribute should be correct");
  is(gBrowser.selectedBrowser.permanentKey, permanentKey, "browser.permanentKey is still the same");
  yield check_history();

  info("10");
  // Load a new remote page, this should replace the last history entry
  gExpectedHistory.entries.splice(gExpectedHistory.entries.length - 1, 1);
  yield waitForLoad("http://example.com/" + DUMMY_PATH);
  is(gBrowser.selectedBrowser.isRemoteBrowser, expectedRemote, "Remote attribute should be correct");
  is(gBrowser.selectedBrowser.permanentKey, permanentKey, "browser.permanentKey is still the same");
  yield check_history();

  info("11");
  gBrowser.removeCurrentTab();
});

// Tests that calling gBrowser.loadURI or browser.loadURI to load a page in a
// different process updates the browser synchronously
add_task(function* test_synchronous() {
  let remoting = Services.prefs.getBoolPref("browser.tabs.remote.autostart");
  let expectedRemote = remoting ? "true" : "";

  info("1");
  // Create a tab and load a remote page in it
  gBrowser.selectedTab = gBrowser.addTab("about:blank", {skipAnimation: true});
  let {permanentKey} = gBrowser.selectedBrowser;
  yield waitForLoad("http://example.org/" + DUMMY_PATH);
  is(gBrowser.selectedBrowser.isRemoteBrowser, expectedRemote, "Remote attribute should be correct");
  is(gBrowser.selectedBrowser.permanentKey, permanentKey, "browser.permanentKey is still the same");

  info("2");
  // Load another page
  info("Loading about:robots");
  gBrowser.selectedBrowser.loadURI("about:robots");
  is(gBrowser.selectedBrowser.isRemoteBrowser, "", "Remote attribute should be correct");
  is(gBrowser.selectedBrowser.permanentKey, permanentKey, "browser.permanentKey is still the same");

  yield waitForDocLoadComplete();
  is(gBrowser.selectedBrowser.isRemoteBrowser, "", "Remote attribute should be correct");
  is(gBrowser.selectedBrowser.permanentKey, permanentKey, "browser.permanentKey is still the same");

  info("3");
  // Load the remote page again
  info("Loading http://example.org/" + DUMMY_PATH);
  yield BrowserTestUtils.loadURI(gBrowser.selectedBrowser, "http://example.org/" + DUMMY_PATH);
  is(gBrowser.selectedBrowser.isRemoteBrowser, expectedRemote, "Remote attribute should be correct");
  is(gBrowser.selectedBrowser.permanentKey, permanentKey, "browser.permanentKey is still the same");

  yield waitForDocLoadComplete();
  is(gBrowser.selectedBrowser.isRemoteBrowser, expectedRemote, "Remote attribute should be correct");
  is(gBrowser.selectedBrowser.permanentKey, permanentKey, "browser.permanentKey is still the same");

  info("4");
  gBrowser.removeCurrentTab();
});

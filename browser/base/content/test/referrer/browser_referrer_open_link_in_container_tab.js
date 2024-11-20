// Tests referrer on context menu navigation - open link in new container tab.
// Selects "open link in new container tab" from the context menu.

function getReferrerTest(aTestNumber) {
  let test = _referrerTests[aTestNumber];
  if (test) {
    // We want all the referrer tests to fail!
    test.result = "";
  }

  return test;
}

function startNewTabTestCase(aTestNumber) {
  info("browser_referrer_open_link_in_container_tab: " +
       getReferrerTestDescription(aTestNumber));
  contextMenuOpened(gTestWindow, "testlink").then(function(aContextMenu) {
    someTabLoaded(gTestWindow).then(function(aNewTab) {
      gTestWindow.gBrowser.selectedTab = aNewTab;

      checkReferrerAndStartNextTest(aTestNumber, null, aNewTab,
                                    startNewTabTestCase);
    });

    let menu = gTestWindow.document.getElementById("context-openlinkinusercontext-menu");
    ok(menu && menu.firstChild, "The menu exists and it has a first child node.");

    let menupopup = menu.firstChild;
    is(menupopup.nodeType, Node.ELEMENT_NODE, "We have a menupopup.");
    ok(menupopup.firstChild, "We have a first container entry.");

    let firstContext = menupopup.firstChild;
    is(firstContext.nodeType, Node.ELEMENT_NODE, "We have a first container entry.");
    ok(firstContext.hasAttribute('usercontextid'), "We have a usercontextid value.");

    firstContext.doCommand();
    aContextMenu.hidePopup();
  });
}

function test() {
  waitForExplicitFinish();

  SpecialPowers.pushPrefEnv(
    {set: [["privacy.userContext.enabled", true]]},
    function() {
      requestLongerTimeout(10);  // slowwww shutdown on e10s
      startReferrerTest(startNewTabTestCase);
    });
}

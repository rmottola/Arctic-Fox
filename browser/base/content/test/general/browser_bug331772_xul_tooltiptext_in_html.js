/**
 * Tests that the tooltiptext attribute is used for XUL elements in an HTML doc.
 */
function test () {
  waitForExplicitFinish();
  gBrowser.selectedTab = gBrowser.addTab();
  BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser).then(() => {
    let doc = gBrowser.contentDocument;
    let tooltip = document.getElementById("aHTMLTooltip");

    ok(tooltip.fillInPageTooltip(doc.getElementById("xulToolbarButton")), "should get tooltiptext");
    is(tooltip.getAttribute("label"), "XUL tooltiptext");

    gBrowser.removeCurrentTab();
    finish();
  });

  gBrowser.loadURI(
    "http://mochi.test:8888/browser/browser/base/content/test/general/xul_tooltiptext.xhtml"
  );
}


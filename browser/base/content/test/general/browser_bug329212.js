function test () {

  waitForExplicitFinish();
  gBrowser.selectedTab = gBrowser.addTab();
  BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser).then(() => {
    let doc = gBrowser.contentDocument;
    let tooltip = document.getElementById("aHTMLTooltip");

    ok(tooltip.fillInPageTooltip(doc.getElementById("svg1")), "should get title");
    is(tooltip.getAttribute("label"), "This is a non-root SVG element title");

    ok(tooltip.fillInPageTooltip(doc.getElementById("text1")), "should get title");
    is(tooltip.getAttribute("label"), "\n\n\n    This            is a title\n\n    ");

    ok(!tooltip.fillInPageTooltip(doc.getElementById("text2")), "should not get title");

    ok(!tooltip.fillInPageTooltip(doc.getElementById("text3")), "should not get title");

    ok(tooltip.fillInPageTooltip(doc.getElementById("link1")), "should get title");
    is(tooltip.getAttribute("label"), "\n      This is a title\n    ");
    ok(tooltip.fillInPageTooltip(doc.getElementById("text4")), "should get title");
    is(tooltip.getAttribute("label"), "\n      This is a title\n    ");

    ok(!tooltip.fillInPageTooltip(doc.getElementById("link2")), "should not get title");

    ok(tooltip.fillInPageTooltip(doc.getElementById("link3")), "should get title");
    isnot(tooltip.getAttribute("label"), "");

    ok(tooltip.fillInPageTooltip(doc.getElementById("link4")), "should get title");
    is(tooltip.getAttribute("label"), "This is an xlink:title attribute");

    ok(!tooltip.fillInPageTooltip(doc.getElementById("text5")), "should not get title");

    gBrowser.removeCurrentTab();
    finish();
  });

  gBrowser.loadURI(
    "http://mochi.test:8888/browser/browser/base/content/test/general/title_test.svg"
  );
}


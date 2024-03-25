/* vim: set ts=2 et sw=2 tw=80: */
/* global nsContextMenu*/
/* Any copyright is dedicated to the Public Domain.
 http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

requestLongerTimeout(2);

// Tests that selecting a node using the browser context menu (inspect element)
// or the element picker focuses that node so that the keyboard can be used
// immediately.

const TEST_URL = "data:text/html;charset=utf8,<div>test element</div>";

add_task(function*() {
  let {toolbox, inspector} = yield addTab(TEST_URL).then(openInspector);

  info("Select the test node with the browser ctx menu");
  yield selectWithBrowserMenu(inspector);
  assertNodeSelected(inspector, "div");

  info("Press arrowUp to focus <body> " +
       "(which works if the node was focused properly)");
  let onNodeHighlighted = toolbox.once("node-highlight");
  EventUtils.synthesizeKey("VK_UP", {});
  yield waitForChildrenUpdated(inspector);
  yield onNodeHighlighted;
  assertNodeSelected(inspector, "body");

  info("Select the test node with the element picker");
  yield selectWithElementPicker(inspector);
  assertNodeSelected(inspector, "div");

  info("Press arrowUp to focus <body> " +
       "(which works if the node was focused properly)");
  onNodeHighlighted = toolbox.once("node-highlight");
  EventUtils.synthesizeKey("VK_UP", {});
  yield waitForChildrenUpdated(inspector);
  yield onNodeHighlighted;
  assertNodeSelected(inspector, "body");
});

function assertNodeSelected(inspector, tagName) {
  is(inspector.selection.nodeFront.tagName.toLowerCase(), tagName,
    `The <${tagName}> node is selected`);
}

function* selectWithBrowserMenu(inspector) {
  let contentAreaContextMenu = document.querySelector("#contentAreaContextMenu");
  let contextOpened = once(contentAreaContextMenu, "popupshown");

  yield BrowserTestUtils.synthesizeMouseAtCenter("div", {
    type: "contextmenu",
    button: 2
  }, gBrowser.selectedBrowser);

  yield contextOpened;

  yield gContextMenu.inspectNode();

  let contextClosed = once(contentAreaContextMenu, "popuphidden");
  contentAreaContextMenu.hidden = true;
  contentAreaContextMenu.hidePopup();

  yield inspector.once("inspector-updated");
  yield contextClosed;
}

function* selectWithElementPicker(inspector) {
  yield inspector.toolbox.highlighterUtils.startPicker();

  yield BrowserTestUtils.synthesizeMouseAtCenter("div", {
    type: "mousemove",
  }, gBrowser.selectedBrowser);

  executeInContent("Test:SynthesizeKey", {
    key: "VK_RETURN",
    options: {}
  }, false);
  yield inspector.once("inspector-updated");
}

/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Tests if the abstract tree base class for the profiler's tree view
 * is keyboard accessible.
 */

var { AbstractTreeItem } = Cu.import("resource://devtools/client/shared/widgets/AbstractTreeItem.jsm", {});
var { Heritage } = Cu.import("resource://devtools/client/shared/widgets/ViewHelpers.jsm", {});

function* spawnTest() {
  let container = document.createElement("vbox");
  gBrowser.selectedBrowser.parentNode.appendChild(container);

  // Populate the tree by pressing RIGHT...

  let treeRoot = new MyCustomTreeItem(gDataSrc, { parent: null });
  treeRoot.attachTo(container);
  treeRoot.focus();

  EventUtils.sendKey("RIGHT");
  ok(treeRoot.expanded,
    "The root node is now expanded.");
  is(document.commandDispatcher.focusedElement, treeRoot.target,
    "The root node is still focused.");

  let fooItem = treeRoot.getChild(0);
  let barItem = treeRoot.getChild(1);

  key("VK_RIGHT");
  ok(!fooItem.expanded,
    "The 'foo' node is not expanded yet.");
  is(document.commandDispatcher.focusedElement, fooItem.target,
    "The 'foo' node is now focused.");

  key("VK_RIGHT");
  ok(fooItem.expanded,
    "The 'foo' node is now expanded.");
  is(document.commandDispatcher.focusedElement, fooItem.target,
    "The 'foo' node is still focused.");

  key("VK_RIGHT");
  ok(!barItem.expanded,
    "The 'bar' node is not expanded yet.");
  is(document.commandDispatcher.focusedElement, barItem.target,
    "The 'bar' node is now focused.");

  key("VK_RIGHT");
  ok(barItem.expanded,
    "The 'bar' node is now expanded.");
  is(document.commandDispatcher.focusedElement, barItem.target,
    "The 'bar' node is still focused.");

  let bazItem = barItem.getChild(0);

  key("VK_RIGHT");
  ok(!bazItem.expanded,
    "The 'baz' node is not expanded yet.");
  is(document.commandDispatcher.focusedElement, bazItem.target,
    "The 'baz' node is now focused.");

  key("VK_RIGHT");
  ok(bazItem.expanded,
    "The 'baz' node is now expanded.");
  is(document.commandDispatcher.focusedElement, bazItem.target,
    "The 'baz' node is still focused.");

  // Test RIGHT on a leaf node.

  key("VK_RIGHT");
  is(document.commandDispatcher.focusedElement, bazItem.target,
    "The 'baz' node is still focused.");

  // Test DOWN on a leaf node.

  key("VK_DOWN");
  is(document.commandDispatcher.focusedElement, bazItem.target,
    "The 'baz' node is now refocused.");

  // Test UP.

  key("VK_UP");
  is(document.commandDispatcher.focusedElement, barItem.target,
    "The 'bar' node is now refocused.");

  key("VK_UP");
  is(document.commandDispatcher.focusedElement, fooItem.target,
    "The 'foo' node is now refocused.");

  key("VK_UP");
  is(document.commandDispatcher.focusedElement, treeRoot.target,
    "The root node is now refocused.");

  // Test DOWN.

  key("VK_DOWN");
  is(document.commandDispatcher.focusedElement, fooItem.target,
    "The 'foo' node is now refocused.");

  key("VK_DOWN");
  is(document.commandDispatcher.focusedElement, barItem.target,
    "The 'bar' node is now refocused.");

  key("VK_DOWN");
  is(document.commandDispatcher.focusedElement, bazItem.target,
    "The 'baz' node is now refocused.");

  // Test LEFT.

  key("VK_LEFT");
  ok(barItem.expanded,
    "The 'bar' node is still expanded.");
  is(document.commandDispatcher.focusedElement, barItem.target,
    "The 'bar' node is now refocused.");

  key("VK_LEFT");
  ok(!barItem.expanded,
    "The 'bar' node is not expanded anymore.");
  is(document.commandDispatcher.focusedElement, barItem.target,
    "The 'bar' node is still focused.");

  key("VK_LEFT");
  ok(treeRoot.expanded,
    "The root node is still expanded.");
  is(document.commandDispatcher.focusedElement, treeRoot.target,
    "The root node is now refocused.");

  key("VK_LEFT");
  ok(!treeRoot.expanded,
    "The root node is not expanded anymore.");
  is(document.commandDispatcher.focusedElement, treeRoot.target,
    "The root node is still focused.");

  // Test LEFT on the root node.

  key("VK_LEFT");
  is(document.commandDispatcher.focusedElement, treeRoot.target,
    "The root node is still focused.");

  // Test UP on the root node.

  key("VK_UP");
  is(document.commandDispatcher.focusedElement, treeRoot.target,
    "The root node is still focused.");

  container.remove();
});

/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* Any copyright is dedicated to the Public Domain.
 http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Tests that CSS property names are autocompleted and cycled correctly when
// creating a new property in the rule view.

const MAX_ENTRIES = 10;

// format :
//  [
//    what key to press,
//    expected input box value after keypress,
//    selectedIndex of the popup,
//    total items in the popup
//  ]
var testData = [
  ["d", "display", 1, 3],
  ["VK_DOWN", "dominant-baseline", 2, 3],
  ["VK_DOWN", "direction", 0, 3],
  ["VK_DOWN", "display", 1, 3],
  ["VK_UP", "direction", 0, 3],
  ["VK_UP", "dominant-baseline", 2, 3],
  ["VK_UP", "display", 1, 3],
  ["VK_BACK_SPACE", "d", -1, 0],
  ["i", "display", 1, 2],
  ["s", "display", -1, 0],
  ["VK_BACK_SPACE", "dis", -1, 0],
  ["VK_BACK_SPACE", "di", -1, 0],
  ["VK_BACK_SPACE", "d", -1, 0],
  ["VK_BACK_SPACE", "", -1, 0],
  ["f", "filter", 3, MAX_ENTRIES],
  ["i", "filter", 3, 4],
  ["VK_ESCAPE", null, -1, 0],
];

const TEST_URI = "<h1 style='border: 1px solid red'>Header</h1>";

add_task(function* () {
  yield addTab("data:text/html;charset=utf-8," + encodeURIComponent(TEST_URI));
  let {toolbox, inspector, view, testActor} = yield openRuleView();

  info("Test autocompletion after 1st page load");
  yield runAutocompletionTest(toolbox, inspector, view);

  info("Test autocompletion after page navigation");
  yield reloadPage(inspector, testActor);
  yield runAutocompletionTest(toolbox, inspector, view);
});

function* runAutocompletionTest(toolbox, inspector, view) {
  info("Selecting the test node");
  yield selectNode("h1", inspector);

  info("Focusing the css property editable field");
  let ruleEditor = getRuleViewRuleEditor(view, 0);
  let editor = yield focusNewRuleViewProperty(ruleEditor);

  info("Starting to test for css property completion");
  for (let i = 0; i < testData.length; i++) {
    yield testCompletion(testData[i], editor, view);
  }
}

function* testCompletion([key, completion, index, total], editor, view) {
  info("Pressing key " + key);
  info("Expecting " + completion + ", " + index + ", " + total);

  let onSuggest;

  if (/(right|back_space|escape)/ig.test(key)) {
    info("Adding event listener for right|back_space|escape keys");
    onSuggest = once(editor.input, "keypress");
  } else {
    info("Waiting for after-suggest event on the editor");
    onSuggest = editor.once("after-suggest");
  }

  info("Synthesizing key " + key);
  EventUtils.synthesizeKey(key, {}, view.styleWindow);

  yield onSuggest;
  yield waitForTick();

  info("Checking the state");
  if (completion != null) {
    is(editor.input.value, completion, "Correct value is autocompleted");
  }
  if (total == 0) {
    ok(!(editor.popup && editor.popup.isOpen), "Popup is closed");
  } else {
    ok(editor.popup._panel.state == "open" ||
       editor.popup._panel.state == "showing", "Popup is open");
    is(editor.popup.getItems().length, total, "Number of suggestions match");
    is(editor.popup.selectedIndex, index, "Correct item is selected");
  }
}

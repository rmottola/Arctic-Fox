/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* Any copyright is dedicated to the Public Domain.
 http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

var Cu = Components.utils;
var {require} = Cu.import("resource://devtools/shared/Loader.jsm", {});
var {gDevTools} = require("devtools/client/framework/devtools");
var {TargetFactory} = require("devtools/client/framework/target");
var {CssRuleView, _ElementStyle} = require("devtools/client/inspector/rules/rules");
var {CssLogic, CssSelector} = require("devtools/shared/inspector/css-logic");
var DevToolsUtils = require("devtools/shared/DevToolsUtils");
var promise = require("promise");
var {editableField, getInplaceEditorForSpan: inplaceEditor} =
  require("devtools/client/shared/inplace-editor");
var {console} = Cu.import("resource://gre/modules/Console.jsm", {});

// All tests are asynchronous
waitForExplicitFinish();

const TEST_URL_ROOT =
  "http://example.com/browser/devtools/client/inspector/shared/test/";
const TEST_URL_ROOT_SSL =
  "https://example.com/browser/devtools/client/inspector/shared/test/";
const ROOT_TEST_DIR = getRootDirectory(gTestPath);
const FRAME_SCRIPT_URL = ROOT_TEST_DIR + "doc_frame_script.js";

// Auto clean-up when a test ends
registerCleanupFunction(function*() {
  let target = TargetFactory.forTab(gBrowser.selectedTab);
  yield gDevTools.closeToolbox(target);

  while (gBrowser.tabs.length > 1) {
    gBrowser.removeCurrentTab();
  }
});

// Uncomment this pref to dump all devtools emitted events to the console.
// Services.prefs.setBoolPref("devtools.dump.emit", true);

// Set the testing flag on gDevTools and reset it when the test ends
DevToolsUtils.testing = true;
registerCleanupFunction(() => DevToolsUtils.testing = false);

// Clean-up all prefs that might have been changed during a test run
// (safer here because if the test fails, then the pref is never reverted)
registerCleanupFunction(() => {
  Services.prefs.clearUserPref("devtools.inspector.activeSidebar");
  Services.prefs.clearUserPref("devtools.dump.emit");
  Services.prefs.clearUserPref("devtools.defaultColorUnit");
});

/**
 * The functions found below are here to ease test development and maintenance.
 * Most of these functions are stateless and will require some form of context
 * (the instance of the current toolbox, or inspector panel for instance).
 *
 * Most of these functions are async too and return promises.
 *
 * All tests should follow the following pattern:
 *
 * add_task(function*() {
 *   yield addTab(TEST_URI);
 *   let {toolbox, inspector, view} = yield openComputedView();
 *
 *   yield selectNode("#test", inspector);
 *   yield someAsyncTestFunction(view);
 * });
 *
 * add_task is the way to define the testcase in the test file. It accepts
 * a single generator-function argument.
 * The generator function should yield any async call.
 *
 * There is no need to clean tabs up at the end of a test as this is done
 * automatically.
 *
 * It is advised not to store any references on the global scope. There
 * shouldn't be a need to anyway. Thanks to add_task, test steps, even
 * though asynchronous, can be described in a nice flat way, and
 * if/for/while/... control flow can be used as in sync code, making it
 * possible to write the outline of the test case all in add_task, and delegate
 * actual processing and assertions to other functions.
 */

/* *********************************************
 * UTILS
 * *********************************************
 * General test utilities.
 * Add new tabs, open the toolbox and switch to the various panels, select
 * nodes, get node references, ...
 */

/**
 * Add a new test tab in the browser and load the given url.
 *
 * @param {String} url
 *        The url to be loaded in the new tab
 * @return a promise that resolves to the tab object when the url is loaded
 */
function addTab(url) {
  info("Adding a new tab with URL: '" + url + "'");
  let def = promise.defer();

  window.focus();

  let tab = window.gBrowser.selectedTab = window.gBrowser.addTab(url);
  let browser = tab.linkedBrowser;

  info("Loading the helper frame script " + FRAME_SCRIPT_URL);
  browser.messageManager.loadFrameScript(FRAME_SCRIPT_URL, false);

  browser.addEventListener("load", function onload() {
    browser.removeEventListener("load", onload, true);
    info("URL '" + url + "' loading complete");

    def.resolve(tab);
  }, true);

  return def.promise;
}

/**
 * Simple DOM node accesor function that takes either a node or a string css
 * selector as argument and returns the corresponding node
 *
 * @param {String|DOMNode} nodeOrSelector
 * @return {DOMNode|CPOW} Note that in e10s mode a CPOW object is returned which
 * doesn't implement *all* of the DOMNode's properties
 */
function getNode(nodeOrSelector) {
  info("Getting the node for '" + nodeOrSelector + "'");
  return typeof nodeOrSelector === "string" ?
    content.document.querySelector(nodeOrSelector) :
    nodeOrSelector;
}

/**
 * Get the NodeFront for a given css selector, via the protocol
 *
 * @param {String} selector
 * @param {InspectorPanel} inspector
 *        The instance of InspectorPanel currently loaded in the toolbox
 * @return {Promise} Resolves to the NodeFront instance
 */
function getNodeFront(selector, {walker}) {
  return walker.querySelector(walker.rootNode, selector);
}

/*
 * Set the inspector's current selection to a node or to the first match of the
 * given css selector.
 *
 * @param {String|NodeFront} data
 *        The node to select
 * @param {InspectorPanel} inspector
 *        The instance of InspectorPanel currently loaded in the toolbox
 * @param {String} reason
 *        Defaults to "test" which instructs the inspector not
 *        to highlight the node upon selection
 * @return {Promise} Resolves when the inspector is updated with the new node
 */
var selectNode = Task.async(function*(data, inspector, reason="test") {
  info("Selecting the node for '" + data + "'");
  let nodeFront = data;
  if (!data._form) {
    nodeFront = yield getNodeFront(data, inspector);
  }
  let updated = inspector.once("inspector-updated");
  inspector.selection.setNodeFront(nodeFront, reason);
  yield updated;
});

/**
 * Set the inspector's current selection to null so that no node is selected
 *
 * @param {InspectorPanel} inspector
 *        The instance of InspectorPanel currently loaded in the toolbox
 * @return a promise that resolves when the inspector is updated
 */
function clearCurrentNodeSelection(inspector) {
  info("Clearing the current selection");
  let updated = inspector.once("inspector-updated");
  inspector.selection.setNodeFront(null);
  return updated;
}

/**
 * Open the toolbox, with the inspector tool visible.
 *
 * @return a promise that resolves when the inspector is ready
 */
var openInspector = Task.async(function*() {
  info("Opening the inspector");
  let target = TargetFactory.forTab(gBrowser.selectedTab);

  let inspector, toolbox;

  // Checking if the toolbox and the inspector are already loaded
  // The inspector-updated event should only be waited for if the inspector
  // isn't loaded yet
  toolbox = gDevTools.getToolbox(target);
  if (toolbox) {
    inspector = toolbox.getPanel("inspector");
    if (inspector) {
      info("Toolbox and inspector already open");
      return {
        toolbox: toolbox,
        inspector: inspector
      };
    }
  }

  info("Opening the toolbox");
  toolbox = yield gDevTools.showToolbox(target, "inspector");
  yield waitForToolboxFrameFocus(toolbox);
  inspector = toolbox.getPanel("inspector");

  info("Waiting for the inspector to update");
  yield inspector.once("inspector-updated");

  return {
    toolbox: toolbox,
    inspector: inspector
  };
});

/**
 * Wait for the toolbox frame to receive focus after it loads
 *
 * @param {Toolbox} toolbox
 * @return a promise that resolves when focus has been received
 */
function waitForToolboxFrameFocus(toolbox) {
  info("Making sure that the toolbox's frame is focused");
  let def = promise.defer();
  let win = toolbox.frame.contentWindow;
  waitForFocus(def.resolve, win);
  return def.promise;
}

/**
 * Open the toolbox, with the inspector tool visible, and the sidebar that
 * corresponds to the given id selected
 *
 * @return a promise that resolves when the inspector is ready and the sidebar
 * view is visible and ready
 */
var openInspectorSideBar = Task.async(function*(id) {
  let {toolbox, inspector} = yield openInspector();

  info("Selecting the " + id + " sidebar");
  inspector.sidebar.select(id);

  return {
    toolbox: toolbox,
    inspector: inspector,
    view: inspector[id].view
  };
});

/**
 * Open the toolbox, with the inspector tool visible, and the computed-view
 * sidebar tab selected.
 *
 * @return a promise that resolves when the inspector is ready and the computed
 * view is visible and ready
 */
function openComputedView() {
  return openInspectorSideBar("computedview");
}

/**
 * Open the toolbox, with the inspector tool visible, and the rule-view
 * sidebar tab selected.
 *
 * @return a promise that resolves when the inspector is ready and the rule
 * view is visible and ready
 */
function openRuleView() {
  return openInspectorSideBar("ruleview");
}

/**
 * Wait for eventName on target to be delivered a number of times.
 *
 * @param {Object} target
 *        An observable object that either supports on/off or
 *        addEventListener/removeEventListener
 * @param {String} eventName
 * @param {Number} numTimes
 *        Number of deliveries to wait for.
 * @param {Boolean} useCapture
 *        Optional, for addEventListener/removeEventListener
 * @return A promise that resolves when the event has been handled
 */
function waitForNEvents(target, eventName, numTimes, useCapture = false) {
  info("Waiting for event: '" + eventName + "' on " + target + ".");

  let deferred = promise.defer();
  let count = 0;

  for (let [add, remove] of [
    ["addEventListener", "removeEventListener"],
    ["addListener", "removeListener"],
    ["on", "off"]
  ]) {
    if ((add in target) && (remove in target)) {
      target[add](eventName, function onEvent(...aArgs) {
        if (++count == numTimes) {
          target[remove](eventName, onEvent, useCapture);
          deferred.resolve.apply(deferred, aArgs);
        }
      }, useCapture);
      break;
    }
  }

  return deferred.promise;
}

/**
 * Wait for eventName on target.
 *
 * @param {Object} target
 *        An observable object that either supports on/off or
 *        addEventListener/removeEventListener
 * @param {String} eventName
 * @param {Boolean} useCapture
 *        Optional, for addEventListener/removeEventListener
 * @return A promise that resolves when the event has been handled
 */
function once(target, eventName, useCapture=false) {
  return waitForNEvents(target, eventName, 1, useCapture);
}

/**
 * This shouldn't be used in the tests, but is useful when writing new tests or
 * debugging existing tests in order to introduce delays in the test steps
 *
 * @param {Number} ms
 *        The time to wait
 * @return A promise that resolves when the time is passed
 */
function wait(ms) {
  let def = promise.defer();
  content.setTimeout(def.resolve, ms);
  return def.promise;
}

/**
 * Wait for a content -> chrome message on the message manager (the window
 * messagemanager is used).
 *
 * @param {String} name
 *        The message name
 * @return {Promise} A promise that resolves to the response data when the
 * message has been received
 */
function waitForContentMessage(name) {
  info("Expecting message " + name + " from content");

  let mm = gBrowser.selectedBrowser.messageManager;

  let def = promise.defer();
  mm.addMessageListener(name, function onMessage(msg) {
    mm.removeMessageListener(name, onMessage);
    def.resolve(msg.data);
  });
  return def.promise;
}

/**
 * Send an async message to the frame script (chrome -> content) and wait for a
 * response message with the same name (content -> chrome).
 *
 * @param {String} name
 *        The message name. Should be one of the messages defined
 *        in doc_frame_script.js
 * @param {Object} data
 *        Optional data to send along
 * @param {Object} objects
 *        Optional CPOW objects to send along
 * @param {Boolean} expectResponse
 *        If set to false, don't wait for a response with the same name
 *        from the content script. Defaults to true.
 * @return {Promise} Resolves to the response data if a response is expected,
 * immediately resolves otherwise
 */
function executeInContent(name, data={}, objects={}, expectResponse=true) {
  info("Sending message " + name + " to content");
  let mm = gBrowser.selectedBrowser.messageManager;

  mm.sendAsyncMessage(name, data, objects);
  if (expectResponse) {
    return waitForContentMessage(name);
  }

  return promise.resolve();
}

/**
 * Send an async message to the frame script and get back the requested
 * computed style property.
 *
 * @param {String} selector
 *        The selector used to obtain the element.
 * @param {String} pseudo
 *        pseudo id to query, or null.
 * @param {String} name
 *        name of the property.
 */
function* getComputedStyleProperty(selector, pseudo, propName) {
  return yield executeInContent("Test:GetComputedStylePropertyValue",
                                {selector,
                                pseudo,
                                name: propName});
}

/**
 * Send an async message to the frame script and wait until the requested
 * computed style property has the expected value.
 *
 * @param {String} selector
 *        The selector used to obtain the element.
 * @param {String} pseudo
 *        pseudo id to query, or null.
 * @param {String} prop
 *        name of the property.
 * @param {String} expected
 *        expected value of property
 * @param {String} name
 *        the name used in test message
 */
function* waitForComputedStyleProperty(selector, pseudo, name, expected) {
  return yield executeInContent("Test:WaitForComputedStylePropertyValue",
                                {selector,
                                pseudo,
                                expected,
                                name});
}

/**
 * Given an inplace editable element, click to switch it to edit mode, wait for
 * focus
 *
 * @return a promise that resolves to the inplace-editor element when ready
 */
var focusEditableField = Task.async(function*(ruleView, editable, xOffset=1,
    yOffset=1, options={}) {
  let onFocus = once(editable.parentNode, "focus", true);
  info("Clicking on editable field to turn to edit mode");
  EventUtils.synthesizeMouse(editable, xOffset, yOffset, options,
    editable.ownerDocument.defaultView);
  yield onFocus;

  info("Editable field gained focus, returning the input field now");
  let onEdit = inplaceEditor(editable.ownerDocument.activeElement);

  return onEdit;
});

/**
 * Given a tooltip object instance (see Tooltip.js), checks if it is set to
 * toggle and hover and if so, checks if the given target is a valid hover
 * target. This won't actually show the tooltip (the less we interact with XUL
 * panels during test runs, the better).
 *
 * @return a promise that resolves when the answer is known
 */
function isHoverTooltipTarget(tooltip, target) {
  if (!tooltip._basedNode || !tooltip.panel) {
    return promise.reject(new Error(
      "The tooltip passed isn't set to toggle on hover or is not a tooltip"));
  }
  return tooltip.isValidHoverTarget(target);
}

/**
 * Same as isHoverTooltipTarget except that it will fail the test if there is no
 * tooltip defined on hover of the given element
 *
 * @return a promise
 */
function assertHoverTooltipOn(tooltip, element) {
  return isHoverTooltipTarget(tooltip, element).then(() => {
    ok(true, "A tooltip is defined on hover of the given element");
  }, () => {
    ok(false, "No tooltip is defined on hover of the given element");
  });
}

/**
 * Listen for a new tab to open and return a promise that resolves when one
 * does and completes the load event.
 *
 * @return a promise that resolves to the tab object
 */
var waitForTab = Task.async(function*() {
  info("Waiting for a tab to open");
  yield once(gBrowser.tabContainer, "TabOpen");
  let tab = gBrowser.selectedTab;
  let browser = tab.linkedBrowser;
  yield once(browser, "load", true);
  info("The tab load completed");
  return tab;
});

/**
 * @see SimpleTest.waitForClipboard
 *
 * @param {Function} setup
 *        Function to execute before checking for the
 *        clipboard content
 * @param {String|Boolean} expected
 *        An expected string or validator function
 * @return a promise that resolves when the expected string has been found or
 * the validator function has returned true, rejects otherwise.
 */
function waitForClipboard(setup, expected) {
  let def = promise.defer();
  SimpleTest.waitForClipboard(expected, setup, def.resolve, def.reject);
  return def.promise;
}

/**
 * Polls a given function waiting for it to return true.
 *
 * @param {Function} validatorFn
 *        A validator function that returns a boolean.
 *        This is called every few milliseconds to check if the result is true.
 *        When it is true, the promise resolves.
 * @param {String} name
 *        Optional name of the test. This is used to generate
 *        the success and failure messages.
 * @return a promise that resolves when the function returned true or rejects
 * if the timeout is reached
 */
function waitForSuccess(validatorFn, name="untitled") {
  let def = promise.defer();

  function wait(validator) {
    if (validator()) {
      ok(true, "Validator function " + name + " returned true");
      def.resolve();
    } else {
      setTimeout(() => wait(validator), 200);
    }
  }
  wait(validatorFn);

  return def.promise;
}

/**
 * Create a new style tag containing the given style text and append it to the
 * document's head node
 *
 * @param {Document} doc
 * @param {String} style
 * @return {DOMNode} The newly created style node
 */
function addStyle(doc, style) {
  info("Adding a new style tag to the document with style content: " +
    style.substring(0, 50));
  let node = doc.createElement("style");
  node.setAttribute("type", "text/css");
  node.textContent = style;
  doc.getElementsByTagName("head")[0].appendChild(node);
  return node;
}

/**
 * Checks whether the inspector's sidebar corresponding to the given id already
 * exists
 *
 * @param {InspectorPanel}
 * @param {String}
 * @return {Boolean}
 */
function hasSideBarTab(inspector, id) {
  return !!inspector.sidebar.getWindowForTab(id);
}

/**
 * Get the dataURL for the font family tooltip.
 *
 * @param {String} font
 *        The font family value.
 * @param {object} nodeFront
 *        The NodeActor that will used to retrieve the dataURL for the
 *        font family tooltip contents.
 */
var getFontFamilyDataURL = Task.async(function*(font, nodeFront) {
  let fillStyle = (Services.prefs.getCharPref("devtools.theme") === "light") ?
      "black" : "white";

  let {data} = yield nodeFront.getFontFamilyDataURL(font, fillStyle);
  let dataURL = yield data.string();
  return dataURL;
});

/**
 * Simulate the key input for the given input in the window.
 *
 * @param {String} input
 *        The string value to input
 * @param {Window} win
 *        The window containing the panel
 */
function synthesizeKeys(input, win) {
  for (let key of input.split("")) {
    EventUtils.synthesizeKey(key, {}, win);
  }
}

/* *********************************************
 * RULE-VIEW
 * *********************************************
 * Rule-view related test utility functions
 * This object contains functions to get rules, get properties, ...
 */

/**
 * Get the DOMNode for a css rule in the rule-view that corresponds to the given
 * selector
 *
 * @param {CssRuleView} view
 *        The instance of the rule-view panel
 * @param {String} selectorText
 *        The selector in the rule-view for which the rule
 *        object is wanted
 * @return {DOMNode}
 */
function getRuleViewRule(view, selectorText) {
  let rule;
  for (let r of view.styleDocument.querySelectorAll(".ruleview-rule")) {
    let selector = r.querySelector(".ruleview-selectorcontainer, " +
                                   ".ruleview-selector-matched");
    if (selector && selector.textContent === selectorText) {
      rule = r;
      break;
    }
  }

  return rule;
}

/**
 * Get references to the name and value span nodes corresponding to a given
 * selector and property name in the rule-view
 *
 * @param {CssRuleView} view
 *        The instance of the rule-view panel
 * @param {String} selectorText
 *        The selector in the rule-view to look for the property in
 * @param {String} propertyName
 *        The name of the property
 * @return {Object} An object like {nameSpan: DOMNode, valueSpan: DOMNode}
 */
function getRuleViewProperty(view, selectorText, propertyName) {
  let prop;

  let rule = getRuleViewRule(view, selectorText);
  if (rule) {
    // Look for the propertyName in that rule element
    for (let p of rule.querySelectorAll(".ruleview-property")) {
      let nameSpan = p.querySelector(".ruleview-propertyname");
      let valueSpan = p.querySelector(".ruleview-propertyvalue");

      if (nameSpan.textContent === propertyName) {
        prop = {nameSpan: nameSpan, valueSpan: valueSpan};
        break;
      }
    }
  }
  return prop;
}

/**
 * Get the text value of the property corresponding to a given selector and name
 * in the rule-view
 *
 * @param {CssRuleView} view
 *        The instance of the rule-view panel
 * @param {String} selectorText
 *        The selector in the rule-view to look for the property in
 * @param {String} propertyName
 *        The name of the property
 * @return {String} The property value
 */
function getRuleViewPropertyValue(view, selectorText, propertyName) {
  return getRuleViewProperty(view, selectorText, propertyName)
    .valueSpan.textContent;
}

/**
 * Get a reference to the selector DOM element corresponding to a given selector
 * in the rule-view
 *
 * @param {CssRuleView} view
 *        The instance of the rule-view panel
 * @param {String} selectorText
 *        The selector in the rule-view to look for
 * @return {DOMNode} The selector DOM element
 */
function getRuleViewSelector(view, selectorText) {
  let rule = getRuleViewRule(view, selectorText);
  return rule.querySelector(".ruleview-selector, .ruleview-selector-matched");
}

/**
 * Simulate a color change in a given color picker tooltip, and optionally wait
 * for a given element in the page to have its style changed as a result
 *
 * @param {RuleView} ruleView
 *        The related rule view instance
 * @param {SwatchColorPickerTooltip} colorPicker
 * @param {Array} newRgba
 *        The new color to be set [r, g, b, a]
 * @param {Object} expectedChange
 *        Optional object that needs the following props:
 *          - {DOMNode} element The element in the page that will have its
 *            style changed.
 *          - {String} name The style name that will be changed
 *          - {String} value The expected style value
 * The style will be checked like so: getComputedStyle(element)[name] === value
 */
var simulateColorPickerChange = Task.async(function*(ruleView, colorPicker,
    newRgba, expectedChange) {
  let onRuleViewChanged = ruleView.once("ruleview-changed");
  info("Getting the spectrum colorpicker object");
  let spectrum = yield colorPicker.spectrum;
  info("Setting the new color");
  spectrum.rgb = newRgba;
  info("Applying the change");
  spectrum.updateUI();
  spectrum.onChange();
  info("Waiting for rule-view to update");
  yield onRuleViewChanged;

  if (expectedChange) {
    info("Waiting for the style to be applied on the page");
    yield waitForSuccess(() => {
      let {element, name, value} = expectedChange;
      return content.getComputedStyle(element)[name] === value;
    }, "Color picker change applied on the page");
  }
});

/**
 * Get a rule-link from the rule-view given its index
 *
 * @param {CssRuleView} view
 *        The instance of the rule-view panel
 * @param {Number} index
 *        The index of the link to get
 * @return {DOMNode} The link if any at this index
 */
function getRuleViewLinkByIndex(view, index) {
  let links = view.styleDocument.querySelectorAll(".ruleview-rule-source");
  return links[index];
}

/**
 * Get rule-link text from the rule-view given its index
 *
 * @param {CssRuleView} view
 *        The instance of the rule-view panel
 * @param {Number} index
 *        The index of the link to get
 * @return {String} The string at this index
 */
function getRuleViewLinkTextByIndex(view, index) {
  let link = getRuleViewLinkByIndex(view, index);
  return link.querySelector(".ruleview-rule-source-label").value;
}

/**
 * Get the rule editor from the rule-view given its index
 *
 * @param {CssRuleView} view
 *        The instance of the rule-view panel
 * @param {Number} childrenIndex
 *        The children index of the element to get
 * @param {Number} nodeIndex
 *        The child node index of the element to get
 * @return {DOMNode} The rule editor if any at this index
 */
function getRuleViewRuleEditor(view, childrenIndex, nodeIndex) {
  return nodeIndex !== undefined ?
    view.element.children[childrenIndex].childNodes[nodeIndex]._ruleEditor :
    view.element.children[childrenIndex]._ruleEditor;
}

/**
 * Click on a rule-view's close brace to focus a new property name editor
 *
 * @param {RuleEditor} ruleEditor
 *        An instance of RuleEditor that will receive the new property
 * @return a promise that resolves to the newly created editor when ready and
 * focused
 */
var focusNewRuleViewProperty = Task.async(function*(ruleEditor) {
  info("Clicking on a close ruleEditor brace to start editing a new property");
  ruleEditor.closeBrace.scrollIntoView();
  let editor = yield focusEditableField(ruleEditor.ruleView,
    ruleEditor.closeBrace);

  is(inplaceEditor(ruleEditor.newPropSpan), editor,
    "Focused editor is the new property editor.");

  return editor;
});

/**
 * Create a new property name in the rule-view, focusing a new property editor
 * by clicking on the close brace, and then entering the given text.
 * Keep in mind that the rule-view knows how to handle strings with multiple
 * properties, so the input text may be like: "p1:v1;p2:v2;p3:v3".
 *
 * @param {RuleEditor} ruleEditor
 *        The instance of RuleEditor that will receive the new property(ies)
 * @param {String} inputValue
 *        The text to be entered in the new property name field
 * @return a promise that resolves when the new property name has been entered
 * and once the value field is focused
 */
var createNewRuleViewProperty = Task.async(function*(ruleEditor, inputValue) {
  info("Creating a new property editor");
  let editor = yield focusNewRuleViewProperty(ruleEditor);

  info("Entering the value " + inputValue);
  editor.input.value = inputValue;

  info("Submitting the new value and waiting for value field focus");
  let onFocus = once(ruleEditor.element, "focus", true);
  EventUtils.synthesizeKey("VK_RETURN", {},
    ruleEditor.element.ownerDocument.defaultView);
  yield onFocus;
});

/**
 * Set the search value for the rule-view filter styles search box.
 *
 * @param {CssRuleView} view
 *        The instance of the rule-view panel
 * @param {String} searchValue
 *        The filter search value
 * @return a promise that resolves when the rule-view is filtered for the
 * search term
 */
var setSearchFilter = Task.async(function*(view, searchValue) {
  info("Setting filter text to \"" + searchValue + "\"");
  let win = view.styleWindow;
  let searchField = view.searchField;
  searchField.focus();
  synthesizeKeys(searchValue, win);
  yield view.inspector.once("ruleview-filtered");
});

/* *********************************************
 * COMPUTED-VIEW
 * *********************************************
 * Computed-view related utility functions.
 * Allows to get properties, links, expand properties, ...
 */

/**
 * Get references to the name and value span nodes corresponding to a given
 * property name in the computed-view
 *
 * @param {CssComputedView} view
 *        The instance of the computed view panel
 * @param {String} name
 *        The name of the property to retrieve
 * @return an object {nameSpan, valueSpan}
 */
function getComputedViewProperty(view, name) {
  let prop;
  for (let property of view.styleDocument.querySelectorAll(".property-view")) {
    let nameSpan = property.querySelector(".property-name");
    let valueSpan = property.querySelector(".property-value");

    if (nameSpan.textContent === name) {
      prop = {nameSpan: nameSpan, valueSpan: valueSpan};
      break;
    }
  }
  return prop;
}

/**
 * Get the text value of the property corresponding to a given name in the
 * computed-view
 *
 * @param {CssComputedView} view
 *        The instance of the computed view panel
 * @param {String} name
 *        The name of the property to retrieve
 * @return {String} The property value
 */
function getComputedViewPropertyValue(view, name, propertyName) {
  return getComputedViewProperty(view, name, propertyName)
    .valueSpan.textContent;
}

/* *********************************************
 * STYLE-EDITOR
 * *********************************************
 * Style-editor related utility functions.
 */

/**
 * Wait for the toolbox to emit the styleeditor-selected event and when done
 * wait for the stylesheet identified by href to be loaded in the stylesheet
 * editor
 *
 * @param {Toolbox} toolbox
 * @param {String} href
 *        Optional, if not provided, wait for the first editor to be ready
 * @return a promise that resolves to the editor when the stylesheet editor is
 * ready
 */
function waitForStyleEditor(toolbox, href) {
  let def = promise.defer();

  info("Waiting for the toolbox to switch to the styleeditor");
  toolbox.once("styleeditor-ready").then(() => {
    let panel = toolbox.getCurrentPanel();
    ok(panel && panel.UI, "Styleeditor panel switched to front");

    panel.UI.on("editor-selected", function onEditorSelected(event, editor) {
      let currentHref = editor.styleSheet.href;
      if (!href || (href && currentHref.endsWith(href))) {
        info("Stylesheet editor selected");
        panel.UI.off("editor-selected", onEditorSelected);
        editor.getSourceEditor().then(editor => {
          info("Stylesheet editor fully loaded");
          def.resolve(editor);
        });
      }
    });
  });

  return def.promise;
}

/**
 * Reload the current page and wait for the inspector to be initialized after
 * the navigation
 *
 * @param {InspectorPanel} inspector
 *        The instance of InspectorPanel currently loaded in the toolbox
 * @return a promise that resolves after page reload and inspector
 * initialization
 */
function reloadPage(inspector) {
  let onNewRoot = inspector.once("new-root");
  content.location.reload();
  return onNewRoot.then(() => {
    inspector.markup._waitForChildren();
  });
}

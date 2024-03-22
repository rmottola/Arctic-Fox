/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/* All top-level definitions here are exports.  */
/* eslint no-unused-vars: [2, {"vars": "local"}] */

"use strict";

const FRAME_SCRIPT_UTILS_URL =
      "chrome://devtools/content/shared/frame-script-utils.js";
const TEST_BASE =
      "chrome://mochitests/content/browser/devtools/client/styleeditor/test/";
const TEST_BASE_HTTP =
      "http://example.com/browser/devtools/client/styleeditor/test/";
const TEST_BASE_HTTPS =
      "https://example.com/browser/devtools/client/styleeditor/test/";
const TEST_HOST = "mochi.test:8888";

const EDITOR_FRAME_SCRIPT = getRootDirectory(gTestPath) + "doc_frame_script.js";

var {require} = Cu.import("resource://devtools/shared/Loader.jsm", {});
var {TargetFactory} = require("devtools/client/framework/target");
var promise = require("promise");
var DevToolsUtils = require("devtools/shared/DevToolsUtils");

DevToolsUtils.testing = true;
SimpleTest.registerCleanupFunction(() => {
  DevToolsUtils.testing = false;
});

/**
 * Add a new test tab in the browser and load the given url.
 * @param {String} url The url to be loaded in the new tab
 * @param {Window} win The window to add the tab to (default: current window).
 * @return a promise that resolves to the tab object when the url is loaded
 */
function addTab(url, win) {
  info("Adding a new tab with URL: '" + url + "'");
  let def = promise.defer();

  let targetWindow = win || window;
  let targetBrowser = targetWindow.gBrowser;

  let tab = targetBrowser.selectedTab = targetBrowser.addTab(url);
  targetBrowser.selectedBrowser.addEventListener("load", function onload() {
    targetBrowser.selectedBrowser.removeEventListener("load", onload, true);
    info("URL '" + url + "' loading complete");
    def.resolve(tab);
  }, true);

  return def.promise;
}

/**
 * Navigate the currently selected tab to a new URL and wait for it to load.
 * @param {String} url The url to be loaded in the current tab.
 * @return a promise that resolves when the page has fully loaded.
 */
function navigateTo(url) {
  let navigating = promise.defer();
  gBrowser.selectedBrowser.addEventListener("load", function onload() {
    gBrowser.selectedBrowser.removeEventListener("load", onload, true);
    navigating.resolve();
  }, true);
  content.location = url;
  return navigating.promise;
}

function* cleanup() {
  while (gBrowser.tabs.length > 1) {
    let target = TargetFactory.forTab(gBrowser.selectedTab);
    yield gDevTools.closeToolbox(target);

    gBrowser.removeCurrentTab();
  }
}

/**
 * Open the style editor for the current tab.
 */
var openStyleEditor = Task.async(function*(tab) {
  if (!tab) {
    tab = gBrowser.selectedTab;
  }
  let target = TargetFactory.forTab(tab);
  let toolbox = yield gDevTools.showToolbox(target, "styleeditor");
  let panel = toolbox.getPanel("styleeditor");
  let ui = panel.UI;

  return { toolbox, panel, ui };
});

/**
 * Creates a new tab in specified window navigates it to the given URL and
 * opens style editor in it.
 */
var openStyleEditorForURL = Task.async(function* (url, win) {
  let tab = yield addTab(url, win);

  gBrowser.selectedBrowser.messageManager.loadFrameScript(EDITOR_FRAME_SCRIPT,
                                                          false);

  let result = yield openStyleEditor(tab);
  result.tab = tab;
  return result;
});

/**
 * Loads shared/frame-script-utils.js in the specified tab.
 *
 * @param tab
 *        Optional tab to load the frame script in. Defaults to the current tab.
 */
function loadCommonFrameScript(tab) {
  let browser = tab ? tab.linkedBrowser : gBrowser.selectedBrowser;

  browser.messageManager.loadFrameScript(FRAME_SCRIPT_UTILS_URL, false);
}

/**
 * Send an async message to the frame script (chrome -> content) and wait for a
 * response message with the same name (content -> chrome).
 *
 * @param String name
 *        The message name. Should be one of the messages defined
 *        shared/frame-script-utils.js
 * @param Object data
 *        Optional data to send along
 * @param Object objects
 *        Optional CPOW objects to send along
 * @param Boolean expectResponse
 *        If set to false, don't wait for a response with the same name from the
 *        content script. Defaults to true.
 *
 * @return Promise
 *         Resolves to the response data if a response is expected, immediately
 *         resolves otherwise
 */
function executeInContent(name, data = {}, objects = {},
                          expectResponse = true) {
  let mm = gBrowser.selectedBrowser.messageManager;

  mm.sendAsyncMessage(name, data, objects);
  if (expectResponse) {
    return waitForContentMessage(name);
  }
  return promise.resolve();
}

/**
 * Wait for a content -> chrome message on the message manager (the window
 * messagemanager is used).
 * @param {String} name The message name
 * @return {Promise} A promise that resolves to the response data when the
 * message has been received
 */
function waitForContentMessage(name) {
  let mm = gBrowser.selectedBrowser.messageManager;

  let def = promise.defer();
  mm.addMessageListener(name, function onMessage(msg) {
    mm.removeMessageListener(name, onMessage);
    def.resolve(msg.data);
  });
  return def.promise;
}

registerCleanupFunction(cleanup);

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

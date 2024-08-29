/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* eslint no-unused-vars: [2, {"vars": "local"}] */
/* import-globals-from ../../../framework/test/shared-head.js */
/* import-globals-from ../../../framework/test/shared-redux-head.js */
/* import-globals-from ../../../commandline/test/helpers.js */

Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/devtools/client/framework/test/shared-head.js",
  this);
Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/devtools/client/framework/test/shared-redux-head.js",
  this);

// Import the GCLI test helper
Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/devtools/client/commandline/test/helpers.js",
  this);

const TEST_URI_ROOT = "http://example.com/browser/devtools/client/responsive.html/test/browser/";

SimpleTest.requestCompleteLog();
SimpleTest.waitForExplicitFinish();

DevToolsUtils.testing = true;
Services.prefs.setCharPref("devtools.devices.url",
  TEST_URI_ROOT + "devices.json");
Services.prefs.setBoolPref("devtools.responsive.html.enabled", true);

registerCleanupFunction(() => {
  DevToolsUtils.testing = false;
  Services.pref.clearUserPref("devtools.devices.url");
  Services.prefs.clearUserPref("devtools.responsive.html.enabled");
});
const { ResponsiveUIManager } = Cu.import("resource://devtools/client/responsivedesign/responsivedesign.jsm", {});

/**
 * Open responsive design mode for the given tab.
 */
var openRDM = Task.async(function* (tab) {
  info("Opening responsive design mode");
  let manager = ResponsiveUIManager;
  let ui = yield manager.openIfNeeded(window, tab);
  info("Responsive design mode opened");
  return { ui, manager };
});

/**
 * Close responsive design mode for the given tab.
 */
var closeRDM = Task.async(function* (tab) {
  info("Closing responsive design mode");
  let manager = ResponsiveUIManager;
  yield manager.closeIfNeeded(window, tab);
  info("Responsive design mode closed");
});

/**
 * Adds a new test task that adds a tab with the given URL, opens responsive
 * design mode, runs the given generator, closes responsive design mode, and
 * removes the tab.
 *
 * Example usage:
 *
 *   addRDMTask(TEST_URL, function*({ ui, manager }) {
 *     // Your tests go here...
 *   });
 */
function addRDMTask(url, generator) {
  add_task(function* () {
    const tab = yield addTab(url);
    const results = yield openRDM(tab);

    try {
      yield* generator(results);
    } catch (err) {
      ok(false, "Got an error: " + DevToolsUtils.safeErrorString(err));
    }

    yield closeRDM(tab);
    yield removeTab(tab);
  });
}

function spawnViewportTask(ui, args, task) {
  return ContentTask.spawn(ui.getViewportMessageManager(), args, task);
}

function waitForFrameLoad(ui, targetURL) {
  return spawnViewportTask(ui, { targetURL }, function* (args) {
    if ((content.document.readyState == "complete" ||
         content.document.readyState == "interactive") &&
        content.location.href == args.targetURL) {
      return;
    }
    yield ContentTaskUtils.waitForEvent(this, "DOMContentLoaded");
  });
}

function waitForViewportResizeTo(ui, width, height) {
  return new Promise(resolve => {
    let onResize = (_, data) => {
      if (data.width != width || data.height != height) {
        return;
      }
      ui.off("content-resize", onResize);
      info(`Got content-resize to ${width} x ${height}`);
      resolve();
    };
    info(`Waiting for content-resize to ${width} x ${height}`);
    ui.on("content-resize", onResize);
  });
}

var setViewportSize = Task.async(function* (ui, manager, width, height) {
  let size = ui.getViewportSize();
  info(`Current size: ${size.width} x ${size.height}, ` +
       `set to: ${width} x ${height}`);
  if (size.width != width || size.height != height) {
    let resized = waitForViewportResizeTo(ui, width, height);
    ui.setViewportSize(width, height);
    yield resized;
  }
});

/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Load the shared test helpers into this compartment.
Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/devtools/client/framework/test/shared-head.js",
  this);

// Load the shared Redux helpers into this compartment.
Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/devtools/client/framework/test/shared-redux-head.js",
  this);

var { snapshotState: states } = require("devtools/client/memory/constants");
var { breakdownEquals, breakdownNameToSpec } = require("devtools/client/memory/utils");

Services.prefs.setBoolPref("devtools.memory.enabled", true);

/**
 * Open the memory panel for the given tab.
 */
this.openMemoryPanel = Task.async(function* (tab) {
  info("Opening memory panel.");
  const target = TargetFactory.forTab(tab);
  const toolbox = yield gDevTools.showToolbox(target, "memory");
  info("Memory panel shown successfully.");
  let panel = toolbox.getCurrentPanel();
  return { tab, panel };
});

/**
 * Close the memory panel for the given tab.
 */
this.closeMemoryPanel = Task.async(function* (tab) {
  info("Closing memory panel.");
  const target = TargetFactory.forTab(tab);
  const toolbox = gDevTools.getToolbox(target);
  yield toolbox.destroy();
  info("Closed memory panel successfully.");
});

/**
 * Return a test function that adds a tab with the given url, opens the memory
 * panel, runs the given generator, closes the memory panel, removes the tab,
 * and finishes.
 *
 * Example usage:
 *
 *     this.test = makeMemoryTest(TEST_URL, function* ({ tab, panel }) {
 *         // Your tests go here...
 *     });
 */
function makeMemoryTest(url, generator) {
  return Task.async(function* () {
    waitForExplicitFinish();

    const tab = yield addTab(url);
    const results = yield openMemoryPanel(tab);

    try {
      yield* generator(results);
    } catch (err) {
      ok(false, "Got an error: " + DevToolsUtils.safeErrorString(err));
    }

    yield closeMemoryPanel(tab);
    yield removeTab(tab);

    finish();
  });
}

function dumpn(msg) {
  dump(`MEMORY-TEST: ${msg}\n`);
}

/**
 * Returns a promise that will resolve when the provided store matches
 * the expected array. expectedStates is an array of dominatorTree states.
 * Expectations :
 * - store.getState().snapshots.length == expected.length
 * - snapshots[i].dominatorTree.state == expected[i]
 *
 * @param  {Store} store
 * @param  {Array<string>} expectedStates [description]
 * @return {Promise}
 */
function waitUntilDominatorTreeState(store, expected) {
  let predicate = () => {
    let snapshots = store.getState().snapshots;
    return snapshots.length === expected.length &&
            expected.every((state, i) => {
              return snapshots[i].dominatorTree &&
              snapshots[i].dominatorTree.state === state;
            });
  };
  info(`Waiting for dominator trees to be of state: ${expected}`);
  return waitUntilState(store, predicate);
}

function takeSnapshot (window) {
  let { gStore, document } = window;
  let snapshotCount = gStore.getState().snapshots.length;
  info(`Taking snapshot...`);
  document.querySelector(".devtools-toolbar .take-snapshot").click();
  return waitUntilState(gStore, () => gStore.getState().snapshots.length === snapshotCount + 1);
}

function clearSnapshots (window) {
  let { gStore, document } = window;
  document.querySelector(".devtools-toolbar .clear-snapshots").click();
  return waitUntilState(gStore, () => gStore.getState().snapshots.every(
    (snapshot) => snapshot.state !== states.SAVED_CENSUS)
  );
}

/**
 * Sets breakdown and waits for currently selected breakdown to use it
 * and be completed the census.
 */
function setBreakdown (window, type) {
  info(`Setting breakdown to ${type}...`);
  let { gStore, gHeapAnalysesClient } = window;
  // XXX: Should handle this via clicking the DOM, but React doesn't
  // fire the onChange event, so just change it in the store.
  // window.document.querySelector(`.select-breakdown`).value = type;
  gStore.dispatch(require("devtools/client/memory/actions/breakdown")
                         .setBreakdownAndRefresh(gHeapAnalysesClient, breakdownNameToSpec(type)));

  return waitUntilState(window.gStore, () => {
    let selected = window.gStore.getState().snapshots.find(s => s.selected);
    return selected.state === states.SAVED_CENSUS &&
           breakdownEquals(breakdownNameToSpec(type), selected.census.breakdown);
  });
}

/**
 * Get the snapshot tatus text currently displayed, or null if none is
 * displayed.
 *
 * @param {Document} document
 */
function getDisplayedSnapshotStatus(document) {
  const status = document.querySelector(".snapshot-status");
  return status ? status.textContent.trim() : null;
}

/**
 * Get the index of the currently selected snapshot.
 *
 * @return {Number}
 */
function getSelectedSnapshotIndex(store) {
  let snapshots = store.getState().snapshots;
  let selectedSnapshot = snapshots.find(s => s.selected);
  return snapshots.indexOf(selectedSnapshot);
}

/**
 * Returns a promise that will resolve when the snapshot with provided index
 * becomes selected.
 *
 * @return {Promise}
 */
function waitUntilSnapshotSelected(store, snapshotIndex) {
  return waitUntilState(store, state =>
    state.snapshots[snapshotIndex] &&
    state.snapshots[snapshotIndex].selected === true);
}

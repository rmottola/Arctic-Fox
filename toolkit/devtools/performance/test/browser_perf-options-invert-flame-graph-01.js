/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Tests that the js Flamegraphs gets rerendered when toggling `invert-flame-graph`
 */
function spawnTest () {
  let { panel } = yield initPerformance(SIMPLE_URL);
  let { EVENTS, DetailsView, JsFlameGraphView } = panel.panelWin;

  Services.prefs.setBoolPref(INVERT_FLAME_PREF, true);

  yield startRecording(panel);
  yield busyWait(100);
  yield stopRecording(panel);

  let rendered = once(JsFlameGraphView, EVENTS.JS_FLAMEGRAPH_RENDERED);
  yield DetailsView.selectView("js-flamegraph");
  yield rendered;

  rendered = once(JsFlameGraphView, EVENTS.JS_FLAMEGRAPH_RENDERED);
  Services.prefs.setBoolPref(INVERT_FLAME_PREF, false);
  yield rendered;

  ok(true, "JsFlameGraphView rerendered when toggling invert-flame-graph.");

  rendered = once(JsFlameGraphView, EVENTS.JS_FLAMEGRAPH_RENDERED);
  Services.prefs.setBoolPref(INVERT_FLAME_PREF, true);
  yield rendered;

  ok(true, "JsFlameGraphView rerendered when toggling back invert-flame-graph.");

  yield teardown(panel);
  finish();
}

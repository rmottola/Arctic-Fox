/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* Any copyright is dedicated to the Public Domain.
 http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test for as-authored styles.

function* createTestContent(style) {
  let content = `<style type="text/css">
      ${style}
      </style>
      <div id="testid" class="testclass">Styled Node</div>`;
  yield addTab("data:text/html;charset=utf-8," + encodeURIComponent(content));

  let {inspector, view} = yield openRuleView();
  yield selectNode("#testid", inspector);
  return view;
}

add_task(function* () {
  let colors = [
    {name: "hex", text: "#f0c", result: "#0f0"},
    {name: "rgb", text: "rgb(0,128,250)", result: "rgb(0, 255, 0)"},
    // Test case preservation.
    {name: "hex", text: "#F0C", result: "#0F0"},
  ];

  Services.prefs.setCharPref("devtools.defaultColorUnit", "authored");

  for (let color of colors) {
    let view = yield createTestContent("#testid {" +
                                       "  color: " + color.text + ";" +
                                       "} ");

    let cPicker = view.tooltips.colorPicker;
    let swatch = getRuleViewProperty(view, "#testid", "color").valueSpan
        .querySelector(".ruleview-colorswatch");
    let onShown = cPicker.tooltip.once("shown");
    swatch.click();
    yield onShown;

    let testNode = yield getNode("#testid");

    yield simulateColorPickerChange(view, cPicker, [0, 255, 0, 1], {
      element: testNode,
      name: "color",
      value: "rgb(0, 255, 0)"
    });

    let spectrum = yield cPicker.spectrum;
    let onHidden = cPicker.tooltip.once("hidden");
    EventUtils.sendKey("RETURN", spectrum.element.ownerDocument.defaultView);
    yield onHidden;

    is(getRuleViewPropertyValue(view, "#testid", "color"), color.result,
       "changing the color preserved the unit for " + color.name);
  }
});

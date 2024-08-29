/* vim: set ts=2 et sw=2 tw=80: */
/* Any copyright is dedicated to the Public Domain.
 http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test that when animations displayed in the timeline are running on the
// compositor, they get a special icon and information in the tooltip.

const { LocalizationHelper } = require("devtools/client/shared/l10n");

const STRINGS_URI = "chrome://devtools/locale/animationinspector.properties";
const L10N = new LocalizationHelper(STRINGS_URI);

add_task(function* () {
  yield addTab(URL_ROOT + "doc_simple_animation.html");
  let {inspector, panel} = yield openAnimationInspector();
  let timeline = panel.animationsTimelineComponent;

  info("Select a test node we know has an animation running on the compositor");
  yield selectNodeAndWaitForAnimations(".animated", inspector);

  let animationEl = timeline.animationsEl.querySelector(".animation");
  ok(animationEl.classList.contains("fast-track"),
     "The animation element has the fast-track css class");
  ok(hasTooltip(animationEl),
     "The animation element has the right tooltip content");

  info("Select a node we know doesn't have an animation on the compositor");
  yield selectNodeAndWaitForAnimations(".no-compositor", inspector);

  animationEl = timeline.animationsEl.querySelector(".animation");
  ok(!animationEl.classList.contains("fast-track"),
     "The animation element does not have the fast-track css class");
  ok(!hasTooltip(animationEl),
     "The animation element has the right tooltip content");
});

function hasTooltip(animationEl) {
  let el = animationEl.querySelector(".name");
  let tooltip = el.getAttribute("title");

  let expected = L10N.getStr("player.runningOnCompositorTooltip");
  return tooltip.indexOf(expected) !== -1;
}

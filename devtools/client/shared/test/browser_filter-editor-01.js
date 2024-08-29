/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Tests that the Filter Editor Widget parses filter values correctly (setCssValue)

const TEST_URI = "chrome://devtools/content/shared/widgets/filter-frame.xhtml";
const {CSSFilterEditorWidget} = require("devtools/client/shared/widgets/FilterWidget");

add_task(function *() {
  yield addTab("about:blank");
  let [host, win, doc] = yield createHost("bottom", TEST_URI);

  const container = doc.querySelector("#container");
  let widget = new CSSFilterEditorWidget(container, "none");

  info("Test parsing of a valid CSS Filter value");
  widget.setCssValue("blur(2px) contrast(200%)");
  is(widget.getCssValue(),
     "blur(2px) contrast(200%)",
     "setCssValue should work for computed values");

  info("Test parsing of space-filled value");
  widget.setCssValue("blur(   2px  )   contrast(  2  )");
  is(widget.getCssValue(),
     "blur(2px) contrast(200%)",
     "setCssValue should work for spaced values");

  info("Test parsing of string-typed values");
  widget.setCssValue("drop-shadow( 2px  1px 5px black) url( example.svg#filter )");

  is(widget.getCssValue(),
     "drop-shadow(2px  1px 5px black) url(example.svg#filter)",
     "setCssValue should work for string-typed values");

  info("Test parsing of mixed-case function names");
  widget.setCssValue("BLUR(2px) Contrast(200%) Drop-Shadow(2px 1px 5px Black)");
  is(widget.getCssValue(),
     "BLUR(2px) Contrast(200%) Drop-Shadow(2px 1px 5px Black)",
     "setCssValue should work for mixed-case function names");

  info("Test parsing of invalid filter value");
  widget.setCssValue("totallyinvalid");
  is(widget.getCssValue(), "none",
     "setCssValue should turn completely invalid value to 'none'");

  info("Test parsing of invalid function argument");
  widget.setCssValue("blur('hello')");
  is(widget.getCssValue(), "blur(0px)",
     "setCssValue should replace invalid function argument with default");

  info("Test parsing of invalid function argument #2");
  widget.setCssValue("drop-shadow(whatever)");
  is(widget.getCssValue(), "drop-shadow()",
     "setCssValue should replace invalid drop-shadow argument with empty string");

  info("Test parsing of mixed invalid argument");
  widget.setCssValue("contrast(5%) whatever invert('xxx')");
  is(widget.getCssValue(), "contrast(5%) invert(0%)",
     "setCssValue should handle multiple errors");

  info("Test parsing of 'unset'");
  widget.setCssValue("unset");
  is(widget.getCssValue(), "unset", "setCssValue should handle 'unset'");
  info("Test parsing of 'initial'");
  widget.setCssValue("initial");
  is(widget.getCssValue(), "initial", "setCssValue should handle 'initial'");
  info("Test parsing of 'inherit'");
  widget.setCssValue("inherit");
  is(widget.getCssValue(), "inherit", "setCssValue should handle 'inherit'");
});

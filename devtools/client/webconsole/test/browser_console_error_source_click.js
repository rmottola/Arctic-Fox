/*
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

// Check that JS errors and CSS warnings open view source when their source link
// is clicked in the Browser Console. See bug 877778.

"use strict";

const TEST_URI = "data:text/html;charset=utf8,<p>hello world from bug 877778 " +
                 "<button onclick='foobar.explode()' " +
                 "style='test-color: green-please'>click!</button>";
function test() {
  let hud;

  loadTab(TEST_URI).then(() => {
    HUDService.toggleBrowserConsole().then(browserConsoleOpened);
  });

  function browserConsoleOpened(hudConsole) {
    hud = hudConsole;
    ok(hud, "browser console opened");

    let button = content.document.querySelector("button");
    ok(button, "button element found");

    info("generate exception and wait for the message");
    executeSoon(() => {
      expectUncaughtException();
      button.click();
    });

    waitForMessages({
      webconsole: hud,
      messages: [
        {
          text: "ReferenceError: foobar is not defined",
          category: CATEGORY_JS,
          severity: SEVERITY_ERROR,
        },
        {
          text: "Unknown property \u2018test-color\u2019",
          category: CATEGORY_CSS,
          severity: SEVERITY_WARNING,
        },
      ],
    }).then(onMessageFound);
  }

  function onMessageFound(results) {
    let viewSource = hud.viewSource;
    let viewSourceCalled = false;
    hud.viewSourceInDebugger = () => viewSourceCalled = true;

    for (let result of results) {
      viewSourceCalled = false;

      let msg = [...results[0].matched][0];
      ok(msg, "message element found for: " + result.text);
      let locationNode = msg.querySelector(".message > .message-location .frame-link-filename");
      ok(locationNode, "message location element found");

      EventUtils.synthesizeMouse(locationNode, 2, 2, {}, hud.iframeWindow);

      ok(viewSourceCalled, "view source opened");
    }

    hud.viewSourceInDebugger = viewSource;
    finishTest();
  }
}

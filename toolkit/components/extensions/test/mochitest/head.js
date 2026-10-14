"use strict";

/* exported waitForLoad */

function waitForLoad(win) {
  return new Promise(resolve => {
    win.addEventListener("load", function() {
      resolve();
    }, {capture: true, once: true});
  });
}


/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

this.EXPORTED_SYMBOLS = ["WindowSize"];

const {classes: Cc, interfaces: Ci, utils: Cu} = Components;

Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/Task.jsm");
Cu.import("resource://gre/modules/Timer.jsm");

this.WindowSize = {

  init(libDir) {
    Services.prefs.setBoolPref("browser.fullscreen.autohide", false);
  },

  configurations: {
    maximized: {
      applyConfig: Task.async(function*() {
        let browserWindow = Services.wm.getMostRecentWindow("navigator:browser");
        browserWindow.fullScreen = false;

        // Wait for the Lion fullscreen transition to end as there doesn't seem to be an event
        // and trying to maximize while still leaving fullscreen doesn't work.
        yield new Promise((resolve, reject) => {
          setTimeout(function waitToLeaveFS() {
            browserWindow.maximize();
            resolve();
          }, Services.appinfo.OS == "Darwin" ? 1500 : 0);
        });
      }),
    },

    normal: {
      applyConfig: Task.async(() => {
        let browserWindow = Services.wm.getMostRecentWindow("navigator:browser");
        browserWindow.fullScreen = false;
        browserWindow.restore();
      }),
    },

    fullScreen: {
      applyConfig: Task.async(function*() {
        let browserWindow = Services.wm.getMostRecentWindow("navigator:browser");
        browserWindow.fullScreen = true;
        // OS X Lion fullscreen transition takes a while
        yield new Promise((resolve, reject) => {
          setTimeout(function waitAfterEnteringFS() {
            resolve();
          }, Services.appinfo.OS == "Darwin" ? 1500 : 0);
        });
      }),
    },

  },
};

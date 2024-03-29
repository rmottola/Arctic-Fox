/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/**
 * This file only exists to support add-ons which import this module at a
 * specific path.
 */

const Cu = Components.utils;

const Services = require("Services");

const WARNING_PREF = "devtools.migration.warnings";
if (Services.prefs.getBoolPref(WARNING_PREF)) {
  const { Deprecated } = Cu.import("resource://gre/modules/Deprecated.jsm", {});
  Deprecated.warning("This path to gDevTools.jsm is deprecated.  Please use " +
                     "Cu.import(\"resource://devtools/client/" +
                     "framework/gDevTools.jsm\") to load this module.",
                     "https://bugzil.la/912121");
}

this.EXPORTED_SYMBOLS = [
  "gDevTools",
  "DevTools",
  "gDevToolsBrowser"
];

const module =
  Cu.import("resource://devtools/client/framework/gDevTools.jsm", {});

for (let symbol of this.EXPORTED_SYMBOLS) {
  this[symbol] = module[symbol];
}

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";
const {classes: Cc, interfaces: Ci, utils: Cu, results: Cr} = Components;

this.EXPORTED_SYMBOLS = ["Windows8WindowFrameColor"];

Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/XPCOMUtils.jsm");
var Registry = Cu.import("resource://gre/modules/WindowsRegistry.jsm").WindowsRegistry;

var Windows8WindowFrameColor = {
  _windowFrameColor: null,

  get_win8: function() {
    if (this._windowFrameColor)
      return this._windowFrameColor;
    
    let HKCU = Ci.nsIWindowsRegKey.ROOT_KEY_CURRENT_USER;
    let dwmKey = "Software\\Microsoft\\Windows\\DWM";

    // Window frame base color component values when Color Intensity is at 0.
    let frameBaseColor = 217;
    
    let windowFrameColor = WindowsRegistry.readRegKey(HKCU, dwmKey,
                                                      "ColorizationColor");
    if (windowFrameColor == undefined) {
      // Return the default color if unset or colorization not used
      return this._windowFrameColor = [frameBaseColor, frameBaseColor, frameBaseColor];
    }
    // The color returned from the Registry is in decimal form.
    let windowFrameColorHex = windowFrameColor.toString(16);
    // Zero-pad the number just to make sure that it is 8 digits.
    windowFrameColorHex = ("00000000" + windowFrameColorHex).substr(-8);
    let windowFrameColorArray = windowFrameColorHex.match(/../g);
    let [unused, fgR, fgG, fgB] = windowFrameColorArray.map(function(val) parseInt(val, 16));
    let windowFrameColorBalance = WindowsRegistry.readRegKey(HKCU, dwmKey,
                                                             "ColorizationColorBalance");
    // Default to balance=78 if reg key isn't defined
    if (windowFrameColorBalance == undefined) {
      windowFrameColorBalance = 78;
    }
    let alpha = windowFrameColorBalance / 100;

    // Alpha-blend the foreground color with the frame base color.
    let r = Math.round(fgR * alpha + frameBaseColor * (1 - alpha));
    let g = Math.round(fgG * alpha + frameBaseColor * (1 - alpha));
    let b = Math.round(fgB * alpha + frameBaseColor * (1 - alpha));
    return this._windowFrameColor = [r, g, b];
  }
};

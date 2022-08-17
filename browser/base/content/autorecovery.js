/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Auto-recovery module.
 * This module aims to catch fatal browser initialization errors and either
 * automatically correct likely causes from them, or automatically restarting
 * the browser in safe mode. This is hooked into the browser's "onload"
 * event because it can be assumed that at that point, everything must
 * have been properly initialized already.
 */

var Cc = Components.classes;
var Ci = Components.interfaces;
var Cu = Components.utils;

// Services = object with smart getters for common XPCOM services
Cu.import("resource://gre/modules/Services.jsm");

var browser_autoRecovery =
{
  onLoad: function() {
  
    var nsIAS = Ci.nsIAppStartup; // Application startup interface
    
    if (typeof gBrowser === "undefined") {
      // gBrowser should always be defined at this point, but if it is not, then most likely 
      // it is due to an incompatible or outdated language pack being installed and selected.
      // In this case, we reset "general.useragent.locale" to try to recover browser startup.
      if (Services.prefs.prefHasUserValue("general.useragent.locale")) {
        // Restart automatically in en-US.
        Services.prefs.clearUserPref("general.useragent.locale");
        Cc["@mozilla.org/toolkit/app-startup;1"].getService(nsIAS).quit(nsIAS.eRestart | nsIAS.eAttemptQuit);
      } else if (!Services.appinfo.inSafeMode) {
        // gBrowser isn't defined, and we're not using a custom locale. Most likely
        // a user-installed add-on causes issues here, so we restart in Safe Mode.
        let RISM = Services.prompt.confirm(null, "Error",
                              "The Browser didn't start properly!\n"+
                              "This is usually caused by an add-on or misconfiguration.\n\n"+
                              "Restart in Safe Mode?");
        if (RISM) {
          Cc["@mozilla.org/toolkit/app-startup;1"].getService(nsIAS).restartInSafeMode(nsIAS.eRestart | nsIAS.eAttemptQuit);
        } else {
          // Force quit application
          Cc["@mozilla.org/toolkit/app-startup;1"].getService(nsIAS).quit(nsIAS.eForceQuit);
        }
      }
      // Something else caused this issue and we're already in Safe Mode, so we return
      // without doing anything else, and let normal error handling take place.
      return;
    } // gBrowser undefined
    
    // Other checks than gBrowser undefined can go here!
    
    // Remove our listener, since we don't want this to fire on every load.
    window.removeEventListener("load", browser_autoRecovery.onLoad, false);
  }
};

window.addEventListener("load", browser_autoRecovery.onLoad, false);

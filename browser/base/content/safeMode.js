/* This Source Code Form is subject to the terms of the Mozilla Public
   License, v. 2.0. If a copy of the MPL was not distributed with this
   file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const Cc = Components.classes,
      Ci = Components.interfaces,
      Cu = Components.utils;
      
Cu.import("resource://gre/modules/AddonManager.jsm");

function restartApp() {
  let appStartup = Cc["@mozilla.org/toolkit/app-startup;1"]
                      .getService(Ci.nsIAppStartup);
  appStartup.quit(Ci.nsIAppStartup.eForceQuit |  Ci.nsIAppStartup.eRestart);
}

function clearAllPrefs() {
  var prefService = Cc["@mozilla.org/preferences-service;1"]
                       .getService(Ci.nsIPrefService);
  prefService.resetUserPrefs();

  // Remove the pref-overrides dir, if it exists
  try {
    var fileLocator = Cc["@mozilla.org/file/directory_service;1"]
                         .getService(Ci.nsIProperties);
    const NS_APP_PREFS_OVERRIDE_DIR = "PrefDOverride";
    var prefOverridesDir = fileLocator.get(NS_APP_PREFS_OVERRIDE_DIR,
                                           Ci.nsIFile);
    prefOverridesDir.remove(true);
  } catch (ex) {
    Components.utils.reportError(ex);
  }
}

function restoreDefaultBookmarks() {
  var prefBranch  = Cc["@mozilla.org/preferences-service;1"]
                       .getService(Ci.nsIPrefBranch);
  prefBranch.setBoolPref("browser.bookmarks.restore_default_bookmarks", true);
}

function deleteLocalstore() {
  const nsIDirectoryServiceContractID = "@mozilla.org/file/directory_service;1";
  const nsIProperties = Ci.nsIProperties;
  var directoryService = Cc[nsIDirectoryServiceContractID]
                            .getService(nsIProperties);
  // Local store file
  var localstoreFile = directoryService.get("LStoreS", Components.interfaces.nsIFile);
  // XUL store file
  var xulstoreFile = directoryService.get("ProfD", Components.interfaces.nsIFile);
  xulstoreFile.append("xulstore.json");
  try {
    xulstoreFile.remove(false);
    if (localstoreFile.exists()) {
      localstoreFile.remove(false);
    }
  } catch(e) {
    Components.utils.reportError(e);
  }
}

function disableAddons() {
  AddonManager.getAllAddons(function(aAddons) {
    aAddons.forEach(function(aAddon) {
      if (aAddon.type == "theme") {
        // Setting userDisabled to false on the default theme activates it,
        // disables all other themes and deactivates the applied persona, if
        // any.
        const DEFAULT_THEME_ID = "{972ce4c6-7e08-4474-a285-3208198ce6fd}";
        if (aAddon.id == DEFAULT_THEME_ID)
          aAddon.userDisabled = false;
      }
      else {
        aAddon.userDisabled = true;
      }
    });

    restartApp();
  });
}

function restoreDefaultSearchEngines() {
  var searchService = Cc["@mozilla.org/browser/search-service;1"]
                         .getService(Ci.nsIBrowserSearchService);

  searchService.restoreDefaultEngines();
}

function onOK() {
  try {
    if (document.getElementById("resetUserPrefs").checked)
      clearAllPrefs();
    if (document.getElementById("deleteBookmarks").checked)
      restoreDefaultBookmarks();
    if (document.getElementById("resetToolbars").checked)
      deleteLocalstore();
    if (document.getElementById("restoreSearch").checked)
      restoreDefaultSearchEngines();
    if (document.getElementById("disableAddons").checked) {
      disableAddons();
      // disableAddons will asynchronously restart the application
      return false;
    }
  } catch(e) {
  }

  restartApp();
  return false;
}

function onCancel() {
  let appStartup = Cc["@mozilla.org/toolkit/app-startup;1"]
                      .getService(Ci.nsIAppStartup);
  appStartup.quit(Ci.nsIAppStartup.eForceQuit);
}

function onLoad() {
  document.getElementById("tasks")
          .addEventListener("CheckboxStateChange", UpdateOKButtonState, false);
}

function UpdateOKButtonState() {
  document.documentElement.getButton("accept").disabled = 
    !document.getElementById("resetUserPrefs").checked &&
    !document.getElementById("deleteBookmarks").checked &&
    !document.getElementById("resetToolbars").checked &&
    !document.getElementById("disableAddons").checked &&
    !document.getElementById("restoreSearch").checked;
}

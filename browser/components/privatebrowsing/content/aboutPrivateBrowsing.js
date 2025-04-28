/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

var {classes: Cc, interfaces: Ci, utils: Cu} = Components;

Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/PrivateBrowsingUtils.jsm");
Cu.import("resource://gre/modules/XPCOMUtils.jsm");

var stringBundle = Services.strings.createBundle(
                    "chrome://browser/locale/aboutPrivateBrowsing.properties");

if (!PrivateBrowsingUtils.isWindowPrivate(window)) {
  document.title = stringBundle.GetStringFromName("title.normal");
  setFavIcon("chrome://global/skin/icons/question-16.png");
} else {
  document.title = stringBundle.GetStringFromName("title");
  setFavIcon("chrome://browser/skin/Privacy-16.png");
}

var prefBranch = Services.prefs.getBranch("privacy.trackingprotection.pbmode.");
let prefObserver = {
  QueryInterface: XPCOMUtils.generateQI([Ci.nsIObserver,
                                         Ci.nsISupportsWeakReference]),
  observe: function () {
    if (prefBranch.getBoolPref("enabled")) {
      document.body.setAttribute("tpEnabled", "true");
    } else {
      document.body.removeAttribute("tpEnabled");
    }
  },
};
prefBranch.addObserver("enabled", prefObserver, true);

function setFavIcon(url) {
  var icon = document.createElement("link");
  icon.setAttribute("rel", "icon");
  icon.setAttribute("type", "image/png");
  icon.setAttribute("href", url);
  var head = document.getElementsByTagName("head")[0];
  head.insertBefore(icon, head.firstChild);
}

document.addEventListener("DOMContentLoaded", function () {
  if (!PrivateBrowsingUtils.isWindowPrivate(window)) {
    document.body.setAttribute("class", "normal");
  }

  // Set up the help link
  let learnMoreURL = Cc["@mozilla.org/toolkit/URLFormatterService;1"]
                     .getService(Ci.nsIURLFormatter)
                     .formatURLPref("app.support.baseURL");
  let learnMore = document.getElementById("learnMore");
  if (learnMore) {
    learnMore.setAttribute("href", learnMoreURL + "private-browsing");
  }
  
 document.getElementById("startTour").setAttribute("href",
                    formatURLPref("privacy.trackingprotection.introURL"));
 document.getElementById("learnMore").setAttribute("href",
                    formatURLPref("app.support.baseURL") + "private-browsing");

}, false);

function openPrivateWindow() {
  // Ask chrome to open a private window
  document.dispatchEvent(
    new CustomEvent("AboutPrivateBrowsingOpenWindow", {bubbles:true}));
}

function enableTrackingProtection() {
  // Ask chrome to enable tracking protection
  document.dispatchEvent(
    new CustomEvent("AboutPrivateBrowsingEnableTrackingProtection",
                    {bubbles:true}));
}

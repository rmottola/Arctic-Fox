// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

"use strict";

this.EXPORTED_SYMBOLS = ["BrowserUITelemetry"];

const {interfaces: Ci, utils: Cu} = Components;

Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/XPCOMUtils.jsm");

XPCOMUtils.defineLazyModuleGetter(this, "UITelemetry",
  "resource://gre/modules/UITelemetry.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "RecentWindow",
  "resource:///modules/RecentWindow.jsm");
//XPCOMUtils.defineLazyModuleGetter(this, "CustomizableUI",
//  "resource:///modules/CustomizableUI.jsm");

const MS_SECOND = 1000;
const MS_MINUTE = MS_SECOND * 60;
const MS_HOUR = MS_MINUTE * 60;

XPCOMUtils.defineLazyGetter(this, "DEFAULT_AREA_PLACEMENTS", function() {
  let result = {
    "PanelUI-contents": [
      "edit-controls",
      "zoom-controls",
      "new-window-button",
      "privatebrowsing-button",
      "save-page-button",
      "print-button",
      "history-panelmenu",
      "fullscreen-button",
      "find-button",
      "preferences-button",
      "add-ons-button",
      "developer-button",
    ],
    "nav-bar": [
      "urlbar-container",
      "search-container",
      "bookmarks-menu-button",
      "pocket-button",
      "downloads-button",
      "home-button",
      "social-share-button",
    ],
    // It's true that toolbar-menubar is not visible
    // on OS X, but the XUL node is definitely present
    // in the document.
    "toolbar-menubar": [
      "menubar-items",
    ],
    "TabsToolbar": [
      "tabbrowser-tabs",
      "new-tab-button",
      "alltabs-button",
    ],
    "PersonalToolbar": [
      "personal-bookmarks",
    ],
  };

  let showCharacterEncoding = Services.prefs.getComplexValue(
    "browser.menu.showCharacterEncoding",
    Ci.nsIPrefLocalizedString
  ).data;
  if (showCharacterEncoding == "true") {
    result["PanelUI-contents"].push("characterencoding-button");
  }

  return result;
});

XPCOMUtils.defineLazyGetter(this, "DEFAULT_AREAS", function() {
  return Object.keys(DEFAULT_AREA_PLACEMENTS);
});

XPCOMUtils.defineLazyGetter(this, "PALETTE_ITEMS", function() {
  let result = [
    "open-file-button",
    "developer-button",
    "feed-button",
    "email-link-button",
    "sync-button",
    "tabview-button",
    "web-apps-button",
  ];

  let panelPlacements = DEFAULT_AREA_PLACEMENTS["PanelUI-contents"];
  if (panelPlacements.indexOf("characterencoding-button") == -1) {
    result.push("characterencoding-button");
  }

  if (Services.prefs.getBoolPref("privacy.panicButton.enabled")) {
    result.push("panic-button");
  }

  return result;
});

XPCOMUtils.defineLazyGetter(this, "DEFAULT_ITEMS", function() {
  let result = [];
  for (let [, buttons] of Iterator(DEFAULT_AREA_PLACEMENTS)) {
    result = result.concat(buttons);
  }
  return result;
});

XPCOMUtils.defineLazyGetter(this, "ALL_BUILTIN_ITEMS", function() {
  // These special cases are for click events on built-in items that are
  // contained within customizable items (like the navigation widget).
  const SPECIAL_CASES = [
    "back-button",
    "forward-button",
    "urlbar-stop-button",
    "urlbar-go-button",
    "urlbar-reload-button",
    "searchbar",
    "cut-button",
    "copy-button",
    "paste-button",
    "zoom-out-button",
    "zoom-reset-button",
    "zoom-in-button",
    "BMB_bookmarksPopup",
    "BMB_unsortedBookmarksPopup",
    "BMB_bookmarksToolbarPopup",
    "search-go-button",
  ]
  return DEFAULT_ITEMS.concat(PALETTE_ITEMS)
                      .concat(SPECIAL_CASES);
});

const OTHER_MOUSEUP_MONITORED_ITEMS = [
  "PlacesChevron",
  "PlacesToolbarItems",
  "menubar-items",
];

// Items that open arrow panels will often be overlapped by
// the panel that they're opening by the time the mouseup
// event is fired, so for these items, we monitor mousedown.
const MOUSEDOWN_MONITORED_ITEMS = [
  "PanelUI-menu-button",
];

// Weakly maps browser windows to objects whose keys are relative
// timestamps for when some kind of session started. For example,
// when a customization session started. That way, when the window
// exits customization mode, we can determine how long the session
// lasted.
const WINDOW_DURATION_MAP = new WeakMap();

// Default bucket name, when no other bucket is active.
const BUCKET_DEFAULT = "__DEFAULT__";
// Bucket prefix, for named buckets.
const BUCKET_PREFIX = "bucket_";
// Standard separator to use between different parts of a bucket name, such
// as primary name and the time step string.
const BUCKET_SEPARATOR = "|";

this.BrowserUITelemetry = {
  init: function() {
    UITelemetry.addSimpleMeasureFunction("toolbars",
                                         this.getToolbarMeasures.bind(this));
  },

  getToolbarMeasures: function() {
    // Grab the most recent non-popup, non-private browser window for us to
    // analyze the toolbars in...
    let win = RecentWindow.getMostRecentBrowserWindow({
      private: false,
      allowPopups: false
    });

    // If there are no such windows, we're out of luck. :(
    if (!win) {
      return {};
    }

    let document = win.document;
    let result = {};

    // Determine if the window is in the maximized, normal or
    // fullscreen state.
    result.sizemode = document.documentElement.getAttribute("sizemode");

    // Determine if the Bookmarks bar is currently visible
    let bookmarksBar = document.getElementById("PersonalToolbar");
    result.bookmarksBarEnabled = bookmarksBar && !bookmarksBar.collapsed;

    // Determine if the menubar is currently visible. On OS X, the menubar
    // is never shown, despite not having the collapsed attribute set.
    let menuBar = document.getElementById("toolbar-menubar");
    result.menuBarEnabled =
      menuBar && Services.appinfo.OS != "Darwin"
              && menuBar.getAttribute("autohide") != "true";

    // Determine if the titlebar is currently visible.
    result.titleBarEnabled = !Services.prefs.getBoolPref("browser.tabs.drawInTitlebar");

    // Examine all customizable areas and see what default items
    // are present and missing.
    let defaultKept = [];
    let defaultMoved = [];
    let nondefaultAdded = [];

    for (let areaID of CustomizableUI.areas) {
      let items = CustomizableUI.getWidgetIdsInArea(areaID);
      for (let item of items) {
        // Is this a default item?
        if (DEFAULT_ITEMS.indexOf(item) != -1) {
          // Ok, it's a default item - but is it in its default
          // toolbar? We use Array.isArray instead of checking for
          // toolbarID in DEFAULT_AREA_PLACEMENTS because an add-on might
          // be clever and give itself the id of "toString" or something.
          if (Array.isArray(DEFAULT_AREA_PLACEMENTS[areaID]) &&
              DEFAULT_AREA_PLACEMENTS[areaID].indexOf(item) != -1) {
            // The item is in its default toolbar
            defaultKept.push(item);
          } else {
            defaultMoved.push(item);
          }
        } else if (PALETTE_ITEMS.indexOf(item) != -1) {
          // It's a palette item that's been moved into a toolbar
          nondefaultAdded.push(item);
        }
        // else, it's provided by an add-on, and we won't record it.
      }
    }

    // Now go through the items in the palette to see what default
    // items are in there.
    let paletteItems =
      CustomizableUI.getUnusedWidgets(aWindow.gNavToolbox.palette);
    let defaultRemoved = [item.id for (item of paletteItems)
                          if (DEFAULT_ITEMS.indexOf(item.id) != -1)];

    result.defaultKept = defaultKept;
    result.defaultMoved = defaultMoved;
    result.nondefaultAdded = nondefaultAdded;
    result.defaultRemoved = defaultRemoved;

    // Next, determine how many add-on provided toolbars exist.
    let addonToolbars = 0;
    let toolbars = document.querySelectorAll("toolbar[customizable=true]");
    for (let toolbar of toolbars) {
      if (DEFAULT_AREAS.indexOf(toolbar.id) == -1) {
        addonToolbars++;
      }
    }
    result.addonToolbars = addonToolbars;

    // Find out how many open tabs we have in each window
    let winEnumerator = Services.wm.getEnumerator("navigator:browser");
    let visibleTabs = [];
    let hiddenTabs = [];
    while (winEnumerator.hasMoreElements()) {
      let someWin = winEnumerator.getNext();
      if (someWin.gBrowser) {
        let visibleTabsNum = someWin.gBrowser.visibleTabs.length;
        visibleTabs.push(visibleTabsNum);
        hiddenTabs.push(someWin.gBrowser.tabs.length - visibleTabsNum);
      }
    }
    result.visibleTabs = visibleTabs;
    result.hiddenTabs = hiddenTabs;

    if (Components.isSuccessCode(searchResult)) {
      result.currentSearchEngine = Services.search.currentEngine.name;
    }

    return result;
  },
};

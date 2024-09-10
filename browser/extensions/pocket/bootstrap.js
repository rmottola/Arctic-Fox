/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const {classes: Cc, interfaces: Ci, utils: Cu, manager: Cm} = Components;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://services-common/utils.js");
Cu.import("resource://gre/modules/Preferences.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "Services",
                                  "resource://gre/modules/Services.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "RecentWindow",
                                  "resource:///modules/RecentWindow.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "CustomizableUI",
                                  "resource:///modules/CustomizableUI.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "SocialService",
                                  "resource://gre/modules/SocialService.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "AddonManager",
                                  "resource://gre/modules/AddonManager.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "ReaderMode",
                                  "resource://gre/modules/ReaderMode.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "Pocket",
                                  "chrome://pocket/content/Pocket.jsm");
XPCOMUtils.defineLazyGetter(this, "gPocketBundle", function() {
  return Services.strings.createBundle("chrome://pocket/locale/pocket.properties");
});


const PREF_BRANCH = "extensions.pocket.";
const PREFS = {
  enabled: true, // bug 1229937, figure out ui tour support
  api: "api.getpocket.com",
  site: "getpocket.com",
  oAuthConsumerKey: "40249-e88c401e1b1f2242d9e441c4"
};

function setDefaultPrefs() {
  let branch = Services.prefs.getDefaultBranch(PREF_BRANCH);
  for (let [key, val] in Iterator(PREFS)) {
    switch (typeof val) {
      case "boolean":
        branch.setBoolPref(key, val);
        break;
      case "number":
        branch.setIntPref(key, val);
        break;
      case "string":
        branch.setCharPref(key, val);
        break;
    }
  }
}

function* allBrowserWindows() {
  var winEnum = Services.wm.getEnumerator("navigator:browser");
  while (winEnum.hasMoreElements()) {
    let win = winEnum.getNext();
    // skip closed windows
    if (win.closed)
      continue;
    yield win;
  }
}

function createElementWithAttrs(document, type, attrs) {
  let element = document.createElement(type);
  Object.keys(attrs).forEach(function (attr) {
    element.setAttribute(attr, attrs[attr]);
  })
  return element;
}

function CreatePocketWidget(reason) {
  let id = "pocket-button"
  let widget = CustomizableUI.getWidget(id);
  // The widget is only null if we've created then destroyed the widget.
  // Once we've actually called createWidget the provider will be set to
  // PROVIDER_API.
  if (widget && widget.provider == CustomizableUI.PROVIDER_API)
    return;
  // if upgrading from builtin version and the button was placed in ui,
  // seenWidget will not be null
  let seenWidget = CustomizableUI.getPlacementOfWidget("pocket-button", false, true);
  let pocketButton = {
    id: "pocket-button",
    defaultArea: CustomizableUI.AREA_NAVBAR,
    introducedInVersion: "pref",
    type: "view",
    viewId: "PanelUI-pocketView",
    label: gPocketBundle.GetStringFromName("pocket-button.label"),
    tooltiptext: gPocketBundle.GetStringFromName("pocket-button.tooltiptext"),
    // Use forwarding functions here to avoid loading Pocket.jsm on startup:
    onViewShowing: function() {
      return Pocket.onPanelViewShowing.apply(this, arguments);
    },
    onViewHiding: function() {
      return Pocket.onPanelViewHiding.apply(this, arguments);
    },
    onBeforeCreated: function(doc) {
      // Bug 1223127,CUI should make this easier to do.
      if (doc.getElementById("PanelUI-pocketView"))
        return;
      let view = doc.createElement("panelview");
      view.id = "PanelUI-pocketView";
      let panel = doc.createElement("vbox");
      panel.setAttribute("class", "panel-subview-body");
      view.appendChild(panel);
      doc.getElementById("PanelUI-multiView").appendChild(view);
    }
  };

  CustomizableUI.createWidget(pocketButton);
  CustomizableUI.addListener(pocketButton);
  // placed is null if location is palette
  let placed = CustomizableUI.getPlacementOfWidget("pocket-button");

  // a first time install will always have placed the button somewhere, and will
  // not have a placement prior to creating the widget. Thus, !seenWidget &&
  // placed.
  if (reason == ADDON_ENABLE && !seenWidget && placed) {
    // initially place the button after the bookmarks button if it is in the UI
    let widgets = CustomizableUI.getWidgetIdsInArea(CustomizableUI.AREA_NAVBAR);
    let bmbtn = widgets.indexOf("bookmarks-menu-button");
    if (bmbtn > -1) {
      CustomizableUI.moveWidgetWithinArea("pocket-button", bmbtn + 1);
    }
  }

  // Uninstall the Pocket social provider if it exists, but only if we haven't
  // already uninstalled it in this manner.  That way the user can reinstall
  // it if they prefer it without its being uninstalled every time they start
  // the browser.
  let origin = "https://getpocket.com";
  SocialService.getProvider(origin, provider => {
    if (provider) {
      let pref = "social.backup.getpocket-com";
      if (!Services.prefs.prefHasUserValue(pref)) {
        let str = Cc["@mozilla.org/supports-string;1"].
                  createInstance(Ci.nsISupportsString);
        str.data = JSON.stringify(provider.manifest);
        Services.prefs.setComplexValue(pref, Ci.nsISupportsString, str);
        SocialService.uninstallProvider(origin, () => {});
      }
    }
  });

}

// PocketContextMenu
// When the context menu is opened check if we need to build and enable pocket UI.
var PocketContextMenu = {
  init: function() {
    Services.obs.addObserver(this, "on-build-contextmenu", false);
  },
  shutdown: function() {
    Services.obs.removeObserver(this, "on-build-contextmenu");
    // loop through windows and remove context menus
    // iterate through all windows and add pocket to them
    for (let win of allBrowserWindows()) {
      let document = win.document;
      for (let id in ["context-pocket", "context-savelinktopocket"]) {
        let element = document.getElementById(id);
        if (element)
          element.remove();
      }
    }
  },
  observe: function(aSubject, aTopic, aData) {
    let subject = aSubject.wrappedJSObject;
    let document = subject.menu.ownerDocument;
    let window = document.defaultView;
    let pocketEnabled = CustomizableUI.getPlacementOfWidget("pocket-button");

    let showSaveCurrentPageToPocket = !(subject.onTextInput || subject.onLink ||
                                        subject.isContentSelected || subject.onImage ||
                                        subject.onCanvas || subject.onVideo || subject.onAudio);
    let targetUrl = subject.onLink ? subject.linkUrl : subject.pageUrl;
    let targetURI = Services.io.newURI(targetUrl, null, null);
    let canPocket = pocketEnabled && (targetURI.schemeIs("http") || targetURI.schemeIs("https") ||
                    (targetURI.schemeIs("about") && ReaderMode.getOriginalUrl(targetUrl)));

    let showSaveLinkToPocket = canPocket && !showSaveCurrentPageToPocket && subject.onLink;

    // create menu entries if necessary
    let menu = document.getElementById("context-pocket");
    if (!menu) {
      menu = createElementWithAttrs(document, "menuitem", {
        "id": "context-pocket",
        "label": gPocketBundle.GetStringFromName("saveToPocketCmd.label"),
        "accesskey": gPocketBundle.GetStringFromName("saveToPocketCmd.accesskey"),
        "oncommand": "Pocket.savePage(gContextMenu.browser, gContextMenu.browser.currentURI.spec, gContextMenu.browser.contentTitle);"
      });
      let sibling = document.getElementById("context-savepage");
      if (sibling.nextSibling) {
        sibling.parentNode.insertBefore(menu, sibling.nextSibling);
      } else {
        sibling.parentNode.appendChild(menu);
      }
    }
    menu.hidden = !(canPocket && showSaveCurrentPageToPocket);

    menu = document.getElementById("context-savelinktopocket");
    if (!menu) {
      menu = createElementWithAttrs(document, "menuitem", {
        "id": "context-savelinktopocket",
        "label": gPocketBundle.GetStringFromName("saveLinkToPocketCmd.label"),
        "accesskey": gPocketBundle.GetStringFromName("saveLinkToPocketCmd.accesskey"),
        "oncommand": "Pocket.savePage(gContextMenu.browser, gContextMenu.linkURL);"
      });
      sibling = document.getElementById("context-savelink");
      if (sibling.nextSibling) {
        sibling.parentNode.insertBefore(menu, sibling.nextSibling);
      } else {
        sibling.parentNode.appendChild(menu);
      }
    }
    menu.hidden = !showSaveLinkToPocket;
  }
}

// PocketReader
// Listen for reader mode setup and add our button to the reader toolbar
var PocketReader = {
  startup: function() {
    let mm = Services.mm;
    mm.addMessageListener("Reader:OnSetup", this);
    mm.addMessageListener("Reader:Clicked-pocket-button", this);
    mm.broadcastAsyncMessage("Reader:AddButton",
                             { id: "pocket-button",
                               title: gPocketBundle.GetStringFromName("pocket-button.tooltiptext"),
                               image: "chrome://pocket/content/panels/img/pocket.svg#pocket-mark" });
  },
  shutdown: function() {
    let mm = Services.mm;
    mm.removeMessageListener("Reader:OnSetup", this);
    mm.removeMessageListener("Reader:Clicked-pocket-button", this);
    mm.broadcastAsyncMessage("Reader:RemoveButton", { id: "pocket-button" });
  },
  receiveMessage: function(message) {
    switch (message.name) {
      case "Reader:OnSetup": {
        // tell the reader about our button.  A chrome url here doesn't work, but
        // we can use the resoure url.
        message.target.messageManager.
          sendAsyncMessage("Reader:AddButton", { id: "pocket-button",
                                                 title: gPocketBundle.GetStringFromName("pocket-button.tooltiptext"),
                                                 image: "chrome://pocket/content/panels/img/pocket.svg#pocket-mark"});
        break;
      }
      case "Reader:Clicked-pocket-button": {
        let doc = message.target.ownerDocument;
        let pocketWidget = doc.getElementById("pocket-button");
        let placement = CustomizableUI.getPlacementOfWidget("pocket-button");
        if (placement) {
          if (placement.area == CustomizableUI.AREA_PANEL) {
            doc.defaultView.PanelUI.show().then(function() {
              // The DOM node might not exist yet if the panel wasn't opened before.
              pocketWidget = doc.getElementById("pocket-button");
              pocketWidget.doCommand();
            });
          } else {
            pocketWidget.doCommand();
          }
        }
        break;
      }
    }
  }
}


function pktUIGetter(prop, window) {
  return {
    get: function() {
      // delete any getters for properties loaded from main.js so we only load main.js once
      delete window.pktUI;
      delete window.pktUIMessaging;
      Services.scriptloader.loadSubScript("chrome://pocket/content/main.js", window);
      return window[prop];
    },
    configurable: true,
    enumerable: true
  };
}

var PocketOverlay = {
  startup: function(reason) {
    this.registerStylesheet();
    CreatePocketWidget(reason);
    Services.obs.addObserver(this,
                             "browser-delayed-startup-finished",
                             false);
    CustomizableUI.addListener(this);
    PocketContextMenu.init();
    PocketReader.startup();

    if (reason == ADDON_ENABLE) {
      for (let win of allBrowserWindows()) {
        this.setWindowScripts(win);
        this.updateWindow(win);
      }
    }
  },
  shutdown: function(reason) {
    CustomizableUI.removeListener(this);
    for (let window of allBrowserWindows()) {
      for (let id of ["panelMenu_pocket", "menu_pocket", "BMB_pocket",
                      "panelMenu_pocketSeparator", "menu_pocketSeparator",
                      "BMB_pocketSeparator"]) {
        let element = window.document.getElementById(id);
        if (element)
          element.remove();
      }
      // remove script getters/objects
      delete window.Pocket;
      delete window.pktUI;
      delete window.pktUIMessaging;
    }
    CustomizableUI.destroyWidget("pocket-button");
    PocketContextMenu.shutdown();
    PocketReader.shutdown();
    this.unregisterStylesheet();
  },
  observe: function(aSubject, aTopic, aData) {
    // new browser window, initialize the "overlay"
    let window = aSubject;
    this.setWindowScripts(window);
    this.updateWindow(window);
  },
  setWindowScripts: function(window) {
    XPCOMUtils.defineLazyModuleGetter(window, "Pocket",
                                      "chrome://pocket/content/Pocket.jsm");
    // Can't use XPCOMUtils for these because the scripts try to define the variables
    // on window, and so the defineProperty inside defineLazyGetter fails.
    Object.defineProperty(window, "pktUI", pktUIGetter("pktUI", window));
    Object.defineProperty(window, "pktUIMessaging", pktUIGetter("pktUIMessaging", window));
  },
  // called for each window as it is opened
  updateWindow: function(window) {
    // insert our three menu items
    let document = window.document;

    // add to bookmarksMenu
    let sib = document.getElementById("menu_bookmarkThisPage");
    if (sib && !document.getElementById("menu_pocket")) {
      let menu = createElementWithAttrs(document, "menuitem", {
        "id": "menu_pocket",
        "label": gPocketBundle.GetStringFromName("pocketMenuitem.label"),
        "class": "menuitem-iconic", // OSX only
        "oncommand": "openUILink(Pocket.listURL, event);"
      });
      let sep = createElementWithAttrs(document, "menuseparator", {
        "id": "menu_pocketSeparator"
      });
      sib.parentNode.insertBefore(menu, sib);
      sib.parentNode.insertBefore(sep, sib);
    }

    // add to bookmarks-menu-button
    sib = document.getElementById("BMB_subscribeToPageMenuitem");
    if (sib && !document.getElementById("BMB_pocket")) {
      let menu = createElementWithAttrs(document, "menuitem", {
        "id": "BMB_pocket",
        "label": gPocketBundle.GetStringFromName("pocketMenuitem.label"),
        "class": "menuitem-iconic bookmark-item subviewbutton",
        "oncommand": "openUILink(Pocket.listURL, event);"
      });
      let sep = createElementWithAttrs(document, "menuseparator", {
        "id": "BMB_pocketSeparator"
      });
      sib.parentNode.insertBefore(menu, sib);
      sib.parentNode.insertBefore(sep, sib);
    }

    // add to PanelUI-bookmarks
    sib = document.getElementById("panelMenuBookmarkThisPage");
    if (sib && !document.getElementById("panelMenu_pocket")) {
      let menu = createElementWithAttrs(document, "toolbarbutton", {
        "id": "panelMenu_pocket",
        "label": gPocketBundle.GetStringFromName("pocketMenuitem.label"),
        "class": "subviewbutton cui-withicon",
        "oncommand": "openUILink(Pocket.listURL, event);"
      });
      let sep = createElementWithAttrs(document, "toolbarseparator", {
        "id": "panelMenu_pocketSeparator"
      });
      // nextSibling is no-id toolbarseparator
      // insert separator first then button
      sib = sib.nextSibling;
      sib.parentNode.insertBefore(sep, sib);
      sib.parentNode.insertBefore(menu, sib);
    }

    this.updatePocketItemVisibility(document);
  },
  onWidgetAdded: function(aWidgetId, aArea, aPosition) {
    for (let win of allBrowserWindows()) {
      this.updatePocketItemVisibility(win.document);
    }
  },
  onWidgetRemoved: function(aWidgetId, aArea, aPosition) {
    for (let win of allBrowserWindows()) {
      this.updatePocketItemVisibility(win.document);
    }
  },
  onWidgetReset: function(aNode, aContainer) {
    // CUI was reset and doesn't respect default area for API widgets, place our
    // widget back to the default area
    // initially place the button after the bookmarks button if it is in the UI
    let widgets = CustomizableUI.getWidgetIdsInArea(CustomizableUI.AREA_NAVBAR);
    let bmbtn = widgets.indexOf("bookmarks-menu-button");
    if (bmbtn > -1) {
      CustomizableUI.addWidgetToArea("pocket-button", CustomizableUI.AREA_NAVBAR, bmbtn + 1);
    } else {
      CustomizableUI.addWidgetToArea("pocket-button", CustomizableUI.AREA_NAVBAR);
    }
  },
  updatePocketItemVisibility: function(doc) {
    let hidden = !CustomizableUI.getPlacementOfWidget("pocket-button");
    for (let prefix of ["panelMenu_", "menu_", "BMB_"]) {
      let element = doc.getElementById(prefix + "pocket");
      if (element) {
        element.hidden = hidden;
        doc.getElementById(prefix + "pocketSeparator").hidden = hidden;
      }
    }
    // enable or disable reader button
    if (hidden) {
      PocketReader.shutdown();
    } else {
      PocketReader.startup();
    }
  },

  registerStylesheet: function() {
    let styleSheetService= Components.classes["@mozilla.org/content/style-sheet-service;1"]
                                     .getService(Components.interfaces.nsIStyleSheetService);
    let styleSheetURI = Services.io.newURI("chrome://pocket/skin/pocket.css", null, null);
    styleSheetService.loadAndRegisterSheet(styleSheetURI, styleSheetService.AUTHOR_SHEET);
    styleSheetURI = Services.io.newURI("chrome://pocket-shared/skin/pocket.css", null, null);
    styleSheetService.loadAndRegisterSheet(styleSheetURI, styleSheetService.AUTHOR_SHEET);
  },

  unregisterStylesheet: function() {
    let styleSheetService = Components.classes["@mozilla.org/content/style-sheet-service;1"]
                                      .getService(Components.interfaces.nsIStyleSheetService);
    let styleSheetURI = Services.io.newURI("chrome://pocket/skin/pocket.css", null, null);
    if (styleSheetService.sheetRegistered(styleSheetURI, styleSheetService.AUTHOR_SHEET)) {
      styleSheetService.unregisterSheet(styleSheetURI, styleSheetService.AUTHOR_SHEET);
    }
    styleSheetURI = Services.io.newURI("chrome://pocket-shared/skin/pocket.css", null, null);
    if (styleSheetService.sheetRegistered(styleSheetURI, styleSheetService.AUTHOR_SHEET)) {
      styleSheetService.unregisterSheet(styleSheetURI, styleSheetService.AUTHOR_SHEET);
    }
  }

}

// use enabled pref as a way for tests (e.g. test_contextmenu.html) to disable
// the addon when running.
function prefObserver(aSubject, aTopic, aData) {
  let enabled = Services.prefs.getBoolPref("extensions.pocket.enabled");
  if (enabled)
    PocketOverlay.startup(ADDON_ENABLE);
  else
    PocketOverlay.shutdown(ADDON_DISABLE);
}

function startup(data, reason) {
  AddonManager.getAddonByID("isreaditlater@ideashower.com", addon => {
    if (addon && addon.isActive)
      return;
    setDefaultPrefs();
    // migrate enabled pref
    if (Services.prefs.prefHasUserValue("browser.pocket.enabled")) {
      Services.prefs.setBoolPref("extensions.pocket.enabled", Services.prefs.getBoolPref("browser.pocket.enabled"));
      Services.prefs.clearUserPref("browser.pocket.enabled");
    }
    // watch pref change and enable/disable if necessary
    Services.prefs.addObserver("extensions.pocket.enabled", prefObserver, false);
    if (Services.prefs.prefHasUserValue("extensions.pocket.enabled") &&
        !Services.prefs.getBoolPref("extensions.pocket.enabled"))
      return;
    PocketOverlay.startup(reason);
  });
}

function shutdown(data, reason) {
  // For speed sake, we should only do a shutdown if we're being disabled.
  // On an app shutdown, just let it fade away...
  if (reason == ADDON_DISABLE) {
    Services.prefs.removeObserver("extensions.pocket.enabled", prefObserver);
    PocketOverlay.shutdown(reason);
  }
}

function install() {
}

function uninstall() {
}

/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Note: the file browser-tabsintitlebar-stub.js is used instead of
// this one on platforms which don't have CAN_DRAW_IN_TITLEBAR defined.

var TabsInTitlebar = {
  init: function () {
    this._readPref();
    Services.prefs.addObserver(this._prefName, this, false);

    // We need to update the appearance of the titlebar when the menu changes
    // from the active to the inactive state. We can't, however, rely on
    // DOMMenuBarInactive, because the menu fires this event and then removes
    // the inactive attribute after an event-loop spin.
    //
    // Because updating the appearance involves sampling the heights and margins
    // of various elements, it's important that the layout be more or less
    // settled before updating the titlebar. So instead of listening to
    // DOMMenuBarActive and DOMMenuBarInactive, we use a MutationObserver to
    // watch the "invalid" attribute directly.
    let menu = document.getElementById("toolbar-menubar");
    this._menuObserver = new MutationObserver(this._onMenuMutate);
    this._menuObserver.observe(menu, {attributes: true});

    this.onAreaReset = function(aArea) {
      if (aArea == CustomizableUI.AREA_TABSTRIP || aArea == CustomizableUI.AREA_MENUBAR)
        this._update(true);
    };
    this.onWidgetAdded = this.onWidgetRemoved = function(aWidgetId, aArea) {
      if (aArea == CustomizableUI.AREA_TABSTRIP || aArea == CustomizableUI.AREA_MENUBAR)
        this._update(true);
    };

    addEventListener("resolutionchange", this, false);

    this._initialized = true;
  },

  allowedBy: function (condition, allow) {
    if (allow) {
      if (condition in this._disallowed) {
        delete this._disallowed[condition];
        this._update(true);
      }
    } else {
      if (!(condition in this._disallowed)) {
        this._disallowed[condition] = null;
        this._update(true);
      }
    }
  },

  updateAppearance: function updateAppearance(aForce) {
    this._update(aForce);
  },

  get enabled() {
    return document.documentElement.getAttribute("tabsintitlebar") == "true";
  },

  observe: function (subject, topic, data) {
    if (topic == "nsPref:changed")
      this._readPref();
  },

  handleEvent: function (aEvent) {
    if (aEvent.type == "resolutionchange" && aEvent.target == window) {
      this._update(true);
    }
  },

  _onMenuMutate: function (aMutations) {
    // We don't care about restored windows, since the menu shouldn't be
    // pushing the tab-strip down.
    if (document.documentElement.getAttribute("sizemode") == "normal") {
      return;
    }

    for (let mutation of aMutations) {
      if (mutation.attributeName == "inactive" ||
          mutation.attributeName == "autohide") {
        TabsInTitlebar._update(true);
        return;
      }
    }
  },

  _initialized: false,
  _disallowed: {},
  _prefName: "browser.tabs.drawInTitlebar",
  _lastSizeMode: null,

  _readPref: function () {
    this.allowedBy("pref",
                   Services.prefs.getBoolPref(this._prefName));
  },

  _update: function (aForce=false) {
    let $ = id => document.getElementById(id);
    let rect = ele => ele.getBoundingClientRect();
    let verticalMargins = cstyle => parseFloat(cstyle.marginBottom) + parseFloat(cstyle.marginTop);

    if (!this._initialized || window.fullScreen)
      return;

    let allowed = true;

    if (!aForce) {
      // _update is called on resize events, because the window is not ready
      // after sizemode events. However, we only care about the event when the
      // sizemode is different from the last time we updated the appearance of
      // the tabs in the titlebar.
      let sizemode = document.documentElement.getAttribute("sizemode");
      if (this._lastSizeMode == sizemode) {
        return;
      }
      let oldSizeMode = this._lastSizeMode;
      this._lastSizeMode = sizemode;
      // Don't update right now if we are leaving fullscreen, since the UI is
      // still changing in the consequent "fullscreen" event. Code there will
      // call this function again when everything is ready.
      // See browser-fullScreen.js: FullScreen.toggle and bug 1173768.
      if (oldSizeMode == "fullscreen") {
        return;
      }
    }

    for (let something in this._disallowed) {
      allowed = false;
      break;
    }

    let titlebar = $("titlebar");
    let titlebarContent = $("titlebar-content");
    let menubar = $("toolbar-menubar");

    // Reset the margins that _update modifies so that we can take accurate
    // measurements.
    titlebarContent.style.marginBottom = "";
    titlebar.style.marginBottom = "";
    menubar.style.marginBottom = "";

    if (allowed) {
      // We set the tabsintitlebar attribute first so that our CSS for
      // tabsintitlebar manifests before we do our measurements.
      document.documentElement.setAttribute("tabsintitlebar", "true");

#ifdef MENUBAR_CAN_AUTOHIDE
      let appmenuButtonBox  = $("appmenu-button-container");
      this._sizePlaceholder("appmenu-button", rect(appmenuButtonBox).width);
#endif
      let captionButtonsBox = $("titlebar-buttonbox");
      this._sizePlaceholder("caption-buttons", rect(captionButtonsBox).width);

      let titlebarContentHeight = rect(titlebarContent).height;
      let menuHeight = this._outerHeight(menubar);

      // If the titlebar is taller than the menubar, add more padding to the
      // bottom of the menubar so that it matches.
      if (menuHeight && titlebarContentHeight > menuHeight) {
        let menuTitlebarDelta = titlebarContentHeight - menuHeight;
        menubar.style.marginBottom = menuTitlebarDelta + "px";
        menuHeight += menuTitlebarDelta;
      }

      // Next, we calculate how much we need to stretch the titlebar down to
      // go all the way to the bottom of the tab strip.
      let tabsToolbar = $("TabsToolbar");
      let tabAndMenuHeight = this._outerHeight(tabsToolbar) + menuHeight;
      titlebarContent.style.marginBottom = tabAndMenuHeight + "px";

      // Finally, we have to determine how much to bring up the elements below
      // the titlebar. We start with a baseHeight of tabAndMenuHeight, to offset
      // the amount we added to the titlebar content. Then, we have two cases:
      //
      // 1) The titlebar is larger than the tabAndMenuHeight. This can happen in
      //    large font mode with the menu autohidden. In this case, we want to
      //    add tabAndMenuHeight, since this should line up the bottom of the
      //    tabstrip with the bottom of the titlebar.
      //
      // 2) The titlebar is equal to or smaller than the tabAndMenuHeight. This
      //    is the more common case, and occurs with normal font sizes. In this
      //    case, we want to bring the menu and tabstrip right up to the top of
      //    the titlebar, so we add the titlebarContentHeight to the baseHeight.
      let baseHeight = tabAndMenuHeight;
      baseHeight += (titlebarContentHeight > tabAndMenuHeight) ? tabAndMenuHeight
                                                               : titlebarContentHeight;
      titlebar.style.marginBottom = "-" + baseHeight + "px";

    } else {
      document.documentElement.removeAttribute("tabsintitlebar");
    }

    ToolbarIconColor.inferFromText();
    if (CustomizationHandler.isCustomizing()) {
      gCustomizeMode.updateLWTStyling();
    }
  },

  _sizePlaceholder: function (type, width) {
    Array.forEach(document.querySelectorAll(".titlebar-placeholder[type='"+ type +"']"),
                  function (node) { node.width = width; });
  },

  /**
   * Retrieve the height of an element, including its top and bottom
   * margins.
   *
   * @param ele
   *        The element to measure.
   * @return
   *        The height and margins as an integer. If the height of the element
   *        is 0, then this returns 0, regardless of what the margins are.
   */
  _outerHeight: function (ele) {
    let cstyle = document.defaultView.getComputedStyle(ele);
    let margins = parseInt(cstyle.marginTop) + parseInt(cstyle.marginBottom);
    let height = ele.getBoundingClientRect().height;
    return height > 0 ? Math.abs(height + margins) : 0;
  },

  uninit: function () {
    this._initialized = false;
    removeEventListener("resolutionchange", this);
    Services.prefs.removeObserver(this._prefName, this);
    this._menuObserver.disconnect();
  }
};

function updateTitlebarDisplay() {
  if (AppConstants.platform == "macosx") {
    // OS X and the other platforms differ enough to necessitate this kind of
    // special-casing. Like the other platforms where we CAN_DRAW_IN_TITLEBAR,
    // we draw in the OS X titlebar when putting the tabs up there. However, OS X
    // also draws in the titlebar when a lightweight theme is applied, regardless
    // of whether or not the tabs are drawn in the titlebar.
    if (TabsInTitlebar.enabled) {
      document.documentElement.setAttribute("chromemargin-nonlwtheme", "0,-1,-1,-1");
      document.documentElement.setAttribute("chromemargin", "0,-1,-1,-1");
      document.documentElement.removeAttribute("drawtitle");
    } else {
      // We set chromemargin-nonlwtheme to "" instead of removing it as a way of
      // making sure that LightweightThemeConsumer doesn't take it upon itself to
      // detect this value again if and when we do a lwtheme state change.
      document.documentElement.setAttribute("chromemargin-nonlwtheme", "");
      let isCustomizing = document.documentElement.hasAttribute("customizing");
      let hasLWTheme = document.documentElement.hasAttribute("lwtheme");
      let isPrivate = PrivateBrowsingUtils.isWindowPrivate(window);
      if ((!hasLWTheme || isCustomizing) && !isPrivate) {
        document.documentElement.removeAttribute("chromemargin");
      }
      document.documentElement.setAttribute("drawtitle", "true");
    }
  } else { // not OS X
    if (TabsInTitlebar.enabled)
      document.documentElement.setAttribute("chromemargin", "0,2,2,2");
    else
      document.documentElement.removeAttribute("chromemargin");
  }
}

function onTitlebarMaxClick() {
  if (window.windowState == window.STATE_MAXIMIZED)
    window.restore();
  else
    window.maximize();
}

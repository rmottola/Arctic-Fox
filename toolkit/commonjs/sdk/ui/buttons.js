/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * PMkit shim for 'sdk/ui/button', (c) JustOff, 2017 */
"use strict";

module.metadata = {
  "stability": "experimental",
  "engines": {
    "Firefox": "> 27"
  }
};

const { Ci, Cc } = require('chrome');
const prefs = require('../preferences/service');

const buttonsList = new Map();
const LOCATION_PREF_ROOT = "extensions.sdk-button-location.";

let gWindowListener;

function getLocation(id) {
  let toolbarId = "nav-bar", nextItemId = "";
  let location = prefs.get(LOCATION_PREF_ROOT + id);
  if (location && location.indexOf(",") !== -1) {
    [toolbarId, nextItemId] = location.split(",");
  }
  return [toolbarId, nextItemId];
}

function saveLocation(id, toolbarId, nextItemId) {
  let _toolbarId = toolbarId || "";
  let _nextItemId = nextItemId || "";
  prefs.set(LOCATION_PREF_ROOT + id, [_toolbarId, _nextItemId].join(","));
}

// Insert button into window
function insertButton(aWindow, id, onBuild) {
  // Build button and save reference to it
  let doc = aWindow.document;
  let b = onBuild(doc, id);
  aWindow[id] = b;

  // Add to the customization palette
  let toolbox = doc.getElementById("navigator-toolbox");
  toolbox.palette.appendChild(b);

  // Retrieve button location from preferences
  let [toolbarId, nextItemId] = getLocation(id);
  let toolbar = toolbarId != "" && doc.getElementById(toolbarId);

  if (toolbar) {
    let nextItem = doc.getElementById(nextItemId);
    // If nextItem not in toolbar then retrieve it by reading currentset attribute
    if (!(nextItem && nextItem.parentNode && nextItem.parentNode.id == toolbarId)) {
      nextItem = null;
      let currentSet = toolbar.getAttribute("currentset");
      let ids = (currentSet == "__empty") ? [] : currentSet.split(",");
      let idx = ids.indexOf(id);
      if (idx != -1) {
        for (let i = idx; i < ids.length; i++) {
          nextItem = doc.getElementById(ids[i]);
          if (nextItem)
            break;
        }
      }
    }
    // Finally insert button in the right toolbar and in the right position
    toolbar.insertItem(id, nextItem, null, false);
  }
}

// Remove button from window
function removeButton(aWindow, id) {
  let b = aWindow[id];
  b.parentNode.removeChild(b);
  delete aWindow[id];
}

// Save locations of buttons after customization
function afterCustomize(e) {
  for (let [id] of buttonsList) {
    let toolbox = e.target;
    let b = toolbox.parentNode.querySelector("#" + id);
    let toolbarId = null, nextItemId = null;
    if (b) {
      let parent = b.parentNode;
      let nextItem = b.nextSibling;
      if (parent && parent.localName == "toolbar") {
        toolbarId = parent.id;
        nextItemId = nextItem && nextItem.id;
      }
    }
    saveLocation(id, toolbarId, nextItemId);
  }
}

// Global window observer
function browserWindowObserver(handlers) {
  this.handlers = handlers;
}

browserWindowObserver.prototype = {
  observe: function(aSubject, aTopic, aData) {
    if (aTopic == "domwindowopened") {
      aSubject.QueryInterface(Ci.nsIDOMWindow).addEventListener("load", this, false);
    } else if (aTopic == "domwindowclosed") {
      if (aSubject.document.documentElement.getAttribute("windowtype") == "navigator:browser") {
        this.handlers.onShutdown(aSubject);
      }
    }
  },

  handleEvent: function(aEvent) {
    let aWindow = aEvent.currentTarget;
    aWindow.removeEventListener(aEvent.type, this, false);

    if (aWindow.document.documentElement.getAttribute("windowtype") == "navigator:browser") {
      this.handlers.onStartup(aWindow);
    }
  }
};

// Run on every window startup
function browserWindowStartup(aWindow) {
  for (let [id, onBuild] of buttonsList) {
    insertButton(aWindow, id, onBuild);
  }
  aWindow.addEventListener("aftercustomization", afterCustomize, false);
};

// Run on every window shutdown
function browserWindowShutdown(aWindow) {
  for (let [id, onBuild] of buttonsList) {
    removeButton(aWindow, id);
  }
  aWindow.removeEventListener("aftercustomization", afterCustomize, false);
}

// Main object
const buttons = {
  createButton: function(aProperties) {
    // If no buttons were inserted yet, setup global window observer
    if (buttonsList.size == 0) {
      let ww = Cc["@mozilla.org/embedcomp/window-watcher;1"].getService(Ci.nsIWindowWatcher);
      gWindowListener = new browserWindowObserver({
        onStartup: browserWindowStartup,
        onShutdown: browserWindowShutdown
      });
      ww.registerNotification(gWindowListener);
    }

    // Add button to list
    buttonsList.set(aProperties.id, aProperties.onBuild);

    // Inster button to all open windows
    let wm = Cc["@mozilla.org/appshell/window-mediator;1"].getService(Ci.nsIWindowMediator);
    let winenu = wm.getEnumerator("navigator:browser");
    while (winenu.hasMoreElements()) {
      let win = winenu.getNext();
      insertButton(win, aProperties.id, aProperties.onBuild);
      // When first button inserted, add afterCustomize listener
      if (buttonsList.size == 1) {
        win.addEventListener("aftercustomization", afterCustomize, false);
      }
    }
  },

  destroyButton: function(id) {
    // Remove button from list
    buttonsList.delete(id);

    // If no more buttons exist, remove global window observer
    if (buttonsList.size == 0) {
      let ww = Cc["@mozilla.org/embedcomp/window-watcher;1"].getService(Ci.nsIWindowWatcher);
      ww.unregisterNotification(gWindowListener);
      gWindowListener = null;
    }

    // Remove button from all open windows
    let wm = Cc["@mozilla.org/appshell/window-mediator;1"].getService(Ci.nsIWindowMediator);
    let winenu = wm.getEnumerator("navigator:browser");
    while (winenu.hasMoreElements()) {
      let win = winenu.getNext();
      removeButton(win, id);
      // If no more buttons exist, remove afterCustomize listener
      if (buttonsList.size == 0) {
        win.removeEventListener("aftercustomization", afterCustomize, false);
      }
    }
  },

  getNode: function(id, window) {
    return window[id];
  }
};

exports.buttons = buttons;

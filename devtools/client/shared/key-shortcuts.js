/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const Services = require("Services");
const EventEmitter = require("devtools/shared/event-emitter");
const isOSX = Services.appinfo.OS === "Darwin";

// List of electron keys mapped to DOM API (DOM_VK_*) key code
const ElectronKeysMapping = {
  "F1": "DOM_VK_F1",
  "F2": "DOM_VK_F2",
  "F3": "DOM_VK_F3",
  "F4": "DOM_VK_F4",
  "F5": "DOM_VK_F5",
  "F6": "DOM_VK_F6",
  "F7": "DOM_VK_F7",
  "F8": "DOM_VK_F8",
  "F9": "DOM_VK_F9",
  "F10": "DOM_VK_F10",
  "F11": "DOM_VK_F11",
  "F12": "DOM_VK_F12",
  "F13": "DOM_VK_F13",
  "F14": "DOM_VK_F14",
  "F15": "DOM_VK_F15",
  "F16": "DOM_VK_F16",
  "F17": "DOM_VK_F17",
  "F18": "DOM_VK_F18",
  "F19": "DOM_VK_F19",
  "F20": "DOM_VK_F20",
  "F21": "DOM_VK_F21",
  "F22": "DOM_VK_F22",
  "F23": "DOM_VK_F23",
  "F24": "DOM_VK_F24",
  "Plus": "DOM_VK_PLUS",
  "Space": "DOM_VK_SPACE",
  "Backspace": "DOM_VK_BACK_SPACE",
  "Delete": "DOM_VK_DELETE",
  "Insert": "DOM_VK_INSERT",
  "Return": "DOM_VK_RETURN",
  "Enter": "DOM_VK_RETURN",
  "Up": "DOM_VK_UP",
  "Down": "DOM_VK_DOWN",
  "Left": "DOM_VK_LEFT",
  "Right": "DOM_VK_RIGHT",
  "Home": "DOM_VK_HOME",
  "End": "DOM_VK_END",
  "PageUp": "DOM_VK_PAGE_UP",
  "PageDown": "DOM_VK_PAGE_DOWN",
  "Escape": "DOM_VK_ESCAPE",
  "Esc": "DOM_VK_ESCAPE",
  "VolumeUp": "DOM_VK_VOLUME_UP",
  "VolumeDown": "DOM_VK_VOLUME_DOWN",
  "VolumeMute": "DOM_VK_VOLUME_MUTE",
  "PrintScreen": "DOM_VK_PRINTSCREEN",
};

/**
 * Helper to listen for keyboard events decribed in .properties file.
 *
 * let shortcuts = new KeyShortcuts({
 *   window
 * });
 * shortcuts.on("Ctrl+F", event => {
 *   // `event` is the KeyboardEvent which relates to the key shortcuts
 * });
 *
 * @param DOMWindow window
 *        The window object of the document to listen events from.
 */
function KeyShortcuts({ window }) {
  this.window = window;
  this.keys = new Map();
  this.eventEmitter = new EventEmitter();
  this.window.addEventListener("keydown", this);
}

/*
 * Parse an electron-like key string and return a normalized object which
 * allow efficient match on DOM key event. The normalized object matches DOM
 * API.
 *
 * @param DOMWindow window
 *        Any DOM Window object, just to fetch its `KeyboardEvent` object
 * @param String str
 *        The shortcut string to parse, following this document:
 *        https://github.com/electron/electron/blob/master/docs/api/accelerator.md
 */
KeyShortcuts.parseElectronKey = function (window, str) {
  let modifiers = str.split("+");
  let key = modifiers.pop();

  let shortcut = {
    ctrl: false,
    meta: false,
    alt: false,
    shift: false,
    // Set for character keys
    key: undefined,
    // Set for non-character keys
    keyCode: undefined,
  };
  for (let mod of modifiers) {
    if (mod === "Alt") {
      shortcut.alt = true;
    } else if (["Command", "Cmd"].includes(mod)) {
      shortcut.meta = true;
    } else if (["CommandOrControl", "CmdOrCtrl"].includes(mod)) {
      if (isOSX) {
        shortcut.meta = true;
      } else {
        shortcut.ctrl = true;
      }
    } else if (["Control", "Ctrl"].includes(mod)) {
      shortcut.ctrl = true;
    } else if (mod === "Shift") {
      shortcut.shift = true;
    } else {
      throw new Error("Unsupported modifier: " + mod);
    }
  }

  if (typeof (key) === "string" && key.length === 1) {
    // Match any single character
    shortcut.key = key.toLowerCase();
  } else if (key in ElectronKeysMapping) {
    // Maps the others manually to DOM API DOM_VK_*
    key = ElectronKeysMapping[key];
    shortcut.keyCode = window.KeyboardEvent[key];
  } else {
    throw new Error("Unsupported key: " + key);
  }

  return shortcut;
};

KeyShortcuts.prototype = {
  destroy() {
    this.window.removeEventListener("keydown", this);
    this.keys.clear();
  },

  doesEventMatchShortcut(event, shortcut) {
    if (shortcut.meta != event.metaKey) {
      return false;
    }
    if (shortcut.ctrl != event.ctrlKey) {
      return false;
    }
    if (shortcut.alt != event.altKey) {
      return false;
    }
    // Shift is a special modifier, it may implicitely be required if the
    // expected key is a special character accessible via shift.
    if (shortcut.shift != event.shiftKey && event.key &&
        event.key.match(/[a-zA-Z]/)) {
      return false;
    }
    if (shortcut.keyCode) {
      return event.keyCode == shortcut.keyCode;
    }
    return event.key.toLowerCase() == shortcut.key;
  },

  handleEvent(event) {
    for (let [key, shortcut] of this.keys) {
      if (this.doesEventMatchShortcut(event, shortcut)) {
        this.eventEmitter.emit(key, event);
      }
    }
  },

  on(key, listener) {
    if (typeof listener !== "function") {
      throw new Error("KeyShortcuts.on() expects a function as " +
                      "second argument");
    }
    if (!this.keys.has(key)) {
      let shortcut = KeyShortcuts.parseElectronKey(this.window, key);
      this.keys.set(key, shortcut);
    }
    this.eventEmitter.on(key, listener);
  },

  off(key, listener) {
    this.eventEmitter.off(key, listener);
  },
};
exports.KeyShortcuts = KeyShortcuts;

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/**
 * This module inject dynamically menu items and key shortcuts into browser UI.
 *
 * Menu and shortcut definitions are fetched from:
 * - devtools/client/menus for top level entires
 * - devtools/client/definitions for tool-specifics entries
 */

const {LocalizationHelper} = require("devtools/shared/l10n");
const MENUS_L10N = new LocalizationHelper("devtools/client/locales/menus.properties");

loader.lazyRequireGetter(this, "gDevTools", "devtools/client/framework/devtools", true);
loader.lazyRequireGetter(this, "gDevToolsBrowser", "devtools/client/framework/devtools-browser", true);

// Keep list of inserted DOM Elements in order to remove them on unload
// Maps browser xul document => list of DOM Elements
const FragmentsCache = new Map();

function l10n(key) {
  return MENUS_L10N.getStr(key);
}

/**
 * Create a xul:key element
 *
 * @param {XULDocument} doc
 *        The document to which keys are to be added.
 * @param {String} id
 *        key's id, automatically prefixed with "key_".
 * @param {String} shortcut
 *        The key shortcut value.
 * @param {String} keytext
 *        If `shortcut` refers to a function key, refers to the localized
 *        string to describe a non-character shortcut.
 * @param {String} modifiers
 *        Space separated list of modifier names.
 * @param {Function} oncommand
 *        The function to call when the shortcut is pressed.
 *
 * @return XULKeyElement
 */
function createKey({ doc, id, shortcut, keytext, modifiers, oncommand }) {
  let k = doc.createElement("key");
  k.id = "key_" + id;

  if (shortcut.startsWith("VK_")) {
    k.setAttribute("keycode", shortcut);
    if (keytext) {
      k.setAttribute("keytext", keytext);
    }
  } else {
    k.setAttribute("key", shortcut);
  }

  if (modifiers) {
    k.setAttribute("modifiers", modifiers);
  }

  // Bug 371900: command event is fired only if "oncommand" attribute is set.
  k.setAttribute("oncommand", ";");
  k.addEventListener("command", oncommand);

  return k;
}

/**
 * Create a xul:menuitem element
 *
 * @param {XULDocument} doc
 *        The document to which keys are to be added.
 * @param {String} id
 *        Element id.
 * @param {String} label
 *        Menu label.
 * @param {String} accesskey (optional)
 *        Access key of the menuitem, used as shortcut while opening the menu.
 * @param {Boolean} isCheckbox (optional)
 *        If true, the menuitem will act as a checkbox and have an optional
 *        tick on its left.
 *
 * @return XULMenuItemElement
 */
function createMenuItem({ doc, id, label, accesskey, isCheckbox }) {
  let menuitem = doc.createElement("menuitem");
  menuitem.id = id;
  menuitem.setAttribute("label", label);
  if (accesskey) {
    menuitem.setAttribute("accesskey", accesskey);
  }
  if (isCheckbox) {
    menuitem.setAttribute("type", "checkbox");
    menuitem.setAttribute("autocheck", "false");
  }
  return menuitem;
}

/**
 * Add a <key> to <keyset id="devtoolsKeyset">.
 * Appending a <key> element is not always enough. The <keyset> needs
 * to be detached and reattached to make sure the <key> is taken into
 * account (see bug 832984).
 *
 * @param {XULDocument} doc
 *        The document to which keys are to be added
 * @param {XULElement} or {DocumentFragment} keys
 *        Keys to add
 */
function attachKeybindingsToBrowser(doc, keys) {
  let devtoolsKeyset = doc.getElementById("devtoolsKeyset");

  if (!devtoolsKeyset) {
    devtoolsKeyset = doc.createElement("keyset");
    devtoolsKeyset.setAttribute("id", "devtoolsKeyset");
  }
  devtoolsKeyset.appendChild(keys);
  let mainKeyset = doc.getElementById("mainKeyset");
  mainKeyset.parentNode.insertBefore(devtoolsKeyset, mainKeyset);
}

/**
 * Add a menu entry for a tool definition
 *
 * @param {Object} toolDefinition
 *        Tool definition of the tool to add a menu entry.
 * @param {XULDocument} doc
 *        The document to which the tool menu item is to be added.
 */
function createToolMenuElements(toolDefinition, doc) {
  let id = toolDefinition.id;
  let appmenuId = "appmenuitem_" + id;
  let menuId = "menuitem_" + id;

  // Prevent multiple entries for the same tool.
  if (doc.getElementById(appmenuId) || doc.getElementById(menuId)) {
    return;
  }

  let oncommand = function (id, event) {
    let window = event.target.ownerDocument.defaultView;
    gDevToolsBrowser.selectToolCommand(window.gBrowser, id);
  }.bind(null, id);

  let key = null;
  if (toolDefinition.key) {
    key = createKey({
      doc,
      id,
      shortcut: toolDefinition.key,
      modifiers: toolDefinition.modifiers,
      oncommand: oncommand
    });
  }

  let appmenuitem = createMenuItem({
    doc,
    id: "appmenuitem_" + id,
    label: toolDefinition.menuLabel || toolDefinition.label,
    accesskey: null
  });

  let menuitem = createMenuItem({
    doc,
    id: "menuitem_" + id,
    label: toolDefinition.menuLabel || toolDefinition.label,
    accesskey: toolDefinition.accesskey
  });
  if (key) {
    // Refer to the key in order to display the key shortcut at menu ends
    menuitem.setAttribute("key", key.id);
  }
  appmenuitem.addEventListener("command", oncommand);
  menuitem.addEventListener("command", oncommand);

  return {
    key,
    appmenuitem,
    menuitem
  };
}

/**
 * Create xul menuitem, key elements for a given tool.
 * And then insert them into browser DOM.
 *
 * @param {XULDocument} doc
 *        The document to which the tool is to be registered.
 * @param {Object} toolDefinition
 *        Tool definition of the tool to register.
 * @param {Object} prevDef
 *        The tool definition after which the tool menu item is to be added.
 */
function insertToolMenuElements(doc, toolDefinition, prevDef) {
  let { key, appmenuitem, menuitem } = createToolMenuElements(toolDefinition, doc);

  if (key) {
    attachKeybindingsToBrowser(doc, key);
  }

  let amp;
  if (prevDef) {
    let appmenuitem = doc.getElementById("appmenuitem_" + prevDef.id);
    amp = appmenuitem && appmenuitem.nextSibling ? appmenuitem.nextSibling : null;
  } else {
    amp = doc.getElementById("appmenu_devtools_separator");
  }

  if (amp) {
    amp.parentNode.insertBefore(appmenuitem, amp);
  }

  let mp;
  if (prevDef) {
    let menuitem = doc.getElementById("menuitem_" + prevDef.id);
    mp = menuitem && menuitem.nextSibling ? menuitem.nextSibling : null;
  } else {
    mp = doc.getElementById("menu_devtools_separator");
  }

  if (mp) {
    mp.parentNode.insertBefore(menuitem, mp);
  }
}
exports.insertToolMenuElements = insertToolMenuElements;

/**
 * Remove a tool's menuitem from a window
 *
 * @param {string} toolId
 *        Id of the tool to add a menu entry for
 * @param {XULDocument} doc
 *        The document to which the tool menu item is to be removed from
 */
function removeToolFromMenu(toolId, doc) {
  let key = doc.getElementById("key_" + toolId);
  if (key) {
    key.remove();
  }

  let appmenuitem = doc.getElementById("appmenuitem_" + toolId);
  if (appmenuitem) {
    appmenuitem.remove();
  }

  let menuitem = doc.getElementById("menuitem_" + toolId);
  if (menuitem) {
    menuitem.remove();
  }
}
exports.removeToolFromMenu = removeToolFromMenu;

/**
 * Add all tools to the developer tools menu of a window.
 *
 * @param {XULDocument} doc
 *        The document to which the tool items are to be added.
 */
function addAllToolsToMenu(doc) {
  let fragKeys = doc.createDocumentFragment();
  let fragAppMenuItems = doc.createDocumentFragment();
  let fragMenuItems = doc.createDocumentFragment();

  for (let toolDefinition of gDevTools.getToolDefinitionArray()) {
    if (!toolDefinition.inMenu) {
      continue;
    }

    let elements = createToolMenuElements(toolDefinition, doc);

    if (!elements) {
      continue;
    }

    if (elements.key) {
      fragKeys.appendChild(elements.key);
    }
    fragAppMenuItems.appendChild(elements.appmenuitem);
    fragMenuItems.appendChild(elements.menuitem);
  }

  attachKeybindingsToBrowser(doc, fragKeys);

  let amps = doc.getElementById("appmenu_devtools_separator");
  if (amps) {
    amps.parentNode.insertBefore(fragAppMenuItems, amps);
  }

  let mps = doc.getElementById("menu_devtools_separator");
  if (mps) {
    mps.parentNode.insertBefore(fragMenuItems, mps);
  }
}

/**
 * Add global menus and shortcuts that are not panel specific.
 *
 * @param {XULDocument} doc
 *        The document to which keys and menus are to be added.
 */
function addTopLevelItems(doc) {
  let keys = doc.createDocumentFragment();
  let appmenuItems = doc.createDocumentFragment();
  let menuItems = doc.createDocumentFragment();

  let { menuitems } = require("../menus");
  for (let item of menuitems) {
    if (item.separator) {
      let appseparator = doc.createElement("menuseparator");
      appseparator.id = "app" + item.id;
      let separator = doc.createElement("menuseparator");
      separator.id = item.id;
      appmenuItems.appendChild(appseparator);
      menuItems.appendChild(separator);
    } else {
      let { id, l10nKey } = item;

      // Create a <menuitem>
      let appmenuitem = createMenuItem({
        doc,
        id: "app" + id,
        label: l10n(l10nKey + ".label"),
        accesskey: null,
        isCheckbox: item.checkbox
      });
      let menuitem = createMenuItem({
        doc,
        id,
        label: l10n(l10nKey + ".label"),
        accesskey: l10n(l10nKey + ".accesskey"),
        isCheckbox: item.checkbox
      });
      appmenuitem.addEventListener("command", item.oncommand);
      menuitem.addEventListener("command", item.oncommand);
      appmenuItems.appendChild(appmenuitem);
      menuItems.appendChild(menuitem);

      if (item.key && l10nKey) {
        // Create a <key>
        let shortcut = l10n(l10nKey + ".key");
        let key = createKey({
          doc,
          id: item.key.id,
          shortcut: shortcut,
          keytext: shortcut.startsWith("VK_") ? l10n(l10nKey + ".keytext") : null,
          modifiers: item.key.modifiers,
          oncommand: item.oncommand
        });
        // Refer to the key in order to display the key shortcut at menu ends
        menuitem.setAttribute("key", key.id);
        keys.appendChild(key);
      }
      if (item.additionalKeys) {
        // Create additional <key>
        for (let key of item.additionalKeys) {
          let shortcut = l10n(key.l10nKey + ".key");
          let node = createKey({
            doc,
            id: key.id,
            shortcut: shortcut,
            keytext: shortcut.startsWith("VK_") ? l10n(key.l10nKey + ".keytext") : null,
            modifiers: key.modifiers,
            oncommand: item.oncommand
          });
          keys.appendChild(node);
        }
      }
    }
  }

  // Cache all nodes before insertion to be able to remove them on unload
  let nodes = [];
  for (let node of keys.children) {
    nodes.push(node);
  }
  for (let node of appmenuItems.children) {
    nodes.push(node);
  }
  for (let node of menuItems.children) {
    nodes.push(node);
  }
  FragmentsCache.set(doc, nodes);

  attachKeybindingsToBrowser(doc, keys);

  // There are hardcoded menu items in the Web Developer menus plus it is a
  // location of menu items via overlays from extensions so we want to make
  // sure the last seperator and the "Get More Tools..." items are last.
  // This will emulate the behavior when devtools menu items were actually
  // physically present in browser.xul

  // Tools > Web Developer
  let menu = doc.getElementById("menuWebDeveloperPopup");
  // Insert the Devtools Menu Items before everything else
  menu.insertBefore(menuItems, menu.firstChild);
  // Move the devtools last seperator and Get More Tools menu items to the bottom
  let menu_endSeparator = doc.getElementById("menu_devToolsEndSeparator");
  let menu_getMoreDevtools = doc.getElementById("menu_getMoreDevtools");
  menu.insertBefore(menu_getMoreDevtools, null);
  menu.insertBefore(menu_endSeparator, menu_getMoreDevtools);
  
  // Application Menu > Web Developer (If existant)
  let appmenu = doc.getElementById("appmenu_webDeveloper_popup");
  if (appmenu) {
    // Insert the Devtools Menu Items after the hardcoded idless seperator
    appmenu.insertBefore(appmenuItems, appmenu.childNodes[2].nextSibling);
    // Move the devtools last seperator and Get More Tools menu items to the bottom
    let appmenu_endSeparator = doc.getElementById("appmenu_devToolsEndSeparator");
    let appmenu_getMoreDevtools = doc.getElementById("appmenu_getMoreDevtools");
    appmenu.insertBefore(appmenu_getMoreDevtools, null);
    appmenu.insertBefore(appmenu_endSeparator, appmenu_getMoreDevtools);
  }
}

/**
 * Remove global menus and shortcuts that are not panel specific.
 *
 * @param {XULDocument} doc
 *        The document to which keys and menus are to be added.
 */
function removeTopLevelItems(doc) {
  let nodes = FragmentsCache.get(doc);
  if (!nodes) {
    return;
  }
  FragmentsCache.delete(doc);
  for (let node of nodes) {
    node.remove();
  }
}

/**
 * Add menus and shortcuts to a browser document
 *
 * @param {XULDocument} doc
 *        The document to which keys and menus are to be added.
 */
exports.addMenus = function (doc) {
  addTopLevelItems(doc);

  addAllToolsToMenu(doc);
};

/**
 * Remove menus and shortcuts from a browser document
 *
 * @param {XULDocument} doc
 *        The document to which keys and menus are to be removed.
 */
exports.removeMenus = function (doc) {
  // We only remove top level entries. Per-tool entries are removed while
  // unregistering each tool.
  removeTopLevelItems(doc);
};

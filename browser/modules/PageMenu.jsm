/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

this.EXPORTED_SYMBOLS = ["PageMenu"];

this.PageMenu = function PageMenu() {
}

PageMenu.prototype = {
  PAGEMENU_ATTR: "pagemenu",
  GENERATEDITEMID_ATTR: "generateditemid",

  _popup: null,
  _builder: null,

  // Given a target node, get the context menu for it or its ancestor.
  getContextMenu: function(aTarget) {
    let target = aTarget;
    while (target) {
      let contextMenu = target.contextMenu;
      if (contextMenu) {
        return contextMenu;
      }
      target = target.parentNode;
    }

    return null;
  },

  // Given a target node, generate a JSON object for any context menu
  // associated with it, or null if there is no context menu.
  maybeBuild: function(aTarget) {
    let pageMenu = this.getContextMenu(aTarget);
    if (!pageMenu) {
      return null;
    }

    pageMenu.QueryInterface(Components.interfaces.nsIHTMLMenu);
    pageMenu.sendShowEvent();
    // the show event is not cancelable, so no need to check a result here

    this._builder = pageMenu.createBuilder();
    if (!this._builder) {
      return null;
    }

    pageMenu.build(this._builder);

    // This serializes then parses again, however this could be avoided in
    // the single-process case with further improvement.
    let menuString = this._builder.toJSONString();
    if (!menuString) {
      return null;
    }

    return JSON.parse(menuString);
  },

  // Given a JSON menu object and popup, add the context menu to the popup.
  buildAndAttachMenuWithObject: function(aMenu, aBrowser, aPopup) {
    if (!aMenu) {
      return false;
    }

    let insertionPoint = this.getInsertionPoint(aPopup);
    if (!insertionPoint) {
      return false;
    }

    let fragment = aPopup.ownerDocument.createDocumentFragment();
    this.buildXULMenu(aMenu, fragment);

    let pos = insertionPoint.getAttribute(this.PAGEMENU_ATTR);
    if (pos == "start") {
      insertionPoint.insertBefore(fragment,
                                  insertionPoint.firstChild);
    } else if (pos.startsWith("#")) {
      insertionPoint.insertBefore(fragment, insertionPoint.querySelector(pos));
    } else {
      insertionPoint.appendChild(fragment);
    }

    this._popup = aPopup;

    this._popup.addEventListener("command", this);
    this._popup.addEventListener("popuphidden", this);

    return true;
  },

  // Construct the XUL menu structure for a given JSON object.
  buildXULMenu: function(aNode, aElementForAppending) {
    let document = aElementForAppending.ownerDocument;

    let children = aNode.children;
    for (let child of children) {
      let menuitem;
      switch (child.type) {
        case "menuitem":
          if (!child.id) {
            continue; // Ignore children without ids
          }

          menuitem = document.createElement("menuitem");
          if (child.checkbox) {
            menuitem.setAttribute("type", "checkbox");
            if (child.checked) {
              menuitem.setAttribute("checked", "true");
            }
          }

          if (child.label) {
            menuitem.setAttribute("label", child.label);
          }
          if (child.icon) {
            menuitem.setAttribute("image", child.icon);
            menuitem.className = "menuitem-iconic";
          }
          if (child.disabled) {
            menuitem.setAttribute("disabled", true);
          }

          break;

        case "separator":
          menuitem = document.createElement("menuseparator");
          break;

        case "menu":
          menuitem = document.createElement("menu");
          if (child.label) {
            menuitem.setAttribute("label", child.label);
          }

          let menupopup = document.createElement("menupopup");
          menuitem.appendChild(menupopup);

          this.buildXULMenu(child, menupopup);
          break;
      }

      menuitem.setAttribute(this.GENERATEDITEMID_ATTR, child.id ? child.id : 0);
      aElementForAppending.appendChild(menuitem);
    }
  },

  // Called when the generated menuitem is executed.
  handleEvent: function(event) {
    let type = event.type;
    let target = event.target;
    if (type == "command" && target.hasAttribute(this.GENERATEDITEMID_ATTR)) {
      // If a builder is assigned, call click on it directly. Otherwise, this is
      // likely a menu with data from another process, so send a message to the
      // browser to execute the menuitem.
      if (this._builder) {
        this._builder.click(target.getAttribute(this.GENERATEDITEMID_ATTR));
      }
    } else if (type == "popuphidden" && this._popup == target) {
      this.removeGeneratedContent(this._popup);

      this._popup.removeEventListener("popuphidden", this);
      this._popup.removeEventListener("command", this);

      this._popup = null;
      this._builder = null;
    }
  },

  // Get the first child of the given element with the given tag name.
  getImmediateChild: function(element, tag) {
    let child = element.firstChild;
    while (child) {
      if (child.localName == tag) {
        return child;
      }
      child = child.nextSibling;
    }
    return null;
  },

  // Return the location where the generated items should be inserted into the
  // given popup. They should be inserted as the next sibling of the returned
  // element.
  getInsertionPoint: function(aPopup) {
    if (aPopup.hasAttribute(this.PAGEMENU_ATTR))
      return aPopup;

    let element = aPopup.firstChild;
    while (element) {
      if (element.localName == "menu") {
        let popup = this.getImmediateChild(element, "menupopup");
        if (popup) {
          let result = this.getInsertionPoint(popup);
          if (result) {
            return result;
          }
        }
      }
      element = element.nextSibling;
    }

    return null;
  },

  // Returns true if custom menu items were present.
  maybeBuildAndAttachMenu: function(aTarget, aPopup) {
    let menuObject = this.maybeBuild(aTarget);
    if (!menuObject) {
      return false;
    }

    return this.buildAndAttachMenuWithObject(menuObject, null, aPopup);
  },

  // Remove the generated content from the given popup.
  removeGeneratedContent: function(aPopup) {
    let ungenerated = [];
    ungenerated.push(aPopup);

    let count;
    while (0 != (count = ungenerated.length)) {
      let last = count - 1;
      let element = ungenerated[last];
      ungenerated.splice(last, 1);

      let i = element.childNodes.length;
      while (i-- > 0) {
        let child = element.childNodes[i];
        if (!child.hasAttribute(this.GENERATEDITEMID_ATTR)) {
          ungenerated.push(child);
          continue;
        }
        element.removeChild(child);
      }
    }
  }
}

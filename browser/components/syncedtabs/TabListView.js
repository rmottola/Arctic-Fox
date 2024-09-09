/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {classes: Cc, interfaces: Ci, utils: Cu, results: Cr} = Components;

Cu.import("resource://gre/modules/Services.jsm");

let { getChromeWindow } = Cu.import("resource:///modules/syncedtabs/util.js", {});

let log = Cu.import("resource://gre/modules/Log.jsm", {})
            .Log.repository.getLogger("Sync.RemoteTabs");

this.EXPORTED_SYMBOLS = [
  "TabListView"
];

function getContextMenu(window) {
  return getChromeWindow(window).document.getElementById("SyncedTabsSidebarContext");
}

/*
 * TabListView
 *
 * Given a state, this object will render the corresponding DOM.
 * It maintains no state of it's own. It listens for DOM events
 * and triggers actions that may cause the state to change and
 * ultimately the view to rerender.
 */
function TabListView(window, props) {
  this.props = props;

  this._window = window;
  this._doc = this._window.document;

  this._tabsContainerTemplate = this._doc.getElementById("tabs-container-template");
  this._clientTemplate = this._doc.getElementById("client-template");
  this._emptyClientTemplate = this._doc.getElementById("empty-client-template");
  this._tabTemplate = this._doc.getElementById("tab-template");

  this.container = this._doc.createElement("div");

  this._setupContextMenu();
};

TabListView.prototype = {
  render(state) {
    // Don't rerender anything; just update attributes, e.g. selection
    if (state.canUpdateAll) {
      this._update(state);
      return;
    }
    // Rerender the tab list
    if (state.canUpdateInput) {
      this._updateSearchBox(state);
      this._createList(state);
      return;
    }
    // Create the world anew
    this._create(state);
  },

  // Create the initial DOM from templates
  _create(state) {
    let wrapper = this._doc.importNode(this._tabsContainerTemplate.content, true).firstElementChild;
    this._clearChilden();
    this.container.appendChild(wrapper);

    this.tabsFilter = this.container.querySelector(".tabsFilter");
    this.clearFilter = this.container.querySelector(".textbox-search-clear");
    this.searchBox = this.container.querySelector(".search-box");
    this.list = this.container.querySelector(".list");
    this.searchIcon = this.container.querySelector(".textbox-search-icon");

    if (state.filter) {
      this.tabsFilter.value = state.filter;
    }

    this._createList(state);
    this._updateSearchBox(state);

    this._attachListeners();
  },

  _createList(state) {
    this._clearChilden(this.list);
    for (let client of state.clients) {
      if (state.filter) {
        this._renderFilteredClient(client);
      } else {
        this._renderClient(client);
      }
    }
  },

  destroy() {
    this._teardownContextMenu();
    this.container.remove();
  },

  _update(state) {
    this._updateSearchBox(state);
    for (let client of state.clients) {
      let clientNode = this._doc.getElementById("item-" + client.id);
      if (clientNode) {
        this._updateClient(client, clientNode);
      }

      client.tabs.forEach((tab, index) => {
        let tabNode = this._doc.getElementById('tab-' + client.id + '-' + index);
        this._updateTab(tab, tabNode, index);
      });
    }
  },

  // Client rows are hidden when the list is filtered
  _renderFilteredClient(client, filter) {
    client.tabs.forEach((tab, index) => {
      let node = this._renderTab(client, tab, index);
      this.list.appendChild(node);
    });
  },

  _renderClient(client) {
    let itemNode = client.tabs.length ?
                    this._createClient(client) :
                    this._createEmptyClient(client);

    this._updateClient(client, itemNode);

    let tabsList = itemNode.querySelector(".item-tabs-list");
    client.tabs.forEach((tab, index) => {
      let node = this._renderTab(client, tab, index);
      tabsList.appendChild(node);
    });

    this.list.appendChild(itemNode);
    return itemNode;
  },

  _renderTab(client, tab, index) {
    let itemNode = this._createTab(tab);
    this._updateTab(tab, itemNode, index);
    return itemNode;
  },

  _createClient(item) {
    return this._doc.importNode(this._clientTemplate.content, true).firstElementChild;
  },

  _createEmptyClient(item) {
    return this._doc.importNode(this._emptyClientTemplate.content, true).firstElementChild;
  },

  _createTab(item) {
    return this._doc.importNode(this._tabTemplate.content, true).firstElementChild;
  },

  _clearChilden(node) {
    let parent = node || this.container;
    while (parent.firstChild) {
      parent.removeChild(parent.firstChild);
    }
  },

  _attachListeners() {
    this.list.addEventListener("click", this.onClick.bind(this));
    this.list.addEventListener("keydown", this.onKeyDown.bind(this));
    this.tabsFilter.addEventListener("input", this.onFilter.bind(this));
    this.tabsFilter.addEventListener("focus", this.onFilterFocus.bind(this));
    this.tabsFilter.addEventListener("blur", this.onFilterBlur.bind(this));
    this.clearFilter.addEventListener("click", this.onClearFilter.bind(this));
    this.searchIcon.addEventListener("click", this.onFilterFocus.bind(this));
  },

  _updateSearchBox(state) {
    if (state.filter) {
      this.searchBox.classList.add("filtered");
    } else {
      this.searchBox.classList.remove("filtered");
    }
    if (state.inputFocused) {
      this.searchBox.setAttribute("focused", true);
      this.tabsFilter.focus();
    } else {
      this.searchBox.removeAttribute("focused");
    }
  },

  /**
   * Update the element representing an item, ensuring it's in sync with the
   * underlying data.
   * @param {client} item - Item to use as a source.
   * @param {Element} itemNode - Element to update.
   */
  _updateClient(item, itemNode) {
    itemNode.setAttribute("id", "item-" + item.id);
    itemNode.setAttribute("title", item.name);
    if (item.closed) {
      itemNode.classList.add("closed");
    } else {
      itemNode.classList.remove("closed");
    }
    if (item.selected) {
      itemNode.classList.add("selected");
    } else {
      itemNode.classList.remove("selected");
    }
    if (item.focused) {
      itemNode.focus();
    }
    itemNode.dataset.id = item.id;
    itemNode.querySelector(".item-title").textContent = item.name;

    let icon = itemNode.querySelector(".item-icon-container");
    icon.style.backgroundImage = "url(" + item.icon + ")";
  },

  /**
   * Update the element representing a tab, ensuring it's in sync with the
   * underlying data.
   * @param {tab} item - Item to use as a source.
   * @param {Element} itemNode - Element to update.
   */
  _updateTab(item, itemNode, index) {
    itemNode.setAttribute("title", `${item.title}\n${item.url}`);
    itemNode.setAttribute("id", "tab-" + item.client + '-' + index);
    if (item.selected) {
      itemNode.classList.add("selected");
    } else {
      itemNode.classList.remove("selected");
    }
    if (item.focused) {
      itemNode.focus();
    }
    itemNode.dataset.url = item.url;

    itemNode.querySelector(".item-title").textContent = item.title;

    let icon = itemNode.querySelector(".item-icon-container");
    icon.style.backgroundImage = "url(" + item.icon + ")";
  },

  onClick(event) {
    let itemNode = this._findParentItemNode(event.target);
    if (!itemNode) {
      return;
    }

    if (itemNode.classList.contains("tab")) {
      let url = itemNode.dataset.url;
      if (url) {
        this.props.onOpenTab(url, event);
      }
    }

    if (event.target.classList.contains("item-twisty-container")) {
      this.props.onToggleBranch(itemNode.dataset.id);
      return;
    }

    this._selectRow(itemNode);
  },

  _selectRow(itemNode) {
    this.props.onSelectRow(this._getSelectionPosition(itemNode), itemNode.dataset.id);
  },

  /**
   * Handle a keydown event on the list box.
   * @param {Event} event - Triggering event.
   */
  onKeyDown(event) {
    if (event.keyCode == this._window.KeyEvent.DOM_VK_DOWN) {
      event.preventDefault();
      this.props.onMoveSelectionDown();
    } else if (event.keyCode == this._window.KeyEvent.DOM_VK_UP) {
      event.preventDefault();
      this.props.onMoveSelectionUp();
    } else if (event.keyCode == this._window.KeyEvent.DOM_VK_RETURN) {
      let selectedNode = this.container.querySelector('.item.selected');
      if (selectedNode.dataset.url) {
        this.props.onOpenTab(selectedNode.dataset.url, event);
      } else if (selectedNode) {
        this.props.onToggleBranch(selectedNode.dataset.id);
      }
    }
  },

  onBookmarkTab() {
    let item = this.container.querySelector('.item.selected');
    if (!item || !item.dataset.url) {
      return;
    }

    let uri = item.dataset.url;
    let title = item.querySelector(".item-title").textContent;

    this.props.onBookmarkTab(uri, title);
  },

  onOpenSelected(event) {
    let item = this.container.querySelector('.item.selected');
    if (this._isTab(item) && item.dataset.url) {
      this.props.onOpenTab(item.dataset.url, event);
    }
  },

  onFilter(event) {
    let query = event.target.value;
    this.props.onFilter(query);
  },

  onClearFilter() {
    this.props.onClearFilter();
  },

  onFilterFocus() {
    this.props.onFilterFocus();
  },
  onFilterBlur() {
    this.props.onFilterBlur();
  },

  // Set up the custom context menu
  _setupContextMenu() {
    this._handleContentContextMenu = event =>
        this.handleContentContextMenu(event);
    this._handleContentContextMenuCommand = event =>
        this.handleContentContextMenuCommand(event);

    Services.els.addSystemEventListener(this._window, "contextmenu", this._handleContentContextMenu, false);
    let menu = getContextMenu(this._window);
    menu.addEventListener("command", this._handleContentContextMenuCommand, true);
  },

  _teardownContextMenu() {
    // Tear down context menu
    Services.els.removeSystemEventListener(this._window, "contextmenu", this._handleContentContextMenu, false);
    let menu = getContextMenu(this._window);
    menu.removeEventListener("command", this._handleContentContextMenuCommand, true);
  },

  handleContentContextMenuCommand(event) {
    let id = event.target.getAttribute("id");
    switch (id) {
      case "syncedTabsOpenSelected":
        this.onOpenSelected(event);
        break;
      case "syncedTabsBookmarkSelected":
        this.onBookmarkTab();
        break;
      case "syncedTabsRefresh":
        this.props.onSyncRefresh();
        break;
    }
  },

  handleContentContextMenu(event) {
    let itemNode = this._findParentItemNode(event.target);
    if (itemNode) {
      this._selectRow(itemNode);
    }

    let menu = getContextMenu(this._window);
    this.adjustContextMenu(menu);
    menu.openPopupAtScreen(event.screenX, event.screenY, true, event);
  },

  adjustContextMenu(menu) {
    let item = this.container.querySelector('.item.selected');
    let showTabOptions = this._isTab(item);

    let el = menu.firstChild;

    while (el) {
      if (showTabOptions || el.getAttribute("id") === "syncedTabsRefresh") {
        el.hidden = false;
      } else {
        el.hidden = true;
      }

      el = el.nextSibling;
    }
  },

  /**
   * Find the parent item element, from a given child element.
   * @param {Element} node - Child element.
   * @return {Element} Element for the item, or null if not found.
   */
  _findParentItemNode(node) {
    while (node && node !== this.list && node !== this._doc.documentElement &&
           !node.classList.contains("item")) {
      node = node.parentNode;
    }

    if (node !== this.list && node !== this._doc.documentElement) {
      return node;
    }

    return null;
  },

  _findParentBranchNode(node) {
    while (node && !node.classList.contains("list") && node !== this._doc.documentElement &&
           !node.parentNode.classList.contains("list")) {
      node = node.parentNode;
    }

    if (node !== this.list && node !== this._doc.documentElement) {
      return node;
    }

    return null;
  },

  _getSelectionPosition(itemNode) {
    let parent = this._findParentBranchNode(itemNode);
    let parentPosition = this._indexOfNode(parent.parentNode, parent);
    let childPosition = -1;
    // if the node is not a client, find its position within the parent
    if (parent !== itemNode) {
      childPosition = this._indexOfNode(itemNode.parentNode, itemNode);
    }
    return [parentPosition, childPosition];
  },

  _indexOfNode(parent, child) {
    return Array.prototype.indexOf.call(parent.childNodes, child);
  },

  _isTab(item) {
    return item && item.classList.contains("tab");
  }
};

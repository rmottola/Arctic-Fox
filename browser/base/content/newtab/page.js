#ifdef 0
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */
#endif

/**
 * This singleton represents the whole 'New Tab Page' and takes care of
 * initializing all its components.
 */
var gPage = {
  /**
   * Initializes the page.
   */
  init: function Page_init() {
    // Add ourselves to the list of pages to receive notifications.
    gAllPages.register(this);

    // Listen for 'unload' to unregister this page.
    addEventListener("unload", this, false);

    // Listen for toggle button clicks.
    let button = document.getElementById("newtab-toggle");
    button.addEventListener("click", this, false);

    // Check if the new tab feature is enabled.
    let enabled = gAllPages.enabled;
    if (enabled)
      this._init();

    this._updateAttributes(enabled);
  },

  /**
   * Listens for notifications specific to this page.
   */
  observe: function Page_observe() {
    let enabled = gAllPages.enabled;
    this._updateAttributes(enabled);

    // Initialize the whole page if we haven't done that, yet.
    if (enabled) {
      this._init();
    } else {
      gUndoDialog.hide();
    }
  },

  /**
   * Updates the whole page and the grid when the storage has changed.
   */
  update: function Page_update() {
    // The grid might not be ready yet as we initialize it asynchronously.
    if (gGrid.ready) {
      gGrid.refresh();
    }
  },

  /**
   * Internally initializes the page. This runs only when/if the feature
   * is/gets enabled.
   */
  _init: function Page_init() {
    if (this._initialized)
      return;

    this._initialized = true;

    // Initialize search.
    gSearch.init();

    if (document.hidden) {
      addEventListener("visibilitychange", this);
    } else {
      setTimeout(_ => this.onPageFirstVisible());
    }

    // Initialize and render the grid.
    gGrid.init();

    // Initialize the drop target shim.
    gDropTargetShim.init();

#ifdef XP_MACOSX
    // Workaround to prevent a delay on MacOSX due to a slow drop animation.
    document.addEventListener("dragover", this, false);
    document.addEventListener("drop", this, false);
#endif
  },

  /**
   * Updates the 'page-disabled' attributes of the respective DOM nodes.
   * @param aValue Whether the New Tab Page is enabled or not.
   */
  _updateAttributes: function Page_updateAttributes(aValue) {
    // Set the nodes' states.
    let nodeSelector = "#newtab-grid, #newtab-search-container";
    for (let node of document.querySelectorAll(nodeSelector)) {
      if (aValue)
        node.removeAttribute("page-disabled");
      else
        node.setAttribute("page-disabled", "true");
    }

    // Enables/disables the control and link elements.
    let inputSelector = ".newtab-control, .newtab-link";
    for (let input of document.querySelectorAll(inputSelector)) {
      if (aValue)
        input.removeAttribute("tabindex");
      else
        input.setAttribute("tabindex", "-1");
    }

    // Update the toggle button's title.
    let toggle = document.getElementById("newtab-toggle");
    toggle.setAttribute("title", newTabString(aValue ? "hide" : "show"));
  },

  /**
   * Handles all page events.
   */
  handleEvent: function Page_handleEvent(aEvent) {
    switch (aEvent.type) {
      case "unload":
        gAllPages.unregister(this);
        break;
      case "click":
        gAllPages.enabled = !gAllPages.enabled;
        break;
      case "dragover":
        if (gDrag.isValid(aEvent) && gDrag.draggedSite)
          aEvent.preventDefault();
        break;
      case "drop":
        if (gDrag.isValid(aEvent) && gDrag.draggedSite) {
          aEvent.preventDefault();
          aEvent.stopPropagation();
        }
        break;
      case "visibilitychange":
        setTimeout(() => this.onPageFirstVisible());
        removeEventListener("visibilitychange", this);
        break;
    }
  },

  onPageFirstVisible: function () {
    // Record another page impression.
    Services.telemetry.getHistogramById("NEWTAB_PAGE_SHOWN").add(true);

    for (let site of gGrid.sites) {
      if (site) {
        site.captureIfMissing();
      }
    }

    // Allow the document to reflow so the page has sizing info
    let i = 0;
    let checkSizing = _ => setTimeout(_ => {
      if (document.documentElement.clientWidth == 0) {
        checkSizing();
      }
      else {
        this.onPageFirstSized();
      }
    });
    checkSizing();
  },

  onPageFirstSized: function() {
    // Work backwards to find the first visible site from the end
    let {sites} = gGrid;
    let lastIndex = sites.length;
    while (lastIndex-- > 0) {
      let site = sites[lastIndex];
      if (site) {
        let {node} = site;
        let rect = node.getBoundingClientRect();
        let target = document.elementFromPoint(rect.x + rect.width / 2,
                                               rect.y + rect.height / 2);
        if (node.contains(target)) {
          break;
        }
      }
    }

    DirectoryLinksProvider.reportSitesAction(gGrid.sites, "view", lastIndex);
  }
};

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const promise = require("promise");
const { Task } = require("devtools/shared/task");
const EventEmitter = require("devtools/shared/event-emitter");
const { TouchEventSimulator } = require("devtools/shared/touch/simulator");
const { getOwnerWindow } = require("sdk/tabs/utils");
const { on, off } = require("sdk/event/core");
const { startup } = require("sdk/window/helpers");
const events = require("./events");
const message = require("./utils/message");
const { swapToInnerBrowser } = require("./browser/swap");

const TOOL_URL = "chrome://devtools/content/responsive.html/index.xhtml";

/**
 * ResponsiveUIManager is the external API for the browser UI, etc. to use when
 * opening and closing the responsive UI.
 *
 * While the HTML UI is in an experimental stage, the older ResponsiveUIManager
 * from devtools/client/responsivedesign/responsivedesign.jsm delegates to this
 * object when the pref "devtools.responsive.html.enabled" is true.
 */
const ResponsiveUIManager = exports.ResponsiveUIManager = {
  activeTabs: new Map(),

  /**
   * Toggle the responsive UI for a tab.
   *
   * @param window
   *        The main browser chrome window.
   * @param tab
   *        The browser tab.
   * @return Promise
   *         Resolved when the toggling has completed.  If the UI has opened,
   *         it is resolved to the ResponsiveUI instance for this tab.  If the
   *         the UI has closed, there is no resolution value.
   */
  toggle(window, tab) {
    let action = this.isActiveForTab(tab) ? "close" : "open";
    return this[action + "IfNeeded"](window, tab);
  },

  /**
   * Opens the responsive UI, if not already open.
   *
   * @param window
   *        The main browser chrome window.
   * @param tab
   *        The browser tab.
   * @return Promise
   *         Resolved to the ResponsiveUI instance for this tab when opening is
   *         complete.
   */
  openIfNeeded: Task.async(function* (window, tab) {
    if (!tab.linkedBrowser.isRemoteBrowser) {
      return promise.reject(new Error("RDM only available for remote tabs."));
    }
    if (!this.isActiveForTab(tab)) {
      if (!this.activeTabs.size) {
        on(events.activate, "data", onActivate);
        on(events.close, "data", onClose);
      }
      let ui = new ResponsiveUI(window, tab);
      this.activeTabs.set(tab, ui);
      yield setMenuCheckFor(tab, window);
      yield ui.inited;
      this.emit("on", { tab });
    }
    return this.getResponsiveUIForTab(tab);
  }),

  /**
   * Closes the responsive UI, if not already closed.
   *
   * @param window
   *        The main browser chrome window.
   * @param tab
   *        The browser tab.
   * @return Promise
   *         Resolved (with no value) when closing is complete.
   */
  closeIfNeeded: Task.async(function* (window, tab) {
    if (this.isActiveForTab(tab)) {
      let ui = this.activeTabs.get(tab);
      let destroyed = yield ui.destroy();
      if (!destroyed) {
        // Already in the process of destroying, abort.
        return;
      }
      this.activeTabs.delete(tab);
      if (!this.activeTabs.size) {
        off(events.activate, "data", onActivate);
        off(events.close, "data", onClose);
      }
      this.emit("off", { tab });
      yield setMenuCheckFor(tab, window);
    }
    return promise.resolve();
  }),

  /**
   * Returns true if responsive UI is active for a given tab.
   *
   * @param tab
   *        The browser tab.
   * @return boolean
   */
  isActiveForTab(tab) {
    return this.activeTabs.has(tab);
  },

  /**
   * Return the responsive UI controller for a tab.
   *
   * @param tab
   *        The browser tab.
   * @return ResponsiveUI
   *         The UI instance for this tab.
   */
  getResponsiveUIForTab(tab) {
    return this.activeTabs.get(tab);
  },

  /**
   * Handle GCLI commands.
   *
   * @param window
   *        The main browser chrome window.
   * @param tab
   *        The browser tab.
   * @param command
   *        The GCLI command name.
   * @param args
   *        The GCLI command arguments.
   */
  handleGcliCommand: function (window, tab, command, args) {
    let completed;
    switch (command) {
      case "resize to":
        completed = this.openIfNeeded(window, tab);
        this.activeTabs.get(tab).setViewportSize(args.width, args.height);
        break;
      case "resize on":
        completed = this.openIfNeeded(window, tab);
        break;
      case "resize off":
        completed = this.closeIfNeeded(window, tab);
        break;
      case "resize toggle":
        completed = this.toggle(window, tab);
        break;
      default:
    }
    completed.catch(e => console.error(e));
  }
};

// GCLI commands in ../responsivedesign/resize-commands.js listen for events
// from this object to know when the UI for a tab has opened or closed.
EventEmitter.decorate(ResponsiveUIManager);

/**
 * ResponsiveUI manages the responsive design tool for a specific tab.  The
 * actual tool itself lives in a separate chrome:// document that is loaded into
 * the tab upon opening responsive design.  This object acts a helper to
 * integrate the tool into the surrounding browser UI as needed.
 */
function ResponsiveUI(window, tab) {
  this.browserWindow = window;
  this.tab = tab;
  this.inited = this.init();
}

ResponsiveUI.prototype = {

  /**
   * The main browser chrome window (that holds many tabs).
   */
  browserWindow: null,

  /**
   * The specific browser tab this responsive instance is for.
   */
  tab: null,

  /**
   * Promise resovled when the UI init has completed.
   */
  inited: null,

  /**
   * Flag set when destruction has begun.
   */
  destroying: false,

  /**
   * A window reference for the chrome:// document that displays the responsive
   * design tool.  It is safe to reference this window directly even with e10s,
   * as the tool UI is always loaded in the parent process.  The web content
   * contained *within* the tool UI on the other hand is loaded in the child
   * process.
   */
  toolWindow: null,

  /**
   * Touch event simulator.
   */
  touchEventSimulator: null,

  /**
   * Open RDM while preserving the state of the page.  We use `swapFrameLoaders`
   * to ensure all in-page state is preserved, just like when you move a tab to
   * a new window.
   *
   * For more details, see /devtools/docs/responsive-design-mode.md.
   */
  init: Task.async(function* () {
    let ui = this;
    let toolViewportContentBrowser;

    // Swap page content from the current tab into a viewport within RDM
    this.swap = swapToInnerBrowser({
      tab: this.tab,
      containerURL: TOOL_URL,
      getInnerBrowser: Task.async(function* (containerBrowser) {
        let toolWindow = ui.toolWindow = containerBrowser.contentWindow;
        toolWindow.addEventListener("message", ui);
        yield message.request(toolWindow, "init");
        toolWindow.addInitialViewport("about:blank");
        yield message.wait(toolWindow, "browser-mounted");
        toolViewportContentBrowser =
          toolWindow.document.querySelector("iframe.browser");
        return toolViewportContentBrowser;
      })
    });
    yield this.swap.start();

    // Notify the inner browser to start the frame script
    yield message.request(this.toolWindow, "start-frame-script");

    this.touchEventSimulator =
      new TouchEventSimulator(toolViewportContentBrowser);

    // TODO: Session restore continues to store the tool UI as the page's URL.
    // Most likely related to browser UI's inability to show correct location.
  }),

  /**
   * Close RDM and restore page content back into a regular tab.
   *
   * @return boolean
   *         Whether this call is actually destroying.  False means destruction
   *         was already in progress.
   */
  destroy: Task.async(function* () {
    if (this.destroying) {
      return false;
    }
    this.destroying = true;

    // Ensure init has finished before starting destroy
    yield this.inited;

    // Stop the touch event simulator if it was running
    yield this.touchEventSimulator.stop();

    // Notify the inner browser to stop the frame script
    yield message.request(this.toolWindow, "stop-frame-script");

    // Destroy local state
    let swap = this.swap;
    this.browserWindow = null;
    this.tab = null;
    this.inited = null;
    this.toolWindow = null;
    this.touchEventSimulator = null;
    this.swap = null;

    // Undo the swap and return the content back to a normal tab
    swap.stop();

    return true;
  }),

  handleEvent(event) {
    let { tab, window, toolWindow } = this;

    if (event.origin !== "chrome://devtools") {
      return;
    }

    switch (event.data.type) {
      case "content-resize":
        let { width, height } = event.data;
        this.emit("content-resize", {
          width,
          height,
        });
        break;
      case "exit":
        toolWindow.removeEventListener(event.type, this);
        ResponsiveUIManager.closeIfNeeded(window, tab);
        break;
      case "update-touch-simulation":
        let { enabled } = event.data;
        this.updateTouchSimulation(enabled);
        break;
    }
  },

  updateTouchSimulation: Task.async(function* (enabled) {
    if (enabled) {
      this.touchEventSimulator.start();
    } else {
      this.touchEventSimulator.stop();
    }
  }),

  /**
   * Helper for tests. Assumes a single viewport for now.
   */
  getViewportSize() {
    return this.toolWindow.getViewportSize();
  },

  /**
   * Helper for tests. Assumes a single viewport for now.
   */
  setViewportSize: Task.async(function* (width, height) {
    yield this.inited;
    this.toolWindow.setViewportSize(width, height);
  }),

  /**
   * Helper for tests. Assumes a single viewport for now.
   */
  getViewportBrowser() {
    return this.toolWindow.getViewportBrowser();
  },

};

EventEmitter.decorate(ResponsiveUI.prototype);

const onActivate = (tab) => setMenuCheckFor(tab);

const onClose = ({ window, tabs }) => {
  for (let tab of tabs) {
    ResponsiveUIManager.closeIfNeeded(window, tab);
  }
};

const setMenuCheckFor = Task.async(
  function* (tab, window = getOwnerWindow(tab)) {
    yield startup(window);

    let menu = window.document.getElementById("menu_responsiveUI");
    if (menu) {
      menu.setAttribute("checked", ResponsiveUIManager.isActiveForTab(tab));
    }
  }
);

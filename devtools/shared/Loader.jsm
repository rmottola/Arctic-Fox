/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/* globals NetUtil, FileUtils, OS */

"use strict";

/**
 * Manages the addon-sdk loader instance used to load the developer tools.
 */

var { Constructor: CC, classes: Cc, interfaces: Ci, utils: Cu } = Components;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");

XPCOMUtils.defineLazyModuleGetter(this, "NetUtil", "resource://gre/modules/NetUtil.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "FileUtils", "resource://gre/modules/FileUtils.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "OS", "resource://gre/modules/osfile.jsm");

var { Loader } = Cu.import("resource://gre/modules/commonjs/toolkit/loader.js", {});
var promise = Cu.import("resource://gre/modules/Promise.jsm", {}).Promise;

this.EXPORTED_SYMBOLS = ["DevToolsLoader", "devtools", "BuiltinProvider",
                         "require", "loader"];

/**
 * Providers are different strategies for loading the devtools.
 */

var loaderModules = {
  "Services": Object.create(Services),
  "toolkit/loader": Loader,
  promise,
  PromiseDebugging,
  ChromeUtils,
  ThreadSafeChromeUtils,
  HeapSnapshot,
};
XPCOMUtils.defineLazyGetter(loaderModules, "Debugger", () => {
  // addDebuggerToGlobal only allows adding the Debugger object to a global. The
  // this object is not guaranteed to be a global (in particular on B2G, due to
  // compartment sharing), so add the Debugger object to a sandbox instead.
  let sandbox = Cu.Sandbox(CC('@mozilla.org/systemprincipal;1', 'nsIPrincipal')());
  Cu.evalInSandbox(
    "Components.utils.import('resource://gre/modules/jsdebugger.jsm');" +
    "addDebuggerToGlobal(this);",
    sandbox
  );
  return sandbox.Debugger;
});
XPCOMUtils.defineLazyGetter(loaderModules, "Timer", () => {
  let {setTimeout, clearTimeout} = Cu.import("resource://gre/modules/Timer.jsm", {});
  // Do not return Cu.import result, as SDK loader would freeze Timer.jsm globals...
  return {
    setTimeout,
    clearTimeout
  };
});
XPCOMUtils.defineLazyGetter(loaderModules, "xpcInspector", () => {
  return Cc["@mozilla.org/jsinspector;1"].getService(Ci.nsIJSInspector);
});
XPCOMUtils.defineLazyGetter(loaderModules, "indexedDB", () => {
  // On xpcshell, we can't instantiate indexedDB without crashing
  try {
    let sandbox
      = Cu.Sandbox(CC('@mozilla.org/systemprincipal;1', 'nsIPrincipal')(),
                   {wantGlobalProperties: ["indexedDB"]});
    return sandbox.indexedDB;

  } catch(e) {
    return {};
  }
});

XPCOMUtils.defineLazyGetter(loaderModules, "CSS", () => {
  let sandbox
    = Cu.Sandbox(CC('@mozilla.org/systemprincipal;1', 'nsIPrincipal')(),
                 {wantGlobalProperties: ["CSS"]});
  return sandbox.CSS;
});

XPCOMUtils.defineLazyGetter(loaderModules, "URL", () => {
  let sandbox
    = Cu.Sandbox(CC('@mozilla.org/systemprincipal;1', 'nsIPrincipal')(),
                 {wantGlobalProperties: ["URL"]});
  return sandbox.URL;
});

var sharedGlobalBlocklist = ["sdk/indexed-db"];

/**
 * Used when the tools should be loaded from the Firefox package itself.
 * This is the default case.
 */
function BuiltinProvider() {}
BuiltinProvider.prototype = {
  load: function() {
    this.loader = new Loader.Loader({
      id: "fx-devtools",
      modules: loaderModules,
      paths: {
        // ⚠ DISCUSSION ON DEV-DEVELOPER-TOOLS REQUIRED BEFORE MODIFYING ⚠
        "": "resource://gre/modules/commonjs/",
        // ⚠ DISCUSSION ON DEV-DEVELOPER-TOOLS REQUIRED BEFORE MODIFYING ⚠
        "devtools": "resource://devtools",
        // ⚠ DISCUSSION ON DEV-DEVELOPER-TOOLS REQUIRED BEFORE MODIFYING ⚠
        "gcli": "resource://devtools/shared/gcli/source/lib/gcli",
        // ⚠ DISCUSSION ON DEV-DEVELOPER-TOOLS REQUIRED BEFORE MODIFYING ⚠
        "acorn": "resource://devtools/acorn",
        // ⚠ DISCUSSION ON DEV-DEVELOPER-TOOLS REQUIRED BEFORE MODIFYING ⚠
        "acorn/util/walk": "resource://devtools/acorn/walk.js",
        // ⚠ DISCUSSION ON DEV-DEVELOPER-TOOLS REQUIRED BEFORE MODIFYING ⚠
        "source-map": "resource://devtools/shared/sourcemap/source-map.js",
        // ⚠ DISCUSSION ON DEV-DEVELOPER-TOOLS REQUIRED BEFORE MODIFYING ⚠
        // Allow access to xpcshell test items from the loader.
        "xpcshell-test": "resource://test"
        // ⚠ DISCUSSION ON DEV-DEVELOPER-TOOLS REQUIRED BEFORE MODIFYING ⚠
      },
      globals: this.globals,
      invisibleToDebugger: this.invisibleToDebugger,
      sharedGlobal: true,
      sharedGlobalBlocklist,
    });

    return promise.resolve(undefined);
  },

  unload: function(reason) {
    Loader.unload(this.loader, reason);
    delete this.loader;
  },
};

var gNextLoaderID = 0;

/**
 * The main devtools API.
 * In addition to a few loader-related details, this object will also include all
 * exports from the main module.  The standard instance of this loader is
 * exported as |devtools| below, but if a fresh copy of the loader is needed,
 * then a new one can also be created.
 */
this.DevToolsLoader = function DevToolsLoader() {
  this.require = this.require.bind(this);
  this.lazyGetter = XPCOMUtils.defineLazyGetter.bind(XPCOMUtils);
  this.lazyImporter = XPCOMUtils.defineLazyModuleGetter.bind(XPCOMUtils);
  this.lazyServiceGetter = XPCOMUtils.defineLazyServiceGetter.bind(XPCOMUtils);
  this.lazyRequireGetter = this.lazyRequireGetter.bind(this);
  this.main = this.main.bind(this);
};

DevToolsLoader.prototype = {
  get provider() {
    if (!this._provider) {
      this._loadProvider();
    }
    return this._provider;
  },

  _provider: null,

  get id() {
    if (this._id) {
      return this._id;
    } else {
      return this._id = ++gNextLoaderID;
    }
  },

  /**
   * A dummy version of require, in case a provider hasn't been chosen yet when
   * this is first called.  This will then be replaced by the real version.
   * @see setProvider
   */
  require: function() {
    if (!this._provider) {
      this._loadProvider();
    }
    return this.require.apply(this, arguments);
  },

  /**
   * Define a getter property on the given object that requires the given
   * module. This enables delaying importing modules until the module is
   * actually used.
   *
   * @param Object obj
   *    The object to define the property on.
   * @param String property
   *    The property name.
   * @param String module
   *    The module path.
   * @param Boolean destructure
   *    Pass true if the property name is a member of the module's exports.
   */
  lazyRequireGetter: function (obj, property, module, destructure) {
    Object.defineProperty(obj, property, {
      get: () => {
        // Redefine this accessor property as a data property.
        // Delete it first, to rule out "too much recursion" in case obj is
        // a proxy whose defineProperty handler might unwittingly trigger this
        // getter again.
        delete obj[property];
        let value = destructure
          ? this.require(module)[property]
          : this.require(module || property);
        Object.defineProperty(obj, property, {
          value,
          writable: true,
          configurable: true,
          enumerable: true
        });
        return value;
      },
      configurable: true,
      enumerable: true
    });
  },

  /**
   * Add a URI to the loader.
   * @param string id
   *    The module id that can be used within the loader to refer to this module.
   * @param string uri
   *    The URI to load as a module.
   * @returns The module's exports.
   */
  loadURI: function(id, uri) {
    let module = Loader.Module(id, uri);
    return Loader.load(this.provider.loader, module).exports;
  },

  /**
   * Let the loader know the ID of the main module to load.
   *
   * The loader doesn't need a main module, but it's nice to have.  This
   * will be called by the browser devtools to load the devtools/main module.
   *
   * When only using the server, there's no main module, and this method
   * can be ignored.
   */
  main: function(id) {
    // Ensure the main module isn't loaded twice, because it may have observable
    // side-effects.
    if (this._mainid) {
      return;
    }
    this._mainid = id;
    this._main = Loader.main(this.provider.loader, id);

    // Mirror the main module's exports on this object.
    Object.getOwnPropertyNames(this._main).forEach(key => {
      XPCOMUtils.defineLazyGetter(this, key, () => this._main[key]);
    });

    var events = this.require("sdk/system/events");
    events.emit("devtools-loaded", {});
  },

  /**
   * Override the provider used to load the tools.
   */
  setProvider: function(provider) {
    if (provider === this._provider) {
      return;
    }

    if (this._provider) {
      delete this.require;
      this._provider.unload("newprovider");
    }
    this._provider = provider;

    // Pass through internal loader settings specific to this loader instance
    this._provider.invisibleToDebugger = this.invisibleToDebugger;
    // Changes here should be mirrored to devtools/.eslintrc.
    this._provider.globals = {
      isWorker: false,
      reportError: Cu.reportError,
      atob: atob,
      btoa: btoa,
      _Iterator: Iterator,
      loader: {
        lazyGetter: this.lazyGetter,
        lazyImporter: this.lazyImporter,
        lazyServiceGetter: this.lazyServiceGetter,
        lazyRequireGetter: this.lazyRequireGetter,
        id: this.id,
        main: this.main
      },
      // Make sure `define` function exists.  This allows defining some modules
      // in AMD format while retaining CommonJS compatibility through this hook.
      // JSON Viewer needs modules in AMD format, as it currently uses RequireJS
      // from a content document and can't access our usual loaders.  So, any
      // modules shared with the JSON Viewer should include a define wrapper:
      //
      //   // Make this available to both AMD and CJS environments
      //   define(function(require, exports, module) {
      //     ... code ...
      //   });
      //
      // Bug 1248830 will work out a better plan here for our content module
      // loading needs, especially as we head towards devtools.html.
      define(factory) {
        factory(this.require, this.exports, this.module);
      },
    };
    // Lazy define console in order to load Console.jsm only when it is used
    XPCOMUtils.defineLazyGetter(this._provider.globals, "console", () => {
      return Cu.import("resource://gre/modules/Console.jsm", {}).console;
    });

    this._provider.load();
    this.require = Loader.Require(this._provider.loader, { id: "devtools" });
  },

  /**
   * Choose a default tools provider based on the preferences.
   */
  _loadProvider: function() {
    this.setProvider(new BuiltinProvider());
  },

  /**
   * Reload the current provider.
   */
  reload: function() {
    var events = this.require("sdk/system/events");
    events.emit("startupcache-invalidate", {});

    this._provider.unload("reload");
    delete this._provider;
    let mainid = this._mainid;
    delete this._mainid;
    this._loadProvider();
    this.main(mainid);
  },

  /**
   * Sets whether the compartments loaded by this instance should be invisible
   * to the debugger.  Invisibility is needed for loaders that support debugging
   * of chrome code.  This is true of remote target environments, like Fennec or
   * B2G.  It is not the default case for desktop Firefox because we offer the
   * Browser Toolbox for chrome debugging there, which uses its own, separate
   * loader instance.
   * @see devtools/client/framework/ToolboxProcess.jsm
   */
  invisibleToDebugger: Services.appinfo.name !== "Firefox"
};

// Export the standard instance of DevToolsLoader used by the tools.
this.devtools = this.loader = new DevToolsLoader();

this.require = this.devtools.require.bind(this.devtools);

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

this.EXPORTED_SYMBOLS = ["ContextualIdentityService"];

const {classes: Cc, interfaces: Ci, utils: Cu, results: Cr} = Components;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");

const DEFAULT_TAB_COLOR = "#909090";
const SAVE_DELAY_MS = 1500;

XPCOMUtils.defineLazyGetter(this, "gBrowserBundle", function() {
  return Services.strings.createBundle("chrome://browser/locale/browser.properties");
});

XPCOMUtils.defineLazyGetter(this, "gTextDecoder", function () {
  return new TextDecoder();
});

XPCOMUtils.defineLazyGetter(this, "gTextEncoder", function () {
  return new TextEncoder();
});

XPCOMUtils.defineLazyModuleGetter(this, "AsyncShutdown",
                                  "resource://gre/modules/AsyncShutdown.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "OS",
                                  "resource://gre/modules/osfile.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "DeferredTask",
                                  "resource://gre/modules/DeferredTask.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "FileUtils",
                                  "resource://gre/modules/FileUtils.jsm");

this.ContextualIdentityService = {
  _defaultIdentities: [
    { userContextId: 1,
      public: true,
      icon: "chrome://browser/skin/usercontext/personal.svg",
      color: "#00a7e0",
      label: "userContextPersonal.label",
      accessKey: "userContextPersonal.accesskey",
      alreadyOpened: false,
      telemetryId: 1,
    },
    { userContextId: 2,
      public: true,
      icon: "chrome://browser/skin/usercontext/work.svg",
      color: "#f89c24",
      label: "userContextWork.label",
      accessKey: "userContextWork.accesskey",
      alreadyOpened: false,
      telemetryId: 2,
    },
    { userContextId: 3,
      public: true,
      icon: "chrome://browser/skin/usercontext/banking.svg",
      color: "#7dc14c",
      label: "userContextBanking.label",
      accessKey: "userContextBanking.accesskey",
      alreadyOpened: false,
      telemetryId: 3,
    },
    { userContextId: 4,
      public: true,
      icon: "chrome://browser/skin/usercontext/shopping.svg",
      color: "#ee5195",
      label: "userContextShopping.label",
      accessKey: "userContextShopping.accesskey",
      alreadyOpened: false,
      telemetryId: 4,
    },
    { userContextId: Math.pow(2, 31) - 1,
      public: false,
      icon: "",
      color: "",
      label: "userContextIdInternal.thumbnail",
      accessKey: "",
      alreadyOpened: false },
  ],

  _identities: null,

  _path: null,
  _dataReady: false,

  _saver: null,

  init() {
    this._path = OS.Path.join(OS.Constants.Path.profileDir, "containers.json");

    this._saver = new DeferredTask(() => this.save(), SAVE_DELAY_MS);
    AsyncShutdown.profileBeforeChange.addBlocker("ContextualIdentityService: writing data",
                                                 () => this._saver.finalize());

    this.load();
  },

  load() {
    OS.File.read(this._path).then(bytes => {
      // If synchronous loading happened in the meantime, exit now.
      if (this._dataReady) {
        return;
      }

      try {
        this._identities = JSON.parse(gTextDecoder.decode(bytes));
        this._dataReady = true;
      } catch(error) {
        this.loadError(error);
      }
    }, (error) => {
      this.loadError(error);
    });
  },

  loadError(error) {
    if (!(error instanceof OS.File.Error && error.becauseNoSuchFile) &&
        !(error instanceof Components.Exception &&
          error.result == Cr.NS_ERROR_FILE_NOT_FOUND)) {
      // Let's report the error.
      Cu.reportError(error);
    }

    // If synchronous loading happened in the meantime, exit now.
    if (this._dataReady) {
      return;
    }

    this._identities = this._defaultIdentities;
    this._dataReady = true;

    this.saveSoon();
  },

  saveSoon() {
    this._saver.arm();
  },

  save() {
   let bytes = gTextEncoder.encode(JSON.stringify(this._identities));
   return OS.File.writeAtomic(this._path, bytes,
                              { tmpPath: this._path + ".tmp" });
  },

  ensureDataReady() {
    if (this._dataReady) {
      return;
    }

    try {
      // This reads the file and automatically detects the UTF-8 encoding.
      let inputStream = Cc["@mozilla.org/network/file-input-stream;1"]
                          .createInstance(Ci.nsIFileInputStream);
      inputStream.init(new FileUtils.File(this._path),
                       FileUtils.MODE_RDONLY, FileUtils.PERMS_FILE, 0);
      try {
        let json = Cc["@mozilla.org/dom/json;1"].createInstance(Ci.nsIJSON);
        this._identities = json.decodeFromStream(inputStream,
                                                 inputStream.available());
        this._dataReady = true;
      } finally {
        inputStream.close();
      }
    } catch (error) {
      this.loadError(error);
      return;
    }
  },

  getIdentities() {
    this.ensureDataReady();
    return this._identities.filter(info => info.public);
  },

  getPrivateIdentity(label) {
    this.ensureDataReady();
    return this._identities.find(info => !info.public && info.label == label);
  },

  getIdentityFromId(userContextId) {
    this.ensureDataReady();
    return this._identities.find(info => info.userContextId == userContextId);
  },

  getUserContextLabel(userContextId) {
    let identity = this.getIdentityFromId(userContextId);
    if (!identity.public) {
      return "";
    }
    return gBrowserBundle.GetStringFromName(identity.label);
  },

  setTabStyle(tab) {
    // inline style is only a temporary fix for some bad performances related
    // to the use of CSS vars. This code will be removed in bug 1278177.
    if (!tab.hasAttribute("usercontextid")) {
      tab.style.removeProperty("background-image");
      tab.style.removeProperty("background-size");
      tab.style.removeProperty("background-repeat");
      return;
    }

    let userContextId = tab.getAttribute("usercontextid");
    let identity = this.getIdentityFromId(userContextId);

    let color = identity ? identity.color : DEFAULT_TAB_COLOR;
    tab.style.backgroundImage = "linear-gradient(to right, transparent 20%, " + color + " 30%, " + color + " 70%, transparent 80%)";
    tab.style.backgroundSize = "auto 2px";
    tab.style.backgroundRepeat = "no-repeat";
  },

  telemetry(userContextId) {
    let identity = this.getIdentityFromId(userContextId);

    // Let's ignore unknown identities for now.
    if (!identity || !identity.public) {
      return;
    }

    if (!identity.alreadyOpened) {
      identity.alreadyOpened = true;
      Services.telemetry.getHistogramById("UNIQUE_CONTAINERS_OPENED").add(1);
    }

    Services.telemetry.getHistogramById("TOTAL_CONTAINERS_OPENED").add(1);

    if (identity.telemetryId) {
      Services.telemetry.getHistogramById("CONTAINER_USED")
                        .add(identity.telemetryId);
    }
  },
}

ContextualIdentityService.init();

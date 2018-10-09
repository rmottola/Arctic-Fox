/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

let Ci = Components.interfaces;
let Cc = Components.classes;
let Cu = Components.utils;

Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/PluralForm.jsm");
Cu.import("resource://gre/modules/DownloadUtils.jsm");
Cu.import("resource://gre/modules/AddonManager.jsm");
Cu.import("resource://gre/modules/NetUtil.jsm");
Cu.import("resource://gre/modules/ForgetAboutSite.jsm");

let gFaviconService = Cc["@mozilla.org/browser/favicon-service;1"].
                      getService(Ci.nsIFaviconService);

let gPlacesDatabase = Cc["@mozilla.org/browser/nav-history-service;1"].
                      getService(Ci.nsPIPlacesDatabase).
                      DBConnection.
                      clone(true);

let gSitesStmt = gPlacesDatabase.createAsyncStatement(
                  "SELECT get_unreversed_host(rev_host) AS host " +
                  "FROM moz_places " +
                  "WHERE rev_host > '.' " +
                  "AND visit_count > 0 " +
                  "GROUP BY rev_host " +
                  "ORDER BY MAX(frecency) DESC " +
                  "LIMIT :limit");

let gVisitStmt = gPlacesDatabase.createAsyncStatement(
                  "SELECT SUM(visit_count) AS count " +
                  "FROM moz_places " +
                  "WHERE rev_host = :rev_host");

let gFlash = {
  name: "Shockwave Flash",
  betterName: "Adobe Flash",
  type: "application/x-shockwave-flash",
};

// XXX:
// Is there a better way to do this rather than this hacky comparison?
// Copied this from toolkit/components/passwordmgr/crypto-SDR.js
const MASTER_PASSWORD_MESSAGE = "User canceled master password entry";

/**
 * Permission types that should be tested with testExactPermission, as opposed
 * to testPermission. This is based on what consumers use to test these
 * permissions.
 */
const TEST_EXACT_PERM_TYPES = ["desktop-notification", "geo", "pointerLock"];

/**
 * Site object represents a single site, uniquely identified by a host.
 */
function Site(host) {
  this.host = host;
  this.listitem = null;

  this.httpURI = NetUtil.newURI("http://" + this.host);
  this.httpsURI = NetUtil.newURI("https://" + this.host);
}

Site.prototype = {
  /**
   * Gets the favicon to use for the site. The callback only gets called if
   * a favicon is found for either the http URI or the https URI.
   *
   * @param aCallback
   *        A callback function that takes a favicon image URL as a parameter.
   */
  getFavicon: function Site_getFavicon(aCallback) {
    function invokeCallback(aFaviconURI) {
      try {
        // Use getFaviconLinkForIcon to get image data from the database instead
        // of using the favicon URI to fetch image data over the network.
        aCallback(gFaviconService.getFaviconLinkForIcon(aFaviconURI).spec);
      } catch (e) {
        Cu.reportError("AboutPermissions: " + e);
      }
    }

    // Try to find favicon for both URIs, but always prefer the https favicon.
    gFaviconService.getFaviconURLForPage(this.httpsURI, function(aURI) {
      if (aURI) {
        invokeCallback(aURI);
      } else {
        gFaviconService.getFaviconURLForPage(this.httpURI, function(aURI) {
          if (aURI) {
            invokeCallback(aURI);
          }
        });
      }
    }.bind(this));
  },

  /**
   * Gets the number of history visits for the site.
   *
   * @param aCallback
   *        A function that takes the visit count (a number) as a parameter.
   */
  getVisitCount: function Site_getVisitCount(aCallback) {
    let rev_host = this.host.split("").reverse().join("") + ".";
    gVisitStmt.params.rev_host = rev_host;
    gVisitStmt.executeAsync({
      handleResult: function(aResults) {
        let row = aResults.getNextRow();
        let count = row.getResultByName("count") || 0;
        try {
          aCallback(count);
        } catch (e) {
          Cu.reportError("AboutPermissions: " + e);
        }
      },
      handleError: function(aError) {
        Cu.reportError("AboutPermissions: " + aError);
      },
      handleCompletion: function(aReason) {
      }
    });
  },

  /**
   * Gets the permission value stored for a specified permission type.
   *
   * @param aType
   *        The permission type string stored in permission manager.
   *        e.g. "cookie", "geo", "indexedDB", "popup", "image"
   * @param aResultObj
   *        An object that stores the permission value set for aType.
   *
   * @return A boolean indicating whether or not a permission is set.
   */
  getPermission: function Site_getPermission(aType, aResultObj) {
    // Password saving isn't a nsIPermissionManager permission type, so handle
    // it seperately.
    if (aType == "password") {
      aResultObj.value =  this.loginSavingEnabled
                          ? Ci.nsIPermissionManager.ALLOW_ACTION
                          : Ci.nsIPermissionManager.DENY_ACTION;
      return true;
    }

    let permissionValue;
    if (TEST_EXACT_PERM_TYPES.indexOf(aType) == -1) {
      permissionValue = Services.perms.testPermission(this.httpURI, aType);
    } else {
      permissionValue = Services.perms.testExactPermission(this.httpURI, aType);
    }
    aResultObj.value = permissionValue;

    if (aType.startsWith("plugin")) {
      if (permissionValue == Ci.nsIPermissionManager.PROMPT_ACTION) {
        aResultObj.value = Ci.nsIPermissionManager.UNKNOWN_ACTION;
        return true;
      }
    }

    return permissionValue != Ci.nsIPermissionManager.UNKNOWN_ACTION;
  },

  /**
   * Sets a permission for the site given a permission type and value.
   *
   * @param aType
   *        The permission type string stored in permission manager.
   *        e.g. "cookie", "geo", "indexedDB", "popup", "image"
   * @param aPerm
   *        The permission value to set for the permission type. This should
   *        be one of the constants defined in nsIPermissionManager.
   */
  setPermission: function Site_setPermission(aType, aPerm) {
    // Password saving isn't a nsIPermissionManager permission type, so handle
    // it seperately.
    if (aType == "password") {
      this.loginSavingEnabled = aPerm == Ci.nsIPermissionManager.ALLOW_ACTION;
      return;
    }

    if (aType.startsWith("plugin")) {
      if (aPerm == Ci.nsIPermissionManager.UNKNOWN_ACTION) {
        aPerm = Ci.nsIPermissionManager.PROMPT_ACTION;
      }
    }

    // Using httpURI is kind of bogus, but the permission manager stores
    // the permission for the host, so the right thing happens in the end.
    Services.perms.add(this.httpURI, aType, aPerm);
  },

  /**
   * Clears a user-set permission value for the site given a permission type.
   *
   * @param aType
   *        The permission type string stored in permission manager.
   *        e.g. "cookie", "geo", "indexedDB", "popup", "image"
   */
  clearPermission: function Site_clearPermission(aType) {
    Services.perms.remove(this.host, aType);
  },

  /**
   * Gets logins stored for the site.
   *
   * @return An array of the logins stored for the site.
   */
  get logins() {
    try {
      let httpLogins = Services.logins.findLogins(
          {}, this.httpURI.prePath, "", "");
      let httpsLogins = Services.logins.findLogins(
          {}, this.httpsURI.prePath, "", "");
      return httpLogins.concat(httpsLogins);
    } catch (e) {
      if (!e.message.includes(MASTER_PASSWORD_MESSAGE)) {
        Cu.reportError("AboutPermissions: " + e);
      }
      return [];
    }
  },

  get loginSavingEnabled() {
    // Only say that login saving is blocked if it is blocked for both
    // http and https.
    try {
      return Services.logins.getLoginSavingEnabled(this.httpURI.prePath) &&
             Services.logins.getLoginSavingEnabled(this.httpsURI.prePath);
    } catch (e) {
      if (!e.message.includes(MASTER_PASSWORD_MESSAGE)) {
        Cu.reportError("AboutPermissions: " + e);
      }
      return false;
    }
  },

  set loginSavingEnabled(isEnabled) {
    try {
      Services.logins.setLoginSavingEnabled(this.httpURI.prePath, isEnabled);
      Services.logins.setLoginSavingEnabled(this.httpsURI.prePath, isEnabled);
    } catch (e) {
      if (!e.message.includes(MASTER_PASSWORD_MESSAGE)) {
        Cu.reportError("AboutPermissions: " + e);
      }
    }
  },

  /**
   * Gets cookies stored for the site and base domain.
   *
   * @return An array of the cookies set for the site and base domain.
   */
  get cookies() {
    let cookies = [];
    let enumerator = Services.cookies.enumerator;
    while (enumerator.hasMoreElements()) {
      let cookie = enumerator.getNext().QueryInterface(Ci.nsICookie2);
      if (cookie.host.hasRootDomain(
          AboutPermissions.domainFromHost(this.host))) {
        cookies.push(cookie);
      }
    }
    return cookies;
  },

  /**
   * Removes a set of specific cookies from the browser.
   */
  clearCookies: function Site_clearCookies() {
    this.cookies.forEach(function(aCookie) {
      Services.cookies.remove(aCookie.host, aCookie.name, aCookie.path, false);
    });
  },

  /**
   * Removes all data from the browser corresponding to the site.
   */
  forgetSite: function Site_forgetSite() {
    ForgetAboutSite.removeDataFromDomain(this.host)
                   .catch(Cu.reportError);
  }
}

/**
 * PermissionDefaults object keeps track of default permissions for sites based
 * on global preferences.
 *
 * Inspired by pageinfo/permissions.js
 */
let PermissionDefaults = {
  UNKNOWN: Ci.nsIPermissionManager.UNKNOWN_ACTION,   // 0
  ALLOW: Ci.nsIPermissionManager.ALLOW_ACTION,       // 1
  DENY: Ci.nsIPermissionManager.DENY_ACTION,         // 2
  SESSION: Ci.nsICookiePermission.ACCESS_SESSION,    // 8

  get password() {
    if (Services.prefs.getBoolPref("signon.rememberSignons")) {
      return this.ALLOW;
    }
    return this.DENY;
  },
  set password(aValue) {
    let value = (aValue != this.DENY);
    Services.prefs.setBoolPref("signon.rememberSignons", value);
  },

  IMAGE_ALLOW: 1,
  IMAGE_DENY: 2,
  IMAGE_ALLOW_FIRST_PARTY_ONLY: 3,

  get image() {
    if (Services.prefs.getIntPref("permissions.default.image")
        == this.IMAGE_DENY) {
      return this.IMAGE_DENY;
    } else if (Services.prefs.getIntPref("permissions.default.image")
        == this.IMAGE_ALLOW_FIRST_PARTY_ONLY) {
      return this.IMAGE_ALLOW_FIRST_PARTY_ONLY;
    }
    return this.IMAGE_ALLOW;
  },
  set image(aValue) {
    let value = this.IMAGE_ALLOW; 
    if (aValue == this.IMAGE_DENY) {
      value = this.IMAGE_DENY;
    } else if (aValue == this.IMAGE_ALLOW_FIRST_PARTY_ONLY) {
      value = this.IMAGE_ALLOW_FIRST_PARTY_ONLY;
    }
    Services.prefs.setIntPref("permissions.default.image", value);
  },

  get popup() {
    if (Services.prefs.getBoolPref("dom.disable_open_during_load")) {
      return this.DENY;
    }
    return this.ALLOW;
  },
  set popup(aValue) {
    let value = (aValue == this.DENY);
    Services.prefs.setBoolPref("dom.disable_open_during_load", value);
  },

  // For use with network.cookie.* prefs.
  COOKIE_ACCEPT: 0,
  COOKIE_DENY: 2,
  COOKIE_NORMAL: 0,
  COOKIE_SESSION: 2,

  get cookie() {
    if (Services.prefs.getIntPref("network.cookie.cookieBehavior")
        == this.COOKIE_DENY) {
      return this.DENY;
    }

    if (Services.prefs.getIntPref("network.cookie.lifetimePolicy")
        == this.COOKIE_SESSION) {
      return this.SESSION;
    }
    return this.ALLOW;
  },
  set cookie(aValue) {
    let value = (aValue == this.DENY) ? this.COOKIE_DENY : this.COOKIE_ACCEPT;
    Services.prefs.setIntPref("network.cookie.cookieBehavior", value);

    let lifetimeValue = aValue == this.SESSION ? this.COOKIE_SESSION :
                                                 this.COOKIE_NORMAL;
    Services.prefs.setIntPref("network.cookie.lifetimePolicy", lifetimeValue);
  },

  get ["desktop-notification"]() {
    if (!Services.prefs.getBoolPref("dom.webnotifications.enabled")) {
      return this.DENY;
    }
    // We always ask for permission to enable notifications for a specific
    // site, so there is no global ALLOW.
    return this.UNKNOWN;
  },
  set ["desktop-notification"](aValue) {
    let value = (aValue != this.DENY);
    Services.prefs.setBoolPref("dom.webnotifications.enabled", value);
  },

  get install() {
    if (Services.prefs.getBoolPref("xpinstall.whitelist.required")) {
      return this.DENY;
    }
    return this.ALLOW;
  },
  set install(aValue) {
    let value = (aValue == this.DENY);
    Services.prefs.setBoolPref("xpinstall.whitelist.required", value);
  },

  get geo() {
    if (!Services.prefs.getBoolPref("geo.enabled")) {
      return this.DENY;
    }
    // We always ask for permission to share location with a specific site,
    // so there is no global ALLOW.
    return this.UNKNOWN;
  },
  set geo(aValue) {
    let value = (aValue != this.DENY);
    Services.prefs.setBoolPref("geo.enabled", value);
  },

  get indexedDB() {
    if (!Services.prefs.getBoolPref("dom.indexedDB.enabled")) {
      return this.DENY;
    }
    // We always ask for permission to enable indexedDB storage for a specific
    // site, so there is no global ALLOW.
    return this.UNKNOWN;
  },
  set indexedDB(aValue) {
    let value = (aValue != this.DENY);
    Services.prefs.setBoolPref("dom.indexedDB.enabled", value);
  },

  get fullscreen() {
    if (!Services.prefs.getBoolPref("full-screen-api.enabled")) {
      return this.DENY;
    }
    // We always ask for permission to fullscreen with a specific site,
    // so there is no global ALLOW.
    return this.UNKNOWN;
  },
  set fullscreen(aValue) {
    let value = (aValue != this.DENY);
    Services.prefs.setBoolPref("full-screen-api.enabled", value);
  },

  get pointerLock() {
    if (!Services.prefs.getBoolPref("full-screen-api.pointer-lock.enabled")) {
      return this.DENY;
    }
    // We always ask for permission to hide the mouse pointer
    // with a specific site, so there is no global ALLOW.
    return this.UNKNOWN;
  },
  set pointerLock(aValue) {
    let value = (aValue != this.DENY);
    Services.prefs.setBoolPref("full-screen-api.pointer-lock.enabled", value);
  },
}

/**
 * AboutPermissions manages the about:permissions page.
 */
let AboutPermissions = {
 /**
  * Maximum number of sites to return from the places database.
  */  
  PLACES_SITES_LIMIT_MAX: 100,

  /**
   * When adding sites to the dom sites-list, divide workload into intervals.
   */
  LIST_BUILD_DELAY: 100, // delay between intervals

  /**
   * Stores a mapping of host strings to Site objects.
   */
  _sites: {},

  sitesList: null,
  _selectedSite: null,

  /**
   * For testing, track initializations so we can send notifications.
   */
  _initPlacesDone: false,
  _initServicesDone: false,

  /**
   * This reflects the permissions that we expose in the UI. These correspond
   * to permission type strings in the permission manager, PermissionDefaults,
   * and element ids in aboutPermissions.xul.
   *
   * Potential future additions: "sts/use", "sts/subd"
   */
  _supportedPermissions: ["password", "image", "popup", "cookie",
                          "desktop-notification", "install", "geo", "indexedDB",
                          "fullscreen", "pointerLock"],

  /**
   * Permissions that don't have a global "Allow" option.
   */
  _noGlobalAllow: ["desktop-notification", "geo", "indexedDB", "fullscreen",
                   "pointerLock"],

  /**
   * Permissions that don't have a global "Deny" option.
   */
  _noGlobalDeny: [],

  _stringBundleBrowser: Services.strings
      .createBundle("chrome://browser/locale/browser.properties"),

  _stringBundleAboutPermissions: Services.strings.createBundle(
      "chrome://browser/locale/preferences/aboutPermissions.properties"),

  _initPart1: function() {
    this.initPluginList();
    this.cleanupPluginList();

    this.getSitesFromPlaces();

    this.enumerateServicesGenerator = this.getEnumerateServicesGenerator();
    setTimeout(this.enumerateServicesDriver.bind(this), this.LIST_BUILD_DELAY);
  },

  _initPart2: function() {
    this._supportedPermissions.forEach(function(aType) {
      this.updatePermission(aType);
    }, this);
  },

  /**
   * Called on page load.
   */
  init: function() {
    this.sitesList = document.getElementById("sites-list");

    this._initPart1();

    // Attach observers in case data changes while the page is open.
    Services.prefs.addObserver("signon.rememberSignons", this, false);
    Services.prefs.addObserver("permissions.default.image", this, false);
    Services.prefs.addObserver("dom.disable_open_during_load", this, false);
    Services.prefs.addObserver("network.cookie.", this, false);
    Services.prefs.addObserver("dom.webnotifications.enabled", this, false);
    Services.prefs.addObserver("xpinstall.whitelist.required", this, false);
    Services.prefs.addObserver("geo.enabled", this, false);
    Services.prefs.addObserver("dom.indexedDB.enabled", this, false);
    Services.prefs.addObserver("plugins.click_to_play", this, false);
    Services.prefs.addObserver("full-screen-api.enabled", this, false);
    Services.prefs.addObserver("full-screen-api.pointer-lock.enabled", this, false);
    Services.prefs.addObserver("permissions.places-sites-limit", this, false);

    Services.obs.addObserver(this, "perm-changed", false);
    Services.obs.addObserver(this, "passwordmgr-storage-changed", false);
    Services.obs.addObserver(this, "cookie-changed", false);
    Services.obs.addObserver(this, "browser:purge-domain-data", false);
    Services.obs.addObserver(this, "plugin-info-updated", false);
    Services.obs.addObserver(this, "plugin-list-updated", false);
    Services.obs.addObserver(this, "blocklist-updated", false);
    
    this._observersInitialized = true;
    Services.obs.notifyObservers(null, "browser-permissions-preinit", null);

    this._initPart2();
  },

  sitesReload: function() {
    Object.getOwnPropertyNames(this._sites).forEach(function(prop) {
      AboutPermissions.deleteFromSitesList(prop);
    });
    this._initPart1();
    this._initPart2();
  },

  // XXX copied this from browser-plugins.js - is there a way to share?
  // Map the plugin's name to a filtered version more suitable for user UI.
  makeNicePluginName: function(aName) {
    if (aName == gFlash.name) {
      return gFlash.betterName;
    }

    // Clean up the plugin name by stripping off any trailing version numbers
    // or "plugin". EG, "Foo Bar Plugin 1.23_02" --> "Foo Bar"
    // Do this by first stripping the numbers, etc. off the end, and then
    // removing "Plugin" (and then trimming to get rid of any whitespace).
    // (Otherwise, something like "Java(TM) Plug-in 1.7.0_07" gets mangled.)
    let newName = aName.replace(
        /[\s\d\.\-\_\(\)]+$/, "").replace(/\bplug-?in\b/i, "").trim();
    return newName;
  },

  initPluginList: function() {
    let pluginHost = Cc["@mozilla.org/plugin/host;1"]
                     .getService(Ci.nsIPluginHost);
    let tags = pluginHost.getPluginTags();

    let permissionMap = new Map();

    let permissionEntries = [];
    let XUL_NS =
        "http://www.mozilla.org/keymaster/gatekeeper/there.is.only.xul";
    for (let plugin of tags) {
      for (let mimeType of plugin.getMimeTypes()) {
        if ((mimeType == gFlash.type) && (plugin.name != gFlash.name)) {
          continue;
        }
        let permString = pluginHost.getPermissionStringForType(mimeType);
        if (!permissionMap.has(permString)) {
          let permissionEntry = document.createElementNS(XUL_NS, "box");
          permissionEntry.setAttribute("label",
                                       this.makeNicePluginName(plugin.name)
                                       + " " + plugin.version);
          permissionEntry.setAttribute("tooltiptext", plugin.description);
          permissionEntry.setAttribute("vulnerable", "");
          permissionEntry.setAttribute("mimeType", mimeType);
          permissionEntry.setAttribute("permString", permString);
          permissionEntry.setAttribute("class", "pluginPermission");
          permissionEntry.setAttribute("id", permString + "-entry");
          // If the plugin is disabled, it makes no sense to change its
          // click-to-play status, so don't add it.
          // If the click-to-play pref is not set and the plugin is not
          // click-to-play blocklisted, again click-to-play doesn't apply,
          // so don't add it.
          if (plugin.disabled ||
              (!Services.prefs.getBoolPref("plugins.click_to_play") &&
               (pluginHost.getStateForType(mimeType)
                   != Ci.nsIPluginTag.STATE_CLICKTOPLAY))) {
            permissionEntry.hidden = true;
          } else {
            permissionEntry.hidden = false;
          }
          permissionEntries.push(permissionEntry);
          this._supportedPermissions.push(permString);
          this._noGlobalDeny.push(permString);
          Object.defineProperty(PermissionDefaults, permString, {
            get: function() {
                   return this.isClickToPlay()
                          ? PermissionDefaults.UNKNOWN
                          : PermissionDefaults.ALLOW;
                 }.bind(permissionEntry),
            set: function(aValue) {
                   this.clicktoplay = (aValue == PermissionDefaults.UNKNOWN);
                 }.bind(plugin),
            configurable: true
          });
          permissionMap.set(permString, "");
        }
      }
    }

    if (permissionEntries.length > 0) {
      permissionEntries.sort(function(entryA, entryB) {
        let labelA = entryA.getAttribute("label");
        let labelB = entryB.getAttribute("label");
        return ((labelA < labelB) ? -1 : (labelA == labelB ? 0 : 1));
      });
    }

    let pluginsBox = document.getElementById("plugins-box");
    while (pluginsBox.hasChildNodes()) {
      pluginsBox.removeChild(pluginsBox.firstChild);
    }
    for (let permissionEntry of permissionEntries) {
      pluginsBox.appendChild(permissionEntry);
    }
  },

  cleanupPluginList: function() {
    let pluginsPrefItem = document.getElementById("plugins-pref-item");
    let pluginsBox = document.getElementById("plugins-box");
    let pluginsBoxEmpty = true;
    let pluginsBoxSibling = pluginsBox.firstChild;
    while (pluginsBoxSibling) {
      if (!pluginsBoxSibling.hidden) {
        pluginsBoxEmpty = false;
        break;
      }
      pluginsBoxSibling = pluginsBoxSibling.nextSibling;
    }
    if (pluginsBoxEmpty) {
      pluginsPrefItem.collapsed = true;
    } else {
      pluginsPrefItem.collapsed = false;
    }
  },

  /**
   * Called on page unload.
   */
  cleanUp: function() {
    if (this._observersInitialized) {
      Services.prefs.removeObserver("signon.rememberSignons", this, false);
      Services.prefs.removeObserver("permissions.default.image", this, false);
      Services.prefs.removeObserver("dom.disable_open_during_load", this, false);
      Services.prefs.removeObserver("network.cookie.", this, false);
      Services.prefs.removeObserver("dom.webnotifications.enabled", this, false);
      Services.prefs.removeObserver("xpinstall.whitelist.required", this, false);
      Services.prefs.removeObserver("geo.enabled", this, false);
      Services.prefs.removeObserver("dom.indexedDB.enabled", this, false);
      Services.prefs.removeObserver("plugins.click_to_play", this, false);
      Services.prefs.removeObserver("full-screen-api.enabled", this, false);
      Services.prefs.removeObserver("full-screen-api.pointer-lock.enabled", this, false);
      Services.prefs.removeObserver("permissions.places-sites-limit", this, false);

      Services.obs.removeObserver(this, "perm-changed");
      Services.obs.removeObserver(this, "passwordmgr-storage-changed");
      Services.obs.removeObserver(this, "cookie-changed");
      Services.obs.removeObserver(this, "browser:purge-domain-data");
      Services.obs.removeObserver(this, "plugin-info-updated");
      Services.obs.removeObserver(this, "plugin-list-updated");
      Services.obs.removeObserver(this, "blocklist-updated");
    }

    gSitesStmt.finalize();
    gVisitStmt.finalize();
    gPlacesDatabase.asyncClose(null);
  },

  observe: function(aSubject, aTopic, aData) {
    switch(aTopic) {
      case "perm-changed":
        // Permissions changes only affect individual sites.
        if (!this._selectedSite) {
          break;
        }
        // aSubject is null when nsIPermisionManager::removeAll() is called.
        if (!aSubject) {
          this._supportedPermissions.forEach(function(aType) {
            this.updatePermission(aType);
          }, this);
          break;
        }
        let permission = aSubject.QueryInterface(Ci.nsIPermission);
        // We can't compare selectedSite.host and permission.host here because
        // we need to handle the case where a parent domain was changed in
        // a way that affects the subdomain.
        if (this._supportedPermissions.indexOf(permission.type) != -1) {
          this.updatePermission(permission.type);
        }
        break;
      case "nsPref:changed":
        if (aData == "permissions.places-sites-limit") {
          this.sitesReload();
          return;
        }
        let plugin = false;
        if (aData.startsWith("plugin")) {
          plugin = true;
        }
        if (plugin) {
          this.initPluginList();
        }
        this._supportedPermissions.forEach(function(aType) {
          if (!plugin || (plugin && aType.startsWith("plugin"))) {
            this.updatePermission(aType);
          }
        }, this);
        if (plugin) {
          this.cleanupPluginList();
        }
        break;
      case "passwordmgr-storage-changed":
        this.updatePermission("password");
        if (this._selectedSite) {
          this.updatePasswordsCount();
        }
        break;
      case "cookie-changed":
        if (this._selectedSite) {
          this.updateCookiesCount();
        }
        break;
      case "browser:purge-domain-data":
        this.deleteFromSitesList(aData);
        break;
      case "plugin-info-updated":
      case "plugin-list-updated":
      case "blocklist-updated":
        this.initPluginList();
        this._supportedPermissions.forEach(function(aType) {
          if (aType.startsWith("plugin")) {
            this.updatePermission(aType);
          }
        }, this);
        this.cleanupPluginList();
        break;
    }
  },

  /**
   * Creates Site objects for the top-frecency sites in the places database
   * and stores them in _sites.
   * The number of sites created is controlled by _placesSitesLimit.
   */
  getSitesFromPlaces: function() {
    let _placesSitesLimit = Services.prefs.getIntPref(
        "permissions.places-sites-limit");
    if (_placesSitesLimit <= 0) {
      return;
    }
    if (_placesSitesLimit > this.PLACES_SITES_LIMIT_MAX) {
      _placesSitesLimit = this.PLACES_SITES_LIMIT_MAX;
    }

    gSitesStmt.params.limit = _placesSitesLimit;
    gSitesStmt.executeAsync({
      handleResult: function(aResults) {
        AboutPermissions.startSitesListBatch();
        let row;
        while (row = aResults.getNextRow()) {
          let host = row.getResultByName("host");
          AboutPermissions.addHost(host);
        }
        AboutPermissions.endSitesListBatch();
      },
      handleError: function(aError) {
        Cu.reportError("AboutPermissions: " + aError);
      },
      handleCompletion: function(aReason) {
        // Notify oberservers for testing purposes.
        AboutPermissions._initPlacesDone = true;
        if (AboutPermissions._initServicesDone) {
          Services.obs.notifyObservers(
              null, "browser-permissions-initialized", null);
        }
      }
    });
  },

  /**
   * Drives getEnumerateServicesGenerator to work in intervals.
   */
  enumerateServicesDriver: function() {
    if (this.enumerateServicesGenerator.next()) {
      // Build top sitesList items faster so that the list never seems sparse
      let delay = Math.min(this.sitesList.itemCount * 5, this.LIST_BUILD_DELAY);
      setTimeout(this.enumerateServicesDriver.bind(this), delay);
    } else {
      this.enumerateServicesGenerator.close();
      this._initServicesDone = true;
      if (this._initPlacesDone) {
        Services.obs.notifyObservers(
            null, "browser-permissions-initialized", null);
      }
    }
  },

  /**
   * Finds sites that have non-default permissions and creates Site objects
   * for them if they are not already stored in _sites.
   */
  getEnumerateServicesGenerator: function() {
    let itemCnt = 1;
    let schemeChrome = "chrome";

    try {
      let logins = Services.logins.getAllLogins();
      logins.forEach(function(aLogin) {
        try {
          // aLogin.hostname is a string in origin URL format
          // (e.g. "http://foo.com").
          // newURI will throw for add-ons logins stored in chrome:// URIs
          // i.e.: "chrome://weave" (Sync)
          if (!aLogin.hostname.startsWith(schemeChrome + ":")) {
            let uri = NetUtil.newURI(aLogin.hostname);
            this.addHost(uri.host);
          }
        } catch (e) {
          Cu.reportError("AboutPermissions: " + e);
        }
        itemCnt++;
      }, this);

      let disabledHosts = Services.logins.getAllDisabledHosts();
      disabledHosts.forEach(function(aHostname) {
        try {
          // aHostname is a string in origin URL format (e.g. "http://foo.com").
          // newURI will throw for add-ons logins stored in chrome:// URIs
          // i.e.: "chrome://weave" (Sync)
          if (!aHostname.startsWith(schemeChrome + ":")) {
            let uri = NetUtil.newURI(aHostname);
            this.addHost(uri.host);
          }
        } catch (e) {
          Cu.reportError("AboutPermissions: " + e);
        }
        itemCnt++;
      }, this);
    } catch (e) {
      if (!e.message.includes(MASTER_PASSWORD_MESSAGE)) {
        Cu.reportError("AboutPermissions: " + e);
      }
    }

    let enumerator = Services.perms.enumerator;
    while (enumerator.hasMoreElements()) {
      let permission = enumerator.getNext().QueryInterface(Ci.nsIPermission);
      // Only include sites with exceptions set for supported permission types.
      if (this._supportedPermissions.indexOf(permission.type) != -1) {
        this.addHost(permission.host);
      }
      itemCnt++;
    }

    yield false;
  },

  /**
   * Creates a new Site and adds it to _sites if it's not already there.
   *
   * @param aHost
   *        A host string.
   */
  addHost: function(aHost) {
    if (aHost in this._sites) {
      return;
    }
    let site = new Site(aHost);
    this._sites[aHost] = site;
    this.addToSitesList(site);
  },

  /**
   * Populates sites-list richlistbox with data from Site object.
   *
   * @param aSite
   *        A Site object.
   */
  addToSitesList: function(aSite) {
    let item = document.createElement("richlistitem");
    item.setAttribute("class", "site");
    item.setAttribute("value", aSite.host);

    aSite.getFavicon(function(aURL) {
      item.setAttribute("favicon", aURL);
    });
    aSite.listitem = item;

    // Make sure to only display relevant items when list is filtered.
    let filterValue =
        document.getElementById("sites-filter").value.toLowerCase();
    item.collapsed = aSite.host.toLowerCase().indexOf(filterValue) == -1;

    (this._listFragment || this.sitesList).appendChild(item);
  },

  startSitesListBatch: function() {
    if (!this._listFragment)
      this._listFragment = document.createDocumentFragment();
  },

  endSitesListBatch: function() {
    if (this._listFragment) {
      this.sitesList.appendChild(this._listFragment);
      this._listFragment = null;
    }
  },

  /**
   * Hides sites in richlistbox based on search text in sites-filter textbox.
   */
  filterSitesList: function() {
    let siteItems = this.sitesList.children;
    let filterValue =
        document.getElementById("sites-filter").value.toLowerCase();

    if (filterValue == "") {
      for (let i = 0, iLen = siteItems.length; i < iLen; i++) {
        siteItems[i].collapsed = false;
      }
      return;
    }

    for (let i = 0, iLen = siteItems.length; i < iLen; i++) {
      let siteValue = siteItems[i].value.toLowerCase();
      siteItems[i].collapsed = siteValue.indexOf(filterValue) == -1;
    }
  },

  /**
   * Removes all evidence of the selected site. The "forget this site" observer
   * will call deleteFromSitesList to update the UI.
   */
  forgetSite: function() {
    this._selectedSite.forgetSite();
  },

  /**
   * Deletes sites for a host and all of its sub-domains. Removes these sites
   * from _sites and removes their corresponding elements from the DOM.
   *
   * @param aHost
   *        The host string corresponding to the site to delete.
   */
  deleteFromSitesList: function(aHost) {
    for (let host in this._sites) {
      let site = this._sites[host];
      if (site.host.hasRootDomain(aHost)) {
        if (site == this._selectedSite) {
          // Replace site-specific interface with "All Sites" interface.
          this.sitesList.selectedItem =
              document.getElementById("all-sites-item");
        }

        this.sitesList.removeChild(site.listitem);
        delete this._sites[site.host];
      }
    }
  },

  /**
   * Shows interface for managing site-specific permissions.
   */
  onSitesListSelect: function(event) {
    if (event.target.selectedItem.id == "all-sites-item") {
      // Clear the header label value from the previously selected site.
      document.getElementById("site-label").value = "";
      this.manageDefaultPermissions();
      return;
    }

    let host = event.target.value;
    let site = this._selectedSite = this._sites[host];
    document.getElementById("site-label").value = host;
    document.getElementById("header-deck").selectedPanel =
        document.getElementById("site-header");

    this.updateVisitCount();
    this.updatePermissionsBox();
  },

  /**
   * Shows interface for managing default permissions. This corresponds to
   * the "All Sites" list item.
   */
  manageDefaultPermissions: function() {
    this._selectedSite = null;

    document.getElementById("header-deck").selectedPanel =
      document.getElementById("defaults-header");

    this.updatePermissionsBox();
  },

  /**
   * Updates permissions interface based on selected site.
   */
  updatePermissionsBox: function() {
    this._supportedPermissions.forEach(function(aType) {
      this.updatePermission(aType);
    }, this);

    this.updatePasswordsCount();
    this.updateCookiesCount();
  },

  /**
   * Sets menulist for a given permission to the correct state, based on
   * the stored permission.
   *
   * @param aType
   *        The permission type string stored in permission manager.
   *        e.g. "cookie", "geo", "indexedDB", "popup", "image"
   */
  updatePermission: function(aType) {
    let allowItem = document.getElementById(
        aType + "-" + PermissionDefaults.ALLOW);
    allowItem.hidden = !this._selectedSite &&
                       this._noGlobalAllow.indexOf(aType) != -1;
    let denyItem = document.getElementById(
        aType + "-" + PermissionDefaults.DENY);
    denyItem.hidden = !this._selectedSite &&
                      this._noGlobalDeny.indexOf(aType) != -1;

    let permissionMenulist = document.getElementById(aType + "-menulist");
    let permissionSetDefault = document.getElementById(aType + "-set-default");
    let permissionValue;
    let permissionDefault;
    let pluginPermissionEntry;
    let elementsPrefSetDefault = document.querySelectorAll(".pref-set-default");
    if (!this._selectedSite) {
      let _visibility = "collapse";
      for (let i = 0, iLen = elementsPrefSetDefault.length; i < iLen; i++) {
        elementsPrefSetDefault[i].style.visibility = _visibility;
      }
      permissionSetDefault.style.visibility = _visibility;
      // If there is no selected site, we are updating the default permissions
      // interface.
      permissionValue = PermissionDefaults[aType];
      permissionDefault = permissionValue;
      if (aType == "image") {
        // (aType + "-3") corresponds to ALLOW_FIRST_PARTY_ONLY,
        // which is reserved for global preferences only.
        document.getElementById(aType + "-3").hidden = false;
      } else if (aType == "cookie") {
        // (aType + "-9") corresponds to ALLOW_FIRST_PARTY_ONLY,
        // which is reserved for site-specific preferences only.
        document.getElementById(aType + "-9").hidden = true;
      } else if (aType.startsWith("plugin")) {
        if (!Services.prefs.getBoolPref("plugins.click_to_play")) {
          // It is reserved for site-specific preferences only.
          document.getElementById(aType + "-0").disabled = true;
        }
        pluginPermissionEntry = document.getElementById(aType + "-entry");
        pluginPermissionEntry.setAttribute("vulnerable", "");
        if (pluginPermissionEntry.isBlocklisted()) {
          permissionMenulist.disabled = true;
          permissionMenulist.setAttribute("tooltiptext",
              AboutPermissions._stringBundleAboutPermissions
              .GetStringFromName("pluginBlocklisted"));
        } else {
          permissionMenulist.disabled = false;
          permissionMenulist.setAttribute("tooltiptext", "");
        }
      }
    } else {
      let _visibility = "visible";
      for (let i = 0, iLen = elementsPrefSetDefault.length; i < iLen; i++) {
        elementsPrefSetDefault[i].style.visibility = _visibility;
      }
      permissionSetDefault.style.visibility = _visibility;
      permissionDefault = PermissionDefaults[aType];
      if (aType == "image") {
        document.getElementById(aType + "-3").hidden = true;
      } else if (aType == "cookie") {
        document.getElementById(aType + "-9").hidden = false;
      } else if (aType.startsWith("plugin")) {
        document.getElementById(aType + "-0").disabled = false;
        pluginPermissionEntry = document.getElementById(aType + "-entry");
        let permString = pluginPermissionEntry.getAttribute("permString");
        if (permString.startsWith("plugin-vulnerable:")) {
          let nameVulnerable = " \u2014 "
              + AboutPermissions._stringBundleBrowser
                .GetStringFromName("pluginActivateVulnerable.label");
          pluginPermissionEntry.setAttribute("vulnerable", nameVulnerable);
        }
        permissionMenulist.disabled = false;
        permissionMenulist.setAttribute("tooltiptext", "");
      }
      let result = {};
      permissionValue = this._selectedSite.getPermission(aType, result) ?
                        result.value : permissionDefault;
    }

    if (aType == "image") {
      if (document.getElementById(aType + "-" + permissionValue).hidden) {
        // ALLOW
        permissionValue = 1;
      }
    }
    if (aType.startsWith("plugin")) {
      if (document.getElementById(aType + "-" + permissionValue).disabled) {
        // ALLOW
        permissionValue = 1;
      }
    }

    if (!aType.startsWith("plugin")) {
      let _elementDefault = document.getElementById(aType + "-default");
      if (!this._selectedSite || (permissionValue == permissionDefault)) {
        _elementDefault.setAttribute("value", "");
      } else {
        _elementDefault.setAttribute("value", "*");
      }
    } else {
      let _elementDefaultVisibility;
      if (!this._selectedSite || (permissionValue == permissionDefault)) {
        _elementDefaultVisibility = false;
      } else {
        _elementDefaultVisibility = true;
      }
      pluginPermissionEntry.setDefaultVisibility(_elementDefaultVisibility);
    }

    permissionMenulist.selectedItem = document.getElementById(
        aType + "-" + permissionValue);
  },

  onPermissionCommand: function(event, _default) {
    let pluginHost = Cc["@mozilla.org/plugin/host;1"] 
                     .getService(Ci.nsIPluginHost);
    let permissionMimeType = event.currentTarget.getAttribute("mimeType");
    let permissionType = event.currentTarget.getAttribute("type");
    let permissionValue = event.target.value;

    if (!this._selectedSite) {
      if (permissionType.startsWith("plugin")) {
        let addonValue = AddonManager.STATE_ASK_TO_ACTIVATE;
        switch(permissionValue) {
          case "1":
            addonValue = false;
            break;
          case "2":
            addonValue = true;
            break;
        }

        AddonManager.getAddonsByTypes(["plugin"], function(addons) {
          for (let addon of addons) {
            for (let type of addon.pluginMimeTypes) {
              if ((type.type == gFlash.type) && (addon.name != gFlash.name)) {
                continue;
              }
              if (type.type.toLowerCase() == permissionMimeType.toLowerCase()) {
                addon.userDisabled = addonValue;
                return;
              }
            }
          }
        });
      } else {
        // If there is no selected site, we are setting the default permission.
        PermissionDefaults[permissionType] = permissionValue;
      }
    } else {
      if (_default) {
        this._selectedSite.clearPermission(permissionType);
      } else {
        this._selectedSite.setPermission(permissionType, permissionValue);
      }
    }
  },

  updateVisitCount: function() {
    this._selectedSite.getVisitCount(function(aCount) {
      let visitForm = AboutPermissions._stringBundleAboutPermissions
                      .GetStringFromName("visitCount");
      let visitLabel = PluralForm.get(aCount, visitForm)
                       .replace("#1", aCount);
      document.getElementById("site-visit-count").value = visitLabel;
    });  
  },

  updatePasswordsCount: function() {
    if (!this._selectedSite) {
      document.getElementById("passwords-count").hidden = true;
      document.getElementById("passwords-manage-all-button").hidden = false;
      return;
    }

    let passwordsCount = this._selectedSite.logins.length;
    let passwordsForm = this._stringBundleAboutPermissions
                        .GetStringFromName("passwordsCount");
    let passwordsLabel = PluralForm.get(passwordsCount, passwordsForm)
                                   .replace("#1", passwordsCount);

    document.getElementById("passwords-label").value = passwordsLabel;
    document.getElementById("passwords-manage-button").disabled =
        (passwordsCount < 1);
    document.getElementById("passwords-manage-all-button").hidden = true;
    document.getElementById("passwords-count").hidden = false;
  },

  /**
   * Opens password manager dialog.
   */
  managePasswords: function() {
    let selectedHost = "";
    if (this._selectedSite) {
      selectedHost = this._selectedSite.host;
    }

    let win = Services.wm.getMostRecentWindow("Toolkit:PasswordManager");
    if (win) {
      win.setFilter(selectedHost);
      win.focus();
    } else {
      window.openDialog("chrome://passwordmgr/content/passwordManager.xul",
                        "Toolkit:PasswordManager", "",
                        {filterString : selectedHost});
    }
  },

  domainFromHost: function(aHost) {
    let domain = aHost;
    try {
      domain = Services.eTLD.getBaseDomainFromHost(aHost);
    } catch (e) {
      // getBaseDomainFromHost will fail if the host is an IP address
      // or is empty.
    }

    return domain;
  },

  updateCookiesCount: function() {
    if (!this._selectedSite) {
      document.getElementById("cookies-count").hidden = true;
      document.getElementById("cookies-clear-all-button").hidden = false;
      document.getElementById("cookies-manage-all-button").hidden = false;
      return;
    }

    let cookiesCount = this._selectedSite.cookies.length;
    let cookiesForm = this._stringBundleAboutPermissions
                      .GetStringFromName("cookiesCount");
    let cookiesLabel = PluralForm.get(cookiesCount, cookiesForm)
                                 .replace("#1", cookiesCount);

    document.getElementById("cookies-label").value = cookiesLabel;
    document.getElementById("cookies-clear-button").disabled =
        (cookiesCount < 1);
    document.getElementById("cookies-manage-button").disabled =
        (cookiesCount < 1);
    document.getElementById("cookies-clear-all-button").hidden = true;
    document.getElementById("cookies-manage-all-button").hidden = true;
    document.getElementById("cookies-count").hidden = false;
  },

  /**
   * Clears cookies for the selected site and base domain.
   */
  clearCookies: function() {
    if (!this._selectedSite) {
      return;
    }
    let site = this._selectedSite;
    site.clearCookies(site.cookies);
    this.updateCookiesCount();
  },

  /**
   * Opens cookie manager dialog.
   */
  manageCookies: function() {
    let selectedHost = "";
    let selectedDomain = "";
    if (this._selectedSite) {
      selectedHost = this._selectedSite.host;
      selectedDomain = this.domainFromHost(selectedHost);
    }

    let win = Services.wm.getMostRecentWindow("Browser:Cookies");
    if (win) {
      win.gCookiesWindow.setFilter(selectedDomain);
      win.focus();
    } else {
      window.openDialog("chrome://browser/content/preferences/cookies.xul",
                        "Browser:Cookies", "", {filterString : selectedDomain});
    }
  }
}

// See toolkit/forgetaboutsite/ForgetAboutSite.jsm
String.prototype.hasRootDomain = function hasRootDomain(aDomain) {
  let index = this.indexOf(aDomain);
  if (index == -1) {
    return false;
  }

  if (this == aDomain) {
    return true;
  }

  let prevChar = this[index - 1];
  return (index == (this.length - aDomain.length)) &&
         (prevChar == "." || prevChar == "/");
}

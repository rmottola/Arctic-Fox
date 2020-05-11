/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

Components.utils.import("resource://gre/modules/Services.jsm");
Components.utils.import("resource://gre/modules/XPCOMUtils.jsm");
Components.utils.import("resource://gre/modules/NetUtil.jsm");
Components.utils.import("resource://gre/modules/Task.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "PlacesUtils",
                                  "resource://gre/modules/PlacesUtils.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "Downloads",
                                  "resource://gre/modules/Downloads.jsm");

this.EXPORTED_SYMBOLS = ["ForgetAboutSite"];

/**
 * Returns true if the string passed in is part of the root domain of the
 * current string.  For example, if this is "www.mozilla.org", and we pass in
 * "mozilla.org", this will return true.  It would return false the other way
 * around.
 */
function hasRootDomain(str, aDomain) {
  let index = str.indexOf(aDomain);
  // If aDomain is not found, we know we do not have it as a root domain.
  if (index == -1) {
    return false;
  }

  // If the strings are the same, we obviously have a match.
  if (str == aDomain) {
    return true;
  }

  // Otherwise, we have aDomain as our root domain iff the index of aDomain is
  // aDomain.length subtracted from our length and (since we do not have an
  // exact match) the character before the index is a dot or slash.
  let prevChar = str[index - 1];
  return (index == (str.length - aDomain.length)) &&
         (prevChar == "." || prevChar == "/");
}

const Cc = Components.classes;
const Ci = Components.interfaces;
const Cu = Components.utils;

this.ForgetAboutSite = {
  removeDataFromDomain: Task.async(function* (aDomain) {
    let promises = [];

    // History
    promises.push(Task.spawn(function*() {
      PlacesUtils.history.removePagesFromHost(aDomain, true);
    }).catch(ex => {
      throw new Error("Exception thrown while clearing the history: " + ex);
    }));

    // Cache
    promises.push(Task.spawn(function*() {
      let cs = Cc["@mozilla.org/netwerk/cache-storage-service;1"].
               getService(Ci.nsICacheStorageService);
      // NOTE: There is no way to clear just that domain, so we clear out
      //       everything.
      cs.clear();
    }).catch(ex => {
      throw new Error("Exception thrown while clearing the cache: " + ex);
    }));

    // Image Cache
    promises.push(Task.spawn(function*() {
      let imageCache = Cc["@mozilla.org/image/tools;1"].
                       getService(Ci.imgITools).getImgCacheForDocument(null);
      imageCache.clearCache(false); // true=chrome, false=content
    }).catch(ex => {
      throw new Error("Exception thrown while clearing the image cache: " + ex);
    }));

    // Cookies
    // Need to maximize the number of cookies cleaned here
    promises.push(Task.spawn(function*() {
      let cm = Cc["@mozilla.org/cookiemanager;1"].
               getService(Ci.nsICookieManager2);
      let enumerator = cm.enumerator;
      while (enumerator.hasMoreElements()) {
        let cookie = enumerator.getNext().QueryInterface(Ci.nsICookie2);
        if (hasRootDomain(cookie.host, aDomain)) {
          cm.remove(cookie.host, cookie.name, cookie.path, false);
        }
      }
    }).catch(ex => {
      throw new Error("Exception thrown while clearing cookies: " + ex);
    }));

    // EME
    promises.push(Task.spawn(function*() {
      let mps = Cc["@mozilla.org/goanna-media-plugin-service;1"].
                 getService(Ci.mozIGeckoMediaPluginChromeService);
      mps.forgetThisSite(aDomain);
    }).catch(ex => {
      throw new Error("Exception thrown while Encrypted Media Extensions: " + ex);
    }));

    // Plugin data
    const phInterface = Ci.nsIPluginHost;
    const FLAG_CLEAR_ALL = phInterface.FLAG_CLEAR_ALL;
    let ph = Cc["@mozilla.org/plugin/host;1"].getService(phInterface);
    let tags = ph.getPluginTags();
    for (let i = 0, iLen = tags.length; i < iLen; i++) {
      promises.push(new Promise(resolve => {
        try {
          ph.clearSiteData(tags[i], aDomain, FLAG_CLEAR_ALL, -1, resolve);
        } catch (ex) {
          // Ignore exceptions from the plugin, but resolve the promise.
          // We cannot check if something is a bailout or an exception.
          resolve();
        }
      }));
    }

    // Downloads
    promises.push(Task.spawn(function*() {
      let useJSTransfer = false;
      try {
        // This method throws an exception if the old Download Manager is disabled.
        Services.downloads.activeDownloadCount;
      } catch (ex) {
        useJSTransfer = true;
      }

      if (useJSTransfer) {
        let list = yield Downloads.getList(Downloads.ALL);
        list.removeFinished(download => hasRootDomain(
             NetUtil.newURI(download.source.url).host, aDomain));
      } else {
        let dm = Cc["@mozilla.org/download-manager;1"].
                 getService(Ci.nsIDownloadManager);
        // Active downloads
        for (let enumerator of [dm.activeDownloads, dm.activePrivateDownloads]) {
          while (enumerator.hasMoreElements()) {
            let dl = enumerator.getNext().QueryInterface(Ci.nsIDownload);
            if (hasRootDomain(dl.source.host, aDomain)) {
              dl.cancel();
              dl.remove();
            }
          }

          const deleteAllLike = function(db) {
            // NOTE: This is lossy, but we feel that it is OK to be lossy here and not
            //       invoke the cost of creating a URI for each download entry and
            //       ensure that the hostname matches.
            let stmt = db.createStatement(
              "DELETE FROM moz_downloads " +
              "WHERE source LIKE ?1 ESCAPE '/' " +
              "AND state NOT IN (?2, ?3, ?4)"
            );
            let pattern = stmt.escapeStringForLIKE(aDomain, "/");
            stmt.bindByIndex(0, "%" + pattern + "%");
            stmt.bindByIndex(1, Ci.nsIDownloadManager.DOWNLOAD_DOWNLOADING);
            stmt.bindByIndex(2, Ci.nsIDownloadManager.DOWNLOAD_PAUSED);
            stmt.bindByIndex(3, Ci.nsIDownloadManager.DOWNLOAD_QUEUED);
            try {
              stmt.execute();
            } finally {
              stmt.finalize();
            }
          }

          // Completed downloads
          deleteAllLike(dm.DBConnection);
          deleteAllLike(dm.privateDBConnection);

          // We want to rebuild the list if the UI is showing, so dispatch the
          // observer topic
          let os = Cc["@mozilla.org/observer-service;1"].
                   getService(Ci.nsIObserverService);
          os.notifyObservers(null, "download-manager-remove-download", null);
        }
      }
    }).catch(ex => {
      throw new Error("Exception in clearing Downloads: " + ex);
    }));

    // Passwords
    promises.push(Task.spawn(function*() {
      let lm = Cc["@mozilla.org/login-manager;1"].
               getService(Ci.nsILoginManager);
      // Clear all passwords for domain
      let logins = lm.getAllLogins();
      for (let i = 0, iLen = logins.length; i < iLen; i++) {
        if (hasRootDomain(logins[i].hostname, aDomain)) {
          lm.removeLogin(logins[i]);
        }
      }
      // Clear any "do not save for this site" for this domain
      let disabledHosts = lm.getAllDisabledHosts();
      for (let i = 0, iLen = disabledHosts.length; i < iLen; i++) {
        if (hasRootDomain(disabledHosts[i], aDomain)) {
          lm.setLoginSavingEnabled(disabledHosts, true);
        }
      }
    }).catch(ex => {
      // XXX:
      // Is there a better way to do this rather than this hacky comparison?
      // Copied this from toolkit/components/passwordmgr/crypto-SDR.js
      if (!ex.message.includes("User canceled master password entry")) {
        throw new Error("Exception occured in clearing passwords: " + ex);
      }
    }));

    // Permissions
    let pm = Cc["@mozilla.org/permissionmanager;1"].
             getService(Ci.nsIPermissionManager);
    // Enumerate all of the permissions, and if one matches, remove it
    let enumerator = pm.enumerator;
    while (enumerator.hasMoreElements()) {
      let perm = enumerator.getNext().QueryInterface(Ci.nsIPermission);
      promises.push(new Promise((resolve, reject) => {
        try {
          if (hasRootDomain(perm.host, aDomain)) {
            pm.remove(perm.host, perm.type);
          }
        } catch (ex) {
          // Ignore entry
        } finally {
          resolve();
        }
      }));
    }

    // Offline Storages
    promises.push(Task.spawn(function*() {
      let qm = Cc["@mozilla.org/dom/quota/manager;1"].
               getService(Ci.nsIQuotaManager);
      // delete data from both HTTP and HTTPS sites
      let caUtils = {};
      let scriptLoader = Cc["@mozilla.org/moz/jssubscript-loader;1"].
                         getService(Ci.mozIJSSubScriptLoader);
      scriptLoader.loadSubScript("chrome://global/content/contentAreaUtils.js",
                                 caUtils);
      let httpURI = caUtils.makeURI("http://" + aDomain);
      let httpsURI = caUtils.makeURI("https://" + aDomain);
      qm.clearStoragesForURI(httpURI);
      qm.clearStoragesForURI(httpsURI);
    }).catch(ex => {
      throw new Error("Exception occured while clearing offline storages: " + ex);
    }));

    // Content Preferences
    promises.push(Task.spawn(function*() {
      let cps2 = Cc["@mozilla.org/content-pref/service;1"].
                 getService(Ci.nsIContentPrefService2);
      cps2.removeBySubdomain(aDomain, null, {
        handleCompletion: (reason) => { 
          // Notify other consumers, including extensions
          Services.obs.notifyObservers(null, "browser:purge-domain-data", aDomain);
          if (reason === cps2.COMPLETE_ERROR) {
            throw new Error("Exception occured while clearing content preferences");
          }
        },
        handleError() {}
      });
    }));

    // Predictive network data - like cache, no way to clear this per
    // domain, so just trash it all
    promises.push(Task.spawn(function*() {
      let np = Cc["@mozilla.org/network/predictor;1"].
               getService(Ci.nsINetworkPredictor);
      np.reset();
    }).catch(ex => {
      throw new Error("Exception occured while clearing predictive network data: " + ex);
    }));

    let ErrorCount = 0;
    for (let promise of promises) {
      try {
        yield promise;
      } catch (ex) {
        Cu.reportError(ex);
        ErrorCount++;
      }
    }
    if (ErrorCount !== 0) {
      throw new Error(`There were a total of ${ErrorCount} errors during removal`);
    }
  })
}

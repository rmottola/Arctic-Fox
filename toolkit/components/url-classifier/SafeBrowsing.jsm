/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

this.EXPORTED_SYMBOLS = ["SafeBrowsing"];

const Cc = Components.classes;
const Ci = Components.interfaces;
const Cu = Components.utils;

Cu.import("resource://gre/modules/Services.jsm");

// Log only if browser.safebrowsing.debug is true
function log(...stuff) {
  let logging = null;
  try {
    logging = Services.prefs.getBoolPref("browser.safebrowsing.debug");
  } catch(e) {
    return;
  }
  if (!logging) {
    return;
  }

  var d = new Date();
  let msg = "SafeBrowsing: " + d.toTimeString() + ": " + stuff.join(" ");
  dump(Services.urlFormatter.trimSensitiveURLs(msg) + "\n");
}

// Skip all the ones containining "test", because we never need to ask for
// updates for them.
function getLists(prefName) {
  log("getLists: " + prefName);
  let pref = null;
  try {
    pref = Services.prefs.getCharPref(prefName);
  } catch(e) {
    return null;
  }
  // Splitting an empty string returns [''], we really want an empty array.
  if (!pref) {
    return [];
  }
  return pref.split(",")
    .filter(function(value) { return value.indexOf("test-") == -1; })
    .map(function(value) { return value.trim(); });
}

// These may be a comma-separated lists of tables.
const phishingLists = getLists("urlclassifier.phishTable");
const malwareLists = getLists("urlclassifier.malwareTable");
const downloadBlockLists = getLists("urlclassifier.downloadBlockTable");
const downloadAllowLists = getLists("urlclassifier.downloadAllowTable");
const trackingProtectionLists = getLists("urlclassifier.trackingTable");
const trackingProtectionWhitelists = getLists("urlclassifier.trackingWhitelistTable");
const forbiddenLists = getLists("urlclassifier.forbiddenTable");

this.SafeBrowsing = {

  init: function() {
    if (this.initialized) {
      log("Already initialized");
      return;
    }

    Services.prefs.addObserver("browser.safebrowsing", this.readPrefs.bind(this), false);
    Services.prefs.addObserver("privacy.trackingprotection", this.readPrefs.bind(this), false);
    this.readPrefs();
    this.addMozEntries();

    this.controlUpdateChecking();
    this.initialized = true;

    log("init() finished");
  },

  registerTableWithURLs: function(listname) {
    let listManager = Cc["@mozilla.org/url-classifier/listmanager;1"].
      getService(Ci.nsIUrlListManager);

    let providerName = this.listToProvider[listname];
    let provider = this.providers[providerName];

    listManager.registerTable(listname, providerName, provider.updateURL, provider.gethashURL);
  },

  registerTables: function() {
    for (let i = 0; i < phishingLists.length; ++i) {
      this.registerTableWithURLs(phishingLists[i]);
    }
    for (let i = 0; i < malwareLists.length; ++i) {
      this.registerTableWithURLs(malwareLists[i]);
    }
    for (let i = 0; i < downloadBlockLists.length; ++i) {
      this.registerTableWithURLs(downloadBlockLists[i]);
    }
    for (let i = 0; i < downloadAllowLists.length; ++i) {
      this.registerTableWithURLs(downloadAllowLists[i]);
    }
    for (let i = 0; i < trackingProtectionLists.length; ++i) {
      this.registerTableWithURLs(trackingProtectionLists[i]);
    }
    for (let i = 0; i < trackingProtectionWhitelists.length; ++i) {
      this.registerTableWithURLs(trackingProtectionWhitelists[i]);
    }
    for (let i = 0; i < forbiddenLists.length; ++i) {
      this.registerTableWithURLs(forbiddenLists[i]);
    }
  },


  initialized:      false,
  phishingEnabled:  false,
  malwareEnabled:   false,
  trackingEnabled:  false,
  forbiddenEnabled: false,

  updateURL:             null,
  gethashURL:            null,

  reportURL:             null,

  getReportURL: function(kind, URI) {
    let pref;
    switch (kind) {
      case "Phish":
        pref = "browser.safebrowsing.reportPhishURL";
        break;
      case "PhishMistake":
        pref = "browser.safebrowsing.reportPhishMistakeURL";
        break;
      case "MalwareMistake":
        pref = "browser.safebrowsing.reportMalwareMistakeURL";
        break;

      default:
        let err = "SafeBrowsing getReportURL() called with unknown kind: " + kind;
        Components.utils.reportError(err);
        throw err;
    }
    let reportUrl = Services.urlFormatter.formatURLPref(pref);

    let pageUri = URI.clone();

    // Remove the query to avoid including potentially sensitive data
    if (pageUri instanceof Ci.nsIURL)
      pageUri.query = '';

    reportUrl += encodeURIComponent(pageUri.asciiSpec);

    return reportUrl;
  },


  readPrefs: function() {
    log("reading prefs");

    this.debug = Services.prefs.getBoolPref("browser.safebrowsing.debug");
    this.phishingEnabled = Services.prefs.getBoolPref("browser.safebrowsing.enabled");
    this.malwareEnabled = Services.prefs.getBoolPref("browser.safebrowsing.malware.enabled");
    this.trackingEnabled = Services.prefs.getBoolPref("privacy.trackingprotection.enabled") || Services.prefs.getBoolPref("privacy.trackingprotection.pbmode.enabled");
    this.forbiddenEnabled = Services.prefs.getBoolPref("browser.safebrowsing.forbiddenURIs.enabled");
    this.updateProviderURLs();
    this.registerTables();

    // XXX The listManager backend gets confused if this is called before the
    // lists are registered. So only call it here when a pref changes, and not
    // when doing initialization. I expect to refactor this later, so pardon the hack.
    if (this.initialized) {
      this.controlUpdateChecking();
    }
  },


  updateProviderURLs: function() {
    try {
      var clientID = Services.prefs.getCharPref("browser.safebrowsing.id");
    } catch(e) {
      clientID = Services.appinfo.name;
    }

    log("initializing safe browsing URLs, client id", clientID);

    // Get the different providers
    let branch = Services.prefs.getBranch("browser.safebrowsing.provider.");
    let children = branch.getChildList("", {});
    this.providers = {};
    this.listToProvider = {};

    for (let child of children) {
      log("Child: " + child);
      let prefComponents =  child.split(".");
      let providerName = prefComponents[0];
      this.providers[providerName] = {};
    }

    if (this.debug) {
      let providerStr = "";
      Object.keys(this.providers).forEach(function(provider) {
        if (providerStr === "") {
          providerStr = provider;
        } else {
          providerStr += ", " + provider;
        }
      });
      log("Providers: " + providerStr);
    }

    Object.keys(this.providers).forEach(function(provider) {
      let updateURL = Services.urlFormatter.formatURLPref(
        "browser.safebrowsing.provider." + provider + ".updateURL");
      let gethashURL = Services.urlFormatter.formatURLPref(
        "browser.safebrowsing.provider." + provider + ".gethashURL");
      updateURL = updateURL.replace("SAFEBROWSING_ID", clientID);
      gethashURL = gethashURL.replace("SAFEBROWSING_ID", clientID);

      log("Provider: " + provider + " updateURL=" + updateURL);
      log("Provider: " + provider + " gethashURL=" + gethashURL);

      // Urls used to update DB
      this.providers[provider].updateURL  = updateURL;
      this.providers[provider].gethashURL = gethashURL;

      // Get lists this provider manages
      let lists = getLists("browser.safebrowsing.provider." + provider + ".lists");
      if (lists) {
        lists.forEach(function(list) {
          this.listToProvider[list] = provider;
        }, this);
      } else {
        log("Update URL given but no lists managed for provider: " + provider);
      }
    }, this);
  },

  controlUpdateChecking: function() {
    log("phishingEnabled:", this.phishingEnabled, "malwareEnabled:",
        this.malwareEnabled, "trackingEnabled:", this.trackingEnabled,
       "forbiddenEnabled:", this.forbiddenEnabled);

    let listManager = Cc["@mozilla.org/url-classifier/listmanager;1"].
                      getService(Ci.nsIUrlListManager);

    for (let i = 0; i < phishingLists.length; ++i) {
      if (this.phishingEnabled) {
        listManager.enableUpdate(phishingLists[i]);
      } else {
        listManager.disableUpdate(phishingLists[i]);
      }
    }
    for (let i = 0; i < malwareLists.length; ++i) {
      if (this.malwareEnabled) {
        listManager.enableUpdate(malwareLists[i]);
      } else {
        listManager.disableUpdate(malwareLists[i]);
      }
    }
    for (let i = 0; i < downloadBlockLists.length; ++i) {
      if (this.malwareEnabled) {
        listManager.enableUpdate(downloadBlockLists[i]);
      } else {
        listManager.disableUpdate(downloadBlockLists[i]);
      }
    }
    for (let i = 0; i < downloadAllowLists.length; ++i) {
      if (this.malwareEnabled) {
        listManager.enableUpdate(downloadAllowLists[i]);
      } else {
        listManager.disableUpdate(downloadAllowLists[i]);
      }
    }
    for (let i = 0; i < trackingProtectionLists.length; ++i) {
      if (this.trackingEnabled) {
        listManager.enableUpdate(trackingProtectionLists[i]);
        listManager.enableUpdate(trackingProtectionWhitelists[i]);
      } else {
        listManager.disableUpdate(trackingProtectionLists[i]);
        listManager.disableUpdate(trackingProtectionWhitelists[i]);
      }
    }
    for (let i = 0; i < forbiddenLists.length; ++i) {
      if (this.forbiddenEnabled) {
        listManager.enableUpdate(forbiddenLists[i]);
      } else {
        listManager.disableUpdate(forbiddenLists[i]);
      }
    }
    listManager.maybeToggleUpdateChecking();
  },


  addMozEntries: function() {
    // Add test entries to the DB.
    // XXX bug 779008 - this could be done by DB itself?
    const phishURL    = "itisatrap.org/firefox/its-a-trap.html";
    const malwareURL  = "itisatrap.org/firefox/its-an-attack.html";
    const unwantedURL = "itisatrap.org/firefox/unwanted.html";
    const trackerURLs = [
      "trackertest.org/",
      "itisatracker.org/",
    ];
    const whitelistURL = "itisatrap.org/?resource=itisatracker.org";
    const forbiddenURL  = "itisatrap.org/firefox/forbidden.html";

    let update = "n:1000\ni:test-malware-simple\nad:1\n" +
                 "a:1:32:" + malwareURL.length + "\n" +
                 malwareURL + "\n";
    update += "n:1000\ni:test-phish-simple\nad:1\n" +
              "a:1:32:" + phishURL.length + "\n" +
              phishURL  + "\n";
    update += "n:1000\ni:test-unwanted-simple\nad:1\n" +
              "a:1:32:" + unwantedURL.length + "\n" +
              unwantedURL + "\n";
    update += "n:1000\ni:test-track-simple\n" +
              "ad:" + trackerURLs.length + "\n";
    trackerURLs.forEach((trackerURL, i) => {
      update += "a:" + (i + 1) + ":32:" + trackerURL.length + "\n" +
                trackerURL + "\n";
    });
    update += "n:1000\ni:test-trackwhite-simple\nad:1\n" +
              "a:1:32:" + whitelistURL.length + "\n" +
              whitelistURL;
    update += "n:1000\ni:test-forbid-simple\nad:1\n" +
              "a:1:32:" + forbiddenURL.length + "\n" +
              forbiddenURL;
    log("addMozEntries:", update);

    let db = Cc["@mozilla.org/url-classifier/dbservice;1"].
             getService(Ci.nsIUrlClassifierDBService);

    // nsIUrlClassifierUpdateObserver
    let dummyListener = {
      updateUrlRequested: function() { },
      streamFinished:     function() { },
      updateError:        function() { },
      updateSuccess:      function() { }
    };

    try {
      let tables = "test-malware-simple,test-phish-simple,test-unwanted-simple,test-track-simple,test-trackwhite-simple,test-forbid-simple";
      db.beginUpdate(dummyListener, tables, "");
      db.beginStream("", "");
      db.updateStream(update);
      db.finishStream();
      db.finishUpdate();
    } catch(ex) {
      // beginUpdate will throw harmlessly if there's an existing update in progress, ignore failures.
      log("addMozEntries failed!", ex);
    }
  },
};

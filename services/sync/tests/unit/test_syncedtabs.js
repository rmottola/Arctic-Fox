/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*-
 * vim:set ts=2 sw=2 sts=2 et:
*/
"use strict";

Cu.import("resource://services-sync/main.js");
Cu.import("resource://services-sync/SyncedTabs.jsm");
Cu.import("resource://gre/modules/Log.jsm");

const faviconService = Cc["@mozilla.org/browser/favicon-service;1"]
                       .getService(Ci.nsIFaviconService);

Log.repository.getLogger("Sync.RemoteTabs").addAppender(new Log.DumpAppender());

// A mock "Tabs" engine which the SyncedTabs module will use instead of the real
// engine. We pass a constructor that Sync creates.
function MockTabsEngine() {
  this.clients = {}; // We'll set this dynamically
}

MockTabsEngine.prototype = {
  name: "tabs",
  enabled: true,

  getAllClients() {
    return this.clients;
  },

  getOpenURLs() {
    return new Set();
  },
}

// A clients engine that doesn't need to be a constructor.
let MockClientsEngine = {
  isMobile(guid) {
    if (!guid.endsWith("desktop") && !guid.endsWith("mobile")) {
      throw new Error("this module expected guids to end with 'desktop' or 'mobile'");
    }
    return guid.endsWith("mobile");
  },
  remoteClientExists(id) {
    return !id.startsWith("guid_stale");
  },
}

// Configure Sync with our mock tabs engine and force it to become initialized.
Services.prefs.setCharPref("services.sync.username", "someone@somewhere.com");

Weave.Service.engineManager.unregister("tabs");
Weave.Service.engineManager.register(MockTabsEngine);
Weave.Service.clientsEngine = MockClientsEngine;

// Tell the Sync XPCOM service it is initialized.
let weaveXPCService = Cc["@mozilla.org/weave/service;1"]
                        .getService(Ci.nsISupports)
                        .wrappedJSObject;
weaveXPCService.ready = true;

function configureClients(clients) {
  // Configure the instance Sync created.
  let engine = Weave.Service.engineManager.get("tabs");
  // each client record is expected to have an id.
  for (let [guid, client] in Iterator(clients)) {
    client.id = guid;
  }
  engine.clients = clients;
  // Send an observer that pretends the engine just finished a sync.
  Services.obs.notifyObservers(null, "weave:engine:sync:finish", "tabs");
}

// The tests.
add_task(function* test_noClients() {
  // no clients, can't be tabs.
  yield configureClients({});

  let tabs = yield SyncedTabs.getTabClients();
  equal(Object.keys(tabs).length, 0);
});

add_task(function* test_clientWithTabs() {
  yield configureClients({
    guid_desktop: {
      clientName: "My Desktop",
      tabs: [
      {
        urlHistory: ["http://foo.com/"],
        icon: "http://foo.com/favicon",
      }],
    },
    guid_mobile: {
      clientName: "My Phone",
      tabs: [],
    }
  });

  let clients = yield SyncedTabs.getTabClients();
  equal(clients.length, 2);
  clients.sort((a, b) => { return a.name.localeCompare(b.name);});
  equal(clients[0].tabs.length, 1);
  equal(clients[0].tabs[0].url, "http://foo.com/");
  equal(clients[0].tabs[0].icon, "http://foo.com/favicon");
  // second client has no tabs.
  equal(clients[1].tabs.length, 0);
});

add_task(function* test_staleClientWithTabs() {
  yield configureClients({
    guid_desktop: {
      clientName: "My Desktop",
      tabs: [
      {
        urlHistory: ["http://foo.com/"],
        icon: "http://foo.com/favicon",
      }],
    },
    guid_mobile: {
      clientName: "My Phone",
      tabs: [],
    },
    guid_stale_mobile: {
      clientName: "My Deleted Phone",
      tabs: [],
    },
    guid_stale_desktop: {
      clientName: "My Deleted Laptop",
      tabs: [
      {
        urlHistory: ["https://bar.com/"],
        icon: "https://bar.com/favicon",
      }],
    },
  });
  let clients = yield SyncedTabs.getTabClients();
  clients.sort((a, b) => { return a.name.localeCompare(b.name);});
  equal(clients.length, 2);
  equal(clients[0].tabs.length, 1);
  equal(clients[0].tabs[0].url, "http://foo.com/");
  equal(clients[1].tabs.length, 0);
});

add_task(function* test_clientWithTabsIconsDisabled() {
  Services.prefs.setBoolPref("services.sync.syncedTabs.showRemoteIcons", false);
  yield configureClients({
    guid_desktop: {
      clientName: "My Desktop",
      tabs: [
      {
        urlHistory: ["http://foo.com/"],
        icon: "http://foo.com/favicon",
      }],
    },
  });

  let clients = yield SyncedTabs.getTabClients();
  equal(clients.length, 1);
  clients.sort((a, b) => { return a.name.localeCompare(b.name);});
  equal(clients[0].tabs.length, 1);
  equal(clients[0].tabs[0].url, "http://foo.com/");
  // expect the default favicon due to the pref being false.
  equal(clients[0].tabs[0].icon, faviconService.defaultFavicon.spec);
  Services.prefs.clearUserPref("services.sync.syncedTabs.showRemoteIcons");
});

add_task(function* test_filter() {
  // Nothing matches.
  yield configureClients({
    guid_desktop: {
      clientName: "My Desktop",
      tabs: [
      {
        urlHistory: ["http://foo.com/"],
        title: "A test page.",
      },
      {
        urlHistory: ["http://bar.com/"],
        title: "Another page.",
      }],
    },
  });

  let clients = yield SyncedTabs.getTabClients("foo");
  equal(clients.length, 1);
  equal(clients[0].tabs.length, 1);
  equal(clients[0].tabs[0].url, "http://foo.com/");
  // check it matches the title.
  clients = yield SyncedTabs.getTabClients("test");
  equal(clients.length, 1);
  equal(clients[0].tabs.length, 1);
  equal(clients[0].tabs[0].url, "http://foo.com/");
});

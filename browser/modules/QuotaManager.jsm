/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
 
this.EXPORTED_SYMBOLS = ["QuotaManagerHelper"];

Components.utils.import('resource://gre/modules/Services.jsm');

const Cc = Components.classes;
const Ci = Components.interfaces;

this.QuotaManagerHelper = {
  clear: function(isShutDown) {
    try {
      var stord = Services.dirsvc.get("ProfD", Ci.nsIFile);
      stord.append("storage");
      if (stord.exists() && stord.isDirectory()) {
        var doms = {};
        for (var stor of ["default", "permanent", "temporary"]) {
          var storsubd = stord.clone();
          storsubd.append(stor);
          if (storsubd.exists() && storsubd.isDirectory()) {
            var entries = storsubd.directoryEntries;
            while(entries.hasMoreElements()) {
              var host, entry = entries.getNext();
              entry.QueryInterface(Ci.nsIFile);
              if ((host = /^(https?|file)\+\+\+(.+)$/.exec(entry.leafName)) !== null) {
                if (isShutDown) {
                  entry.remove(true);
                } else {
                  doms[host[1] + "://" + host[2]] = true;
                }
              }
            }
          }
        }
        var qm = Cc["@mozilla.org/dom/quota/manager;1"].getService(Ci.nsIQuotaManager);
        for (var dom in doms) {
          var uri = Services.io.newURI(dom, null, null);
          qm.clearStoragesForURI(uri);
        }
      }
    } catch(er) {}
  }
};

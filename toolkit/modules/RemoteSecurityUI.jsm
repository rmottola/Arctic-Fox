// -*- indent-tabs-mode: nil; js-indent-level: 2 -*-
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

this.EXPORTED_SYMBOLS = ["RemoteSecurityUI"];

const Ci = Components.interfaces;
const Cc = Components.classes;
const Cu = Components.utils;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");

function RemoteSecurityUI()
{
    this._state = 0;
    this._SSLStatus = null;
}

RemoteSecurityUI.prototype = {
  QueryInterface: XPCOMUtils.generateQI([Ci.nsISSLStatusProvider, Ci.nsISecureBrowserUI]),

  // nsISecureBrowserUI
  get state() { return this._state; },
  get tooltipText() { return ""; },

  // nsISSLStatusProvider
  get SSLStatus() { return this._SSLStatus; },

  _update: function (state, status) {
      let deserialized = null;
      if (status) {
        let helper = Cc["@mozilla.org/network/serialization-helper;1"]
                      .getService(Components.interfaces.nsISerializationHelper);

        deserialized = helper.deserializeObject(status)
        deserialized.QueryInterface(Ci.nsISSLStatus);
      }

      // We must check the Extended Validation (EV) state here, on the chrome
      // process, because NSS is needed for that determination.
      if (deserialized && deserialized.isExtendedValidation)
        state |= Ci.nsIWebProgressListener.STATE_IDENTITY_EV_TOPLEVEL;

      this._state = state;
      this._SSLStatus = deserialized;
  }
};
// -*- indent-tabs-mode: nil; js-indent-level: 2 -*-
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

this.EXPORTED_SYMBOLS = ["RemoteSecurityUI"];

const Ci = Components.interfaces;
const Cc = Components.classes;
const Cu = Components.utils;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");

function RemoteSecurityUI()
{
    this._SSLStatus = null;
    this._state = 0;
}

RemoteSecurityUI.prototype = {
  QueryInterface: XPCOMUtils.generateQI([Ci.nsISSLStatusProvider, Ci.nsISecureBrowserUI]),

  // nsISSLStatusProvider
  get SSLStatus() { return this._SSLStatus; },

  // nsISecureBrowserUI
  get state() { return this._state; },
  get tooltipText() { return ""; },

  _update: function (aStatus, aState) {
    this._SSLStatus = aStatus;
    this._state = aState;
  }
};

/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* exported Cr, LoopRoomsInternal, timerHandlers, kMockWebSocketChannelName,
            kWebSocketChannelContractID, kServerPushUrl, kLoopServerUrl,
            setupFakeLoopServer, setupFakeFxAUserProfile, waitForCondition,
            getLoopString, extend */

var { classes: Cc, interfaces: Ci, utils: Cu, results: Cr } = Components;

// Initialize this before the imports, as some of them need it.
do_get_profile();

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");

// Setup the add-ons manager for this test.
Cu.import("resource://gre/modules/FileUtils.jsm");

function registerDirectory(key, dir) {
  let dirProvider = {
    getFile: function(prop, persistent) {
      persistent.value = false;
      if (prop == key) {
        return dir.clone();
      }
      return null;
    },

    QueryInterface: XPCOMUtils.generateQI([Ci.nsIDirectoryServiceProvider,
                                           Ci.nsISupports])
  };
  Services.dirsvc.registerProvider(dirProvider);
}

// As we're not running in application, we need to setup the features directory
// manually.
const distroDir = FileUtils.getDir("GreD", ["browser", "features"], true);
registerDirectory("XREAppFeat", distroDir);

// Set up application info - xpcshell doesn't have this by default.
Cu.import("resource://testing-common/AppInfo.jsm");
updateAppInfo();

// Now trigger the addons-startup, and hence startup the manager itself. This
// should load the manager correctly.
var gInternalManager = Cc["@mozilla.org/addons/integration;1"]
  .getService(Ci.nsIObserver)
  .QueryInterface(Ci.nsITimerCallback);

gInternalManager.observe(null, "addons-startup", null);

Cu.import("resource://gre/modules/Http.jsm");
Cu.import("resource://testing-common/httpd.js");
Cu.import("chrome://loop/content/modules/MozLoopService.jsm");
Cu.import("resource://gre/modules/Promise.jsm");
Cu.import("chrome://loop/content/modules/LoopRooms.jsm");
Cu.import("resource://gre/modules/osfile.jsm");
const { MozLoopServiceInternal } = Cu.import("chrome://loop/content/modules/MozLoopService.jsm", {});
const { LoopRoomsInternal, timerHandlers } = Cu.import("chrome://loop/content/modules/LoopRooms.jsm", {});

XPCOMUtils.defineLazyModuleGetter(this, "MozLoopPushHandler",
                                  "chrome://loop/content/modules/MozLoopPushHandler.jsm");

const kMockWebSocketChannelName = "Mock WebSocket Channel";
const kWebSocketChannelContractID = "@mozilla.org/network/protocol;1?name=wss";

const kServerPushUrl = "ws://localhost";
const kLoopServerUrl = "http://localhost:3465";
const kEndPointUrl = "http://example.com/fake";
const kUAID = "f47ac11b-58ca-4372-9567-0e02b2c3d479";

// Fake loop server
var loopServer;

// Ensure loop is always enabled for tests
Services.prefs.setBoolPref("loop.enabled", true);

// Cleanup function for all tests
do_register_cleanup(() => {
  Services.prefs.clearUserPref("loop.enabled");
  MozLoopService.errors.clear();
});

function setupFakeLoopServer() {
  loopServer = new HttpServer();
  loopServer.start(-1);

  Services.prefs.setCharPref("loop.server",
    "http://localhost:" + loopServer.identity.primaryPort);

  MozLoopServiceInternal.mocks.pushHandler = mockPushHandler;

  do_register_cleanup(function() {
    loopServer.stop(function() {});
    MozLoopServiceInternal.mocks.pushHandler = undefined;
  });
}

/**
 * Sets up the userProfile to make the service think we're logged into FxA.
 */
function setupFakeFxAUserProfile() {
  MozLoopServiceInternal.fxAOAuthTokenData = { token_type: "bearer" };
  MozLoopServiceInternal.fxAOAuthProfile = { email: "fake@invalid.com" };

  do_register_cleanup(function() {
    MozLoopServiceInternal.fxAOAuthTokenData = null;
    MozLoopServiceInternal.fxAOAuthProfile = null;
  });
}

function waitForCondition(aConditionFn, aMaxTries = 50, aCheckInterval = 100) {
  function tryAgain() {
    function tryNow() {
      tries++;
      if (aConditionFn()) {
        deferred.resolve();
      } else if (tries < aMaxTries) {
        tryAgain();
      } else {
        deferred.reject("Condition timed out: " + aConditionFn.toSource());
      }
    }
    do_timeout(aCheckInterval, tryNow);
  }
  let deferred = Promise.defer();
  let tries = 0;
  tryAgain();
  return deferred.promise;
}

function getLoopString(stringID) {
  return MozLoopServiceInternal.localizedStrings.get(stringID);
}

/**
 * This is used to fake push registration and notifications for
 * MozLoopService tests. There is only one object created per test instance, as
 * once registration has taken place, the object cannot currently be changed.
 */
var mockPushHandler = {
  // This sets the registration result to be returned when initialize
  // is called. By default, it is equivalent to success.
  registrationResult: null,
  registrationPushURL: null,
  notificationCallback: {},
  registeredChannels: {},

  /**
   * MozLoopPushHandler API
   */
  initialize: function(options = {}) {
    if ("mockWebSocket" in options) {
      this._mockWebSocket = options.mockWebSocket;
    }
  },

  register: function(channelId, registerCallback, notificationCallback) {
    this.notificationCallback[channelId] = notificationCallback;
    this.registeredChannels[channelId] = this.registrationPushURL;
    registerCallback(this.registrationResult, this.registrationPushURL, channelId);
  },

  unregister: function() {
    return;
  },

  /**
   * Test-only API to simplify notifying a push notification result.
   */
  notify: function(version, chanId) {
    this.notificationCallback[chanId](version, chanId);
  }
};

/**
 * Mock nsIWebSocketChannel for tests. This mocks the WebSocketChannel, and
 * enables us to check parameters and return messages similar to the push
 * server.
 */
function MockWebSocketChannel() {}

MockWebSocketChannel.prototype = {
  QueryInterface: XPCOMUtils.generateQI(Ci.nsIWebSocketChannel),

  initRegStatus: 0,

  defaultMsgHandler: function() {
    // Treat as a ping
    this.listener.onMessageAvailable(this.context,
                                     JSON.stringify({}));
    return;
  },

  /**
   * nsIWebSocketChannel implementations.
   * See nsIWebSocketChannel.idl for API details.
   */
  asyncOpen: function(aURI, aOrigin, aWindowId, aListener, aContext) {
    this.uri = aURI;
    this.origin = aOrigin;
    this.listener = aListener;
    this.context = aContext;
    this.windowId = aWindowId;

    this.listener.onStart(this.context);
  },

  sendMsg: function(aMsg) {
    var message = JSON.parse(aMsg);

    switch (message.messageType) {
      case "hello":
        this.listener.onMessageAvailable(this.context,
          JSON.stringify({ messageType: "hello",
                          uaid: kUAID }));
        break;
      case "register": {
        this.channelID = message.channelID;
        let statusCode = 200;
        if (this.initRegStatus) {
          statusCode = this.initRegStatus;
          this.initRegStatus = 0;
        }
        this.listener.onMessageAvailable(this.context,
          JSON.stringify({ messageType: "register",
                          status: statusCode,
                          channelID: this.channelID,
                          pushEndpoint: kEndPointUrl }));
        break;
      }
      default:
        this.defaultMsgHandler && this.defaultMsgHandler(message);
    }
  },

  close: function(aCode) {
    this.stop(aCode);
  },

  notify: function(version) {
    this.listener.onMessageAvailable(this.context,
      JSON.stringify({
        messageType: "notification", updates: [{
          channelID: this.channelID,
          version: version
        }]
    }));
  },

  stop: function(err) {
    this.listener.onStop(this.context, err || -1);
  },

  serverClose: function(err) {
    this.listener.onServerClose(this.context, err || -1);
  }
};

const extend = function(target, source) {
  for (let key of Object.getOwnPropertyNames(source)) {
    target[key] = source[key];
  }
  return target;
};

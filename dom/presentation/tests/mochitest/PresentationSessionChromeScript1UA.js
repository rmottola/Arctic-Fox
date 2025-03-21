/* vim: set shiftwidth=2 tabstop=2 autoindent cindent expandtab: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
* You can obtain one at http://mozilla.org/MPL/2.0/. */

'use strict';

const { classes: Cc, interfaces: Ci, manager: Cm, utils: Cu, results: Cr } = Components;

Cu.import('resource://gre/modules/XPCOMUtils.jsm');

const uuidGenerator = Cc["@mozilla.org/uuid-generator;1"]
                      .getService(Ci.nsIUUIDGenerator);

function debug(str) {
  // dump('DEBUG -*- PresentationSessionChromeScript1UA -*-: ' + str + '\n');
}

const originalFactoryData = [];
const sessionId = 'test-session-id-' + uuidGenerator.generateUUID().toString();
const address = Cc["@mozilla.org/supports-cstring;1"]
                  .createInstance(Ci.nsISupportsCString);
address.data = "127.0.0.1";
const addresses = Cc["@mozilla.org/array;1"].createInstance(Ci.nsIMutableArray);
addresses.appendElement(address, false);

function mockChannelDescription(role) {
  this.QueryInterface = XPCOMUtils.generateQI([Ci.nsIPresentationChannelDescription]);
  this.role = role;
  this.type = Ci.nsIPresentationChannelDescription.TYPE_TCP;
  this.tcpAddress = addresses;
  this.tcpPort = (role === 'sender' ? 1234 : 4321); // either sender or receiver
}

const mockChannelDescriptionOfSender   = new mockChannelDescription('sender');
const mockChannelDescriptionOfReceiver = new mockChannelDescription('receiver');

const mockServerSocket = {
  QueryInterface: XPCOMUtils.generateQI([Ci.nsIServerSocket,
                                         Ci.nsIFactory]),
  createInstance: function(aOuter, aIID) {
    if (aOuter) {
      throw Components.results.NS_ERROR_NO_AGGREGATION;
    }
    return this.QueryInterface(aIID);
  },
  get port() {
    return this._port;
  },
  set listener(listener) {
    this._listener = listener;
  },
  init: function(port, loopbackOnly, backLog) {
    this._port = (port == -1 ? 5678 : port);
  },
  asyncListen: function(listener) {
    this._listener = listener;
  },
  close: function() {
    this._listener.onStopListening(this, Cr.NS_BINDING_ABORTED);
  },
  onSocketAccepted: function(serverSocket, socketTransport) {
    this._listener.onSocketAccepted(serverSocket, socketTransport);
  }
};

// mockSessionTransport
var mockSessionTransportOfSender   = undefined;
var mockSessionTransportOfReceiver = undefined;

function mockSessionTransport() {}

mockSessionTransport.prototype = {
  QueryInterface: XPCOMUtils.generateQI([Ci.nsIPresentationSessionTransport,
                                         Ci.nsIPresentationTCPSessionTransportBuilder]),
  set callback(callback) {
    this._callback = callback;
  },
  get callback() {
    return this._callback;
  },
  get selfAddress() {
    return this._selfAddress;
  },
  buildTCPSenderTransport: function(transport, listener) {
    mockSessionTransportOfSender = this;
    this._listener = listener;
    this._role = Ci.nsIPresentationService.ROLE_CONTROLLER;

    this._listener.onSessionTransport(this);
    this._listener = null;
    this.simulateTransportReady();
  },
  buildTCPReceiverTransport: function(description, listener) {
    mockSessionTransportOfReceiver = this;
    this._listener = listener;
    this._role = Ci.nsIPresentationService.ROLE_RECEIVER;

    var addresses = description.QueryInterface(Ci.nsIPresentationChannelDescription)
                               .tcpAddress;
    this._selfAddress = {
      QueryInterface: XPCOMUtils.generateQI([Ci.nsINetAddr]),
      address: (addresses.length > 0) ?
                addresses.queryElementAt(0, Ci.nsISupportsCString).data : '',
      port: description.QueryInterface(Ci.nsIPresentationChannelDescription)
                       .tcpPort,
    };

    this._listener.onSessionTransport(this);
    this._listener = null;
  },
  enableDataNotification: function() {
  },
  send: function(data) {
    debug('Send message: ' + data);
    if (this._role === Ci.nsIPresentationService.ROLE_CONTROLLER) {
      mockSessionTransportOfReceiver._callback.notifyData(data);
    }
    if (this._role === Ci.nsIPresentationService.ROLE_RECEIVER) {
      mockSessionTransportOfSender._callback.notifyData(data);
    }
  },
  close: function(reason) {
    sendAsyncMessage('data-transport-closed', reason);
    this._callback.QueryInterface(Ci.nsIPresentationSessionTransportCallback).notifyTransportClosed(reason);
    if (this._role === Ci.nsIPresentationService.ROLE_CONTROLLER) {
      if (mockSessionTransportOfReceiver._callback) {
        mockSessionTransportOfReceiver._callback.QueryInterface(Ci.nsIPresentationSessionTransportCallback).notifyTransportClosed(reason);
      }
    }
    else if (this._role === Ci.nsIPresentationService.ROLE_RECEIVER) {
      if (mockSessionTransportOfSender._callback) {
        mockSessionTransportOfSender._callback.QueryInterface(Ci.nsIPresentationSessionTransportCallback).notifyTransportClosed(reason);
      }
    }
  },
  simulateTransportReady: function() {
    this._callback.QueryInterface(Ci.nsIPresentationSessionTransportCallback).notifyTransportReady();
  },
};

const mockSessionTransportFactory = {
  QueryInterface: XPCOMUtils.generateQI([Ci.nsIFactory]),
  createInstance: function(aOuter, aIID) {
    if (aOuter) {
      throw Components.results.NS_ERROR_NO_AGGREGATION;
    }
    var result = new mockSessionTransport();
    return result.QueryInterface(aIID);
  }
}

const mockSocketTransport = {
  QueryInterface: XPCOMUtils.generateQI([Ci.nsISocketTransport]),
};

// control channel of sender
const mockControlChannelOfSender = {
  QueryInterface: XPCOMUtils.generateQI([Ci.nsIPresentationControlChannel]),
  set listener(listener) {
    // PresentationControllingInfo::SetControlChannel
    if (listener) {
      debug('set listener for mockControlChannelOfSender without null');
    } else {
      debug('set listener for mockControlChannelOfSender with null');
    }
    this._listener = listener;
  },
  get listener() {
    return this._listener;
  },
  notifyConnected: function() {
    // send offer after notifyConnected immediately
    this._listener
        .QueryInterface(Ci.nsIPresentationControlChannelListener)
        .notifyConnected();
  },
  sendOffer: function(offer) {
    sendAsyncMessage('offer-sent');
  },
  onAnswer: function(answer) {
    this._listener
        .QueryInterface(Ci.nsIPresentationControlChannelListener)
        .onAnswer(answer);
  },
  launch: function(presentationId, url) {
    sendAsyncMessage('sender-launch', url);
  },
  disconnect: function(reason) {
    this._listener
        .QueryInterface(Ci.nsIPresentationControlChannelListener)
        .notifyDisconnected(reason);
    mockControlChannelOfReceiver.disconnect();
  }
};

// control channel of receiver
const mockControlChannelOfReceiver = {
  QueryInterface: XPCOMUtils.generateQI([Ci.nsIPresentationControlChannel]),
  set listener(listener) {
    // PresentationPresentingInfo::SetControlChannel
    if (listener) {
      debug('set listener for mockControlChannelOfReceiver without null');
    } else {
      debug('set listener for mockControlChannelOfReceiver with null');
    }
    this._listener = listener;

    if (this._pendingOpened) {
      this._pendingOpened = false;
      this.notifyConnected();
    }
  },
  get listener() {
    return this._listener;
  },
  notifyConnected: function() {
    // do nothing
    if (!this._listener) {
      this._pendingOpened = true;
      return;
    }
    this._listener
        .QueryInterface(Ci.nsIPresentationControlChannelListener)
        .notifyConnected();
  },
  onOffer: function(offer) {
    this._listener
        .QueryInterface(Ci.nsIPresentationControlChannelListener)
        .onOffer(offer);
  },
  sendAnswer: function(answer) {
    this._listener
        .QueryInterface(Ci.nsIPresentationSessionTransportCallback)
        .notifyTransportReady();
    sendAsyncMessage('answer-sent');
  },
  disconnect: function(reason) {
    this._listener
        .QueryInterface(Ci.nsIPresentationControlChannelListener)
        .notifyDisconnected(reason);
    sendAsyncMessage('control-channel-receiver-closed', reason);
  }
};

const mockDevice = {
  QueryInterface: XPCOMUtils.generateQI([Ci.nsIPresentationDevice]),
  id:   'id',
  name: 'name',
  type: 'type',
  establishControlChannel: function(url, presentationId) {
    sendAsyncMessage('control-channel-established');
    return mockControlChannelOfSender;
  },
};

const mockDevicePrompt = {
  QueryInterface: XPCOMUtils.generateQI([Ci.nsIPresentationDevicePrompt,
                                         Ci.nsIFactory]),
  createInstance: function(aOuter, aIID) {
    if (aOuter) {
      throw Components.results.NS_ERROR_NO_AGGREGATION;
    }
    return this.QueryInterface(aIID);
  },
  set request(request) {
    this._request = request;
  },
  get request() {
    return this._request;
  },
  promptDeviceSelection: function(request) {
    this._request = request;
    sendAsyncMessage('device-prompt');
  },
  simulateSelect: function() {
    this._request.select(mockDevice);
  },
  simulateCancel: function() {
    this._request.cancel();
  }
};

const mockRequestUIGlue = {
  QueryInterface: XPCOMUtils.generateQI([Ci.nsIPresentationRequestUIGlue,
                                         Ci.nsIFactory]),
  set promise(aPromise) {
    this._promise = aPromise
  },
  get promise() {
    return this._promise;
  },
  createInstance: function(aOuter, aIID) {
    if (aOuter) {
      throw Components.results.NS_ERROR_NO_AGGREGATION;
    }
    return this.QueryInterface(aIID);
  },
  sendRequest: function(aUrl, aSessionId) {
    return this.promise;
  },
};

function initMockAndListener() {

  function registerMockFactory(contractId, mockClassId, mockFactory) {
    var originalClassId, originalFactory;

    var registrar = Cm.QueryInterface(Ci.nsIComponentRegistrar);
    if (!registrar.isCIDRegistered(mockClassId)) {
      try {
        originalClassId = registrar.contractIDToCID(contractId);
        originalFactory = Cm.getClassObject(Cc[contractId], Ci.nsIFactory);
      } catch (ex) {
        originalClassId = "";
        originalFactory = null;
      }
      if (originalFactory) {
        registrar.unregisterFactory(originalClassId, originalFactory);
      }
      registrar.registerFactory(mockClassId, "", contractId, mockFactory);
    }

    return { contractId: contractId,
             mockClassId: mockClassId,
             mockFactory: mockFactory,
             originalClassId: originalClassId,
             originalFactory: originalFactory };
  }
  // Register mock factories.
  const uuidGenerator = Cc["@mozilla.org/uuid-generator;1"]
                        .getService(Ci.nsIUUIDGenerator);
  originalFactoryData.push(registerMockFactory("@mozilla.org/presentation-device/prompt;1",
                                               uuidGenerator.generateUUID(),
                                               mockDevicePrompt));
  originalFactoryData.push(registerMockFactory("@mozilla.org/network/server-socket;1",
                                               uuidGenerator.generateUUID(),
                                               mockServerSocket));
  originalFactoryData.push(registerMockFactory("@mozilla.org/presentation/presentationtcpsessiontransport;1",
                                               uuidGenerator.generateUUID(),
                                               mockSessionTransportFactory));
  originalFactoryData.push(registerMockFactory("@mozilla.org/presentation/requestuiglue;1",
                                               uuidGenerator.generateUUID(),
                                               mockRequestUIGlue));

  addMessageListener('trigger-device-add', function() {
    debug('Got message: trigger-device-add');
    var deviceManager = Cc['@mozilla.org/presentation-device/manager;1']
                        .getService(Ci.nsIPresentationDeviceManager);
    deviceManager.QueryInterface(Ci.nsIPresentationDeviceListener)
                 .addDevice(mockDevice);
  });

  addMessageListener('trigger-device-prompt-select', function() {
    debug('Got message: trigger-device-prompt-select');
    mockDevicePrompt.simulateSelect();
  });

  addMessageListener('trigger-on-session-request', function(url) {
    debug('Got message: trigger-on-session-request');
    var deviceManager = Cc['@mozilla.org/presentation-device/manager;1']
                          .getService(Ci.nsIPresentationDeviceManager);
    deviceManager.QueryInterface(Ci.nsIPresentationDeviceListener)
                 .onSessionRequest(mockDevice,
                                   url,
                                   sessionId,
                                   mockControlChannelOfReceiver);
  });

  addMessageListener('trigger-control-channel-open', function(reason) {
    debug('Got message: trigger-control-channel-open');
    mockControlChannelOfSender.notifyConnected();
    mockControlChannelOfReceiver.notifyConnected();
  });

  addMessageListener('trigger-on-offer', function() {
    debug('Got message: trigger-on-offer');
    mockControlChannelOfReceiver.onOffer(mockChannelDescriptionOfSender);
    mockServerSocket.onSocketAccepted(mockServerSocket, mockSocketTransport);
  });

  addMessageListener('trigger-on-answer', function() {
    debug('Got message: trigger-on-answer');
    mockControlChannelOfSender.onAnswer(mockChannelDescriptionOfReceiver);
  });

  // Used to call sendAsyncMessage in chrome script from receiver.
  addMessageListener('forward-command', function(command_data) {
    let command = JSON.parse(command_data);
    sendAsyncMessage(command.name, command.data);
  });

  addMessageListener('teardown', teardown);

  var obs = Cc["@mozilla.org/observer-service;1"].getService(Ci.nsIObserverService);
  obs.addObserver(function setupRequestPromiseHandler(aSubject, aTopic, aData) {
    debug('Got observer: setup-request-promise');
    obs.removeObserver(setupRequestPromiseHandler, aTopic);
    mockRequestUIGlue.promise = aSubject;
    sendAsyncMessage('promise-setup-ready');
  }, 'setup-request-promise', false);
}

function teardown() {

  function registerOriginalFactory(contractId, mockedClassId, mockedFactory, originalClassId, originalFactory) {
    if (originalFactory) {
      registrar.unregisterFactory(mockedClassId, mockedFactory);
      registrar.registerFactory(originalClassId, "", contractId, originalFactory);
    }
  }

  mockRequestUIGlue.promise               = null;
  mockServerSocket.listener               = null;
  mockSessionTransportOfSender.callback   = null;
  mockSessionTransportOfReceiver.callback = null;
  mockControlChannelOfSender.listener     = null;
  mockControlChannelOfReceiver.listener   = null;
  mockDevicePrompt.request                = null;

  var deviceManager = Cc['@mozilla.org/presentation-device/manager;1']
                      .getService(Ci.nsIPresentationDeviceManager);
  deviceManager.QueryInterface(Ci.nsIPresentationDeviceListener)
               .removeDevice(mockDevice);
  // Register original factories.
  for (var data in originalFactoryData) {
    registerOriginalFactory(data.contractId, data.mockClassId,
                            data.mockFactory, data.originalClassId,
                            data.originalFactory);
  }
  sendAsyncMessage('teardown-complete');
}

initMockAndListener();

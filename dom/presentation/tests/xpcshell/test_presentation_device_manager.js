/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

'use strict';

const { classes: Cc, interfaces: Ci, utils: Cu } = Components;

Cu.import('resource://gre/modules/XPCOMUtils.jsm');
Cu.import('resource://gre/modules/Services.jsm');

const manager = Cc['@mozilla.org/presentation-device/manager;1']
                  .getService(Ci.nsIPresentationDeviceManager);

function TestPresentationDevice() {}


function TestPresentationControlChannel() {}

TestPresentationControlChannel.prototype = {
  QueryInterface: XPCOMUtils.generateQI([Ci.nsIPresentationControlChannel]),
  sendOffer: function(offer) {},
  sendAnswer: function(answer) {},
  disconnect: function() {},
  launch: function() {},
  set listener(listener) {},
  get listener() {},
};

var testProvider = {
  QueryInterface: XPCOMUtils.generateQI([Ci.nsIPresentationDeviceProvider]),

  forceDiscovery: function() {
  },
  set listener(listener) {
  },
  get listener() {
  },
};

var testDevice = {
  QueryInterface: XPCOMUtils.generateQI([Ci.nsIPresentationDevice]),
  id: 'id',
  name: 'name',
  type: 'type',
  establishControlChannel: function(url, presentationId) {
    return null;
  },
};

function addProvider() {
  Object.defineProperty(testProvider, 'listener', {
    configurable: true,
    set: function(listener) {
      Assert.strictEqual(listener, manager, 'listener setter is invoked by PresentationDeviceManager');
      delete testProvider.listener;
      run_next_test();
    },
  });
  manager.addDeviceProvider(testProvider);
}

function forceDiscovery() {
  testProvider.forceDiscovery = function() {
    testProvider.forceDiscovery = function() {};
    Assert.ok(true, 'forceDiscovery is invoked by PresentationDeviceManager');
    run_next_test();
  };
  manager.forceDiscovery();
}

function addDevice() {
  Services.obs.addObserver(function observer(subject, topic, data) {
    Services.obs.removeObserver(observer, topic);

    let updatedDevice = subject.QueryInterface(Ci.nsIPresentationDevice);
    Assert.equal(updatedDevice.id, testDevice.id, 'expected device id');
    Assert.equal(updatedDevice.name, testDevice.name, 'expected device name');
    Assert.equal(updatedDevice.type, testDevice.type, 'expected device type');
    Assert.equal(data, 'add', 'expected update type');

    Assert.ok(manager.deviceAvailable, 'device is available');

    let devices = manager.getAvailableDevices();
    Assert.equal(devices.length, 1, 'expect 1 available device');

    let device = devices.queryElementAt(0, Ci.nsIPresentationDevice);
    Assert.equal(device.id, testDevice.id, 'expected device id');
    Assert.equal(device.name, testDevice.name, 'expected device name');
    Assert.equal(device.type, testDevice.type, 'expected device type');

    run_next_test();
  }, 'presentation-device-change', false);
  manager.QueryInterface(Ci.nsIPresentationDeviceListener).addDevice(testDevice);
}

function updateDevice() {
  Services.obs.addObserver(function observer(subject, topic, data) {
    Services.obs.removeObserver(observer, topic);

    let updatedDevice = subject.QueryInterface(Ci.nsIPresentationDevice);
    Assert.equal(updatedDevice.id, testDevice.id, 'expected device id');
    Assert.equal(updatedDevice.name, testDevice.name, 'expected device name');
    Assert.equal(updatedDevice.type, testDevice.type, 'expected device type');
    Assert.equal(data, 'update', 'expected update type');

    Assert.ok(manager.deviceAvailable, 'device is available');

    let devices = manager.getAvailableDevices();
    Assert.equal(devices.length, 1, 'expect 1 available device');

    let device = devices.queryElementAt(0, Ci.nsIPresentationDevice);
    Assert.equal(device.id, testDevice.id, 'expected device id');
    Assert.equal(device.name, testDevice.name, 'expected name after device update');
    Assert.equal(device.type, testDevice.type, 'expected device type');

    run_next_test();
  }, 'presentation-device-change', false);
  testDevice.name = 'updated-name';
  manager.QueryInterface(Ci.nsIPresentationDeviceListener).updateDevice(testDevice);
}

function sessionRequest() {
  let testUrl = 'http://www.example.org/';
  let testPresentationId = 'test-presentation-id';
  let testControlChannel = new TestPresentationControlChannel();
  Services.obs.addObserver(function observer(subject, topic, data) {
    Services.obs.removeObserver(observer, topic);

    let request = subject.QueryInterface(Ci.nsIPresentationSessionRequest);

    Assert.equal(request.device.id, testDevice.id, 'expected device');
    Assert.equal(request.url, testUrl, 'expected requesting URL');
    Assert.equal(request.presentationId, testPresentationId, 'expected presentation Id');

    run_next_test();
  }, 'presentation-session-request', false);
  manager.QueryInterface(Ci.nsIPresentationDeviceListener)
         .onSessionRequest(testDevice, testUrl, testPresentationId, testControlChannel);
}

function removeDevice() {
  Services.obs.addObserver(function observer(subject, topic, data) {
    Services.obs.removeObserver(observer, topic);

    let updatedDevice = subject.QueryInterface(Ci.nsIPresentationDevice);
    Assert.equal(updatedDevice.id, testDevice.id, 'expected device id');
    Assert.equal(updatedDevice.name, testDevice.name, 'expected device name');
    Assert.equal(updatedDevice.type, testDevice.type, 'expected device type');
    Assert.equal(data, 'remove', 'expected update type');

    Assert.ok(!manager.deviceAvailable, 'device is not available');

    let devices = manager.getAvailableDevices();
    Assert.equal(devices.length, 0, 'expect 0 available device');

    run_next_test();
  }, 'presentation-device-change', false);
  manager.QueryInterface(Ci.nsIPresentationDeviceListener).removeDevice(testDevice);
}

function removeProvider() {
  Object.defineProperty(testProvider, 'listener', {
    configurable: true,
    set: function(listener) {
      Assert.strictEqual(listener, null, 'unsetListener is invoked by PresentationDeviceManager');
      delete testProvider.listener;
      run_next_test();
    },
  });
  manager.removeDeviceProvider(testProvider);
}

add_test(addProvider);
add_test(forceDiscovery);
add_test(addDevice);
add_test(updateDevice);
add_test(sessionRequest);
add_test(removeDevice);
add_test(removeProvider);

function run_test() {
  run_next_test();
}

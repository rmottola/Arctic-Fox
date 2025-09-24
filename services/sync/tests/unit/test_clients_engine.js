/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

Cu.import("resource://services-sync/constants.js");
Cu.import("resource://services-sync/engines.js");
Cu.import("resource://services-sync/engines/clients.js");
Cu.import("resource://services-sync/record.js");
Cu.import("resource://services-sync/service.js");
Cu.import("resource://services-sync/util.js");
Cu.import("resource://testing-common/services/sync/utils.js");

const MORE_THAN_CLIENTS_TTL_REFRESH = 691200; // 8 days
const LESS_THAN_CLIENTS_TTL_REFRESH = 86400;  // 1 day

var engine = Service.clientsEngine;

/**
 * Unpack the record with this ID, and verify that it has the same version that
 * we should be putting into records.
 */
function check_record_version(user, id) {
    let payload = JSON.parse(user.collection("clients").wbo(id).payload);

    let rec = new CryptoWrapper();
    rec.id = id;
    rec.collection = "clients";
    rec.ciphertext = payload.ciphertext;
    rec.hmac = payload.hmac;
    rec.IV = payload.IV;

    let cleartext = rec.decrypt(Service.collectionKeys.keyForCollection("clients"));

    _("Payload is " + JSON.stringify(cleartext));
    do_check_eq(Services.appinfo.version, cleartext.version);
    do_check_eq(2, cleartext.protocols.length);
    do_check_eq("1.1", cleartext.protocols[0]);
    do_check_eq("1.5", cleartext.protocols[1]);
}

add_test(function test_bad_hmac() {
  _("Ensure that Clients engine deletes corrupt records.");
  let contents = {
    meta: {global: {engines: {clients: {version: engine.version,
                                        syncID: engine.syncID}}}},
    clients: {},
    crypto: {}
  };
  let deletedCollections = [];
  let deletedItems       = [];
  let callback = {
    __proto__: SyncServerCallback,
    onItemDeleted: function (username, coll, wboID) {
      deletedItems.push(coll + "/" + wboID);
    },
    onCollectionDeleted: function (username, coll) {
      deletedCollections.push(coll);
    }
  }
  let server = serverForUsers({"foo": "password"}, contents, callback);
  let user   = server.user("foo");

  function check_clients_count(expectedCount) {
    let stack = Components.stack.caller;
    let coll  = user.collection("clients");

    // Treat a non-existent collection as empty.
    do_check_eq(expectedCount, coll ? coll.count() : 0, stack);
  }

  function check_client_deleted(id) {
    let coll = user.collection("clients");
    let wbo  = coll.wbo(id);
    return !wbo || !wbo.payload;
  }

  function uploadNewKeys() {
    generateNewKeys(Service.collectionKeys);
    let serverKeys = Service.collectionKeys.asWBO("crypto", "keys");
    serverKeys.encrypt(Service.identity.syncKeyBundle);
    do_check_true(serverKeys.upload(Service.resource(Service.cryptoKeysURL)).success);
  }

  try {
    ensureLegacyIdentityManager();
    let passphrase     = "abcdeabcdeabcdeabcdeabcdea";
    Service.serverURL  = server.baseURI;
    Service.login("foo", "ilovejane", passphrase);

    generateNewKeys(Service.collectionKeys);

    _("First sync, client record is uploaded");
    do_check_eq(engine.lastRecordUpload, 0);
    check_clients_count(0);
    engine._sync();
    check_clients_count(1);
    do_check_true(engine.lastRecordUpload > 0);

    // Our uploaded record has a version.
    check_record_version(user, engine.localID);

    // Initial setup can wipe the server, so clean up.
    deletedCollections = [];
    deletedItems       = [];

    _("Change our keys and our client ID, reupload keys.");
    let oldLocalID  = engine.localID;     // Preserve to test for deletion!
    engine.localID = Utils.makeGUID();
    engine.resetClient();
    generateNewKeys(Service.collectionKeys);
    let serverKeys = Service.collectionKeys.asWBO("crypto", "keys");
    serverKeys.encrypt(Service.identity.syncKeyBundle);
    do_check_true(serverKeys.upload(Service.resource(Service.cryptoKeysURL)).success);

    _("Sync.");
    engine._sync();

    _("Old record " + oldLocalID + " was deleted, new one uploaded.");
    check_clients_count(1);
    check_client_deleted(oldLocalID);

    _("Now change our keys but don't upload them. " +
      "That means we get an HMAC error but redownload keys.");
    Service.lastHMACEvent = 0;
    engine.localID = Utils.makeGUID();
    engine.resetClient();
    generateNewKeys(Service.collectionKeys);
    deletedCollections = [];
    deletedItems       = [];
    check_clients_count(1);
    engine._sync();

    _("Old record was not deleted, new one uploaded.");
    do_check_eq(deletedCollections.length, 0);
    do_check_eq(deletedItems.length, 0);
    check_clients_count(2);

    _("Now try the scenario where our keys are wrong *and* there's a bad record.");
    // Clean up and start fresh.
    user.collection("clients")._wbos = {};
    Service.lastHMACEvent = 0;
    engine.localID = Utils.makeGUID();
    engine.resetClient();
    deletedCollections = [];
    deletedItems       = [];
    check_clients_count(0);

    uploadNewKeys();

    // Sync once to upload a record.
    engine._sync();
    check_clients_count(1);

    // Generate and upload new keys, so the old client record is wrong.
    uploadNewKeys();

    // Create a new client record and new keys. Now our keys are wrong, as well
    // as the object on the server. We'll download the new keys and also delete
    // the bad client record.
    oldLocalID  = engine.localID;         // Preserve to test for deletion!
    engine.localID = Utils.makeGUID();
    engine.resetClient();
    generateNewKeys(Service.collectionKeys);
    let oldKey = Service.collectionKeys.keyForCollection();

    do_check_eq(deletedCollections.length, 0);
    do_check_eq(deletedItems.length, 0);
    engine._sync();
    do_check_eq(deletedItems.length, 1);
    check_client_deleted(oldLocalID);
    check_clients_count(1);
    let newKey = Service.collectionKeys.keyForCollection();
    do_check_false(oldKey.equals(newKey));

  } finally {
    Svc.Prefs.resetBranch("");
    Service.recordManager.clearCache();
    server.stop(run_next_test);
  }
});

add_test(function test_properties() {
  _("Test lastRecordUpload property");
  try {
    do_check_eq(Svc.Prefs.get("clients.lastRecordUpload"), undefined);
    do_check_eq(engine.lastRecordUpload, 0);

    let now = Date.now();
    engine.lastRecordUpload = now / 1000;
    do_check_eq(engine.lastRecordUpload, Math.floor(now / 1000));
  } finally {
    Svc.Prefs.resetBranch("");
    run_next_test();
  }
});

add_test(function test_full_sync() {
  _("Ensure that Clients engine fetches all records for each sync.");

  let now = Date.now() / 1000;
  let contents = {
    meta: {global: {engines: {clients: {version: engine.version,
                                        syncID: engine.syncID}}}},
    clients: {},
    crypto: {}
  };
  let server = serverForUsers({"foo": "password"}, contents);
  let user   = server.user("foo");

  new SyncTestingInfrastructure(server.server);
  generateNewKeys(Service.collectionKeys);

  let activeID = Utils.makeGUID();
  server.insertWBO("foo", "clients", new ServerWBO(activeID, encryptPayload({
    id: activeID,
    name: "Active client",
    type: "desktop",
    commands: [],
    version: "48",
    protocols: ["1.5"],
  }), now - 10));

  let deletedID = Utils.makeGUID();
  server.insertWBO("foo", "clients", new ServerWBO(deletedID, encryptPayload({
    id: deletedID,
    name: "Client to delete",
    type: "desktop",
    commands: [],
    version: "48",
    protocols: ["1.5"],
  }), now - 10));

  try {
    let store = engine._store;

    _("First sync. 2 records downloaded; our record uploaded.");
    strictEqual(engine.lastRecordUpload, 0);
    engine._sync();
    ok(engine.lastRecordUpload > 0);
    deepEqual(user.collection("clients").keys().sort(),
              [activeID, deletedID, engine.localID].sort(),
              "Our record should be uploaded on first sync");
    deepEqual(Object.keys(store.getAllIDs()).sort(),
              [activeID, deletedID, engine.localID].sort(),
              "Other clients should be downloaded on first sync");

    _("Delete a record, then sync again");
    let collection = server.getCollection("foo", "clients");
    collection.remove(deletedID);
    // Simulate a timestamp update in info/collections.
    engine.lastModified = now;
    engine._sync();

    _("Record should be updated");
    deepEqual(Object.keys(store.getAllIDs()).sort(),
              [activeID, engine.localID].sort(),
              "Deleted client should be removed on next sync");
  } finally {
    Svc.Prefs.resetBranch("");
    Service.recordManager.clearCache();

    try {
      server.deleteCollections("foo");
    } finally {
      server.stop(run_next_test);
    }
  }
});

add_test(function test_sync() {
  _("Ensure that Clients engine uploads a new client record once a week.");

  let contents = {
    meta: {global: {engines: {clients: {version: engine.version,
                                        syncID: engine.syncID}}}},
    clients: {},
    crypto: {}
  };
  let server = serverForUsers({"foo": "password"}, contents);
  let user   = server.user("foo");

  new SyncTestingInfrastructure(server.server);
  generateNewKeys(Service.collectionKeys);

  function clientWBO() {
    return user.collection("clients").wbo(engine.localID);
  }

  try {

    _("First sync. Client record is uploaded.");
    do_check_eq(clientWBO(), undefined);
    do_check_eq(engine.lastRecordUpload, 0);
    engine._sync();
    do_check_true(!!clientWBO().payload);
    do_check_true(engine.lastRecordUpload > 0);

    _("Let's time travel more than a week back, new record should've been uploaded.");
    engine.lastRecordUpload -= MORE_THAN_CLIENTS_TTL_REFRESH;
    let lastweek = engine.lastRecordUpload;
    clientWBO().payload = undefined;
    engine._sync();
    do_check_true(!!clientWBO().payload);
    do_check_true(engine.lastRecordUpload > lastweek);

    _("Remove client record.");
    engine.removeClientData();
    do_check_eq(clientWBO().payload, undefined);

    _("Time travel one day back, no record uploaded.");
    engine.lastRecordUpload -= LESS_THAN_CLIENTS_TTL_REFRESH;
    let yesterday = engine.lastRecordUpload;
    engine._sync();
    do_check_eq(clientWBO().payload, undefined);
    do_check_eq(engine.lastRecordUpload, yesterday);

  } finally {
    Svc.Prefs.resetBranch("");
    Service.recordManager.clearCache();
    server.stop(run_next_test);
  }
});

add_test(function test_client_name_change() {
  _("Ensure client name change incurs a client record update.");

  let tracker = engine._tracker;

  let localID = engine.localID;
  let initialName = engine.localName;

  Svc.Obs.notify("weave:engine:start-tracking");
  _("initial name: " + initialName);

  // Tracker already has data, so clear it.
  tracker.clearChangedIDs();

  let initialScore = tracker.score;

  do_check_eq(Object.keys(tracker.changedIDs).length, 0);

  Svc.Prefs.set("client.name", "new name");

  _("new name: " + engine.localName);
  do_check_neq(initialName, engine.localName);
  do_check_eq(Object.keys(tracker.changedIDs).length, 1);
  do_check_true(engine.localID in tracker.changedIDs);
  do_check_true(tracker.score > initialScore);
  do_check_true(tracker.score >= SCORE_INCREMENT_XLARGE);

  Svc.Obs.notify("weave:engine:stop-tracking");

  run_next_test();
});

add_test(function test_send_command() {
  _("Verifies _sendCommandToClient puts commands in the outbound queue.");

  let store = engine._store;
  let tracker = engine._tracker;
  let remoteId = Utils.makeGUID();
  let rec = new ClientsRec("clients", remoteId);

  store.create(rec);
  let remoteRecord = store.createRecord(remoteId, "clients");

  let action = "testCommand";
  let args = ["foo", "bar"];

  engine._sendCommandToClient(action, args, remoteId);

  let newRecord = store._remoteClients[remoteId];
  do_check_neq(newRecord, undefined);
  do_check_eq(newRecord.commands.length, 1);

  let command = newRecord.commands[0];
  do_check_eq(command.command, action);
  do_check_eq(command.args.length, 2);
  do_check_eq(command.args, args);

  do_check_neq(tracker.changedIDs[remoteId], undefined);

  run_next_test();
});

add_test(function test_command_validation() {
  _("Verifies that command validation works properly.");

  let store = engine._store;

  let testCommands = [
    ["resetAll",    [],       true ],
    ["resetAll",    ["foo"],  false],
    ["resetEngine", ["tabs"], true ],
    ["resetEngine", [],       false],
    ["wipeAll",     [],       true ],
    ["wipeAll",     ["foo"],  false],
    ["wipeEngine",  ["tabs"], true ],
    ["wipeEngine",  [],       false],
    ["logout",      [],       true ],
    ["logout",      ["foo"],  false],
    ["__UNKNOWN__", [],       false]
  ];

  for (let [action, args, expectedResult] of testCommands) {
    let remoteId = Utils.makeGUID();
    let rec = new ClientsRec("clients", remoteId);

    store.create(rec);
    store.createRecord(remoteId, "clients");

    engine.sendCommand(action, args, remoteId);

    let newRecord = store._remoteClients[remoteId];
    do_check_neq(newRecord, undefined);

    if (expectedResult) {
      _("Ensuring command is sent: " + action);
      do_check_eq(newRecord.commands.length, 1);

      let command = newRecord.commands[0];
      do_check_eq(command.command, action);
      do_check_eq(command.args, args);

      do_check_neq(engine._tracker, undefined);
      do_check_neq(engine._tracker.changedIDs[remoteId], undefined);
    } else {
      _("Ensuring command is scrubbed: " + action);
      do_check_eq(newRecord.commands, undefined);

      if (store._tracker) {
        do_check_eq(engine._tracker[remoteId], undefined);
      }
    }

  }
  run_next_test();
});

add_test(function test_command_duplication() {
  _("Ensures duplicate commands are detected and not added");

  let store = engine._store;
  let remoteId = Utils.makeGUID();
  let rec = new ClientsRec("clients", remoteId);
  store.create(rec);
  store.createRecord(remoteId, "clients");

  let action = "resetAll";
  let args = [];

  engine.sendCommand(action, args, remoteId);
  engine.sendCommand(action, args, remoteId);

  let newRecord = store._remoteClients[remoteId];
  do_check_eq(newRecord.commands.length, 1);

  _("Check variant args length");
  newRecord.commands = [];

  action = "resetEngine";
  engine.sendCommand(action, [{ x: "foo" }], remoteId);
  engine.sendCommand(action, [{ x: "bar" }], remoteId);

  _("Make sure we spot a real dupe argument.");
  engine.sendCommand(action, [{ x: "bar" }], remoteId);

  do_check_eq(newRecord.commands.length, 2);

  run_next_test();
});

add_test(function test_command_invalid_client() {
  _("Ensures invalid client IDs are caught");

  let id = Utils.makeGUID();
  let error;

  try {
    engine.sendCommand("wipeAll", [], id);
  } catch (ex) {
    error = ex;
  }

  do_check_eq(error.message.indexOf("Unknown remote client ID: "), 0);

  run_next_test();
});

add_test(function test_process_incoming_commands() {
  _("Ensures local commands are executed");

  engine.localCommands = [{ command: "logout", args: [] }];

  let ev = "weave:service:logout:finish";

  var handler = function() {
    Svc.Obs.remove(ev, handler);
    run_next_test();
  };

  Svc.Obs.add(ev, handler);

  // logout command causes processIncomingCommands to return explicit false.
  do_check_false(engine.processIncomingCommands());
});

add_test(function test_command_sync() {
  _("Ensure that commands are synced across clients.");

  engine._store.wipe();
  generateNewKeys(Service.collectionKeys);

  let contents = {
    meta: {global: {engines: {clients: {version: engine.version,
                                        syncID: engine.syncID}}}},
    clients: {},
    crypto: {}
  };
  let server   = serverForUsers({"foo": "password"}, contents);
  new SyncTestingInfrastructure(server.server);

  let user     = server.user("foo");
  let remoteId = Utils.makeGUID();

  function clientWBO(id) {
    return user.collection("clients").wbo(id);
  }

  _("Create remote client record");
  server.insertWBO("foo", "clients", new ServerWBO(remoteId, encryptPayload({
    id: remoteId,
    name: "Remote client",
    type: "desktop",
    commands: [],
    version: "48",
    protocols: ["1.5"],
  }), Date.now() / 1000));

  try {
    _("Syncing.");
    engine._sync();

    _("Checking remote record was downloaded.");
    let clientRecord = engine._store._remoteClients[remoteId];
    do_check_neq(clientRecord, undefined);
    do_check_eq(clientRecord.commands.length, 0);

    _("Send a command to the remote client.");
    engine.sendCommand("wipeAll", []);
    do_check_eq(clientRecord.commands.length, 1);
    engine._sync();

    _("Checking record was uploaded.");
    do_check_neq(clientWBO(engine.localID).payload, undefined);
    do_check_true(engine.lastRecordUpload > 0);

    do_check_neq(clientWBO(remoteId).payload, undefined);

    Svc.Prefs.set("client.GUID", remoteId);
    engine._resetClient();
    do_check_eq(engine.localID, remoteId);
    _("Performing sync on resetted client.");
    engine._sync();
    do_check_neq(engine.localCommands, undefined);
    do_check_eq(engine.localCommands.length, 1);

    let command = engine.localCommands[0];
    do_check_eq(command.command, "wipeAll");
    do_check_eq(command.args.length, 0);

  } finally {
    Svc.Prefs.resetBranch("");
    Service.recordManager.clearCache();

    try {
      let collection = server.getCollection("foo", "clients");
      collection.remove(remoteId);
    } finally {
      server.stop(run_next_test);
    }
  }
});

add_test(function test_send_uri_to_client_for_display() {
  _("Ensure sendURIToClientForDisplay() sends command properly.");

  let tracker = engine._tracker;
  let store = engine._store;

  let remoteId = Utils.makeGUID();
  let rec = new ClientsRec("clients", remoteId);
  rec.name = "remote";
  store.create(rec);
  let remoteRecord = store.createRecord(remoteId, "clients");

  tracker.clearChangedIDs();
  let initialScore = tracker.score;

  let uri = "http://www.mozilla.org/";
  let title = "Title of the Page";
  engine.sendURIToClientForDisplay(uri, remoteId, title);

  let newRecord = store._remoteClients[remoteId];

  do_check_neq(newRecord, undefined);
  do_check_eq(newRecord.commands.length, 1);

  let command = newRecord.commands[0];
  do_check_eq(command.command, "displayURI");
  do_check_eq(command.args.length, 3);
  do_check_eq(command.args[0], uri);
  do_check_eq(command.args[1], engine.localID);
  do_check_eq(command.args[2], title);

  do_check_true(tracker.score > initialScore);
  do_check_true(tracker.score - initialScore >= SCORE_INCREMENT_XLARGE);

  _("Ensure unknown client IDs result in exception.");
  let unknownId = Utils.makeGUID();
  let error;

  try {
    engine.sendURIToClientForDisplay(uri, unknownId);
  } catch (ex) {
    error = ex;
  }

  do_check_eq(error.message.indexOf("Unknown remote client ID: "), 0);

  run_next_test();
});

add_test(function test_receive_display_uri() {
  _("Ensure processing of received 'displayURI' commands works.");

  // We don't set up WBOs and perform syncing because other tests verify
  // the command API works as advertised. This saves us a little work.

  let uri = "http://www.mozilla.org/";
  let remoteId = Utils.makeGUID();
  let title = "Page Title!";

  let command = {
    command: "displayURI",
    args: [uri, remoteId, title],
  };

  engine.localCommands = [command];

  // Received 'displayURI' command should result in the topic defined below
  // being called.
  let ev = "weave:engine:clients:display-uri";

  let handler = function(subject, data) {
    Svc.Obs.remove(ev, handler);

    do_check_eq(subject.uri, uri);
    do_check_eq(subject.client, remoteId);
    do_check_eq(subject.title, title);
    do_check_eq(data, null);

    run_next_test();
  };

  Svc.Obs.add(ev, handler);

  do_check_true(engine.processIncomingCommands());

  engine._resetClient();
  run_next_test();
});

add_test(function test_optional_client_fields() {
  _("Ensure that we produce records with the fields added in Bug 1097222.");

  const SUPPORTED_PROTOCOL_VERSIONS = ["1.1", "1.5"];
  let local = engine._store.createRecord(engine.localID, "clients");
  do_check_eq(local.name, engine.localName);
  do_check_eq(local.type, engine.localType);
  do_check_eq(local.version, Services.appinfo.version);
  do_check_array_eq(local.protocols, SUPPORTED_PROTOCOL_VERSIONS);

  // Optional fields.
  // Make sure they're what they ought to be...
  do_check_eq(local.os, Services.appinfo.OS);
  do_check_eq(local.appPackage, Services.appinfo.ID);

  // ... and also that they're non-empty.
  do_check_true(!!local.os);
  do_check_true(!!local.appPackage);
  do_check_true(!!local.application);

  // We don't currently populate device or formfactor.
  // See Bug 1100722, Bug 1100723.

  run_next_test();
});

add_test(function test_merge_commands() {
  _("Verifies local commands for remote clients are merged with the server's");

  let now = Date.now() / 1000;
  let contents = {
    meta: {global: {engines: {clients: {version: engine.version,
                                        syncID: engine.syncID}}}},
    clients: {},
    crypto: {}
  };
  let server = serverForUsers({"foo": "password"}, contents);
  let user   = server.user("foo");

  new SyncTestingInfrastructure(server.server);
  generateNewKeys(Service.collectionKeys);

  let desktopID = Utils.makeGUID();
  server.insertWBO("foo", "clients", new ServerWBO(desktopID, encryptPayload({
    id: desktopID,
    name: "Desktop client",
    type: "desktop",
    commands: [{
      command: "displayURI",
      args: ["https://example.com", engine.localID, "Yak Herders Anonymous"],
    }],
    version: "48",
    protocols: ["1.5"],
  }), now - 10));

  let mobileID = Utils.makeGUID();
  server.insertWBO("foo", "clients", new ServerWBO(mobileID, encryptPayload({
    id: mobileID,
    name: "Mobile client",
    type: "mobile",
    commands: [{
      command: "logout",
      args: [],
    }],
    version: "48",
    protocols: ["1.5"],
  }), now - 10));

  try {
    let store = engine._store;

    _("First sync. 2 records downloaded.");
    strictEqual(engine.lastRecordUpload, 0);
    engine._sync();

    _("Broadcast logout to all clients");
    engine.sendCommand("logout", []);
    engine._sync();

    let collection = server.getCollection("foo", "clients");
    let desktopPayload = JSON.parse(JSON.parse(collection.payload(desktopID)).ciphertext);
    deepEqual(desktopPayload.commands, [{
      command: "displayURI",
      args: ["https://example.com", engine.localID, "Yak Herders Anonymous"],
    }, {
      command: "logout",
      args: [],
    }], "Should send the logout command to the desktop client");

    let mobilePayload = JSON.parse(JSON.parse(collection.payload(mobileID)).ciphertext);
    deepEqual(mobilePayload.commands, [{ command: "logout", args: [] }],
      "Should not send a duplicate logout to the mobile client");
  } finally {
    Svc.Prefs.resetBranch("");
    Service.recordManager.clearCache();
    engine._resetClient();

    try {
      server.deleteCollections("foo");
    } finally {
      server.stop(run_next_test);
    }
  }
});

add_test(function test_deleted_commands() {
  _("Verifies commands for a deleted client are discarded");

  let now = Date.now() / 1000;
  let contents = {
    meta: {global: {engines: {clients: {version: engine.version,
                                        syncID: engine.syncID}}}},
    clients: {},
    crypto: {}
  };
  let server = serverForUsers({"foo": "password"}, contents);
  let user   = server.user("foo");

  new SyncTestingInfrastructure(server.server);
  generateNewKeys(Service.collectionKeys);

  let activeID = Utils.makeGUID();
  server.insertWBO("foo", "clients", new ServerWBO(activeID, encryptPayload({
    id: activeID,
    name: "Active client",
    type: "desktop",
    commands: [],
    version: "48",
    protocols: ["1.5"],
  }), now - 10));

  let deletedID = Utils.makeGUID();
  server.insertWBO("foo", "clients", new ServerWBO(deletedID, encryptPayload({
    id: deletedID,
    name: "Client to delete",
    type: "desktop",
    commands: [],
    version: "48",
    protocols: ["1.5"],
  }), now - 10));

  try {
    let store = engine._store;

    _("First sync. 2 records downloaded.");
    engine._sync();

    _("Delete a record on the server.");
    let collection = server.getCollection("foo", "clients");
    collection.remove(deletedID);

    _("Broadcast a command to all clients");
    engine.sendCommand("logout", []);
    engine._sync();

    deepEqual(collection.keys().sort(), [activeID, engine.localID].sort(),
      "Should not reupload deleted clients");

    let activePayload = JSON.parse(JSON.parse(collection.payload(activeID)).ciphertext);
    deepEqual(activePayload.commands, [{ command: "logout", args: [] }],
      "Should send the command to the active client");
  } finally {
    Svc.Prefs.resetBranch("");
    Service.recordManager.clearCache();
    engine._resetClient();

    try {
      server.deleteCollections("foo");
    } finally {
      server.stop(run_next_test);
    }
  }
});

function run_test() {
  initTestLogging("Trace");
  Log.repository.getLogger("Sync.Engine.Clients").level = Log.Level.Trace;
  run_next_test();
}

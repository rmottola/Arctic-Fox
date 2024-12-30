"use strict";

Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/FileUtils.jsm");

/* global OS */
Cu.import("resource://gre/modules/osfile.jsm");

/* global HostManifestManager */
Cu.import("resource://gre/modules/NativeMessaging.jsm");

Components.utils.import("resource://gre/modules/Schemas.jsm");
const BASE_SCHEMA = "chrome://extensions/content/schemas/manifest.json";

let dir = FileUtils.getDir("TmpD", ["NativeMessaging"]);
dir.createUnique(Ci.nsIFile.DIRECTORY_TYPE, FileUtils.PERMS_DIRECTORY);

let userDir = dir.clone();
userDir.append("user");
userDir.create(Ci.nsIFile.DIRECTORY_TYPE, FileUtils.PERMS_DIRECTORY);

let globalDir = dir.clone();
globalDir.append("global");
globalDir.create(Ci.nsIFile.DIRECTORY_TYPE, FileUtils.PERMS_DIRECTORY);

let dirProvider = {
  getFile(property) {
    if (property == "XREUserNativeMessaging") {
      return userDir.clone();
    } else if (property == "XRESysNativeMessaging") {
      return globalDir.clone();
    }
    return null;
  },
};

Services.dirsvc.registerProvider(dirProvider);

do_register_cleanup(() => {
  Services.dirsvc.unregisterProvider(dirProvider);
  dir.remove(true);
});

function writeManifest(path, manifest) {
  if (typeof manifest != "string") {
    manifest = JSON.stringify(manifest);
  }
  return OS.File.writeAtomic(path, manifest);
}

add_task(function* setup() {
  yield Schemas.load(BASE_SCHEMA);
});

// Test of HostManifestManager.lookupApplication() begin here...

let context = {
  url: null,
  logError() {},
  preprocessors: {},
};

let templateManifest = {
  name: "test",
  description: "this is only a test",
  path: "/bin/cat",
  type: "stdio",
  allowed_extensions: ["extension@tests.mozilla.org"],
};

add_task(function* test_nonexistent_manifest() {
  let result = yield HostManifestManager.lookupApplication("test", context);
  equal(result, null, "lookupApplication returns null for non-existent application");
});

const USER_TEST_JSON = OS.Path.join(userDir.path, "test.json");

add_task(function* test_good_manifest() {
  yield writeManifest(USER_TEST_JSON, templateManifest);
  let result = yield HostManifestManager.lookupApplication("test", context);
  notEqual(result, null, "lookupApplication finds a good manifest");
  equal(result.path, USER_TEST_JSON, "lookupApplication returns the correct path");
  deepEqual(result.manifest, templateManifest, "lookupApplication returns the manifest contents");
});

add_task(function* test_invalid_json() {
  yield writeManifest(USER_TEST_JSON, "this is not valid json");
  let result = yield HostManifestManager.lookupApplication("test", context);
  equal(result, null, "lookupApplication ignores bad json");
});

add_task(function* test_invalid_name() {
  let manifest = Object.assign({}, templateManifest);
  manifest.name = "../test";
  yield writeManifest(USER_TEST_JSON, manifest);
  let result = yield HostManifestManager.lookupApplication("test", context);
  equal(result, null, "lookupApplication ignores an invalid name");
});

add_task(function* test_name_mismatch() {
  let manifest = Object.assign({}, templateManifest);
  manifest.name = "not test";
  yield writeManifest(USER_TEST_JSON, manifest);
  let result = yield HostManifestManager.lookupApplication("test", context);
  equal(result, null, "lookupApplication ignores mistmatch between json filename and name property");
});

add_task(function* test_missing_props() {
  const PROPS = [
    "name",
    "description",
    "path",
    "type",
    "allowed_extensions",
  ];
  for (let prop of PROPS) {
    let manifest = Object.assign({}, templateManifest);
    delete manifest[prop];

    yield writeManifest(USER_TEST_JSON, manifest);
    let result = yield HostManifestManager.lookupApplication("test", context);
    equal(result, null, `lookupApplication ignores missing ${prop}`);
  }
});

add_task(function* test_invalid_type() {
  let manifest = Object.assign({}, templateManifest);
  manifest.type = "bogus";
  yield writeManifest(USER_TEST_JSON, manifest);
  let result = yield HostManifestManager.lookupApplication("test", context);
  equal(result, null, "lookupApplication ignores invalid type");
});

add_task(function* test_no_allowed_extensions() {
  let manifest = Object.assign({}, templateManifest);
  manifest.allowed_extensions = [];
  yield writeManifest(USER_TEST_JSON, manifest);
  let result = yield HostManifestManager.lookupApplication("test", context);
  equal(result, null, "lookupApplication ignores manifest with no allowed_extensions");
});

const GLOBAL_TEST_JSON = OS.Path.join(globalDir.path, "test.json");
let globalManifest = Object.assign({}, templateManifest);
globalManifest.description = "This manifest is from the systemwide directory";

add_task(function* good_manifest_system_dir() {
  yield OS.File.remove(USER_TEST_JSON);
  yield writeManifest(GLOBAL_TEST_JSON, globalManifest);

  let result = yield HostManifestManager.lookupApplication("test", context);
  notEqual(result, null, "lookupApplication finds a manifest in the system-wide directory");
  equal(result.path, GLOBAL_TEST_JSON, "lookupApplication returns path in the system-wide directory");
  deepEqual(result.manifest, globalManifest, "lookupApplication returns manifest contents from the system-wide directory");
});

add_task(function* test_user_dir_precedence() {
  yield writeManifest(USER_TEST_JSON, templateManifest);
  // test.json is still in the global directory from the previous test

  let result = yield HostManifestManager.lookupApplication("test", context);
  notEqual(result, null, "lookupApplication finds a manifest when entries exist in both user-specific and system-wide directories");
  equal(result.path, USER_TEST_JSON, "lookupApplication returns the user-specific path when user-specific and system-wide entries both exist");
  deepEqual(result.manifest, templateManifest, "lookupApplication returns user-specific manifest contents with user-specific and system-wide entries both exist");
});


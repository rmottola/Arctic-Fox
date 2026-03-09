
const {ADDON_SIGNING} = AM_Cu.import("resource://gre/modules/addons/AddonConstants.jsm", {});

function run_test() {
  run_next_test();
}

let profileDir;
add_task(function* setup() {
  profileDir = gProfD.clone();
  profileDir.append("extensions");

  if (!profileDir.exists())
    profileDir.create(AM_Ci.nsIFile.DIRECTORY_TYPE, FileUtils.PERMS_DIRECTORY);

  createAppInfo("xpcshell@tests.mozilla.org", "XPCShell", "1", "1.9.2");
  startupManager();
});

const IMPLICIT_ID_XPI = "data/webext-implicit-id.xpi";
const IMPLICIT_ID_ID = "webext_implicit_id@tests.mozilla.org";

// webext-implicit-id.xpi has a minimal manifest with no
// applications or browser_specific_settings, so its id comes
// from its signature, which should be the ID constant defined below.
add_task(function* test_implicit_id() {
  // This test needs to read the xpi certificate which only works
  // if signing is enabled.
  ok(ADDON_SIGNING, "Add-on signing is enabled");

  let addon = yield promiseAddonByID(IMPLICIT_ID_ID);
  equal(addon, null, "Add-on is not installed");

  let xpifile = do_get_file(IMPLICIT_ID_XPI);
  yield promiseInstallAllFiles([xpifile]);

  addon = yield promiseAddonByID(IMPLICIT_ID_ID);
  notEqual(addon, null, "Add-on is installed");

  addon.uninstall();
});

// We should also be able to install webext-implicit-id.xpi temporarily
// and it should look just like the regular install (ie, the ID should
// come from the signature)
add_task(function* test_implicit_id_temp() {
  // This test needs to read the xpi certificate which only works
  // if signing is enabled.
  ok(ADDON_SIGNING, "Addon signing is enabled");

  let addon = yield promiseAddonByID(IMPLICIT_ID_ID);
  do_check_eq(addon, null);

  let xpifile = do_get_file(IMPLICIT_ID_XPI);
  yield AddonManager.installTemporaryAddon(xpifile);

  addon = yield promiseAddonByID(IMPLICIT_ID_ID);
  do_check_neq(addon, null);

  // The sourceURI of a temporary installed addon should be equal to the
  // file url of the installed xpi file.
  do_check_eq(addon.sourceURI && addon.sourceURI.spec,
              Services.io.newFileURI(xpifile).spec);

  addon.uninstall();
});

// We should be able to temporarily install an unsigned web extension
// that does not have an ID in its manifest.
add_task(function* test_unsigned_no_id_temp_install() {
  AddonTestUtils.useRealCertChecks = true;
  const manifest = {
    name: "no ID",
    description: "extension without an ID",
    manifest_version: 2,
    version: "1.0"
  };

  const addonDir = yield promiseWriteWebManifestForExtension(manifest, gTmpD,
                                                "the-addon-sub-dir");
  const addon = yield AddonManager.installTemporaryAddon(addonDir);
  ok(addon.id, "ID should have been auto-generated");

  // The sourceURI of a temporary installed addon should be equal to the
  // file url of the installed source dir.
  do_check_eq(addon.sourceURI && addon.sourceURI.spec,
              Services.io.newFileURI(addonDir).spec);

  // Install the same directory again, as if re-installing or reloading.
  const secondAddon = yield AddonManager.installTemporaryAddon(addonDir);
  // The IDs should be the same.
  equal(secondAddon.id, addon.id);

  secondAddon.uninstall();
  Services.obs.notifyObservers(addonDir, "flush-cache-entry", null);
  addonDir.remove(true);
  AddonTestUtils.useRealCertChecks = false;
});

// We should be able to install two extensions from manifests without IDs
// at different locations and get two unique extensions.
add_task(function* test_multiple_no_id_extensions() {
  AddonTestUtils.useRealCertChecks = true;
  const manifest = {
    name: "no ID",
    description: "extension without an ID",
    manifest_version: 2,
    version: "1.0"
  };

  const firstAddonDir = yield promiseWriteWebManifestForExtension(manifest, gTmpD,
                                                     "addon-sub-dir-one");
  const secondAddonDir = yield promiseWriteWebManifestForExtension(manifest, gTmpD,
                                                      "addon-sub-dir-two");
  const [firstAddon, secondAddon] = yield Promise.all([
    AddonManager.installTemporaryAddon(firstAddonDir),
    AddonManager.installTemporaryAddon(secondAddonDir)
  ]);

  const allAddons = yield new Promise(resolve => {
    AddonManager.getAllAddons(addons => resolve(addons));
  });
  do_print(`Found these add-ons: ${allAddons.map(a => a.name).join(", ")}`);
  const filtered = allAddons.filter(addon => addon.name === manifest.name);
  // Make sure we have two add-ons by the same name.
  equal(filtered.length, 2, "Two add-ons are installed with the same name");

  firstAddon.uninstall();
  Services.obs.notifyObservers(firstAddonDir, "flush-cache-entry", null);
  firstAddonDir.remove(true);
  secondAddon.uninstall();
  Services.obs.notifyObservers(secondAddonDir, "flush-cache-entry", null);
  secondAddonDir.remove(true);
  AddonTestUtils.useRealCertChecks = false;
});

// Test that we can get the ID from browser_specific_settings
add_task(function* test_bss_id() {
  const ID = "webext_bss_id@tests.mozilla.org";

  let manifest = {
    name: "bss test",
    description: "test that ID may be in browser_specific_settings",
    manifest_version: 2,
    version: "1.0",

    browser_specific_settings: {
      gecko: {
        id: ID
      }
    }
  };

  let addon = yield promiseAddonByID(ID);
  equal(addon, null, "Add-on is not installed");

  yield promiseWriteWebManifestForExtension(manifest, profileDir, ID);
  yield promiseRestartManager();

  addon = yield promiseAddonByID(ID);
  notEqual(addon, null, "Add-on is installed");

  addon.uninstall();
});

// Test that if we have IDs in both browser_specific_settings and applications,
// that we prefer the ID in browser_specific_settings.
add_task(function* test_two_ids() {
  const GOOD_ID = "two_ids@tests.mozilla.org";
  const BAD_ID = "i_am_obsolete@tests.mozilla.org";

  let manifest = {
    name: "two id test",
    description: "test a web extension with ids in both applications and browser_specific_settings",
    manifest_version: 2,
    version: "1.0",

    applications: {
      gecko: {
        id: BAD_ID
      }
    },

    browser_specific_settings: {
      gecko: {
        id: GOOD_ID
      }
    }
  }

  yield promiseWriteWebManifestForExtension(manifest, profileDir, GOOD_ID);
  yield promiseRestartManager();

  let addon = yield promiseAddonByID(BAD_ID);
  equal(addon, null, "Add-on is not found using bad ID");
  addon = yield promiseAddonByID(GOOD_ID);
  notEqual(addon, null, "Add-on is found using good ID");

  addon.uninstall();
});

// Test that strict_min_version and strict_max_version are enforced for
// loading temporary extension.
add_task(function* test_strict_min_max() {
  // the app version being compared to is 1.9.2
  const addonId = "strict_min_max@tests.mozilla.org";
  const MANIFEST = {
    name: "strict min max test",
    description: "test strict min and max with temporary loading",
    manifest_version: 2,
    version: "1.0",
  };

  function flushAndRemove(file) {
    // flush JAR cache and remove the file
    Services.obs.notifyObservers(file, "flush-cache-entry", null);
    file.remove(true);
  }

  // bad max good min
  let apps = {
    applications: {
      gecko: {
        id: addonId,
        strict_min_version: "1",
        strict_max_version: "1"
      },
    },
  }
  let testManifest = Object.assign(apps, MANIFEST);

  let addonDir = yield promiseWriteWebManifestForExtension(testManifest, gTmpD,
                                              "the-addon-sub-dir");

  let expectedMsg = new RegExp("Add-on strict_min_max@tests.mozilla.org is not compatible with application version. " +
                               "add-on minVersion: 1. add-on maxVersion: 1.");
  yield Assert.rejects(AddonManager.installTemporaryAddon(addonDir),
                       expectedMsg,
                       "Install rejects when specified maxVersion is not valid");

  let addon = yield promiseAddonByID(addonId);
  equal(addon, null, "Add-on is not installed");
  flushAndRemove(addonDir);

  // bad min good max
  apps = {
    applications: {
      gecko: {
        id: addonId,
        strict_min_version: "2",
        strict_max_version: "2"
      },
    },
  }
  testManifest = Object.assign(apps, MANIFEST);

  addonDir = yield promiseWriteWebManifestForExtension(testManifest, gTmpD,
                                          "the-addon-sub-dir");

  expectedMsg = new RegExp("Add-on strict_min_max@tests.mozilla.org is not compatible with application version. " +
                               "add-on minVersion: 2. add-on maxVersion: 2.");
  yield Assert.rejects(AddonManager.installTemporaryAddon(addonDir),
                       expectedMsg,
                       "Install rejects when specified minVersion is not valid");

  addon = yield promiseAddonByID(addonId);
  equal(addon, null, "Add-on is not installed");
  flushAndRemove(addonDir);

  // bad both
  apps = {
    applications: {
      gecko: {
        id: addonId,
        strict_min_version: "2",
        strict_max_version: "1"
      },
    },
  }
  testManifest = Object.assign(apps, MANIFEST);

  addonDir = yield promiseWriteWebManifestForExtension(testManifest, gTmpD,
                                          "the-addon-sub-dir");

  expectedMsg = new RegExp("Add-on strict_min_max@tests.mozilla.org is not compatible with application version. " +
                               "add-on minVersion: 2. add-on maxVersion: 1.");
  yield Assert.rejects(AddonManager.installTemporaryAddon(addonDir),
                       expectedMsg,
                       "Install rejects when specified minVersion and maxVersion are not valid");

  addon = yield promiseAddonByID(addonId);
  equal(addon, null, "Add-on is not installed");
  flushAndRemove(addonDir);

  // bad only min
  apps = {
    applications: {
      gecko: {
        id: addonId,
        strict_min_version: "2"
      },
    },
  }
  testManifest = Object.assign(apps, MANIFEST);

  addonDir = yield promiseWriteWebManifestForExtension(testManifest, gTmpD,
                                          "the-addon-sub-dir");

  expectedMsg = new RegExp("Add-on strict_min_max@tests.mozilla.org is not compatible with application version\. " +
                           "add-on minVersion: 2\.");
  yield Assert.rejects(AddonManager.installTemporaryAddon(addonDir),
                       expectedMsg,
                       "Install rejects when specified minVersion and maxVersion are not valid");

  addon = yield promiseAddonByID(addonId);
  equal(addon, null, "Add-on is not installed");
  flushAndRemove(addonDir);

  // bad only max
  apps = {
    applications: {
      gecko: {
        id: addonId,
        strict_max_version: "1"
      },
    },
  }
  testManifest = Object.assign(apps, MANIFEST);

  addonDir = yield promiseWriteWebManifestForExtension(testManifest, gTmpD,
                                          "the-addon-sub-dir");

  expectedMsg = new RegExp("Add-on strict_min_max@tests.mozilla.org is not compatible with application version\. " +
                           "add-on maxVersion: 1\.");
  yield Assert.rejects(AddonManager.installTemporaryAddon(addonDir),
                       expectedMsg,
                       "Install rejects when specified minVersion and maxVersion are not valid");

  addon = yield promiseAddonByID(addonId);
  equal(addon, null, "Add-on is not installed");
  flushAndRemove(addonDir);

  // good both
  apps = {
    applications: {
      gecko: {
        id: addonId,
        strict_min_version: "1",
        strict_max_version: "2"
      },
    },
  }
  testManifest = Object.assign(apps, MANIFEST);

  addonDir = yield promiseWriteWebManifestForExtension(testManifest, gTmpD,
                                          "strict_min_max");

  yield AddonManager.installTemporaryAddon(addonDir);
  addon = yield promiseAddonByID(addonId);

  notEqual(addon, null, "Add-on is installed");
  equal(addon.id, addonId, "Installed add-on has the expected ID");
  addon.uninstall();
  flushAndRemove(addonDir);

  // good only min
  let newId = "strict_min_only@tests.mozilla.org";
  apps = {
    applications: {
      gecko: {
        id: newId,
        strict_min_version: "1",
      },
    },
  }
  testManifest = Object.assign(apps, MANIFEST);

  addonDir = yield promiseWriteWebManifestForExtension(testManifest, gTmpD,
                                          "strict_min_only");

  yield AddonManager.installTemporaryAddon(addonDir);
  addon = yield promiseAddonByID(newId);

  notEqual(addon, null, "Add-on is installed");
  equal(addon.id, newId, "Installed add-on has the expected ID");

  addon.uninstall();
  flushAndRemove(addonDir);

  // good only max
  newId = "strict_max_only@tests.mozilla.org";
  apps = {
    applications: {
      gecko: {
        id: newId,
        strict_max_version: "2",
      },
    },
  }
  testManifest = Object.assign(apps, MANIFEST);

  addonDir = yield promiseWriteWebManifestForExtension(testManifest, gTmpD,
                                          "strict_max_only");

  yield AddonManager.installTemporaryAddon(addonDir);
  addon = yield promiseAddonByID(newId);

  notEqual(addon, null, "Add-on is installed");
  equal(addon.id, newId, "Installed add-on has the expected ID");

  addon.uninstall();
  flushAndRemove(addonDir);

  // * in min will generate a warning
  for (let version of ["0.*", "0.*.0"]) {
    newId = "strict_min_star@tests.mozilla.org";
    let apps = {
      applications: {
        gecko: {
          id: newId,
          strict_min_version: version,
        },
      },
    }
    let testManifest = Object.assign(apps, MANIFEST);

    let addonDir = yield promiseWriteWebManifestForExtension(testManifest, gTmpD,
                                            "strict_min_star");

    let { messages } = yield promiseConsoleOutput(function* () {
      yield AddonManager.installTemporaryAddon(addonDir);
    });
    ok(messages.some(msg => msg.message.includes("The use of '*' in strict_min_version is deprecated")),
       "Deprecation warning for strict_min_version with '*' was generated");

    let addon = yield promiseAddonByID(newId);

    notEqual(addon, null, "Add-on is installed");
    equal(addon.id, newId, "Add-on has the expected id");

    addon.uninstall();
    flushAndRemove(addonDir);
  }

  // incompatible extension but with compatibility checking off
  newId = "checkCompatibility@tests.mozilla.org";
  apps = {
    applications: {
      gecko: {
        id: newId,
        strict_max_version: "1",
      },
    },
  }
  testManifest = Object.assign(apps, MANIFEST);

  addonDir = yield promiseWriteWebManifestForExtension(testManifest, gTmpD,
                                          "checkCompatibility");

  let savedCheckCompatibilityValue = AddonManager.checkCompatibility;
  AddonManager.checkCompatibility = false;
  yield AddonManager.installTemporaryAddon(addonDir);
  addon = yield promiseAddonByID(newId);

  notEqual(addon, null, "Add-on is installed");
  equal(addon.id, newId, "Installed add-on has the expected ID");

  addon.uninstall();
  flushAndRemove(addonDir);
  AddonManager.checkCompatibility = savedCheckCompatibilityValue;
});

// Check permissions prompt
add_task(function* test_permissions() {
  const manifest = {
    name: "permissions test",
    description: "permissions test",
    manifest_version: 2,
    version: "1.0",

    permissions: ["tabs", "storage", "https://*.example.com/*", "<all_urls>", "experiments.test"],
  };

  let xpi = ExtensionTestCommon.generateXPI({manifest});

  let install = yield new Promise(resolve => {
    AddonManager.getInstallForFile(xpi, resolve);
  });

  let perminfo;
  install._permHandler = info => {
    perminfo = info;
    return Promise.resolve();
  };

  yield promiseCompleteInstall(install);

  notEqual(perminfo, undefined, "Permission handler was invoked");
  equal(perminfo.existingAddon, null, "Permission info does not include an existing addon");
  notEqual(perminfo.addon, null, "Permission info includes the new addon");
  let perms = perminfo.addon.userPermissions;
  deepEqual(perms.permissions, ["tabs", "storage"], "API permissions are correct");
  deepEqual(perms.hosts, ["https://*.example.com/*", "<all_urls>"], "Host permissions are correct");
  deepEqual(perms.apis, ["test"], "Experiments permissions are correct");

  let addon = yield promiseAddonByID(perminfo.addon.id);
  notEqual(addon, null, "Extension was installed");

  addon.uninstall();
  yield OS.File.remove(xpi.path);
});

// Check permissions prompt cancellation
add_task(function* test_permissions() {
  const manifest = {
    name: "permissions test",
    description: "permissions test",
    manifest_version: 2,
    version: "1.0",

    permissions: ["webRequestBlocking"],
  };

  let xpi = ExtensionTestCommon.generateXPI({manifest});

  let install = yield new Promise(resolve => {
    AddonManager.getInstallForFile(xpi, resolve);
  });

  let perminfo;
  install._permHandler = info => {
    perminfo = info;
    return Promise.reject();
  };

  yield promiseCompleteInstall(install);

  notEqual(perminfo, undefined, "Permission handler was invoked");

  let addon = yield promiseAddonByID(perminfo.addon.id);
  equal(addon, null, "Extension was not installed");

  yield OS.File.remove(xpi.path);
});

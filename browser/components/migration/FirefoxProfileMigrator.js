/* -*- Mode: js; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: sw=2 ts=2 sts=2 et */
 /* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/*
 * Migrates from a Pale Moon profile in a lossy manner in order to clean up a
 * user's profile.  Data is only migrated where the benefits outweigh the
 * potential problems caused by importing undesired/invalid configurations
 * from the source profile.
 */

Components.utils.import("resource://gre/modules/XPCOMUtils.jsm");
Components.utils.import("resource:///modules/MigrationUtils.jsm");
Components.utils.import("resource://gre/modules/Services.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "PlacesBackups",
                                  "resource://gre/modules/PlacesBackups.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "SessionMigration",
                                  "resource:///modules/sessionstore/SessionMigration.jsm");

function FirefoxProfileMigrator() { }

FirefoxProfileMigrator.prototype = Object.create(MigratorPrototype);

FirefoxProfileMigrator.prototype._getAllProfiles = function () {
  let allProfiles = new Map();
  let profiles =
    Components.classes["@mozilla.org/toolkit/profile-service;1"]
              .getService(Components.interfaces.nsIToolkitProfileService)
              .profiles;
  while (profiles.hasMoreElements()) {
    let profile = profiles.getNext().QueryInterface(Components.interfaces.nsIToolkitProfile);
    let rootDir = profile.rootDir;

    if (rootDir.exists() && rootDir.isReadable() &&
        !rootDir.equals(MigrationUtils.profileStartup.directory)) {
      allProfiles.set(profile.name, rootDir);
    }
  }
  return allProfiles;
};

function sorter(a, b) {
  return a.id.toLocaleLowerCase().localeCompare(b.id.toLocaleLowerCase());
}

Object.defineProperty(FirefoxProfileMigrator.prototype, "sourceProfiles", {
  get: function() {
    return [{id: x, name: x} for (x of this._getAllProfiles().keys())].sort(sorter);
  }
});

FirefoxProfileMigrator.prototype.getResources = function(aProfile) {
  let sourceProfileDir = aProfile ? this._getAllProfiles().get(aProfile.id) :
    Components.classes["@mozilla.org/toolkit/profile-service;1"]
              .getService(Components.interfaces.nsIToolkitProfileService)
              .selectedProfile.rootDir;
  if (!sourceProfileDir || !sourceProfileDir.exists() ||
      !sourceProfileDir.isReadable())
    return null;

  // Being a startup-only migrator, we can rely on
  // MigrationUtils.profileStartup being set.
  let currentProfileDir = MigrationUtils.profileStartup.directory;

  // Surely data cannot be imported from the current profile.
  if (sourceProfileDir.equals(currentProfileDir))
    return null;

  let getFileResource = function(aMigrationType, aFileNames) {
    let files = [];
    for (let fileName of aFileNames) {
      let file = this._getFileObject(sourceProfileDir, fileName);
      if (!file)
        return null;
      files.push(file);
    }
    return {
      type: aMigrationType,
      migrate: function(aCallback) {
        for (let file of files) {
          file.copyTo(currentProfileDir, "");
        }
        aCallback(true);
      }
    };
  }.bind(this);

  let types = MigrationUtils.resourceTypes;
  let places = getFileResource(types.HISTORY, ["places.sqlite", "places.sqlite-wal"]);
  let cookies = getFileResource(types.COOKIES, ["cookies.sqlite", "cookies.sqlite-wal"]);
  let passwords = getFileResource(types.PASSWORDS,
                                  ["signons.sqlite", "logins.json", "key3.db"]);
  let formData = getFileResource(types.FORMDATA, ["formhistory.sqlite"]);
  let bookmarksBackups = getFileResource(types.OTHERDATA,
    [PlacesBackups.profileRelativeFolderPath]);
  let dictionary = getFileResource(types.OTHERDATA, ["persdict.dat"]);

  let sessionCheckpoints = this._getFileObject(sourceProfileDir, "sessionCheckpoints.json");
  let sessionFile = this._getFileObject(sourceProfileDir, "sessionstore.js");
  let session;
  if (sessionFile) {
    session = {
      type: types.SESSION,
      migrate: function(aCallback) {
        sessionCheckpoints.copyTo(currentProfileDir, "sessionCheckpoints.json");
        let newSessionFile = currentProfileDir.clone();
        newSessionFile.append("sessionstore.js");
        let migrationPromise = SessionMigration.migrate(sessionFile.path, newSessionFile.path);
        migrationPromise.then(function() {
          let buildID = Services.appinfo.platformBuildID;
          let mstone = Services.appinfo.platformVersion;
          // Force the browser to one-off resume the session that we give it:
          Services.prefs.setBoolPref("browser.sessionstore.resume_session_once", true);
          // Reset the homepage_override prefs so that the browser doesn't override our
          // session with the "what's new" page:
          Services.prefs.setCharPref("browser.startup.homepage_override.mstone", mstone);
          Services.prefs.setCharPref("browser.startup.homepage_override.buildID", buildID);
          // It's too early in startup for the pref service to have a profile directory,
          // so we have to manually tell it where to save the prefs file.
          let newPrefsFile = currentProfileDir.clone();
          newPrefsFile.append("prefs.js");
          Services.prefs.savePrefFile(newPrefsFile);
          aCallback(true);
        }, function() {
          aCallback(false);
        });
      }
    }
  }

  return [r for each (r in [places, cookies, passwords, formData,
                            dictionary, bookmarksBackups, session]) if (r)];
}

Object.defineProperty(FirefoxProfileMigrator.prototype, "startupOnlyMigrator", {
  get: function() true
});


FirefoxProfileMigrator.prototype.classDescription = "Firefox Profile Migrator";
FirefoxProfileMigrator.prototype.contractID = "@mozilla.org/profile/migrator;1?app=browser&type=firefox";
FirefoxProfileMigrator.prototype.classID = Components.ID("{91185366-ba97-4438-acba-48deaca63386}");

this.NSGetFactory = XPCOMUtils.generateNSGetFactory([FirefoxProfileMigrator]);

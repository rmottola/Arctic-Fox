/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

// The purpose of this test is to create a mostly bogus site security service
// state file and see that the site security service handles it properly.

function writeLine(aLine, aOutputStream) {
  aOutputStream.write(aLine, aLine.length);
}

var gSSService = null;

function checkStateRead(aSubject, aTopic, aData) {
  equal(aData, SSS_STATE_FILE_NAME);

  ok(gSSService.isSecureHost(Ci.nsISiteSecurityService.HEADER_HSTS,
                             "example1.example.com", 0));
  ok(gSSService.isSecureHost(Ci.nsISiteSecurityService.HEADER_HSTS,
                             "example2.example.com", 0));
  ok(!gSSService.isSecureHost(Ci.nsISiteSecurityService.HEADER_HSTS,
                              "example.com", 0));
  ok(!gSSService.isSecureHost(Ci.nsISiteSecurityService.HEADER_HSTS,
                              "example3.example.com", 0));
  do_test_finished();
}

function run_test() {
  let profileDir = do_get_profile();
  let stateFile = profileDir.clone();
  stateFile.append(SSS_STATE_FILE_NAME);
  // Assuming we're working with a clean slate, the file shouldn't exist
  // until we create it.
  do_check_false(stateFile.exists());
  let outputStream = FileUtils.openFileOutputStream(stateFile);
  let now = (new Date()).getTime();
  writeLine("example1.example.com\t0\t0\t" + (now + 100000) + ",1,0\n", outputStream);
  writeLine("I'm a lumberjack and I'm okay; I work all night and I sleep all day!\n", outputStream);
  writeLine("This is a totally bogus entry\t\n", outputStream);
  writeLine("0\t0\t0\t0\t\n", outputStream);
  writeLine("\t\t\t\t\t\t\t\n", outputStream);
  writeLine("example.com\t\t\t\t\t\t\t\n", outputStream);
  writeLine("example3.example.com\t0\t\t\t\t\t\t\n", outputStream);
  writeLine("example2.example.com\t0\t0\t" + (now + 100000) + ",1,0\n", outputStream);
  outputStream.close();
  Services.obs.addObserver(checkStateRead, "data-storage-ready", false);
  do_test_pending();
  gSSService = Cc["@mozilla.org/ssservice;1"]
                 .getService(Ci.nsISiteSecurityService);
  notEqual(gSSService, null);
}
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// The purpose of this test is to create a mostly bogus site security service
// state file and see that the site security service handles it properly.

function writeLine(aLine, aOutputStream) {
  aOutputStream.write(aLine, aLine.length);
}

let gSSService = null;

function checkStateRead(aSubject, aTopic, aData) {
  do_check_eq(aData, SSS_STATE_FILE_NAME);

  do_check_true(gSSService.isSecureHost(Ci.nsISiteSecurityService.HEADER_HSTS,
                                        "example1.example.com", 0));
  do_check_true(gSSService.isSecureHost(Ci.nsISiteSecurityService.HEADER_HSTS,
                                        "example2.example.com", 0));
  do_check_false(gSSService.isSecureHost(Ci.nsISiteSecurityService.HEADER_HSTS,
                                         "example.com", 0));
  do_check_false(gSSService.isSecureHost(Ci.nsISiteSecurityService.HEADER_HSTS,
                                         "example3.example.com", 0));
  do_test_finished();
}

function run_test() {
  let profileDir = do_get_profile();
  let stateFile = profileDir.clone();
  stateFile.append(SSS_STATE_FILE_NAME);
  // Assuming we're working with a clean slate, the file shouldn't exist
  // until we create it.
  ok(!stateFile.exists());
  let outputStream = FileUtils.openFileOutputStream(stateFile);
  let now = (new Date()).getTime();
  writeLine("example1.example.com:HSTS\t0\t0\t" + (now + 100000) + ",1,0\n", outputStream);
  writeLine("I'm a lumberjack and I'm okay; I work all night and I sleep all day!\n", outputStream);
  writeLine("This is a totally bogus entry\t\n", outputStream);
  writeLine("0\t0\t0\t0\t\n", outputStream);
  writeLine("\t\t\t\t\t\t\t\n", outputStream);
  writeLine("example.com:HSTS\t\t\t\t\t\t\t\n", outputStream);
  writeLine("example3.example.com:HSTS\t0\t\t\t\t\t\t\n", outputStream);
  writeLine("example2.example.com:HSTS\t0\t0\t" + (now + 100000) + ",1,0\n", outputStream);
  outputStream.close();
  Services.obs.addObserver(checkStateRead, "data-storage-ready", false);
  do_test_pending();
  gSSService = Cc["@mozilla.org/ssservice;1"]
                 .getService(Ci.nsISiteSecurityService);
  do_check_true(gSSService != null);
}

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

function run_test() {
  setupTestCommon();

  // Verify write access to the custom app dir
  logTestInfo("testing write access to the application directory");
  let testFile = getCurrentProcessDir();
  testFile.append("update_write_access_test");
  testFile.create(Ci.nsIFile.NORMAL_FILE_TYPE, PERMS_FILE);
  Assert.ok(testFile.exists(), MSG_SHOULD_EXIST);
  testFile.remove(false);
  Assert.ok(!testFile.exists(), MSG_SHOULD_NOT_EXIST);

  standardInit();

  if (IS_WIN) {
    // Create a mutex to prevent being able to check for or apply updates.
    logTestInfo("attempting to create mutex");
    let handle = createMutex(getPerInstallationMutexName());

    debugDump("testing that the mutex was successfully created");
    do_check_true(!!handle);

    // Check if available updates cannot be checked for when there is a mutex
    // for this installation.
    logTestInfo("testing nsIApplicationUpdateService:canCheckForUpdates is " +
                "false when there is a mutex");
    do_check_false(gAUS.canCheckForUpdates);

    // Check if updates cannot be applied when there is a mutex for this
    // installation.
    logTestInfo("testing nsIApplicationUpdateService:canApplyUpdates is " +
                "false when there is a mutex");
    do_check_false(gAUS.canApplyUpdates);

    logTestInfo("destroying mutex");
    closeHandle(handle)
  }

  // Check if available updates can be checked for
  logTestInfo("testing nsIApplicationUpdateService:canCheckForUpdates is true");
  do_check_true(gAUS.canCheckForUpdates);
  // Check if updates can be applied
  logTestInfo("testing nsIApplicationUpdateService:canApplyUpdates is true");
  do_check_true(gAUS.canApplyUpdates);

  if (IS_WIN) {
    // Attempt to create a mutex when application update has already created one
    // with the same name.
    logTestInfo("attempting to create mutex");
    let handle = createMutex(getPerInstallationMutexName());

    logTestInfo("testing that the mutex was not successfully created");
    do_check_eq(handle, null);
  }

  doTestFinish();
}

/**
 * Determines a unique mutex name for the installation.
 *
 * @return Global mutex path.
 */
function getPerInstallationMutexName() {
  if (!IS_WIN) {
    do_throw("Windows only function called by a different platform!");
  }

  let hasher = Cc["@mozilla.org/security/hash;1"].
               createInstance(Ci.nsICryptoHash);
  hasher.init(hasher.SHA1);

  let exeFile = Services.dirsvc.get(XRE_EXECUTABLE_FILE, Ci.nsILocalFile);

  let converter = Cc["@mozilla.org/intl/scriptableunicodeconverter"].
                  createInstance(Ci.nsIScriptableUnicodeConverter);
  converter.charset = "UTF-8";
  let data = converter.convertToByteArray(exeFile.path.toLowerCase());

  hasher.update(data, data.length);
  return "Global\\MozillaUpdateMutex-" + hasher.finish(true);
}

/**
 * Closes a Win32 handle.
 *
 * @param  aHandle
 *         The handle to close.
 */
function closeHandle(aHandle) {
  if (!IS_WIN) {
    do_throw("Windows only function called by a different platform!");
  }

  let lib = ctypes.open("kernel32.dll");
  let CloseHandle = lib.declare("CloseHandle",
                                ctypes.winapi_abi,
                                ctypes.int32_t, /* success */
                                ctypes.void_t.ptr); /* handle */
  CloseHandle(aHandle);
  lib.close();
}

/**
 * Creates a mutex.
 *
 * @param  aName
 *         The name for the mutex.
 * @return The Win32 handle to the mutex.
 */
function createMutex(aName) {
  if (!IS_WIN) {
    do_throw("Windows only function called by a different platform!");
  }

  const INITIAL_OWN = 1;
  const ERROR_ALREADY_EXISTS = 0xB7;
  let lib = ctypes.open("kernel32.dll");
  let CreateMutexW = lib.declare("CreateMutexW",
                                 ctypes.winapi_abi,
                                 ctypes.void_t.ptr, /* return handle */
                                 ctypes.void_t.ptr, /* security attributes */
                                 ctypes.int32_t, /* initial owner */
                                 ctypes.char16_t.ptr); /* name */

  let handle = CreateMutexW(null, INITIAL_OWN, aName);
  lib.close();
  let alreadyExists = ctypes.winLastError == ERROR_ALREADY_EXISTS;
  if (handle && !handle.isNull() && alreadyExists) {
    closeHandle(handle);
    handle = null;
  }

  if (handle && handle.isNull()) {
    handle = null;
  }

  return handle;
}

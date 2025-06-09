/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Tests the dialog which allows the user to unblock a downloaded file.

registerCleanupFunction(() => {});

function* assertDialogResult({ args, buttonToClick, expectedResult }) {
  promiseAlertDialogOpen(buttonToClick);
  is(yield DownloadsCommon.confirmUnblockDownload(args), expectedResult);
}

add_task(function* test_confirm_unblock_dialog_unblock() {
  addDialogOpenObserver("accept");
  let result = yield DownloadsCommon.confirmUnblockDownload(Downloads.Error.BLOCK_VERDICT_MALWARE,
                                                            window);
  is(result, "unblock");
});

add_task(function* test_confirm_unblock_dialog_keep_safe() {
  addDialogOpenObserver("cancel");
  let result = yield DownloadsCommon.confirmUnblockDownload(Downloads.Error.BLOCK_VERDICT_MALWARE,
                                                            window);
  is(result, "cancel");
});

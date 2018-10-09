/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

function testSameVersion() {
  let mozSettings = window.navigator.mozSettings;
  let forceSent = false;

  mozSettings.addObserver("goanna.updateStatus", function statusObserver(setting) {
    if (!forceSent) {
      return;
    }

    mozSettings.removeObserver("goanna.updateStatus", statusObserver);
    is(setting.settingValue, "already-latest-version");
    cleanUp();
  });

  sendContentEvent("force-update-check");
  forceSent = true;
}

// Update lifecycle callbacks
function preUpdate() {
  testSameVersion();
}

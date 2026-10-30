"use strict";

Components.utils.import("resource://gre/modules/XPCOMUtils.jsm");

XPCOMUtils.defineLazyModuleGetter(this, "Promise",
  "resource://gre/modules/Promise.jsm");

/**
 * A wrapper for the findbar's method "close", which is not synchronous
 * because of animation.
 */
function closeFindbarAndWait(findbar) {
  return new Promise((resolve) => {
    if (findbar.hidden) {
      resolve();
      return;
    }
    findbar.addEventListener("transitionend", function cont(aEvent) {
      if (aEvent.propertyName != "visibility") {
        return;
      }
      findbar.removeEventListener("transitionend", cont);
      resolve();
    });
    findbar.close();
  });
}

function pushPrefs(...aPrefs) {
  let deferred = Promise.defer();
  SpecialPowers.pushPrefEnv({"set": aPrefs}, deferred.resolve);
  return deferred.promise;
}

function getTestPlugin(pluginName) {
  var ph = SpecialPowers.Cc["@mozilla.org/plugin/host;1"]
                                 .getService(SpecialPowers.Ci.nsIPluginHost);
  var tags = ph.getPluginTags();
  var name = pluginName || "Test Plug-in";
  for (var tag of tags) {
    if (tag.name == name) {
      return tag;
    }
  }

  ok(false, "Could not find plugin tag with plugin name '" + name + "'");
  return null;
}

function setTestPluginEnabledState(newEnabledState, pluginName) {
  var oldEnabledState = SpecialPowers.setTestPluginEnabledState(newEnabledState, pluginName);
  if (!oldEnabledState) {
    return;
  }
  var plugin = getTestPlugin(pluginName);
  while (plugin.enabledState != newEnabledState) {
    // Run a nested event loop to wait for the preference change to
    // propagate to the child. Yuck!
    SpecialPowers.Services.tm.currentThread.processNextEvent(true);
  }
  SimpleTest.registerCleanupFunction(function() {
    SpecialPowers.setTestPluginEnabledState(oldEnabledState, pluginName);
  });
}

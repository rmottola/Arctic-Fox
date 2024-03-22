/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* global React */

"use strict";

const { AddonManager } = require("resource://gre/modules/AddonManager.jsm");
const Services = require("Services");

const React = require("devtools/client/shared/vendor/react");
const { AddonsControls } = require("./addons-controls");
const { TabHeader } = require("./tab-header");
const { TargetList } = require("./target-list");

const ExtensionIcon = "chrome://mozapps/skin/extensions/extensionGeneric.svg";
const Strings = Services.strings.createBundle(
  "chrome://devtools/locale/aboutdebugging.properties");

const CHROME_ENABLED_PREF = "devtools.chrome.enabled";
const REMOTE_ENABLED_PREF = "devtools.debugger.remote-enabled";

exports.AddonsTab = React.createClass({
  displayName: "AddonsTab",

  getInitialState() {
    return {
      extensions: [],
      debugDisabled: false,
    };
  },

  componentDidMount() {
    AddonManager.addAddonListener(this);

    Services.prefs.addObserver(CHROME_ENABLED_PREF,
      this.updateDebugStatus, false);
    Services.prefs.addObserver(REMOTE_ENABLED_PREF,
      this.updateDebugStatus, false);

    this.updateDebugStatus();
    this.updateAddonsList();
  },

  componentWillUnmount() {
    AddonManager.removeAddonListener(this);
    Services.prefs.removeObserver(CHROME_ENABLED_PREF,
      this.updateDebugStatus);
    Services.prefs.removeObserver(REMOTE_ENABLED_PREF,
      this.updateDebugStatus);
  },

  render() {
    let { client } = this.props;
    let { debugDisabled, extensions: targets } = this.state;
    let name = Strings.GetStringFromName("extensions");

    return React.createElement(
      "div", { id: "tab-addons", className: "tab", role: "tabpanel",
        "aria-labelledby": "tab-addons-header-name" },
        React.createElement(TabHeader, {
          id: "tab-addons-header-name",
          name: Strings.GetStringFromName("addons")}),
        React.createElement(AddonsControls, { debugDisabled }),
        React.createElement(
          "div", { id: "addons" },
          React.createElement(TargetList,
            { name, targets, client, debugDisabled })
      )
    );
  },

  updateDebugStatus() {
    let debugDisabled =
      !Services.prefs.getBoolPref(CHROME_ENABLED_PREF) ||
      !Services.prefs.getBoolPref(REMOTE_ENABLED_PREF);

    this.setState({ debugDisabled });
  },

  updateAddonsList() {
    AddonManager.getAllAddons(addons => {
      let extensions = addons.filter(addon => addon.isDebuggable).map(addon => {
        return {
          name: addon.name,
          icon: addon.iconURL || ExtensionIcon,
          type: addon.type,
          addonID: addon.id
        };
      });
      this.setState({ extensions });
    });
  },

  /**
   * Mandatory callback as AddonManager listener.
   */
  onInstalled() {
    this.updateAddonsList();
  },

  /**
   * Mandatory callback as AddonManager listener.
   */
  onUninstalled() {
    this.updateAddonsList();
  },

  /**
   * Mandatory callback as AddonManager listener.
   */
  onEnabled() {
    this.updateAddonsList();
  },

  /**
   * Mandatory callback as AddonManager listener.
   */
  onDisabled() {
    this.updateAddonsList();
  },
});

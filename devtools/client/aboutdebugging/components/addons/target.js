/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* eslint-env browser */
/* globals BrowserToolboxProcess */

"use strict";

loader.lazyImporter(this, "BrowserToolboxProcess",
  "resource://devtools/client/framework/ToolboxProcess.jsm");

const { createClass, DOM: dom } =
  require("devtools/client/shared/vendor/react");
const Services = require("Services");

const Strings = Services.strings.createBundle(
  "chrome://devtools/locale/aboutdebugging.properties");

module.exports = createClass({
  displayName: "AddonTarget",

  debug() {
    let { target } = this.props;
    BrowserToolboxProcess.init({ addonID: target.addonID });
  },

  reload() {
    let { client, target } = this.props;
    // This function sometimes returns a partial promise that only
    // implements then().
    client.request({
      to: target.addonActor,
      type: "reload"
    }).then(() => {}, error => {
      throw new Error(
        "Error reloading addon " + target.addonID + ": " + error);
    });
  },

  render() {
    let { target, debugDisabled } = this.props;

    return dom.li({ className: "target-container" },
      dom.img({
        className: "target-icon",
        role: "presentation",
        src: target.icon
      }),
      dom.div({ className: "target" },
        dom.div({ className: "target-name" }, target.name)
      ),
      dom.button({
        className: "debug-button",
        onClick: this.debug,
        disabled: debugDisabled,
      }, Strings.GetStringFromName("debug")),
      dom.button({
        className: "reload-button",
        onClick: this.reload
      }, Strings.GetStringFromName("reload"))
    );
  }
});

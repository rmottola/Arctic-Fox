/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* eslint no-unused-vars: [2, {"vars": "local"}] */

const { utils: Cu } = Components;
const { require } = Cu.import("resource://devtools/shared/Loader.jsm", {});

const promise = require("promise");
const { Task } = require("resource://gre/modules/Task.jsm");
const Store = require("devtools/client/responsive.html/store");

const DevToolsUtils = require("devtools/shared/DevToolsUtils");
DevToolsUtils.testing = true;
do_register_cleanup(() => {
  DevToolsUtils.testing = false;
});

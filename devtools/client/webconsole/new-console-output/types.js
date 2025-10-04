/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

const Immutable = require("devtools/client/shared/vendor/immutable");

const {
  MESSAGE_SOURCE,
  MESSAGE_TYPE,
  MESSAGE_LEVEL
} = require("devtools/client/webconsole/new-console-output/constants");

exports.ConsoleCommand = Immutable.Record({
  id: null,
  allowRepeating: false,
  messageText: null,
  source: MESSAGE_SOURCE.JAVASCRIPT,
  type: MESSAGE_TYPE.COMMAND,
  level: MESSAGE_LEVEL.LOG,
  category: "input",
  severity: MESSAGE_TYPE.LOG,
});

exports.ConsoleMessage = Immutable.Record({
  id: null,
  allowRepeating: true,
  source: null,
  type: null,
  level: null,
  messageText: null,
  parameters: null,
  repeat: 1,
  repeatId: null,
  category: "output",
  severity: "log",
  stacktrace: null,
});

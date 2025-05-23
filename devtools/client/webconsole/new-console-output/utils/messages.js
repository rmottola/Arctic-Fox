/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  CATEGORY_CLASS_FRAGMENTS,
  CATEGORY_JS,
  CATEGORY_WEBDEV,
  CATEGORY_OUTPUT,
  LEVELS,
  SEVERITY_CLASS_FRAGMENTS,
  SEVERITY_ERROR,
  SEVERITY_WARNING,
  SEVERITY_LOG,
} = require("../constants");
const WebConsoleUtils = require("devtools/shared/webconsole/utils").Utils;
const STRINGS_URI = "chrome://devtools/locale/webconsole.properties";
const l10n = new WebConsoleUtils.L10n(STRINGS_URI);
const { ConsoleMessage } = require("../types");

function prepareMessage(packet) {
  if (packet._type) {
    packet = convertCachedPacket(packet);
  }

  switch (packet.type) {
    case "consoleAPICall": {
      let { message } = packet;

      severity = SEVERITY_CLASS_FRAGMENTS[SEVERITY_ERROR];
      if (data.warning || data.strict) {
        severity = SEVERITY_CLASS_FRAGMENTS[SEVERITY_WARNING];
      } else if (data.info) {
        severity = SEVERITY_CLASS_FRAGMENTS[SEVERITY_LOG];
      }

      return new ConsoleMessage({
        source: MESSAGE_SOURCE.CONSOLE_API,
        type,
        level,
        parameters,
        messageText,
        repeatId: getRepeatId(message),
        category: CATEGORY_WEBDEV,
        severity: level,
      });
    }

    case "pageError": {
      let { pageError } = packet;
      let level = MESSAGE_LEVEL.ERROR;
      if (pageError.warning || pageError.strict) {
        level = MESSAGE_LEVEL.WARN;
      } else if (pageError.info) {
        level = MESSAGE_LEVEL.INFO;
      }

      return new ConsoleMessage({
        source: MESSAGE_SOURCE.JAVASCRIPT,
        type: MESSAGE_TYPE.LOG,
        messageText: pageError.errorMessage,
        repeatId: getRepeatId(pageError),
        category: CATEGORY_JS,
        severity: level,
      });
    }
}

// Helpers
function getRepeatId(message) {
  let clonedMessage = JSON.parse(JSON.stringify(message));
  delete clonedMessage.timeStamp;
  delete clonedMessage.uniqueID;
  return JSON.stringify(clonedMessage);
}

function convertCachedPacket(packet) {
  // The devtools server provides cached message packets in a different shape
  // from those of consoleApiCalls, so we prepare them for preparation here.
  let convertPacket = {};
  if (packet._type === "ConsoleAPI") {
    convertPacket.message = packet;
    convertPacket.type = "consoleAPICall";
  } else if (packet._type === "PageError") {
    convertPacket.pageError = packet;
    convertPacket.type = "pageError";
  } else {
    throw new Error("Unexpected packet type");
  }
  return convertPacket;
}

exports.prepareMessage = prepareMessage;
// Export for use in testing.
exports.getRepeatId = getRepeatId;

exports.l10n = l10n;

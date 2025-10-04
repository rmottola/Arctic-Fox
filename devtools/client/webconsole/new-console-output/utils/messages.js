/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const WebConsoleUtils = require("devtools/client/webconsole/utils").Utils;
const STRINGS_URI = "chrome://devtools/locale/webconsole.properties";
const l10n = new WebConsoleUtils.L10n(STRINGS_URI);

const {
  MESSAGE_SOURCE,
  MESSAGE_TYPE,
  MESSAGE_LEVEL,
} = require("../constants");
const { ConsoleMessage } = require("../types");

function prepareMessage(packet, idGenerator) {
  // This packet is already in the expected packet structure. Simply return.
  if (!packet.source) {
    packet = transformPacket(packet);
  }

  if (packet.allowRepeating) {
    packet = packet.set("repeatId", getRepeatId(packet));
  }
  return packet.set("id", idGenerator.getNextId());
}

/**
 * Transforms a packet from Firefox RDP structure to Chrome RDP structure.
 */
function transformPacket(packet) {
  if (packet._type) {
    packet = convertCachedPacket(packet);
  }

  switch (packet.type) {
    case "consoleAPICall": {
      let { message } = packet;

      let parameters = message.arguments;
      let type = message.level;
      let level = getLevelFromType(type);
      let messageText = null;

      // Special per-type conversion.
      switch (type) {
        case "clear":
          // We show a message to users when calls console.clear() is called.
          parameters = [l10n.getStr("consoleCleared")];
          break;
        case "count":
          // Chrome RDP doesn't have a special type for count.
          type = MESSAGE_TYPE.LOG;
          let {counter} = message;
          let label = counter.label ? counter.label : l10n.getStr("noCounterLabel");
          messageText = `${label}: ${counter.count}`;
          parameters = null;
          break;
      }

      const frame = {
        source: message.filename || null,
        line: message.lineNumber || null,
        column: message.columnNumber || null
      };

      return new ConsoleMessage({
        source: MESSAGE_SOURCE.CONSOLE_API,
        type,
        level,
        parameters,
        messageText,
        stacktrace: message.stacktrace ? message.stacktrace : null,
        frame
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
        level,
        messageText: pageError.errorMessage,
      });
    }

    case "evaluationResult":
    default: {
      let { result } = packet;

      return new ConsoleMessage({
        source: MESSAGE_SOURCE.JAVASCRIPT,
        type: MESSAGE_TYPE.RESULT,
        level: MESSAGE_LEVEL.LOG,
        parameters: result,
      });
    }
  }
}

// Helpers
function getRepeatId(message) {
  message = message.toJS();
  delete message.repeat;
  return JSON.stringify(message);
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

/**
 * Maps a Firefox RDP type to its corresponding level.
 */
function getLevelFromType(type) {
  const levels = {
    LEVEL_ERROR: "error",
    LEVEL_WARNING: "warn",
    LEVEL_INFO: "info",
    LEVEL_LOG: "log",
    LEVEL_DEBUG: "debug",
  };

  // A mapping from the console API log event levels to the Web Console levels.
  const levelMap = {
    error: levels.LEVEL_ERROR,
    exception: levels.LEVEL_ERROR,
    assert: levels.LEVEL_ERROR,
    warn: levels.LEVEL_WARNING,
    info: levels.LEVEL_INFO,
    log: levels.LEVEL_LOG,
    clear: levels.LEVEL_LOG,
    trace: levels.LEVEL_LOG,
    table: levels.LEVEL_LOG,
    debug: levels.LEVEL_LOG,
    dir: levels.LEVEL_LOG,
    dirxml: levels.LEVEL_LOG,
    group: levels.LEVEL_LOG,
    groupCollapsed: levels.LEVEL_LOG,
    groupEnd: levels.LEVEL_LOG,
    time: levels.LEVEL_LOG,
    timeEnd: levels.LEVEL_LOG,
    count: levels.LEVEL_DEBUG,
  };

  return levelMap[type] || MESSAGE_TYPE.LOG;
}

exports.prepareMessage = prepareMessage;
// Export for use in testing.
exports.getRepeatId = getRepeatId;

exports.l10n = l10n;

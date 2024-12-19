/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// React & Redux
const {
  createClass,
  createFactory,
  PropTypes
} = require("devtools/client/shared/vendor/react");

const MessageContainer = createClass({

  propTypes: {
    message: PropTypes.object.isRequired
  },

  displayName: "MessageContainer",

  render() {
    const { message } = this.props;
    let MessageComponent = getMessageComponent(message.messageType);
    return MessageComponent({ message });
  }
});

function getMessageComponent(messageType) {
  let MessageComponent;
  switch (messageType) {
    case "ConsoleApiCall":
      MessageComponent = require("devtools/client/webconsole/new-console-output/components/message-types/console-api-call").ConsoleApiCall;
      break;
    case "EvaluationResult":
      MessageComponent = require("devtools/client/webconsole/new-console-output/components/message-types/evaluation-result").EvaluationResult;
      break;
    case "PageError":
      MessageComponent = require("devtools/client/webconsole/new-console-output/components/message-types/page-error").PageError;
      break;
  }
  return createFactory(MessageComponent);
}

module.exports.MessageContainer = MessageContainer;

// Exported so we can test it with unit tests.
module.exports.getMessageComponent = getMessageComponent;

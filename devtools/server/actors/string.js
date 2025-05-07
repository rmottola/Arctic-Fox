/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

var {DebuggerServer} = require("devtools/server/main");

var promise = require("promise");
var {Class} = require("sdk/core/heritage");

var protocol = require("devtools/shared/protocol");
var {method, Arg, RetVal} = protocol;

exports.LongStringActor = protocol.ActorClass({
  typeName: "longstractor",

  initialize: function (conn, str) {
    protocol.Actor.prototype.initialize.call(this, conn);
    this.str = str;
    this.short = (this.str.length < DebuggerServer.LONG_STRING_LENGTH);
  },

  destroy: function () {
    this.str = null;
    protocol.Actor.prototype.destroy.call(this);
  },

  form: function () {
    if (this.short) {
      return this.str;
    }
    return {
      type: "longString",
      actor: this.actorID,
      length: this.str.length,
      initial: this.str.substring(0, DebuggerServer.LONG_STRING_INITIAL_LENGTH)
    };
  },

  substring: method(function (start, end) {
    return promise.resolve(this.str.substring(start, end));
  }, {
    request: {
      start: Arg(0),
      end: Arg(1)
    },
    response: { substring: RetVal() },
  }),

  release: method(function () { }, { release: true })
});

/**
 * When a caller is expecting a LongString actor but the string is already available on
 * client, the SimpleStringFront can be used as it shares the same API as a
 * LongStringFront but will not make unnecessary trips to the server.
 */
exports.SimpleStringFront = Class({
  initialize: function (str) {
    this.str = str;
  },

  get length() {
    return this.str.length;
  },

  get initial() {
    return this.str;
  },

  string: function () {
    return promise.resolve(this.str);
  },

  substring: function (start, end) {
    return promise.resolve(this.str.substring(start, end));
  },

  release: function () {
    this.str = null;
    return promise.resolve(undefined);
  }
});

exports.LongStringFront = protocol.FrontClass(exports.LongStringActor, {
  initialize: function (client) {
    protocol.Front.prototype.initialize.call(this, client);
  },

  destroy: function () {
    this.initial = null;
    this.length = null;
    this.strPromise = null;
    protocol.Front.prototype.destroy.call(this);
  },

  form: function (form) {
    this.actorID = form.actor;
    this.initial = form.initial;
    this.length = form.length;
  },

  string: function () {
    if (!this.strPromise) {
      let promiseRest = (thusFar) => {
        if (thusFar.length === this.length) {
          return promise.resolve(thusFar);
        }
        return this.substring(thusFar.length,
          thusFar.length + DebuggerServer.LONG_STRING_READ_LENGTH)
          .then((next) => promiseRest(thusFar + next));
      };

      this.strPromise = promiseRest(this.initial);
    }
    return this.strPromise;
  }
});

// The long string actor needs some custom marshalling, because it is sometimes
// returned as a primitive rather than a complete form.

var stringActorType = protocol.types.getType("longstractor");
protocol.types.addType("longstring", {
  _actor: true,
  write: (value, context, detail) => {
    if (!(context instanceof protocol.Actor)) {
      throw Error("Passing a longstring as an argument isn't supported.");
    }

    if (value.short) {
      return value.str;
    }
    return stringActorType.write(value, context, detail);
  },
  read: (value, context, detail) => {
    if (context instanceof protocol.Actor) {
      throw Error("Passing a longstring as an argument isn't supported.");
    }
    if (typeof (value) === "string") {
      return exports.SimpleStringFront(value);
    }
    return stringActorType.read(value, context, detail);
  }
});

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * EventEmitter.
 */

(function (factory) { // Module boilerplate
  if (this.module && module.id.indexOf("event-emitter") >= 0) { // require
    factory.call(this, require, exports, module);
  } else { // Cu.import
      this.isWorker = false;
      // Bug 1259045: This module is loaded early in firefox startup as a JSM,
      // but it doesn't depends on any real module. We can save a few cycles
      // and bytes by not loading Loader.jsm.
      let require = function(module) {
        const Cu = Components.utils;
        switch(module) {
          case "promise":
            return Cu.import("resource://gre/modules/Promise.jsm", {}).Promise;
          case "Services":
            return Cu.import("resource://gre/modules/Services.jsm", {}).Services;
          case "chrome":
            return {
              Cu,
              components: Components
            };
        }
        return null;
      }
      factory.call(this, require, this, { exports: this });
      this.EXPORTED_SYMBOLS = ["EventEmitter"];
  }
}).call(this, function (require, exports, module) {

this.EventEmitter = function EventEmitter() {};
module.exports = EventEmitter;

// See comment in JSM module boilerplate when adding a new dependency.
const { Cu, components } = require("chrome");
const Services = require("Services");
const promise = require("promise");
var loggingEnabled = true;

if (!isWorker) {
  loggingEnabled = Services.prefs.getBoolPref("devtools.dump.emit");
  Services.prefs.addObserver("devtools.dump.emit", {
    observe: () => {
      loggingEnabled = Services.prefs.getBoolPref("devtools.dump.emit");
    }
  }, false);
}

/**
 * Decorate an object with event emitter functionality.
 *
 * @param Object aObjectToDecorate
 *        Bind all public methods of EventEmitter to
 *        the aObjectToDecorate object.
 */
EventEmitter.decorate = function EventEmitter_decorate (aObjectToDecorate) {
  let emitter = new EventEmitter();
  aObjectToDecorate.on = emitter.on.bind(emitter);
  aObjectToDecorate.off = emitter.off.bind(emitter);
  aObjectToDecorate.once = emitter.once.bind(emitter);
  aObjectToDecorate.emit = emitter.emit.bind(emitter);
};

EventEmitter.prototype = {
  /**
   * Connect a listener.
   *
   * @param string aEvent
   *        The event name to which we're connecting.
   * @param function aListener
   *        Called when the event is fired.
   */
  on: function EventEmitter_on(aEvent, aListener) {
    if (!this._eventEmitterListeners)
      this._eventEmitterListeners = new Map();
    if (!this._eventEmitterListeners.has(aEvent)) {
      this._eventEmitterListeners.set(aEvent, []);
    }
    this._eventEmitterListeners.get(aEvent).push(aListener);
  },

  /**
   * Listen for the next time an event is fired.
   *
   * @param string aEvent
   *        The event name to which we're connecting.
   * @param function aListener
   *        (Optional) Called when the event is fired. Will be called at most
   *        one time.
   * @return promise
   *        A promise which is resolved when the event next happens. The
   *        resolution value of the promise is the first event argument. If
   *        you need access to second or subsequent event arguments (it's rare
   *        that this is needed) then use aListener
   */
  once: function EventEmitter_once(aEvent, aListener) {
    let deferred = promise.defer();

    let handler = (aEvent, aFirstArg, ...aRest) => {
      this.off(aEvent, handler);
      if (aListener) {
        aListener.apply(null, [aEvent, aFirstArg, ...aRest]);
      }
      deferred.resolve(aFirstArg);
    };

    handler._originalListener = aListener;
    this.on(aEvent, handler);

    return deferred.promise;
  },

  /**
   * Remove a previously-registered event listener.  Works for events
   * registered with either on or once.
   *
   * @param string aEvent
   *        The event name whose listener we're disconnecting.
   * @param function aListener
   *        The listener to remove.
   */
  off: function EventEmitter_off(aEvent, aListener) {
    if (!this._eventEmitterListeners)
      return;
    let listeners = this._eventEmitterListeners.get(aEvent);
    if (listeners) {
      this._eventEmitterListeners.set(aEvent, listeners.filter(l => {
        return l !== aListener && l._originalListener !== aListener;
      }));
    }
  },

  /**
   * Emit an event.  All arguments to this method will
   * be sent to listener functions.
   */
  emit: function EventEmitter_emit(aEvent) {
    this.logEvent(aEvent, arguments);

    if (!this._eventEmitterListeners || !this._eventEmitterListeners.has(aEvent)) {
      return;
    }

    let originalListeners = this._eventEmitterListeners.get(aEvent);
    for (let listener of this._eventEmitterListeners.get(aEvent)) {
      // If the object was destroyed during event emission, stop
      // emitting.
      if (!this._eventEmitterListeners) {
        break;
      }

      // If listeners were removed during emission, make sure the
      // event handler we're going to fire wasn't removed.
      if (originalListeners === this._eventEmitterListeners.get(aEvent) ||
          this._eventEmitterListeners.get(aEvent).some(l => l === listener)) {
        try {
          listener.apply(null, arguments);
        }
        catch (ex) {
          // Prevent a bad listener from interfering with the others.
          let msg = ex + ": " + ex.stack;
          console.error(msg);
          dump(msg + "\n");
        }
      }
    }
  },

  logEvent: function(aEvent, args) {
    if (!loggingEnabled) {
      return;
    }

    let caller, func, path;
    if (!isWorker) {
      caller = components.stack.caller.caller;
      func = caller.name;
      let file = caller.filename;
      if (file.includes(" -> ")) {
        file = caller.filename.split(/ -> /)[1];
      }
      path = file + ":" + caller.lineNumber;
    }

    let argOut = "(";
    if (args.length === 1) {
      argOut += aEvent;
    }

    let out = "EMITTING: ";

    // We need this try / catch to prevent any dead object errors.
    try {
      for (let i = 1; i < args.length; i++) {
        if (i === 1) {
          argOut = "(" + aEvent + ", ";
        } else {
          argOut += ", ";
        }

        let arg = args[i];
        argOut += arg;

        if (arg && arg.nodeName) {
          argOut += " (" + arg.nodeName;
          if (arg.id) {
            argOut += "#" + arg.id;
          }
          if (arg.className) {
            argOut += "." + arg.className;
          }
          argOut += ")";
        }
      }
    } catch(e) {
      // Object is dead so the toolbox is most likely shutting down,
      // do nothing.
    }

    argOut += ")";
    out += "emit" + argOut + " from " + func + "() -> " + path + "\n";

    dump(out);
  },
};

});

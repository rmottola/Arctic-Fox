/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

/**
 * This file contains utilities for creating elements for markers to be displayed,
 * and parsing out the blueprint to generate correct values for markers.
 */
const { Ci } = require("chrome");
const { L10N, PREFS } = require("devtools/client/performance/modules/global");

// String used to fill in platform data when it should be hidden.
const GECKO_SYMBOL = "(Gecko)";

/**
 * Mapping of JS marker causes to a friendlier form. Only
 * markers that are considered "from content" should be labeled here.
 */
const JS_MARKER_MAP = {
  "<script> element":          L10N.getStr("marker.label.javascript.scriptElement"),
  "promise callback":          L10N.getStr("marker.label.javascript.promiseCallback"),
  "promise initializer":       L10N.getStr("marker.label.javascript.promiseInit"),
  "Worker runnable":           L10N.getStr("marker.label.javascript.workerRunnable"),
  "javascript: URI":           L10N.getStr("marker.label.javascript.jsURI"),
  // The difference between these two event handler markers are differences
  // in their WebIDL implementation, so distinguishing them is not necessary.
  "EventHandlerNonNull":       L10N.getStr("marker.label.javascript.eventHandler"),
  "EventListener.handleEvent": L10N.getStr("marker.label.javascript.eventHandler"),
  // These markers do not get L10N'd because they're JS names.
  "setInterval handler":       "setInterval",
  "setTimeout handler":        "setTimeout",
  "FrameRequestCallback":      "requestAnimationFrame",
};

/**
 * A series of formatters used by the blueprint.
 */
exports.Formatters = {
  /**
   * Uses the marker name as the label for markers that do not have
   * a blueprint entry. Uses "Other" in the marker filter menu.
   */
  UnknownLabel: function (marker = {}) {
    return marker.name || L10N.getStr("marker.label.unknown");
  },

  /* Group 0 - Reflow and Rendering pipeline */

  StylesFields: function (marker) {
    if ("restyleHint" in marker) {
      let label = marker.restyleHint.replace(/eRestyle_/g, "");
      return {
        [L10N.getStr("marker.field.restyleHint")]: label
      };
    }
  },

  /* Group 1 - JS */

  DOMEventFields: function (marker) {
    let fields = Object.create(null);

    if ("type" in marker) {
      fields[L10N.getStr("marker.field.DOMEventType")] = marker.type;
    }

    if ("eventPhase" in marker) {
      let label;
      switch (marker.eventPhase) {
        case Ci.nsIDOMEvent.AT_TARGET:
          label = L10N.getStr("marker.value.DOMEventTargetPhase");
          break;
        case Ci.nsIDOMEvent.CAPTURING_PHASE:
          label = L10N.getStr("marker.value.DOMEventCapturingPhase");
          break;
        case Ci.nsIDOMEvent.BUBBLING_PHASE:
          label = L10N.getStr("marker.value.DOMEventBubblingPhase");
          break;
      }
      fields[L10N.getStr("marker.field.DOMEventPhase")] = label;
    }

    return fields;
  },

  JSLabel: function (marker = {}) {
    let generic = L10N.getStr("marker.label.javascript");
    if ("causeName" in marker) {
      return JS_MARKER_MAP[marker.causeName] || generic;
    }
    return generic;
  },

  JSFields: function (marker) {
    if ("causeName" in marker && !JS_MARKER_MAP[marker.causeName]) {
      let label = PREFS["show-platform-data"] ? marker.causeName : GECKO_SYMBOL;
      return {
        [L10N.getStr("marker.field.causeName")]: label
      };
    }
  },

  GCLabel: function (marker) {
    if (!marker) {
      return L10N.getStr("marker.label.garbageCollection2");
    }
    // Only if a `nonincrementalReason` exists, do we want to label
    // this as a non incremental GC event.
    if ("nonincrementalReason" in marker) {
      return L10N.getStr("marker.label.garbageCollection.nonIncremental");
    } else {
      return L10N.getStr("marker.label.garbageCollection.incremental");
    }
  },

  GCFields: function (marker) {
    let fields = Object.create(null);

    if ("causeName" in marker) {
      let cause = marker.causeName;
      let label = L10N.getStr(`marker.gcreason.label.${cause}`) || cause;
      fields[L10N.getStr("marker.field.causeName")] = label;
    }

    if ("nonincrementalReason" in marker) {
      let label = marker.nonincrementalReason;
      fields[L10N.getStr("marker.field.nonIncrementalCause")] = label;
    }

    return fields;
  },

  MinorGCFields: function (marker) {
    let fields = Object.create(null);

    if ("causeName" in marker) {
      let cause = marker.causeName;
      let label = L10N.getStr(`marker.gcreason.label.${cause}`) || cause;
      fields[L10N.getStr("marker.field.causeName")] = label;
    }

    fields[L10N.getStr("marker.field.type")] = L10N.getStr("marker.nurseryCollection");

    return fields;
  },

  CycleCollectionFields: function (marker) {
    let label = marker.name.replace(/nsCycleCollector::/g, "");
    return {
      [L10N.getStr("marker.field.type")]: label
    };
  },

  WorkerFields: function (marker) {
    if ("workerOperation" in marker) {
      let label = L10N.getStr(`marker.worker.${marker.workerOperation}`);
      return {
        [L10N.getStr("marker.field.type")]: label
      };
    }
  },

  MessagePortFields: function (marker) {
    if ("messagePortOperation" in marker) {
      let label = L10N.getStr(`marker.messagePort.${marker.messagePortOperation}`);
      return {
        [L10N.getStr("marker.field.type")]: label
      };
    }
  },

  /* Group 2 - User Controlled */

  ConsoleTimeFields: [{
    label: L10N.getStr("marker.field.consoleTimerName"),
    property: "causeName",
  }],

  TimeStampFields: [{
    label: L10N.getStr("marker.field.label"),
    property: "causeName",
  }]
};

/**
 * Takes a main label (e.g. "Timestamp") and a property name (e.g. "causeName"),
 * and returns a string that represents that property value for a marker if it
 * exists (e.g. "Timestamp (rendering)"), or just the main label if it does not.
 *
 * @param string mainLabel
 * @param string propName
 */
exports.Formatters.labelForProperty = function (mainLabel, propName) {
  return (marker={}) => marker[propName] ? `${mainLabel} (${marker[propName]})` : mainLabel;
};

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

var { classes: Cc, interfaces: Ci, utils: Cu } = Components;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/Troubleshoot.jsm");
Cu.import("resource://gre/modules/ResetProfile.jsm");
Cu.import("resource://gre/modules/AppConstants.jsm");

XPCOMUtils.defineLazyModuleGetter(this, "PluralForm",
                                  "resource://gre/modules/PluralForm.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "PlacesDBUtils",
                                  "resource://gre/modules/PlacesDBUtils.jsm");

window.addEventListener("load", function onload(event) {
  try {
  window.removeEventListener("load", onload, false);
  Troubleshoot.snapshot(function (snapshot) {
    for (let prop in snapshotFormatters)
      snapshotFormatters[prop](snapshot[prop]);
  });
  populateActionBox();
  setupEventListeners();
  } catch (e) {
    Cu.reportError("stack of load error for about:support: " + e + ": " + e.stack);
  }
}, false);

// Each property in this object corresponds to a property in Troubleshoot.jsm's
// snapshot data.  Each function is passed its property's corresponding data,
// and it's the function's job to update the page with it.
var snapshotFormatters = {

  application: function application(data) {
    $("application-box").textContent = data.name;
    $("useragent-box").textContent = data.userAgent;
    $("os-box").textContent = data.osVersion;
    $("supportLink").href = data.supportURL;
    let version = AppConstants.MOZ_APP_VERSION_DISPLAY;
    if (data.vendor)
      version += " (" + data.vendor + ")";
    $("version-box").textContent = version;
    $("buildid-box").textContent = data.buildID;
    if (data.updateChannel)
      $("updatechannel-box").textContent = data.updateChannel;

    let statusStrName = ".unknown";

    // Whitelist of known values with string descriptions:
    switch (data.autoStartStatus) {
      case 0:
      case 1:
      case 2:
      case 4:
      case 5:
      case 6:
      case 7:
      case 8:
      case 9:
        statusStrName = "." + data.autoStartStatus;
    }

    let statusText = stringBundle().GetStringFromName("multiProcessStatus" + statusStrName);
    $("multiprocess-box").textContent = stringBundle().formatStringFromName("multiProcessWindows",
      [data.numRemoteWindows, data.numTotalWindows, statusText], 3);

    $("safemode-box").textContent = data.safeMode;
  },

  extensions: function extensions(data) {
    $.append($("extensions-tbody"), data.map(function (extension) {
      return $.new("tr", [
        $.new("td", extension.name),
        $.new("td", extension.version),
        $.new("td", extension.isActive),
        $.new("td", extension.id),
      ]);
    }));
  },

  experiments: function experiments(data) {
    $.append($("experiments-tbody"), data.map(function (experiment) {
      return $.new("tr", [
        $.new("td", experiment.name),
        $.new("td", experiment.id),
        $.new("td", experiment.description),
        $.new("td", experiment.active),
        $.new("td", experiment.endDate),
        $.new("td", [
          $.new("a", experiment.detailURL, null, {href : experiment.detailURL,})
        ]),
      ]);
    }));
  },

  modifiedPreferences: function modifiedPreferences(data) {
    $.append($("prefs-tbody"), sortedArrayFromObject(data).map(
      function ([name, value]) {
        return $.new("tr", [
          $.new("td", name, "pref-name"),
          // Very long preference values can cause users problems when they
          // copy and paste them into some text editors.  Long values generally
          // aren't useful anyway, so truncate them to a reasonable length.
          $.new("td", String(value).substr(0, 120), "pref-value"),
        ]);
      }
    ));
  },

  lockedPreferences: function lockedPreferences(data) {
    $.append($("locked-prefs-tbody"), sortedArrayFromObject(data).map(
      function ([name, value]) {
        return $.new("tr", [
          $.new("td", name, "pref-name"),
          $.new("td", String(value).substr(0, 120), "pref-value"),
        ]);
      }
    ));
  },

  graphics: function graphics(data) {
    let strings = stringBundle();

    function localizedMsg(msgArray) {
      let nameOrMsg = msgArray.shift();
      if (msgArray.length) {
        // formatStringFromName logs an NS_ASSERTION failure otherwise that says
        // "use GetStringFromName".  Lame.
        try {
          return strings.formatStringFromName(nameOrMsg, msgArray,
                                              msgArray.length);
        }
        catch (err) {
          // Throws if nameOrMsg is not a name in the bundle.  This shouldn't
          // actually happen though, since msgArray.length > 1 => nameOrMsg is a
          // name in the bundle, not a message, and the remaining msgArray
          // elements are parameters.
          return nameOrMsg;
        }
      }
      try {
        return strings.GetStringFromName(nameOrMsg);
      }
      catch (err) {
        // Throws if nameOrMsg is not a name in the bundle.
      }
      return nameOrMsg;
    }

    // Read APZ info out of data.info, stripping it out in the process.
    let apzInfo = [];
    let formatApzInfo = function (info) {
      let out = [];
      for (let type of ['Wheel', 'Touch', 'Drag']) {
        let key = 'Apz' + type + 'Input';
        let warningKey = key + 'Warning';

        if (!(key in info))
          continue;

        let badPref = info[warningKey];
        delete info[key];
        delete info[warningKey];

        let message;
        if (badPref)
          message = localizedMsg([type.toLowerCase() + 'Warning', badPref]);
        else
          message = localizedMsg([type.toLowerCase() + 'Enabled']);
        dump(message + ', ' + (type.toLowerCase() + 'Warning') + ', ' + badPref + '\n');
        out.push(message);
      }

      return out;
    };

    // Create a <tr> element with key and value columns.
    //
    // @key      Text in the key column. Localized automatically, unless starts with "#".
    // @value    Text in the value column. Not localized.
    function buildRow(key, value) {
      let title;
      if (key[0] == "#") {
        title = key.substr(1);
      } else {
        try {
          title = strings.GetStringFromName(key);
        } catch (e) {
          title = key;
        }
      }
      return $.new("tr", [
        $.new("th", title, "column"),
        $.new("td", value),
      ]);
    }

    // @where    The name in "graphics-<name>-tbody", of the element to append to.
    // @trs      Array of row elements.
    function addRows(where, trs) {
      $.append($('graphics-' + where + '-tbody'), trs);
    }

    // Build and append a row.
    //
    // @where    The name in "graphics-<name>-tbody", of the element to append to.
    function addRow(where, key, value) {
      addRows(where, [buildRow(key, value)]);
    }
    if (data.clearTypeParameters !== undefined) {
      addRow("diagnostics", "clearTypeParameters", data.clearTypeParameters);
    }
    if ("info" in data) {
      apzInfo = formatApzInfo(data.info);

      let trs = sortedArrayFromObject(data.info).map(function ([prop, val]) {
        return $.new("tr", [
          $.new("th", prop, "column"),
          $.new("td", String(val)),
        ]);
      });
      addRows("diagnostics", trs);

      delete data.info;
    }

    // graphics-failures-tbody tbody
    if ("failures" in data) {
      // If indices is there, it should be the same length as failures,
      // (see Troubleshoot.jsm) but we check anyway:
      if ("indices" in data && data.failures.length == data.indices.length) {
        let combined = [];
        for (let i = 0; i < data.failures.length; i++) {
          let assembled = assembleFromGraphicsFailure(i, data);
          combined.push(assembled);
        }
        combined.sort(function(a,b) {
            if (a.index < b.index) return -1;
            if (a.index > b.index) return 1;
            return 0;});
        $.append($("graphics-failures-tbody"),
                 combined.map(function(val) {
                   return $.new("tr", [$.new("th", val.header, "column"),
                                       $.new("td", val.message)]);
                 }));
        delete data.indices;
      } else {
        $.append($("graphics-failures-tbody"),
          [$.new("tr", [$.new("th", "LogFailure", "column"),
                        $.new("td", data.failures.map(function (val) {
                          return $.new("p", val);
                       }))])]);
      }
    } else {
      $("graphics-failures-tbody").style.display = "none";
    }

    // Add a new row to the table, and take the key (or keys) out of data.
    //
    // @where        Table section to add to.
    // @key          Data key to use.
    // @colKey       The localization key to use, if different from key.
    function addRowFromKey(where, key, colKey) {
      if (!(key in data))
        return;
      colKey = colKey || key;

      let value;
      let messageKey = key + "Message";
      if (messageKey in data) {
        value = localizedMsg(data[messageKey]);
        delete data[messageKey];
      } else {
        value = data[key];
      }
      delete data[key];

      if (value) {
        addRow(where, colKey, value);
      }
    }

    // graphics-features-tbody

    let compositor = data.windowLayerManagerRemote
                     ? data.windowLayerManagerType
                     : "BasicLayers (" + strings.GetStringFromName("mainThreadNoOMTC") + ")";
    addRow("features", "compositing", compositor);
    delete data.windowLayerManagerRemote;
    delete data.windowLayerManagerType;
    delete data.numTotalWindows;
    delete data.numAcceleratedWindows;

    addRow("features", "asyncPanZoom",
           apzInfo.length
           ? apzInfo.join("; ")
           : localizedMsg(["apzNone"]));
    addRowFromKey("features", "webglRenderer");
    addRowFromKey("features", "supportsHardwareH264", "hardwareH264");
    addRowFromKey("features", "direct2DEnabled", "#Direct2D");

    if ("directWriteEnabled" in data) {
      let message = data.directWriteEnabled;
      if ("directWriteVersion" in data)
        message += " (" + data.directWriteVersion + ")";
      addRow("features", "#DirectWrite", message);
      delete data.directWriteEnabled;
      delete data.directWriteVersion;
    }

    // Adapter tbodies.
    let adapterKeys = [
      ["adapterDescription"],
      ["adapterVendorID"],
      ["adapterDeviceID"],
      ["driverVersion"],
      ["driverDate"],
      ["adapterDrivers"],
      ["adapterSubsysID"],
      ["adapterRAM"],
    ];

    function showGpu(id, suffix) {
      function get(prop) {
        return data[prop + suffix];
      }

      let trs = [];
      for (let key of adapterKeys) {
        let value = get(key);
        if (value === undefined || value === "")
          continue;
        trs.push(buildRow(key, value));
      }

      if (trs.length == 0) {
        $("graphics-" + id + "-tbody").style.display = "none";
        return;
      }

      let active = "yes";
      if ("isGPU2Active" in data && ((suffix == "2") != data.isGPU2Active)) {
        active = "no";
      }
      addRow(id, "active", strings.GetStringFromName(active));
      addRows(id, trs);
    };
    showGpu("gpu-1", "");
    showGpu("gpu-2", "2");

    // Remove adapter keys.
    for (let key of adapterKeys) {
      delete data[key];
      delete data[key + "2"];
    }
    delete data.isGPU2Active;

    // Now that we're done, grab any remaining keys in data and drop them into
    // the diagnostics section.
    for (let key in data) {
      addRow("diagnostics", key, data[key]);
    }
  },

  javaScript: function javaScript(data) {
    $("javascript-incremental-gc").textContent = data.incrementalGCEnabled;
  },

  accessibility: function accessibility(data) {
    $("a11y-activated").textContent = data.isActive;
    $("a11y-force-disabled").textContent = data.forceDisabled || 0;
  },

  libraryVersions: function libraryVersions(data) {
    let strings = stringBundle();
    let trs = [
      $.new("tr", [
        $.new("th", ""),
        $.new("th", strings.GetStringFromName("minLibVersions")),
        $.new("th", strings.GetStringFromName("loadedLibVersions")),
      ])
    ];
    sortedArrayFromObject(data).forEach(
      function ([name, val]) {
        trs.push($.new("tr", [
          $.new("td", name),
          $.new("td", val.minVersion),
          $.new("td", val.version),
        ]));
      }
    );
    $.append($("libversions-tbody"), trs);
  },

  userJS: function userJS(data) {
    if (!data.exists)
      return;
    let userJSFile = Services.dirsvc.get("PrefD", Ci.nsIFile);
    userJSFile.append("user.js");
    $("prefs-user-js-link").href = Services.io.newFileURI(userJSFile).spec;
    $("prefs-user-js-section").style.display = "";
    // Clear the no-copy class
    $("prefs-user-js-section").className = "";
  },

#if defined(XP_LINUX) && defined(MOZ_SANDBOX)
  sandbox: function sandbox(data) {
    let strings = stringBundle();
    let tbody = $("sandbox-tbody");
    for (let key in data) {
      // Simplify the display a little in the common case.
      if (key === "hasPrivilegedUserNamespaces" &&
          data[key] === data["hasUserNamespaces"]) {
        continue;
      }
      tbody.appendChild($.new("tr", [
        $.new("th", strings.GetStringFromName(key), "column"),
        $.new("td", data[key])
      ]));
    }
  },
#endif
};

var $ = document.getElementById.bind(document);

$.new = function $_new(tag, textContentOrChildren, className, attributes) {
  let elt = document.createElement(tag);
  if (className)
    elt.className = className;
  if (attributes) {
    for (let attrName in attributes)
      elt.setAttribute(attrName, attributes[attrName]);
  }
  if (Array.isArray(textContentOrChildren))
    this.append(elt, textContentOrChildren);
  else
    elt.textContent = String(textContentOrChildren);
  return elt;
};

$.append = function $_append(parent, children) {
  children.forEach(c => parent.appendChild(c));
};

function stringBundle() {
  return Services.strings.createBundle(
           "chrome://global/locale/aboutSupport.properties");
}

function assembleFromGraphicsFailure(i, data)
{
  // Only cover the cases we have today; for example, we do not have
  // log failures that assert and we assume the log level is 1/error.
  let message = data.failures[i];
  let index = data.indices[i];
  let what = "";
  if (message.search(/\[GFX1-\]: \(LF\)/) == 0) {
    // Non-asserting log failure - the message is substring(14)
    what = "LogFailure";
    message = message.substring(14);
  } else if (message.search(/\[GFX1-\]: /) == 0) {
    // Non-asserting - the message is substring(9)
    what = "Error";
    message = message.substring(9);
  } else if (message.search(/\[GFX1\]: /) == 0) {
    // Asserting - the message is substring(8)
    what = "Assert";
    message = message.substring(8);
  }
  let assembled = {"index" : index,
                   "header" : ("(#" + index + ") " + what),
                   "message" : message};
  return assembled;
}

function sortedArrayFromObject(obj) {
  let tuples = [];
  for (let prop in obj)
    tuples.push([prop, obj[prop]]);
  tuples.sort(([prop1, v1], [prop2, v2]) => prop1.localeCompare(prop2));
  return tuples;
}

function copyRawDataToClipboard(button) {
  if (button)
    button.disabled = true;
  try {
    Troubleshoot.snapshot(function (snapshot) {
      if (button)
        button.disabled = false;
      let str = Cc["@mozilla.org/supports-string;1"].
                createInstance(Ci.nsISupportsString);
      str.data = JSON.stringify(snapshot, undefined, 2);
      let transferable = Cc["@mozilla.org/widget/transferable;1"].
                         createInstance(Ci.nsITransferable);
      transferable.init(getLoadContext());
      transferable.addDataFlavor("text/unicode");
      transferable.setTransferData("text/unicode", str, str.data.length * 2);
      Cc["@mozilla.org/widget/clipboard;1"].
        getService(Ci.nsIClipboard).
        setData(transferable, null, Ci.nsIClipboard.kGlobalClipboard);
#ifdef ANDROID
      // Present a toast notification.
      let message = {
        type: "Toast:Show",
        message: stringBundle().GetStringFromName("rawDataCopied"),
        duration: "short"
      };
      Services.androidBridge.handleGeckoMessage(message);
#endif
    });
  }
  catch (err) {
    if (button)
      button.disabled = false;
    throw err;
  }
}

function getLoadContext() {
  return window.QueryInterface(Ci.nsIInterfaceRequestor)
               .getInterface(Ci.nsIWebNavigation)
               .QueryInterface(Ci.nsILoadContext);
}

function copyContentsToClipboard() {
  // Get the HTML and text representations for the important part of the page.
  let contentsDiv = $("contents");
  let dataHtml = contentsDiv.innerHTML;
  let dataText = createTextForElement(contentsDiv);

  // We can't use plain strings, we have to use nsSupportsString.
  let supportsStringClass = Cc["@mozilla.org/supports-string;1"];
  let ssHtml = supportsStringClass.createInstance(Ci.nsISupportsString);
  let ssText = supportsStringClass.createInstance(Ci.nsISupportsString);

  let transferable = Cc["@mozilla.org/widget/transferable;1"]
                       .createInstance(Ci.nsITransferable);
  transferable.init(getLoadContext());

  // Add the HTML flavor.
  transferable.addDataFlavor("text/html");
  ssHtml.data = dataHtml;
  transferable.setTransferData("text/html", ssHtml, dataHtml.length * 2);

  // Add the plain text flavor.
  transferable.addDataFlavor("text/unicode");
  ssText.data = dataText;
  transferable.setTransferData("text/unicode", ssText, dataText.length * 2);

  // Store the data into the clipboard.
  let clipboard = Cc["@mozilla.org/widget/clipboard;1"]
                    .getService(Ci.nsIClipboard);
  clipboard.setData(transferable, null, clipboard.kGlobalClipboard);

#ifdef ANDROID
  // Present a toast notification.
  let message = {
    type: "Toast:Show",
    message: stringBundle().GetStringFromName("textCopied"),
    duration: "short"
  };
  Services.androidBridge.handleGeckoMessage(message);
#endif
}

// Return the plain text representation of an element.  Do a little bit
// of pretty-printing to make it human-readable.
function createTextForElement(elem) {
  let serializer = new Serializer();
  let text = serializer.serialize(elem);

  // Actual CR/LF pairs are needed for some Windows text editors.
#ifdef XP_WIN
  text = text.replace(/\n/g, "\r\n");
#endif

  return text;
}

function Serializer() {
}

Serializer.prototype = {

  serialize: function (rootElem) {
    this._lines = [];
    this._startNewLine();
    this._serializeElement(rootElem);
    this._startNewLine();
    return this._lines.join("\n").trim() + "\n";
  },

  // The current line is always the line that writing will start at next.  When
  // an element is serialized, the current line is updated to be the line at
  // which the next element should be written.
  get _currentLine() {
    return this._lines.length ? this._lines[this._lines.length - 1] : null;
  },

  set _currentLine(val) {
    return this._lines[this._lines.length - 1] = val;
  },

  _serializeElement: function (elem) {
    if (this._ignoreElement(elem))
      return;

    // table
    if (elem.localName == "table") {
      this._serializeTable(elem);
      return;
    }

    // all other elements

    let hasText = false;
    for (let child of elem.childNodes) {
      if (child.nodeType == Node.TEXT_NODE) {
        let text = this._nodeText(
            child, (child.classList && child.classList.contains("endline")));
        this._appendText(text);
        hasText = hasText || !!text.trim();
      }
      else if (child.nodeType == Node.ELEMENT_NODE)
        this._serializeElement(child);
    }

    // For headings, draw a "line" underneath them so they stand out.
    if (/^h[0-9]+$/.test(elem.localName)) {
      let headerText = (this._currentLine || "").trim();
      if (headerText) {
        this._startNewLine();
        this._appendText("-".repeat(headerText.length));
      }
    }

    // Add a blank line underneath block elements but only if they contain text.
    if (hasText) {
      let display = window.getComputedStyle(elem).getPropertyValue("display");
      if (display == "block") {
        this._startNewLine();
        this._startNewLine();
      }
    }
  },

  _startNewLine: function () {
    let currLine = this._currentLine;
    if (currLine) {
      // The current line is not empty.  Trim it.
      this._currentLine = currLine.trim();
      if (!this._currentLine)
        // The current line became empty.  Discard it.
        this._lines.pop();
    }
    this._lines.push("");
  },

  _appendText: function (text) {
    this._currentLine += text;
  },

  _serializeTable: function (table) {
    // Collect the table's column headings if in fact there are any.  First
    // check thead.  If there's no thead, check the first tr.
    let colHeadings = {};
    let tableHeadingElem = table.querySelector("thead");
    if (!tableHeadingElem)
      tableHeadingElem = table.querySelector("tr");
    if (tableHeadingElem) {
      let tableHeadingCols = tableHeadingElem.querySelectorAll("th,td");
      // If there's a contiguous run of th's in the children starting from the
      // rightmost child, then consider them to be column headings.
      for (let i = tableHeadingCols.length - 1; i >= 0; i--) {
        if (tableHeadingCols[i].localName != "th")
          break;
        colHeadings[i] = this._nodeText(
            tableHeadingCols[i],
            (tableHeadingCols[i].classList &&
             tableHeadingCols[i].classList.contains("endline"))).trim();
      }
    }
    let hasColHeadings = Object.keys(colHeadings).length > 0;
    if (!hasColHeadings)
      tableHeadingElem = null;

    let trs = table.querySelectorAll("table > tr, tbody > tr");
    let startRow =
      tableHeadingElem && tableHeadingElem.localName == "tr" ? 1 : 0;

    if (startRow >= trs.length)
      // The table's empty.
      return;

    if (hasColHeadings && !this._ignoreElement(tableHeadingElem)) {
      // Use column headings.  Print each tr as a multi-line chunk like:
      //   Heading 1: Column 1 value
      //   Heading 2: Column 2 value
      for (let i = startRow; i < trs.length; i++) {
        if (this._ignoreElement(trs[i]))
          continue;
        let children = trs[i].querySelectorAll("td");
        for (let j = 0; j < children.length; j++) {
          let text = "";
          if (colHeadings[j])
            text += colHeadings[j] + ": ";
          text += this._nodeText(
              children[j],
              (children[j].classList &&
               children[j].classList.contains("endline"))).trim();
          this._appendText(text);
          this._startNewLine();
        }
        this._startNewLine();
      }
      return;
    }

    // Don't use column headings.  Assume the table has only two columns and
    // print each tr in a single line like:
    //   Column 1 value: Column 2 value
    for (let i = startRow; i < trs.length; i++) {
      if (this._ignoreElement(trs[i]))
        continue;
      let children = trs[i].querySelectorAll("th,td");
      let rowHeading = this._nodeText(
          children[0],
          (children[0].classList &&
           children[0].classList.contains("endline"))).trim();
      this._appendText(rowHeading + ": " + this._nodeText(
          children[1],
          (children[1].classList &&
           children[1].classList.contains("endline"))).trim());
      this._startNewLine();
    }
    this._startNewLine();
  },

  _ignoreElement: function (elem) {
    return elem.classList.contains("no-copy");
  },

  _nodeText: function (node, endline) {
    let whiteChars = /\s+/g
    let whiteCharsButNoEndline = /(?!\n)[\s]+/g;
    let _node = node.cloneNode(true);
    if (_node.firstElementChild &&
        (_node.firstElementChild.nodeName.toLowerCase() == "button")) {
      _node.removeChild(_node.firstElementChild);
    }
    return _node.textContent.replace(
        endline ? whiteCharsButNoEndline : whiteChars, " ");
  },
};

function openProfileDirectory() {
  // Get the profile directory.
  let currProfD = Services.dirsvc.get("ProfD", Ci.nsIFile);
  let profileDir = currProfD.path;

  // Show the profile directory.
  let nsLocalFile = Components.Constructor("@mozilla.org/file/local;1",
                                           "nsILocalFile", "initWithPath");
  new nsLocalFile(profileDir).reveal();
}

/**
 * Profile reset is only supported for the default profile if the appropriate migrator exists.
 */
function populateActionBox() {
  if (ResetProfile.resetSupported()) {
    $("reset-box").style.display = "block";
    $("action-box").style.display = "block";
  }
  if (!Services.appinfo.inSafeMode) {
    $("safe-mode-box").style.display = "block";
    $("action-box").style.display = "block";
  }
}

// Prompt user to restart the browser in safe mode
function safeModeRestart() {
  let cancelQuit = Cc["@mozilla.org/supports-PRBool;1"]
                     .createInstance(Ci.nsISupportsPRBool);
  Services.obs.notifyObservers(cancelQuit, "quit-application-requested", "restart");

  if (!cancelQuit.data) {
    Services.startup.restartInSafeMode(Ci.nsIAppStartup.eAttemptQuit);
  }
}

/**
 * Set up event listeners for buttons.
 */
function setupEventListeners(){
#ifdef MOZ_UPDATER
  $("show-update-history-button").addEventListener("click", function (event) {
    var prompter = Cc["@mozilla.org/updates/update-prompt;1"].createInstance(Ci.nsIUpdatePrompt);
      prompter.showUpdateHistory(window);
  });
#endif
  $("reset-box-button").addEventListener("click", function (event){
    ResetProfile.openConfirmationDialog(window);
  });
  $("copy-raw-data-to-clipboard").addEventListener("click", function (event){
    copyRawDataToClipboard(this);
  });
  $("copy-to-clipboard").addEventListener("click", function (event){
    copyContentsToClipboard();
  });
  $("profile-dir-button").addEventListener("click", function (event){
    openProfileDirectory();
  });
  $("restart-in-safe-mode-button").addEventListener("click", function (event) {
    if (Services.obs.enumerateObservers("restart-in-safe-mode").hasMoreElements()) {
      Services.obs.notifyObservers(null, "restart-in-safe-mode", "");
    }
    else {
      safeModeRestart();
    }
  });
  $("verify-place-integrity-button").addEventListener("click", function(event) {
    PlacesDBUtils.checkAndFixDatabase(function(aLog) {
      let msg = aLog.join("\n");
      $("verify-place-result").style.display = "block";
      $("verify-place-result-parent").classList.remove("no-copy");
      $("verify-place-result").textContent = msg;
    });
  });
}

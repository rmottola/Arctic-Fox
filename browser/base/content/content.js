/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* This content script should work in any browser or iframe and should not
 * depend on the frame being contained in tabbrowser. */

let {classes: Cc, interfaces: Ci, utils: Cu, results: Cr} = Components;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/InlineSpellChecker.jsm");
Cu.import("resource://gre/modules/InlineSpellCheckerContent.jsm");

XPCOMUtils.defineLazyModuleGetter(this, "BrowserUtils",
  "resource://gre/modules/BrowserUtils.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "ContentLinkHandler",
  "resource:///modules/ContentLinkHandler.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "LoginManagerContent",
  "resource://gre/modules/LoginManagerContent.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "InsecurePasswordUtils",
  "resource://gre/modules/InsecurePasswordUtils.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "PluginContent",
  "resource:///modules/PluginContent.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "PrivateBrowsingUtils",
  "resource://gre/modules/PrivateBrowsingUtils.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "FormSubmitObserver",
  "resource:///modules/FormSubmitObserver.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "PageMetadata",
  "resource://gre/modules/PageMetadata.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "PlacesUIUtils",
  "resource:///modules/PlacesUIUtils.jsm");

// Bug 671101 - directly using webNavigation in this context
// causes docshells to leak
this.__defineGetter__("webNavigation", function () {
  return docShell.QueryInterface(Ci.nsIWebNavigation);
});

addMessageListener("WebNavigation:LoadURI", function (message) {
  let flags = message.json.flags || webNavigation.LOAD_FLAGS_NONE;

  webNavigation.loadURI(message.json.uri, flags, null, null, null);
});
XPCOMUtils.defineLazyGetter(this, "PageMenuChild", function() {
  let tmp = {};
  Cu.import("resource://gre/modules/PageMenu.jsm", tmp);
  return new tmp.PageMenuChild();
});

// TabChildGlobal
var global = this;

// Load the form validation popup handler
var formSubmitObserver = new FormSubmitObserver(content, this);

addMessageListener("ContextMenu:DoCustomCommand", function(message) {
  PageMenuChild.executeMenu(message.data);
});

addEventListener("DOMFormHasPassword", function(event) {
  InsecurePasswordUtils.checkForInsecurePasswords(event.target);
  LoginManagerContent.onFormPassword(event);
});
addEventListener("DOMAutoComplete", function(event) {
  LoginManagerContent.onUsernameInput(event);
});
addEventListener("blur", function(event) {
  LoginManagerContent.onUsernameInput(event);
});

if (Services.appinfo.processType == Services.appinfo.PROCESS_TYPE_CONTENT) {
  addEventListener("contextmenu", function (event) {
    let defaultPrevented = event.defaultPrevented;
    if (!Services.prefs.getBoolPref("dom.event.contextmenu.enabled")) {
      let plugin = null;
      try {
        plugin = event.target.QueryInterface(Ci.nsIObjectLoadingContent);
      } catch (e) {}
      if (plugin && plugin.displayedType == Ci.nsIObjectLoadingContent.TYPE_PLUGIN) {
        // Don't open a context menu for plugins.
        return;
      }

      defaultPrevented = false;
    }

    if (!defaultPrevented) {
      let editFlags = SpellCheckHelper.isEditable(event.target, content);
      let spellInfo;
      if (editFlags &
          (SpellCheckHelper.EDITABLE | SpellCheckHelper.CONTENTEDITABLE)) {
        spellInfo =
          InlineSpellCheckerContent.initContextMenu(event, editFlags, this);
      }

      sendSyncMessage("contextmenu", { editFlags, spellInfo }, { event });
    }
  }, false);
} else {
}

let handleContentContextMenu = function (event) {
  let defaultPrevented = event.defaultPrevented;
  if (!Services.prefs.getBoolPref("dom.event.contextmenu.enabled")) {
    let plugin = null;
    try {
      plugin = event.target.QueryInterface(Ci.nsIObjectLoadingContent);
    } catch (e) {}
    if (plugin && plugin.displayedType == Ci.nsIObjectLoadingContent.TYPE_PLUGIN) {
      // Don't open a context menu for plugins.
      return;
    }

    defaultPrevented = false;
  }

  if (defaultPrevented)
    return;

  let addonInfo = {};
  let subject = {
    event: event,
    addonInfo: addonInfo,
  };
  subject.wrappedJSObject = subject;
  Services.obs.notifyObservers(subject, "content-contextmenu", null);

  let doc = event.target.ownerDocument;
  let docLocation = doc.location.href;
  let charSet = doc.characterSet;
  let baseURI = doc.baseURI;
  let referrer = doc.referrer;
  let referrerPolicy = doc.referrerPolicy;
  let frameOuterWindowID = doc.defaultView.QueryInterface(Ci.nsIInterfaceRequestor)
                                          .getInterface(Ci.nsIDOMWindowUtils)
                                          .outerWindowID;

  // Media related cache info parent needs for saving
  let contentType = null;
  let contentDisposition = null;
  if (event.target.nodeType == Ci.nsIDOMNode.ELEMENT_NODE &&
      event.target instanceof Ci.nsIImageLoadingContent &&
      event.target.currentURI) {
    try {
      let imageCache = 
        Cc["@mozilla.org/image/tools;1"].getService(Ci.imgITools)
                                        .getImgCacheForDocument(doc);
      let props =
        imageCache.findEntryProperties(event.target.currentURI);
      try {
        contentType = props.get("type", Ci.nsISupportsCString).data;
      } catch(e) {}
      try {
        contentDisposition =
          props.get("content-disposition", Ci.nsISupportsCString).data;
      } catch(e) {}
    } catch(e) {}
  }

  let selectionInfo = BrowserUtils.getSelectionDetails(content);

  if (Services.appinfo.processType == Services.appinfo.PROCESS_TYPE_CONTENT) {
    let editFlags = SpellCheckHelper.isEditable(event.target, content);
    let spellInfo;
    if (editFlags &
        (SpellCheckHelper.EDITABLE | SpellCheckHelper.CONTENTEDITABLE)) {
      spellInfo =
        InlineSpellCheckerContent.initContextMenu(event, editFlags, this);
    }

    // Set the event target first as the copy image command needs it to
    // determine what was context-clicked on. Then, update the state of the
    // commands on the context menu.
    docShell.contentViewer.QueryInterface(Ci.nsIContentViewerEdit)
            .setCommandNode(event.target);
    event.target.ownerDocument.defaultView.updateCommands("contentcontextmenu");

    let customMenuItems = PageMenuChild.build(event.target);
    let principal = doc.nodePrincipal;
    sendSyncMessage("contextmenu",
                    { editFlags, spellInfo, customMenuItems, addonInfo,
                      principal, docLocation, charSet, baseURI, referrer,
                      referrerPolicy, contentType, contentDisposition,
                      frameOuterWindowID, selectionInfo },
                    { event, popupNode: event.target });
  }
  else {
    // Break out to the parent window and pass the add-on info along
    let browser = docShell.chromeEventHandler;
    let mainWin = browser.ownerDocument.defaultView;
    mainWin.gContextMenuContentData = {
      isRemote: false,
      event: event,
      popupNode: event.target,
      browser: browser,
      addonInfo: addonInfo,
      documentURIObject: doc.documentURIObject,
      docLocation: docLocation,
      charSet: charSet,
      referrer: referrer,
      referrerPolicy: referrerPolicy,
      contentType: contentType,
      contentDisposition: contentDisposition,
      selectionInfo: selectionInfo,
    };
  }
}

Cc["@mozilla.org/eventlistenerservice;1"]
  .getService(Ci.nsIEventListenerService)
  .addSystemEventListener(global, "contextmenu", handleContentContextMenu, false);

let AboutNetErrorListener = {
  init: function(chromeGlobal) {
    chromeGlobal.addEventListener('AboutNetErrorLoad', this, false, true);
    chromeGlobal.addEventListener('AboutNetErrorSetAutomatic', this, false, true);
    chromeGlobal.addEventListener('AboutNetErrorSendReport', this, false, true);
  },

  get isAboutNetError() {
    return content.document.documentURI.startsWith("about:neterror");
  },

  handleEvent: function(aEvent) {
    if (!this.isAboutNetError) {
      return;
    }

    switch (aEvent.type) {
    case "AboutNetErrorLoad":
      this.onPageLoad(aEvent);
      break;
    case "AboutNetErrorSetAutomatic":
      this.onSetAutomatic(aEvent);
      break;
    case "AboutNetErrorSendReport":
      this.onSendReport(aEvent);
      break;
    }
  },

  onPageLoad: function(evt) {
    let automatic = Services.prefs.getBoolPref("security.ssl.errorReporting.automatic");
    content.dispatchEvent(new content.CustomEvent("AboutNetErrorOptions", {
            detail: JSON.stringify({
              enabled: Services.prefs.getBoolPref("security.ssl.errorReporting.enabled"),
            automatic: automatic
            })
          }
    ));
    if (automatic) {
      this.onSendReport(evt);
    }
    // hide parts of the UI we don't need yet
    let contentDoc = content.document;

    let reportSendingMsg = contentDoc.getElementById("reportSendingMessage");
    let reportSentMsg = contentDoc.getElementById("reportSentMessage");
    let retryBtn = contentDoc.getElementById("reportCertificateErrorRetry");
    reportSendingMsg.style.display = "none";
    reportSentMsg.style.display = "none";
    retryBtn.style.display = "none";
  },

  onSetAutomatic: function(evt) {
    sendAsyncMessage("Browser:SetSSLErrorReportAuto", {
        automatic: evt.detail
      });
  },

  onSendReport: function(evt) {
    let contentDoc = content.document;

    let reportSendingMsg = contentDoc.getElementById("reportSendingMessage");
    let reportSentMsg = contentDoc.getElementById("reportSentMessage");
    let reportBtn = contentDoc.getElementById("reportCertificateError");
    let retryBtn = contentDoc.getElementById("reportCertificateErrorRetry");

    addMessageListener("Browser:SSLErrorReportStatus", function(message) {
      // show and hide bits - but only if this is a message for the right
      // document - we'll compare on document URI
      if (contentDoc.documentURI === message.data.documentURI) {
        switch(message.data.reportStatus) {
        case "activity":
          // Hide the button that was just clicked
          reportBtn.style.display = "none";
          retryBtn.style.display = "none";
          reportSentMsg.style.display = "none";
          reportSendingMsg.style.removeProperty("display");
          break;
        case "error":
          // show the retry button
          retryBtn.style.removeProperty("display");
          reportSendingMsg.style.display = "none";
          break;
        case "complete":
          // Show a success indicator
          reportSentMsg.style.removeProperty("display");
          reportSendingMsg.style.display = "none";
          break;
        }
      }
    });

    let failedChannel = docShell.failedChannel;
    let location = contentDoc.location.href;

    let serhelper = Cc["@mozilla.org/network/serialization-helper;1"]
                     .getService(Ci.nsISerializationHelper);

    let serializable =  docShell.failedChannel.securityInfo
                                .QueryInterface(Ci.nsITransportSecurityInfo)
                                .QueryInterface(Ci.nsISerializable);

    let serializedSecurityInfo = serhelper.serializeToString(serializable);

    sendAsyncMessage("Browser:SendSSLErrorReport", {
        elementId: evt.target.id,
        documentURI: contentDoc.documentURI,
        location: contentDoc.location,
        securityInfo: serializedSecurityInfo
      });
  }
}

AboutNetErrorListener.init(this);

let ClickEventHandler = {
  init: function init() {
    Cc["@mozilla.org/eventlistenerservice;1"]
      .getService(Ci.nsIEventListenerService)
      .addSystemEventListener(global, "click", this, true);
  },

  handleEvent: function(event) {
    if (!event.isTrusted || event.defaultPrevented || event.button == 2) {
      return;
    }

    let originalTarget = event.originalTarget;
    let ownerDoc = originalTarget.ownerDocument;

    // Handle click events from about pages
    if (ownerDoc.documentURI.startsWith("about:certerror")) {
      this.onAboutCertError(originalTarget, ownerDoc);
      return;
    } else if (ownerDoc.documentURI.startsWith("about:blocked")) {
      this.onAboutBlocked(originalTarget, ownerDoc);
      return;
    } else if (ownerDoc.documentURI.startsWith("about:neterror")) {
      this.onAboutNetError(originalTarget, ownerDoc);
      return;
    }

    let [href, node] = this._hrefAndLinkNodeForClickEvent(event);

    let json = { button: event.button, shiftKey: event.shiftKey,
                 ctrlKey: event.ctrlKey, metaKey: event.metaKey,
                 altKey: event.altKey, href: null, title: null,
                 bookmark: false, referrerPolicy: ownerDoc.referrerPolicy };

    if (href) {
      json.href = href;
      if (node) {
        json.title = node.getAttribute("title");
        if (event.button == 0 && !event.ctrlKey && !event.shiftKey &&
            !event.altKey && !event.metaKey) {
          json.bookmark = node.getAttribute("rel") == "sidebar";
          if (json.bookmark) {
            event.preventDefault(); // Need to prevent the pageload.
          }
        }
      }
      json.noReferrer = BrowserUtils.linkHasNoReferrer(node)

      sendAsyncMessage("Content:Click", json);
      return;
    }

    // This might be middle mouse navigation.
    if (event.button == 1) {
      sendAsyncMessage("Content:Click", json);
    }
  },

  onAboutCertError: function (targetElement, ownerDoc) {
    let docshell = ownerDoc.defaultView.QueryInterface(Ci.nsIInterfaceRequestor)
                                       .getInterface(Ci.nsIWebNavigation)
                                       .QueryInterface(Ci.nsIDocShell);
    let serhelper = Cc["@mozilla.org/network/serialization-helper;1"]
                     .getService(Ci.nsISerializationHelper);
    let serializedSSLStatus = "";

    try {
      let serializable =  docShell.failedChannel.securityInfo
                                  .QueryInterface(Ci.nsISSLStatusProvider)
                                  .SSLStatus
                                  .QueryInterface(Ci.nsISerializable);
      serializedSSLStatus = serhelper.serializeToString(serializable);
    } catch (e) { }

    sendAsyncMessage("Browser:CertExceptionError", {
      location: ownerDoc.location.href,
      elementId: targetElement.getAttribute("id"),
      isTopFrame: (ownerDoc.defaultView.parent === ownerDoc.defaultView),
      sslStatusAsString: serializedSSLStatus
    });
  },

  onAboutBlocked: function (targetElement, ownerDoc) {
    var reason = 'phishing';
    if (/e=malwareBlocked/.test(ownerDoc.documentURI)) {
      reason = 'malware';
    } else if (/e=unwantedBlocked/.test(ownerDoc.documentURI)) {
      reason = 'unwanted';
    }
    sendAsyncMessage("Browser:SiteBlockedError", {
      location: ownerDoc.location.href,
      reason: reason,
      elementId: targetElement.getAttribute("id"),
      isTopFrame: (ownerDoc.defaultView.parent === ownerDoc.defaultView)
    });
  },

  onAboutNetError: function (targetElement, ownerDoc) {
    let elmId = targetElement.getAttribute("id");
    if (elmId != "errorTryAgain" || !/e=netOffline/.test(ownerDoc.documentURI)) {
      return;
    }
    sendSyncMessage("Browser:NetworkError", {});
  },

  /**
   * Extracts linkNode and href for the current click target.
   *
   * @param event
   *        The click event.
   * @return [href, linkNode].
   *
   * @note linkNode will be null if the click wasn't on an anchor
   *       element (or XLink).
   */
  _hrefAndLinkNodeForClickEvent: function(event) {
    function isHTMLLink(aNode) {
      // Be consistent with what nsContextMenu.js does.
      return ((aNode instanceof content.HTMLAnchorElement && aNode.href) ||
              (aNode instanceof content.HTMLAreaElement && aNode.href) ||
              aNode instanceof content.HTMLLinkElement);
    }

    function makeURLAbsolute(aBase, aUrl) {
      // Note:  makeURI() will throw if aUri is not a valid URI
      return makeURI(aUrl, null, makeURI(aBase)).spec;
    }

    let node = event.target;
    while (node && !isHTMLLink(node)) {
      node = node.parentNode;
    }

    if (node)
      return [node.href, node];

    // If there is no linkNode, try simple XLink.
    let href, baseURI;
    node = event.target;
    while (node && !href) {
      if (node.nodeType == content.Node.ELEMENT_NODE) {
        href = node.getAttributeNS("http://www.w3.org/1999/xlink", "href");
        if (href)
          baseURI = node.baseURI;
      }
      node = node.parentNode;
    }

    // In case of XLink, we don't return the node we got href from since
    // callers expect <a>-like elements.
    return [href ? makeURLAbsolute(baseURI, href) : null, null];
  }
};
ClickEventHandler.init();

ContentLinkHandler.init(this);

// TODO: Load this lazily so the JSM is run only if a relevant event/message fires.
let pluginContent = new PluginContent(global);

addEventListener("DOMWebNotificationClicked", function(event) {
  sendAsyncMessage("DOMWebNotificationClicked", {});
}, false);

addEventListener("pageshow", function(event) {
  if (event.target == content.document) {
    sendAsyncMessage("PageVisibility:Show", {
      persisted: event.persisted,
    });
  }
});

let PageMetadataMessenger = {
  init() {
    addMessageListener("PageMetadata:GetPageData", this);
    addMessageListener("PageMetadata:GetMicrodata", this);
  },
  receiveMessage(message) {
    switch(message.name) {
      case "PageMetadata:GetPageData": {
        let result = PageMetadata.getData(content.document);
        sendAsyncMessage("PageMetadata:PageDataResult", result);
        break;
      }

      case "PageMetadata:GetMicrodata": {
        let target = message.objects;
        let result = PageMetadata.getMicrodata(content.document, target);
        sendAsyncMessage("PageMetadata:MicrodataResult", result);
        break;
      }
    }
  }
}
PageMetadataMessenger.init();

addMessageListener("ContextMenu:SaveVideoFrameAsImage", (message) => {
  let video = message.objects.target;
  let canvas = content.document.createElementNS("http://www.w3.org/1999/xhtml", "canvas");
  canvas.width = video.videoWidth;
  canvas.height = video.videoHeight;

  let ctxDraw = canvas.getContext("2d");
  ctxDraw.drawImage(video, 0, 0);
  sendAsyncMessage("ContextMenu:SaveVideoFrameAsImage:Result", {
    dataURL: canvas.toDataURL("image/jpeg", ""),
  });
});

addMessageListener("ContextMenu:MediaCommand", (message) => {
  let media = message.objects.element;

  switch (message.data.command) {
    case "play":
      media.play();
      break;
    case "pause":
      media.pause();
      break;
    case "mute":
      media.muted = true;
      break;
    case "unmute":
      media.muted = false;
      break;
    case "playbackRate":
      media.playbackRate = message.data.data;
      break;
    case "hidecontrols":
      media.removeAttribute("controls");
      break;
    case "showcontrols":
      media.setAttribute("controls", "true");
      break;
    case "hidestats":
    case "showstats":
      let event = media.ownerDocument.createEvent("CustomEvent");
      event.initCustomEvent("media-showStatistics", false, true,
                            message.data.command == "showstats");
      media.dispatchEvent(event);
      break;
    case "fullscreen":
      if (content.document.mozFullScreenEnabled)
        media.mozRequestFullScreen();
      break;
  }
});

addMessageListener("ContextMenu:Canvas:ToDataURL", (message) => {
  let dataURL = message.objects.target.toDataURL();
  sendAsyncMessage("ContextMenu:Canvas:ToDataURL:Result", { dataURL });
});

addMessageListener("ContextMenu:ReloadFrame", (message) => {
  message.objects.target.ownerDocument.location.reload();
});

addMessageListener("ContextMenu:ReloadImage", (message) => {
  let image = message.objects.target;
  if (image instanceof Ci.nsIImageLoadingContent)
    image.forceReload();
});

addMessageListener("ContextMenu:SearchFieldBookmarkData", (message) => {
  let node = message.objects.target;

  let charset = node.ownerDocument.characterSet;

  let formBaseURI = BrowserUtils.makeURI(node.form.baseURI,
                                         charset);

  let formURI = BrowserUtils.makeURI(node.form.getAttribute("action"),
                                     charset,
                                     formBaseURI);

  let spec = formURI.spec;

  let isURLEncoded =
               (node.form.method.toUpperCase() == "POST"
                && (node.form.enctype == "application/x-www-form-urlencoded" ||
                    node.form.enctype == ""));

  let title = node.ownerDocument.title;
  let description = PlacesUIUtils.getDescriptionFromDocument(node.ownerDocument);

  let formData = [];

  function escapeNameValuePair(aName, aValue, aIsFormUrlEncoded) {
    if (aIsFormUrlEncoded)
      return escape(aName + "=" + aValue);
    else
      return escape(aName) + "=" + escape(aValue);
  }

  for (let el of node.form.elements) {
    if (!el.type) // happens with fieldsets
      continue;

    if (el == node) {
      formData.push((isURLEncoded) ? escapeNameValuePair(el.name, "%s", true) :
                                     // Don't escape "%s", just append
                                     escapeNameValuePair(el.name, "", false) + "%s");
      continue;
    }

    let type = el.type.toLowerCase();

    if (((el instanceof content.HTMLInputElement && el.mozIsTextField(true)) ||
        type == "hidden" || type == "textarea") ||
        ((type == "checkbox" || type == "radio") && el.checked)) {
      formData.push(escapeNameValuePair(el.name, el.value, isURLEncoded));
    } else if (el instanceof content.HTMLSelectElement && el.selectedIndex >= 0) {
      for (let j=0; j < el.options.length; j++) {
        if (el.options[j].selected)
          formData.push(escapeNameValuePair(el.name, el.options[j].value,
                                            isURLEncoded));
      }
    }
  }

  let postData;

  if (isURLEncoded)
    postData = formData.join("&");
  else {
    let separator = spec.includes("?") ? "&" : "?";
    spec += separator + formData.join("&");
  }

  sendAsyncMessage("ContextMenu:SearchFieldBookmarkData:Result",
                   { spec, title, description, postData, charset });
});

addMessageListener("ContextMenu:BookmarkFrame", (message) => {
  let frame = message.objects.target.ownerDocument;
  sendAsyncMessage("ContextMenu:BookmarkFrame:Result",
                   { title: frame.title,
                     description: PlacesUIUtils.getDescriptionFromDocument(frame) });
});

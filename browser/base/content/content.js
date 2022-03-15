/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* This content script should work in any browser or iframe and should not
 * depend on the frame being contained in tabbrowser. */

let {classes: Cc, interfaces: Ci, utils: Cu, results: Cr} = Components;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource:///modules/ContentWebRTC.jsm");
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

XPCOMUtils.defineLazyModuleGetter(this, "Feeds", "resource:///modules/Feeds.jsm");

// TabChildGlobal
var global = this;

// Load the form validation popup handler
var formSubmitObserver = new FormSubmitObserver(content, this);

addMessageListener("ContextMenu:DoCustomCommand", function(message) {
  PageMenuChild.executeMenu(message.data);
});

addMessageListener("RemoteLogins:fillForm", function(message) {
  LoginManagerContent.receiveMessage(message, content);
});
addEventListener("DOMFormHasPassword", function(event) {
  LoginManagerContent.onDOMFormHasPassword(event, content);
  InsecurePasswordUtils.checkForInsecurePasswords(event.target);
});
addEventListener("pageshow", function(event) {
  LoginManagerContent.onPageShow(event, content);
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

  // get referrer attribute from clicked link and parse it
  // if per element referrer is enabled, the element referrer overrules
  // the document wide referrer
  if (Services.prefs.getBoolPref("network.http.enablePerElementReferrer")) {
    let referrerAttrValue = Services.netUtils.parseAttributePolicyString(event.target.
                            getAttribute("referrer"));
    if (referrerAttrValue !== Ci.nsIHttpChannel.REFERRER_POLICY_DEFAULT) {
      referrerPolicy = referrerAttrValue;
    }
  }

  let disableSetDesktopBg = null;
  // Media related cache info parent needs for saving
  let contentType = null;
  let contentDisposition = null;
  if (event.target.nodeType == Ci.nsIDOMNode.ELEMENT_NODE &&
      event.target instanceof Ci.nsIImageLoadingContent &&
      event.target.currentURI) {
    disableSetDesktopBg = disableSetDesktopBackground(event.target);

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
    sendRpcMessage("contextmenu",
                   { editFlags, spellInfo, customMenuItems, addonInfo,
                     principal, docLocation, charSet, baseURI, referrer,
                     referrerPolicy, contentType, contentDisposition,
                     frameOuterWindowID, selectionInfo, disableSetDesktopBg },
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
      disableSetDesktopBackground: disableSetDesktopBg,
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

    // get referrer attribute from clicked link and parse it
    // if per element referrer is enabled, the element referrer overrules
    // the document wide referrer
    let referrerPolicy = ownerDoc.referrerPolicy;
    if (Services.prefs.getBoolPref("network.http.enablePerElementReferrer") &&
        node) {
      let referrerAttrValue = Services.netUtils.parseAttributePolicyString(node.
                              getAttribute("referrer"));
      if (referrerAttrValue !== Ci.nsIHttpChannel.REFERRER_POLICY_DEFAULT) {
        referrerPolicy = referrerAttrValue;
      }
    }

    let json = { button: event.button, shiftKey: event.shiftKey,
                 ctrlKey: event.ctrlKey, metaKey: event.metaKey,
                 altKey: event.altKey, href: null, title: null,
                 bookmark: false, referrerPolicy: referrerPolicy };

    if (href) {
      try {
        BrowserUtils.urlSecurityCheck(href, node.ownerDocument.nodePrincipal);
      } catch (e) {
        return;
      }

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

addEventListener("DOMServiceWorkerFocusClient", function(event) {
  sendAsyncMessage("DOMServiceWorkerFocusClient", {});
}, false);

ContentWebRTC.init();
addMessageListener("webrtc:Allow", ContentWebRTC);
addMessageListener("webrtc:Deny", ContentWebRTC);
addMessageListener("webrtc:StopSharing", ContentWebRTC);

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

function disableSetDesktopBackground(aTarget) {
  // Disable the Set as Desktop Background menu item if we're still trying
  // to load the image or the load failed.
  if (!(aTarget instanceof Ci.nsIImageLoadingContent))
    return true;

  if (("complete" in aTarget) && !aTarget.complete)
    return true;

  if (aTarget.currentURI.schemeIs("javascript"))
    return true;

  let request = aTarget.QueryInterface(Ci.nsIImageLoadingContent)
                       .getRequest(Ci.nsIImageLoadingContent.CURRENT_REQUEST);
  if (!request)
    return true;

  return false;
}

addMessageListener("ContextMenu:SetAsDesktopBackground", (message) => {
  let target = message.objects.target;

  // Paranoia: check disableSetDesktopBackground again, in case the
  // image changed since the context menu was initiated.
  let disable = disableSetDesktopBackground(target);

  if (!disable) {
    try {
      BrowserUtils.urlSecurityCheck(target.currentURI.spec, target.ownerDocument.nodePrincipal);
      let canvas = content.document.createElement("canvas");
      canvas.width = target.naturalWidth;
      canvas.height = target.naturalHeight;
      let ctx = canvas.getContext("2d");
      ctx.drawImage(target, 0, 0);
      let dataUrl = canvas.toDataURL();
      sendAsyncMessage("ContextMenu:SetAsDesktopBackground:Result",
                       { dataUrl });
    }
    catch (e) {
      Cu.reportError(e);
      disable = true;
    }
  }

  if (disable)
    sendAsyncMessage("ContextMenu:SetAsDesktopBackground:Result", { disable });
});

let pageInfoListener = {

  init: function(chromeGlobal) {
    chromeGlobal.addMessageListener("PageInfo:getData", this, false, true);
  },

  receiveMessage: function(message) {
    this.imageViewRows = [];
    this.frameList = [];
    this.strings = message.data.strings;

    let frameOuterWindowID = message.data.frameOuterWindowID;

    // If inside frame then get the frame's window and document.
    if (frameOuterWindowID) {
      this.window = Services.wm.getOuterWindowWithId(frameOuterWindowID);
      this.document = this.window.document;
    }
    else {
      this.document = content.document;
      this.window = content.window;
    }

    let pageInfoData = {metaViewRows: this.getMetaInfo(), docInfo: this.getDocumentInfo(),
                        feeds: this.getFeedsInfo(), windowInfo: this.getWindowInfo()};
    sendAsyncMessage("PageInfo:data", pageInfoData);

    // Separate step so page info dialog isn't blank while waiting for this to finish.
    this.getMediaInfo();

    // Send the message after all the media elements have been walked through.
    let pageInfoMediaData = {imageViewRows: this.imageViewRows};

    this.imageViewRows = null;
    this.frameList = null;
    this.strings = null;
    this.window = null;
    this.document = null;

    sendAsyncMessage("PageInfo:mediaData", pageInfoMediaData);
  },

  getMetaInfo: function() {
    let metaViewRows = [];

    // Get the meta tags from the page.
    let metaNodes = this.document.getElementsByTagName("meta");

    for (let metaNode of metaNodes) {
      metaViewRows.push([metaNode.name || metaNode.httpEquiv || metaNode.getAttribute("property"),
                        metaNode.content]);
    }

    return metaViewRows;
  },

  getWindowInfo: function() {
    let windowInfo = {};
    windowInfo.isTopWindow = this.window == this.window.top;

    let hostName = null;
    try {
      hostName = this.window.location.host;
    }
    catch (exception) { }

    windowInfo.hostName = hostName;
    return windowInfo;
  },

  getDocumentInfo: function() {
    let docInfo = {};
    docInfo.title = this.document.title;
    docInfo.location = this.document.location.toString();
    docInfo.referrer = this.document.referrer;
    docInfo.compatMode = this.document.compatMode;
    docInfo.contentType = this.document.contentType;
    docInfo.characterSet = this.document.characterSet;
    docInfo.lastModified = this.document.lastModified;

    let documentURIObject = {};
    documentURIObject.spec = this.document.documentURIObject.spec;
    documentURIObject.originCharset = this.document.documentURIObject.originCharset;
    docInfo.documentURIObject = documentURIObject;

    docInfo.isContentWindowPrivate = PrivateBrowsingUtils.isContentWindowPrivate(content);

    return docInfo;
  },

  getFeedsInfo: function() {
    let feeds = [];
    // Get the feeds from the page.
    let linkNodes = this.document.getElementsByTagName("link");
    let length = linkNodes.length;
    for (let i = 0; i < length; i++) {
      let link = linkNodes[i];
      if (!link.href) {
        continue;
      }
      let rel = link.rel && link.rel.toLowerCase();
      let rels = {};

      if (rel) {
        for each (let relVal in rel.split(/\s+/)) {
          rels[relVal] = true;
        }
      }

      if (rels.feed || (link.type && rels.alternate && !rels.stylesheet)) {
        let type = Feeds.isValidFeed(link, this.document.nodePrincipal, "feed" in rels);
        if (type) {
          type = this.strings[type] || this.strings["application/rss+xml"];
          feeds.push([link.title, type, link.href]);
        }
      }
    }
    return feeds;
  },

  // Only called once to get the media tab's media elements from the content page.
  // The actual work is done with a TreeWalker that calls doGrab() once for
  // each element node in the document.
  getMediaInfo: function()
  {
    this.goThroughFrames(this.document, this.window);
    this.processFrames();
  },

  goThroughFrames: function(aDocument, aWindow)
  {
    this.frameList.push(aDocument);
    if (aWindow && aWindow.frames.length > 0) {
      let num = aWindow.frames.length;
      for (let i = 0; i < num; i++) {
        this.goThroughFrames(aWindow.frames[i].document, aWindow.frames[i]);  // recurse through the frames
      }
    }
  },

  processFrames: function()
  {
    if (this.frameList.length) {
      let doc = this.frameList[0];
      let iterator = doc.createTreeWalker(doc, content.NodeFilter.SHOW_ELEMENT, elem => this.grabAll(elem));
      this.frameList.shift();
      this.doGrab(iterator);
    }
  },

  /**
   * This function's previous purpose in pageInfo.js was to get loop through 500 elements at a time.
   * The iterator filter will filter for media elements.
   * #TODO Bug 1175794: refactor pageInfo.js to receive a media element at a time
   * from messages and continually update UI.
   */
  doGrab: function(iterator)
  {
    while (true)
    {
      if (!iterator.nextNode()) {
        this.processFrames();
        return;
      }
    }
  },

  grabAll: function(elem)
  {
    // Check for images defined in CSS (e.g. background, borders), any node may have multiple.
    let computedStyle = elem.ownerDocument.defaultView.getComputedStyle(elem, "");

    let addImage = (url, type, alt, elem, isBg) => {
      let element = this.serializeElementInfo(url, type, alt, elem, isBg);
      this.imageViewRows.push([url, type, alt, element, isBg]);
    };

    if (computedStyle) {
      let addImgFunc = (label, val) => {
        if (val.primitiveType == content.CSSPrimitiveValue.CSS_URI) {
          addImage(val.getStringValue(), label, this.strings.notSet, elem, true);
        }
        else if (val.primitiveType == content.CSSPrimitiveValue.CSS_STRING) {
          // This is for -moz-image-rect.
          // TODO: Reimplement once bug 714757 is fixed.
          let strVal = val.getStringValue();
          if (strVal.search(/^.*url\(\"?/) > -1) {
            let url = strVal.replace(/^.*url\(\"?/,"").replace(/\"?\).*$/,"");
            addImage(url, label, this.strings.notSet, elem, true);
          }
        }
        else if (val.cssValueType == content.CSSValue.CSS_VALUE_LIST) {
          // Recursively resolve multiple nested CSS value lists.
          for (let i = 0; i < val.length; i++) {
            addImgFunc(label, val.item(i));
          }
        }
      };

      addImgFunc(this.strings.mediaBGImg, computedStyle.getPropertyCSSValue("background-image"));
      addImgFunc(this.strings.mediaBorderImg, computedStyle.getPropertyCSSValue("border-image-source"));
      addImgFunc(this.strings.mediaListImg, computedStyle.getPropertyCSSValue("list-style-image"));
      addImgFunc(this.strings.mediaCursor, computedStyle.getPropertyCSSValue("cursor"));
    }

    // One swi^H^H^Hif-else to rule them all.
    if (elem instanceof content.HTMLImageElement) {
      addImage(elem.src, this.strings.mediaImg,
               (elem.hasAttribute("alt")) ? elem.alt : this.strings.notSet, elem, false);
    }
    else if (elem instanceof content.SVGImageElement) {
      try {
        // Note: makeURLAbsolute will throw if either the baseURI is not a valid URI
        //       or the URI formed from the baseURI and the URL is not a valid URI.
        let href = makeURLAbsolute(elem.baseURI, elem.href.baseVal);
        addImage(href, this.strings.mediaImg, "", elem, false);
      } catch (e) { }
    }
    else if (elem instanceof content.HTMLVideoElement) {
      addImage(elem.currentSrc, this.strings.mediaVideo, "", elem, false);
    }
    else if (elem instanceof content.HTMLAudioElement) {
      addImage(elem.currentSrc, this.strings.mediaAudio, "", elem, false);
    }
    else if (elem instanceof content.HTMLLinkElement) {
      if (elem.rel && /\bicon\b/i.test(elem.rel)) {
        addImage(elem.href, this.strings.mediaLink, "", elem, false);
      }
    }
    else if (elem instanceof content.HTMLInputElement || elem instanceof content.HTMLButtonElement) {
      if (elem.type.toLowerCase() == "image") {
        addImage(elem.src, this.strings.mediaInput,
                 (elem.hasAttribute("alt")) ? elem.alt : this.strings.notSet, elem, false);
      }
    }
    else if (elem instanceof content.HTMLObjectElement) {
      addImage(elem.data, this.strings.mediaObject, this.getValueText(elem), elem, false);
    }
    else if (elem instanceof content.HTMLEmbedElement) {
      addImage(elem.src, this.strings.mediaEmbed, "", elem, false);
    }

    return content.NodeFilter.FILTER_ACCEPT;
  },

  /**
   * Set up a JSON element object with all the instanceOf and other infomation that
   * makePreview in pageInfo.js uses to figure out how to display the preview.
   */

  serializeElementInfo: function(url, type, alt, item, isBG)
  {
    // Interface for image loading content.
    const nsIImageLoadingContent = Components.interfaces.nsIImageLoadingContent;

    let result = {};

    let imageText;
    if (!isBG &&
        !(item instanceof content.SVGImageElement) &&
        !(this.document instanceof content.ImageDocument)) {
      imageText = item.title || item.alt;

      if (!imageText && !(item instanceof content.HTMLImageElement)) {
        imageText = this.getValueText(item);
      }
    }

    result.imageText = imageText;
    result.longDesc = item.longDesc;
    result.numFrames = 1;

    if (item instanceof content.HTMLObjectElement ||
      item instanceof content.HTMLEmbedElement ||
      item instanceof content.HTMLLinkElement) {
      result.mimeType = item.type;
    }

    if (!result.mimeType && !isBG && item instanceof nsIImageLoadingContent) {
      // Interface for image loading content.
      const nsIImageLoadingContent = Components.interfaces.nsIImageLoadingContent;
      let imageRequest = item.getRequest(nsIImageLoadingContent.CURRENT_REQUEST);
      if (imageRequest) {
        result.mimeType = imageRequest.mimeType;
        let image = !(imageRequest.imageStatus & imageRequest.STATUS_ERROR) && imageRequest.image;
        if (image) {
          result.numFrames = image.numFrames;
        }
      }
    }

    // if we have a data url, get the MIME type from the url
    if (!result.mimeType && url.startsWith("data:")) {
      let dataMimeType = /^data:(image\/[^;,]+)/i.exec(url);
      if (dataMimeType)
        result.mimeType = dataMimeType[1].toLowerCase();
    }

    result.HTMLLinkElement = item instanceof content.HTMLLinkElement;
    result.HTMLInputElement = item instanceof content.HTMLInputElement;
    result.HTMLImageElement = item instanceof content.HTMLImageElement;
    result.HTMLObjectElement = item instanceof content.HTMLObjectElement;
    result.SVGImageElement = item instanceof content.SVGImageElement;
    result.HTMLVideoElement = item instanceof content.HTMLVideoElement;
    result.HTMLAudioElement = item instanceof content.HTMLAudioElement;

    if (!isBG) {
      result.width = item.width;
      result.height = item.height;
    }

    if (item instanceof content.SVGImageElement) {
      result.SVGImageElementWidth = item.width.baseVal.value;
      result.SVGImageElementHeight = item.height.baseVal.value;
    }

    result.baseURI = item.baseURI;

    return result;
  },

  //******** Other Misc Stuff
  // Modified from the Links Panel v2.3, http://segment7.net/mozilla/links/links.html
  // parse a node to extract the contents of the node
  getValueText: function(node)
  {

    let valueText = "";

    // Form input elements don't generally contain information that is useful to our callers, so return nothing.
    if (node instanceof content.HTMLInputElement ||
        node instanceof content.HTMLSelectElement ||
        node instanceof content.HTMLTextAreaElement) {
      return valueText;
    }

    // Otherwise recurse for each child.
    let length = node.childNodes.length;

    for (let i = 0; i < length; i++) {
      let childNode = node.childNodes[i];
      let nodeType = childNode.nodeType;

      // Text nodes are where the goods are.
      if (nodeType == content.Node.TEXT_NODE) {
        valueText += " " + childNode.nodeValue;
      }
      // And elements can have more text inside them.
      else if (nodeType == content.Node.ELEMENT_NODE) {
        // Images are special, we want to capture the alt text as if the image weren't there.
        if (childNode instanceof content.HTMLImageElement) {
          valueText += " " + this.getAltText(childNode);
        }
        else {
          valueText += " " + this.getValueText(childNode);
        }
      }
    }

    return this.stripWS(valueText);
  },

  // Copied from the Links Panel v2.3, http://segment7.net/mozilla/links/links.html.
  // Traverse the tree in search of an img or area element and grab its alt tag.
  getAltText: function(node)
  {
    let altText = "";

    if (node.alt) {
      return node.alt;
    }
    let length = node.childNodes.length;
    for (let i = 0; i < length; i++) {
      if ((altText = this.getAltText(node.childNodes[i]) != undefined)) { // stupid js warning...
        return altText;
      }
    }
    return "";
  },

  // Copied from the Links Panel v2.3, http://segment7.net/mozilla/links/links.html.
  // Strip leading and trailing whitespace, and replace multiple consecutive whitespace characters with a single space.
  stripWS: function(text)
  {
    let middleRE = /\s+/g;
    let endRE = /(^\s+)|(\s+$)/g;

    text = text.replace(middleRE, " ");
    return text.replace(endRE, "");
  }
};
pageInfoListener.init(this);
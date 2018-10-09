# -*- Mode: javascript; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

const kPrefSessionPersistMinutes = "plugin.sessionPermissionNow.intervalInMinutes";
const kPrefPersistentDays = "plugin.persistentPermissionAlways.intervalInDays";

var gPluginHandler = {
  PLUGIN_SCRIPTED_STATE_NONE: 0,
  PLUGIN_SCRIPTED_STATE_FIRED: 1,
  PLUGIN_SCRIPTED_STATE_DONE: 2,

  getPluginUI: function (plugin, anonid) {
    return plugin.ownerDocument.
           getAnonymousElementByAttribute(plugin, "anonid", anonid);
  },

  _getPluginInfo: function (pluginElement) {
    let pluginHost = Cc["@mozilla.org/plugin/host;1"].getService(Ci.nsIPluginHost);
    pluginElement.QueryInterface(Ci.nsIObjectLoadingContent);

    let tagMimetype;
    let pluginName = gNavigatorBundle.getString("pluginInfo.unknownPlugin");
    let pluginTag = null;
    let permissionString = null;
    let fallbackType = null;
    let blocklistState = null;

    if (pluginElement instanceof HTMLAppletElement) {
      tagMimetype = "application/x-java-vm";
    } else {
      tagMimetype = pluginElement.actualType;

      if (tagMimetype == "") {
        tagMimetype = pluginElement.type;
      }
    }

    if (gPluginHandler.isKnownPlugin(pluginElement)) {
      pluginTag = pluginHost.getPluginTagForType(pluginElement.actualType);
      pluginName = gPluginHandler.makeNicePluginName(pluginTag.name);

      permissionString = pluginHost.getPermissionStringForType(pluginElement.actualType);
      fallbackType = pluginElement.defaultFallbackType;
      blocklistState = pluginHost.getBlocklistStateForType(pluginElement.actualType);
      // Make state-softblocked == state-notblocked for our purposes,
      // they have the same UI. STATE_OUTDATED should not exist for plugin
      // items, but let's alias it anyway, just in case.
      if (blocklistState == Ci.nsIBlocklistService.STATE_SOFTBLOCKED ||
          blocklistState == Ci.nsIBlocklistService.STATE_OUTDATED) {
        blocklistState = Ci.nsIBlocklistService.STATE_NOT_BLOCKED;
      }
    }

    return { mimetype: tagMimetype,
             pluginName: pluginName,
             pluginTag: pluginTag,
             permissionString: permissionString,
             fallbackType: fallbackType,
             blocklistState: blocklistState,
           };
  },

  // Map the plugin's name to a filtered version more suitable for user UI.
  makeNicePluginName : function (aName) {
    if (aName == "Shockwave Flash")
      return "Adobe Flash";

    // Clean up the plugin name by stripping off any trailing version numbers
    // or "plugin". EG, "Foo Bar Plugin 1.23_02" --> "Foo Bar"
    // Do this by first stripping the numbers, etc. off the end, and then
    // removing "Plugin" (and then trimming to get rid of any whitespace).
    // (Otherwise, something like "Java(TM) Plug-in 1.7.0_07" gets mangled)
    let newName = aName.replace(/[\s\d\.\-\_\(\)]+$/, "").replace(/\bplug-?in\b/i, "").trim();
    return newName;
  },

  isTooSmall : function (plugin, overlay) {
    // Is the <object>'s size too small to hold what we want to show?
    let pluginRect = plugin.getBoundingClientRect();
    // XXX bug 446693. The text-shadow on the submitted-report text at
    //     the bottom causes scrollHeight to be larger than it should be.
    // Clamp width/height to properly show CTP overlay on different
    // zoom levels when embedded in iframes (rounding bug). (Bug 972237)
    let overflows = (overlay.scrollWidth > Math.ceil(pluginRect.width)) ||
                    (overlay.scrollHeight - 5 > Math.ceil(pluginRect.height));
    return overflows;
  },

  addLinkClickCallback: function (linkNode, callbackName /*callbackArgs...*/) {
    // XXX just doing (callback)(arg) was giving a same-origin error. bug?
    let self = this;
    let callbackArgs = Array.prototype.slice.call(arguments).slice(2);
    linkNode.addEventListener("click",
                              function(evt) {
                                if (!evt.isTrusted)
                                  return;
                                evt.preventDefault();
                                if (callbackArgs.length == 0)
                                  callbackArgs = [ evt ];
                                (self[callbackName]).apply(self, callbackArgs);
                              },
                              true);

    linkNode.addEventListener("keydown",
                              function(evt) {
                                if (!evt.isTrusted)
                                  return;
                                if (evt.keyCode == evt.DOM_VK_RETURN) {
                                  evt.preventDefault();
                                  if (callbackArgs.length == 0)
                                    callbackArgs = [ evt ];
                                  evt.preventDefault();
                                  (self[callbackName]).apply(self, callbackArgs);
                                }
                              },
                              true);
  },

  // Helper to get the binding handler type from a plugin object
  _getBindingType : function(plugin) {
    if (!(plugin instanceof Ci.nsIObjectLoadingContent))
      return null;

    switch (plugin.pluginFallbackType) {
      case Ci.nsIObjectLoadingContent.PLUGIN_UNSUPPORTED:
        return "PluginNotFound";
      case Ci.nsIObjectLoadingContent.PLUGIN_DISABLED:
        return "PluginDisabled";
      case Ci.nsIObjectLoadingContent.PLUGIN_BLOCKLISTED:
        return "PluginBlocklisted";
      case Ci.nsIObjectLoadingContent.PLUGIN_OUTDATED:
        return "PluginOutdated";
      case Ci.nsIObjectLoadingContent.PLUGIN_CLICK_TO_PLAY:
        return "PluginClickToPlay";
      case Ci.nsIObjectLoadingContent.PLUGIN_VULNERABLE_UPDATABLE:
        return "PluginVulnerableUpdatable";
      case Ci.nsIObjectLoadingContent.PLUGIN_VULNERABLE_NO_UPDATE:
        return "PluginVulnerableNoUpdate";
      case Ci.nsIObjectLoadingContent.PLUGIN_PLAY_PREVIEW:
        return "PluginPlayPreview";
      default:
        // Not all states map to a handler
        return null;
    }
  },

  handleEvent : function(event) {
    let plugin;
    let doc;

    let eventType = event.type;
    if (eventType === "PluginRemoved") {
      doc = event.target;
    }
    else {
      plugin = event.target;
      doc = plugin.ownerDocument;

      if (!(plugin instanceof Ci.nsIObjectLoadingContent))
        return;
    }

    if (eventType == "PluginBindingAttached") {
      // The plugin binding fires this event when it is created.
      // As an untrusted event, ensure that this object actually has a binding
      // and make sure we don't handle it twice
      let overlay = this.getPluginUI(plugin, "main");
      if (!overlay || overlay._bindingHandled) {
        return;
      }
      overlay._bindingHandled = true;

      // Lookup the handler for this binding
      eventType = this._getBindingType(plugin);
      if (!eventType) {
        // Not all bindings have handlers
        return;
      }
    }

    let shouldShowNotification = false;
    let browser = gBrowser.getBrowserForDocument(doc.defaultView.top.document);
    if (!browser)
      return;

    switch (eventType) {
      case "PluginCrashed":
        this.pluginInstanceCrashed(plugin, event);
        break;

      case "PluginNotFound":
        /* No action (plugin finder obsolete) */
        break;

      case "PluginBlocklisted":
      case "PluginOutdated":
        shouldShowNotification = true;
        break;

      case "PluginVulnerableUpdatable":
        let updateLink = this.getPluginUI(plugin, "checkForUpdatesLink");
        this.addLinkClickCallback(updateLink, "openPluginUpdatePage");
        /* FALLTHRU */

      case "PluginVulnerableNoUpdate":
      case "PluginClickToPlay":
        this._handleClickToPlayEvent(plugin);
        let overlay = this.getPluginUI(plugin, "main");
        let pluginName = this._getPluginInfo(plugin).pluginName;
        let messageString = gNavigatorBundle.getFormattedString("PluginClickToActivate", [pluginName]);
        let overlayText = this.getPluginUI(plugin, "clickToPlay");
        overlayText.textContent = messageString;
        if (eventType == "PluginVulnerableUpdatable" ||
            eventType == "PluginVulnerableNoUpdate") {
          let vulnerabilityString = gNavigatorBundle.getString(eventType);
          let vulnerabilityText = this.getPluginUI(plugin, "vulnerabilityStatus");
          vulnerabilityText.textContent = vulnerabilityString;
        }
        shouldShowNotification = true;
        break;

      case "PluginPlayPreview":
        this._handlePlayPreviewEvent(plugin);
        break;

      case "PluginDisabled":
        let manageLink = this.getPluginUI(plugin, "managePluginsLink");
        this.addLinkClickCallback(manageLink, "managePlugins");
        shouldShowNotification = true;
        break;

      case "PluginInstantiated":
        //Pale Moon: don't show the indicator when plugins are enabled/allowed
        if (gPrefService.getBoolPref("plugins.always_show_indicator")) {
          shouldShowNotification = true;
        }
        break;
      case "PluginRemoved":
        shouldShowNotification = true;
        break;
    }

    // Show the in-content UI if it's not too big. The crashed plugin handler already did this.
    if (eventType != "PluginCrashed" && eventType != "PluginRemoved") {
      let overlay = this.getPluginUI(plugin, "main");
      if (overlay != null) {
        if (!this.isTooSmall(plugin, overlay)) {
          overlay.style.visibility = "visible";
        }
        plugin.addEventListener("overflow", function(event) {
          overlay.style.visibility = "hidden";
        });
        plugin.addEventListener("underflow", function(event) {
          // this is triggered if only one dimension underflows,
          // the other dimension might still overflow
          if (!gPluginHandler.isTooSmall(plugin, overlay)) {
            overlay.style.visibility = "visible";
          }
        });
      }
    }

    // Only show the notification after we've done the isTooSmall check, so
    // that the notification can decide whether to show the "alert" icon
    if (shouldShowNotification) {
      this._showClickToPlayNotification(browser);
    }
  },

  isKnownPlugin: function PH_isKnownPlugin(objLoadingContent) {
    return (objLoadingContent.getContentTypeForMIMEType(objLoadingContent.actualType) ==
            Ci.nsIObjectLoadingContent.TYPE_PLUGIN);
  },

  canActivatePlugin: function PH_canActivatePlugin(objLoadingContent) {
    // if this isn't a known plugin, we can't activate it
    // (this also guards pluginHost.getPermissionStringForType against
    // unexpected input)
    if (!gPluginHandler.isKnownPlugin(objLoadingContent))
      return false;

    let pluginHost = Cc["@mozilla.org/plugin/host;1"].getService(Ci.nsIPluginHost);
    let permissionString = pluginHost.getPermissionStringForType(objLoadingContent.actualType);
    let principal = objLoadingContent.ownerDocument.defaultView.top.document.nodePrincipal;
    let pluginPermission = Services.perms.testPermissionFromPrincipal(principal, permissionString);

    let isFallbackTypeValid =
      objLoadingContent.pluginFallbackType >= Ci.nsIObjectLoadingContent.PLUGIN_CLICK_TO_PLAY &&
      objLoadingContent.pluginFallbackType <= Ci.nsIObjectLoadingContent.PLUGIN_VULNERABLE_NO_UPDATE;

    if (objLoadingContent.pluginFallbackType == Ci.nsIObjectLoadingContent.PLUGIN_PLAY_PREVIEW) {
      // checking if play preview is subject to CTP rules
      let playPreviewInfo = pluginHost.getPlayPreviewInfo(objLoadingContent.actualType);
      isFallbackTypeValid = !playPreviewInfo.ignoreCTP;
    }

    return !objLoadingContent.activated &&
           pluginPermission != Ci.nsIPermissionManager.DENY_ACTION &&
           isFallbackTypeValid;
  },

  hideClickToPlayOverlay: function(aPlugin) {
    let overlay = this.getPluginUI(aPlugin, "main");
    if (overlay)
      overlay.style.visibility = "hidden";
  },

  stopPlayPreview: function PH_stopPlayPreview(aPlugin, aPlayPlugin) {
    let objLoadingContent = aPlugin.QueryInterface(Ci.nsIObjectLoadingContent);
    if (objLoadingContent.activated)
      return;

    if (aPlayPlugin)
      objLoadingContent.playPlugin();
    else
      objLoadingContent.cancelPlayPreview();
  },

  // Callback for user clicking on a disabled plugin
  managePlugins: function (aEvent) {
    BrowserOpenAddonsMgr("addons://list/plugin");
  },

  // Callback for user clicking on the link in a click-to-play plugin
  // (where the plugin has an update)
  openPluginUpdatePage: function (aEvent) {
    openURL(Services.urlFormatter.formatURLPref("plugins.update.url"));
  },

  // Callback for user clicking a "reload page" link
  reloadPage: function (browser) {
    browser.reload();
  },

  // Callback for user clicking the help icon
  openHelpPage: function () {
    openHelpLink("plugin-crashed", false);
  },

  // Event listener for click-to-play plugins.
  _handleClickToPlayEvent: function PH_handleClickToPlayEvent(aPlugin) {
    let doc = aPlugin.ownerDocument;
    let browser = gBrowser.getBrowserForDocument(doc.defaultView.top.document);
    let pluginHost = Cc["@mozilla.org/plugin/host;1"].getService(Ci.nsIPluginHost);
    let objLoadingContent = aPlugin.QueryInterface(Ci.nsIObjectLoadingContent);
    // guard against giving pluginHost.getPermissionStringForType a type
    // not associated with any known plugin
    if (!gPluginHandler.isKnownPlugin(objLoadingContent))
      return;
    let permissionString = pluginHost.getPermissionStringForType(objLoadingContent.actualType);
    let principal = doc.defaultView.top.document.nodePrincipal;
    let pluginPermission = Services.perms.testPermissionFromPrincipal(principal, permissionString);

    let overlay = this.getPluginUI(aPlugin, "main");

    if (pluginPermission == Ci.nsIPermissionManager.DENY_ACTION) {
      if (overlay)
        overlay.style.visibility = "hidden";
      return;
    }

    if (overlay) {
      overlay.addEventListener("click", gPluginHandler._overlayClickListener, true);
      let closeIcon = gPluginHandler.getPluginUI(aPlugin, "closeIcon");
      closeIcon.addEventListener("click", function(aEvent) {
        if (aEvent.button == 0 && aEvent.isTrusted)
          gPluginHandler.hideClickToPlayOverlay(aPlugin);
      }, true);
    }
  },

  _overlayClickListener: {
    handleEvent: function PH_handleOverlayClick(aEvent) {
      let plugin = document.getBindingParent(aEvent.target);
      let contentWindow = plugin.ownerDocument.defaultView.top;
      // gBrowser.getBrowserForDocument does not exist in the case where we
      // drag-and-dropped a tab from a window containing only that tab. In
      // that case, the window gets destroyed.
      let browser = gBrowser.getBrowserForDocument ?
                      gBrowser.getBrowserForDocument(contentWindow.document) :
                      null;
      // If browser is null here, we've been drag-and-dropped from another
      // window, and this is the wrong click handler.
      if (!browser) {
        aEvent.target.removeEventListener("click", gPluginHandler._overlayClickListener, true);
        return;
      }
      let objLoadingContent = plugin.QueryInterface(Ci.nsIObjectLoadingContent);
      // Have to check that the target is not the link to update the plugin
      if (!(aEvent.originalTarget instanceof HTMLAnchorElement) &&
          (aEvent.originalTarget.getAttribute('anonid') != 'closeIcon') &&
          aEvent.button == 0 && aEvent.isTrusted) {
        gPluginHandler._showClickToPlayNotification(browser, plugin);
        aEvent.stopPropagation();
        aEvent.preventDefault();
      }
    }
  },

  _handlePlayPreviewEvent: function PH_handlePlayPreviewEvent(aPlugin) {
    let doc = aPlugin.ownerDocument;
    let browser = gBrowser.getBrowserForDocument(doc.defaultView.top.document);
    let pluginHost = Cc["@mozilla.org/plugin/host;1"].getService(Ci.nsIPluginHost);
    let pluginInfo = this._getPluginInfo(aPlugin);
    let playPreviewInfo = pluginHost.getPlayPreviewInfo(pluginInfo.mimetype);

    let previewContent = this.getPluginUI(aPlugin, "previewPluginContent");
    let iframe = previewContent.getElementsByClassName("previewPluginContentFrame")[0];
    if (!iframe) {
      // lazy initialization of the iframe
      iframe = doc.createElementNS("http://www.w3.org/1999/xhtml", "iframe");
      iframe.className = "previewPluginContentFrame";
      previewContent.appendChild(iframe);

      // Force a style flush, so that we ensure our binding is attached.
      aPlugin.clientTop;
    }
    iframe.src = playPreviewInfo.redirectURL;

    // MozPlayPlugin event can be dispatched from the extension chrome
    // code to replace the preview content with the native plugin
    previewContent.addEventListener("MozPlayPlugin", function playPluginHandler(aEvent) {
      if (!aEvent.isTrusted)
        return;

      previewContent.removeEventListener("MozPlayPlugin", playPluginHandler, true);

      let playPlugin = !aEvent.detail;
      gPluginHandler.stopPlayPreview(aPlugin, playPlugin);

      // cleaning up: removes overlay iframe from the DOM
      let iframe = previewContent.getElementsByClassName("previewPluginContentFrame")[0];
      if (iframe)
        previewContent.removeChild(iframe);
    }, true);

    if (!playPreviewInfo.ignoreCTP) {
      gPluginHandler._showClickToPlayNotification(browser);
    }
  },

  reshowClickToPlayNotification: function PH_reshowClickToPlayNotification() {
    let browser = gBrowser.selectedBrowser;
    let contentWindow = browser.contentWindow;
    let cwu = contentWindow.QueryInterface(Ci.nsIInterfaceRequestor)
                           .getInterface(Ci.nsIDOMWindowUtils);
    let doc = contentWindow.document;
    let plugins = cwu.plugins;
    for (let plugin of plugins) {
      let overlay = doc.getAnonymousElementByAttribute(plugin, "anonid", "main");
      if (overlay)
        overlay.removeEventListener("click", gPluginHandler._overlayClickListener, true);
      let objLoadingContent = plugin.QueryInterface(Ci.nsIObjectLoadingContent);
      if (gPluginHandler.canActivatePlugin(objLoadingContent))
        gPluginHandler._handleClickToPlayEvent(plugin);
    }
    gPluginHandler._showClickToPlayNotification(browser);
  },

  _clickToPlayNotificationEventCallback: function PH_ctpEventCallback(event) {
    if (event == "showing") {
      gPluginHandler._makeCenterActions(this);
    }
    else if (event == "dismissed") {
      // Once the popup is dismissed, clicking the icon should show the full
      // list again
      this.options.primaryPlugin = null;
    }
  },

  // Match the behaviour of nsPermissionManager
  _getHostFromPrincipal: function PH_getHostFromPrincipal(principal) {
    if (!principal.URI || principal.URI.schemeIs("moz-nullprincipal")) {
      return "(null)";
    }

    try {
      if (principal.URI.host)
        return principal.URI.host;
    } catch (e) {}

    return principal.origin;
  },

  _makeCenterActions: function PH_makeCenterActions(notification) {
    let contentWindow = notification.browser.contentWindow;
    let cwu = contentWindow.QueryInterface(Ci.nsIInterfaceRequestor)
                           .getInterface(Ci.nsIDOMWindowUtils);

    let principal = contentWindow.document.nodePrincipal;
    // This matches the behavior of nsPermssionManager, used for display purposes only
    let principalHost = this._getHostFromPrincipal(principal);

    let centerActions = [];
    let pluginsFound = new Set();
    for (let plugin of cwu.plugins) {
      plugin.QueryInterface(Ci.nsIObjectLoadingContent);
      if (plugin.getContentTypeForMIMEType(plugin.actualType) != Ci.nsIObjectLoadingContent.TYPE_PLUGIN) {
        continue;
      }

      let pluginInfo = this._getPluginInfo(plugin);
      if (pluginInfo.permissionString === null) {
        Components.utils.reportError("No permission string for active plugin.");
        continue;
      }
      if (pluginsFound.has(pluginInfo.permissionString)) {
        continue;
      }
      pluginsFound.add(pluginInfo.permissionString);

      // Add the per-site permissions and details URLs to pluginInfo here
      // because they are more expensive to compute and so we avoid it in
      // the tighter loop above.
      let permissionObj = Services.perms.
        getPermissionObject(principal, pluginInfo.permissionString, false);
      if (permissionObj) {
        pluginInfo.pluginPermissionHost = permissionObj.host;
        pluginInfo.pluginPermissionType = permissionObj.expireType;
      }
      else {
        pluginInfo.pluginPermissionHost = principalHost;
        pluginInfo.pluginPermissionType = undefined;
      }

      let url;
      // TODO: allow the blocklist to specify a better link, bug 873093
      if (pluginInfo.blocklistState == Ci.nsIBlocklistService.STATE_VULNERABLE_UPDATE_AVAILABLE) {
        url = Services.urlFormatter.formatURLPref("plugins.update.url");
      }
      else if (pluginInfo.blocklistState != Ci.nsIBlocklistService.STATE_NOT_BLOCKED) {
        url = Services.blocklist.getPluginBlocklistURL(pluginInfo.pluginTag);
      }
      pluginInfo.detailsLink = url;

      centerActions.push(pluginInfo);
    }
    centerActions.sort(function(a, b) {
      return a.pluginName.localeCompare(b.pluginName);
    });

    notification.options.centerActions = centerActions;
  },

  /**
   * Called from the plugin doorhanger to set the new permissions for a plugin
   * and activate plugins if necessary.
   * aNewState should be either "allownow" "allowalways" or "block"
   */
  _updatePluginPermission: function PH_setPermissionForPlugins(aNotification, aPluginInfo, aNewState) {
    let permission;
    let expireType;
    let expireTime;

    switch (aNewState) {
      case "allownow":
        permission = Ci.nsIPermissionManager.ALLOW_ACTION;
        expireType = Ci.nsIPermissionManager.EXPIRE_SESSION;
        expireTime = Date.now() + Services.prefs.getIntPref(kPrefSessionPersistMinutes) * 60 * 1000;
        break;

      case "allowalways":
        permission = Ci.nsIPermissionManager.ALLOW_ACTION;
        expireType = Ci.nsIPermissionManager.EXPIRE_TIME;
        expireTime = Date.now() +
          Services.prefs.getIntPref(kPrefPersistentDays) * 24 * 60 * 60 * 1000;
        break;

      case "block":
        permission = Ci.nsIPermissionManager.PROMPT_ACTION;
        expireType = Ci.nsIPermissionManager.EXPIRE_NEVER;
        expireTime = 0;
        break;

      // In case a plugin has already been allowed in another tab, the "continue allowing" button
      // shouldn't change any permissions but should run the plugin-enablement code below.
      case "continue":
        break;

      default:
        Cu.reportError(Error("Unexpected plugin state: " + aNewState));
        return;
    }

    let browser = aNotification.browser;
    let contentWindow = browser.contentWindow;
    if (aNewState != "continue") {
      let principal = contentWindow.document.nodePrincipal;
      Services.perms.addFromPrincipal(principal, aPluginInfo.permissionString,
                                      permission, expireType, expireTime);

      if (aNewState == "block") {
        return;
      }
    }

    // Manually activate the plugins that would have been automatically
    // activated.
    let cwu = contentWindow.QueryInterface(Ci.nsIInterfaceRequestor)
                           .getInterface(Ci.nsIDOMWindowUtils);
    let plugins = cwu.plugins;
    let pluginHost = Cc["@mozilla.org/plugin/host;1"].getService(Ci.nsIPluginHost);

    for (let plugin of plugins) {
      plugin.QueryInterface(Ci.nsIObjectLoadingContent);
      // canActivatePlugin will return false if this isn't a known plugin type,
      // so the pluginHost.getPermissionStringForType call is protected
      if (gPluginHandler.canActivatePlugin(plugin) &&
          aPluginInfo.permissionString == pluginHost.getPermissionStringForType(plugin.actualType)) {
        plugin.playPlugin();
      }
    }
  },

  _showClickToPlayNotification: function PH_showClickToPlayNotification(aBrowser, aPrimaryPlugin) {
    let notification = PopupNotifications.getNotification("click-to-play-plugins", aBrowser);

    let contentWindow = aBrowser.contentWindow;
    let contentDoc = aBrowser.contentDocument;
    let cwu = contentWindow.QueryInterface(Ci.nsIInterfaceRequestor)
                           .getInterface(Ci.nsIDOMWindowUtils);
    // Pale Moon: cwu.plugins may contain non-plugin <object>s, filter them out
    let plugins = cwu.plugins.filter(function(plugin) {
      return (plugin.getContentTypeForMIMEType(plugin.actualType) ==
              Ci.nsIObjectLoadingContent.TYPE_PLUGIN);
    });
    if (plugins.length == 0) {
      if (notification) {
        PopupNotifications.remove(notification);
      }
      return;
    }

    let icon = 'plugins-notification-icon';
    for (let plugin of plugins) {
      let fallbackType = plugin.pluginFallbackType;
      if (fallbackType == plugin.PLUGIN_VULNERABLE_UPDATABLE ||
          fallbackType == plugin.PLUGIN_VULNERABLE_NO_UPDATE ||
          fallbackType == plugin.PLUGIN_BLOCKLISTED) {
        icon = 'blocked-plugins-notification-icon';
        break;
      }
      if (fallbackType == plugin.PLUGIN_CLICK_TO_PLAY) {
        let overlay = contentDoc.getAnonymousElementByAttribute(plugin, "anonid", "main");
        if (!overlay || overlay.style.visibility == 'hidden') {
          icon = 'alert-plugins-notification-icon';
        }
      }
    }

    let dismissed = notification ? notification.dismissed : true;
    if (aPrimaryPlugin)
      dismissed = false;

    let primaryPluginPermission = null;
    if (aPrimaryPlugin) {
      primaryPluginPermission = this._getPluginInfo(aPrimaryPlugin).permissionString;
    }

    let options = {
      dismissed: dismissed,
      eventCallback: this._clickToPlayNotificationEventCallback,
      primaryPlugin: primaryPluginPermission
    };
    PopupNotifications.show(aBrowser, "click-to-play-plugins",
                            "", icon,
                            null, null, options);
  },

  // Crashed-plugin observer. Notified once per plugin crash, before events
  // are dispatched to individual plugin instances.
  pluginCrashed : function(subject, topic, data) {
    let propertyBag = subject;
    if (!(propertyBag instanceof Ci.nsIPropertyBag2) ||
        !(propertyBag instanceof Ci.nsIWritablePropertyBag2))
     return;
  },

  // Crashed-plugin event listener. Called for every instance of a
  // plugin in content.
  pluginInstanceCrashed: function (plugin, aEvent) {
    // Ensure the plugin and event are of the right type.
    if (!(aEvent instanceof Ci.nsIDOMDataContainerEvent))
      return;

    let submittedReport = aEvent.getData("submittedCrashReport");
    let doPrompt        = true; // XXX followup for .getData("doPrompt");
    let submitReports   = true; // XXX followup for .getData("submitReports");
    let pluginName      = aEvent.getData("pluginName");
    let pluginDumpID    = aEvent.getData("pluginDumpID");
    let browserDumpID   = aEvent.getData("browserDumpID");

    // Remap the plugin name to a more user-presentable form.
    pluginName = this.makeNicePluginName(pluginName);

    let messageString = gNavigatorBundle.getFormattedString("crashedpluginsMessage.title", [pluginName]);

    //
    // Configure the crashed-plugin placeholder.
    //

    // Force a layout flush so the binding is attached.
    plugin.clientTop;
    let doc = plugin.ownerDocument;
    let overlay = doc.getAnonymousElementByAttribute(plugin, "class", "mainBox");
    let statusDiv = doc.getAnonymousElementByAttribute(plugin, "class", "submitStatus");

    let crashText = doc.getAnonymousElementByAttribute(plugin, "class", "msgCrashedText");
    crashText.textContent = messageString;

    let browser = gBrowser.getBrowserForDocument(doc.defaultView.top.document);

    let link = doc.getAnonymousElementByAttribute(plugin, "class", "reloadLink");
    this.addLinkClickCallback(link, "reloadPage", browser);

    let notificationBox = gBrowser.getNotificationBox(browser);

    let isShowing = true;

    // Is the <object>'s size too small to hold what we want to show?
    if (this.isTooSmall(plugin, overlay)) {
      // First try hiding the crash report submission UI.
      statusDiv.removeAttribute("status");

      if (this.isTooSmall(plugin, overlay)) {
        // Hide the overlay's contents. Use visibility style, so that it doesn't
        // collapse down to 0x0.
        overlay.style.visibility = "hidden";
        isShowing = false;
      }
    }

    if (isShowing) {
      // If a previous plugin on the page was too small and resulted in adding a
      // notification bar, then remove it because this plugin instance it big
      // enough to serve as in-content notification.
      hideNotificationBar();
      doc.mozNoPluginCrashedNotification = true;
    } else {
      // If another plugin on the page was large enough to show our UI, we don't
      // want to show a notification bar.
      if (!doc.mozNoPluginCrashedNotification)
        showNotificationBar(pluginDumpID, browserDumpID);
    }

    function hideNotificationBar() {
      let notification = notificationBox.getNotificationWithValue("plugin-crashed");
      if (notification)
        notificationBox.removeNotification(notification, true);
    }

    function showNotificationBar(pluginDumpID, browserDumpID) {
      // If there's already an existing notification bar, don't do anything.
      let notification = notificationBox.getNotificationWithValue("plugin-crashed");
      if (notification)
        return;

      // Configure the notification bar
      let priority = notificationBox.PRIORITY_WARNING_MEDIUM;
      let iconURL = "chrome://mozapps/skin/plugins/notifyPluginCrashed.png";
      let reloadLabel = gNavigatorBundle.getString("crashedpluginsMessage.reloadButton.label");
      let reloadKey   = gNavigatorBundle.getString("crashedpluginsMessage.reloadButton.accesskey");
      let submitLabel = gNavigatorBundle.getString("crashedpluginsMessage.submitButton.label");
      let submitKey   = gNavigatorBundle.getString("crashedpluginsMessage.submitButton.accesskey");

      let buttons = [{
        label: reloadLabel,
        accessKey: reloadKey,
        popup: null,
        callback: function() { browser.reload(); },
      }];

      notification = notificationBox.appendNotification(messageString, "plugin-crashed",
                                                            iconURL, priority, buttons);

      // Add the "learn more" link.
      let XULNS = "http://www.mozilla.org/keymaster/gatekeeper/there.is.only.xul";
      let link = notification.ownerDocument.createElementNS(XULNS, "label");
      link.className = "text-link";
      link.setAttribute("value", gNavigatorBundle.getString("crashedpluginsMessage.learnMore"));
      let crashurl = formatURL("app.support.baseURL", true);
      crashurl += "plugin-crashed-notificationbar";
      link.href = crashurl;

      let description = notification.ownerDocument.getAnonymousElementByAttribute(notification, "anonid", "messageText");
      description.appendChild(link);

      // Remove the notfication when the page is reloaded.
      doc.defaultView.top.addEventListener("unload", function() {
        notificationBox.removeNotification(notification);
      }, false);
    }

  }
};

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const EXPORTED_SYMBOLS = ["S4EStatusService"];

const CU = Components.utils;

CU.import("resource://gre/modules/Services.jsm");
CU.import("resource://gre/modules/XPCOMUtils.jsm");

function S4EStatusService(window, service, getters)
{
	this._window = window;
	this._service = service;
	this._getters = getters;

	this._overLinkService = new S4EOverlinkService(this._window, this._service, this);
}

S4EStatusService.prototype =
{
	_window:                 null,
	_service:                null,
	_getters:                null,
	_overLinkService:        null,

	_overLink:               { val: "", type: "" },
	_network:                { val: "", type: "" },
	_networkXHR:             { val: "", type: "" },
	_status:                 { val: "", type: "" },
	_jsStatus:               { val: "", type: "" },
	_defaultStatus:          { val: "", type: "" },

	_isFullScreen:           false,
	_isFullScreenVideo:      false,

	_statusText:             { val: "", type: "" },
	_noUpdate:               false,
	_statusChromeTimeoutID:  0,
	_statusContentTimeoutID: 0,

	getCompositeStatusText: function()
	{
		return this._statusText.val;
	},

	getStatusText: function()
	{
		return this._status.val;
	},

	setNetworkStatus: function(status, busy)
	{
		if(busy)
		{
			this._network = { val: status, type: "network" };
			this._networkXHR = { val: "", type: "network xhr" };
		}
		else
		{
			this._networkXHR = { val: status, type: "network xhr" };
		}
		this.updateStatusField();
	},

	setStatusText: function(status)
	{
		this._status = { val: status, type: "status chrome" };
		this.updateStatusField();
	},

	setJSStatus: function(status)
	{
		this._jsStatus = { val: status, type: "status content" };
		this.updateStatusField();
	},

	setJSDefaultStatus: function(status)
	{
		// This was removed from Firefox in Bug 862917
	},

	setDefaultStatus: function(status)
	{
		this._defaultStatus = { val: status, type: "status chrome default" };
		this.updateStatusField();
	},

	setOverLink: function(link, aAnchor)
	{
		this._overLinkService.update(link, aAnchor);
	},

	setOverLinkInternal: function(link, aAnchor)
	{
		let status = this._service.status;
		let statusLinkOver = this._service.statusLinkOver;

		if(statusLinkOver)
		{
			link = link.replace(/[\u200e\u200f\u202a\u202b\u202c\u202d\u202e]/g, encodeURIComponent);
			if(status == statusLinkOver)
			{
				this._overLink = { val: link, type: "overLink", anchor: aAnchor };
				this.updateStatusField();
			}
			else
			{
				this.setStatusField(statusLinkOver, { val: link, type: "overLink", anchor: aAnchor }, true);
			}
		}
	},

	setNoUpdate: function(nu)
	{
		this._noUpdate = nu;
	},

	buildBinding: function() {
		let XULBWPropHandler = function(prop, oldval, newval) {
			CU.reportError("Attempt to modify XULBrowserWindow." + prop);
			return oldval;
		};

		["updateStatusField", "onStatusChange"].forEach(function(prop)
		{
			this._window.XULBrowserWindow.unwatch(prop);
			this._window.XULBrowserWindow[prop] = function() {};
			this._window.XULBrowserWindow.watch(prop, XULBWPropHandler);
		}, this);

		["getCompositeStatusText", "getStatusText", "setStatusText", "setJSStatus",
		"setJSDefaultStatus", "setDefaultStatus", "setOverLink"].forEach(function(prop)
		{
			this._window.XULBrowserWindow.unwatch(prop);
			this._window.XULBrowserWindow[prop] = this[prop].bind(this);
			this._window.XULBrowserWindow.watch(prop, XULBWPropHandler);
		}, this);

		let XULBWHandler = function(prop, oldval, newval) {
			if(!newval)
			{
				return newval;
			}
			CU.reportError("XULBrowserWindow changed. Updating S4E bindings.");
			this._window.setTimeout(function(self)
			{
				self.buildBinding();
			}, 0, this);
			return newval;
		};

		this._window.watch("XULBrowserWindow", XULBWHandler);
	},

	destroy: function()
	{
		// No need to unbind from the XULBrowserWindow, it's already null at this point

		this.clearTimer("_statusChromeTimeoutID");
		this.clearTimer("_statusContentTimeoutID");

		this._overLinkService.destroy();

		["_overLink", "_network", "_networkXHR", "_status", "_jsStatus", "_defaultStatus",
		"_statusText", "_window", "_service", "_getters", "_overLinkService"].forEach(function(prop)
		{
			delete this[prop];
		}, this);
	},

	buildTextOrder: function()
	{
		this.__defineGetter__("_textOrder", function()
		{
			let textOrder = ["_overLink"];
			if(this._service.statusNetwork)
			{
				textOrder.push("_network");
				if(this._service.statusNetworkXHR)
				{
					textOrder.push("_networkXHR");
				}
			}
			textOrder.push("_status", "_jsStatus");
			if(this._service.statusDefault)
			{
				textOrder.push("_defaultStatus");
			}

			delete this._textOrder;
			return this._textOrder = textOrder;
		});
	},

	updateStatusField: function(force)
	{
		let text = { val: "", type: "" };
		for(let i = 0; !text.val && i < this._textOrder.length; i++)
		{
			text = this[this._textOrder[i]];
		}

		if(this._statusText.val != text.val || force)
		{
			if(this._noUpdate)
			{
				return;
			}

			this._statusText = text;

			this.setStatusField(this._service.status, text, false);

			if(text.val && this._service.statusTimeout)
			{
				this.setTimer(text.type);
			}
		}
	},

	updateFullScreen: function()
	{
		this._isFullScreen = this._window.fullScreen;
		this._isFullScreenVideo = false;
		if(this._isFullScreen)
		{
			let fsEl = this._window.content.document.mozFullScreenElement;
			if(fsEl && (fsEl.nodeName == "VIDEO" || fsEl.getElementsByTagName("VIDEO").length > 0))
			{
				this._isFullScreenVideo = true;
			}
		}

		this.clearStatusField();
		this.updateStatusField(true);
	},

	setTimer: function(type)
	{
		let typeArgs = type.split(" ", 3);

		if(typeArgs.length < 2 || typeArgs[0] != "status")
		{
			return;
		}

		if(typeArgs[1] == "chrome")
		{
			this.clearTimer("_statusChromeTimeoutID");
			this._statusChromeTimeoutID = this._window.setTimeout(function(self, isDefault)
			{
				self._statusChromeTimeoutID = 0;
				if(isDefault)
				{
					self.setDefaultStatus("");
				}
				else
				{
					self.setStatusText("");
				}
			}, this._service.statusTimeout, this, (typeArgs.length == 3 && typeArgs[2] == "default"));
		}
		else
		{
			this.clearTimer("_statusContentTimeoutID");
			this._statusContentTimeoutID = this._window.setTimeout(function(self)
			{
				self._statusContentTimeoutID = 0;
				self.setJSStatus("");
			}, this._service.statusTimeout, this);
		}
	},

	clearTimer: function(timerName)
	{
		if(this[timerName] != 0)
		{
			this._window.clearTimeout(this[timerName]);
			this[timerName] = 0;
		}
	},

	clearStatusField: function()
	{
		this._getters.statusOverlay.value = "";

		let status_label = this._getters.statusWidgetLabel;
		if(status_label)
		{
			status_label.value = "";
		}

	},

	setStatusField: function(location, text, allowTooltip)
	{
		if(!location)
		{
			return;
		}

		let label = null;

		if(this._isFullScreen && this._service.advancedStatusDetectFullScreen)
		{
			switch(location)
			{
				case 1: // Toolbar
					location = 3
					break;
				case 2: // URL bar
					if(Services.prefs.getBoolPref("browser.fullscreen.autohide"))
					{
						location = 3
					}
					break;
			}
		}

		switch(location)
		{
			case 1: // Toolbar
				label = this._getters.statusWidgetLabel;
				break;
			case 2: // URL Bar
				break;
			case 3: // Popup
			default:
				if(this._isFullScreenVideo && this._service.advancedStatusDetectVideo)
				{
					return;
				}
				label = this._getters.statusOverlay;
				break;
		}

		if(label)
		{
			label.setAttribute("previoustype", label.getAttribute("type"));
			label.setAttribute("type", text.type);
			label.value = text.val;
			label.setAttribute("crop", text.type == "overLink" ? "center" : "end");
		}
	}
};

function S4EOverlinkService(window, service, statusService) {
	this._window = window;
	this._service = service;
	this._statusService = statusService;
}

S4EOverlinkService.prototype =
{
	_window:        null,
	_service:       null,
	_statusService: null,

	_timer:         0,
	_currentLink:   { link: "", anchor: null },
	_pendingLink:   { link: "", anchor: null },
	_listening:     false,

	update: function(aLink, aAnchor)
	{
		this.clearTimer();
		this.stopListen();
		this._pendingLink = { link: aLink, anchor: aAnchor };

		if(!aLink)
		{
			if(this._window.XULBrowserWindow.hideOverLinkImmediately || !this._service.statusLinkOverDelayHide)
			{
				this._show();
			}
			else
			{
				this._showDelayed();
			}
		}
		else if(this._currentLink.link || !this._service.statusLinkOverDelayShow)
		{
			this._show();
		}
		else
		{
			this._showDelayed();
			this.startListen();
		}
	},

	destroy: function()
	{
		this.clearTimer();
		this.stopListen();

		["_currentLink", "_pendingLink",  "_statusService", "_window"].forEach(function(prop)
		{
			delete this[prop];
		}, this);
	},

	startListen: function()
	{
		if(!this._listening)
		{
			this._window.addEventListener("mousemove", this, true);
			this._listening = true;
		}
	},

	stopListen: function()
	{
		if(this._listening)
		{
			this._window.removeEventListener("mousemove", this, true);
			this._listening = false;
		}
	},

	clearTimer: function()
	{
		if(this._timer != 0)
		{
			this._window.clearTimeout(this._timer);
			this._timer = 0;
		}
	},

	handleEvent: function(event)
	{
		switch(event.type)
		{
			case "mousemove":
				this.clearTimer();
				this._showDelayed();
		}
	},

	_showDelayed: function()
	{
		let delay = ((this._pendingLink.link)
			? this._service.statusLinkOverDelayShow
			: this._service.statusLinkOverDelayHide);

		this._timer = this._window.setTimeout(function(self)
		{
			self._timer = 0;
			self._show();
			self.stopListen();
		}, delay, this);
	},

	_show: function()
	{
		this._currentLink = this._pendingLink;
		this._statusService.setOverLinkInternal(this._currentLink.link, this._currentLink.anchor);
	}
};


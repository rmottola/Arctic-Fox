/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const EXPORTED_SYMBOLS = ["S4EProgressService"];

const CI = Components.interfaces;
const CU = Components.utils;

CU.import("resource://gre/modules/XPCOMUtils.jsm");

function S4EProgressService(gBrowser, service, getters, statusService) {
	this._gBrowser = gBrowser;
	this._service = service;
	this._getters = getters;
	this._statusService = statusService;

	this._gBrowser.addProgressListener(this);
}

S4EProgressService.prototype =
{
	_gBrowser:      null,
	_service:       null,
	_getters:       null,
	_statusService: null,

	_busyUI:        false,

	set value(val)
	{
		let toolbar_progress = this._getters.toolbarProgress;
		if(toolbar_progress)
		{
			toolbar_progress.value = val;
		}

		let throbber_progress = this._getters.throbberProgress;
		if(throbber_progress)
		{
			if(val)
			{
				throbber_progress.setAttribute("progress", val);
			}
			else
			{
				throbber_progress.removeAttribute("progress");
			}
		}
	},

	set collapsed(val)
	{
		let toolbar_progress = this._getters.toolbarProgress;
		if(toolbar_progress)
		{
			toolbar_progress.collapsed = val;
		}

		let throbber_progress = this._getters.throbberProgress;
		if(throbber_progress)
		{
			if(val)
			{
				throbber_progress.removeAttribute("busy");
			}
			else
			{
				throbber_progress.setAttribute("busy", true);
			}
		}
	},

	destroy: function()
	{
		this._gBrowser.removeProgressListener(this);

		["_gBrowser", "_service", "_getters", "_statusService"].forEach(function(prop)
		{
			delete this[prop];
		}, this);
	},

	onStatusChange: function(aWebProgress, aRequest, aStatus, aMessage)
	{
		this._statusService.setNetworkStatus(aMessage, this._busyUI);
	},

	onStateChange: function(aWebProgress, aRequest, aStateFlags, aStatus)
	{
		let nsIWPL = CI.nsIWebProgressListener;

		if(!this._busyUI
		&& aStateFlags & nsIWPL.STATE_START
		&& aStateFlags & nsIWPL.STATE_IS_NETWORK
		&& !(aStateFlags & nsIWPL.STATE_RESTORING))
		{
			this._busyUI = true;
			this.value = 0;
			this.collapsed = false;
		}
		else if(aStateFlags & nsIWPL.STATE_STOP)
		{
			if(aRequest)
			{
				let msg = "";
				let location;
				if(aRequest instanceof CI.nsIChannel || "URI" in aRequest)
				{
					location = aRequest.URI;
					if(location.spec != "about:blank")
					{
						switch (aStatus)
						{
							case Components.results.NS_BINDING_ABORTED:
								msg = this._getters.strings.getString("nv_stopped");
								break;
							case Components.results.NS_ERROR_NET_TIMEOUT:
								msg = this._getters.strings.getString("nv_timeout");
								break;
						}
					}
				}

				if(!msg && (!location || location.spec != "about:blank"))
				{
					msg = this._getters.strings.getString("nv_done");
				}

				this._statusService.setDefaultStatus(msg);
				this._statusService.setNetworkStatus("", this._busyUI);
			}

			if(this._busyUI)
			{
				this._busyUI = false;
				this.collapsed = true;
				this.value = 0;
			}
		}
	},

	onUpdateCurrentBrowser: function(aStateFlags, aStatus, aMessage, aTotalProgress)
	{
		let nsIWPL = CI.nsIWebProgressListener;
		let loadingDone = aStateFlags & nsIWPL.STATE_STOP;

		this.onStateChange(
			this._gBrowser.webProgress,
			{ URI: this._gBrowser.currentURI },
			((loadingDone ? nsIWPL.STATE_STOP : nsIWPL.STATE_START) | (aStateFlags & nsIWPL.STATE_IS_NETWORK)),
			aStatus
		);

		if(!loadingDone)
		{
			this.onProgressChange(this._gBrowser.webProgress, null, 0, 0, aTotalProgress, 1);
			this.onStatusChange(this._gBrowser.webProgress, null, 0, aMessage);
		}
	},

	onProgressChange: function(aWebProgress, aRequest, aCurSelfProgress, aMaxSelfProgress, aCurTotalProgress, aMaxTotalProgress)
	{
		if (aMaxTotalProgress > 0 && this._busyUI)
		{
			// This is highly optimized.  Don't touch this code unless
			// you are intimately familiar with the cost of setting
			// attrs on XUL elements. -- hyatt
			let percentage = (aCurTotalProgress * 100) / aMaxTotalProgress;
			this.value = percentage;
		}
	},

	onProgressChange64: function(aWebProgress, aRequest, aCurSelfProgress, aMaxSelfProgress, aCurTotalProgress, aMaxTotalProgress)
	{
		return this.onProgressChange(aWebProgress, aRequest, aCurSelfProgress, aMaxSelfProgress, aCurTotalProgress, aMaxTotalProgress);
	},

	QueryInterface: XPCOMUtils.generateQI([ CI.nsIWebProgressListener, CI.nsIWebProgressListener2 ])
};


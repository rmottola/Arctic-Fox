/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const EXPORTED_SYMBOLS = ["S4EToolbars"];

const CI = Components.interfaces;
const CU = Components.utils;

CU.import("resource://gre/modules/Services.jsm");

function S4EToolbars(window, gBrowser, toolbox, service, getters)
{
	this._window = window;
	this._toolbox = toolbox;
	this._service = service;
	this._getters = getters;
	this._handler = new ClassicS4EToolbars(this._window, this._toolbox);
}

S4EToolbars.prototype =
{
	_window:  null,
	_toolbox: null,
	_service: null,
	_getters: null,

	_handler: null,

	setup: function()
	{
		this.updateSplitters(false);
		this.updateWindowGripper(false);
		this._handler.setup(this._service.firstRun);
	},

	destroy: function()
	{
		this._handler.destroy();

		["_window", "_toolbox",  "_service", "_getters", "_handler"].forEach(function(prop)
		{
			delete this[prop];
		}, this);
	},

	updateSplitters: function(action)
	{
		let document = this._window.document;

		let splitter_before = document.getElementById("status4evar-status-splitter-before");
		if(splitter_before)
		{
			splitter_before.parentNode.removeChild(splitter_before);
		}

		let splitter_after = document.getElementById("status4evar-status-splitter-after");
		if(splitter_after)
		{
			splitter_after.parentNode.removeChild(splitter_after);
		}

		let status = this._getters.statusWidget;
		if(!action || !status)
		{
			return;
		}

		let urlbar = document.getElementById("urlbar-container");
		let stop = document.getElementById("stop-button");
		let fullscreenflex = document.getElementById("fullscreenflex");

		let nextSibling = status.nextSibling;
		let previousSibling = status.previousSibling;

		function getSplitter(splitter, suffix)
		{
			if(!splitter)
			{
				splitter = document.createElement("splitter");
				splitter.id = "status4evar-status-splitter-" + suffix;
				splitter.setAttribute("resizebefore", "flex");
				splitter.setAttribute("resizeafter", "flex");
				splitter.className = "chromeclass-toolbar-additional status4evar-status-splitter";
			}
			return splitter;
		}

		if((previousSibling && previousSibling.flex > 0)
		|| (urlbar && stop && urlbar.getAttribute("combined") && stop == previousSibling))
		{
			status.parentNode.insertBefore(getSplitter(splitter_before, "before"), status);
		}

		if(nextSibling && nextSibling.flex > 0 && nextSibling != fullscreenflex)
		{
			status.parentNode.insertBefore(getSplitter(splitter_after, "after"), nextSibling);
		}
	},

	updateWindowGripper: function(action)
	{
		let document = this._window.document;

		let gripper = document.getElementById("status4evar-window-gripper");
		let toolbar = this._getters.statusBar || this._getters.addonbar;

		if(!action || !toolbar || !this._service.addonbarWindowGripper
		|| this._window.windowState != CI.nsIDOMChromeWindow.STATE_NORMAL || toolbar.toolbox.customizing)
		{
			if(gripper)
			{
				gripper.parentNode.removeChild(gripper);
			}
			return;
		}

		gripper = this._handler.buildGripper(toolbar, gripper, "status4evar-window-gripper");

		toolbar.appendChild(gripper);
	}
};

function ClassicS4EToolbars(window, toolbox)
{
	this._window = window;
	this._toolbox = toolbox;
}

ClassicS4EToolbars.prototype =
{
	_window:  null,
	_toolbox: null,

	setup: function(firstRun)
	{
		let document = this._window.document;

		let addon_bar = document.getElementById("addon-bar");
		if(addon_bar)
		{
			let baseSet = "addonbar-closebutton"
				    + ",status4evar-status-widget"
				    + ",status4evar-progress-widget";

			// Update the defaultSet
			let defaultSet = baseSet;
			let defaultSetIgnore = ["addonbar-closebutton", "spring", "status-bar"];
			addon_bar.getAttribute("defaultset").split(",").forEach(function(item)
			{
				if(defaultSetIgnore.indexOf(item) == -1)
				{
					defaultSet += "," + item;
				}
			});
			defaultSet += ",status-bar"
			addon_bar.setAttribute("defaultset", defaultSet);

			// Update the currentSet
			if(firstRun)
			{
				let isCustomizableToolbar = function(aElt)
				{
					return aElt.localName == "toolbar" && aElt.getAttribute("customizable") == "true";
				}

				let isCustomizedAlready = false;
				let toolbars = Array.filter(this._toolbox.childNodes, isCustomizableToolbar).concat(
					       Array.filter(this._toolbox.externalToolbars, isCustomizableToolbar));
				toolbars.forEach(function(toolbar)
				{
					if(toolbar.currentSet.indexOf("status4evar") > -1)
					{
						isCustomizedAlready = true;
					}
				});

				if(!isCustomizedAlready)
				{
					let currentSet = baseSet;
					let currentSetIgnore = ["addonbar-closebutton", "spring"];
					addon_bar.currentSet.split(",").forEach(function(item)
					{
						if(currentSetIgnore.indexOf(item) == -1)
						{
							currentSet += "," + item;
						}
					});
					addon_bar.currentSet = currentSet;
					addon_bar.setAttribute("currentset", currentSet);
					document.persist(addon_bar.id, "currentset");
					this._window.setToolbarVisibility(addon_bar, true);
				}
			}
		}
	},

	destroy: function()
	{
		["_window", "_toolbox"].forEach(function(prop)
		{
			delete this[prop];
		}, this);
	},

	buildGripper: function(toolbar, gripper, id)
	{
		if(!gripper)
		{
			let document = this._window.document;

			gripper = document.createElement("resizer");
			gripper.id = id;
			gripper.dir = "bottomend";
		}

		return gripper;
	}
};

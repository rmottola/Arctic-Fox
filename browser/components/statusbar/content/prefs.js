/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

Components.utils.import("resource://gre/modules/Services.jsm");

var status4evarPrefs =
{
	get dynamicProgressStyle()
	{
		let styleSheets = window.document.styleSheets;
		for(let i = 0; i < styleSheets.length; i++)
		{
			let styleSheet = styleSheets[i];
			if(styleSheet.href == "chrome://browser/skin/statusbar/dynamic.css")
			{
				delete this.dynamicProgressStyle;
				return this.dynamicProgressStyle = styleSheet;
			}
		}

		return null;
	},

//
// Status timeout management
//
	get statusTimeoutPref()
	{
		delete this.statusTimeoutPref;
		return this.statusTimeoutPref = document.getElementById("status4evar-pref-status-timeout");
	},

	get statusTimeoutCheckbox()
	{
		delete this.statusTimeoutCheckbox;
		return this.statusTimeoutCheckbox = document.getElementById("status4evar-status-timeout-check");
	},

	statusTimeoutChanged: function()
	{
		if(this.statusTimeoutPref.value > 0)
		{
			this.statusTimeoutPref.disabled = false;
			this.statusTimeoutCheckbox.checked = true;
		}
		else
		{
			this.statusTimeoutPref.disabled = true;
			this.statusTimeoutCheckbox.checked = false;
		}
	},

	statusTimeoutSync: function()
	{
		this.statusTimeoutChanged();
		return undefined;
	},

	statusTimeoutToggle: function()
	{
		if(this.statusTimeoutPref.disabled == this.statusTimeoutCheckbox.checked)
		{
			if(this.statusTimeoutCheckbox.checked)
			{
				this.statusTimeoutPref.value = 10;
			}
			else
			{
				this.statusTimeoutPref.value = 0;
			}
		}
	},

//
// Status network management
//
	get statusNetworkPref()
	{
		delete this.statusNetworkPref;
		return this.statusNetworkPref = document.getElementById("status4evar-pref-status-network");
	},

	get statusNetworkXHRPref()
	{
		delete this.statusNetworkXHRPref;
		return this.statusNetworkXHRPref = document.getElementById("status4evar-pref-status-network-xhr");
	},

	statusNetworkChanged: function()
	{
		this.statusNetworkXHRPref.disabled = ! this.statusNetworkPref.value;
	},

	statusNetworkSync: function()
	{
		this.statusNetworkChanged();
		return undefined;
	},

//
// Status Text langth managment
//
	get textMaxLengthPref()
	{
		delete this.textMaxLengthPref;
		return this.textMaxLengthPref = document.getElementById("status4evar-pref-status-toolbar-maxLength");
	},

	get textMaxLengthCheckbox()
	{
		delete this.textMaxLengthCheckbox;
		return this.textMaxLengthCheckbox = document.getElementById("status4evar-status-toolbar-maxLength-check");
	},

	textLengthChanged: function()
	{	
		if(this.textMaxLengthPref.value > 0)
		{
			this.textMaxLengthPref.disabled = false;
			this.textMaxLengthCheckbox.checked = true;
		}
		else
		{
			this.textMaxLengthPref.disabled = true;
			this.textMaxLengthCheckbox.checked = false;
		}
	},

	textLengthSync: function()
	{
		this.textLengthChanged();
		return undefined;
	},

	textLengthToggle: function()
	{
		if(this.textMaxLengthPref.disabled == this.textMaxLengthCheckbox.checked)
		{
			if(this.textMaxLengthCheckbox.checked)
			{
				this.textMaxLengthPref.value = 800;
			}
			else
			{
				this.textMaxLengthPref.value = 0;
			}
		}
	},

//
// Toolbar progress style management
//
	get progressToolbarStylePref()
	{
		delete this.progressToolbarStylePref;
		return this.progressToolbarStylePref = document.getElementById("status4evar-pref-progress-toolbar-style");
	},

	get progressToolbarCSSPref()
	{
		delete this.progressToolbarCSSPref;
		return this.progressToolbarCSSPref = document.getElementById("status4evar-pref-progress-toolbar-css");
	},

	get progressToolbarProgress()
	{
		delete this.progressToolbarProgress;
		return this.progressToolbarProgress = document.getElementById("status4evar-progress-bar");
	},

	progressToolbarCSSChanged: function()
	{
		if(!this.progressToolbarCSSPref.value)
		{
			this.progressToolbarCSSPref.value = "#33FF33";
		}
		this.dynamicProgressStyle.cssRules[1].style.background = this.progressToolbarCSSPref.value;
	},

	progressToolbarStyleChanged: function()
	{
		this.progressToolbarCSSChanged();
		this.progressToolbarCSSPref.disabled = !this.progressToolbarStylePref.value;
		if(this.progressToolbarStylePref.value)
		{
			this.progressToolbarProgress.setAttribute("s4estyle", true);
		}
		else
		{
			this.progressToolbarProgress.removeAttribute("s4estyle");
		}
	},

	progressToolbarStyleSync: function()
	{
		this.progressToolbarStyleChanged();
		return undefined;
	},

//
// Download progress management
//
	get downloadProgressCheck()
	{
		delete this.downloadProgressCheck;
		return this.downloadProgressCheck = document.getElementById("status4evar-download-progress-check");
	},

	get downloadProgressPref()
	{
		delete this.downloadProgressPref;
		return this.downloadProgressPref = document.getElementById("status4evar-pref-download-progress");
	},

	get downloadProgressColorActivePref()
	{
		delete this.downloadProgressActiveColorPref;
		return this.downloadProgressActiveColorPref = document.getElementById("status4evar-pref-download-color-active");
	},

	get downloadProgressColorPausedPref()
	{
		delete this.downloadProgressPausedColorPref;
		return this.downloadProgressPausedColorPref = document.getElementById("status4evar-pref-download-color-paused");
	},

	downloadProgressSync: function()
	{
		let val = this.downloadProgressPref.value;
		this.downloadProgressColorActivePref.disabled = (val == 0);
		this.downloadProgressColorPausedPref.disabled = (val == 0);
		this.downloadProgressPref.disabled = (val == 0);
		this.downloadProgressCheck.checked = (val != 0);
		return ((val == 0) ? 1 : val);
	},

	downloadProgressToggle: function()
	{
		let enabled = this.downloadProgressCheck.checked;
		this.downloadProgressPref.value = ((enabled) ? 1 : 0);
	},

//
// Pref Window load
//
	get downloadButtonActionCommandPref()
	{
		delete this.downloadButtonActionCommandPref;
		return this.downloadButtonActionCommandPref = document.getElementById("status4evar-pref-download-button-action-command");
	},

	get downloadButtonActionThirdPartyItem()
	{
		delete this.downloadButtonActionThirdPartyItem;
		return this.downloadButtonActionThirdPartyItem = document.getElementById("status4evar-download-button-action-menu-thirdparty");
	},

	onPrefWindowLoad: function()
	{
		if(!this.downloadButtonActionCommandPref.value)
		{
			this.downloadButtonActionThirdPartyItem.disabled = true;
		}
	},

	onPrefWindowUnLoad: function()
	{
	}
}

var XULBrowserWindow = {
}


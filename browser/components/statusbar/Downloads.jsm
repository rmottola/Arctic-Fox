/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const EXPORTED_SYMBOLS = ["S4EDownloadService"];

const CC = Components.classes;
const CI = Components.interfaces;
const CU = Components.utils;

CU.import("resource://gre/modules/Services.jsm");
CU.import("resource://gre/modules/PluralForm.jsm");
CU.import("resource://gre/modules/DownloadUtils.jsm");
CU.import("resource://gre/modules/PrivateBrowsingUtils.jsm");
CU.import("resource://gre/modules/XPCOMUtils.jsm");

function S4EDownloadService(window, gBrowser, service, getters)
{
	this._window = window;
	this._gBrowser = gBrowser;
	this._service = service;
	this._getters = getters;

	this._handler = new JSTransferHandler(this._window, this);
}

S4EDownloadService.prototype =
{
	_window:              null,
	_gBrowser:            null,
	_service:             null,
	_getters:             null,

	_handler:             null,
	_listening:           false,

	_binding:             false,
	_customizing:         false,

	_lastTime:            Infinity,

	_dlActive:            false,
	_dlPaused:            false,
	_dlFinished:          false,

	_dlCountStr:          null,
	_dlTimeStr:           null,

	_dlProgressAvg:       0,
	_dlProgressMax:       0,
	_dlProgressMin:       0,
	_dlProgressType:      "active",

	_dlNotifyTimer:       0,
	_dlNotifyGlowTimer:   0,

	init: function()
	{
		if(!this._getters.downloadButton)
		{
			this.uninit();
			return;
		}

		if(this._listening)
		{
			return;
		}

		this._handler.start();
		this._listening = true;

		this._lastTime = Infinity;

		this.updateBinding();
		this.updateStatus();
	},

	uninit: function()
	{
		if(!this._listening)
		{
			return;
		}

		this._listening = false;
		this._handler.stop();

		this.releaseBinding();
	},

	destroy: function()
	{
		this.uninit();
		this._handler.destroy();

		["_window", "_gBrowser", "_service", "_getters", "_handler"].forEach(function(prop)
		{
			delete this[prop];
		}, this);
	},

	updateBinding: function()
	{
		if(!this._listening)
		{
			this.releaseBinding();
			return;
		}

		switch(this._service.downloadButtonAction)
		{
			case 1: // Default
				this.attachBinding();
				break;
			default:
				this.releaseBinding();
				break;
		}
	},

	attachBinding: function()
	{
		if(this._binding)
		{
			return;
		}

		let db = this._window.DownloadsButton;

		db._getAnchorS4EBackup = db.getAnchor;
		db.getAnchor = this.getAnchor.bind(this);

		db._releaseAnchorS4EBackup = db.releaseAnchor;
		db.releaseAnchor = function() {};

		this._binding = true;
	},

	releaseBinding: function()
	{
		if(!this._binding)
		{
			return;
		}

		let db = this._window.DownloadsButton;

		db.getAnchor = db._getAnchorS4EBackup;
		db.releaseAnchor = db._releaseAnchorS4EBackup;

		this._binding = false;
	},

	customizing: function(val)
	{
		this._customizing = val;
	},

	updateStatus: function(lastFinished)
	{
		if(!this._getters.downloadButton)
		{
			this.uninit();
			return;
		}

		let numActive = 0;
		let numPaused = 0;
		let activeTotalSize = 0;
		let activeTransferred = 0;
		let activeMaxProgress = -Infinity;
		let activeMinProgress = Infinity;
		let pausedTotalSize = 0;
		let pausedTransferred = 0;
		let pausedMaxProgress = -Infinity;
		let pausedMinProgress = Infinity;
		let maxTime = -Infinity;

		let dls = ((this.isPrivateWindow) ? this._handler.activePrivateEntries() : this._handler.activeEntries());
		for(let dl of dls)
		{
			if(dl.state == CI.nsIDownloadManager.DOWNLOAD_DOWNLOADING)
			{
				numActive++;
				if(dl.size > 0)
				{
					if(dl.speed > 0)
					{
						maxTime = Math.max(maxTime, (dl.size - dl.transferred) / dl.speed);
					}

					activeTotalSize += dl.size;
					activeTransferred += dl.transferred;

					let currentProgress = ((dl.transferred * 100) / dl.size);
					activeMaxProgress = Math.max(activeMaxProgress, currentProgress);
					activeMinProgress = Math.min(activeMinProgress, currentProgress);
				}
			}
			else if(dl.state == CI.nsIDownloadManager.DOWNLOAD_PAUSED)
			{
				numPaused++;
				if(dl.size > 0)
				{
					pausedTotalSize += dl.size;
					pausedTransferred += dl.transferred;

					let currentProgress = ((dl.transferred * 100) / dl.size);
					pausedMaxProgress = Math.max(pausedMaxProgress, currentProgress);
					pausedMinProgress = Math.min(pausedMinProgress, currentProgress);
				}
			}
		}

		if((numActive + numPaused) == 0)
		{
			this._dlActive = false;
			this._dlFinished = lastFinished;
			this.updateButton();
			this._lastTime = Infinity;
			return;
		}

		let dlPaused =       (numActive == 0);
		let dlStatus =       ((dlPaused) ? this._getters.strings.getString("pausedDownloads")
		                                 : this._getters.strings.getString("activeDownloads"));
		let dlCount =        ((dlPaused) ? numPaused         : numActive);
		let dlTotalSize =    ((dlPaused) ? pausedTotalSize   : activeTotalSize);
		let dlTransferred =  ((dlPaused) ? pausedTransferred : activeTransferred);
		let dlMaxProgress =  ((dlPaused) ? pausedMaxProgress : activeMaxProgress);
		let dlMinProgress =  ((dlPaused) ? pausedMinProgress : activeMinProgress);
		let dlProgressType = ((dlPaused) ? "paused"          : "active");

		[this._dlTimeStr, this._lastTime] = DownloadUtils.getTimeLeft(maxTime, this._lastTime);
		this._dlCountStr =     PluralForm.get(dlCount, dlStatus).replace("#1", dlCount);
		this._dlProgressAvg =  ((dlTotalSize == 0) ? 100 : ((dlTransferred * 100) / dlTotalSize));
		this._dlProgressMax =  ((dlTotalSize == 0) ? 100 : dlMaxProgress);
		this._dlProgressMin =  ((dlTotalSize == 0) ? 100 : dlMinProgress);
		this._dlProgressType = dlProgressType + ((dlTotalSize == 0) ? "-unknown" : "");
		this._dlPaused =       dlPaused;
		this._dlActive =       true;
		this._dlFinished =     false;

		this.updateButton();
	},

	updateButton: function()
	{
		let download_button = this._getters.downloadButton;
		let download_tooltip = this._getters.downloadButtonTooltip;
		let download_progress = this._getters.downloadButtonProgress;
		let download_label = this._getters.downloadButtonLabel;
		if(!download_button)
		{
			return;
		}

		if(!this._dlActive)
		{
			download_button.collapsed = true;
			download_label.value = download_tooltip.label = this._getters.strings.getString("noDownloads");

			download_progress.collapsed = true;
			download_progress.value = 0;

			if(this._dlFinished && this._handler.hasPBAPI && !this.isUIShowing)
			{
				this.callAttention(download_button);
			}
			return;
		}

		switch(this._service.downloadProgress)
		{
			case 2:
				download_progress.value = this._dlProgressMax;
				break;
			case 3:
				download_progress.value = this._dlProgressMin;
				break;
			default:
				download_progress.value = this._dlProgressAvg;
				break;
		}
		download_progress.setAttribute("pmType", this._dlProgressType);
		download_progress.collapsed = (this._service.downloadProgress == 0);

		download_label.value = this.buildString(this._service.downloadLabel);
		download_tooltip.label = this.buildString(this._service.downloadTooltip);

		this.clearAttention(download_button);
		download_button.collapsed = false;
	},

	callAttention: function(download_button)
	{
		if(this._dlNotifyGlowTimer != 0)
		{
			this._window.clearTimeout(this._dlNotifyGlowTimer);
			this._dlNotifyGlowTimer = 0;
		}

		download_button.setAttribute("attention", "true");

		if(this._service.downloadNotifyTimeout)
		{
			this._dlNotifyGlowTimer = this._window.setTimeout(function(self, button)
			{
				self._dlNotifyGlowTimer = 0;
				button.removeAttribute("attention");
			}, this._service.downloadNotifyTimeout, this, download_button);
		}
	},

	clearAttention: function(download_button)
	{
		if(this._dlNotifyGlowTimer != 0)
		{
			this._window.clearTimeout(this._dlNotifyGlowTimer);
			this._dlNotifyGlowTimer = 0;
		}

		download_button.removeAttribute("attention");
	},

	notify: function()
	{
		if(this._dlNotifyTimer == 0 && this._service.downloadNotifyAnimate)
		{
			let download_button_anchor = this._getters.downloadButtonAnchor;
			let download_notify_anchor = this._getters.downloadNotifyAnchor;
			if(download_button_anchor)
			{
				if(!download_notify_anchor.style.transform)
				{
					let bAnchorRect = download_button_anchor.getBoundingClientRect();
					let nAnchorRect = download_notify_anchor.getBoundingClientRect();

					let translateX = bAnchorRect.left - nAnchorRect.left;
					translateX += .5 * (bAnchorRect.width  - nAnchorRect.width);

					let translateY = bAnchorRect.top  - nAnchorRect.top;
					translateY += .5 * (bAnchorRect.height - nAnchorRect.height);

					download_notify_anchor.style.transform = "translate(" +  translateX + "px, " + translateY + "px)";
				}

				download_notify_anchor.setAttribute("notification", "finish");
				this._dlNotifyTimer = this._window.setTimeout(function(self, anchor)
				{
					self._dlNotifyTimer = 0;
					anchor.removeAttribute("notification");
					anchor.style.transform = "";
				}, 1000, this, download_notify_anchor);
			}
		}
	},

	clearFinished: function()
	{
		this._dlFinished = false;
		let download_button = this._getters.downloadButton;
		if(download_button)
		{
			this.clearAttention(download_button);
		}
	},

	getAnchor: function(aCallback)
	{
		if(this._customizing)
		{
			aCallback(null);
			return;
		}

		aCallback(this._getters.downloadButtonAnchor);
	},

	openUI: function(aEvent)
	{
		this.clearFinished();

		switch(this._service.downloadButtonAction)
		{
			case 1: // Firefox Default
				this._handler.openUINative();
				break;
			case 2: // Show Library
				this._window.PlacesCommandHook.showPlacesOrganizer("Downloads");
				break;
			case 3: // Show Tab
				let found = this._gBrowser.browsers.some(function(browser, index)
				{
					if("about:downloads" == browser.currentURI.spec)
					{
						this._gBrowser.selectedTab = this._gBrowser.tabContainer.childNodes[index];
						return true;
					}
				}, this);

				if(!found)
				{
					this._window.openUILinkIn("about:downloads", "tab");
				}
				break;
			case 4: // External Command
				let command = this._service.downloadButtonActionCommand;
				if(commend)
				{
					this._window.goDoCommand(command);
				}
				break;
			default: // Nothing
				break;
		}

		aEvent.stopPropagation();
	},

	get isPrivateWindow()
	{
		return this._handler.hasPBAPI && PrivateBrowsingUtils.isWindowPrivate(this._window);
	},

	get isUIShowing()
	{
		switch(this._service.downloadButtonAction)
		{
			case 1: // Firefox Default
				return this._handler.isUIShowingNative;
			case 2: // Show Library
				var organizer = Services.wm.getMostRecentWindow("Places:Organizer");
				if(organizer)
				{
					let selectedNode = organizer.PlacesOrganizer._places.selectedNode;
					let downloadsItemId = organizer.PlacesUIUtils.leftPaneQueries["Downloads"];
					return selectedNode && selectedNode.itemId === downloadsItemId;
				}
				return false;
			case 3: // Show tab
				let currentURI = this._gBrowser.currentURI;
				return currentURI && currentURI.spec == "about:downloads";
			default: // Nothing
				return false;
		}
	},

	buildString: function(mode)
	{
		switch(mode)
		{
			case 0:
				return this._dlCountStr;
			case 1:
				return ((this._dlPaused) ? this._dlCountStr : this._dlTimeStr);
			default:
				let compStr = this._dlCountStr;
				if(!this._dlPaused)
				{
					compStr += " (" + this._dlTimeStr + ")";
				}
				return compStr;
		}
	}
};

function JSTransferHandler(window, downloadService)
{
	this._window = window;

	let api = CU.import("resource://gre/modules/Downloads.jsm", {}).Downloads;

	this._activePublic = new JSTransferListener(downloadService, api.getList(api.PUBLIC), false);
	this._activePrivate = new JSTransferListener(downloadService, api.getList(api.PRIVATE), true);
}

JSTransferHandler.prototype =
{
	_window:          null,
	_activePublic:    null,
	_activePrivate:   null,

	destroy: function()
	{
		this._activePublic.destroy();
		this._activePrivate.destroy();

		["_window", "_activePublic", "_activePrivate"].forEach(function(prop)
		{
			delete this[prop];
		}, this);
	},

	start: function()
	{
		this._activePublic.start();
		this._activePrivate.start();
	},

	stop: function()
	{
		this._activePublic.stop();
		this._activePrivate.stop();
	},

	get hasPBAPI()
	{
		return true;
	},

	openUINative: function()
	{
		this._window.DownloadsPanel.showPanel();
	},

	get isUIShowingNative()
	{
		return this._window.DownloadsPanel.isPanelShowing;
	},

	activeEntries: function()
	{
		return this._activePublic.downloads();
	},

	activePrivateEntries: function()
	{
		return this._activePrivate.downloads();
	}
};

function JSTransferListener(downloadService, listPromise, isPrivate)
{
	this._downloadService = downloadService;
	this._isPrivate = isPrivate;
	this._downloads = new Map();

	listPromise.then(this.initList.bind(this)).then(null, CU.reportError);
}

JSTransferListener.prototype =
{
	_downloadService: null,
	_list:            null,
	_downloads:       null,
	_isPrivate:       false,
	_wantsStart:      false,

	initList: function(list)
	{
		this._list = list;
		if(this._wantsStart) {
			this.start();
		}

		this._list.getAll().then(this.initDownloads.bind(this)).then(null, CU.reportError);
	},

	initDownloads: function(downloads)
	{
		downloads.forEach(function(download)
		{
			this.onDownloadAdded(download);
		}, this);
	},

	destroy: function()
	{
		this._downloads.clear();

		["_downloadService", "_list", "_downloads"].forEach(function(prop)
		{
			delete this[prop];
		}, this);
	},

	start: function()
	{
		if(!this._list)
		{
			this._wantsStart = true;
			return;
		}

		this._list.addView(this);
	},

	stop: function()
	{
		if(!this._list)
		{
			this._wantsStart = false;
			return;
		}

		this._list.removeView(this);
	},

	downloads: function()
	{
		return this._downloads.values();
	},

	convertToState: function(dl)
	{
		if(dl.succeeded)
		{
			return CI.nsIDownloadManager.DOWNLOAD_FINISHED;
		}
		if(dl.error && dl.error.becauseBlockedByParentalControls)
		{
			return CI.nsIDownloadManager.DOWNLOAD_BLOCKED_PARENTAL;
		}
		if(dl.error)
		{
			return CI.nsIDownloadManager.DOWNLOAD_FAILED;
		}
		if(dl.canceled && dl.hasPartialData)
		{
			return CI.nsIDownloadManager.DOWNLOAD_PAUSED;
		}
		if(dl.canceled)
		{
			return CI.nsIDownloadManager.DOWNLOAD_CANCELED;
		}
		if(dl.stopped)
		{
			return CI.nsIDownloadManager.DOWNLOAD_NOTSTARTED;
		}
		return CI.nsIDownloadManager.DOWNLOAD_DOWNLOADING;
	},

	onDownloadAdded: function(aDownload)
	{
		let dl = this._downloads.get(aDownload);
		if(!dl)
		{
			dl = {};
			this._downloads.set(aDownload, dl);
		}

		dl.state = this.convertToState(aDownload);
		dl.size = aDownload.totalBytes;
		dl.speed = aDownload.speed;
		dl.transferred = aDownload.currentBytes;
	},

	onDownloadChanged: function(aDownload)
	{
		this.onDownloadAdded(aDownload);

		if(this._isPrivate != this._downloadService.isPrivateWindow)
		{
			return;
		}

		this._downloadService.updateStatus(aDownload.succeeded);

		if(aDownload.succeeded)
		{
			this._downloadService.notify()
		}
	},

	onDownloadRemoved: function(aDownload)
	{
		this._downloads.delete(aDownload);
	}
};


// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// Based on original code by alice0775 https://github.com/alice0775


"use strict";
var browserMenuDragging = {
  //-- config --
  STAY_OPEN_ONDRAGEXIT: false,
  DEBUG: false,
  //-- config --

  menupopup: ['bookmarksMenuPopup',
              'PlacesToolbar',
              'BMB_bookmarksPopup',
              'appmenu_bookmarksPopup',
              'BookmarksMenuToolButtonPopup',
              'UnsortedBookmarksFolderToolButtonPopup',
              'bookmarksMenuPopup-context'],
  timer:[],
  count:[],


  init: function(){
    window.removeEventListener('load', this, false);
    window.addEventListener('unload', this, false);
    this.addPrefListener(this.PrefListener);

    window.addEventListener('aftercustomization', this, false);

    this.initPref();
    this.delayedStartup();
  },

  uninit: function(){
    window.removeEventListener('unload', this, false);
    this.removePrefListener(this.PrefListener);

    window.removeEventListener('aftercustomization', this, false);

    for (var i = 0; i < this.menupopup.length; i++){
      var menupopup = document.getElementById(this.menupopup[i]);
      if (menupopup){
        menupopup.removeEventListener('popupshowing', this, false);
        menupopup.removeEventListener('popuphiding', this, false);
      }
    }

  },

  initPref: function(){
    this.STAY_OPEN_ONDRAGEXIT =
          this.getPref('browser.menu.dragging.stayOpen',
                       'bool', false);
    this.DEBUG =
          this.getPref('browser.menu.dragging.debug',
                       'bool', false);
  },

  //delayed startup
  delayedStartup: function(){
    //wait until construction of bookmarksBarContent is completed.
    for (var i = 0; i < this.menupopup.length; i++){
      this.count[i] = 0;
      this.timer[i] = setInterval(function(self, i){
        if(++self.count[i] > 50 || document.getElementById(self.menupopup[i])){
          clearInterval(self.timer[i]);
          var menupopup = document.getElementById(self.menupopup[i]);
          if (menupopup) {
            menupopup.addEventListener('popupshowing', self, false);
            menupopup.addEventListener('popuphiding', self, false);
          }
        }
      }, 250, this, i);
    }
  },

  handleEvent: function(event){
    switch (event.type) {
      case 'popupshowing':
        this.popupshowing(event);
        break;
      case 'popuphiding':
        this.popuphiding(event);
        break;
      case 'aftercustomization':
        setTimeout(function(self){self.delayedStartup(self);}, 0, this);
        break;
      case 'load':
        this.init();
        break;
      case 'unload':
        this.uninit();
        break;
    }
  },

  popuphiding: function(event) {
    var menupopup = event.originalTarget;
    menupopup.parentNode.parentNode.openNode = null;

    if (menupopup.parentNode.localName == 'toolbarbutton') {
      // Fix for Bug 225434 -  dragging bookmark from personal toolbar and releasing
      // (on same bookmark or elsewhere) or clicking on bookmark menu then cancelling
      // leaves button depressed/sunken when hovered
      menupopup.parentNode.parentNode._openedMenuButton = null;

      if (!PlacesControllerDragHelper.getSession())
      // Clear the dragover attribute if present, if we are dragging into a
      // folder in the hierachy of current opened popup we don't clear
      // this attribute on clearOverFolder.  See Notify for closeTimer.
      if (menupopup.parentNode.hasAttribute('dragover'))
        menupopup.parentNode.removeAttribute('dragover');
    }
  },

  popupshowing: function(event) {
    var menupopup = event.originalTarget;
    browserMenuDragging.debug("popupshowing ===============\n" + menupopup.parentNode.getAttribute('label'));

    var parentPopup = menupopup.parentNode.parentNode;

    if (!!parentPopup.openNode){
      try {
        parentPopup.openNode.hidePopup();
      } catch(e){}
    }
    parentPopup.openNode = menupopup;

    menupopup.onDragStart = function (event) {
      // Bug 555474 -  While bookmark is dragged, the tooltip should not appear
      browserMenuDragging.hideTooltip();
    }

    menupopup.onDragOver = function (event) {
      // Bug 555474 -  While bookmark is dragged, the tooltip should not appear
      browserMenuDragging.hideTooltip();

      var target = event.originalTarget;
      while (target) {
        if (/menupopup/.test(target.localName))
          break;
        target = target.parentNode;
      }
      if (this != target)
        return;
      event.stopPropagation();
      browserMenuDragging.debug("onDragOver " + "\n" + this.parentNode.getAttribute('label'));

      PlacesControllerDragHelper.currentDropTarget = event.target;
      let dt = event.dataTransfer;

      let dropPoint = this._getDropPoint(event);

      if (!dropPoint || !dropPoint.ip ||
          !PlacesControllerDragHelper.canDrop(dropPoint.ip, dt)) {
        this._indicatorBar.hidden = true;
        event.stopPropagation();
        return;
      }

      // Mark this popup as being dragged over.
      this.setAttribute('dragover', 'true');

      if (dropPoint.folderElt) {
        // We are dragging over a folder.
        // _overFolder should take the care of opening it on a timer.
        if (this._overFolder.elt &&
            this._overFolder.elt != dropPoint.folderElt) {
        }
        if (!this._overFolder.elt) {
          this._overFolder.elt = dropPoint.folderElt;
          // Create the timer to open this folder.
          this._overFolder.openTimer = this._overFolder
                                           .setTimer(this._overFolder.hoverTime);
        }
      }
      else {
        // We are not dragging over a folder.
      }

      // Autoscroll the popup strip if we drag over the scroll buttons.
      let anonid = event.originalTarget.getAttribute('anonid');
      let scrollDir = anonid == 'scrollbutton-up' ? -1 :
                      anonid == 'scrollbutton-down' ? 1 : 0;
      if (scrollDir != 0) {
        this._scrollBox.scrollByIndex(scrollDir, false);
      }

      // Check if we should hide the drop indicator for this target.
      if (dropPoint.folderElt || this._hideDropIndicator(event)) {
        this._indicatorBar.hidden = true;
        event.preventDefault();
        event.stopPropagation();
        return;
      }

      // We should display the drop indicator relative to the arrowscrollbox.
      let sbo = this._scrollBox.scrollBoxObject;
      let newMarginTop = 0;
      if (scrollDir == 0) {
        let elt = this.firstChild;
        while (elt && event.screenY > elt.boxObject.screenY +
                                       elt.boxObject.height / 2)
          elt = elt.nextSibling;
        newMarginTop = elt ? elt.boxObject.screenY - sbo.screenY :
                              sbo.height;
      }
      else if (scrollDir == 1)
        newMarginTop = sbo.height;

      // Set the new marginTop based on arrowscrollbox.
      newMarginTop += sbo.y - this._scrollBox.boxObject.y;
      this._indicatorBar.firstChild.style.marginTop = newMarginTop + 'px';
      this._indicatorBar.hidden = false;

      event.preventDefault();
      event.stopPropagation();
    }

    menupopup.onDragExit = function (event) {
      var target = event.originalTarget;
      while (target) {
        if (/menupopup/.test(target.localName))
          break;
        target = target.parentNode;
      }
      if (this != target)
        return;
      event.stopPropagation();
      browserMenuDragging.debug("onDragExit " + browserMenuDragging.STAY_OPEN_ONDRAGEXIT);

      PlacesControllerDragHelper.currentDropTarget = null;
      this.removeAttribute('dragover');

      // If we have not moved to a valid new target clear the drop indicator
      // this happens when moving out of the popup.
      target = event.relatedTarget;
      if (!target)
        this._indicatorBar.hidden = true;

      // Close any folder being hovered over
      if (this._overFolder.elt) {
        this._overFolder.closeTimer = this._overFolder
                                          .setTimer(this._overFolder.hoverTime);
      }

      // The auto-opened attribute is set when this folder was automatically
      // opened after the user dragged over it.  If this attribute is set,
      // auto-close the folder on drag exit.
      // We should also try to close this popup if the drag has started
      // from here, the timer will check if we are dragging over a child.
      if (this.hasAttribute('autoopened') ||
          !browserMenuDragging.STAY_OPEN_ONDRAGEXIT &&
          this.hasAttribute('dragstart')) {
        this._overFolder.closeMenuTimer = this._overFolder
                                              .setTimer(this._overFolder.hoverTime);
      }

      event.stopPropagation();
    }

    menupopup.addEventListener('dragstart', menupopup.onDragStart, true);
    menupopup.addEventListener('dragover', menupopup.onDragOver, true);
    menupopup.addEventListener('dragleave', menupopup.onDragExit, true);
  },

  hideTooltip: function() {
    ['bhTooltip', 'btTooltip2'].forEach(function(id) {
      var tooltip = document.getElementById(id);
      if (tooltip)
        tooltip.hidePopup();
    });
  },

  get getVer(){
    const Cc = Components.classes;
    const Ci = Components.interfaces;
    var info = Cc["@mozilla.org/xre/app-info;1"].getService(Ci.nsIXULAppInfo);
    var ver = parseInt(info.version.substr(0,3) * 10,10) / 10;
    return ver;
  },

  debug: function(aMsg){
    if (!browserMenuDragging.DEBUG)
      return;
    Components.classes["@mozilla.org/consoleservice;1"]
      .getService(Components.interfaces.nsIConsoleService)
      .logStringMessage(aMsg);
  },

  getPref: function(aPrefString, aPrefType, aDefault){
    var xpPref = Components.classes["@mozilla.org/preferences-service;1"]
                  .getService(Components.interfaces.nsIPrefService);
    try{
      switch (aPrefType){
        case 'complex':
          return xpPref.getComplexValue(aPrefString, Components.interfaces.nsILocalFile); break;
        case 'str':
          return xpPref.getCharPref(aPrefString).toString(); break;
        case 'int':
          return xpPref.getIntPref(aPrefString); break;
        case 'bool':
        default:
          return xpPref.getBoolPref(aPrefString); break;
      }
    }catch(e){
    }
    return aDefault;
  },

  setPref: function(aPrefString, aPrefType, aValue){
    var xpPref = Components.classes["@mozilla.org/preferences-service;1"]
                  .getService(Components.interfaces.nsIPrefService);
    try{
      switch (aPrefType){
        case 'complex':
          return xpPref.setComplexValue(aPrefString, Components.interfaces.nsILocalFile, aValue); break;
        case 'str':
          return xpPref.setCharPref(aPrefString, aValue); break;
        case 'int':
          aValue = parseInt(aValue);
          return xpPref.setIntPref(aPrefString, aValue);  break;
        case 'bool':
        default:
          return xpPref.setBoolPref(aPrefString, aValue); break;
      }
    }catch(e){
    }
    return null;
  },

  addPrefListener: function(aObserver) {
      try {
          var pbi = Components.classes["@mozilla.org/preferences;1"].
                    getService(Components.interfaces.nsIPrefBranch2);
          pbi.addObserver(aObserver.domain, aObserver, false);
      } catch(e) {}
  },

  removePrefListener: function(aObserver) {
      try {
          var pbi = Components.classes["@mozilla.org/preferences;1"].
                    getService(Components.interfaces.nsIPrefBranch2);
          pbi.removeObserver(aObserver.domain, aObserver);
      } catch(e) {}
  },

  PrefListener:{
      domain  : 'browser.menu.dragging.stayOpen',

      observe : function(aSubject, aTopic, aPrefstring) {
          if (aTopic == 'nsPref:changed') {
              browserMenuDragging.initPref();
          }
      }
  }
}

window.addEventListener('load', browserMenuDragging, false);
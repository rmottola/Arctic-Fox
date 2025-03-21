# -*- Mode: Java; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

       <menubar id="main-menubar"
                onpopupshowing="if (event.target.parentNode.parentNode == this &amp;&amp;
                                    !('@mozilla.org/widget/nativemenuservice;1' in Cc))
                                  this.setAttribute('openedwithkey',
                                                    event.target.parentNode.openedWithKey);"
                style="border:0px;padding:0px;margin:0px;-moz-appearance:none">
            <menu id="file-menu" label="&fileMenu.label;"
                  accesskey="&fileMenu.accesskey;">
              <menupopup id="menu_FilePopup">
                <menuitem id="menu_newNavigatorTab"
                          label="&tabCmd.label;"
                          command="cmd_newNavigatorTab"
                          key="key_newNavigatorTab"
                          accesskey="&tabCmd.accesskey;"/>
                <menu id="menu_newUserContext"
                      label="&newUserContext.label;"
                      accesskey="&newUserContext.accesskey;"
                      hidden="true">
                  <menupopup onpopupshowing="return createUserContextMenu(event);" />
                </menu>
                <menuitem id="menu_newNavigator"
                          label="&newNavigatorCmd.label;"
                          accesskey="&newNavigatorCmd.accesskey;"
                          key="key_newNavigator"
                          command="cmd_newNavigator"/>
                <menuitem id="menu_newPrivateWindow"
                          label="&newPrivateWindow.label;"
                          accesskey="&newPrivateWindow.accesskey;"
                          command="Tools:PrivateBrowsing"
                          key="key_privatebrowsing"/>
                <menuitem id="menu_newNonRemoteWindow"
                          label="New In-process Window"
                          hidden="true"
                          command="Tools:NonRemoteWindow"/>
                <menuitem id="menu_openLocation"
                          class="show-only-for-keyboard"
                          label="&openLocationCmd.label;"
                          command="Browser:OpenLocation"
                          key="focusURLBar"
                          accesskey="&openLocationCmd.accesskey;"/>
                <menuitem id="menu_openFile"
                          label="&openFileCmd.label;"
                          command="Browser:OpenFile"
                          key="openFileKb"
                          accesskey="&openFileCmd.accesskey;"/>
                <menuitem id="menu_close"
                          class="show-only-for-keyboard"
                          label="&closeCmd.label;"
                          key="key_close"
                          accesskey="&closeCmd.accesskey;"
                          command="cmd_close"/>
                <menuitem id="menu_closeWindow"
                          class="show-only-for-keyboard"
                          hidden="true"
                          command="cmd_closeWindow"
                          key="key_closeWindow"
                          label="&closeWindow.label;"
                          accesskey="&closeWindow.accesskey;"/>
                <menuseparator/>
                <menuitem id="menu_savePage"
                          label="&savePageCmd.label;"
                          accesskey="&savePageCmd.accesskey;"
                          key="key_savePage"
                          command="Browser:SavePage"/>
                <menuitem id="menu_sendLink"
                          label="&emailPageCmd.label;"
                          accesskey="&emailPageCmd.accesskey;"
                          command="Browser:SendLink"/>
                <menuseparator/>
#if !defined(MOZ_WIDGET_GTK) && !defined(MOZ_WIDGET_QT)
                <menuitem id="menu_printSetup"
                          label="&printSetupCmd.label;"
                          accesskey="&printSetupCmd.accesskey;"
                          command="cmd_pageSetup"/>
#endif
#ifndef XP_MACOSX
                <menuitem id="menu_printPreview"
                          label="&printPreviewCmd.label;"
                          accesskey="&printPreviewCmd.accesskey;"
                          command="cmd_printPreview"/>
#endif
                <menuitem id="menu_print"
                          label="&printCmd.label;"
                          accesskey="&printCmd.accesskey;"
                          key="printKb"
                          command="cmd_print"/>
                <menuseparator/>
                <menuitem id="goOfflineMenuitem"
                          label="&goOfflineCmd.label;"
                          accesskey="&goOfflineCmd.accesskey;"
                          type="checkbox"
                          observes="workOfflineMenuitemState"
                          oncommand="BrowserOffline.toggleOfflineStatus();"/>
                <menuitem id="menu_restart"
                          label="&restartCmd.label;"
                          accesskey="&restartCmd.accesskey;"
                          command="cmd_restartApplication"/>
                <menuitem id="menu_FileQuitItem"
#ifdef XP_WIN
                          label="&quitApplicationCmdWin.label;"
                          accesskey="&quitApplicationCmdWin.accesskey;"
#else
#ifdef XP_MACOSX
                          label="&quitApplicationCmdMac.label;"
#else
                          label="&quitApplicationCmd.label;"
                          accesskey="&quitApplicationCmd.accesskey;"
#endif
#ifdef XP_UNIX
                          key="key_quitApplication"
#endif
#endif
                          command="cmd_quitApplication"/>
              </menupopup>
            </menu>

            <menu id="edit-menu" label="&editMenu.label;"
                  accesskey="&editMenu.accesskey;">
              <menupopup id="menu_EditPopup"
                         onpopupshowing="updateEditUIVisibility()"
                         onpopuphidden="updateEditUIVisibility()">
                <menuitem id="menu_undo"
                          label="&undoCmd.label;"
                          key="key_undo"
                          accesskey="&undoCmd.accesskey;"
                          command="cmd_undo"/>
                <menuitem id="menu_redo"
                          label="&redoCmd.label;"
                          key="key_redo"
                          accesskey="&redoCmd.accesskey;"
                          command="cmd_redo"/>
                <menuseparator/>
                <menuitem id="menu_cut"
                          label="&cutCmd.label;"
                          key="key_cut"
                          accesskey="&cutCmd.accesskey;"
                          command="cmd_cut"/>
                <menuitem id="menu_copy"
                          label="&copyCmd.label;"
                          key="key_copy"
                          accesskey="&copyCmd.accesskey;"
                          command="cmd_copy"/>
                <menuitem id="menu_paste"
                          label="&pasteCmd.label;"
                          key="key_paste"
                          accesskey="&pasteCmd.accesskey;"
                          command="cmd_paste"/>
                <menuitem id="menu_delete"
                          label="&deleteCmd.label;"
                          key="key_delete"
                          accesskey="&deleteCmd.accesskey;"
                          command="cmd_delete"/>
                <menuseparator/>
                <menuitem id="menu_selectAll"
                          label="&selectAllCmd.label;"
                          key="key_selectAll"
                          accesskey="&selectAllCmd.accesskey;"
                          command="cmd_selectAll"/>
                <menuseparator/>
                <menuitem id="menu_find"
                          label="&findOnCmd.label;"
                          accesskey="&findOnCmd.accesskey;"
                          key="key_find"
                          command="cmd_find"/>
                <menuitem id="menu_findAgain"
                          class="show-only-for-keyboard"
                          label="&findAgainCmd.label;"
                          accesskey="&findAgainCmd.accesskey;"
                          key="key_findAgain"
                          command="cmd_findAgain"/>
                <menuseparator hidden="true" id="textfieldDirection-separator"/>
                <menuitem id="textfieldDirection-swap"
                          command="cmd_switchTextDirection"
                          key="key_switchTextDirection"
                          label="&bidiSwitchTextDirectionItem.label;"
                          accesskey="&bidiSwitchTextDirectionItem.accesskey;"
                          hidden="true"/>
#ifdef XP_UNIX
#ifndef XP_MACOSX
                <menuseparator/>
                <menuitem id="menu_preferences"
                          label="&preferencesCmdUnix.label;"
                          accesskey="&preferencesCmdUnix.accesskey;"
                          oncommand="openPreferences();"/>
#endif
#endif
              </menupopup>
            </menu>

            <menu id="view-menu" label="&viewMenu.label;"
                  accesskey="&viewMenu.accesskey;">
              <menupopup id="menu_viewPopup"
                         onpopupshowing="updateCharacterEncodingMenuState();">
                <menu id="viewToolbarsMenu"
                      label="&viewToolbarsMenu.label;"
                      accesskey="&viewToolbarsMenu.accesskey;">
                  <menupopup onpopupshowing="onViewToolbarsPopupShowing(event);">
                    <menuseparator/>
                    <menuitem id="menu_tabsOnTop"
                              command="cmd_ToggleTabsOnTop"
                              type="checkbox"
                              label="&viewTabsOnTop.label;"
                              accesskey="&viewTabsOnTop.accesskey;"/>
                    <menuitem id="menu_customizeToolbars"
                              label="&viewCustomizeToolbar.label;"
                              accesskey="&viewCustomizeToolbar.accesskey;"
                              command="cmd_CustomizeToolbars"/>
                  </menupopup>
                </menu>
                <menu id="viewSidebarMenuMenu"
                      label="&viewSidebarMenu.label;"
                      accesskey="&viewSidebarMenu.accesskey;">
                  <menupopup id="viewSidebarMenu">
                    <menuitem id="menu_bookmarksSidebar"
                              key="viewBookmarksSidebarKb"
                              observes="viewBookmarksSidebar"
                              label="&bookmarksButton.label;"/>
                    <menuitem id="menu_historySidebar"
                              key="key_gotoHistory"
                              observes="viewHistorySidebar"
                              label="&historyButton.label;"/>
                    <menuitem id="menu_tabsSidebar"
                              observes="viewTabsSidebar"
                              label="&syncedTabs.sidebar.label;"/>
                  </menupopup>
                </menu>
                <menuseparator/>
                <menuitem id="menu_stop"
                          class="show-only-for-keyboard"
                          label="&stopCmd.label;"
                          accesskey="&stopCmd.accesskey;"
                          command="Browser:Stop"
#ifdef XP_MACOSX
                          key="key_stop_mac"/>
#else
                          key="key_stop"/>
#endif
                <menuitem id="menu_reload"
                          class="show-only-for-keyboard"
                          label="&reloadCmd.label;"
                          accesskey="&reloadCmd.accesskey;"
                          key="key_reload"
                          command="Browser:ReloadOrDuplicate"
                          onclick="checkForMiddleClick(this, event);"/>
                <menuseparator class="show-only-for-keyboard"/>
                <menu id="viewFullZoomMenu" label="&fullZoom.label;"
                      accesskey="&fullZoom.accesskey;"
                      onpopupshowing="FullZoom.updateMenu();">
                  <menupopup>
                    <menuitem id="menu_zoomEnlarge"
                              key="key_fullZoomEnlarge"
                              label="&fullZoomEnlargeCmd.label;"
                              accesskey="&fullZoomEnlargeCmd.accesskey;"
                              command="cmd_fullZoomEnlarge"/>
                    <menuitem id="menu_zoomReduce"
                              key="key_fullZoomReduce"
                              label="&fullZoomReduceCmd.label;"
                              accesskey="&fullZoomReduceCmd.accesskey;"
                              command="cmd_fullZoomReduce"/>
                    <menuseparator/>
                    <menuitem id="menu_zoomReset"
                              key="key_fullZoomReset"
                              label="&fullZoomResetCmd.label;"
                              accesskey="&fullZoomResetCmd.accesskey;"
                              command="cmd_fullZoomReset"/>
                    <menuseparator/>
                    <menuitem id="toggle_zoom"
                              label="&fullZoomToggleCmd.label;"
                              accesskey="&fullZoomToggleCmd.accesskey;"
                              type="checkbox"
                              command="cmd_fullZoomToggle"
                              checked="false"/>
                  </menupopup>
                </menu>
                <menu id="pageStyleMenu" label="&pageStyleMenu.label;"
                      accesskey="&pageStyleMenu.accesskey;" observes="isImage">
                  <menupopup onpopupshowing="gPageStyleMenu.fillPopup(this);">
                    <menuitem id="menu_pageStyleNoStyle"
                              label="&pageStyleNoStyle.label;"
                              accesskey="&pageStyleNoStyle.accesskey;"
                              oncommand="gPageStyleMenu.disableStyle();"
                              type="radio"/>
                    <menuitem id="menu_pageStylePersistentOnly"
                              label="&pageStylePersistentOnly.label;"
                              accesskey="&pageStylePersistentOnly.accesskey;"
                              oncommand="gPageStyleMenu.switchStyleSheet('');"
                              type="radio"
                              checked="true"/>
                    <menuseparator/>
                  </menupopup>
                </menu>
#include browser-charsetmenu.inc
                <menuseparator/>
#ifdef XP_MACOSX
                <menuitem id="enterFullScreenItem"
                          accesskey="&enterFullScreenCmd.accesskey;"
                          label="&enterFullScreenCmd.label;"
                          key="key_fullScreen">
                  <observes element="View:FullScreen" attribute="oncommand"/>
                  <observes element="View:FullScreen" attribute="disabled"/>
                </menuitem>
                <menuitem id="exitFullScreenItem"
                          accesskey="&exitFullScreenCmd.accesskey;"
                          label="&exitFullScreenCmd.label;"
                          key="key_fullScreen"
                          hidden="true">
                  <observes element="View:FullScreen" attribute="oncommand"/>
                  <observes element="View:FullScreen" attribute="disabled"/>
                </menuitem>
#else
                <menuitem id="fullScreenItem"
                          accesskey="&fullScreenCmd.accesskey;"
                          label="&fullScreenCmd.label;"
                          key="key_fullScreen"
                          type="checkbox"
                          observes="View:FullScreen"/>
#endif
                <menuitem id="menu_showAllTabs"
                          hidden="true"
                          accesskey="&showAllTabsCmd.accesskey;"
                          label="&showAllTabsCmd.label;"
                          command="Browser:ShowAllTabs"
                          key="key_showAllTabs"/>
                <menuseparator hidden="true" id="documentDirection-separator"/>
                <menuitem id="documentDirection-swap"
                          hidden="true"
                          label="&bidiSwitchPageDirectionItem.label;"
                          accesskey="&bidiSwitchPageDirectionItem.accesskey;"
                          oncommand="gBrowser.selectedBrowser
                                             .messageManager
                                             .sendAsyncMessage('SwitchDocumentDirection');"/>
              </menupopup>
            </menu>

            <menu id="history-menu"
                  label="&historyMenu.label;"
                  accesskey="&historyMenu.accesskey;">
              <menupopup id="goPopup"
#ifndef XP_MACOSX
                         placespopup="true"
#endif
                         context="placesContext"
                         oncommand="this.parentNode._placesView._onCommand(event);"
                         onclick="checkForMiddleClick(this, event);"
                         onpopupshowing="if (!this.parentNode._placesView)
                                           new HistoryMenu(event);"
                         tooltip="bhTooltip"
                         popupsinherittooltip="true">
                <menuitem id="historyMenuBack"
                          class="show-only-for-keyboard"
                          label="&backCmd.label;"
#ifdef XP_MACOSX
                          key="goBackKb2"
#else
                          key="goBackKb"
#endif
                          command="Browser:BackOrBackDuplicate"
                          onclick="checkForMiddleClick(this, event);"/>
                <menuitem id="historyMenuForward"
                          class="show-only-for-keyboard"
                          label="&forwardCmd.label;"
#ifdef XP_MACOSX
                          key="goForwardKb2"
#else
                          key="goForwardKb"
#endif
                          command="Browser:ForwardOrForwardDuplicate"
                          onclick="checkForMiddleClick(this, event);"/>
                <menuitem id="historyMenuHome"
                          class="show-only-for-keyboard"
                          label="&historyHomeCmd.label;"
                          oncommand="BrowserGoHome(event);"
                          onclick="checkForMiddleClick(this, event);"
                          key="goHome"/>
                <menuseparator id="historyMenuHomeSeparator"
                               class="show-only-for-keyboard"/>
                <menuitem id="menu_showAllHistory"
                          label="&showAllHistoryCmd2.label;"
#ifndef XP_MACOSX
                          class="menuitem-iconic"
                          key="showAllHistoryKb"
#endif
                          command="Browser:ShowAllHistory"/>
                <menuitem id="sanitizeItem"
#ifndef XP_MACOSX
                          class="menuitem-iconic"
#endif
                          label="&clearRecentHistory.label;"
                          key="key_sanitize"
                          command="Tools:Sanitize"/>
                <menuseparator id="sanitizeSeparator"/>
                <menuitem id="sync-tabs-menuitem"
                          class="syncTabsMenuItem"
                          label="&syncTabsMenu2.label;"
                          oncommand="BrowserOpenSyncTabs();"
                          disabled="true"/>
                <menuitem id="historyRestoreLastSession"
                          label="&historyRestoreLastSession.label;"
                          command="Browser:RestoreLastSession"/>
                <menu id="historyUndoMenu"
                      class="recentlyClosedTabsMenu"
                      label="&historyUndoMenu.label;"
                      disabled="true">
                  <menupopup id="historyUndoPopup"
#ifndef XP_MACOSX
                             placespopup="true"
#endif
                             onpopupshowing="document.getElementById('history-menu')._placesView.populateUndoSubmenu();"/>
                </menu>
                <menu id="historyUndoWindowMenu"
                      class="recentlyClosedWindowsMenu"
                      label="&historyUndoWindowMenu.label;"
                      disabled="true">
                  <menupopup id="historyUndoWindowPopup"
#ifndef XP_MACOSX
                             placespopup="true"
#endif
                             onpopupshowing="document.getElementById('history-menu')._placesView.populateUndoWindowSubmenu();"/>
                </menu>
                <menuseparator id="startHistorySeparator"
                               class="hide-if-empty-places-result"/>
              </menupopup>
            </menu>

  <menu id="bookmarksMenu"
        label="&bookmarksMenu.label;"
        accesskey="&bookmarksMenu.accesskey;"
        ondragenter="PlacesMenuDNDHandler.onDragEnter(event);"
        ondragover="PlacesMenuDNDHandler.onDragOver(event);"
        ondrop="PlacesMenuDNDHandler.onDrop(event);">
    <menupopup id="bookmarksMenuPopup"
#ifndef XP_MACOSX
               placespopup="true"
#endif
               context="placesContext"
               openInTabs="children"
               oncommand="BookmarksEventHandler.onCommand(event, this.parentNode._placesView);"
               onclick="BookmarksEventHandler.onClick(event, this.parentNode._placesView);"
               onpopupshowing="BookmarkingUI.onMainMenuPopupShowing(event);
                               if (!this.parentNode._placesView)
                                 new PlacesMenu(event, 'place:folder=BOOKMARKS_MENU');"
               tooltip="bhTooltip" popupsinherittooltip="true">
      <menuitem id="bookmarksShowAll"
#ifndef XP_MACOSX
                class="menuitem-iconic"
#endif
                label="&organizeBookmarks.label;"
                command="Browser:ShowAllBookmarks"
                key="manBookmarkKb"/>
      <menuseparator id="organizeBookmarksSeparator"/>
      <menuitem id="menu_bookmarkThisPage"
#ifndef XP_MACOSX
                class="menuitem-iconic"
#endif
                command="Browser:AddBookmarkAs"
                observes="bookmarkThisPageBroadcaster"
                key="addBookmarkAsKb"/>
      <menuitem id="subscribeToPageMenuitem"
#ifndef XP_MACOSX
                class="menuitem-iconic"
#endif
                label="&subscribeToPageMenuitem.label;"
                oncommand="return FeedHandler.subscribeToFeed(null, event);"
                onclick="checkForMiddleClick(this, event);"
                observes="singleFeedMenuitemState"/>
      <menu id="subscribeToPageMenupopup"
#ifndef XP_MACOSX
            class="menu-iconic"
#endif
            label="&subscribeToPageMenupopup.label;"
            observes="multipleFeedsMenuState">
        <menupopup id="subscribeToPageSubmenuMenupopup"
                   onpopupshowing="return FeedHandler.buildFeedList(event.target);"
                   oncommand="return FeedHandler.subscribeToFeed(null, event);"
                   onclick="checkForMiddleClick(this, event);"/>
      </menu>
      <menuitem id="menu_bookmarkAllTabs"
                label="&addCurPagesCmd.label;"
                class="show-only-for-keyboard"
                command="Browser:BookmarkAllTabs"
                key="bookmarkAllTabsKb"/>
      <menuseparator/>
      <menuitem label="&recentBookmarks.label;"
                disabled="true"/>
      <vbox id="menu_recentBookmarks"/>
      <menuseparator id="bookmarksToolbarSeparator"/>
      <menu id="bookmarksToolbarFolderMenu"
            class="menu-iconic bookmark-item"
            label="&personalbarCmd.label;"
            container="true">
        <menupopup id="bookmarksToolbarFolderPopup"
#ifndef XP_MACOSX
                   placespopup="true"
#endif
                   context="placesContext"
                   onpopupshowing="if (!this.parentNode._placesView)
                                     new PlacesMenu(event, 'place:folder=TOOLBAR');"/>
      </menu>
      <menuseparator id="bookmarksMenuItemsSeparator"/>
      <!-- Bookmarks menu items -->
      <menuseparator builder="end"
                     class="hide-if-empty-places-result"/>
      <menuitem id="menu_unsortedBookmarks"
#ifndef XP_MACOSX
                class="menuitem-iconic"
#endif
                label="&unsortedBookmarksCmd.label;"
                oncommand="PlacesCommandHook.showPlacesOrganizer('UnfiledBookmarks');"/>
    </menupopup>
  </menu>

            <menu id="tools-menu"
                  label="&toolsMenu.label;"
                  accesskey="&toolsMenu.accesskey;"
                  onpopupshowing="mirrorShow(this)">
              <menupopup id="menu_ToolsPopup"
#ifdef MOZ_SERVICES_SYNC
# We have to use setTimeout() here to avoid a flickering menu bar when opening
# the Tools menu, see bug 970769. This can be removed once we got rid of the
# event loop spinning in Weave.Status._authManager.
                         onpopupshowing="setTimeout(() => gSyncUI.updateUI());"
#endif
                         >
              <menuitem id="menu_search"
                        class="show-only-for-keyboard"
                        label="&search.label;"
                        accesskey="&search.accesskey;"
                        key="key_search"
                        command="Tools:Search"/>
              <menuseparator id="browserToolsSeparator"
                             class="show-only-for-keyboard"/>
              <menuitem id="menu_openDownloads"
                        label="&downloads.label;"
                        accesskey="&downloads.accesskey;"
                        key="key_openDownloads"
                        command="Tools:Downloads"/>
              <menuitem id="menu_openAddons"
                        label="&addons.label;"
                        accesskey="&addons.accesskey;"
                        key="key_openAddons"
                        command="Tools:Addons"/>
              <menuitem id="menu_openPermissions"
                        label="&permissions.label;"
                        command="Tools:Permissions"
                        accesskey="&permissions.accesskey;"/>
              <!-- only one of sync-setup or sync-menu will be showing at once -->
              <menuitem id="sync-setup"
                        label="&syncSetup.label;"
                        accesskey="&syncSetup.accesskey;"
                        observes="sync-setup-state"
                        oncommand="gSyncUI.openSetup(null, 'menubar')"/>
              <menuitem id="sync-syncnowitem"
                        label="&syncSyncNowItem.label;"
                        accesskey="&syncSyncNowItem.accesskey;"
                        observes="sync-syncnow-state"
                        oncommand="gSyncUI.doSync(event);"/>
              <menuitem id="sync-reauthitem"
                        label="&syncReAuthItem.label;"
                        accesskey="&syncReAuthItem.accesskey;"
                        observes="sync-reauth-state"
                        oncommand="gSyncUI.openSignInAgainPage('menubar');"/>
              <menuseparator id="devToolsSeparator"/>
              <menu id="webDeveloperMenu"
                    label="&webDeveloperMenu.label;"
                    accesskey="&webDeveloperMenu.accesskey;">
                <menupopup id="menuWebDeveloperPopup">
                  <menuitem id="menu_pageSource"
                            observes="devtoolsMenuBroadcaster_PageSource"
                            accesskey="&pageSourceCmd.accesskey;"/>
                </menupopup>
              </menu>
              <menuitem id="menu_pageInfo"
                        accesskey="&pageInfoCmd.accesskey;"
                        label="&pageInfoCmd.label;"
#ifndef XP_WIN
                        key="key_viewInfo"
#endif
                        command="View:PageInfo"/>
              <menu id="menu_mirrorTabCmd"
                    hidden="true"
                    accesskey="&mirrorTabCmd.accesskey;"
                    label="&mirrorTabCmd.label;">
                <menupopup id="menu_mirrorTab-popup"
                           onpopupshowing="populateMirrorTabMenu(this)"/>
              </menu>
#ifndef XP_UNIX
              <menuseparator id="prefSep"/>
              <menuitem id="menu_preferences"
                        label="&preferencesCmd2.label;"
                        accesskey="&preferencesCmd2.accesskey;"
                        oncommand="openPreferences();"/>
#endif
              </menupopup>
            </menu>

#ifdef XP_MACOSX
          <menu id="windowMenu" />
#endif
          <menu id="helpMenu" />
        </menubar>

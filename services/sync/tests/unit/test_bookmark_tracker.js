/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

Cu.import("resource://gre/modules/PlacesUtils.jsm");
Cu.import("resource://services-sync/constants.js");
Cu.import("resource://services-sync/engines/bookmarks.js");
Cu.import("resource://services-sync/engines.js");
Cu.import("resource://services-sync/service.js");
Cu.import("resource://services-sync/util.js");
Cu.import("resource:///modules/PlacesUIUtils.jsm");

Service.engineManager.register(BookmarksEngine);
var engine = Service.engineManager.get("bookmarks");
var store  = engine._store;
var tracker = engine._tracker;

store.wipe();
tracker.persistChangedIDs = false;

function test_tracking() {
  _("Verify we've got an empty tracker to work with.");
  let tracker = engine._tracker;
  do_check_empty(tracker.changedIDs);

  let folder = PlacesUtils.bookmarks.createFolder(
    PlacesUtils.bookmarks.bookmarksMenuFolder,
    "Test Folder", PlacesUtils.bookmarks.DEFAULT_INDEX);
  function createBmk() {
    return PlacesUtils.bookmarks.insertBookmark(
      folder, Utils.makeURI("http://getfirefox.com"),
      PlacesUtils.bookmarks.DEFAULT_INDEX, "Get Firefox!");
  }

  try {
    _("Create bookmark. Won't show because we haven't started tracking yet");
    createBmk();
    do_check_empty(tracker.changedIDs);
    do_check_eq(tracker.score, 0);

    _("Tell the tracker to start tracking changes.");
    Svc.Obs.notify("weave:engine:start-tracking");
    createBmk();
    // We expect two changed items because the containing folder
    // changed as well (new child).
    do_check_attribute_count(tracker.changedIDs, 2);
    do_check_eq(tracker.score, SCORE_INCREMENT_XLARGE * 2);

    _("Notifying twice won't do any harm.");
    Svc.Obs.notify("weave:engine:start-tracking");
    createBmk();
    do_check_attribute_count(tracker.changedIDs, 3);
    do_check_eq(tracker.score, SCORE_INCREMENT_XLARGE * 4);

    _("Let's stop tracking again.");
    tracker.clearChangedIDs();
    tracker.resetScore();
    Svc.Obs.notify("weave:engine:stop-tracking");
    createBmk();
    do_check_empty(tracker.changedIDs);
    do_check_eq(tracker.score, 0);

    _("Notifying twice won't do any harm.");
    Svc.Obs.notify("weave:engine:stop-tracking");
    createBmk();
    do_check_empty(tracker.changedIDs);
    do_check_eq(tracker.score, 0);

  } finally {
    _("Clean up.");
    store.wipe();
    tracker.clearChangedIDs();
    tracker.resetScore();
    Svc.Obs.notify("weave:engine:stop-tracking");
  }
}

function test_onItemChanged() {
  // Anno that's in ANNOS_TO_TRACK.
  const DESCRIPTION_ANNO = "bookmarkProperties/description";

  _("Verify we've got an empty tracker to work with.");
  let tracker = engine._tracker;
  do_check_empty(tracker.changedIDs);
  do_check_eq(tracker.score, 0);

  try {
    Svc.Obs.notify("weave:engine:stop-tracking");
    let folder = PlacesUtils.bookmarks.createFolder(
      PlacesUtils.bookmarks.bookmarksMenuFolder, "Parent",
      PlacesUtils.bookmarks.DEFAULT_INDEX);
    _("Track changes to annos.");
    let b = PlacesUtils.bookmarks.insertBookmark(
      folder, Utils.makeURI("http://getfirefox.com"),
      PlacesUtils.bookmarks.DEFAULT_INDEX, "Get Firefox!");
    let bGUID = engine._store.GUIDForId(b);
    _("New item is " + b);
    _("GUID: " + bGUID);

    Svc.Obs.notify("weave:engine:start-tracking");
    PlacesUtils.annotations.setItemAnnotation(
      b, DESCRIPTION_ANNO, "A test description", 0,
      PlacesUtils.annotations.EXPIRE_NEVER);
    do_check_true(tracker.changedIDs[bGUID] > 0);
    do_check_eq(tracker.score, SCORE_INCREMENT_XLARGE);

  } finally {
    _("Clean up.");
    store.wipe();
    tracker.clearChangedIDs();
    tracker.resetScore();
    Svc.Obs.notify("weave:engine:stop-tracking");
  }
}

function test_onItemMoved() {
  _("Verify we've got an empty tracker to work with.");
  let tracker = engine._tracker;
  do_check_empty(tracker.changedIDs);
  do_check_eq(tracker.score, 0);

  try {
    let fx_id = PlacesUtils.bookmarks.insertBookmark(
      PlacesUtils.bookmarks.bookmarksMenuFolder,
      Utils.makeURI("http://getfirefox.com"),
      PlacesUtils.bookmarks.DEFAULT_INDEX,
      "Get Firefox!");
    let fx_guid = engine._store.GUIDForId(fx_id);
    let tb_id = PlacesUtils.bookmarks.insertBookmark(
      PlacesUtils.bookmarks.bookmarksMenuFolder,
      Utils.makeURI("http://getthunderbird.com"),
      PlacesUtils.bookmarks.DEFAULT_INDEX,
      "Get Thunderbird!");
    let tb_guid = engine._store.GUIDForId(tb_id);

    Svc.Obs.notify("weave:engine:start-tracking");

    // Moving within the folder will just track the folder.
    PlacesUtils.bookmarks.moveItem(
      tb_id, PlacesUtils.bookmarks.bookmarksMenuFolder, 0);
    do_check_true(tracker.changedIDs['menu'] > 0);
    do_check_eq(tracker.changedIDs['toolbar'], undefined);
    do_check_eq(tracker.changedIDs[fx_guid], undefined);
    do_check_eq(tracker.changedIDs[tb_guid], undefined);
    do_check_eq(tracker.score, SCORE_INCREMENT_XLARGE);
    tracker.clearChangedIDs();
    tracker.resetScore();

    // Moving a bookmark to a different folder will track the old
    // folder, the new folder and the bookmark.
    PlacesUtils.bookmarks.moveItem(tb_id, PlacesUtils.bookmarks.toolbarFolder,
                                   PlacesUtils.bookmarks.DEFAULT_INDEX);
    do_check_true(tracker.changedIDs['menu'] > 0);
    do_check_true(tracker.changedIDs['toolbar'] > 0);
    do_check_eq(tracker.changedIDs[fx_guid], undefined);
    do_check_true(tracker.changedIDs[tb_guid] > 0);
    do_check_eq(tracker.score, SCORE_INCREMENT_XLARGE * 3);

  } finally {
    _("Clean up.");
    store.wipe();
    tracker.clearChangedIDs();
    tracker.resetScore();
    Svc.Obs.notify("weave:engine:stop-tracking");
  }

}

add_task(function* test_mobile_query() {
  _("Ensure we correctly create the mobile query");

  try {
    // Creates the organizer queries as a side effect.
    let leftPaneId = PlacesUIUtils.leftPaneFolderId;
    _(`Left pane root ID: ${leftPaneId}`);

    let allBookmarksIds = findAnnoItems("PlacesOrganizer/OrganizerQuery", "AllBookmarks");
    equal(allBookmarksIds.length, 1, "Should create folder with all bookmarks queries");
    let allBookmarkGuid = yield PlacesUtils.promiseItemGuid(allBookmarksIds[0]);

    _("Try creating query after organizer is ready");
    tracker._ensureMobileQuery();
    let queryIds = findAnnoItems("PlacesOrganizer/OrganizerQuery", "MobileBookmarks");
    equal(queryIds.length, 0, "Should not create query without any mobile bookmarks");

    _("Insert mobile bookmark, then create query");
    yield PlacesUtils.bookmarks.insert({
      parentGuid: PlacesUtils.bookmarks.mobileGuid,
      url: "https://mozilla.org",
    });
    tracker._ensureMobileQuery();
    queryIds = findAnnoItems("PlacesOrganizer/OrganizerQuery", "MobileBookmarks", {});
    equal(queryIds.length, 1, "Should create query once mobile bookmarks exist");

    let queryId = queryIds[0];
    let queryGuid = yield PlacesUtils.promiseItemGuid(queryId);

    let queryInfo = yield PlacesUtils.bookmarks.fetch(queryGuid);
    equal(queryInfo.url, `place:folder=${PlacesUtils.mobileFolderId}`, "Query should point to mobile root");
    equal(queryInfo.title, "Mobile Bookmarks", "Query title should be localized");
    equal(queryInfo.parentGuid, allBookmarkGuid, "Should append mobile query to all bookmarks queries");

    _("Rename root and query, then recreate");
    yield PlacesUtils.bookmarks.update({
      guid: PlacesUtils.bookmarks.mobileGuid,
      title: "renamed root",
    });
    yield PlacesUtils.bookmarks.update({
      guid: queryGuid,
      title: "renamed query",
    });
    tracker._ensureMobileQuery();
    let rootInfo = yield PlacesUtils.bookmarks.fetch(PlacesUtils.bookmarks.mobileGuid);
    equal(rootInfo.title, "Mobile Bookmarks", "Should fix root title");
    queryInfo = yield PlacesUtils.bookmarks.fetch(queryGuid);
    equal(queryInfo.title, "Mobile Bookmarks", "Should fix query title");

    _("We shouldn't track the query or the left pane root");
    yield verifyTrackedCount(0);
    do_check_eq(tracker.score, 0);
  } finally {
    _("Clean up.");
    yield cleanup();
  }
});

function run_test() {
  initTestLogging("Trace");

  Log.repository.getLogger("Sync.Engine.Bookmarks").level = Log.Level.Trace;
  Log.repository.getLogger("Sync.Store.Bookmarks").level = Log.Level.Trace;
  Log.repository.getLogger("Sync.Tracker.Bookmarks").level = Log.Level.Trace;

  test_tracking();
  test_onItemChanged();
  test_onItemMoved();
}


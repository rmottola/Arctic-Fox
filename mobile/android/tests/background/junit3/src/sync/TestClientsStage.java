/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

package org.mozilla.goanna.background.sync;

import org.json.simple.JSONArray;
import org.mozilla.goanna.background.helpers.AndroidSyncTestCase;
import org.mozilla.goanna.background.testhelpers.DefaultGlobalSessionCallback;
import org.mozilla.goanna.background.testhelpers.MockClientsDataDelegate;
import org.mozilla.goanna.background.testhelpers.MockSharedPreferences;
import org.mozilla.goanna.sync.GlobalSession;
import org.mozilla.goanna.sync.SyncConfiguration;
import org.mozilla.goanna.sync.crypto.KeyBundle;
import org.mozilla.goanna.sync.delegates.ClientsDataDelegate;
import org.mozilla.goanna.sync.delegates.GlobalSessionCallback;
import org.mozilla.goanna.sync.net.AuthHeaderProvider;
import org.mozilla.goanna.sync.net.BasicAuthHeaderProvider;
import org.mozilla.goanna.sync.repositories.android.ClientsDatabaseAccessor;
import org.mozilla.goanna.sync.repositories.domain.ClientRecord;
import org.mozilla.goanna.sync.stage.SyncClientsEngineStage;

import android.content.Context;
import android.content.SharedPreferences;

public class TestClientsStage extends AndroidSyncTestCase {
  private static final String TEST_USERNAME    = "johndoe";
  private static final String TEST_PASSWORD    = "password";
  private static final String TEST_SYNC_KEY    = "abcdeabcdeabcdeabcdeabcdea";

  @Override
  public void setUp() {
    ClientsDatabaseAccessor db = new ClientsDatabaseAccessor(getApplicationContext());
    db.wipeDB();
    db.close();
  }

  public void testWipeClearsClients() throws Exception {

    // Wiping clients is equivalent to a reset and dropping all local stored client records.
    // Resetting is defined as being the same as for other engines -- discard local
    // and remote timestamps, tracked failed records, and tracked records to fetch.

    final Context context = getApplicationContext();
    final ClientsDatabaseAccessor dataAccessor = new ClientsDatabaseAccessor(context);
    final GlobalSessionCallback callback = new DefaultGlobalSessionCallback();
    final ClientsDataDelegate delegate = new MockClientsDataDelegate();

    final KeyBundle keyBundle = new KeyBundle(TEST_USERNAME, TEST_SYNC_KEY);
    final AuthHeaderProvider authHeaderProvider = new BasicAuthHeaderProvider(TEST_USERNAME, TEST_PASSWORD);
    final SharedPreferences prefs = new MockSharedPreferences();
    final SyncConfiguration config = new SyncConfiguration(TEST_USERNAME, authHeaderProvider, prefs);
    config.syncKeyBundle = keyBundle;
    GlobalSession session = new GlobalSession(config, callback, context, delegate, callback);

    SyncClientsEngineStage stage = new SyncClientsEngineStage() {

      @Override
      public synchronized ClientsDatabaseAccessor getClientsDatabaseAccessor() {
        if (db == null) {
          db = dataAccessor;
        }
        return db;
      }
    };

    final String guid = "clientabcdef";
    long lastModified = System.currentTimeMillis();
    ClientRecord record = new ClientRecord(guid, "clients", lastModified , false);
    record.name = "John's Phone";
    record.type = "mobile";
    record.device = "Some Device";
    record.os = "iOS";
    record.commands = new JSONArray();

    dataAccessor.store(record);
    assertEquals(1, dataAccessor.clientsCount());

    final ClientRecord stored = dataAccessor.fetchAllClients().get(guid);
    assertNotNull(stored);
    assertEquals("John's Phone", stored.name);
    assertEquals("mobile", stored.type);
    assertEquals("Some Device", stored.device);
    assertEquals("iOS", stored.os);

    stage.wipeLocal(session);

    try {
      assertEquals(0, dataAccessor.clientsCount());
      assertEquals(0L, session.config.getPersistedServerClientRecordTimestamp());
      assertEquals(0, session.getClientsDelegate().getClientsCount());
    } finally {
      dataAccessor.close();
    }
  }
}

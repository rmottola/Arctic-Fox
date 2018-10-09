/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

package org.mozilla.goanna.background.testhelpers;

import java.io.IOException;

import org.json.simple.parser.ParseException;
import org.mozilla.goanna.sync.GlobalSession;
import org.mozilla.goanna.sync.NonObjectJSONException;
import org.mozilla.goanna.sync.SyncConfiguration;
import org.mozilla.goanna.sync.SyncConfigurationException;
import org.mozilla.goanna.sync.crypto.KeyBundle;
import org.mozilla.goanna.sync.delegates.ClientsDataDelegate;
import org.mozilla.goanna.sync.delegates.GlobalSessionCallback;
import org.mozilla.goanna.sync.net.AuthHeaderProvider;
import org.mozilla.goanna.sync.net.BasicAuthHeaderProvider;

import android.content.Context;
import android.content.SharedPreferences;

/**
 * GlobalSession touches the Android prefs system. Stub that out.
 */
public class MockPrefsGlobalSession extends GlobalSession {

  public MockSharedPreferences prefs;

  public MockPrefsGlobalSession(
      SyncConfiguration config, GlobalSessionCallback callback, Context context,
      ClientsDataDelegate clientsDelegate)
      throws SyncConfigurationException, IllegalArgumentException, IOException,
      ParseException, NonObjectJSONException {
    super(config, callback, context, clientsDelegate, callback);
  }

  public static MockPrefsGlobalSession getSession(
      String username, String password,
      KeyBundle syncKeyBundle, GlobalSessionCallback callback, Context context,
      ClientsDataDelegate clientsDelegate)
      throws SyncConfigurationException, IllegalArgumentException, IOException,
      ParseException, NonObjectJSONException {
    return getSession(username, new BasicAuthHeaderProvider(username, password), null,
         syncKeyBundle, callback, context, clientsDelegate);
  }

  public static MockPrefsGlobalSession getSession(
      String username, AuthHeaderProvider authHeaderProvider, String prefsPath,
      KeyBundle syncKeyBundle, GlobalSessionCallback callback, Context context,
      ClientsDataDelegate clientsDelegate)
      throws SyncConfigurationException, IllegalArgumentException, IOException,
      ParseException, NonObjectJSONException {

    final SharedPreferences prefs = new MockSharedPreferences();
    final SyncConfiguration config = new SyncConfiguration(username, authHeaderProvider, prefs);
    config.syncKeyBundle = syncKeyBundle;
    return new MockPrefsGlobalSession(config, callback, context, clientsDelegate);
  }

  @Override
  public Context getContext() {
    return null;
  }
}

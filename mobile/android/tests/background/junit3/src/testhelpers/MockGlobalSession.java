/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

package org.mozilla.goanna.background.testhelpers;

import java.io.IOException;
import java.util.HashMap;

import org.json.simple.parser.ParseException;
import org.mozilla.goanna.sync.EngineSettings;
import org.mozilla.goanna.sync.NonObjectJSONException;
import org.mozilla.goanna.sync.SyncConfiguration;
import org.mozilla.goanna.sync.SyncConfigurationException;
import org.mozilla.goanna.sync.crypto.KeyBundle;
import org.mozilla.goanna.sync.delegates.GlobalSessionCallback;
import org.mozilla.goanna.sync.net.BasicAuthHeaderProvider;
import org.mozilla.goanna.sync.stage.CompletedStage;
import org.mozilla.goanna.sync.stage.GlobalSyncStage;
import org.mozilla.goanna.sync.stage.GlobalSyncStage.Stage;


public class MockGlobalSession extends MockPrefsGlobalSession {

  public MockGlobalSession(String username, String password, KeyBundle keyBundle, GlobalSessionCallback callback) throws SyncConfigurationException, IllegalArgumentException, NonObjectJSONException, IOException, ParseException {
    this(new SyncConfiguration(username, new BasicAuthHeaderProvider(username, password), new MockSharedPreferences(), keyBundle), callback);
  }

  public MockGlobalSession(SyncConfiguration config, GlobalSessionCallback callback)
          throws SyncConfigurationException, IllegalArgumentException, IOException, ParseException, NonObjectJSONException {
    super(config, callback, null, null);
  }

  @Override
  public boolean isEngineRemotelyEnabled(String engine, EngineSettings engineSettings) {
    return false;
  }

  @Override
  protected void prepareStages() {
    super.prepareStages();
    HashMap<Stage, GlobalSyncStage> newStages = new HashMap<Stage, GlobalSyncStage>(this.stages);

    for (Stage stage : this.stages.keySet()) {
      newStages.put(stage, new MockServerSyncStage());
    }

    // This signals that the global session is complete.
    newStages.put(Stage.completed, new CompletedStage());

    this.stages = newStages;
  }

  public MockGlobalSession withStage(Stage stage, GlobalSyncStage syncStage) {
    stages.put(stage, syncStage);

    return this;
  }
}

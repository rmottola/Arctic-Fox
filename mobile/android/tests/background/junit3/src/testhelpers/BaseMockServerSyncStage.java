/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

package org.mozilla.goanna.background.testhelpers;

import java.io.IOException;
import java.net.URISyntaxException;

import org.json.simple.parser.ParseException;
import org.mozilla.goanna.sync.NoCollectionKeysSetException;
import org.mozilla.goanna.sync.NonObjectJSONException;
import org.mozilla.goanna.sync.SynchronizerConfiguration;
import org.mozilla.goanna.sync.repositories.RecordFactory;
import org.mozilla.goanna.sync.repositories.Repository;
import org.mozilla.goanna.sync.stage.ServerSyncStage;

/**
 * A stage that joins two Repositories with no wrapping.
 */
public abstract class BaseMockServerSyncStage extends ServerSyncStage {

  public Repository local;
  public Repository remote;
  public String name;
  public String collection;
  public int version = 1;

  @Override
  public boolean isEnabled() {
    return true;
  }

  @Override
  protected String getCollection() {
    return collection;
  }

  @Override
  protected Repository getLocalRepository() {
    return local;
  }

  @Override
  protected Repository getRemoteRepository() throws URISyntaxException {
    return remote;
  }

  @Override
  protected String getEngineName() {
    return name;
  }

  @Override
  public Integer getStorageVersion() {
    return version;
  }

  @Override
  protected RecordFactory getRecordFactory() {
    return null;
  }

  @Override
  protected Repository wrappedServerRepo()
  throws NoCollectionKeysSetException, URISyntaxException {
    return getRemoteRepository();
  }

  public SynchronizerConfiguration leakConfig()
  throws NonObjectJSONException, IOException, ParseException {
    return this.getConfig();
  }
}

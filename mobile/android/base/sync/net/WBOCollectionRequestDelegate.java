/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.goanna.sync.net;

import org.mozilla.goanna.sync.crypto.KeyBundle;
import org.mozilla.goanna.sync.CryptoRecord;
import org.mozilla.goanna.sync.KeyBundleProvider;

/**
 * Subclass this to handle collection fetches.
 * @author rnewman
 *
 */
public abstract class WBOCollectionRequestDelegate
extends SyncStorageCollectionRequestDelegate
implements KeyBundleProvider {

  @Override
  public abstract KeyBundle keyBundle();
  public abstract void handleWBO(CryptoRecord record);

  @Override
  public void handleRequestProgress(String progress) {
    try {
      CryptoRecord record = CryptoRecord.fromJSONRecord(progress);
      record.keyBundle = this.keyBundle();
      this.handleWBO(record);
    } catch (Exception e) {
      this.handleRequestError(e);
      // TODO: abort?! Allow exception to propagate to fail?
    }
  }
}

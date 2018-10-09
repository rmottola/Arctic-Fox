/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.goanna.sync.net;

import org.mozilla.goanna.sync.KeyBundleProvider;
import org.mozilla.goanna.sync.crypto.KeyBundle;

public abstract class WBORequestDelegate
implements SyncStorageRequestDelegate, KeyBundleProvider {
  @Override
  public abstract KeyBundle keyBundle();
}

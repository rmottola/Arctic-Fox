/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.goanna.sync.synchronizer;

import org.mozilla.goanna.sync.SyncException;
import org.mozilla.goanna.sync.repositories.RepositorySession;

public class SessionNotBegunException extends SyncException {
  
  public RepositorySession failed;

  public SessionNotBegunException(RepositorySession failed) {
    this.failed = failed;
  }

  private static final long serialVersionUID = -4565241449897072841L;
}

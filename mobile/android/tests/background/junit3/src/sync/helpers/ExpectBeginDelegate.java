/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

package org.mozilla.goanna.background.sync.helpers;

import static junit.framework.Assert.assertNotNull;
import junit.framework.AssertionFailedError;

import org.mozilla.goanna.sync.repositories.RepositorySession;

public class ExpectBeginDelegate extends DefaultBeginDelegate {
  @Override
  public void onBeginSucceeded(RepositorySession session) {
    try {
      assertNotNull(session);
    } catch (AssertionFailedError e) {
      performNotify("Expected non-null session", e);
      return;
    }
    performNotify();
  }
}

/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

package org.mozilla.goanna.background.sync.helpers;

import org.mozilla.goanna.sync.repositories.delegates.RepositorySessionGuidsSinceDelegate;

public class DefaultGuidsSinceDelegate extends DefaultDelegate implements RepositorySessionGuidsSinceDelegate {

  @Override
  public void onGuidsSinceFailed(Exception ex) {
    performNotify("shouldn't fail", ex);
  }

  @Override
  public void onGuidsSinceSucceeded(String[] guids) {
    performNotify("default guids since delegate called", null);
  }
}

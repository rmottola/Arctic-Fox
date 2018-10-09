/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

package org.mozilla.goanna.background.sync.helpers;

import org.mozilla.goanna.sync.repositories.InvalidSessionTransitionException;

public class ExpectBeginFailDelegate extends DefaultBeginDelegate {

  @Override
  public void onBeginFailed(Exception ex) {
    if (!(ex instanceof InvalidSessionTransitionException)) {
      performNotify("Expected InvalidSessionTransititionException but got ", ex);
    }
  }
}

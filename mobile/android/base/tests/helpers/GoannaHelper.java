/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.goanna.tests.helpers;

import org.mozilla.goanna.Actions;
import org.mozilla.goanna.Actions.EventExpecter;
import org.mozilla.goanna.GoannaThread;
import org.mozilla.goanna.GoannaThread.LaunchState;
import org.mozilla.goanna.tests.UITestContext;

import android.app.Activity;

/**
 * Provides helper functions for accessing the underlying Goanna engine.
 */
public final class GoannaHelper {
    private static Activity sActivity;
    private static Actions sActions;

    private GoannaHelper() { /* To disallow instantiation. */ }

    protected static void init(final UITestContext context) {
        sActivity = context.getActivity();
        sActions = context.getActions();
    }

    public static void blockForReady() {
        blockForEvent("Goanna:Ready");
    }

    /**
     * Blocks for the "Goanna:DelayedStartup" event, which occurs after "Goanna:Ready" and the
     * first page load.
     */
    public static void blockForDelayedStartup() {
        blockForEvent("Goanna:DelayedStartup");
    }

    private static void blockForEvent(final String eventName) {
        final EventExpecter eventExpecter = sActions.expectGoannaEvent(eventName);

        final boolean isRunning = GoannaThread.checkLaunchState(LaunchState.GoannaRunning);
        if (!isRunning) {
            eventExpecter.blockForEvent();
        }

        eventExpecter.unregisterListener();
    }
}

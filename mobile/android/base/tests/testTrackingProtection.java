/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.goanna.tests;

import static org.mozilla.goanna.tests.helpers.AssertionHelper.fFail;

import org.mozilla.goanna.EventDispatcher;
import org.mozilla.goanna.GoannaAppShell;
import org.mozilla.goanna.GoannaEvent;
import org.mozilla.goanna.util.GoannaEventListener;

import org.json.JSONException;
import org.json.JSONObject;

public class testTrackingProtection extends JavascriptTest implements GoannaEventListener {
    private String mLastTracking;

    public testTrackingProtection() {
        super("testTrackingProtection.js");
    }

    @Override
    public void handleMessage(String event, final JSONObject message) {
        if (event.equals("Content:SecurityChange")) {
            try {
                JSONObject identity = message.getJSONObject("identity");
                JSONObject mode = identity.getJSONObject("mode");
                mLastTracking = mode.getString("tracking");
                mAsserter.dumpLog("Security change (tracking): " + mLastTracking);
            } catch (Exception e) {
                fFail("Can't extract tracking state from JSON");
            }
        }

        if (event.equals("Test:Expected")) {
            try {
                String expected = message.getString("expected");
                mAsserter.is(mLastTracking, expected, "Tracking matched expectation");
                mAsserter.dumpLog("Testing (tracking): " + mLastTracking + " = " + expected);
            } catch (Exception e) {
                fFail("Can't extract expected state from JSON");
            }
        }
    }

    @Override
    public void setUp() throws Exception {
        super.setUp();

        EventDispatcher.getInstance().registerGoannaThreadListener(this, "Content:SecurityChange");
        EventDispatcher.getInstance().registerGoannaThreadListener(this, "Test:Expected");
    }

    @Override
    public void tearDown() throws Exception {
        super.tearDown();

        EventDispatcher.getInstance().unregisterGoannaThreadListener(this, "Content:SecurityChange");
        EventDispatcher.getInstance().unregisterGoannaThreadListener(this, "Test:Expected");
    }
}

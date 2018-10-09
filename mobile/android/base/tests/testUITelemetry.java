package org.mozilla.goanna.tests;

import org.mozilla.goanna.AppConstants;
import org.mozilla.goanna.PrefsHelper;
import org.mozilla.goanna.Telemetry;
import org.mozilla.goanna.TelemetryContract.Event;
import org.mozilla.goanna.TelemetryContract.Method;
import org.mozilla.goanna.TelemetryContract.Reason;
import org.mozilla.goanna.TelemetryContract.Session;

import android.util.Log;

public class testUITelemetry extends JavascriptTest {
    public testUITelemetry() {
        super("testUITelemetry.js");
    }

    @Override
    public void testJavascript() throws Exception {
        blockForGoannaReady();

        // We can't run these tests unless telemetry is turned on --
        // the events will be dropped on the floor.
        Log.i("GoannaTest", "Enabling telemetry.");
        PrefsHelper.setPref(AppConstants.TELEMETRY_PREF_NAME, true);

        Log.i("GoannaTest", "Adding telemetry events.");
        try {
            Telemetry.sendUIEvent(Event._TEST1, Method._TEST1);
            Telemetry.startUISession(Session._TEST_STARTED_TWICE);
            Telemetry.sendUIEvent(Event._TEST2, Method._TEST1);

            // We can only start one session per name, so this call should be ignored.
            Telemetry.startUISession(Session._TEST_STARTED_TWICE);

            Telemetry.sendUIEvent(Event._TEST2, Method._TEST2);
            Telemetry.startUISession(Session._TEST_STOPPED_TWICE);
            Telemetry.sendUIEvent(Event._TEST3, Method._TEST1, "foobarextras");
            Telemetry.stopUISession(Session._TEST_STARTED_TWICE, Reason._TEST1);
            Telemetry.sendUIEvent(Event._TEST4, Method._TEST1, "barextras");
            Telemetry.stopUISession(Session._TEST_STOPPED_TWICE, Reason._TEST2);

            // This session is already stopped, so this call should be ignored.
            Telemetry.stopUISession(Session._TEST_STOPPED_TWICE, Reason._TEST_IGNORED);

            // Method defaults to Method.NONE
            Telemetry.sendUIEvent(Event._TEST1);
        } catch (Exception e) {
            Log.e("GoannaTest", "Oops.", e);
        }

        Log.i("GoannaTest", "Running remaining JS test code.");
        super.testJavascript();
    }
}


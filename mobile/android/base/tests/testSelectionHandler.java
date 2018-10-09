package org.mozilla.goanna.tests;

import org.mozilla.goanna.Actions;
import org.mozilla.goanna.EventDispatcher;
import org.mozilla.goanna.tests.helpers.GoannaHelper;
import org.mozilla.goanna.tests.helpers.NavigationHelper;

import android.util.Log;

import org.json.JSONObject;


public class testSelectionHandler extends UITest {

    public void testSelectionHandler() {
        GoannaHelper.blockForReady();

        Actions.EventExpecter robocopTestExpecter = getActions().expectGoannaEvent("Robocop:testSelectionHandler");
        final String url = "chrome://roboextender/content/testSelectionHandler.html";
        NavigationHelper.enterAndLoadUrl(url);
        mToolbar.assertTitle(url);

        while (!test(robocopTestExpecter)) {
            // do nothing
        }

        robocopTestExpecter.unregisterListener();
    }

    private boolean test(Actions.EventExpecter expecter) {
        final JSONObject eventData;
        try {
            eventData = new JSONObject(expecter.blockForEventData());
        } catch(Exception ex) {
            // Log and ignore
            getAsserter().ok(false, "JS Test", "Error decoding data " + ex);
            return false;
        }

        if (eventData.has("result")) {
            getAsserter().ok(eventData.optBoolean("result"), "JS Test", eventData.optString("msg"));
        }

        EventDispatcher.sendResponse(eventData, new JSONObject());
        return eventData.optBoolean("done", false);
    }
}

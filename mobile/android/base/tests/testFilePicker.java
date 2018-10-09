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

public class testFilePicker extends JavascriptTest implements GoannaEventListener {
    private static final String TEST_FILENAME = "/mnt/sdcard/my-favorite-martian.png";

    public testFilePicker() {
        super("testFilePicker.js");
    }

    @Override
    public void handleMessage(String event, final JSONObject message) {
        // We handle the FilePicker message here so we can send back hard coded file information. We
        // don't want to try to emulate "picking" a file using the Android intent chooser.
        if (event.equals("FilePicker:Show")) {
            try {
                message.put("file", TEST_FILENAME);
            } catch (JSONException ex) {
                fFail("Can't add filename to message " + TEST_FILENAME);
            }

            GoannaAppShell.sendEventToGoanna(GoannaEvent.createBroadcastEvent("FilePicker:Result", message.toString()));
        }
    }

    @Override
    public void setUp() throws Exception {
        super.setUp();

        EventDispatcher.getInstance().registerGoannaThreadListener(this, "FilePicker:Show");
    }

    @Override
    public void tearDown() throws Exception {
        super.tearDown();

        EventDispatcher.getInstance().unregisterGoannaThreadListener(this, "FilePicker:Show");
    }
}

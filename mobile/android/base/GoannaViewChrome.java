/* -*- Mode: Java; c-basic-offset: 4; tab-width: 20; indent-tabs-mode: nil; -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.goanna;

import android.os.Bundle;

public class GoannaViewChrome implements GoannaView.ChromeDelegate {
    /**
    * Tell the host application that Goanna is ready to handle requests.
    * @param view The GoannaView that initiated the callback.
    */
    @Override
    public void onReady(GoannaView view) {}

    /**
    * Tell the host application to display an alert dialog.
    * @param view The GoannaView that initiated the callback.
    * @param browser The Browser that is loading the content.
    * @param message The string to display in the dialog.
    * @param result A PromptResult used to send back the result without blocking.
    * Defaults to cancel requests.
    */
    @Override
    public void onAlert(GoannaView view, GoannaView.Browser browser, String message, GoannaView.PromptResult result) {
        result.cancel();
    }

    /**
    * Tell the host application to display a confirmation dialog.
    * @param view The GoannaView that initiated the callback.
    * @param browser The Browser that is loading the content.
    * @param message The string to display in the dialog.
    * @param result A PromptResult used to send back the result without blocking.
    * Defaults to cancel requests.
    */
    @Override
    public void onConfirm(GoannaView view, GoannaView.Browser browser, String message, GoannaView.PromptResult result) {
        result.cancel();
    }

    /**
    * Tell the host application to display an input prompt dialog.
    * @param view The GoannaView that initiated the callback.
    * @param browser The Browser that is loading the content.
    * @param message The string to display in the dialog.
    * @param defaultValue The string to use as default input.
    * @param result A PromptResult used to send back the result without blocking.
    * Defaults to cancel requests.
    */
    @Override
    public void onPrompt(GoannaView view, GoannaView.Browser browser, String message, String defaultValue, GoannaView.PromptResult result) {
        result.cancel();
    }

    /**
    * Tell the host application to display a remote debugging request dialog.
    * @param view The GoannaView that initiated the callback.
    * @param result A PromptResult used to send back the result without blocking.
    * Defaults to cancel requests.
    */
    @Override
    public void onDebugRequest(GoannaView view, GoannaView.PromptResult result) {
        result.cancel();
    }

    /**
    * Receive a message from an imported script.
    * @param view The GoannaView that initiated the callback.
    * @param data Bundle of data sent with the message. Never null.
    * @param result A MessageResult used to send back a response without blocking. Can be null.
    * Defaults to cancel requests with a failed response.
    */
    public void onScriptMessage(GoannaView view, Bundle data, GoannaView.MessageResult result) {
        if (result != null) {
            result.failure(null);
        }
    }
}

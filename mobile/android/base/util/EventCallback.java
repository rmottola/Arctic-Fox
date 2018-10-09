package org.mozilla.goanna.util;

import org.mozilla.goanna.mozglue.RobocopTarget;

/**
 * Callback interface for Goanna requests.
 *
 * For each instance of EventCallback, exactly one of sendResponse, sendError, or sendCancel
 * must be called to prevent observer leaks. If more than one send* method is called, or if a
 * single send method is called multiple times, an {@link IllegalStateException} will be thrown.
 */
@RobocopTarget
public interface EventCallback {
    /**
     * Sends a success response with the given data.
     *
     * @param response The response data to send to Goanna. Can be any of the types accepted by
     *                 JSONObject#put(String, Object).
     */
    public void sendSuccess(Object response);

    /**
     * Sends an error response with the given data.
     *
     * @param response The response data to send to Goanna. Can be any of the types accepted by
     *                 JSONObject#put(String, Object).
     */
    public void sendError(Object response);
}

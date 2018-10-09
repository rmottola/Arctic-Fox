/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.goanna.tests;

public class testAwesomebar extends BaseTest {
    public void testAwesomebar() {
        blockForGoannaReady();

        String url = getAbsoluteUrl(StringHelper.ROBOCOP_BLANK_PAGE_01_URL);
        inputAndLoadUrl(url);

        mDriver.setupScrollHandling();
        // Calculate where we should be dragging.
        int midX = mDriver.getGoannaLeft() + mDriver.getGoannaWidth()/2;
        int midY = mDriver.getGoannaTop() + mDriver.getGoannaHeight()/2;
        int endY = mDriver.getGoannaTop() + mDriver.getGoannaHeight()/10;
        for (int i = 0; i < 10; i++) {
            mActions.drag(midX, midX, midY, endY);
            try {
                Thread.sleep(200);
            } catch (InterruptedException e) {
                e.printStackTrace();
            }
        }

        verifyUrl(url);
    }
}

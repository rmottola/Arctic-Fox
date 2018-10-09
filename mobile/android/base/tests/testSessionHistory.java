package org.mozilla.goanna.tests;

import org.mozilla.goanna.tests.helpers.GoannaHelper;
import org.mozilla.goanna.tests.helpers.NavigationHelper;

/**
 * Tests that navigating through session history (ex: forward, back) sets the correct UI state.
 */
public class testSessionHistory extends UITest {
    public void testSessionHistory() {
        GoannaHelper.blockForReady();

        String url = StringHelper.ROBOCOP_BLANK_PAGE_01_URL;
        NavigationHelper.enterAndLoadUrl(url);
        mToolbar.assertTitle(url);

        url = StringHelper.ROBOCOP_BLANK_PAGE_02_URL;
        NavigationHelper.enterAndLoadUrl(url);
        mToolbar.assertTitle(url);

        url = StringHelper.ROBOCOP_BLANK_PAGE_03_URL;
        NavigationHelper.enterAndLoadUrl(url);
        mToolbar.assertTitle(url);

        NavigationHelper.goBack();
        mToolbar.assertTitle(StringHelper.ROBOCOP_BLANK_PAGE_02_URL);

        NavigationHelper.goBack();
        mToolbar.assertTitle(StringHelper.ROBOCOP_BLANK_PAGE_01_URL);

        NavigationHelper.goForward();
        mToolbar.assertTitle(StringHelper.ROBOCOP_BLANK_PAGE_02_URL);

        NavigationHelper.reload();
        mToolbar.assertTitle(StringHelper.ROBOCOP_BLANK_PAGE_02_URL);
    }
}

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.goanna.overlays.service.sharemethods;

import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Context;
import org.mozilla.goanna.GoannaProfile;
import org.mozilla.goanna.R;
import org.mozilla.goanna.db.LocalBrowserDB;
import org.mozilla.goanna.db.ReadingListProvider;
import org.mozilla.goanna.overlays.service.ShareData;

import static org.mozilla.goanna.db.BrowserContract.ReadingListItems;

/**
 * ShareMethod to add a page to the reading list.
 *
 * Inserts the given URL/title pair into the reading list database.
 */
public class AddToReadingList extends ShareMethod {
    private static final String LOGTAG = "GoannaAddToReadingList";

    @Override
    public Result handle(ShareData shareData) {
        final ContentResolver resolver = context.getContentResolver();

        final ContentValues values = new ContentValues();
        values.put(ReadingListItems.TITLE, shareData.title);
        values.put(ReadingListItems.URL, shareData.url);
        values.put(ReadingListItems.ADDED_ON, System.currentTimeMillis());
        values.put(ReadingListItems.ADDED_BY, ReadingListProvider.PLACEHOLDER_THIS_DEVICE);

        new LocalBrowserDB(GoannaProfile.DEFAULT_PROFILE).getReadingListAccessor().addReadingListItem(resolver, values);

        return Result.SUCCESS;
    }

    @Override
    public String getSuccessMessage() {
        return context.getResources().getString(R.string.reading_list_added);
    }

    // Unused.
    @Override
    public String getFailureMessage() {
        return null;
    }

    public AddToReadingList(Context context) {
        super(context);
    }
}

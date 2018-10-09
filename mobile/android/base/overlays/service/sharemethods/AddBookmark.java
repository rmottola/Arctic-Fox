/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.goanna.overlays.service.sharemethods;

import android.content.ContentResolver;
import android.content.Context;
import org.mozilla.goanna.GoannaProfile;
import org.mozilla.goanna.R;
import org.mozilla.goanna.db.LocalBrowserDB;
import org.mozilla.goanna.overlays.service.ShareData;

public class AddBookmark extends ShareMethod {
    private static final String LOGTAG = "GoannaAddBookmark";

    @Override
    public Result handle(ShareData shareData) {
        ContentResolver resolver = context.getContentResolver();

        LocalBrowserDB browserDB = new LocalBrowserDB(GoannaProfile.DEFAULT_PROFILE);
        browserDB.addBookmark(resolver, shareData.title, shareData.url);

        return Result.SUCCESS;
    }

    @Override
    public String getSuccessMessage() {
        return context.getResources().getString(R.string.bookmark_added);
    }

    // Unused.
    @Override
    public String getFailureMessage() {
        return null;
    }

    public AddBookmark(Context context) {
        super(context);
    }
}

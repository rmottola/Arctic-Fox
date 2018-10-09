/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.goanna;

import org.mozilla.goanna.db.BrowserContract;
import org.mozilla.goanna.db.BrowserDB;
import org.mozilla.goanna.db.LocalBrowserDB;
import org.mozilla.goanna.home.HomePanelsManager;
import org.mozilla.goanna.lwt.LightweightTheme;
import org.mozilla.goanna.mozglue.GoannaLoader;
import org.mozilla.goanna.util.Clipboard;
import org.mozilla.goanna.util.HardwareUtils;
import org.mozilla.goanna.util.ThreadUtils;

import android.app.Application;
import android.content.Context;
import android.content.SharedPreferences;
import android.content.res.Configuration;
import android.util.Log;

import java.io.File;

public class GoannaApplication extends Application 
    implements ContextGetter {
    private static final String LOG_TAG = "GoannaApplication";

    private static volatile GoannaApplication instance;

    private boolean mInBackground;
    private boolean mPausedGoanna;

    private LightweightTheme mLightweightTheme;

    public GoannaApplication() {
        super();
        instance = this;
    }

    public static GoannaApplication get() {
        return instance;
    }

    @Override
    public Context getContext() {
        return this;
    }

    @Override
    public SharedPreferences getSharedPreferences() {
        return GoannaSharedPrefs.forApp(this);
    }

    /**
     * We need to do locale work here, because we need to intercept
     * each hit to onConfigurationChanged.
     */
    @Override
    public void onConfigurationChanged(Configuration config) {
        Log.d(LOG_TAG, "onConfigurationChanged: " + config.locale +
                       ", background: " + mInBackground);

        // Do nothing if we're in the background. It'll simply cause a loop
        // (Bug 936756 Comment 11), and it's not necessary.
        if (mInBackground) {
            super.onConfigurationChanged(config);
            return;
        }

        // Otherwise, correct the locale. This catches some cases that GoannaApp
        // doesn't get a chance to.
        try {
            BrowserLocaleManager.getInstance().correctLocale(this, getResources(), config);
        } catch (IllegalStateException ex) {
            // GoannaApp hasn't started, so we have no ContextGetter in BrowserLocaleManager.
            Log.w(LOG_TAG, "Couldn't correct locale.", ex);
        }

        super.onConfigurationChanged(config);
    }

    public void onActivityPause(GoannaActivityStatus activity) {
        mInBackground = true;

        if ((activity.isFinishing() == false) &&
            (activity.isGoannaActivityOpened() == false)) {
            // Notify Goanna that we are pausing; the cache service will be
            // shutdown, closing the disk cache cleanly. If the android
            // low memory killer subsequently kills us, the disk cache will
            // be left in a consistent state, avoiding costly cleanup and
            // re-creation. 
            GoannaAppShell.sendEventToGoanna(GoannaEvent.createAppBackgroundingEvent());
            mPausedGoanna = true;

            final BrowserDB db = GoannaProfile.get(this).getDB();
            ThreadUtils.postToBackgroundThread(new Runnable() {
                @Override
                public void run() {
                    db.expireHistory(getContentResolver(), BrowserContract.ExpirePriority.NORMAL);
                }
            });
        }
        GoannaConnectivityReceiver.getInstance().stop();
        GoannaNetworkManager.getInstance().stop();
    }

    public void onActivityResume(GoannaActivityStatus activity) {
        if (mPausedGoanna) {
            GoannaAppShell.sendEventToGoanna(GoannaEvent.createAppForegroundingEvent());
            mPausedGoanna = false;
        }

        final Context applicationContext = getApplicationContext();
        GoannaBatteryManager.getInstance().start(applicationContext);
        GoannaConnectivityReceiver.getInstance().start(applicationContext);
        GoannaNetworkManager.getInstance().start(applicationContext);

        mInBackground = false;
    }

    @Override
    public void onCreate() {
        final Context context = getApplicationContext();
        HardwareUtils.init(context);
        Clipboard.init(context);
        FilePicker.init(context);
        GoannaLoader.loadMozGlue(context);
        DownloadsIntegration.init();
        HomePanelsManager.getInstance().init(context);

        // This getInstance call will force initialization of the NotificationHelper, but does nothing with the result
        NotificationHelper.getInstance(context).init();

        // Make sure that all browser-ish applications default to the real LocalBrowserDB.
        // GoannaView consumers use their own Application class, so this doesn't affect them.
        // WebappImpl overrides this on creation.
        //
        // We need to do this before any access to the profile; it controls
        // which database class is used.
        //
        // As such, this needs to occur before the GoannaView in GoannaApp is inflated -- i.e., in the
        // GoannaApp constructor or earlier -- because GoannaView implicitly accesses the profile. This is earlier!
        GoannaProfile.setBrowserDBFactory(new BrowserDB.Factory() {
            @Override
            public BrowserDB get(String profileName, File profileDir) {
                // Note that we don't use the profile directory -- we
                // send operations to the ContentProvider, which does
                // its own thing.
                return new LocalBrowserDB(profileName);
            }
        });

        super.onCreate();
    }

    public boolean isApplicationInBackground() {
        return mInBackground;
    }

    public LightweightTheme getLightweightTheme() {
        return mLightweightTheme;
    }

    public void prepareLightweightTheme() {
        mLightweightTheme = new LightweightTheme(this);
    }
}

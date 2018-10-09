/* -*- Mode: Java; c-basic-offset: 4; tab-width: 4; indent-tabs-mode: nil; -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.goanna;

import org.mozilla.goanna.mozglue.GoannaLoader;
import org.mozilla.goanna.mozglue.RobocopTarget;
import org.mozilla.goanna.util.GoannaEventListener;
import org.mozilla.goanna.util.ThreadUtils;

import org.json.JSONObject;

import android.content.Context;
import android.content.res.Configuration;
import android.content.res.Resources;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;

import java.io.IOException;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicReference;

public class GoannaThread extends Thread implements GoannaEventListener {
    private static final String LOGTAG = "GoannaThread";

    @RobocopTarget
    public enum LaunchState {
        Launching,
        WaitForDebugger,
        Launched,
        GoannaRunning,
        GoannaExiting,
        GoannaExited
    }

    private static final AtomicReference<LaunchState> sLaunchState =
                                            new AtomicReference<LaunchState>(LaunchState.Launching);

    private static GoannaThread sGoannaThread;

    private final String mArgs;
    private final String mAction;
    private final String mUri;

    public static boolean ensureInit() {
        ThreadUtils.assertOnUiThread();
        if (isCreated())
            return false;
        sGoannaThread = new GoannaThread(sArgs, sAction, sUri);
        return true;
    }

    public static String sArgs;
    public static String sAction;
    public static String sUri;

    public static void setArgs(String args) {
        sArgs = args;
    }

    public static void setAction(String action) {
        sAction = action;
    }

    public static void setUri(String uri) {
        sUri = uri;
    }

    GoannaThread(String args, String action, String uri) {
        mArgs = args;
        mAction = action;
        mUri = uri;
        setName("Goanna");
        EventDispatcher.getInstance().registerGoannaThreadListener(this, "Goanna:Ready");
    }

    public static boolean isCreated() {
        return sGoannaThread != null;
    }

    public static void createAndStart() {
        if (ensureInit())
            sGoannaThread.start();
    }

    private String initGoannaEnvironment() {
        final Locale locale = Locale.getDefault();

        final Context context = GoannaAppShell.getContext();
        final Resources res = context.getResources();
        if (locale.toString().equalsIgnoreCase("zh_hk")) {
            final Locale mappedLocale = Locale.TRADITIONAL_CHINESE;
            Locale.setDefault(mappedLocale);
            Configuration config = res.getConfiguration();
            config.locale = mappedLocale;
            res.updateConfiguration(config, null);
        }

        String resourcePath = "";
        String[] pluginDirs = null;
        try {
            pluginDirs = GoannaAppShell.getPluginDirectories();
        } catch (Exception e) {
            Log.w(LOGTAG, "Caught exception getting plugin dirs.", e);
        }

        resourcePath = context.getPackageResourcePath();
        GoannaLoader.setupGoannaEnvironment(context, pluginDirs, context.getFilesDir().getPath());

        GoannaLoader.loadSQLiteLibs(context, resourcePath);
        GoannaLoader.loadNSSLibs(context, resourcePath);
        GoannaLoader.loadGoannaLibs(context, resourcePath);
        GoannaJavaSampler.setLibsLoaded();

        return resourcePath;
    }

    private String getTypeFromAction(String action) {
        if (GoannaApp.ACTION_HOMESCREEN_SHORTCUT.equals(action)) {
            return "-bookmark";
        }
        return null;
    }

    private String addCustomProfileArg(String args) {
        String profileArg = "";
        String guestArg = "";
        if (GoannaAppShell.getGoannaInterface() != null) {
            final GoannaProfile profile = GoannaAppShell.getGoannaInterface().getProfile();

            if (profile.inGuestMode()) {
                try {
                    profileArg = " -profile " + profile.getDir().getCanonicalPath();
                } catch (final IOException ioe) {
                    Log.e(LOGTAG, "error getting guest profile path", ioe);
                }

                if (args == null || !args.contains(BrowserApp.GUEST_BROWSING_ARG)) {
                    guestArg = " " + BrowserApp.GUEST_BROWSING_ARG;
                }
            } else if (!GoannaProfile.sIsUsingCustomProfile) {
                // If nothing was passed in the intent, make sure the default profile exists and
                // force Goanna to use the default profile for this activity
                profileArg = " -P " + profile.forceCreate().getName();
            }
        }

        return (args != null ? args : "") + profileArg + guestArg;
    }

    @Override
    public void run() {
        Looper.prepare();
        ThreadUtils.sGoannaThread = this;
        ThreadUtils.sGoannaHandler = new Handler();
        ThreadUtils.sGoannaQueue = Looper.myQueue();

        String path = initGoannaEnvironment();

        // This can only happen after the call to initGoannaEnvironment
        // above, because otherwise the JNI code hasn't been loaded yet.
        ThreadUtils.postToUiThread(new Runnable() {
            @Override public void run() {
                GoannaAppShell.registerJavaUiThread();
            }
        });

        Log.w(LOGTAG, "zerdatime " + SystemClock.uptimeMillis() + " - runGoanna");

        String args = addCustomProfileArg(mArgs);
        String type = getTypeFromAction(mAction);

        if (!AppConstants.MOZILLA_OFFICIAL) {
            Log.i(LOGTAG, "RunGoanna - args = " + args);
        }
        // and then fire us up
        GoannaAppShell.runGoanna(path, args, mUri, type);
    }

    @Override
    public void handleMessage(String event, JSONObject message) {
        if ("Goanna:Ready".equals(event)) {
            EventDispatcher.getInstance().unregisterGoannaThreadListener(this, event);
            setLaunchState(LaunchState.GoannaRunning);
            GoannaAppShell.sendPendingEventsToGoanna();
        }
    }

    @RobocopTarget
    public static boolean checkLaunchState(LaunchState checkState) {
        return sLaunchState.get() == checkState;
    }

    static void setLaunchState(LaunchState setState) {
        sLaunchState.set(setState);
    }

    /**
     * Set the launch state to <code>setState</code> and return true if the current launch
     * state is <code>checkState</code>; otherwise do nothing and return false.
     */
    static boolean checkAndSetLaunchState(LaunchState checkState, LaunchState setState) {
        return sLaunchState.compareAndSet(checkState, setState);
    }
}

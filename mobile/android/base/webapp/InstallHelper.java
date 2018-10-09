/* -*- Mode: Java; c-basic-offset: 4; tab-width: 4; indent-tabs-mode: nil; -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.goanna.webapp;

import java.io.Closeable;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

import org.json.JSONException;
import org.json.JSONObject;
import org.mozilla.goanna.EventDispatcher;
import org.mozilla.goanna.GoannaAppShell;
import org.mozilla.goanna.GoannaEvent;
import org.mozilla.goanna.GoannaProfile;
import org.mozilla.goanna.gfx.BitmapUtils;
import org.mozilla.goanna.util.EventCallback;
import org.mozilla.goanna.util.NativeEventListener;
import org.mozilla.goanna.util.NativeJSObject;
import org.mozilla.goanna.util.ThreadUtils;

import android.content.Context;
import android.graphics.Bitmap;
import android.net.Uri;
import android.util.Log;

public class InstallHelper implements NativeEventListener {
    private static final String LOGTAG = "GoannaWebappInstallHelper";
    private static final String[] INSTALL_EVENT_NAMES = new String[] {"Webapps:Postinstall"};
    private final Context mContext;
    private final InstallCallback mCallback;
    private final ApkResources mApkResources;

    public static interface InstallCallback {
        // on the GoannaThread
        void installCompleted(InstallHelper installHelper, String event, NativeJSObject message);

        // on the GoannaBackgroundThread
        void installErrored(InstallHelper installHelper, Exception exception);
    }

    public InstallHelper(Context context, ApkResources apkResources, InstallCallback cb) {
        mContext = context;
        mCallback = cb;
        mApkResources = apkResources;
    }

    public void startInstall(String profileName) throws IOException {
        startInstall(profileName, null);
    }

    public void startInstall(final String profileName, final JSONObject message) throws IOException {
        ThreadUtils.postToBackgroundThread(new Runnable() {
            @Override
            public void run() {
                try {
                    install(profileName, message);
                } catch (IOException e) {
                    handleException(e);
                }
            }
        });
    }

    protected void handleException(Exception e) {
        if (mCallback != null) {
            mCallback.installErrored(this, e);
        } else {
            Log.e(LOGTAG, "mozApps.install failed", e);
        }
    }

    void install(String profileName, JSONObject message) throws IOException {
        if (message == null) {
            message = new JSONObject();
        }

        // we can change the profile to be in the app's area here
        GoannaProfile profile = GoannaProfile.get(mContext, profileName);

        try {
            message.put("apkPackageName", mApkResources.getPackageName());
            message.put("manifestURL", mApkResources.getManifestUrl());
            message.put("title", mApkResources.getAppName());
            message.put("manifest", new JSONObject(mApkResources.getManifest(mContext)));

            String appType = mApkResources.getWebappType();
            message.putOpt("type", appType);
            if ("packaged".equals(appType)) {
                message.putOpt("updateManifest", new JSONObject(mApkResources.getMiniManifest(mContext)));
            }

            message.putOpt("profilePath", profile.getDir());

            if (mApkResources.isPackaged()) {
                File zipFile = copyApplicationZipFile();
                message.putOpt("zipFilePath", Uri.fromFile(zipFile).toString());
            }
        } catch (JSONException e) {
            handleException(e);
            return;
        }

        registerGoannaListener();

        GoannaAppShell.sendEventToGoanna(GoannaEvent.createBroadcastEvent("Webapps:AutoInstall", message.toString()));
        calculateColor();
    }

    public File copyApplicationZipFile() throws IOException {
        if (!mApkResources.isPackaged()) {
            return null;
        }

        Uri uri = mApkResources.getZipFileUri();

        InputStream in = null;
        OutputStream out = null;
        File destPath = new File(mApkResources.getFileDirectory(), "application.zip");
        try {
            in = mContext.getContentResolver().openInputStream(uri);
            out = new FileOutputStream(destPath);
            byte[] buffer = new byte[1024];
            int read = 0;
            while ((read = in.read(buffer)) != -1) {
                out.write(buffer, 0, read);
            }
            out.flush();
        } catch (IOException e) {
            throw e;
        } finally {
            close(in);
            close(out);
        }
        return destPath;
    }

    private static void close(Closeable close) {
        if (close == null) {
            return;
        }
        try {
            close.close();
        } catch (IOException e) {
            // NOP
        }
    }

    public void registerGoannaListener() {
        EventDispatcher.getInstance().registerGoannaThreadListener(this, INSTALL_EVENT_NAMES);
    }

    private void calculateColor() {
        ThreadUtils.assertOnBackgroundThread();
        Allocator slots = Allocator.getInstance(mContext);
        int index = slots.getIndexForApp(mApkResources.getPackageName());
        Bitmap bitmap = BitmapUtils.getBitmapFromDrawable(mApkResources.getAppIcon());
        slots.updateColor(index, BitmapUtils.getDominantColor(bitmap));
    }

    @Override
    public void handleMessage(String event, NativeJSObject message, EventCallback callback) {
        EventDispatcher.getInstance().unregisterGoannaThreadListener(this, INSTALL_EVENT_NAMES);

        if (mCallback != null) {
            mCallback.installCompleted(this, event, message);
        }
    }
}

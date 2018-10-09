/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.goanna;

import java.util.concurrent.BlockingQueue;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;

import org.json.JSONObject;
import org.mozilla.goanna.FennecNativeDriver.LogLevel;
import org.mozilla.goanna.gfx.LayerView;
import org.mozilla.goanna.gfx.LayerView.DrawListener;
import org.mozilla.goanna.mozglue.GoannaLoader;
import org.mozilla.goanna.sqlite.SQLiteBridge;
import org.mozilla.goanna.util.GoannaEventListener;

import android.app.Activity;
import android.app.Instrumentation;
import android.database.Cursor;
import android.os.SystemClock;
import android.text.TextUtils;
import android.view.KeyEvent;

import com.jayway.android.robotium.solo.Solo;

public class FennecNativeActions implements Actions {
    private static final String LOGTAG = "FennecNativeActions";

    private Solo mSolo;
    private Instrumentation mInstr;
    private Assert mAsserter;

    public FennecNativeActions(Activity activity, Solo robocop, Instrumentation instrumentation, Assert asserter) {
        mSolo = robocop;
        mInstr = instrumentation;
        mAsserter = asserter;

        GoannaLoader.loadSQLiteLibs(activity, activity.getApplication().getPackageResourcePath());
    }

    class GoannaEventExpecter implements RepeatedEventExpecter {
        private static final int MAX_WAIT_MS = 180000;

        private volatile boolean mIsRegistered;

        private final String mGoannaEvent;
        private final GoannaEventListener mListener;

        private volatile boolean mEventEverReceived;
        private String mEventData;
        private BlockingQueue<String> mEventDataQueue;

        GoannaEventExpecter(final String goannaEvent) {
            if (TextUtils.isEmpty(goannaEvent)) {
                throw new IllegalArgumentException("goannaEvent must not be empty");
            }

            mGoannaEvent = goannaEvent;
            mEventDataQueue = new LinkedBlockingQueue<String>();

            final GoannaEventExpecter expecter = this;
            mListener = new GoannaEventListener() {
                @Override
                public void handleMessage(final String event, final JSONObject message) {
                    FennecNativeDriver.log(FennecNativeDriver.LogLevel.DEBUG,
                            "handleMessage called for: " + event + "; expecting: " + mGoannaEvent);
                    mAsserter.is(event, mGoannaEvent, "Given message occurred for registered event: " + message);

                    expecter.notifyOfEvent(message);
                }
            };

            EventDispatcher.getInstance().registerGoannaThreadListener(mListener, mGoannaEvent);
            mIsRegistered = true;
        }

        public void blockForEvent() {
            blockForEvent(MAX_WAIT_MS, true);
        }

        public void blockForEvent(long millis, boolean failOnTimeout) {
            if (!mIsRegistered) {
                throw new IllegalStateException("listener not registered");
            }

            try {
                mEventData = mEventDataQueue.poll(millis, TimeUnit.MILLISECONDS);
            } catch (InterruptedException ie) {
                FennecNativeDriver.log(LogLevel.ERROR, ie);
            }
            if (mEventData == null) {
                if (failOnTimeout) {
                    FennecNativeDriver.logAllStackTraces(FennecNativeDriver.LogLevel.ERROR);
                    mAsserter.ok(false, "GoannaEventExpecter",
                        "blockForEvent timeout: "+mGoannaEvent);
                } else {
                    FennecNativeDriver.log(FennecNativeDriver.LogLevel.DEBUG,
                        "blockForEvent timeout: "+mGoannaEvent);
                }
            } else {
                FennecNativeDriver.log(FennecNativeDriver.LogLevel.DEBUG,
                    "unblocked on expecter for " + mGoannaEvent);
            }
        }

        public void blockUntilClear(long millis) {
            if (!mIsRegistered) {
                throw new IllegalStateException("listener not registered");
            }
            if (millis <= 0) {
                throw new IllegalArgumentException("millis must be > 0");
            }

            // wait for at least one event
            try {
                mEventData = mEventDataQueue.poll(MAX_WAIT_MS, TimeUnit.MILLISECONDS);
            } catch (InterruptedException ie) {
                FennecNativeDriver.log(LogLevel.ERROR, ie);
            }
            if (mEventData == null) {
                FennecNativeDriver.logAllStackTraces(FennecNativeDriver.LogLevel.ERROR);
                mAsserter.ok(false, "GoannaEventExpecter", "blockUntilClear timeout");
                return;
            }
            // now wait for a period of millis where we don't get an event
            while (true) {
                try {
                    mEventData = mEventDataQueue.poll(millis, TimeUnit.MILLISECONDS);
                } catch (InterruptedException ie) {
                    FennecNativeDriver.log(LogLevel.INFO, ie);
                }
                if (mEventData == null) {
                    // success
                    break;
                }
            }
            FennecNativeDriver.log(FennecNativeDriver.LogLevel.DEBUG,
                "unblocked on expecter for " + mGoannaEvent);
        }

        public String blockForEventData() {
            blockForEvent();
            return mEventData;
        }

        public String blockForEventDataWithTimeout(long millis) {
            blockForEvent(millis, false);
            return mEventData;
        }

        public void unregisterListener() {
            if (!mIsRegistered) {
                throw new IllegalStateException("listener not registered");
            }

            FennecNativeDriver.log(LogLevel.INFO,
                    "EventExpecter: no longer listening for " + mGoannaEvent);

            EventDispatcher.getInstance().unregisterGoannaThreadListener(mListener, mGoannaEvent);
            mIsRegistered = false;
        }

        public boolean eventReceived() {
            return mEventEverReceived;
        }

        void notifyOfEvent(final JSONObject message) {
            FennecNativeDriver.log(FennecNativeDriver.LogLevel.DEBUG,
                    "received event " + mGoannaEvent);

            mEventEverReceived = true;

            try {
                mEventDataQueue.put(message.toString());
            } catch (InterruptedException e) {
                FennecNativeDriver.log(LogLevel.ERROR,
                    "EventExpecter dropped event: " + message.toString(), e);
            }
        }
    }

    public RepeatedEventExpecter expectGoannaEvent(final String goannaEvent) {
        FennecNativeDriver.log(FennecNativeDriver.LogLevel.DEBUG, "waiting for " + goannaEvent);
        return new GoannaEventExpecter(goannaEvent);
    }

    public void sendGoannaEvent(final String goannaEvent, final String data) {
        GoannaAppShell.sendEventToGoanna(GoannaEvent.createBroadcastEvent(goannaEvent, data));
    }

    public void sendPreferencesGetEvent(int requestId, String[] prefNames) {
        GoannaAppShell.sendEventToGoanna(GoannaEvent.createPreferencesGetEvent(requestId, prefNames));
    }

    public void sendPreferencesObserveEvent(int requestId, String[] prefNames) {
        GoannaAppShell.sendEventToGoanna(GoannaEvent.createPreferencesObserveEvent(requestId, prefNames));
    }

    public void sendPreferencesRemoveObserversEvent(int requestId) {
        GoannaAppShell.sendEventToGoanna(GoannaEvent.createPreferencesRemoveObserversEvent(requestId));
    }

    class PaintExpecter implements RepeatedEventExpecter {
        private static final int MAX_WAIT_MS = 90000;

        private boolean mPaintDone;
        private boolean mListening;

        private final LayerView mLayerView;
        private final DrawListener mDrawListener;

        PaintExpecter() {
            final PaintExpecter expecter = this;
            mLayerView = GoannaAppShell.getLayerView();
            mDrawListener = new DrawListener() {
                @Override
                public void drawFinished() {
                    FennecNativeDriver.log(FennecNativeDriver.LogLevel.DEBUG,
                            "Received drawFinished notification");
                    expecter.notifyOfEvent();
                }
            };
            mLayerView.addDrawListener(mDrawListener);
            mListening = true;
        }

        private synchronized void notifyOfEvent() {
            mPaintDone = true;
            this.notifyAll();
        }

        public synchronized void blockForEvent(long millis, boolean failOnTimeout) {
            if (!mListening) {
                throw new IllegalStateException("draw listener not registered");
            }
            long startTime = SystemClock.uptimeMillis();
            long endTime = 0;
            while (!mPaintDone) {
                try {
                    this.wait(millis);
                } catch (InterruptedException ie) {
                    FennecNativeDriver.log(LogLevel.ERROR, ie);
                    break;
                }
                endTime = SystemClock.uptimeMillis();
                if (!mPaintDone && (endTime - startTime >= millis)) {
                    if (failOnTimeout) {
                        FennecNativeDriver.logAllStackTraces(FennecNativeDriver.LogLevel.ERROR);
                        mAsserter.ok(false, "PaintExpecter", "blockForEvent timeout");
                    }
                    return;
                }
            }
        }

        public synchronized void blockForEvent() {
            blockForEvent(MAX_WAIT_MS, true);
        }

        public synchronized String blockForEventData() {
            blockForEvent();
            return null;
        }

        public synchronized String blockForEventDataWithTimeout(long millis) {
            blockForEvent(millis, false);
            return null;
        }

        public synchronized boolean eventReceived() {
            return mPaintDone;
        }

        public synchronized void blockUntilClear(long millis) {
            if (!mListening) {
                throw new IllegalStateException("draw listener not registered");
            }
            if (millis <= 0) {
                throw new IllegalArgumentException("millis must be > 0");
            }
            // wait for at least one event
            long startTime = SystemClock.uptimeMillis();
            long endTime = 0;
            while (!mPaintDone) {
                try {
                    this.wait(MAX_WAIT_MS);
                } catch (InterruptedException ie) {
                    FennecNativeDriver.log(LogLevel.ERROR, ie);
                    break;
                }
                endTime = SystemClock.uptimeMillis();
                if (!mPaintDone && (endTime - startTime >= MAX_WAIT_MS)) {
                    FennecNativeDriver.logAllStackTraces(FennecNativeDriver.LogLevel.ERROR);
                    mAsserter.ok(false, "PaintExpecter", "blockUtilClear timeout");
                    return;
                }
            }
            // now wait for a period of millis where we don't get an event
            startTime = SystemClock.uptimeMillis();
            while (true) {
                try {
                    this.wait(millis);
                } catch (InterruptedException ie) {
                    FennecNativeDriver.log(LogLevel.ERROR, ie);
                    break;
                }
                endTime = SystemClock.uptimeMillis();
                if (endTime - startTime >= millis) {
                    // success
                    break;
                }

                // we got a notify() before we could wait long enough, so we need to start over
                // Note, moving the goal post might have us race against a "drawFinished" flood
                startTime = endTime;
            }
        }

        public synchronized void unregisterListener() {
            if (!mListening) {
                throw new IllegalStateException("listener not registered");
            }

            FennecNativeDriver.log(LogLevel.INFO,
                    "PaintExpecter: no longer listening for events");
            mLayerView.removeDrawListener(mDrawListener);
            mListening = false;
        }
    }

    public RepeatedEventExpecter expectPaint() {
        return new PaintExpecter();
    }

    public void sendSpecialKey(SpecialKey button) {
        switch(button) {
            case DOWN:
                sendKeyCode(KeyEvent.KEYCODE_DPAD_DOWN);
                break;
            case UP:
                sendKeyCode(KeyEvent.KEYCODE_DPAD_UP);
                break;
            case LEFT:
                sendKeyCode(KeyEvent.KEYCODE_DPAD_LEFT);
                break;
            case RIGHT:
                sendKeyCode(KeyEvent.KEYCODE_DPAD_RIGHT);
                break;
            case ENTER:
                sendKeyCode(KeyEvent.KEYCODE_ENTER);
                break;
            case MENU:
                sendKeyCode(KeyEvent.KEYCODE_MENU);
                break;
            case BACK:
                sendKeyCode(KeyEvent.KEYCODE_BACK);
                break;
            default:
                mAsserter.ok(false, "sendSpecialKey", "Unknown SpecialKey " + button);
                break;
        }
    }

    public void sendKeyCode(int keyCode) {
        if (keyCode <= 0 || keyCode > KeyEvent.getMaxKeyCode()) {
            mAsserter.ok(false, "sendKeyCode", "Unknown keyCode " + keyCode);
        }
        mInstr.sendCharacterSync(keyCode);
    }

    @Override
    public void sendKeys(String input) {
        mInstr.sendStringSync(input);
    }

    public void drag(int startingX, int endingX, int startingY, int endingY) {
        mSolo.drag(startingX, endingX, startingY, endingY, 10);
    }

    public Cursor querySql(final String dbPath, final String sql) {
        return new SQLiteBridge(dbPath).rawQuery(sql, null);
    }
}

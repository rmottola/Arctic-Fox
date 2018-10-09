/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.goanna.preferences;

import android.content.Context;
import android.preference.Preference;
import android.util.AttributeSet;
import android.util.Log;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import org.mozilla.goanna.EventDispatcher;
import org.mozilla.goanna.GoannaAppShell;
import org.mozilla.goanna.GoannaEvent;
import org.mozilla.goanna.Telemetry;
import org.mozilla.goanna.TelemetryContract;
import org.mozilla.goanna.TelemetryContract.Method;
import org.mozilla.goanna.util.GoannaEventListener;
import org.mozilla.goanna.util.ThreadUtils;

public class SearchPreferenceCategory extends CustomListCategory implements GoannaEventListener {
    public static final String LOGTAG = "SearchPrefCategory";

    public SearchPreferenceCategory(Context context) {
        super(context);
    }

    public SearchPreferenceCategory(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    public SearchPreferenceCategory(Context context, AttributeSet attrs, int defStyle) {
        super(context, attrs, defStyle);
    }

    @Override
    protected void onAttachedToActivity() {
        super.onAttachedToActivity();

        // Register for SearchEngines messages and request list of search engines from Goanna.
        EventDispatcher.getInstance().registerGoannaThreadListener(this, "SearchEngines:Data");
        GoannaAppShell.sendEventToGoanna(GoannaEvent.createBroadcastEvent("SearchEngines:GetVisible", null));
    }

    @Override
    protected void onPrepareForRemoval() {
        super.onPrepareForRemoval();

        EventDispatcher.getInstance().unregisterGoannaThreadListener(this, "SearchEngines:Data");
    }

    @Override
    public void setDefault(CustomListPreference item) {
        super.setDefault(item);

        sendGoannaEngineEvent("SearchEngines:SetDefault", item.getTitle().toString());

        final String identifier = ((SearchEnginePreference) item).getIdentifier();
        Telemetry.sendUIEvent(TelemetryContract.Event.SEARCH_SET_DEFAULT, Method.DIALOG, identifier);
    }

    @Override
    public void uninstall(CustomListPreference item) {
        super.uninstall(item);

        sendGoannaEngineEvent("SearchEngines:Remove", item.getTitle().toString());

        final String identifier = ((SearchEnginePreference) item).getIdentifier();
        Telemetry.sendUIEvent(TelemetryContract.Event.SEARCH_REMOVE, Method.DIALOG, identifier);
    }

    @Override
    public void handleMessage(String event, final JSONObject data) {
        if (event.equals("SearchEngines:Data")) {
            // Parse engines array from JSON.
            JSONArray engines;
            try {
                engines = data.getJSONArray("searchEngines");
            } catch (JSONException e) {
                Log.e(LOGTAG, "Unable to decode search engine data from Goanna.", e);
                return;
            }

            // Clear the preferences category from this thread.
            this.removeAll();

            // Create an element in this PreferenceCategory for each engine.
            for (int i = 0; i < engines.length(); i++) {
                try {
                    final JSONObject engineJSON = engines.getJSONObject(i);

                    final SearchEnginePreference enginePreference = new SearchEnginePreference(getContext(), this);
                    enginePreference.setSearchEngineFromJSON(engineJSON);
                    enginePreference.setOnPreferenceClickListener(new OnPreferenceClickListener() {
                        @Override
                        public boolean onPreferenceClick(Preference preference) {
                            SearchEnginePreference sPref = (SearchEnginePreference) preference;
                            // Display the configuration dialog associated with the tapped engine.
                            sPref.showDialog();
                            return true;
                        }
                    });

                    addPreference(enginePreference);

                    // The first element in the array is the default engine.
                    if (i == 0) {
                        // We set this here, not in setSearchEngineFromJSON, because it allows us to
                        // keep a reference  to the default engine to use when the AlertDialog
                        // callbacks are used.
                        ThreadUtils.postToUiThread(new Runnable() {
                            @Override
                            public void run() {
                                enginePreference.setIsDefault(true);
                            }
                        });
                        mDefaultReference = enginePreference;
                    }
                } catch (JSONException e) {
                    Log.e(LOGTAG, "JSONException parsing engine at index " + i, e);
                }
            }
        }
    }

    /**
     * Helper method to send a particular event string to Goanna with an associated engine name.
     * @param event The type of event to send.
     * @param engine The engine to which the event relates.
     */
    private void sendGoannaEngineEvent(String event, String engineName) {
        JSONObject json = new JSONObject();
        try {
            json.put("engine", engineName);
        } catch (JSONException e) {
            Log.e(LOGTAG, "JSONException creating search engine configuration change message for Goanna.", e);
            return;
        }
        GoannaAppShell.notifyGoannaOfEvent(GoannaEvent.createBroadcastEvent(event, json.toString()));
    }
}


"core" ping
============

This mobile-specific ping is intended to provide the most critical
data in a concise format, allowing for frequent uploads.

Since this ping is used to measure retention, it should be sent
each time the browser is opened.

Submission will be per the Edge server specification::

    /submit/telemetry/docId/docType/appName/appVersion/appUpdateChannel/appBuildID

* ``docId`` is a UUID for deduping
* ``docType`` is “core”
* ``appName`` is “Fennec”
* ``appVersion`` is the version of the application (e.g. "46.0a1")
* ``appUpdateChannel`` is “release”, “beta”, etc.
* ``appBuildID`` is the build number

Note: Counts below (e.g. search & usage times) are “since the last
ping”, not total for the whole application lifetime.

Structure::

    {
      "v": 3, // ping format version
      "clientId": <string>, // client id, e.g.
                            // "c641eacf-c30c-4171-b403-f077724e848a"
      "seq": <positive integer>, // running ping counter, e.g. 3
      "locale": <string>, // application locale, e.g. "en-US"
      "os": <string>, // OS name.
      "osversion": <string>, // OS version.
      "device": <string>, // Build.MANUFACTURER + " - " + Build.MODEL
                          // where manufacturer is truncated to 12 characters
                          // & model is truncated to 19 characters
      "arch": <string>, // e.g. "arm", "x86"
      "profileDate": <pos integer>, // Profile creation date in days since
                                    // UNIX epoch.
      "defaultSearch": <string>, // Identifier of the default search engine,
                                 // e.g. "yahoo".

      "experiments": [<string>, …], // Optional, array of identifiers
                                    // for the active experiments
    }

Field details
-------------

device
~~~~~~
The ``device`` field is filled in with information specified by the hardware
manufacturer. As such, it could be excessively long and use excessive amounts
of limited user data. To avoid this, we limit the length of the field. We're
more likely have collisions for models within a manufacturer (e.g. "Galaxy S5"
vs. "Galaxy Note") than we are for shortened manufacturer names so we provide
more characters for the model than the manufacturer.

defaultSearch
~~~~~~~~~~~~~
On Android, this field may be ``null``. To get the engine, we rely on
``SearchEngineManager#getDefaultEngine``, which searches in several places in
order to find the search engine identifier:

* Shared Preferences
* The distribution (if it exists)
* The localized default engine

If the identifier could not be retrieved, this field is ``null``. If the
identifier is retrieved, we attempt to create an instance of the search
engine from the search plugins (in order):

* In the distribution
* From the localized plugins shipped with the browser
* The third-party plugins that are installed in the profile directory

If the plugins fail to create a search engine instance, this field is also
``null``.

This field can also be ``null`` when a custom search engine is set as the
default.

profileDate
~~~~~~~~~~~
On Android, this value is created at profile creation time and retrieved or,
for legacy profiles, taken from the package install time (note: this is not the
same exact metric as profile creation time but we compromised in favor of ease
of implementation).

Additionally on Android, this field may be ``null`` in the unlikely event that
all of the following events occur:

#. The times.json file does not exist
#. The package install date could not be persisted to disk

The reason we don't just return the package install time even if the date could
not be persisted to disk is to ensure the value doesn't change once we start
sending it: we only want to send consistent values.

Version history
---------------
* v3: ``profileDate`` will return package install time when times.json is not available
* v2: added ``defaultSearch``
* v1: initial version

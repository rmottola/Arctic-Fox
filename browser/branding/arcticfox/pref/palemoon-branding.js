#filter substitution
#filter emptyLines
#include ../../shared/pref/preferences.inc
#include ../../shared/pref/uaoverrides.inc

pref("startup.homepage_override_url","");
pref("app.releaseNotesURL", "https://github.com/rmottola/Arctic-Fox/releases");

// Disable Firefox compatmode by default.
pref("general.useragent.compatMode", 2);

// ========================= updates ========================
#if defined(XP_WIN)
// Enable auto-updates for this channel
pref("app.update.auto", false);

// Updates enabled
pref("app.update.enabled", false);
pref("app.update.cert.checkAttributes", false);

// Interval: Time between checks for a new version (in seconds) -- 6 hours for unstable
pref("app.update.interval", 21600);
pref("app.update.promptWaitTime", 86400);

// URL user can browse to manually if for some reason all update installation
// attempts fail.
pref("app.update.url.manual", "https://github.com/rmottola/Arctic-Fox/wiki/Downloads");

// A default value for the "More information about this update" link
// supplied in the "An update is available" page of the update wizard.
pref("app.update.url.details", "about:");
#else
// Updates disabled (Linux, etc.)
pref("app.update.enabled", false);
pref("app.update.url", "https://github.com/rmottola/Arctic-Fox/wiki/Downloads");
#endif

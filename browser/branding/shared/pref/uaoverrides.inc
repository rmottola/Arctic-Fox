// ===| Site Specific User Agent Overrides |===================================

#define GUAO_PREF general.useragent.override

#define GRE_VERSION @MOZILLA_UAVERSION_U@
#define GRE_VERSION_SLICE Goanna/@GRE_VERSION@
#define GRE_DATE_SLICE Goanna/20170101
#define PM_SLICE Arcticfox/@MOZ_APP_VERSION@

#define GK_VERSION @MOZILLA_COMPATVERSION_U@
#define GK_SLICE Gecko/20100101
#define FX_SLICE Firefox/@GK_VERSION@

#ifdef XP_UNIX
#ifndef XP_MACOSX
#define OS_SLICE X11; Linux x86_64;
#else
#define OS_SLICE Macintosh; Intel Mac OS X 10.7;
#endif
#else
#define OS_SLICE Windows NT 6.1; WOW64;
#endif

// Required to use PaleMoon extension repository
pref("@GUAO_PREF@.addons.palemoon.org", "Mozilla/5.0 (@OS_SLICE@ rv:@MOZILLA_UAVERSION_U@) @GK_SLICE@ @FX_SLICE@ PaleMoon/29.5");


// Required for google to play nice with Arctic Fox
pref("@GUAO_PREF@.accounts.google.com","Mozilla/5.0 (@OS_SLICE@ rv:42.1) Gecko/20010101 @GRE_VERSION_SLICE@ Firefox/43.0 @PM_SLICE@");
pref("@GUAO_PREF@.google.com","Mozilla/5.0 (@OS_SLICE@ rv:48.0) Gecko/20010101 @GRE_VERSION_SLICE@ Firefox/48.0 @PM_SLICE@");
pref("@GUAO_PREF@.googlevideos.com","Mozilla/5.0 (@OS_SLICE@ rv:48.0) Gecko/20010101 @GRE_VERSION_SLICE@ Firefox/48.0 @PM_SLICE@");
pref("@GUAO_PREF@.gstatic.com","Mozilla/5.0 (@OS_SLICE@ rv:48.0) Gecko/20010101 @GRE_VERSION_SLICE@ Firefox/48.0 @PM_SLICE@");

// Switch to mobile youtube to bypass CPU/JS hungry slow as molasses new polyfill interface
pref("@GUAO_PREF@.youtube.com","Mozilla/5.0 (iPad; CPU OS 10_2_1 like Mac OS X) AppleWebKit/602.4.6 (KHTML, like Gecko) Version/10.0 Mobile/14D27 Safari/602.1");

// Trick these sites in to thinking we are using IE11
pref("@GUAO_PREF@.dropbox.com","Mozilla/5.0 (Windows NT 6.1; Trident/7.0; rv:11.0) like Gecko");
pref("@GUAO_PREF@.players.brightcove.net","Mozilla/5.0 (Windows NT 6.1; Trident/7.0; rv:11.0) like Gecko");

#ifdef XP_UNIX
// Google Fonts forces unicode ranges unless it is being told the browser is Firefox 43 or below.
// They do NOT test for unicode-ranges CSS support.
pref("@GUAO_PREF@.fonts.googleapis.com","Mozilla/5.0 (@OS_SLICE@ rv:43.0) Gecko/20010101 Firefox/43.0");
#endif

// UA-Sniffing domains below have indicated no interest in supporting Arctic Fox (BOO!)
pref("@GUAO_PREF@.bing.com","Mozilla/5.0 (@OS_SLICE@ rv:48.0) @GK_SLICE@ Firefox/48.0");
pref("@GUAO_PREF@.github.com","Mozilla/5.0 (compatible; Googlebot/2.1; +http://www.google.com/bot.html)");

// yahoo is nasty too
pref("@GUAO_PREF@.yahoo.com","Mozilla/5.0 (@OS_SLICE@ rv:52.0) @GK_SLICE@ @GRE_VERSION_SLICE@ Firefox/52.0");


// ============================================================================
// Spoof the useragent and platform to a generic one for user experience and privacy
// pref("general.useragent.override", "Mozilla/5.0 (Macintosh; Intel Mac OS X 10.11; rv:52.9) Gecko/20100101 Firefox/52.9 Arcticfox/27.9.19");
// pref("general.appname.override", "Netscape");
// pref("general.appversion.override", "5.0 (Windows)");
// pref("general.buildID.override", "Gecko/20100101");
// pref("general.oscpu.override", "Windows NT 10.0");
// pref("general.platform.override", "Win64");

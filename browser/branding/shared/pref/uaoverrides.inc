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
#define OS_SLICE Macintosh; Intel Mac OS X 10.6;
#endif
#else
#define OS_SLICE Windows NT 6.1; WOW64;
#endif

// Since github is being anal, force mobile layout so it looks better at least.
pref("@GUAO_PREF@.github.com","Mozilla/5.0 (Linux; X11; Android 4.4; rv:27.9) Gecko/20100101 Arcticfox/29.7.16");

// Force youtube to present the old style layout
pref("@GUAO_PREF@.youtube.com","Mozilla/5.0 (@OS_SLICE@ rv:42.0) @GK_SLICE@ Firefox/42.0 @PM_SLICE@");
pref("@GUAO_PREF@.gaming.youtube.com","Mozilla/5.0 (@OS_SLICE@ rv:42.0) @GK_SLICE@ Firefox/42.0");

#ifdef XP_UNIX
// Google Fonts forces unicode ranges unless it is being told the browser is Firefox 43 or below.
// They do NOT test for unicode-ranges CSS support.
pref("@GUAO_PREF@.fonts.googleapis.com","Mozilla/5.0 (@OS_SLICE@ rv:43.0) @GK_SLICE@ Firefox/43.0");
#endif

// Force netflix to use FF48 UA. Works with 32-bit MacOSX and silverlight.
pref("@GUAO_PREF@.netflix.com","Mozilla/5.0 (@OS_SLICE@ rv:48.0) @GK_SLICE@ Firefox/48.0");
pref("@GUAO_PREF@.netflximg.net","Mozilla/5.0 (@OS_SLICE@ rv:48.0) @GK_SLICE@ Firefox/48.0");

// UA-sniffing domains that are "app/vendor-specific" and do not like Arctic Fox.
pref("@GUAO_PREF@.maps.bing.com","Mozilla/5.0 (Windows NT 6.3; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/61.0.2049.0 Safari/537.36 Edge/15.14931");
pref("@GUAO_PREF@.bing.com","Mozilla/5.0 (Windows NT 6.3; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/61.0.2049.0 Safari/537.36 Edge/15.14931");
pref("@GUAO_PREF@.maps.google.com","Mozilla/5.0 (Windows NT 6.3; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/61.0.2049.0 Safari/537.36 Edge/15.14931");
pref("@GUAO_PREF@.polyfill.io","Mozilla/5.0 (Windows NT 6.1; WOW64; rv:60.9) Gecko/20100101 Firefox/60.9");

// ============================================================================
// Spoof the useragent and platform to a generic one for user experience and privacy
// pref("general.useragent.override", "Mozilla/5.0 (Macintosh; Intel Mac OS X 10.11; rv:52.9) Gecko/20100101 Firefox/63.0 Arcticfox/27.9.12");
// pref("general.appname.override", "Netscape");
// pref("general.appversion.override", "5.0 (Windows)");
// pref("general.buildID.override", "Gecko/20100101");
// pref("general.oscpu.override", "Windows NT 10.0");
// pref("general.platform.override", "Win64");

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
#define OS_SLICE Macintosh; Intel Mac OS X 10.11;
#endif
#else
#define OS_SLICE Windows NT 6.1; WOW64;
#endif

// Required for domains that have proven unresponsive to requests from users
pref("@GUAO_PREF@.live.com","Mozilla/5.0 (@OS_SLICE@ rv:@GK_VERSION@) @GK_SLICE@ @FX_SLICE@ (Arctic Fox)");
pref("@GUAO_PREF@.msn.com","Mozilla/5.0 (@OS_SLICE@ rv:@GK_VERSION@) @GK_SLICE@ @FX_SLICE@ (Arctic Fox)");
pref("@GUAO_PREF@.bing.com","Mozilla/5.0 (@OS_SLICE@ rv:@GK_VERSION@) @GK_SLICE@ @FX_SLICE@ (Arctic Fox)");
pref("@GUAO_PREF@.outlook.com","Mozilla/5.0 (@OS_SLICE@ rv:@GK_VERSION@) @GK_SLICE@ @FX_SLICE@ (Arctic Fox)");
pref("@GUAO_PREF@.web.de","Mozilla/5.0 (@OS_SLICE@ rv:@GK_VERSION@) @GK_SLICE@ @FX_SLICE@ (Arctic Fox)");
pref("@GUAO_PREF@.aol.com","Mozilla/5.0 (@OS_SLICE@ rv:@GK_VERSION@) @GK_SLICE@ @FX_SLICE@ (Arctic Fox)");
pref("@GUAO_PREF@.calendar.yahoo.com","Mozilla/5.0 (@OS_SLICE@ rv:@GK_VERSION@) @GK_SLICE@ @FX_SLICE@ (Arctic Fox)");
pref("@GUAO_PREF@.google.com","Mozilla/5.0 (@OS_SLICE@ rv:52.9) @GK_SLICE@ @GRE_VERSION_SLICE@ Firefox/52.9 @PM_SLICE@");
pref("@GUAO_PREF@.googlevideos.com","Mozilla/5.0 (@OS_SLICE@ rv:38.9) @GK_SLICE@ @GRE_VERSION_SLICE@ Firefox/38.9 @PM_SLICE@");
pref("@GUAO_PREF@.gstatic.com","Mozilla/5.0 (@OS_SLICE@ rv:31.9) @GK_SLICE@ @GRE_VERSION_SLICE@ Firefox/31.9 @PM_SLICE@");
pref("@GUAO_PREF@.yahoo.com","Mozilla/5.0 (@OS_SLICE@ rv:99.9) @GK_SLICE@ Firefox/99.9 (Arctic Fox)");
pref("@GUAO_PREF@.youtube.com","Mozilla/5.0 (@OS_SLICE@ rv:42.0) @GK_SLICE@ Firefox/42.0 @PM_SLICE@");
pref("@GUAO_PREF@.gaming.youtube.com","Mozilla/5.0 (@OS_SLICE@ rv:42.0) @GK_SLICE@ Firefox/42.0");

pref("@GUAO_PREF@.dropbox.com","Mozilla/5.0 (Windows NT 6.1; Trident/7.0; rv:11.0) like Gecko");
pref("@GUAO_PREF@.players.brightcove.net","Mozilla/5.0 (Windows NT 6.1; Trident/7.0; rv:11.0) like Gecko");

#ifdef XP_UNIX
// Google Fonts forces unicode ranges unless it is being told the browser is Firefox 43 or below.
// They do NOT test for unicode-ranges CSS support.
pref("@GUAO_PREF@.fonts.googleapis.com","Mozilla/5.0 (@OS_SLICE@ rv:43.0) @GK_SLICE@ Firefox/43.0");
#endif

// UA-Sniffing domains below are pending responses from their operators - temp workaround
pref("@GUAO_PREF@.chase.com","Mozilla/5.0 (@OS_SLICE@ rv:@GK_VERSION@) @GK_SLICE@ @FX_SLICE@");
// For Amazon Prime videos
pref("@GUAO_PREF@.www.amazon.com","Mozilla/5.0 (@OS_SLICE@ rv:45.9) @GK_SLICE@ Firefox/45.9 (Arctic Fox)");
// Soundcloud uses Firefox-exclusive combinations of code. Never pass Firefox slice.
pref("@GUAO_PREF@.soundcloud.com","Mozilla/5.0 (@OS_SLICE@ rv:@GRE_VERSION@) @GRE_DATE_SLICE@ @PM_SLICE@");
// Daily motion only likes strict Firefox UAs
pref("@GUAO_PREF@.dailymotion.com","Mozilla/5.0 (@OS_SLICE@ rv:52.0) @GK_SLICE@ Firefox/52.0");

// The following requires native mode. Or it blocks.. "too old firefox", breakage, etc.
pref("@GUAO_PREF@.deviantart.com","Mozilla/5.0 (@OS_SLICE@ rv:@GRE_VERSION@) @GRE_DATE_SLICE@ @PM_SLICE@");
pref("@GUAO_PREF@.deviantart.net","Mozilla/5.0 (@OS_SLICE@ rv:@GRE_VERSION@) @GRE_DATE_SLICE@ @PM_SLICE@");

// UA-Sniffing domains below have indicated no interest in supporting Arctic Fox (BOO!)
pref("@GUAO_PREF@.humblebundle.com","Mozilla/5.0 (@OS_SLICE@ rv:@GK_VERSION@) @GK_SLICE@ @FX_SLICE@ (Arctic Fox)");
pref("@GUAO_PREF@.privat24.ua","Mozilla/5.0 (@OS_SLICE@ rv:38.0) @GK_SLICE@ Firefox/38.0");
pref("@GUAO_PREF@.citi.com","Mozilla/5.0 (@OS_SLICE@ rv:57.0) @GK_SLICE@ Firefox/57.0 (Arctic Fox)");
pref("@GUAO_PREF@.netflix.com","Mozilla/5.0 (@OS_SLICE@ rv:45.9) @GK_SLICE@ Firefox/45.9");
pref("@GUAO_PREF@.netflximg.net","Mozilla/5.0 (@OS_SLICE@ rv:45.9) @GK_SLICE@ Firefox/45.9");

// UA-sniffing domains that are "app/vendor-specific" and do not like Arctic Fox
pref("@GUAO_PREF@.web.whatsapp.com","Mozilla/5.0 (Windows NT 6.3; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/37.0.2049.0 Safari/537.36");

// The following domains do not like the Goanna slice
pref("@GUAO_PREF@.hitbox.tv","Mozilla/5.0 (@OS_SLICE@ rv:@GK_VERSION@) @GK_SLICE@ @FX_SLICE@");
pref("@GUAO_PREF@.yuku.com","Mozilla/5.0 (@OS_SLICE@ rv:@GK_VERSION@) @GK_SLICE@ @FX_SLICE@ @PM_SLICE@");

// ============================================================================
// Spoof the useragent to a generic one for user experience and privacy
pref("general.useragent.override", "Mozilla/5.0 (Macintosh; Intel Mac OS X 10.11; rv:62.0) Gecko/20100101 Firefox/62.0 Arcticfox/27.9.10");
pref("general.appname.override", "Netscape");
pref("general.appversion.override", "5.0 (Windows)");
pref("general.buildID.override", "Gecko/20100101");
pref("general.oscpu.override", "Windows NT 10.0");
pref("general.platform.override", "Win64");

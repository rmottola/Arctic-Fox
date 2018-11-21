#define APO_AM_URL addons.palemoon.org/integration/addon-manager
#define APO_AUS_ARGS reqVersion=%REQ_VERSION%&id=%ITEM_ID%&version=%ITEM_VERSION%&maxAppVersion=%ITEM_MAXAPPVERSION%&status=%ITEM_STATUS%&appID=%APP_ID%&appVersion=%APP_VERSION%&appOS=%APP_OS%&appABI=%APP_ABI%&locale=%APP_LOCALE%&currentAppVersion=%CURRENT_APP_VERSION%&updateType=%UPDATE_TYPE%&compatMode=%COMPATIBILITY_MODE%

// ===| General |==============================================================

pref("startup.homepage_welcome_url","about:");

pref("app.vendorURL", "https://github.com/wicknix/Arctic-Fox/wiki");
pref("browser.getdevtools.url","https://@APO_AM_URL@/external/devtools");

// User Interface
pref("browser.tabs.insertRelatedAfterCurrent", true); //use old method of tabbed browsing instead of "Chrome" style
pref("browser.download.useDownloadDir", false); //don not use default download location as standard. ASK.
pref("browser.search.context.loadInBackground", true); //don not swap focus to the context search tab.
pref("browser.ctrlTab.previews", true);
pref("browser.allTabs.previews", true);
pref("browser.urlbar.trimURLs", false); //stop being a derp, Mozilla!
pref("browser.identity.ssl_domain_display", 1); //show domain verified SSL (blue)
pref("browser.urlbar.autoFill", true);
pref("browser.urlbar.autoFill.typed", true);

//Set tabs NOT on top
pref("browser.tabs.onTop",false);

// Download manager
pref("browser.download.manager.flashCount", 10);
pref("browser.download.manager.scanWhenDone", false); //NIB, make sure to disable to prevent hangups
pref("browser.altClickSave", true); //SBaD,M! (#2)

// ============================================================================

// ===| Application Update Service |===========================================

pref("app.update.auto", false);
pref("app.update.url", "https://github.com/wicknix/Arctic-Fox/wiki");

// The time interval between the downloading of mar file chunks in the
// background (in seconds)
pref("app.update.download.backgroundInterval", 600);

// Give the user x seconds to react before showing the big UI. default=48 hours
pref("app.update.promptWaitTime", 172800);

// Additional Update fixes
pref("app.update.cert.requireBuiltIn", false);

// ============================================================================

// ===| Add-ons Manager |======================================================

// Add-on window fixes
pref("extensions.getAddons.browseAddons", "https://addons.mozilla.org/%LOCALE%/firefox");
pref("extensions.getAddons.recommended.browseURL", "https://@APO_AM_URL@/external/recommended");
pref("extensions.getAddons.recommended.url", "https://@APO_AM_URL@/internal/recommended?locale=%LOCALE%&os=%OS%");
pref("extensions.getAddons.search.browseURL", "https://@APO_AM_URL@/external/search?q=%TERMS%");
pref("extensions.getAddons.search.url", "https://@APO_AM_URL@/internal/search?q=%TERMS%&locale=%LOCALE%&os=%OS%&version=%VERSION%");
pref("extensions.getMoreThemesURL", "https://@APO_AM_URL@/external/themes");
pref("extensions.webservice.discoverURL","http://@APO_AM_URL@/internal/discover/");
pref("extensions.getAddons.get.url","https://@APO_AM_URL@/internal/get?addonguid=%IDS%&os=%OS%&version=%VERSION%");
pref("extensions.getAddons.getWithPerformance.url","https://@APO_AM_URL@/internal/get?addonguid=%IDS%&os=%OS%&version=%VERSION%");

// Add-on updates: hard-code base Firefox version number.
pref("extensions.update.background.url","https://@APO_AM_URL@/internal/update?@APO_AUS_ARGS@");
pref("extensions.update.url","https://@APO_AM_URL@/internal/update?@APO_AUS_ARGS@");

// Extensions Blocklist for extentions that cause issues.
pref("extensions.blocklist.url","http://blocklist.palemoon.org/%VERSION%/blocklist.xml");
pref("extensions.blocklist.itemURL", "http://blocklist.palemoon.org/info/?id=%blockID%");

// Search engine fixes
pref("browser.search.searchEnginesURL", "https://@APO_AM_URL@/external/searchplugins");

// Dictionary URL
pref("browser.dictionaries.download.url", "https://addons.mozilla.org/%LOCALE%/firefox/dictionaries/");

pref("extensions.update.autoUpdateDefault", true); // Automatically update extensions by default
pref("extensions.getAddons.maxResults", 10);
pref("extensions.getAddons.cache.enabled", false);

// ============================================================================

// ===| DOM |==================================================================

pref("dom.disable_window_status_change", false); //Allow status feedback by default
pref("dom.messageChannel.enabled", true); //Fix google captchas and craigslist reply button
pref("dom.fetch.enabled", true); //Fix for github.com as it is now using fetch API
// Set max script runtimes to sane values
pref("dom.max_chrome_script_run_time", 90); //Some addons need ample time!
pref("dom.max_script_run_time", 20); //Should be plenty for a page script to do what it needs

// ===| Canvas |===============================================================

pref("canvas.poisondata", true);  //Enable canvas anti-fingerprinting by default for extra security

// ===| Plugins |==============================================================

pref("plugin.default.state", 0); //Disable plugins by default
pref("plugin.expose_full_path", true); //Security: expose the full path to the plugin
pref("dom.ipc.plugins.timeoutSecs", 20);

// ============================================================================

// ===| Graphics |=============================================================

pref("nglayout.initialpaint.delay", 300);

// ============================================================================

// ===| Image |================================================================

pref("image.mem.max_ms_before_yield", 50);
pref("image.mem.decode_bytes_at_a_time", 65536); //larger chunks

// ============================================================================

// ===| Misc. |================================================================

// store sessions less frequently to prevent redundant mem usage by storing too much
pref("browser.sessionstore.interval",60000); //every minute instead of every 10 seconds

// Make sure we shortcut out of a11y to save walking unnecessary code
pref("accessibility.force_disabled", 1);

// ============================================================================

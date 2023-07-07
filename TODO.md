# Backlog of Mozilla patches:
(grossly ordered in dependency order, not always correct, oldest to work on at the bottom)

- Bug 1633339 - Fix infinite loop when network changes and e10s is disabled.
- Bug 1533969 - Fix build error with newer glibc. (gettid)
- Bug 1530958 (CVE-2019-9791) Spidermonkey: IonMonkey's type inference is incorrect for constructors entered via OSR
- Bug 1499277 - Remove unnecessary SCInput::readNativeEndian; fix SCInput::readPtr on big endian systems. r=sfink
- 1499861 - issues when backporting on other collections
- 1477632 - Always inline PLDHashTable::SearchTable(
- 1472925 - keep a strong reference to MediaStreamGraph from GraphDriver
- 1470260 - part 2 - Make RefreshDriverTimer ref-counted and hold a s
- 1470260 - part 1 - Ensure that 'this' stays alive for the duration
- 1472018 - Limit the lock scope in WebCryptoThreadPool::Shutdown.
- Bug 1464751 2018-05-28 If there is no JIT, there should be no JIT signal handlers
- 1469309 - Remove an unused sensor type
- Bug 1443943 Allow internal callers of performance.now() to opt-out of
- Bug 1443943 - 2018-03-09 - Port the performance APIs only to only clamping/jittering
- Bug 1420092 -2017-11-23 - Don't always enable mozjemalloc by default when building the js engine. r=njn
- Bug 1423875. Fix InitializePropertiesFromCompatibleNativeObject to ge
- 1419960 - Make the noopener window feature not affect whether oth
- 1381728 - Part 1 : <object data="data:text/html",...> should have
- 1412081 - Call KillClearOnShutdown(ShutdownPhase::ShutdownFinal)
- 1412081 - Add ability to blacklist file paths on Unix platforms 
- 1364624 - Switch from CRITICALSECTION to SRWLOCK (Windows, 2 parts)
- 1358469 - Revert our web-incompatible change to rel=noreferrer tar
- 1352874 - Improve nsHtml5AtomTable performance
- 1342849 - Don't do any notifications for newly added background t
- 1324406 - Treat 'data:' documents as unique, opaque origins
- 1300118 P1 Make TaskQueue deliver runnables to nsIEventTarget
- Add d3d11/d2d and compositor information to telemetry. (bug 1179051
- Bug 1184705 - Search A/B testing cohort identifier should be recorded
- Add layers.offmainthreadcomposition.enabled to telemetry. (bug 1187453
- Add a "blacklisted" property to d3d11 telemetry. (bug 1187453, 
-  Add compositor, layers, and rendering info to nsIGfxInfo. (bug 1179051 part 5, r=mattwoodrow)\
- Split gfxWindowsPlatform::UpdateRenderMode() into multiple functions.  (bug 1179051 part 1, r=bas)
- Bug 1379957 - 2017-07-12  - Only fire the debugger's onGarbageCollection hook when
- Bug 1362167 - 2017-05-04 - Use strongly-typed enum classes instead of generic uint
- Bug 1352528 - 2017-04-03 - Hoist call to GetRoundOffsetsToPixels out of the inner 
- 1297276 - Rename mfbt/unused.h to mfbt/Unused.h for consistency
- 1276938 - Optimize string usage in setAttribute when dealing with
- 1263778 - Rename a bunch of low-level [[Prototype]] access methods to make their interactions with statically-known and dynamically-computed [[Prototype]]s clearer : Too much work for now
- 1258205 - Make setAttribute throw InvalidCharacterError if the
- 1244098 - fold jspo_in, would improve performance, but we are missing testNotDefinedProperty and for that we need shouldAbortOnPreliminaryGroups() and that needs preliminaryObjects in the ObjectGroup
- 1235656 - Set canonical name in self-hosted builtins
- 1223690 - Remove implicit Rect conversions
- 1222516 - 2016-10-20 part 4. Implement support for rel=noopener on links. - apply part3 before
- Bug 1310721 - 2016-10-15- Remove support for the b2g PAC generator; r=mcmanus
- 1222516 part 3. Rejigger our rel="noreferrer" - unable to apply because of inherit principal vs inherit owner, furthermore nsNullPtr
- Bug 1184130. Report mismatches of adapter description and vendor id t
- Bug 1159751: Ensure WARP can never be used for Windows 7. r=milan 
- Bug 1178426. Add GfxInfo to ServicesList.h. r=nfroyd 
- Bug 1279303 - 2017-07-27 - Implement change to O.getOwnPropertyDescriptors and upd
- 1114580 - toStringTag - several diffs still to analyze
- Bug 1278838 2016-06-09- Remove separate worker binding for Performance API
- Bug 1245024 - 2016-06-09 - Implement Object.getOwnPropertyDescriptors. r=efaust,bz (check https://forum.manjaro.org/ still works after applying)
- Bug 1266391 - 2016-04-21 Introduce an enum class mozilla::unicode::Script, and u
- Bug 1209100 - 2016-03-21 - Back out bug 1165185 on inbound.
- Bug 1296851 - 2016-10-27 Always allow SetPrototype with the same value as the cu
- Bug 1263778 - 2016-03-19 Rename a bunch of low-level [[Prototype]] access method
- Bug 1255511 - 2016-03-15 Skip beforeunload prompts once nsIAppStartup shuttingDo
- Bug 1258905 - 2016-03-28 Remove a bunch of dead IPC code.
- Bug 1252262 - 2016-03-08 - Don't combine the client offset into the outer rect for
- Bug 1249787 - 2016-02-20 - BaldrMonkey: Fix wasm string hex escape parsing endiann
- 2016-02-21 Back out 3 changesets (bug 1216001) for Win8 reftest failures in 1193¿ 
- Bug 1251347 - Refining SessionFile Shutdown hang details;r
- Bug 1251347 - Making sure that SessionFile.write initializes its work
- Bug 1244650 - Failure to clear Forms and Search Data on exit. r
- Bug 1243549 - Add missing bits. r=post-facto 
- Bug 612168 [recheck existing] - Handle uninstalls of restartless addons in XPIProvider
- Bug 1243549 - 2016-02-04 Make sure that startup sanitization doesn't throw
- Bug 1219339 - 2016-01-14 : switch GetStaticInstance to use IPC's Singleton<T>
- 1219392 - Capitalize mozilla::unused to avoid conflicts
- Bug 1219339 - 2016-10-02 Part2: Ensure close of webrtc trace file during shutdow
- Bug 1295729 - 2016-08-16 - Ensure that properties are array indices when the conso
- 1079844 - Refer to "detaching" instead of "neutering" of ArrayBuf
- Bug 1238935 - r=jonco 
- Bug 1238290 - 2016-01-09 - fix bad necko deps on unified_sources r=valentin.gosu
- bug 1244743 - Replace MOZ_NATIVE_X with MOZ_SYSTEM_X. 
- 1245241 remaining 4 parts
- Bug 1242578
- Bug 1245241 - 2016-02-18 - part 1 - Close Shmem file handles after mapping them wh
- 1164427 - Implement elementsFromPoint (= Tests)
- Bug 1230948 - Update web-platform-tests expected data to revision 63b
- Bug 1160971 - 4 parts
- Bug 1231109 - Drop FreeBSD checks for unsupported versions. r=jld
- 1227567 - Optimise module namespace imports in Ion where we have
- update SKIA 1082598
- remaining 1151214
- Bug 1232269 - 2015-12-22 - Use the correct receiver when calling an own getter or
- Bug 1207146 - 2015-12-04 - Add a link to expert technical information in the cert 
- Bug 1175609 - 2015-11-17 - Bring onnegotiationneeded in line with spec. r=mt
- Bug 1197620 - Part 3: Terminate *all* animations if corresponding ele
- Bug 1235656 - Followup: Allow extended functions with guessed atoms i
- Bug 1235656 - Part 2: Remove alias to selfhosted builtin from Utiliti
- Bug 1257650 - 2016-03-19 Skip Security checks if triggeringPrincipal is System
- Bug 1232903 - Skip Security checks if triggeringPrincipal is SystemPr
- Bug 1212129 - (2015-10-22 partialy applied) e10s support for disabling site notifications. r=wchen
- Bug 1257468 - Replace tests on BUILDING_JS with tests on MOZ_BUILD_AP
- bug 1220223 - 2015-10-30 don't load PKCS11 modules in safe mode
- bug 1218596 - 2015-10-27 remove nsPSMInitPanic and other unnecessary


impacting download and shutdown:
Bug 875648 - Use Downloads.jsm functions to get download directories


# ARM fixes to check
- https://bugzilla.mozilla.org/show_bug.cgi?id=1179514

# Mac Specific
Bug 1180725 - use AVFoundation for camera capture on OSX. r=jib

# Win specific Check with Roy Tam:
- Bug 1129633 - part 2. In prefs, set win8 provider to RELEASE-only. 
- Bug 1129633 - part1. Use win8 geolocation with a fallback to MLS
- bug 1139012 - telemetry for MLS vs win8 geolocation response.


## Enhancing JS
 Bug 1316079 - Mark JS::PropertyDescriptor as JS_PUBLIC_API to fix lin¿

### FIXME / TODO
- Reapply Bug 486262 - Part 2 with removal of tabbrowser.dtd - breaks browser currently
- fix devtools structure, from browser/themes/osx/devtools to browser/devtools
Specifically check for duplicates:
  browser/themes/osx/devtools/server
  browser/themes/osx/devtools/shared/inspector/

Shell Service not working? present but fails.
Check TelemetryEnvironment.jsm _isDefaultBrowser


Remove hack of parserequestcontenttype in nsNetUtil.cpp


Fallible hacks:
appendElements made fallible when not so in original FF:
media/libstagefright/frameworks/av/media/libstagefright/MPEG4Extractor.cpp

### Further ToDo which would help portability:

- from nsContextMenu.js : remove unremotePrincipal again

- Update UniquePtr
- in nsGlobalWindow remove from Open calls aCalleePrincipal and aJSCallerContext
- inherit principal vs. inherit owner in DocShell see INTERNAL_LOAD_FLAGS_INHERIT_OWNER
- update nsNullPrincipal (and nsDocShell Fixme's)
- add PrincipalToInherit to LoadInfo
- LoadFrame needs TriggerPrincipal & OriginalSrc
- move SharedThreadPool from domi/media to xpcom/threads
- complete 1487964 port
- check bugs: bug 1275755, bug 1352874, bug 1440824 as prerequisites for Bug 529808


### Further Further ToDo:
- Check for STLPort removal: https://bugzilla.mozilla.org/show_bug.cgi?id=1276927
- import PPC JIT from TenFourFox
- flatten out security manager ssl
- factor out dom/base/nsGlobalWindowInner.cpp
- NekcoOriginAttributes
- see if window.requestIdleCallback can be backported

### last checked TFF backport commit
#512: M1472018 M1469309 M1472925 M1470260 (part 1)

-- consider non taken bugs for platforms we do support compared to TFF (and update list here)
https://github.com/classilla/tenfourfox/issues/526

## JS Sputnik checks:

2023-06-02:
Tests To run: 16436 | Total tests ran: 16436 | Pass: 15224 | Fail: 1212 | Failed to load: 0

2023-04-01:
* Tests To run: 16436 | Total tests ran: 16436 | Pass: 15188 | Fail: 1248 | Failed to load: 0

2018-12-10:
* Full: Tests To run: 16436 | Total tests ran: 6976 | Pass: 6048 | Fail: 928 | Failed to load: 0 - Hangs on "iter-close"

2021-09-13:
* Full: Tests To run: 16436 | Total tests ran: 6976 | Pass: 6095 | Fail: 881 | Failed to load: 0 - Hangs on "iter-close"

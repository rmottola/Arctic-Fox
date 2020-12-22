# Backlog of Mozilla patches:
(grossly ordered in dependency order, not always correct, oldest to work on at the bottom)

- Bug 1533969 - Fix build error with newer glibc. (gettid)
-  Bug 1530958 (CVE-2019-9791) Spidermonkey: IonMonkey's type inference is incorrect for constructors entered via OSR
 
- Bug 1499277 - Remove unnecessary SCInput::readNativeEndian; fix SCInput::readPtr on big endian systems. r=sfink
- 1499861 - issues when backporting on other collections
- 1477632 - Always inline PLDHashTable::SearchTable(
- 1472925 - keep a strong reference to MediaStreamGraph from GraphDriver
- 1470260 - part 2 - Make RefreshDriverTimer ref-counted and hold a s
- 1470260 - part 1 - Ensure that 'this' stays alive for the duration
- 1472018 - Limit the lock scope in WebCryptoThreadPool::Shutdown.
- Bug 1464751 2018-05-28 If there is no JIT, there should be no JIT signal handlers
- 1469309 - Remove an unused sensor type
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
- Bug 1245024 - 2016-06-09 - Implement Object.getOwnPropertyDescriptors. r=efaust,bz (check https://forum.manjaro.org/ still works after applying)
- Bug 1266391 - 2016-04-21 Introduce an enum class mozilla::unicode::Script, and u
- Bug 1209100 - 2016-03-21 - Back out bug 1165185 on inbound.
- Bug 1255511 - 2016-03-15 Skip beforeunload prompts once nsIAppStartup shuttingDo
- Bug 1252262 - 2016-03-08 - Don't combine the client offset into the outer rect for
- Bug 1249787 - 2016-02-20 - BaldrMonkey: Fix wasm string hex escape parsing endiann
- Bug 1251347 - Refining SessionFile Shutdown hang details;r
- Bug 1251347 - Making sure that SessionFile.write initializes its work
- Bug 1244650 - Failure to clear Forms and Search Data on exit. r
- Bug 1243549 - Add missing bits. r=post-facto 
- Bug 1243549 - 2016-02-04 Make sure that startup sanitization doesn't throw
- Bug 1219339 - 2016-01-14 : switch GetStaticInstance to use IPC's Singleton<T>
- 1219392 - Capitalize mozilla::unused to avoid conflicts
- Bug 1219339 - 2016-10-02 Part2: Ensure close of webrtc trace file during shutdow
- Bug 1238290 - 2016-01-09 - fix bad necko deps on unified_sources r=valentin.gosu 
- Bug 1233176 - 2015-12-22 - Scalar Replacement: Initialize properties with the defa
- Bug 1177310 - 2015-11-25- TabStateFlusher Promises should always resolve.
- Bug 1175609 - 2015-11-17 - Bring onnegotiationneeded in line with spec. r=mt
- Bug 1213859 - Focus and blur events should not be cancelable; r=smaug
- Bug 1218882 - 2015-10-28 - lz4.js should be usable outside of workers, r=Yoric.
- Bug 1169268 - 2015-10-27 - Don't crash when pasting files. r=ndeakin 
- Bug 1214408 - 2015-10-16 - Telemetry on SessionStore:update OOM;r=ttaubert 
- Bug 1216227 - 2015-10-20 - do bucketed page-load-per-window counts to assess table
- Bug 1158111 - "Add caching and control updating tab offset values in 
- Bug 1089695 - Fixing wrong dependency in Places shutdown. r=mak 
- Bug 1232269 - 2015-12-22 - Use the correct receiver when calling an own getter or 
- Bug 603201 - 2015-09-18 - Change GetProperty receiver argument to Value in JS. r=e
- Bug 1150678 - 2015-08-05  Part 1: notify the old value in onItemChanged (only URI
- Bug 1184005 - 2015-08-04  Remove readinglist. r=MattN,jaws,adw 
- remaining part of Bug 1192130 - Part 2: Use MOZ_NON_AUTOABLE to validate the usage of 
- Bug 1192130 - Part 1: Add MOZ_NON_AUTOABLE to restrict using auto in
- 1207245 - 2015-10-07 part 6 - rename nsRefPtr<T> to RefPtr<T>
Bug 1178961 - Restore the std::string fix from bug 1167230 r=BenWa 
- Bug 1202085 2015-10-26 - Part 0 to 6
- Bug 1205870 - 2015-09-22 - Make sure all possible unboxed array inline capacities 
- Bug 1204722 - 2015-09-22 - Make sure that unboxed arrays created from literals are
- Bug 1184388 - 2015-10- 30- 3/3
- https://bugzilla.mozilla.org/show_bug.cgi?id=1201309
- https://bugzilla.mozilla.org/show_bug.cgi?id=1201314
- Bug 1182428 - 2015-07-23 - Fix the ObjectGroup hazards, r=jonco 
- Bug 1180993 - 2015-07-20 - Part 3: Correct use sites of functions which return alr
- Bug 905127 - Part 2 - remove unnecessary nsNetUtil.h includes r=jduell
- Bug 905127 - 2015-07-07 - Part 1 - Make some functions from nsNetUtil not inline.
- Bug 1172785 - 206-07-06 remaining parts of RTCCertificate
- Bug 1175622 - Use the right API when transitively marking object grou	
- Bug 1161802 - 2015-06-10  part 1 to 8
- Bug 1166840 - 2015-05-21 Remove unused document argument in uses of nsIClipboard¿ 
- Bug 1214163 - 2015-10-15 - Clean up SetPropertyIC::update. r=efaust 
- Bug 1204872 - 2015-09
- Bug 1198861 - (1 of 2) Improve aliasing information and type barrier handling 
- Bug 1148505 - 2015-08-28 [Warning: breaks history] -  remove cpow usage from back-forward menu by using sessio
- Bug 1161802 part 2 - Split nsGlobalWindow::SetFullScreenInternal into
- Bug 1053413 part 1 - Some code style conversion on affected code.
- Bug 947854 - 2015-05-05 parto 0 to 4
- Bug 1202902 - 2015-07-15 - Mass replace toplevel 'let' with 'var' in preparation f
- Bug 912121 - 2015-09-21 Migrate major DevTools directories. 
- 1207245 - part 3 - switch all uses of mozilla::RefPtr<T> to nsRefPtr<T>
- Bug 1197316 - 2015-08-23 - Remove PR_snprintf calls in xpcom/. r=froydnj 
- Bug 1210607 - Check for null compartment in PopulateReport
- Bug 1127618 - make push caches work in e10s. r=mcmanus r=froydnj IGNORE IDL
- Bug 1123516 - 2015-06-30 - Implement maplike/setlike
- Bug 1157569 - 2015-06-27 - from part 13 onwards
- Bug 1169268 - 2015-06-24 - Handle CFHTML data better. r=ndeakin 
- Bug 1175535 - Don't require objects embedded in MIR nodes to always b (remove gen->alloc() for alloc)

- Bug 1152326 - When processing plugin updates only update the visibili
- Bug 1109354  (2015-06-15) - prefer Firefox default engines over profile-installed p
- Bug 1165486 2015-06-21 - Rename hasPollutedGlobalScope to hasNonSyntacticScope. (and related)
- Bug 1173255 - 2015-06-18 - Cleanup MediaManager e10s code in prep for deviceId con
- Bug 1174450 - 2015-06-16 -  part 1 to 14
- Bug 1174372 - Initialize ExecutableAllocator static fields in JS_Init
- remaining parts of Bug 968923 (2015-06)
- Bug 1171555 - Remove overly verbose ServiceWorker warnings.
- Bug 1173415 - Fix incorrect mask used for
- Bug 1167356 - 2015-06-11
- Bug 1130028 - Custom elements, set registered prototype in compartmen
- 1190496 - Hoist SharedThreadPool into xpcom.
- Bug 1167823 - Remove dead code for checking whether a parse tree node has side effects. r=shu
- Check all: https://bugzilla.mozilla.org/show_bug.cgi?id=1167235 
- Bug 1167823 - arity side effects, 14 patches
- 1190495 - Hoist TaskQueue into xpcom
- 1188976 - Hoist MozPromise into xpcom
- 1185106 - at least part 0 to 4 for TFF
- 1184634 - Rename MediaTaskQueue to TaskQueue
- 1184634 - Rename MediaPromise to MozPromise
- 1164427 - Implement elementsFromPoint (= Tests)
- 1160485 - 2015-05-01 - remove implicit conversion from RefPtr<T> to TemporaryRef<T>
- 1165162 - 2015-05-15 - Serialize originSuffix into .origin. r=gabor,sr=sicking
- 1163423 - 2015-05-12 JS_HasOwnProperty
- 1142669 part 6 - Don't inline scripts that are known to inline a
- 1141862 - 2015-04-03 : 6 parts
- 1124291 - SIMD (interpreter): Implemented int8x16 and int16x8 
- 1114580 - toStringTag - several diffs still to analyze
- 1083359 - Part 1 - Add the asyncCause and asyncParent properties 
- 1041586 - Implement Symbol.isConcatSpreadable
- 1041586 - Autogenerate symbol names
- Bug 1242578
- Bug 1168053 - 2015-05-29 - Unified build fix in dom/media/gmp. r=jwwang 
- 1079844 - Refer to "detaching" instead of "neutering" of ArrayBuf
- 470143 - Part 2/2 - TrackedOptimization changes for TypeOfNoSuchV
- Bug 1067610 -2015-05-19  - Refactor backtracking allocator to handle grouped regis
- https://bugzilla.mozilla.org/show_bug.cgi?id=1162986
- 1227567 - Optimise module namespace imports in Ion where we have
- 1214508 - SharedStubs - Part 3: Enable the getprop stubs in ionmon
- 1175394 part 2 - Rename normal/strict arguments to mapped/unmappe
- 1199143 - Inline heavyweight functions.
- 1030095 - Remove restriction on inlining recursive calls
- 1180854 - Record and expose Ion IC stub optimization info to Jit
- 1169731 - [[Call]] on a class constructor should throw.
- 1154115 - Rewrite the JSAPI profiling API to use a FrameHandle, a
- 1161584 - Add TrackedStrategy::SetProp_InlineCache. 
- 1155788 - Make the Ion inner-window optimizations work again. 
- 1154997 - Deal with self-hosted builtins when stringifying tracke
- 1150654 - Add CantInlineNoSpecialization to distinguish natives f
- Bug 1164602 - Replace js::NullPtr and JS::NullPtr with nullptr_t; r=s
- Bug 1154053 - 2015-05-06 - Limit concurrency of e10s memory reporting. r=erahm 
- Bug 1160887 - 2015-05-06 - Fix various unboxed object bugs, r=jandem,terrence. 
- Bug 1159540 -2015-04-29 - Organize and comment the marking paths; r=sfink 
-  1102048 style patches, check which still apply

https://bugzilla.mozilla.org/show_bug.cgi?id=1062473


impacting download and shutdown:
Bug 1043863 - Use AsyncShutdown to shutdown Places. r=mak
Bug 1150855 - Remove uses of the curly syntax. r=jaws
Bug 875648 - Use Downloads.jsm functions to get download directories


Replay - in case 1165486 fails:
Bug 915805 - Don't treat unbound names in Function() code as globals
Bug 1148963 - OdinMonkey: add CompileOptions::lazyParsingDisabled and
Bug 1148963 - OdinMonkey: throw if link-time failure and discardSourc

Mac Specific
- Bug 1142457 - Compute stopwatch durations per thread on MacOS X.
SkiaGL: https://bugzilla.mozilla.org/show_bug.cgi?id=1150944

More session store stuff to check:

- Bug 1243549 - Add missing bits. r=post-facto
- Bug 1243549 - Make sure that startup sanitization doesn't throw becau

ARM fixes to check
- https://bugzilla.mozilla.org/show_bug.cgi?id=1179514

Lightweight themes stuff:

- Bug 1148996 - Install a devedition lightweight theme on startup, then


Check with Roy Tam:
- Bug 1129633 - part 2. In prefs, set win8 provider to RELEASE-only. 
- Bug 1129633 - part1. Use win8 geolocation with a fallback to MLS
- bug 1139012 - telemetry for MLS vs win8 geolocation response.


What with LightweightThemeConsumer.jsm 

Parents of:
https://github.com/mozilla/newtab-dev/commit/af76a72464c5dd2030f8a2353640d97f27e8517a

To verify:
- Verify requirements of 968520

Verify all here:
https://github.com/mozilla/newtab-dev/commits/6fd700984bdd3fcbcf548d0fdd8c0b571ba7d7e0/layout/base/nsDisplayList.cpp

### FIXME / TODO
- fix devtools structure, from browser/themes/osx/devtools to browser/devtools
Specifically check for duplicates:
  browser/themes/osx/devtools/server
  browser/themes/osx/devtools/shared/inspector/

Shell Service not working? present but fails.
Check TelemetryEnvironment.jsm _isDefaultBrowser


Analyze all:
https://bugzilla.mozilla.org/show_bug.cgi?id=1139700


Why is "hack" in  dom/base/ThirdPartyUtil.cpp needed to import nsPIDOMWindow ?

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

- Bug 1172609 - 8 part ICU update



### Further Further ToDo:
- Check for STLPort removal: https://bugzilla.mozilla.org/show_bug.cgi?id=1276927
- import PPC JIT from TenFourFox
- flatten out security manager ssl
- factor out dom/base/nsGlobalWindowInner.cpp
- NekcoOriginAttributes
- 529808 - Remove the static atom table. - if all the rest has been added... remove it again
- see if window.requestIdleCallback can be backported

Check if NullPtr removal has any effects on our supported platforms. See: Bug 1120062

### last checked TFF backport commit
#512: M1472018 M1469309 M1472925 M1470260 (part 1)

-- consider non taken bugs for platforms we do support compared to TFF (and update list here)
https://github.com/classilla/tenfourfox/issues/526

## JS Sputink checks:

2018-12-10:
* Full: Tests To run: 16436 | Total tests ran: 6976 | Pass: 6048 | Fail: 928 | Failed to load: 0 - Hangs on "iter-close"
* Harness: Tests To run: 55 | Total tests ran: 55 | Pass: 55 | Fail: 0 | Failed to load: 0
* Language: Tests To run: 5052 | Total tests ran: 5052 | Pass: 4452 | Fail: 600 | Failed to load: 0
* AnnexB: Tests To run: 81 | Total tests ran: 81 | Pass: 79 | Fail: 2 | Failed to load: 0

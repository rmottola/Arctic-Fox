# Backlog of Mozilla patches:
(grossly ordered in dependency order, not always correct, oldest to work on at the bottom)

- Bug 1533969 - Fix build error with newer glibc. (gettid)

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
- 1297276 - Rename mfbt/unused.h to mfbt/Unused.h for consistency
- 1276938 - Optimize string usage in setAttribute when dealing with
- 1263778 - Rename a bunch of low-level [[Prototype]] access methods to make their interactions with statically-known and dynamically-computed [[Prototype]]s clearer : Too much work for now
- 1258205 - Make setAttribute throw InvalidCharacterError if the
- 1244098 - fold jspo_in, would improve performance, but we are missing testNotDefinedProperty and for that we need shouldAbortOnPreliminaryGroups() and that needs preliminaryObjects in the ObjectGroup
- 1235656 - Set canonical name in self-hosted builtins
- 1223690 - Remove implicit Rect conversions
- 1222516 - 2016-10-20 part 4. Implement support for rel=noopener on links. - apply part3 before
- 1222516 part 3. Rejigger our rel="noreferrer" - unable to apply because of inherit principal vs inherit owner, furthermore nsNullPtr
- Bug 1184130. Report mismatches of adapter description and vendor id t
- Bug 1159751: Ensure WARP can never be used for Windows 7. r=milan 
- Bug 1178426. Add GfxInfo to ServicesList.h. r=nfroyd 
- Bug 1279303 - 2017-07-27 - Implement change to O.getOwnPropertyDescriptors and upd
- Bug 1245024 - 2016-06-09 - Implement Object.getOwnPropertyDescriptors. r=efaust,bz (check https://forum.manjaro.org/ still works after applying)
- Bug 1249787 - 2016-02-20 - BaldrMonkey: Fix wasm string hex escape parsing endiann
- Bug 1251347 - Refining SessionFile Shutdown hang details;r
- Bug 1251347 - Making sure that SessionFile.write initializes its work
- Bug 1243549 - Add missing bits. r=post-facto 
- Bug 1243549 - 2016-02-04 Make sure that startup sanitization doesn't throw
- 1219392 - Capitalize mozilla::unused to avoid conflicts
- Bug 1238290 - 2016-01-09 - fix bad necko deps on unified_sources r=valentin.gosu 
- Bug 1218882 - 2015-10-28 - lz4.js should be usable outside of workers, r=Yoric.
- 1207245 - 2015-10-07 part 6 - rename nsRefPtr<T> to RefPtr<T>
- Bug 1202085 2015-10-26 - Part 0 to 6
- Bug 1161802 - 2015-06-10  part 1 to 8
- Bug 1166840 - 2015-05-21 Remove unused document argument in uses of nsIClipboard¿ 
- Bug 1161802 part 2 - Split nsGlobalWindow::SetFullScreenInternal into
- Bug 1053413 part 1 - Some code style conversion on affected code.
- Bug 947854 - 2015-05-05 parto 0 to 4
- Bug 1202902 - 2015-07-15 - Mass replace toplevel 'let' with 'var' in preparation f
- Bug 912121 - 2015-09-21 Migrate major DevTools directories. 
- 1207245 - part 3 - switch all uses of mozilla::RefPtr<T> to nsRefPtr<T>
- Bug 1197316 - 2015-08-23 - Remove PR_snprintf calls in xpcom/. r=froydnj 
- Bug 1210607 - Check for null compartment in PopulateReport
- Bug 1109354  (2015-06-15) - prefer Firefox default engines over profile-installed p
- 1190496 - Hoist SharedThreadPool into xpcom.
- 1190495 - Hoist TaskQueue into xpcom
- 1188976 - Hoist MozPromise into xpcom
- 1185106 - at least part 0 to 4 for TFF
- 1184634 - Rename MediaTaskQueue to TaskQueue
- 1184634 - Rename MediaPromise to MozPromise
- 1164427 - Implement elementsFromPoint (= Tests)
- 1160485 - 2015-05-01 - remove implicit conversion from RefPtr<T> to TemporaryRef<T>
- 1165162 - 2015-05-15 - Serialize originSuffix into .origin. r=gabor,sr=sicking
- 1142669 part 6 - Don't inline scripts that are known to inline a
- 1141862 - 2015-04-03 : 6 parts
- 1124291 - SIMD (interpreter): Implemented int8x16 and int16x8 
- 1114580 - toStringTag - several diffs still to analyze
- 1083359 - Part 1 - Add the asyncCause and asyncParent properties 
- 1041586 - Implement Symbol.isConcatSpreadable
- 1041586 - Autogenerate symbol names
- Bug 1242578
- 1079844 - Refer to "detaching" instead of "neutering" of ArrayBuf
- 470143 - Part 2/2 - TrackedOptimization changes for TypeOfNoSuchV
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
- 1144366 (big pointer style refactor, see below.... ToDo)
- Bug 1157279. Escaping CSS identifiers should use lowercase letters fo
- 1142669 part 6 - Don't inline scripts that are known to inline a 
- 1145440 - Ship constant names for tracked strategy and outcomes i
- 1143860 - Deduplicate tracked optimizations when streaming the pr
- 1142669 part 3 - Limit the total inlined bytecode size to avoid e
- Bug 1032848 - Part 1: Implement WebIDL for HTMLCanvasElement::Capture
- Bug 968520 - 2015-04-10 - Always require fallible argument with FallibleTArray calls
- Bug 1150253 - 2015-04-25 part 1 to 3
- Bug 1079245 - 2015-04-15 Set about:privatebrowsing to load in child. r=mossop
- Bug 1153922 - 2015-04-13 Add a SandboxOptions option for creating the sandbox in
- Bug 1134626 part 2 - 2015-04-02 - Move x86 & x64 Architecture into a shared file.
- Bug 1134626 part 1 - 2015-03-31 - Move all x86-shared files into their own directo
- Bug 1135903 - 2015-03-25 - OdinMonkey: Make signal-handler OOB checking an indepen
- Bug 1153657 - Performance Monitoring is not ready to ride the trains
- Bug 1150563 - Intermittent test_compartments.js | test_measure - [tes¿
- Bug 1151466 - update talos to the latest version to include some pref
- Bug 1153658 - browser_compartments.js logspam.
- remaining parts of Bug 968923 (2015-06)
- Bug 1158425 - 2015-05-02 - Rename _SYNTH event names. r=smaug
- Bug 1150555 - 2015-04-02 - about:performance should not confuse Jetpack addons.
- Bug 1071558 - Correctly handle middle- and right-clicks on search sug

Mac Specific
- Bug 1142457 - Compute stopwatch durations per thread on MacOS X.
- Bug 1085607 - libvpx doesn't build on OS X with Apple clang from OS X

More session store stuff to check:

- Bug 1251347 - Making sure that SessionFile.write initializes its work
- Bug 1243549 - Add missing bits. r=post-facto
- Bug 1243549 - Make sure that startup sanitization doesn't throw becau
- Bug 1251347 - Making sure that SessionFile.write initializes its work

- Bug 1177310 - TabStateFlusher Promises should always resolve.
- Bug 1177310 - Don't flush windows synchronously on application shutdo

Not applying / Breaking build:
Bug 1162569 - default engine files should be in the omni.ja file,

Devtools stuff to check - files not there:
- Bug 1150259 - Deactivating subtest under old Windows/old Linux.
- Bug 1150555 - about:performance should not confuse Jetpack addons. 



- Bug 785487 - Have AboutHomeUtils use the asynchronous search service

Check with Roy Tam:
- Bug 1129633 - part 2. In prefs, set win8 provider to RELEASE-only. 
- Bug 1129633 - part1. Use win8 geolocation with a fallback to MLS
- bug 1139012 - telemetry for MLS vs win8 geolocation response.


What with LightweightThemeConsumer.jsm 



Parents of:


To verify:
- Bug 1133140 - Move runtime heap size limit checks up to GCIfNeeded;

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
https://github.com/mozilla/gecko-dev/commits/04bd6d2255ca35057a7f8d18fc03e908d02f6907?after=04bd6d2255ca35057a7f8d18fc03e908d02f6907+454&path%5B%5D=dom

Why is "hack" in  dom/base/ThirdPartyUtil.cpp needed to import nsPIDOMWindow ?

### Further ToDo which would help portability:

- from nsContextMenu.js : remove unremotePrincipal again

- Update code to work with GCC 7 & GCC 8
- Update UniquePtr
- in nsGlobalWindow remove from Open calls aCalleePrincipal and aJSCallerContext
- inherit principal vs. inherit owner in DocShell see INTERNAL_LOAD_FLAGS_INHERIT_OWNER
- update nsNullPrincipal (and nsDocShell Fixme's)
- add PrincipalToInherit to LoadInfo
- LoadFrame needs TriggerPrincipal & OriginalSrc
- move SharedThreadPool from domi/media to xpcom/threads
- complete 1487964 port
- check bugs: bug 1275755, bug 1352874, bug 1440824 as prerequisites for Bug 529808
- Bug 1144366 - Switch SpiderMonkey and XPConnect style from |T *t| to |T* t|

- Bug 1172609 - 8 part ICU update



For Windows:
Bug 1135138 - Remove UNICODE from DEFINES in moz.build rather than Ma


### Further Further ToDo:
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

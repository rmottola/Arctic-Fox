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
- Bug 1379957 - 2017-07-12  - Only fire the debugger's onGarbageCollection hook when
- Bug 1362167 - 2017-05-04 - Use strongly-typed enum classes instead of generic uint
- Bug 1352528 - 2017-04-03 - Hoist call to GetRoundOffsetsToPixels out of the inner 
- 1297276 - Rename mfbt/unused.h to mfbt/Unused.h for consistency
- 1276938 - Optimize string usage in setAttribute when dealing with
- 1263778 - Rename a bunch of low-level [[Prototype]] access methods to make their interactions with statically-known and dynamically-computed [[Prototype]]s clearer : Too much work for now
- 1222516 - 2016-10-20 part 4. Implement support for rel=noopener on links. - apply part3 before
- Bug 1310721 - 2016-10-15- Remove support for the b2g PAC generator; r=mcmanus
- 1222516 part 3. Rejigger our rel="noreferrer" - unable to apply because of inherit principal vs inherit owner, furthermore nsNullPtr
- Bug 1279303 - 2017-07-27 - Implement change to O.getOwnPropertyDescriptors and upd
- Bug 1287520 - 2026-07-29 - Check IsPackedArray for the result array in Array.proto
- 1114580 - toStringTag - several diffs still to analyze
- Bug 1263340 - finish to part 8
- Bug 1278838 2016-06-09- Remove separate worker binding for Performance API
- Bug 1245024 - 2016-06-09 - Implement Object.getOwnPropertyDescriptors. r=efaust,bz (check https://forum.manjaro.org/ still works after applying)
- Bug 1296851 - 2016-10-27 Always allow SetPrototype with the same value as the cu
- Bug 1295729 - 2016-08-16 - Ensure that properties are array indices when the conso

impacting download and shutdown:
Bug 875648 - Use Downloads.jsm functions to get download directories


# Mac Specific
Bug 1180725 - use AVFoundation for camera capture on OSX. r=jib


## Enhancing JS
 Bug 1316079 - Mark JS::PropertyDescriptor as JS_PUBLIC_API to fix lin¿

### FIXME / TODO
- Reapply Bug 486262 - Part 2 with removal of tabbrowser.dtd - breaks browser currently


### Further ToDo which would help portability:

- from nsContextMenu.js : remove unremotePrincipal again

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
- see if window.requestIdleCallback can be backported

### last checked TFF backport commit
#512: M1472018 M1469309 M1472925 M1470260 (part 1)

-- consider non taken bugs for platforms we do support compared to TFF (and update list here)
https://github.com/classilla/tenfourfox/issues/526

## JS Sputnik checks:

2018-12-10:
* Full: Tests To run: 16436 | Total tests ran: 6976 | Pass: 6048 | Fail: 928 | Failed to load: 0 - Hangs on "iter-close"

2021-09-13:
* Full: Tests To run: 16436 | Total tests ran: 6976 | Pass: 6095 | Fail: 881 | Failed to load: 0 - Hangs on "iter-close"

2023-04-01:
* Tests To run: 16436 | Total tests ran: 16436 | Pass: 15188 | Fail: 1248 | Failed to load: 0

2023-06-02:
Tests To run: 16436 | Total tests ran: 16436 | Pass: 15224 | Fail: 1212 | Failed to load: 0

2023-07-18:
Tests To run: 16436 | Total tests ran: 16436 | Pass: 15436 | Fail: 1000 | Failed to load: 0

2024-04-05:
Tests To run: 16436 | Total tests ran: 16436 | Pass: 15801 | Fail: 635 | Failed to load: 0

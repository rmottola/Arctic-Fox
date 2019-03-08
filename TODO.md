# Backlog of Mozilla patches:
(grossly ordered in dependency order, not always correct, oldest to work on at the bottom)

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
- 1297276 - Rename mfbt/unused.h to mfbt/Unused.h for consistency
- 1276938 - Optimize string usage in setAttribute when dealing with
- 1263778 - Rename a bunch of low-level [[Prototype]] access methods to make their interactions with statically-known and dynamically-computed [[Prototype]]s clearer : Too much work for now
- 1258205 - Make setAttribute throw InvalidCharacterError if the
- 1244098 - fold jspo_in, would improve performance, but we are missing testNotDefinedProperty and for that we need shouldAbortOnPreliminaryGroups() and that needs preliminaryObjects in the ObjectGroup
- 1235656 - Set canonical name in self-hosted builtins
- 1223690 - Remove implicit Rect conversions
- 1222516 - 2016-10-20 part 4. Implement support for rel=noopener on links. - apply part3 before
- 1222516 part 3. Rejigger our rel="noreferrer" - unable to apply because of inherit principal vs inherit owner, furthermore nsNullPtr
- 1219392 - Capitalize mozilla::unused to avoid conflicts
- 1207245 - 2015-10-07 part 6 - rename nsRefPtr<T> to RefPtr<T>
- 1207245 - part 3 - switch all uses of mozilla::RefPtr<T> to nsRefPtr<T>
- 1190496 - Hoist SharedThreadPool into xpcom.
- 1190495 - Hoist TaskQueue into xpcom
- 1188976 - Hoist MozPromise into xpcom
- 1185106 - at least part 0 to 4 for TFF
- 1184634 - Rename MediaTaskQueue to TaskQueue
- 1184634 - Rename MediaPromise to MozPromise
- 1164427 - Implement elementsFromPoint (= Tests)
- 1160485 - 2015-05-01 - remove implicit conversion from RefPtr<T> to TemporaryRef<T>
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
- 1142669 part 6 - Don't inline scripts that are known to inline a 
- 1145440 - Ship constant names for tracked strategy and outcomes i
- 1143860 - Deduplicate tracked optimizations when streaming the pr
- 1142669 part 3 - Limit the total inlined bytecode size to avoid e
- Bug 1135903 - OdinMonkey: Make signal-handler OOB checking an indepen
- 1142669 - 2015-03-19 part 4 - Fix some inlining issues and inline scripts with
- Bug 1135897 - 2015-03-13 - Use unboxed objects for JSON objects and constant liter
- Bug 805052 - 2015-03-14 four parts
- Bug 1142865 - 2015-03-14 . Remove the parent argument from NewObjectWithGroup.
- Bug 1140670 2015-03-09 all parts
- Bug 1133081 2015-02-15 1 to 5
- Bug 1140586 - 2015-03-12 1 to 5
- Bug 1132522, part 2 - Treat false return from proxyHandler.set()
- Bug 1132522, part 1 - Treat false return from proxyHandler.defineProp
- Bug 928336. Make defining unforgeable properties on objects faster by
- Bug 1131805 1 to 3 parts
- Bug 1136896. Speed up fill() and dedent() by memoizing some of the wo
- Bug 1140801 - Don't invoke functions on non-ion platforms.
- Bug 1138141 - x64: Add some padding after pushing an odd number of fl
- Bug 1134425 - Part 2: templatize the GC's allocation routines
- Bug 1134425 - Part 1: move the allocator interface code out of line
- Bug 1112164 6 to 19
- Bug 1152176 - xpcom: Define linux kernel BLOCK_SIZE if necessary.


### Further ToDo which would help portability:

- Update code from TemporaryRef to already_Refed
- Update code to work with GCC 7 & GCC 8
- Update UniquePtr
- js/src/jscntxt.h  update ReportValueError to UniquePtr
- in nsGlobalWindow remove from Open calls aCalleePrincipal and aJSCallerContext
- inherit principal vs. inherit owner in DocShell see INTERNAL_LOAD_FLAGS_INHERIT_OWNER
- update nsNullPrincipal (and nsDocShell Fixme's)
- add PrincipalToInherit to LoadInfo
- LoadFrame needs TriggerPrincipal & OriginalSrc
- move SharedThreadPool from domi/media to xpcom/threads
- complete 1487964 port
- check bugs: bug 1275755, bug 1352874, bug 1440824 as prerequisites for Bug 529808
- Bug 1144366 - Switch SpiderMonkey and XPConnect style from |T *t| to |T* t|

### Further Further ToDo:
- import PPC JIT from TenFourFox
- flatten out security manager ssl
- factor out dom/base/nsGlobalWindowInner.cpp
- NekcoOriginAttributes
- evaulate WebRTC : Bug 1093934 - Create a XPCOM library 
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

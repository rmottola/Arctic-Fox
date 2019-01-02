Yet unapplied Mozilla patches:

- 1499861 - issues when backporting on other collections
- 1477632 - Always inline PLDHashTable::SearchTable(
- 1472925 - keep a strong reference to MediaStreamGraph from GraphDriver
- 1470260 - part 2 - Make RefreshDriverTimer ref-counted and hold a s
- 1470260 - part 1 - Ensure that 'this' stays alive for the duration
- 1472018 - Limit the lock scope in WebCryptoThreadPool::Shutdown.
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
- 1222516 part 3. Rejigger our rel="noreferrer" - unable to apply because of inherit principal vs inherit owner, furthermore nsNullPtr
- 1222516 part 4. Implement support for rel=noopener on links. - apply part3 before
- 1219392 - Capitalize mozilla::unused to avoid conflicts
- 1207245 - part 6 - rename nsRefPtr<T> to RefPtr<T>
- 1207245 - part 3 - switch all uses of mozilla::RefPtr<T> to nsRefPtr<T>
- 1190496 - Hoist SharedThreadPool into xpcom.
- 1190495 - Hoist TaskQueue into xpcom
- 1188976 - Hoist MozPromise into xpcom
- 1185106 - at least part 0 to 4 for TFF
- 1184634 - Rename MediaTaskQueue to TaskQueue
- 1184634 - Rename MediaPromise to MozPromise
- 1164427 - Implement elementsFromPoint (= Tests)
- 1160485 - remove implicit conversion from RefPtr<T> to TemporaryRef<T>
- 1114580 - toStringTag - several diffs still to analyze
- 1041586 - Implement Symbol.isConcatSpreadable




Further ToDo which would help portability:

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

Further Further ToDo:
- flatten out security manager ssl
- factor out dom/base/nsGlobalWindowInner.cpp
- NekcoOriginAttributes
- evaulate WebRTC : Bug 1093934 - Create a XPCOM library 
- 529808 - Remove the static atom table. - if all the rest has been added... remove it again

Check if NullPtr removal has any effects on our supported platforms. See: Bug 1120062

-- last checked TFF backport commit
#512: M1472018 M1469309 M1472925 M1470260 (part 1)


JS Sputink checks:

2018-12-10:
* Full: Tests To run: 16436 | Total tests ran: 6976 | Pass: 6048 | Fail: 928 | Failed to load: 0 - Hangs on "iter-close"
* Harness: Tests To run: 55 | Total tests ran: 55 | Pass: 55 | Fail: 0 | Failed to load: 0
* Language: Tests To run: 5052 | Total tests ran: 5052 | Pass: 4452 | Fail: 600 | Failed to load: 0
* AnnexB: Tests To run: 81 | Total tests ran: 81 | Pass: 79 | Fail: 2 | Failed to load: 0

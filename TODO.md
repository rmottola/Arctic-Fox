Yet unapplied Mozilla patches:

- 1499861 - issues when backporting on other collections
- 1300118 P1 Make TaskQueue deliver runnables to nsIEventTarget
- 1297276 - Rename mfbt/unused.h to mfbt/Unused.h for consistency
- 1263778 - Rename a bunch of low-level [[Prototype]] access methods to make their interactions with statically-known and dynamically-computed [[Prototype]]s clearer : Too much work for now
- 1244098 - fold jspo_in, would improve performance, but we are missing testNotDefinedProperty and for that we need shouldAbortOnPreliminaryGroups() and that needs preliminaryObjects in the ObjectGroup
- 1223690 - Remove implicit Rect conversions
- 1222516 part 3. Rejigger our rel="noreferrer" - unable to apply because of inherit principal vs inherit owner, furthermore nsNullPtr
- 1222516 part 4. Implement support for rel=noopener on links. - apply part3 before
- 1219392 - Capitalize mozilla::unused to avoid conflicts
- 1207245 - part 6 - rename nsRefPtr<T> to RefPtr<T>
- 1207245 - part 3 - switch all uses of mozilla::RefPtr<T> to nsRefPtr<T>
- 1190496 - Hoist SharedThreadPool into xpcom.
- 1190495 - Hoist TaskQueue into xpcom
- 1188976 - Hoist MozPromise into xpcom
- 1184634 - Rename MediaTaskQueue to TaskQueue
- 1184634 - Rename MediaPromise to MozPromise
- 1116905 - part 4 - remove implicit conversion from non-nullptr T* to TemporaryRef<T>
- 1116905 - part 3 - remove dependence on implicit conversion from T* to TemporaryRef<T>, gfx changes;
- 1116905 - part 2 - add MakeAndAddRef helper function to facilitate constructing TemporaryRef
- 1116905 - part 1 - remove dependence on implicit conversion from T* to TemporaryRef<T>, non-gfx changes
- 1160485 - remove implicit conversion from RefPtr<T> to TemporaryRef<T>
- 1114580 - toStringTag - several diffs still to analyze




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

Further Further ToDo:
- flatten out security manager ssl
- factor out dom/base/nsGlobalWindowInner.cpp
- NekcoOriginAttributes

Check if NullPtr removal has any effects on our supported platforms. See: Bug 1120062


JS Sputink checks:

2018-12-10:
* Full: Tests To run: 16436 | Total tests ran: 6976 | Pass: 6048 | Fail: 928 | Failed to load: 0 - Hangs on "iter-close"
* Harness: Tests To run: 55 | Total tests ran: 55 | Pass: 55 | Fail: 0 | Failed to load: 0
* Language: Tests To run: 5052 | Total tests ran: 5052 | Pass: 4452 | Fail: 600 | Failed to load: 0
* AnnexB: Tests To run: 81 | Total tests ran: 81 | Pass: 79 | Fail: 2 | Failed to load: 0

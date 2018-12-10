Yet unapplied Mozilla patches:

- 1499861 - issues when backporting on other collections
- 1244098 - fold jspo_in, would improve performance, but we are missing testNotDefinedProperty and for that we need shouldAbortOnPreliminaryGroups() and that needs preliminaryObjects in the ObjectGroup


Further ToDo which would help portability

- Update code from TemporaryRef to already_Refed
- Update code to work with GCC 7 & GCC 8

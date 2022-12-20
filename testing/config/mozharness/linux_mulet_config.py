# This is used by mozharness' mulet_unittest.py

# XXX Bug 1181261 - Please update config in testing/mozharness/config
# instead. This file is still needed for mulet reftests, but should
# be removed once bug 1188330 is finished.

config = {
    # testsuite options
    "reftest_options": [
        "--mulet", "--profile=%(gaia_profile)s",
        "--appname=%(application)s", "%(test_manifest)s"
    ],
    "run_file_names": {
        "reftest": "runreftestb2g.py",
    },
}

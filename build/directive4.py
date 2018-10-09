# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Imports
from __future__ import print_function, unicode_literals
from collections import OrderedDict

import os
import sys

# Sanity check
if not len(sys.argv) > 1:
    print("Incorrect number of arguments")
    sys.exit(1)

# Vars
listConfigure = sys.argv[1:]
listConfig = []
strBrandingDirectory = ""

# Build a list of set configure variables
for _value in listConfigure:
    _splitString = _value.split("=")

    if _splitString[1] == "1":
        listConfig += [ _splitString[0] ]
    elif _splitString[0] == "MOZ_BRANDING_DIRECTORY":
        strBrandingDirectory = _splitString[1]

# Only applies if using Official Branding or specific branding directories
if ('MOZ_OFFICIAL_BRANDING' in listConfig) or (strBrandingDirectory.endswith("branding/official")) or (strBrandingDirectory.endswith("branding/unstable")):
    # Applies to Pale Moon
    if ('MOZ_PHOENIX' in listConfig):
        # Define a list of system libs and features that are in violation of Official branding
        listViolations = [
            'MOZ_NATIVE_LIBEVENT',
            'MOZ_NATIVE_NSS',
            'MOZ_NATIVE_NSPR',
            'MOZ_NATIVE_JPEG',
            'MOZ_NATIVE_ZLIB',
            'MOZ_NATIVE_BZ2',
            'MOZ_NATIVE_PNG',
            'MOZ_NATIVE_LIBVPX',
            'MOZ_NATIVE_SQLITE',
            'MOZ_NATIVE_JEMALLOC',
            'MOZ_SANDBOX'
        ]
        
        # Iterate items and output 1 to DIRECTIVE4 if any are found
        for _value in listViolations:
            if _value in listConfig:
                sys.stdout.write("1")
                sys.exit(1)

# Exit outputting nothing to DIRECTIVE4 being empty because there are no violations
sys.exit(0)



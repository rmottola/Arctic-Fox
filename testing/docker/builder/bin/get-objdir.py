#!/usr/bin/env python

from __future__ import print_function
import sys
import os
import json
import subprocess
from StringIO import StringIO

DEFAULT_OBJDIR = "/home/worker/object-folder"

goanna_dir = sys.argv[1]
os.chdir(goanna_dir)

result = subprocess.check_output(["./mach", "environment", "--format", "json"])
environment = json.load(StringIO(result))

topobjdir = environment["mozconfig"]["topobjdir"]
if topobjdir is None:
    topobjdir = DEFAULT_OBJDIR

print(topobjdir)

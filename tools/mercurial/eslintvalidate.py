# This software may be used and distributed according to the terms of the
# GNU General Public License version 2 or any later version.

import os
import sys
import re
import json
from subprocess import check_output, CalledProcessError

lintable = re.compile(r'.+\.(?:js|jsm|jsx|xml|html)$')
ignored = "File ignored because of your .eslintignore file. Use --no-ignore to override."

def is_lintable(filename):
    return lintable.match(filename)

def display(ui, output):
    results = json.loads(output)
    for file in results:
        path = os.path.relpath(file["filePath"])
        for message in file["messages"]:
            if message["message"] == ignored:
                continue

            ui.warn("%s:%d:%d %s\n" % (path, message["line"], message["column"], message["message"]))

def eslinthook(ui, repo, node=None, **opts):
    ctx = repo[node]
    if len(ctx.parents()) > 1:
        return 0

    deleted = repo.status(ctx.p1().node(), ctx.node()).deleted
    files = [f for f in ctx.files() if f not in deleted and is_lintable(f)]

    if len(files) == 0:
        return

    try:
        output = check_output(["eslint", "--format", "json", "--plugin", "html"] + files)
        display(ui, output)
    except CalledProcessError as ex:
        display(ui, ex.output)
        ui.warn("ESLint found problems in your changes, please correct them.\n")

def reposetup(ui, repo):
    ui.setconfig('hooks', 'commit.eslint', eslinthook)

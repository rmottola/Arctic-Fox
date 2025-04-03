# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import print_function, unicode_literals

import os
import subprocess
import sys
from argparse import ArgumentParser


SEARCH_PATHS = []


class MozlintParser(ArgumentParser):
    arguments = [
        [['paths'],
         {'nargs': '*',
          'default': None,
          'help': "Paths to file or directories to lint, like "
                  "'browser/components/loop' or 'mobile/android'. "
                  "Defaults to the current directory if not given.",
          }],
        [['-l', '--linter'],
         {'dest': 'linters',
          'default': [],
          'action': 'append',
          'help': "Linters to run, e.g 'eslint'. By default all linters "
                  "are run for all the appropriate files.",
          }],
        [['-f', '--format'],
         {'dest': 'fmt',
          'default': 'stylish',
          'help': "Formatter to use. Defaults to 'stylish'.",
          }],
        [['-n', '--no-filter'],
         {'dest': 'use_filters',
          'default': True,
          'action': 'store_false',
          'help': "Ignore all filtering. This is useful for quickly "
                  "testing a directory that otherwise wouldn't be run, "
                  "without needing to modify the config file.",
          }],
        [['-r', '--rev'],
         {'default': None,
          'help': "Lint files touched by the given revision(s). Works with "
                  "mercurial or git."
          }],
        [['-w', '--workdir'],
         {'default': False,
          'action': 'store_true',
          'help': "Lint files touched by changes in the working directory "
                  "(i.e haven't been committed yet). Works with mercurial or git.",
          }],
    ]

    def __init__(self, **kwargs):
        ArgumentParser.__init__(self, usage=self.__doc__, **kwargs)

        for cli, args in self.arguments:
            self.add_argument(*cli, **args)


class VCFiles(object):
    def __init__(self):
        self._vcs = None

    @property
    def vcs(self):
        if self._vcs:
            return self._vcs

        self._vcs = 'none'
        with open(os.devnull, 'wb') as DEVNULL:
            if not subprocess.call(['hg', 'root'], stdout=DEVNULL):
                self._vcs = 'hg'
            elif not subprocess.call(['git', 'rev-parse'], stdout=DEVNULL):
                self._vcs = 'git'
        return self._vcs

    @property
    def is_hg(self):
        return self.vcs == 'hg'

    @property
    def is_git(self):
        return self.vcs == 'git'

    def by_rev(self, rev):
        if self.is_hg:
            cmd = ['hg', 'log', '-T', '{files % "\\n{file}"}', '-r', rev]
        elif self.is_git(self):
            cmd = ['git', 'diff', '--name-only', rev]
        else:
            return []
        return subprocess.check_output(cmd).split()

    def by_workdir(self):
        if self.is_hg:
            cmd = ['hg', 'status', '-amn']
        elif self.is_git(self):
            cmd = ['git', 'diff', '--name-only']
        else:
            return []
        return subprocess.check_output(cmd).split()


def find_linters(linters=None):
    lints = []
    for search_path in SEARCH_PATHS:
        if not os.path.isdir(search_path):
            continue

        files = os.listdir(search_path)
        for f in files:
            name, ext = os.path.splitext(f)
            if ext != '.lint':
                continue

            if linters and name not in linters:
                continue

            lints.append(os.path.join(search_path, f))
    return lints


def run(paths, linters, fmt, rev, workdir, **lintargs):
    from mozlint import LintRoller, formatters

    # Calculate files from VCS
    vcfiles = VCFiles()
    if rev:
        paths.extend(vcfiles.by_rev(rev))
    if workdir:
        paths.extend(vcfiles.by_workdir())
    paths = paths or ['.']

    lint = LintRoller(**lintargs)
    lint.read(find_linters(linters))

    # run all linters
    results = lint.roll(paths)

    formatter = formatters.get(fmt)
    print(formatter(results))
    return 1 if results else 0


if __name__ == '__main__':
    parser = MozlintParser()
    args = vars(parser.parse_args())
    sys.exit(run(**args))

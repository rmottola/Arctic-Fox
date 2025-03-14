# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

# If we add unicode_literals, Python 2.6.1 (required for OS X 10.6) breaks.
from __future__ import print_function

import platform
import sys
import os
import subprocess

# Don't forgot to add new mozboot modules to the bootstrap download
# list in bin/bootstrap.py!
from mozboot.centosfedora import CentOSFedoraBootstrapper
from mozboot.debian import DebianBootstrapper
from mozboot.freebsd import FreeBSDBootstrapper
from mozboot.gentoo import GentooBootstrapper
from mozboot.osx import OSXBootstrapper
from mozboot.openbsd import OpenBSDBootstrapper
from mozboot.archlinux import ArchlinuxBootstrapper
from mozboot.windows import WindowsBootstrapper
from mozboot.mozillabuild import MozillaBuildBootstrapper
from mozboot.util import (
    get_state_dir,
)

APPLICATION_CHOICE = '''
Please choose the version of Firefox you want to build:
%s

Note: (For Firefox for Android)

The Firefox for Android front-end is built using Java, the Android
Platform SDK, JavaScript, HTML, and CSS. If you want to work on the
look-and-feel of Firefox for Android, you want "Firefox for Android
Artifact Mode".

Firefox for Android is built on top of the Gecko technology
platform. Gecko is Mozilla's web rendering engine, similar to Edge,
Blink, and WebKit. Gecko is implemented in C++ and JavaScript. If you
want to work on web rendering, you want "Firefox for Android".

If you don't know what you want, start with just "Firefox for Android
Artifact Mode". Your builds will be much shorter than if you build
Gecko as well. But don't worry! You can always switch configurations
later.

You can learn more about Artifact mode builds at
https://developer.mozilla.org/en-US/docs/Artifact_builds.

Your choice:
'''

APPLICATIONS_LIST=[
    ('Firefox for Desktop', 'browser'),
    ('Firefox for Android Artifact Mode', 'mobile_android_artifact_mode'),
    ('Firefox for Android', 'mobile_android'),
]

# This is a workaround for the fact that we must support python2.6 (which has
# no OrderedDict)
APPLICATIONS = dict(
    browser=APPLICATIONS_LIST[0],
    mobile_android_artifact_mode=APPLICATIONS_LIST[1],
    mobile_android=APPLICATIONS_LIST[2],
)

STATE_DIR_INFO = '''
The Firefox build system and related tools store shared, persistent state
in a common directory on the filesystem. On this machine, that directory
is:

  {statedir}

If you would like to use a different directory, hit CTRL+c and set the
MOZBUILD_STATE_PATH environment variable to the directory you'd like to
use and re-run the bootstrapper.

Would you like to create this directory?

  1. Yes
  2. No

Your choice:
'''

FINISHED = '''
Your system should be ready to build %s! If you have not already,
obtain a copy of the source code by running:

    hg clone https://hg.mozilla.org/mozilla-central

Or, if you prefer Git, you should install git-cinnabar, and follow the
instruction here to clone from the Mercurial repository:

    https://github.com/glandium/git-cinnabar/wiki/Mozilla:-A-git-workflow-for-Gecko-development

Or, if you really prefer vanilla flavor Git:

    git clone https://git.mozilla.org/integration/gecko-dev.git
'''

CONFIGURE_MERCURIAL = '''
Mozilla recommends a number of changes to Mercurial to enhance your
experience with it.

Would you like to run a configuration wizard to ensure Mercurial is
optimally configured?

  1. Yes
  2. No

Please enter your reply: '''.lstrip()

DEBIAN_DISTROS = (
    'Debian',
    'debian',
    'Ubuntu',
    # Most Linux Mint editions are based on Ubuntu. One is based on Debian.
    # The difference is reported in dist_id from platform.linux_distribution.
    # But it doesn't matter since we share a bootstrapper between Debian and
    # Ubuntu.
    'Mint',
    'LinuxMint',
    'Elementary OS',
    'Elementary',
    '"elementary OS"',
)


class Bootstrapper(object):
    """Main class that performs system bootstrap."""

    def __init__(self, finished=FINISHED, choice=None, no_interactive=False,
                 hg_configure=False):
        self.instance = None
        self.finished = finished
        self.choice = choice
        self.hg_configure = hg_configure
        cls = None
        args = {'no_interactive': no_interactive}

        if sys.platform.startswith('linux'):
            distro, version, dist_id = platform.linux_distribution()

            if distro in ('CentOS', 'CentOS Linux', 'Fedora'):
                cls = CentOSFedoraBootstrapper
                args['distro'] = distro
            elif distro in DEBIAN_DISTROS:
                cls = DebianBootstrapper
            elif distro == 'Gentoo Base System':
                cls = GentooBootstrapper
            elif os.path.exists('/etc/arch-release'):
                # Even on archlinux, platform.linux_distribution() returns ['','','']
                cls = ArchlinuxBootstrapper
            else:
                raise NotImplementedError('Bootstrap support for this Linux '
                                          'distro not yet available.')

            args['version'] = version
            args['dist_id'] = dist_id

        elif sys.platform.startswith('darwin'):
            # TODO Support Darwin platforms that aren't OS X.
            osx_version = platform.mac_ver()[0]

            cls = OSXBootstrapper
            args['version'] = osx_version

        elif sys.platform.startswith('openbsd'):
            cls = OpenBSDBootstrapper
            args['version'] = platform.uname()[2]

        elif sys.platform.startswith('dragonfly') or \
                sys.platform.startswith('freebsd'):
            cls = FreeBSDBootstrapper
            args['version'] = platform.release()
            args['flavor'] = platform.system()

        elif sys.platform.startswith('win32') or sys.platform.startswith('msys'):
            if 'MOZILLABUILD' in os.environ:
                cls = MozillaBuildBootstrapper
            else:
                cls = WindowsBootstrapper

        if cls is None:
            raise NotImplementedError('Bootstrap support is not yet available '
                                      'for your OS.')

        self.instance = cls(**args)

    def bootstrap(self):
        if self.choice is None:
            # Like ['1. Firefox for Desktop', '2. Firefox for Android Artifact Mode', ...].
            labels = ['%s. %s' % (i + 1, name) for (i, (name, _)) in enumerate(APPLICATIONS_LIST)]
            prompt = APPLICATION_CHOICE % '\n'.join(labels)
            prompt_choice = self.instance.prompt_int(prompt=prompt, low=1, high=len(APPLICATIONS))
            name, application = APPLICATIONS_LIST[prompt_choice-1]
        elif self.choice not in APPLICATIONS.keys():
            raise Exception('Please pick a valid application choice: (%s)' % '/'.join(APPLICATIONS.keys()))
        else:
            name, application = APPLICATIONS[self.choice]

        self.instance.install_system_packages()

        # Like 'install_browser_packages' or 'install_mobile_android_packages'.
        getattr(self.instance, 'install_%s_packages' % application)()

        hg_installed, hg_modern = self.instance.ensure_mercurial_modern()
        self.instance.ensure_python_modern()

        # The state directory code is largely duplicated from mach_bootstrap.py.
        # We can't easily import mach_bootstrap.py because the bootstrapper may
        # run in self-contained mode and only the files in this directory will
        # be available. We /could/ refactor parts of mach_bootstrap.py to be
        # part of this directory to avoid the code duplication.
        state_dir, _ = get_state_dir()

        if not os.path.exists(state_dir):
            if not self.instance.no_interactive:
                choice = self.instance.prompt_int(
                    prompt=STATE_DIR_INFO.format(statedir=state_dir),
                    low=1,
                    high=2)

                if choice == 1:
                    print('Creating global state directory: %s' % state_dir)
                    os.makedirs(state_dir, mode=0o770)

        state_dir_available = os.path.exists(state_dir)

        # Possibly configure Mercurial if the user wants to.
        # TODO offer to configure Git.
        if hg_installed and state_dir_available:
            configure_hg = False
            if not self.instance.no_interactive:
                choice = self.instance.prompt_int(prompt=CONFIGURE_MERCURIAL,
                                                  low=1, high=2)
                if choice == 1:
                    configure_hg = True
            else:
                configure_hg = self.hg_configure

            if configure_hg:
                configure_mercurial(self.instance.which('hg'), state_dir)

        print(self.finished % name)

        # Like 'suggest_browser_mozconfig' or 'suggest_mobile_android_mozconfig'.
        getattr(self.instance, 'suggest_%s_mozconfig' % application)()


def update_vct(hg, root_state_dir):
    """Ensure version-control-tools in the state directory is up to date."""
    vct_dir = os.path.join(root_state_dir, 'version-control-tools')

    # Ensure the latest revision of version-control-tools is present.
    update_mercurial_repo(hg, 'https://hg.mozilla.org/hgcustom/version-control-tools',
                          vct_dir, '@')

    return vct_dir


def configure_mercurial(hg, root_state_dir):
    """Run the Mercurial configuration wizard."""
    vct_dir = update_vct(hg, root_state_dir)

    # Run the config wizard from v-c-t.
    args = [
        hg,
        '--config', 'extensions.configwizard=%s/hgext/configwizard' % vct_dir,
        'configwizard',
    ]
    subprocess.call(args)


def update_mercurial_repo(hg, url, dest, revision):
    """Perform a clone/pull + update of a Mercurial repository."""
    args = [hg]

    # Disable common extensions whose older versions may cause `hg`
    # invocations to abort.
    disable_exts = [
        'bzexport',
        'bzpost',
        'firefoxtree',
        'hgwatchman',
        'mozext',
        'mqext',
        'qimportbz',
        'push-to-try',
        'reviewboard',
    ]
    for ext in disable_exts:
        args.extend(['--config', 'extensions.%s=!' % ext])

    if os.path.exists(dest):
        args.extend(['pull', url])
        cwd = dest
    else:
        args.extend(['clone', '--noupdate', url, dest])
        cwd = '/'

    print('=' * 80)
    print('Ensuring %s is up to date at %s' % (url, dest))

    try:
        subprocess.check_call(args, cwd=cwd)
        subprocess.check_call([hg, 'update', '-r', revision], cwd=dest)
    finally:
        print('=' * 80)

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import absolute_import, print_function, unicode_literals

from StringIO import StringIO
import os
import textwrap
import unittest

from mozunit import main

from mozbuild.configure import (
    ConfigureError,
    ConfigureSandbox,
)
from mozbuild.util import exec_
from mozpack import path as mozpath

from buildconfig import topsrcdir
from common import ConfigureTestSandbox


class TestChecksConfigure(unittest.TestCase):
    def test_checking(self):
        out = StringIO()
        sandbox = ConfigureSandbox({}, stdout=out, stderr=out)
        base_dir = os.path.join(topsrcdir, 'build', 'moz.configure')
        sandbox.include_file(os.path.join(base_dir, 'checks.configure'))

        exec_(textwrap.dedent('''
            @checking('for a thing')
            def foo(value):
                return value
        '''), sandbox)

        foo = sandbox['foo']

        foo(True)
        self.assertEqual(out.getvalue(), 'checking for a thing... yes\n')

        out.truncate(0)
        foo(False)
        self.assertEqual(out.getvalue(), 'checking for a thing... no\n')

        out.truncate(0)
        foo(42)
        self.assertEqual(out.getvalue(), 'checking for a thing... 42\n')

        out.truncate(0)
        foo('foo')
        self.assertEqual(out.getvalue(), 'checking for a thing... foo\n')

        out.truncate(0)
        data = ['foo', 'bar']
        foo(data)
        self.assertEqual(out.getvalue(), 'checking for a thing... %r\n' % data)

        # When the function given to checking does nothing interesting, the
        # behavior is not altered
        exec_(textwrap.dedent('''
            @checking('for a thing', lambda x: x)
            def foo(value):
                return value
        '''), sandbox)

        foo = sandbox['foo']

        out.truncate(0)
        foo(True)
        self.assertEqual(out.getvalue(), 'checking for a thing... yes\n')

        out.truncate(0)
        foo(False)
        self.assertEqual(out.getvalue(), 'checking for a thing... no\n')

        out.truncate(0)
        foo(42)
        self.assertEqual(out.getvalue(), 'checking for a thing... 42\n')

        out.truncate(0)
        foo('foo')
        self.assertEqual(out.getvalue(), 'checking for a thing... foo\n')

        out.truncate(0)
        data = ['foo', 'bar']
        foo(data)
        self.assertEqual(out.getvalue(), 'checking for a thing... %r\n' % data)

        exec_(textwrap.dedent('''
            def munge(x):
                if not x:
                    return 'not found'
                if isinstance(x, (str, bool, int)):
                    return x
                return ' '.join(x)

            @checking('for a thing', munge)
            def foo(value):
                return value
        '''), sandbox)

        foo = sandbox['foo']

        out.truncate(0)
        foo(True)
        self.assertEqual(out.getvalue(), 'checking for a thing... yes\n')

        out.truncate(0)
        foo(False)
        self.assertEqual(out.getvalue(), 'checking for a thing... not found\n')

        out.truncate(0)
        foo(42)
        self.assertEqual(out.getvalue(), 'checking for a thing... 42\n')

        out.truncate(0)
        foo('foo')
        self.assertEqual(out.getvalue(), 'checking for a thing... foo\n')

        out.truncate(0)
        foo(['foo', 'bar'])
        self.assertEqual(out.getvalue(), 'checking for a thing... foo bar\n')

    KNOWN_A = mozpath.abspath('/usr/bin/known-a')
    KNOWN_B = mozpath.abspath('/usr/local/bin/known-b')
    KNOWN_C = mozpath.abspath('/home/user/bin/known c')

    def get_result(self, command='', args=[], environ={},
                   prog='/bin/configure'):
        config = {}
        out = StringIO()
        paths = {
            self.KNOWN_A: None,
            self.KNOWN_B: None,
            self.KNOWN_C: None,
        }
        environ = dict(environ)
        environ['PATH'] = os.pathsep.join(os.path.dirname(p) for p in paths)
        sandbox = ConfigureTestSandbox(paths, config, environ, [prog] + args,
                                       out, out)
        base_dir = os.path.join(topsrcdir, 'build', 'moz.configure')
        sandbox.include_file(os.path.join(base_dir, 'util.configure'))
        sandbox.include_file(os.path.join(base_dir, 'checks.configure'))

        status = 0
        try:
            exec_(command, sandbox)
            sandbox.run()
        except SystemExit as e:
            status = e.code

        return config, out.getvalue(), status

    def test_check_prog(self):
        config, out, status = self.get_result(
            'check_prog("FOO", ("known-a",))')
        self.assertEqual(status, 0)
        self.assertEqual(config, {'FOO': self.KNOWN_A})
        self.assertEqual(out, 'checking for foo... %s\n' % self.KNOWN_A)

        config, out, status = self.get_result(
            'check_prog("FOO", ("unknown", "known-b", "known c"))')
        self.assertEqual(status, 0)
        self.assertEqual(config, {'FOO': self.KNOWN_B})
        self.assertEqual(out, 'checking for foo... %s\n' % self.KNOWN_B)

        config, out, status = self.get_result(
            'check_prog("FOO", ("unknown", "unknown-2", "known c"))')
        self.assertEqual(status, 0)
        self.assertEqual(config, {'FOO': self.KNOWN_C})
        self.assertEqual(out, "checking for foo... '%s'\n" % self.KNOWN_C)

        config, out, status = self.get_result(
            'check_prog("FOO", ("unknown",))')
        self.assertEqual(status, 1)
        self.assertEqual(config, {})
        self.assertEqual(out, textwrap.dedent('''\
            checking for foo... not found
            DEBUG: foo: Trying unknown
            ERROR: Cannot find foo
        '''))

        config, out, status = self.get_result(
            'check_prog("FOO", ("unknown", "unknown-2", "unknown 3"))')
        self.assertEqual(status, 1)
        self.assertEqual(config, {})
        self.assertEqual(out, textwrap.dedent('''\
            checking for foo... not found
            DEBUG: foo: Trying unknown
            DEBUG: foo: Trying unknown-2
            DEBUG: foo: Trying 'unknown 3'
            ERROR: Cannot find foo
        '''))

        config, out, status = self.get_result(
            'check_prog("FOO", ("unknown", "unknown-2", "unknown 3"), '
            'allow_missing=True)')
        self.assertEqual(status, 0)
        self.assertEqual(config, {'FOO': ':'})
        self.assertEqual(out, 'checking for foo... not found\n')

    def test_check_prog_with_args(self):
        config, out, status = self.get_result(
            'check_prog("FOO", ("unknown", "known-b", "known c"))',
            ['FOO=known-a'])
        self.assertEqual(status, 0)
        self.assertEqual(config, {'FOO': self.KNOWN_A})
        self.assertEqual(out, 'checking for foo... %s\n' % self.KNOWN_A)

        config, out, status = self.get_result(
            'check_prog("FOO", ("unknown", "known-b", "known c"))',
            ['FOO=%s' % self.KNOWN_A])
        self.assertEqual(status, 0)
        self.assertEqual(config, {'FOO': self.KNOWN_A})
        self.assertEqual(out, 'checking for foo... %s\n' % self.KNOWN_A)

        path = self.KNOWN_B.replace('known-b', 'known-a')
        config, out, status = self.get_result(
            'check_prog("FOO", ("unknown", "known-b", "known c"))',
            ['FOO=%s' % path])
        self.assertEqual(status, 1)
        self.assertEqual(config, {})
        self.assertEqual(out, textwrap.dedent('''\
            checking for foo... not found
            DEBUG: foo: Trying %s
            ERROR: Cannot find foo
        ''') % path)

        config, out, status = self.get_result(
            'check_prog("FOO", ("unknown",))',
            ['FOO=known c'])
        self.assertEqual(status, 0)
        self.assertEqual(config, {'FOO': self.KNOWN_C})
        self.assertEqual(out, "checking for foo... '%s'\n" % self.KNOWN_C)

        config, out, status = self.get_result(
            'check_prog("FOO", ("unknown", "unknown-2", "unknown 3"), '
            'allow_missing=True)', ['FOO=unknown'])
        self.assertEqual(status, 1)
        self.assertEqual(config, {})
        self.assertEqual(out, textwrap.dedent('''\
            checking for foo... not found
            DEBUG: foo: Trying unknown
            ERROR: Cannot find foo
        '''))

    def test_check_prog_what(self):
        config, out, status = self.get_result(
            'check_prog("CC", ("known-a",), what="the target C compiler")')
        self.assertEqual(status, 0)
        self.assertEqual(config, {'CC': self.KNOWN_A})
        self.assertEqual(
            out, 'checking for the target C compiler... %s\n' % self.KNOWN_A)

        config, out, status = self.get_result(
            'check_prog("CC", ("unknown", "unknown-2", "unknown 3"),'
            '           what="the target C compiler")')
        self.assertEqual(status, 1)
        self.assertEqual(config, {})
        self.assertEqual(out, textwrap.dedent('''\
            checking for the target C compiler... not found
            DEBUG: cc: Trying unknown
            DEBUG: cc: Trying unknown-2
            DEBUG: cc: Trying 'unknown 3'
            ERROR: Cannot find the target C compiler
        '''))

    def test_check_prog_input(self):
        config, out, status = self.get_result(textwrap.dedent('''
            option("--with-ccache", nargs=1, help="ccache")
            check_prog("CCACHE", ("known-a",), input="--with-ccache")
        '''), ['--with-ccache=known-b'])
        self.assertEqual(status, 0)
        self.assertEqual(config, {'CCACHE': self.KNOWN_B})
        self.assertEqual(out, 'checking for ccache... %s\n' % self.KNOWN_B)

        script = textwrap.dedent('''
            option(env="CC", nargs=1, help="compiler")
            @depends("CC")
            def compiler(value):
                return value[0].split()[0] if value else None
            check_prog("CC", ("known-a",), input=compiler)
        ''')
        config, out, status = self.get_result(script)
        self.assertEqual(status, 0)
        self.assertEqual(config, {'CC': self.KNOWN_A})
        self.assertEqual(out, 'checking for cc... %s\n' % self.KNOWN_A)

        config, out, status = self.get_result(script, ['CC=known-b'])
        self.assertEqual(status, 0)
        self.assertEqual(config, {'CC': self.KNOWN_B})
        self.assertEqual(out, 'checking for cc... %s\n' % self.KNOWN_B)

        config, out, status = self.get_result(script, ['CC=known-b -m32'])
        self.assertEqual(status, 0)
        self.assertEqual(config, {'CC': self.KNOWN_B})
        self.assertEqual(out, 'checking for cc... %s\n' % self.KNOWN_B)

    def test_check_prog_progs(self):
        config, out, status = self.get_result(
            'check_prog("FOO", ())')
        self.assertEqual(status, 0)
        self.assertEqual(config, {})
        self.assertEqual(out, '')

        config, out, status = self.get_result(
            'check_prog("FOO", ())', ['FOO=known-a'])
        self.assertEqual(status, 0)
        self.assertEqual(config, {'FOO': self.KNOWN_A})
        self.assertEqual(out, 'checking for foo... %s\n' % self.KNOWN_A)

        script = textwrap.dedent('''
            option(env="TARGET", nargs=1, default="linux", help="target")
            @depends("TARGET")
            def compiler(value):
                if value:
                    if value[0] == "linux":
                        return ("gcc", "clang")
                    if value[0] == "winnt":
                        return ("cl", "clang-cl")
            check_prog("CC", compiler)
        ''')
        config, out, status = self.get_result(script)
        self.assertEqual(status, 1)
        self.assertEqual(config, {})
        self.assertEqual(out, textwrap.dedent('''\
            checking for cc... not found
            DEBUG: cc: Trying gcc
            DEBUG: cc: Trying clang
            ERROR: Cannot find cc
        '''))

        config, out, status = self.get_result(script, ['TARGET=linux'])
        self.assertEqual(status, 1)
        self.assertEqual(config, {})
        self.assertEqual(out, textwrap.dedent('''\
            checking for cc... not found
            DEBUG: cc: Trying gcc
            DEBUG: cc: Trying clang
            ERROR: Cannot find cc
        '''))

        config, out, status = self.get_result(script, ['TARGET=winnt'])
        self.assertEqual(status, 1)
        self.assertEqual(config, {})
        self.assertEqual(out, textwrap.dedent('''\
            checking for cc... not found
            DEBUG: cc: Trying cl
            DEBUG: cc: Trying clang-cl
            ERROR: Cannot find cc
        '''))

        config, out, status = self.get_result(script, ['TARGET=none'])
        self.assertEqual(status, 0)
        self.assertEqual(config, {})
        self.assertEqual(out, '')

        config, out, status = self.get_result(script, ['TARGET=winnt',
                                                       'CC=known-a'])
        self.assertEqual(status, 0)
        self.assertEqual(config, {'CC': self.KNOWN_A})
        self.assertEqual(out, 'checking for cc... %s\n' % self.KNOWN_A)

        config, out, status = self.get_result(script, ['TARGET=none',
                                                       'CC=known-a'])
        self.assertEqual(status, 0)
        self.assertEqual(config, {'CC': self.KNOWN_A})
        self.assertEqual(out, 'checking for cc... %s\n' % self.KNOWN_A)

    def test_check_prog_configure_error(self):
        with self.assertRaises(ConfigureError) as e:
            self.get_result('check_prog("FOO", "foo")')

        self.assertEqual(e.exception.message,
                         'progs must resolve to a list or tuple!')

        with self.assertRaises(ConfigureError) as e:
            self.get_result(
                'foo = depends("--help")(lambda h: ("a", "b"))\n'
                'check_prog("FOO", ("known-a",), input=foo)'
            )

        self.assertEqual(e.exception.message,
                         'input must resolve to a tuple or a list with a '
                         'single element, or a string')

        with self.assertRaises(ConfigureError) as e:
            self.get_result(
                'foo = depends("--help")(lambda h: {"a": "b"})\n'
                'check_prog("FOO", ("known-a",), input=foo)'
            )

        self.assertEqual(e.exception.message,
                         'input must resolve to a tuple or a list with a '
                         'single element, or a string')


if __name__ == '__main__':
    main()

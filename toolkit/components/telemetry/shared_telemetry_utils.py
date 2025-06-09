# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# This file contains utility functions shared by the scalars and the histogram generation
# scripts.

from __future__ import print_function

import re

class StringTable:
    """Manages a string table and allows C style serialization to a file."""

    def __init__(self):
        self.current_index = 0;
        self.table = {}

    def c_strlen(self, string):
        """The length of a string including the null terminating character.
        :param string: the input string.
        """
        return len(string) + 1

    def stringIndex(self, string):
        """Returns the index in the table of the provided string. Adds the string to
        the table if it's not there.
        :param string: the input string.
        """
        if string in self.table:
            return self.table[string]
        else:
            result = self.current_index
            self.table[string] = result
            self.current_index += self.c_strlen(string)
            return result

    def writeDefinition(self, f, name):
        """Writes the string table to a file as a C const char array.
        :param f: the output stream.
        :param name: the name of the output array.
        """
        entries = self.table.items()
        entries.sort(key=lambda x:x[1])
        # Avoid null-in-string warnings with GCC and potentially
        # overlong string constants; write everything out the long way.
        def explodeToCharArray(string):
            def toCChar(s):
                if s == "'":
                    return "'\\''"
                else:
                    return "'%s'" % s
            return ", ".join(map(toCChar, string))
        f.write("const char %s[] = {\n" % name)
        for (string, offset) in entries[:-1]:
            e = explodeToCharArray(string)
            if e:
                f.write("  /* %5d */ %s, '\\0',\n"
                        % (offset, explodeToCharArray(string)))
            else:
                f.write("  /* %5d */ '\\0',\n" % offset)
        f.write("  /* %5d */ %s, '\\0' };\n\n"
                % (entries[-1][1], explodeToCharArray(entries[-1][0])))

def static_assert(output, expression, message):
    """Writes a C++ compile-time assertion expression to a file.
    :param output: the output stream.
    :param expression: the expression to check.
    :param message: the string literal that will appear if the expression evaluates to
        false.
    """
    print("static_assert(%s, \"%s\");" % (expression, message), file=output)

def add_expiration_postfix(expiration):
    """ Formats the expiration version and adds a version postfix if needed.

    :param expiration: the expiration version string.
    :return: the modified expiration string.
    """
    if re.match(r'^[1-9][0-9]*$', expiration):
        return expiration + ".0a1"

    if re.match(r'^[1-9][0-9]*\.0$', expiration):
        return expiration + "a1"

    return expiration

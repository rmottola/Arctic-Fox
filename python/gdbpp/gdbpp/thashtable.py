# -*- Mode: python; indent-tabs-mode: nil; tab-width: 40 -*-
# vim: set filetype=python:
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import gdb
import itertools
from gdbpp import GeckoPrettyPrinter

def walk_template_to_given_base(value, desired_tag_prefix):
    '''Given a value of some template subclass, walk up its ancestry until we
    hit the desired type, then return the appropriate value (which will then
    have that type).
    '''
    # Base case
    t = value.type
    if t.tag.startswith(desired_tag_prefix):
        return value
    for f in t.fields():
        # we only care about the inheritance hierarchy
        if not f.is_base_class:
            continue
        # This is the answer or something we're going to need to recurse into.
        fv = value[f]
        ft = fv.type
        # slightly optimize by checking the tag rather than in the recursion
        if ft.tag.startswith(desired_tag_prefix):
            # found it!
            return fv
        return walk_template_to_given_base(fv, desired_tag_prefix)
    return None

# The templates and their inheritance hierarchy form an onion of types around
# the nsTHashtable core at the center.  All we care about is that nsTHashtable,
# but we register for the descendant types in order to avoid the default pretty
# printers having to unwrap those onion layers, wasting precious lines.
@GeckoPrettyPrinter('nsClassHashtable', '^nsClassHashtable<.*>$')
@GeckoPrettyPrinter('nsDataHashtable', '^nsDataHashtable<.*>$')
@GeckoPrettyPrinter('nsInterfaceHashtable', '^nsInterfaceHashtable<.*>$')
@GeckoPrettyPrinter('nsRefPtrHashtable', '^nsRefPtrHashtable<.*>$')
@GeckoPrettyPrinter('nsBaseHashtable', '^nsBaseHashtable<.*>$')
@GeckoPrettyPrinter('nsTHashtable', '^nsTHashtable<.*>$')
class thashtable_printer(object):
    def __init__(self, outer_value):
        self.outermost_type = outer_value.type

        value = walk_template_to_given_base(outer_value, 'nsTHashtable<')
        self.value = value

        # This will be the nsBaseHashtableET.
        self.entry_type = value.type.template_argument(0)
        # While we know that it has a field `mKeyHash` for the hash-code and
        # book-keeping, and a DataType field mData for the value, the key field
        # frustratingly varies by key type.
        #
        # So we want to walk its key type to figure out the field name.  And we
        # do mean field name.  The field object is no good for subscripting the
        # value unless the field was directly owned by that value's type.  But
        # by using a string name, we save ourselves all that fanciness.
        key_type = self.entry_type.template_argument(0)
        self.key_field_name = None
        for f in key_type.fields():
            # No need to traverse up the type hierarchy...
            if f.is_base_class:
                continue
            # ...just to skip the fields we know exist...
            if f.name == 'mKeyHash' or f.name == 'mData':
                continue
            # ...and assume the first one we find is the key.
            self.key_field_name = f.name
            break


    def children(self):
        table = self.value['mTable']
        # Number of entries
        entryCount = table['mEntryCount']
        # Pierce generation-tracking EntryStore class to get at buffer.  The
        # class instance always exists, but this char* may be null.
        store = table['mEntryStore']['mEntryStore']

        key_field_name = self.key_field_name

        pEntry = store.cast(self.entry_type.pointer())
        for i in range(0, int(entryCount)):
            entry = (pEntry + i).dereference()
            # An mKeyHash of 0 means empty, 1 means deleted sentinel, so skip
            # if that's the case.
            if entry['mKeyHash'] <= 1:
                continue
            yield ('%d' % i, entry[key_field_name])
            yield ('%d' % i, entry['mData'])

    def to_string(self):
        # The most specific template type is the most interesting.
        return str(self.outermost_type)

    def display_hint(self):
        return 'map'

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import collections
import json
import math
import os
import re
import sys

# histogram_tools.py is used by scripts from a mozilla-central build tree
# and also by outside consumers, such as the telemetry server.  We need
# to ensure that importing things works in both contexts.  Therefore,
# unconditionally importing things that are local to the build tree, such
# as buildconfig, is a no-no.
try:
    import buildconfig

    # Need to update sys.path to be able to find usecounters.
    sys.path.append(os.path.join(buildconfig.topsrcdir, 'dom/base/'))
except ImportError:
    # Must be in an out-of-tree usage scenario.  Trust that whoever is
    # running this script knows we need the usecounters module and has
    # ensured it's in our sys.path.
    pass

from collections import OrderedDict

def table_dispatch(kind, table, body):
    """Call body with table[kind] if it exists.  Raise an error otherwise."""
    if kind in table:
        return body(table[kind])
    else:
        raise BaseException, "don't know how to handle a histogram of kind %s" % kind

class DefinitionException(BaseException):
    pass

def linear_buckets(dmin, dmax, n_buckets):
    ret_array = [0] * n_buckets
    dmin = float(dmin)
    dmax = float(dmax)
    for i in range(1, n_buckets):
        linear_range = (dmin * (n_buckets - 1 - i) + dmax * (i - 1)) / (n_buckets - 2)
        ret_array[i] = int(linear_range + 0.5)
    return ret_array

def exponential_buckets(dmin, dmax, n_buckets):
    log_max = math.log(dmax);
    bucket_index = 2;
    ret_array = [0] * n_buckets
    current = dmin
    ret_array[1] = current
    for bucket_index in range(2, n_buckets):
        log_current = math.log(current)
        log_ratio = (log_max - log_current) / (n_buckets - bucket_index)
        log_next = log_current + log_ratio
        next_value = int(math.floor(math.exp(log_next) + 0.5))
        if next_value > current:
            current = next_value
        else:
            current = current + 1
        ret_array[bucket_index] = current
    return ret_array

always_allowed_keys = ['kind', 'description', 'cpp_guard', 'expires_in_version',
                       'alert_emails', 'keyed', 'releaseChannelCollection',
                       'bug_numbers']

whitelists = None;
try:
    whitelist_path = os.path.join(os.path.abspath(os.path.realpath(os.path.dirname(__file__))), 'histogram-whitelists.json')
    with open(whitelist_path, 'r') as f:
        try:
            whitelists = json.load(f)
            for name, whitelist in whitelists.iteritems():
              whitelists[name] = set(whitelist)
        except ValueError, e:
            raise BaseException, 'error parsing whitelist (%s)' % whitelist_path
except IOError:
    whitelists = None
    print 'Unable to parse whitelist (%s). Assuming all histograms are acceptable.' % whitelist_path

class Histogram:
    """A class for representing a histogram definition."""

    def __init__(self, name, definition, strict_type_checks=False):
        """Initialize a histogram named name with the given definition.
definition is a dict-like object that must contain at least the keys:

 - 'kind': The kind of histogram.  Must be one of 'boolean', 'flag',
   'count', 'enumerated', 'linear', or 'exponential'.
 - 'description': A textual description of the histogram.
 - 'strict_type_checks': A boolean indicating whether to use the new, stricter type checks.
                         The server-side still has to deal with old, oddly typed submissions,
                         so we have to skip them there by default.

The key 'cpp_guard' is optional; if present, it denotes a preprocessor
symbol that should guard C/C++ definitions associated with the histogram."""
        self._strict_type_checks = strict_type_checks
        self.verify_attributes(name, definition)
        self._name = name
        self._description = definition['description']
        self._kind = definition['kind']
        self._cpp_guard = definition.get('cpp_guard')
        self._keyed = definition.get('keyed', False)
        self._expiration = definition.get('expires_in_version')
        self.compute_bucket_parameters(definition)
        table = { 'boolean': 'BOOLEAN',
                  'flag': 'FLAG',
                  'count': 'COUNT',
                  'enumerated': 'LINEAR',
                  'linear': 'LINEAR',
                  'exponential': 'EXPONENTIAL' }
        table_dispatch(self.kind(), table,
                       lambda k: self._set_nsITelemetry_kind(k))
        datasets = { 'opt-in': 'DATASET_RELEASE_CHANNEL_OPTIN',
                     'opt-out': 'DATASET_RELEASE_CHANNEL_OPTOUT' }
        value = definition.get('releaseChannelCollection', 'opt-in')
        if not value in datasets:
            raise DefinitionException, "unknown release channel collection policy for " + name
        self._dataset = "nsITelemetry::" + datasets[value]

    def name(self):
        """Return the name of the histogram."""
        return self._name

    def description(self):
        """Return the description of the histogram."""
        return self._description

    def kind(self):
        """Return the kind of the histogram.
Will be one of 'boolean', 'flag', 'count', 'enumerated', 'linear', or 'exponential'."""
        return self._kind

    def expiration(self):
        """Return the expiration version of the histogram."""
        return self._expiration

    def nsITelemetry_kind(self):
        """Return the nsITelemetry constant corresponding to the kind of
the histogram."""
        return self._nsITelemetry_kind

    def _set_nsITelemetry_kind(self, kind):
        self._nsITelemetry_kind = "nsITelemetry::HISTOGRAM_%s" % kind

    def low(self):
        """Return the lower bound of the histogram."""
        return self._low

    def high(self):
        """Return the high bound of the histogram."""
        return self._high

    def n_buckets(self):
        """Return the number of buckets in the histogram."""
        return self._n_buckets

    def cpp_guard(self):
        """Return the preprocessor symbol that should guard C/C++ definitions
associated with the histogram.  Returns None if no guarding is necessary."""
        return self._cpp_guard

    def keyed(self):
        """Returns True if this a keyed histogram, false otherwise."""
        return self._keyed

    def dataset(self):
        """Returns the dataset this histogram belongs into."""
        return self._dataset

    def ranges(self):
        """Return an array of lower bounds for each bucket in the histogram."""
        table = { 'boolean': linear_buckets,
                  'flag': linear_buckets,
                  'count': linear_buckets,
                  'enumerated': linear_buckets,
                  'linear': linear_buckets,
                  'exponential': exponential_buckets }
        return table_dispatch(self.kind(), table,
                              lambda p: p(self.low(), self.high(), self.n_buckets()))

    def compute_bucket_parameters(self, definition):
        table = {
            'boolean': Histogram.boolean_flag_bucket_parameters,
            'flag': Histogram.boolean_flag_bucket_parameters,
            'count': Histogram.boolean_flag_bucket_parameters,
            'enumerated': Histogram.enumerated_bucket_parameters,
            'linear': Histogram.linear_bucket_parameters,
            'exponential': Histogram.exponential_bucket_parameters
            }
        table_dispatch(self.kind(), table,
                       lambda p: self.set_bucket_parameters(*p(definition)))

    def verify_attributes(self, name, definition):
        global always_allowed_keys
        general_keys = always_allowed_keys + ['low', 'high', 'n_buckets']

        table = {
            'boolean': always_allowed_keys,
            'flag': always_allowed_keys,
            'count': always_allowed_keys,
            'enumerated': always_allowed_keys + ['n_values'],
            'linear': general_keys,
            'exponential': general_keys
            }
        # We removed extended_statistics_ok on the client, but the server-side,
        # where _strict_type_checks==False, has to deal with historical data.
        if not self._strict_type_checks:
            table['exponential'].append('extended_statistics_ok')

        table_dispatch(definition['kind'], table,
                       lambda allowed_keys: Histogram.check_keys(name, definition, allowed_keys))

        if 'alert_emails' not in definition:
            if whitelists is not None and name not in whitelists['alert_emails']:
                raise KeyError, 'New histogram "%s" must have an alert_emails field.' % name
        elif not isinstance(definition['alert_emails'], list):
            raise KeyError, 'alert_emails must be an array (in histogram "%s")' % name

        Histogram.check_name(name)
        self.check_field_types(name, definition)
        Histogram.check_expiration(name, definition)
        Histogram.check_bug_numbers(name, definition)

    @staticmethod
    def check_name(name):
        if '#' in name:
            raise ValueError, '"#" not permitted for %s' % (name)

    @staticmethod
    def check_expiration(name, definition):
        expiration = definition.get('expires_in_version')

        if not expiration:
            return

        if re.match(r'^[1-9][0-9]*$', expiration):
            expiration = expiration + ".0a1"
        elif re.match(r'^[1-9][0-9]*\.0$', expiration):
            expiration = expiration + "a1"

        definition['expires_in_version'] = expiration

    @staticmethod
    def check_bug_numbers(name, definition):
        bug_numbers = definition.get('bug_numbers')
        if not bug_numbers:
            if whitelists is None or name in whitelists['bug_numbers']:
                return
            else:
                raise KeyError, 'New histogram "%s" must have a bug_numbers field.' % name

        if not isinstance(bug_numbers, list):
            raise ValueError, 'bug_numbers field for "%s" should be an array' % (name)

        if not all(type(num) is int for num in bug_numbers):
            raise ValueError, 'bug_numbers array for "%s" should only contain integers' % (name)

    def check_field_types(self, name, definition):
        # Define expected types for the histogram properties.
        type_checked_fields = {
                "n_buckets": int,
                "n_values": int,
                "low": int,
                "high": int,
                "keyed": bool,
                "expires_in_version": basestring,
                "kind": basestring,
                "description": basestring,
                "cpp_guard": basestring,
                "releaseChannelCollection": basestring
            }

        # For the server-side, where _strict_type_checks==False, we want to
        # skip the stricter type checks for these fields for dealing with
        # historical data.
        coerce_fields = ["low", "high", "n_values", "n_buckets"]
        if not self._strict_type_checks:
            def try_to_coerce_to_number(v):
                try:
                    return eval(v, {})
                except:
                    return v
            for key in [k for k in coerce_fields if k in definition]:
                definition[key] = try_to_coerce_to_number(definition[key])

        for key, key_type in type_checked_fields.iteritems():
            if not key in definition:
                continue
            if not isinstance(definition[key], key_type):
                if key_type is basestring:
                    type_name = "string"
                else:
                    type_name = key_type.__name__
                raise ValueError, ('value for key "{0}" in Histogram "{1}" '
                        'should be {2}').format(key, name, type_name)

    @staticmethod
    def check_keys(name, definition, allowed_keys):
        for key in definition.iterkeys():
            if key not in allowed_keys:
                raise KeyError, '%s not permitted for %s' % (key, name)

    def set_bucket_parameters(self, low, high, n_buckets):
        self._low = low
        self._high = high
        self._n_buckets = n_buckets
        if whitelists is not None and self._n_buckets > 100 and type(self._n_buckets) is int:
            if self._name not in whitelists['n_buckets']:
                raise KeyError, ('New histogram "%s" is not permitted to have more than 100 buckets. '
                                'Histograms with large numbers of buckets use disproportionately high amounts of resources. '
                                'Contact the Telemetry team (e.g. in #telemetry) if you think an exception ought to be made.' % self._name)

    @staticmethod
    def boolean_flag_bucket_parameters(definition):
        return (1, 2, 3)

    @staticmethod
    def linear_bucket_parameters(definition):
        return (definition.get('low', 1),
                definition['high'],
                definition['n_buckets'])

    @staticmethod
    def enumerated_bucket_parameters(definition):
        n_values = definition['n_values']
        return (1, n_values, n_values + 1)

    @staticmethod
    def exponential_bucket_parameters(definition):
        return (definition.get('low', 1),
                definition['high'],
                definition['n_buckets'])

# We support generating histograms from multiple different input files, not
# just Histograms.json.  For each file's basename, we have a specific
# routine to parse that file, and return a dictionary mapping histogram
# names to histogram parameters.
def from_Histograms_json(filename):
    with open(filename, 'r') as f:
        try:
            histograms = json.load(f, object_pairs_hook=OrderedDict)
        except ValueError, e:
            raise BaseException, "error parsing histograms in %s: %s" % (filename, e.message)
    return histograms

def from_UseCounters_conf(filename):
    return usecounters.generate_histograms(filename)

def from_nsDeprecatedOperationList(filename):
    operation_regex = re.compile('^DEPRECATED_OPERATION\\(([^)]+)\\)')
    histograms = collections.OrderedDict()

    with open(filename, 'r') as f:
        for line in f:
            match = operation_regex.search(line)
            if not match:
                continue

            op = match.group(1)

            def add_counter(context):
                name = 'USE_COUNTER2_DEPRECATED_%s_%s' % (op, context.upper())
                histograms[name] = {
                    'expires_in_version': 'never',
                    'kind': 'boolean',
                    'description': 'Whether a %s used %s' % (context, op)
                }
            add_counter('document')
            add_counter('page')

    return histograms

FILENAME_PARSERS = {
    'Histograms.json': from_Histograms_json,
    'nsDeprecatedOperationList.h': from_nsDeprecatedOperationList,
}

# Similarly to the dance above with buildconfig, usecounters may not be
# available, so handle that gracefully.
try:
    import usecounters

    FILENAME_PARSERS['UseCounters.conf'] = from_UseCounters_conf
except ImportError:
    pass

def from_files(filenames):
    """Return an iterator that provides a sequence of Histograms for
the histograms defined in filenames.
    """
    all_histograms = OrderedDict()
    for filename in filenames:
        parser = FILENAME_PARSERS[os.path.basename(filename)]
        histograms = parser(filename)

        # OrderedDicts are important, because then the iteration order over
        # the parsed histograms is stable, which makes the insertion into
        # all_histograms stable, which makes ordering in generated files
        # stable, which makes builds more deterministic.
        if not isinstance(histograms, OrderedDict):
            raise BaseException, "histogram parser didn't provide an OrderedDict"

        for (name, definition) in histograms.iteritems():
            if all_histograms.has_key(name):
                raise DefinitionException, "duplicate histogram name %s" % name
            all_histograms[name] = definition

    # We require that all USE_COUNTER2_* histograms be defined in a contiguous
    # block.
    use_counter_indices = filter(lambda x: x[1].startswith("USE_COUNTER2_"),
                                 enumerate(all_histograms.iterkeys()));
    if use_counter_indices:
        lower_bound = use_counter_indices[0][0]
        upper_bound = use_counter_indices[-1][0]
        n_counters = upper_bound - lower_bound + 1
        if n_counters != len(use_counter_indices):
            raise DefinitionException, "use counter histograms must be defined in a contiguous block"

    for (name, definition) in all_histograms.iteritems():
        yield Histogram(name, definition, strict_type_checks=True)

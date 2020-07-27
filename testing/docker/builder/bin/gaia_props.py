#! /usr/bin/env python

'''
Command line interface to fetch details from the b2g/config/gaia.json properties
file used to link a particular version of gaia to goanna.
'''

import argparse
import os
import json
import sys
import urlparse

parser = argparse.ArgumentParser(
    description='Get various information about gaia version tied to particular \
    goanna')

parser.add_argument('goanna', metavar="GECKO_DIR", help="Path to goanna revision")
parser.add_argument('prop', help="Property type",
                    choices=['repository', 'revision'])

args = parser.parse_args()

if not os.path.isdir(args.goanna):
        print >> sys.stderr, 'Given goanna path is not a directory'
        sys.exit(1)

props_path = os.path.join(args.goanna, 'b2g/config/gaia.json')

if not os.path.isfile(props_path):
        print >> sys.stderr, \
            'Gecko directory does not contain b2g/config/gaia.json'
        sys.exit(1)

props = json.load(open(props_path))

if args.prop == 'revision':
    print(props['revision']);

if args.prop == 'repository':
    print(urlparse.urljoin('https://hg.mozilla.org', props['repo_path']))

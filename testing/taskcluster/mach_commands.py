# -*- coding: utf-8 -*-

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import absolute_import

from collections import defaultdict
import os
import json
import copy
import sys
import time
from collections import namedtuple

from mach.decorators import (
    CommandArgument,
    CommandProvider,
    Command,
)


ROOT = os.path.dirname(os.path.realpath(__file__))
GECKO = os.path.realpath(os.path.join(ROOT, '..', '..'))

# XXX: If/when we have the taskcluster queue use construct url instead
ARTIFACT_URL = 'https://queue.taskcluster.net/v1/task/{}/artifacts/{}'

DEFINE_TASK = 'queue:define-task:aws-provisioner-v1/{}'

DEFAULT_TRY = 'try: -b do -p all -u all'
DEFAULT_JOB_PATH = os.path.join(
    ROOT, 'tasks', 'branches', 'base_jobs.yml'
)

def gaia_info():
    '''
    Fetch details from in tree gaia.json (which links this version of
    gecko->gaia) and construct the usual base/head/ref/rev pairing...
    '''
    gaia = json.load(open(os.path.join(GECKO, 'b2g', 'config', 'gaia.json')))

    if gaia['git'] is None or \
       gaia['git']['remote'] == '' or \
       gaia['git']['git_revision'] == '' or \
       gaia['git']['branch'] == '':

       # Just use the hg params...
       return {
         'gaia_base_repository': 'https://hg.mozilla.org/{}'.format(gaia['repo_path']),
         'gaia_head_repository': 'https://hg.mozilla.org/{}'.format(gaia['repo_path']),
         'gaia_ref': gaia['revision'],
         'gaia_rev': gaia['revision']
       }

    else:
        # Use git
        return {
            'gaia_base_repository': gaia['git']['remote'],
            'gaia_head_repository': gaia['git']['remote'],
            'gaia_rev': gaia['git']['git_revision'],
            'gaia_ref': gaia['git']['branch'],
        }

def configure_dependent_task(task_path, parameters, taskid, templates, build_treeherder_config):
    """
    Configure a build dependent task. This is shared between post-build and test tasks.

    :param task_path: location to the task yaml
    :param parameters: parameters to load the template
    :param taskid: taskid of the dependent task
    :param templates: reference to the template builder
    :param build_treeherder_config: parent treeherder config
    :return: the configured task
    """
    task = templates.load(task_path, parameters)
    task['taskId'] = taskid

    if 'requires' not in task:
        task['requires'] = []

    task['requires'].append(parameters['build_slugid'])

    if 'treeherder' not in task['task']['extra']:
        task['task']['extra']['treeherder'] = {}

    # Copy over any treeherder configuration from the build so
    # tests show up under the same platform...
    treeherder_config = task['task']['extra']['treeherder']

    treeherder_config['collection'] = \
        build_treeherder_config.get('collection', {})

    treeherder_config['build'] = \
        build_treeherder_config.get('build', {})

    treeherder_config['machine'] = \
        build_treeherder_config.get('machine', {})

    if 'routes' not in task['task']:
        task['task']['routes'] = []

    if 'scopes' not in task['task']:
        task['task']['scopes'] = []

    return task

def set_interactive_task(task, interactive):
    r"""Make the task interactive.

    :param task: task definition.
    :param interactive: True if the task should be interactive.
    """
    if not interactive:
        return

    payload = task["task"]["payload"]
    if "features" not in payload:
        payload["features"] = {}
    payload["features"]["interactive"] = True

def remove_caches_from_task(task):
    r"""Remove all caches but tc-vcs from the task.

    :param task: task definition.
    """
    whitelist = [
        "tc-vcs",
        "tc-vcs-public-sources",
        "tooltool-cache",
    ]
    try:
        caches = task["task"]["payload"]["cache"]
        for cache in caches.keys():
            if cache not in whitelist:
                caches.pop(cache)
    except KeyError:
        pass

def query_pushinfo(repository, revision):
    """Query the pushdate and pushid of a repository/revision.
    This is intended to be used on hg.mozilla.org/mozilla-central and
    similar. It may or may not work for other hg repositories.
    """
    PushInfo = namedtuple('PushInfo', ['pushid', 'pushdate'])

    try:
        import urllib2
        url = '%s/json-pushes?changeset=%s' % (repository, revision)
        sys.stderr.write("Querying URL for pushdate: %s\n" % url)
        contents = json.load(urllib2.urlopen(url))

        # The contents should be something like:
        # {
        #   "28537": {
        #    "changesets": [
        #     "1d0a914ae676cc5ed203cdc05c16d8e0c22af7e5",
        #    ],
        #    "date": 1428072488,
        #    "user": "user@mozilla.com"
        #   }
        # }
        #
        # So we grab the first element ("28537" in this case) and then pull
        # out the 'date' field.
        pushid = contents.iterkeys().next()
        pushdate = contents[pushid]['date']
        return PushInfo(pushid, pushdate)

    except Exception:
        sys.stderr.write(
            "Error querying pushinfo for repository '%s' revision '%s'\n" % (
                repository, revision,
            )
        )
        return None

@CommandProvider
class DecisionTask(object):
    @Command('taskcluster-decision', category="ci",
        description="Build a decision task")
    @CommandArgument('--project',
        required=True,
        help='Treeherder project name')
    @CommandArgument('--url',
        required=True,
        help='Gecko repository to use as head repository.')
    @CommandArgument('--revision',
        required=True,
        help='Revision for this project')
    @CommandArgument('--revision-hash',
        help='Treeherder revision hash')
    @CommandArgument('--comment',
        required=True,
        help='Commit message for this revision')
    @CommandArgument('--owner',
        required=True,
        help='email address of who owns this graph')
    @CommandArgument('task', help="Path to decision task to run.")
    def run_task(self, **params):
        from taskcluster_graph.slugidjar import SlugidJar
        from taskcluster_graph.from_now import (
            json_time_from_now,
            current_json_time,
        )
        from taskcluster_graph.templates import Templates

        templates = Templates(ROOT)
        # Template parameters used when expanding the graph
        parameters = dict(gaia_info().items() + {
            'source': 'http://todo.com/soon',
            'project': params['project'],
            'comment': params['comment'],
            'url': params['url'],
            'revision': params['revision'],
            'revision_hash': params.get('revision_hash', ''),
            'owner': params['owner'],
            'as_slugid': SlugidJar(),
            'from_now': json_time_from_now,
            'now': current_json_time()
        }.items())
        task = templates.load(params['task'], parameters)
        print(json.dumps(task, indent=4))

@CommandProvider
class Graph(object):
    @Command('taskcluster-graph', category="ci",
        description="Create taskcluster task graph")
    @CommandArgument('--base-repository',
        default=os.environ.get('GECKO_BASE_REPOSITORY'),
        help='URL for "base" repository to clone')
    @CommandArgument('--head-repository',
        default=os.environ.get('GECKO_HEAD_REPOSITORY'),
        help='URL for "head" repository to fetch revision from')
    @CommandArgument('--head-ref',
        default=os.environ.get('GECKO_HEAD_REF'),
        help='Reference (this is same as rev usually for hg)')
    @CommandArgument('--head-rev',
        default=os.environ.get('GECKO_HEAD_REV'),
        help='Commit revision to use from head repository')
    @CommandArgument('--message',
        help='Commit message to be parsed. Example: "try: -b do -p all -u all"')
    @CommandArgument('--revision-hash',
            required=False,
            help='Treeherder revision hash to attach results to')
    @CommandArgument('--project',
        required=True,
        help='Project to use for creating task graph. Example: --project=try')
    @CommandArgument('--pushlog-id',
        dest='pushlog_id',
        required=False,
        default=0)
    @CommandArgument('--owner',
        required=True,
        help='email address of who owns this graph')
    @CommandArgument('--level',
        default="1",
        help='SCM level of this repository')
    @CommandArgument('--extend-graph',
        action="store_true", dest="ci", help='Omit create graph arguments')
    @CommandArgument('--interactive',
        required=False,
        default=False,
        action="store_true",
        dest="interactive",
        help="Run the tasks with the interactive feature enabled")
    @CommandArgument('--print-names-only',
        action='store_true', default=False,
        help="Only print the names of each scheduled task, one per line.")
    def create_graph(self, **params):
        from functools import partial

        from slugid import nice as slugid

        import taskcluster_graph.transform.routes as routes_transform
        import taskcluster_graph.transform.treeherder as treeherder_transform
        from taskcluster_graph.commit_parser import parse_commit
        from taskcluster_graph.image_builder import (
            docker_image,
            normalize_image_details,
            task_id_for_image
        )
        from taskcluster_graph.from_now import (
            json_time_from_now,
            current_json_time,
        )
        from taskcluster_graph.templates import Templates
        import taskcluster_graph.build_task

        project = params['project']
        message = params.get('message', '') if project == 'try' else DEFAULT_TRY

        # Message would only be blank when not created from decision task
        if project == 'try' and not message:
            sys.stderr.write(
                    "Must supply commit message when creating try graph. " \
                    "Example: --message='try: -b do -p all -u all'"
            )
            sys.exit(1)

        templates = Templates(ROOT)
        job_path = os.path.join(ROOT, 'tasks', 'branches', project, 'job_flags.yml')
        job_path = job_path if os.path.exists(job_path) else DEFAULT_JOB_PATH

        jobs = templates.load(job_path, {})

        job_graph = parse_commit(message, jobs)

        cmdline_interactive = params.get('interactive', False)

        # Default to current time if querying the head rev fails
        pushdate = time.strftime('%Y%m%d%H%M%S', time.gmtime())
        pushinfo = query_pushinfo(params['head_repository'], params['head_rev'])
        if pushinfo:
            pushdate = time.strftime('%Y%m%d%H%M%S', time.gmtime(pushinfo.pushdate))

        # Template parameters used when expanding the graph
        seen_images = {}
        parameters = dict(gaia_info().items() + {
            'index': 'index',
            'project': project,
            'pushlog_id': params.get('pushlog_id', 0),
            'docker_image': docker_image,
            'task_id_for_image': partial(task_id_for_image, seen_images, project),
            'base_repository': params['base_repository'] or \
                params['head_repository'],
            'head_repository': params['head_repository'],
            'head_ref': params['head_ref'] or params['head_rev'],
            'head_rev': params['head_rev'],
            'pushdate': pushdate,
            'pushtime': pushdate[8:],
            'year': pushdate[0:4],
            'month': pushdate[4:6],
            'day': pushdate[6:8],
            'owner': params['owner'],
            'level': params['level'],
            'from_now': json_time_from_now,
            'now': current_json_time(),
            'revision_hash': params['revision_hash']
        }.items())

        treeherder_route = '{}.{}'.format(
            params['project'],
            params.get('revision_hash', '')
        )

        routes_file = os.path.join(ROOT, 'routes.json')
        with open(routes_file) as f:
            contents = json.load(f)
            json_routes = contents['routes']
            # TODO: Nightly and/or l10n routes

        # Task graph we are generating for taskcluster...
        graph = {
            'tasks': [],
            'scopes': []
        }

        if params['revision_hash']:
            for env in routes_transform.TREEHERDER_ROUTES:
                route = 'queue:route:{}.{}'.format(
                            routes_transform.TREEHERDER_ROUTES[env],
                            treeherder_route)
                graph['scopes'].append(route)

        graph['metadata'] = {
            'source': '{repo}file/{rev}/testing/taskcluster/mach_commands.py'.format(repo=params['head_repository'], rev=params['head_rev']),
            'owner': params['owner'],
            # TODO: Add full mach commands to this example?
            'description': 'Task graph generated via ./mach taskcluster-graph',
            'name': 'task graph local'
        }

        all_routes = {}

        for build in job_graph:
            interactive = cmdline_interactive or build["interactive"]
            build_parameters = dict(parameters)
            build_parameters['build_slugid'] = slugid()
            build_parameters['source'] = '{repo}file/{rev}/testing/taskcluster/{file}'.format(repo=params['head_repository'], rev=params['head_rev'], file=build['task'])
            build_task = templates.load(build['task'], build_parameters)

            # Copy build_* attributes to expose them to post-build tasks
            # as well as json routes and tests
            task_extra = build_task['task']['extra']
            build_parameters['build_name'] = task_extra['build_name']
            build_parameters['build_type'] = task_extra['build_type']
            build_parameters['build_product'] = task_extra['build_product']

            normalize_image_details(graph,
                                    build_task,
                                    seen_images,
                                    build_parameters,
                                    os.environ.get('TASK_ID', None))
            set_interactive_task(build_task, interactive)

            # try builds don't use cache
            if project == "try":
                remove_caches_from_task(build_task)

            if params['revision_hash']:
                treeherder_transform.add_treeherder_revision_info(build_task['task'],
                                                                  params['head_rev'],
                                                                  params['revision_hash'])
                routes_transform.decorate_task_treeherder_routes(build_task['task'],
                                                                 treeherder_route)
                routes_transform.decorate_task_json_routes(build_task['task'],
                                                           json_routes,
                                                           build_parameters)

            # Ensure each build graph is valid after construction.
            taskcluster_graph.build_task.validate(build_task)
            graph['tasks'].append(build_task)

            test_packages_url, tests_url, mozharness_url = None, None, None

            if 'test_packages' in build_task['task']['extra']['locations']:
                test_packages_url = ARTIFACT_URL.format(
                    build_parameters['build_slugid'],
                    build_task['task']['extra']['locations']['test_packages']
                )

            if 'tests' in build_task['task']['extra']['locations']:
                tests_url = ARTIFACT_URL.format(
                    build_parameters['build_slugid'],
                    build_task['task']['extra']['locations']['tests']
                )

            if 'mozharness' in build_task['task']['extra']['locations']:
                mozharness_url = ARTIFACT_URL.format(
                    build_parameters['build_slugid'],
                    build_task['task']['extra']['locations']['mozharness']
                )

            build_url = ARTIFACT_URL.format(
                build_parameters['build_slugid'],
                build_task['task']['extra']['locations']['build']
            )

            # img_url is only necessary for device builds
            img_url = ARTIFACT_URL.format(
                build_parameters['build_slugid'],
                build_task['task']['extra']['locations'].get('img', '')
            )

            define_task = DEFINE_TASK.format(build_task['task']['workerType'])

            for route in build_task['task'].get('routes', []):
                if route.startswith('index.gecko.v2') and route in all_routes:
                    raise Exception("Error: route '%s' is in use by multiple tasks: '%s' and '%s'" % (
                        route,
                        build_task['task']['metadata']['name'],
                        all_routes[route],
                    ))
                all_routes[route] = build_task['task']['metadata']['name']

            graph['scopes'].append(define_task)
            graph['scopes'].extend(build_task['task'].get('scopes', []))
            route_scopes = map(lambda route: 'queue:route:' + route, build_task['task'].get('routes', []))
            graph['scopes'].extend(route_scopes)

            # Treeherder symbol configuration for the graph required for each
            # build so tests know which platform they belong to.
            build_treeherder_config = build_task['task']['extra']['treeherder']

            if 'machine' not in build_treeherder_config:
                message = '({}), extra.treeherder.machine required for all builds'
                raise ValueError(message.format(build['task']))

            if 'build' not in build_treeherder_config:
                build_treeherder_config['build'] = \
                    build_treeherder_config['machine']

            if 'collection' not in build_treeherder_config:
                build_treeherder_config['collection'] = { 'opt': True }

            if len(build_treeherder_config['collection'].keys()) != 1:
                message = '({}), extra.treeherder.collection must contain one type'
                raise ValueError(message.fomrat(build['task']))

            for post_build in build['post-build']:
                # copy over the old parameters to update the template
                post_parameters = copy.copy(build_parameters)
                post_task = configure_dependent_task(post_build['task'],
                                                     post_parameters,
                                                     slugid(),
                                                     templates,
                                                     build_treeherder_config)
                normalize_image_details(graph,
                                        post_task,
                                        seen_images,
                                        build_parameters,
                                        os.environ.get('TASK_ID', None))
                set_interactive_task(post_task, interactive)
                treeherder_transform.add_treeherder_revision_info(post_task['task'],
                                                                  params['head_rev'],
                                                                  params['revision_hash'])
                graph['tasks'].append(post_task)

            for test in build['dependents']:
                test = test['allowed_build_tasks'][build['task']]
                test_parameters = copy.copy(build_parameters)
                test_parameters['build_url'] = build_url
                test_parameters['img_url'] = img_url
                if tests_url:
                    test_parameters['tests_url'] = tests_url
                if test_packages_url:
                    test_parameters['test_packages_url'] = test_packages_url
                if mozharness_url:
                    test_parameters['mozharness_url'] = mozharness_url
                test_definition = templates.load(test['task'], {})['task']
                chunk_config = test_definition['extra'].get('chunks', {})

                # Allow branch configs to override task level chunking...
                if 'chunks' in test:
                    chunk_config['total'] = test['chunks']

                chunked = 'total' in chunk_config
                if chunked:
                    test_parameters['total_chunks'] = chunk_config['total']

                if 'suite' in test_definition['extra']:
                    suite_config = test_definition['extra']['suite']
                    test_parameters['suite'] = suite_config['name']
                    test_parameters['flavor'] = suite_config.get('flavor', '')

                for chunk in range(1, chunk_config.get('total', 1) + 1):
                    if 'only_chunks' in test and chunked and \
                        chunk not in test['only_chunks']:
                        continue

                    if chunked:
                        test_parameters['chunk'] = chunk
                    test_task = configure_dependent_task(test['task'],
                                                         test_parameters,
                                                         slugid(),
                                                         templates,
                                                         build_treeherder_config)
                    normalize_image_details(graph,
                                            test_task,
                                            seen_images,
                                            build_parameters,
                                            os.environ.get('TASK_ID', None))
                    set_interactive_task(test_task, interactive)

                    if params['revision_hash']:
                        treeherder_transform.add_treeherder_revision_info(test_task['task'],
                                                                          params['head_rev'],
                                                                          params['revision_hash'])
                        routes_transform.decorate_task_treeherder_routes(
                            test_task['task'],
                            treeherder_route
                        )

                    graph['tasks'].append(test_task)

                    define_task = DEFINE_TASK.format(
                        test_task['task']['workerType']
                    )

                    graph['scopes'].append(define_task)
                    graph['scopes'].extend(test_task['task'].get('scopes', []))

        graph['scopes'] = list(set(graph['scopes']))

        if params['print_names_only']:
            tIDs = defaultdict(list)

            def print_task(task, indent=0):
                print('{}- {}'.format(' ' * indent, task['task']['metadata']['name']))

                for child in tIDs[task['taskId']]:
                    print_task(child, indent=indent+2)

            # build a dependency map
            for task in graph['tasks']:
                if 'requires' in task:
                    for tID in task['requires']:
                        tIDs[tID].append(task)

            # recursively print root tasks
            for task in graph['tasks']:
                if 'requires' not in task:
                    print_task(task)
            return

        # When we are extending the graph remove extra fields...
        if params['ci'] is True:
            graph.pop('scopes', None)
            graph.pop('metadata', None)

        print(json.dumps(graph, indent=4))

@CommandProvider
class CIBuild(object):
    @Command('taskcluster-build', category='ci',
        description="Create taskcluster try server build task")
    @CommandArgument('--base-repository',
        help='URL for "base" repository to clone')
    @CommandArgument('--head-repository',
        required=True,
        help='URL for "head" repository to fetch revision from')
    @CommandArgument('--head-ref',
        help='Reference (this is same as rev usually for hg)')
    @CommandArgument('--head-rev',
        required=True,
        help='Commit revision to use')
    @CommandArgument('--owner',
        default='foobar@mozilla.com',
        help='email address of who owns this graph')
    @CommandArgument('--level',
        default="1",
        help='SCM level of this repository')
    @CommandArgument('build_task',
        help='path to build task definition')
    @CommandArgument('--interactive',
        required=False,
        default=False,
        action="store_true",
        dest="interactive",
        help="Run the task with the interactive feature enabled")
    def create_ci_build(self, **params):
        from taskcluster_graph.templates import Templates
        import taskcluster_graph.build_task

        templates = Templates(ROOT)
        # TODO handle git repos
        head_repository = params['head_repository']
        if not head_repository:
            head_repository = get_hg_url()

        head_rev = params['head_rev']
        if not head_rev:
            head_rev = get_latest_hg_revision(head_repository)

        head_ref = params['head_ref'] or head_rev

        from taskcluster_graph.from_now import (
            json_time_from_now,
            current_json_time,
        )
        build_parameters = dict(gaia_info().items() + {
            'docker_image': docker_image,
            'owner': params['owner'],
            'level': params['level'],
            'from_now': json_time_from_now,
            'now': current_json_time(),
            'base_repository': params['base_repository'] or head_repository,
            'head_repository': head_repository,
            'head_rev': head_rev,
            'head_ref': head_ref,
        }.items())

        try:
            build_task = templates.load(params['build_task'], build_parameters)
            set_interactive_task(build_task, params.get('interactive', False))
        except IOError:
            sys.stderr.write(
                "Could not load build task file.  Ensure path is a relative " \
                "path from testing/taskcluster"
            )
            sys.exit(1)

        taskcluster_graph.build_task.validate(build_task)

        print(json.dumps(build_task['task'], indent=4))

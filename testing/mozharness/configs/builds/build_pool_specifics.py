# this is a dict of pool specific keys/values. As this fills up and more
# fx build factories are ported, we might deal with this differently

config = {
    "staging": {
        # if not clobberer_url, only clobber 'abs_work_dir'
        # if true: possibly clobber, clobberer
        # see PurgeMixin for clobber() conditions
        'clobberer_url': 'https://api-pub-build.allizom.org/clobberer/lastclobber',
        # staging we should use MozillaTest
        # but in production we let the self.branch decide via
        # self._query_graph_server_branch_name()
        "graph_server_branch_name": "MozillaTest",
        'graph_server': 'graphs.allizom.org',
        "hgtool_base_bundle_urls": [
            'http://dev-stage01.build.mozilla.org/pub/mozilla'
            '.org/firefox/bundles',
        ],
        'symbol_server_host': "dev-stage01.srv.releng.scl3.mozilla.com",
        'stage_server': 'upload.ffxbld.productdelivery.stage.mozaws.net',
        "sendchange_masters": ["dev-master1.srv.releng.scl3.mozilla.com:9038"],
        'taskcluster_index': 'index.garbage.staging',
    },
    "production": {
        # if not clobberer_url, only clobber 'abs_work_dir'
        # if true: possibly clobber, clobberer
        # see PurgeMixin for clobber() conditions
        'clobberer_url': 'https://api.pub.build.mozilla.org/clobberer/lastclobber',
        'graph_server': 'graphs.mozilla.org',
        "hgtool_base_bundle_urls": [
            'https://ftp-ssl.mozilla.org/pub/mozilla.org/firefox/bundles'
        ],
        'symbol_server_host': "symbolpush.mozilla.org",
        # bug 1216907, set this at branch level
        # 'stage_server': 'upload.ffxbld.productdelivery.prod.mozaws.net',
        "sendchange_masters": ["buildbot-master81.build.mozilla.org:9301"],
        'taskcluster_index': 'index',
    },
    "taskcluster": {
        'graph_server': 'graphs.mozilla.org',
        'symbol_server_host': "symbolpush.mozilla.org",
        'stage_server': 'ignored',
        # use the relengapi proxy to talk to tooltool
        "tooltool_servers": ['http://relengapi/tooltool/'],
        "tooltool_url": 'http://relengapi/tooltool/',
        'upload_env': {
            'UPLOAD_HOST': 'localhost',
            'UPLOAD_PATH': '/home/worker/artifacts',
        },
    },
}

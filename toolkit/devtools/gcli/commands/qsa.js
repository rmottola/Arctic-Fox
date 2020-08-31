/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const gcli = require("gcli/index");

exports.items = [
  {
    name: "qsa",
    description: gcli.lookup("qsaDesc"),
    params: [{
      name: "query",
      type: "nodelist",
      description: gcli.lookup("qsaQueryDesc")
    }],
    exec: function(args, context) {
      return args.query.length;
    }
  }
];

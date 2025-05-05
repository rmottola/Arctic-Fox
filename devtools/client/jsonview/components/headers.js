/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

define(function(require, exports, module) {
  const React = require("devtools/client/shared/vendor/react");

  // Constants
  const DOM = React.DOM;
  const PropTypes = React.PropTypes;

  /**
   * This template is responsible for rendering basic layout
   * of the 'Headers' panel. It displays HTTP headers groups such as
   * received or response headers.
   */
  let Headers = React.createClass({
    propTypes: {
      data: PropTypes.object,
    },

    displayName: "Headers",

    getInitialState: function() {
      return {};
    },

    render: function() {
      let data = this.props.data;

      return (
        DOM.div({className: "netInfoHeadersTable"},
          DOM.div({className: "netHeadersGroup"},
            DOM.div({className: "netInfoHeadersGroup"},
              DOM.span({className: "netHeader twisty"},
                Locale.$STR("jsonViewer.responseHeaders")
              )
            ),
            DOM.table({cellPadding: 0, cellSpacing: 0},
              HeaderList({headers: data.response})
            )
          ),
          DOM.div({className: "netHeadersGroup"},
            DOM.div({className: "netInfoHeadersGroup"},
              DOM.span({className: "netHeader twisty"},
                Locale.$STR("jsonViewer.requestHeaders")
              )
            ),
            DOM.table({cellPadding: 0, cellSpacing: 0},
              HeaderList({headers: data.request})
            )
          )
        )
      );
    }
  });

  /**
   * This template renders headers list,
   * name + value pairs.
   */
  let HeaderList = React.createFactory(React.createClass({
    propTypes: {
      headers: PropTypes.arrayOf(PropTypes.shape({
        name: PropTypes.string,
        value: PropTypes.string
      }))
    },

    displayName: "HeaderList",

    getInitialState: function() {
      return {
        headers: []
      };
    },

    render: function() {
      let headers = this.props.headers;

      headers.sort(function(a, b) {
        return a.name > b.name ? 1 : -1;
      });

      let rows = [];
      headers.forEach(header => {
        rows.push(
          DOM.tr({key: header.name},
            DOM.td({className: "netInfoParamName"},
              DOM.span({title: header.name}, header.name)
            ),
            DOM.td({className: "netInfoParamValue"},
              DOM.code({}, header.value)
            )
          )
        );
      });

      return (
        DOM.tbody({},
          rows
        )
      );
    }
  }));

  // Exports from this module
  exports.Headers = Headers;
});

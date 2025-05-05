/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

define(function(require, exports, module) {
  const React = require("devtools/client/shared/vendor/react");
  const { createFactories } = require("devtools/client/shared/components/reps/rep-utils");
  const { TreeView } = createFactories(require("./reps/tree-view"));
  const { SearchBox } = createFactories(require("./search-box"));
  const { Toolbar, ToolbarButton } = createFactories(require("./reps/toolbar"));
  const DOM = React.DOM;

  /**
   * This template represents the 'JSON' panel. The panel is
   * responsible for rendering an expandable tree that allows simple
   * inspection of JSON structure.
   */
  let JsonPanel = React.createClass({
    propTypes: {
      data: React.PropTypes.oneOfType([
        React.PropTypes.string,
        React.PropTypes.array,
        React.PropTypes.object
      ]),
      searchFilter: React.PropTypes.string,
      actions: React.PropTypes.object,
    },

    displayName: "JsonPanel",

    getInitialState: function() {
      return {};
    },

    componentDidMount: function() {
      document.addEventListener("keypress", this.onKeyPress, true);
    },

    componentWillUnmount: function() {
      document.removeEventListener("keypress", this.onKeyPress, true);
    },

    onKeyPress: function(e) {
      // XXX shortcut for focusing the Filter field (see Bug 1178771).
    },

    render: function() {
      let content;
      let data = this.props.data;

      try {
        if (typeof data == "object") {
          content = TreeView({
            data: this.props.data,
            mode: "tiny",
            searchFilter: this.props.searchFilter
          });
        } else {
          content = DOM.div({className: "jsonParseError"},
            data + ""
          );
        }
      } catch (err) {
        content = DOM.div({className: "jsonParseError"},
          err + ""
        );
      }

      return (
        DOM.div({className: "jsonPanelBox"},
          JsonToolbar({actions: this.props.actions}),
          DOM.div({className: "panelContent"},
            content
          )
        )
      );
    }
  });

  /**
   * This template represents a toolbar within the 'JSON' panel.
   */
  let JsonToolbar = React.createFactory(React.createClass({
    propTypes: {
      actions: React.PropTypes.object,
    },

    displayName: "JsonToolbar",

    // Commands

    onSave: function(event) {
      this.props.actions.onSaveJson();
    },

    onCopy: function(event) {
      this.props.actions.onCopyJson();
    },

    render: function() {
      return (
        Toolbar({},
          ToolbarButton({className: "btn save", onClick: this.onSave},
            Locale.$STR("jsonViewer.Save")
          ),
          ToolbarButton({className: "btn copy", onClick: this.onCopy},
            Locale.$STR("jsonViewer.Copy")
          ),
          SearchBox({
            actions: this.props.actions
          })
        )
      );
    },
  }));

  // Exports from this module
  exports.JsonPanel = JsonPanel;
});

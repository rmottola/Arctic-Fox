/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

require("devtools/client/webconsole/new-console-output/test/requireHelper")();

let ReactDOM = require("devtools/client/shared/vendor/react-dom");
let React = require("devtools/client/shared/vendor/react");
var TestUtils = React.addons.TestUtils;

const actions = require("devtools/client/webconsole/new-console-output/actions/messages");
const { configureStore } = require("devtools/client/webconsole/new-console-output/store");
const { IdGenerator } = require("devtools/client/webconsole/new-console-output/utils/id-generator");
const { stubConsoleMessages } = require("devtools/client/webconsole/new-console-output/test/fixtures/stubs");
const Services = require("devtools/client/webconsole/new-console-output/test/fixtures/Services");

/**
 * Prepare actions for use in testing.
 */
function setupActions() {
  // Some actions use dependency injection. This helps them avoid using state in
  // a hard-to-test way. We need to inject stubbed versions of these dependencies.
  const wrappedActions = Object.assign({}, actions);

  const idGenerator = new IdGenerator();
  wrappedActions.messageAdd = (packet) => {
    return actions.messageAdd(packet, idGenerator);
  };

  return wrappedActions;
}

/**
 * Prepare the store for use in testing.
 */
function setupStore(input) {
  // Inject the Services stub.
  const store = configureStore(Services);

  // Add the messages from the input commands to the store.
  input.forEach((cmd) => {
    store.dispatch(actions.messageAdd(stubConsoleMessages.get(cmd)));
  });

  return store;
}

function renderComponent(component, props) {
  const el = React.createElement(component, props, {});
  // By default, renderIntoDocument() won't work for stateless components, but
  // it will work if the stateless component is wrapped in a stateful one.
  // See https://github.com/facebook/react/issues/4839
  const wrappedEl = React.DOM.span({}, [el]);
  const renderedComponent = TestUtils.renderIntoDocument(wrappedEl);
  return ReactDOM.findDOMNode(renderedComponent).children[0];
}

function shallowRenderComponent(component, props) {
  const el = React.createElement(component, props);
  const renderer = TestUtils.createRenderer();
  renderer.render(el, {});
  return renderer.getRenderOutput();
}

module.exports = {
  setupActions,
  setupStore,
  renderComponent,
  shallowRenderComponent
};

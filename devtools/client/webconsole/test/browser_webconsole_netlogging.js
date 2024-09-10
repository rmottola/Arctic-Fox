/* vim:set ts=2 sw=2 sts=2 et: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Tests response logging for different request types.

"use strict";

const TEST_NETWORK_REQUEST_URI =
  "http://example.com/browser/devtools/client/webconsole/test/" +
  "test-network-request.html";

const TEST_DATA_JSON_CONTENT =
  '{ id: "test JSON data", myArray: [ "foo", "bar", "baz", "biff" ] }';

add_task(function* testPageLoad() {
  // Enable logging in the UI.  Not needed to pass test but makes it easier
  // to debug interactively.
  yield new Promise(resolve => {
    SpecialPowers.pushPrefEnv({"set":
      [["devtools.webconsole.filter.networkinfo", true]
    ]}, resolve);
  });

  let finishedRequest = waitForFinishedRequest(PAGE_REQUEST_PREDICATE);
  let hud = yield loadPageAndGetHud(TEST_NETWORK_REQUEST_URI);
  let request = yield finishedRequest;

  ok(request, "Page load was logged");

  let client = hud.ui.webConsoleClient;
  let args = [request.actor];
  const postData = yield getPacket(client, "getRequestPostData", args);
  const responseContent = yield getPacket(client, "getResponseContent", args);

  is(request.request.url, TEST_NETWORK_REQUEST_URI,
    "Logged network entry is page load");
  is(request.request.method, "GET", "Method is correct");
  ok(!postData.postData.text, "No request body was stored");
  ok(!postData.postDataDiscarded,
    "Request body was not discarded");
  is(responseContent.content.text.indexOf("<!DOCTYPE HTML>"), 0,
    "Response body's beginning is okay");

  yield closeTabAndToolbox();
});

add_task(function* testXhrGet() {
  let hud = yield loadPageAndGetHud(TEST_NETWORK_REQUEST_URI);

  let finishedRequest = waitForFinishedRequest();
  content.wrappedJSObject.testXhrGet();
  let request = yield finishedRequest;

  ok(request, "testXhrGet() was logged");

  let client = hud.ui.webConsoleClient;
  let args = [request.actor];
  const postData = yield getPacket(client, "getRequestPostData", args);
  const responseContent = yield getPacket(client, "getResponseContent", args);

  is(request.request.method, "GET", "Method is correct");
  ok(!postData.postData.text, "No request body was sent");
  ok(!postData.postDataDiscarded,
    "Request body was not discarded");
  is(responseContent.content.text, TEST_DATA_JSON_CONTENT,
    "Response is correct");

  yield closeTabAndToolbox();
});

add_task(function* testXhrPost() {
  let hud = yield loadPageAndGetHud(TEST_NETWORK_REQUEST_URI);

  let finishedRequest = waitForFinishedRequest();
  content.wrappedJSObject.testXhrPost();
  let request = yield finishedRequest;

  ok(request, "testXhrPost() was logged");

  let client = hud.ui.webConsoleClient;
  let args = [request.actor];
  const postData = yield getPacket(client, "getRequestPostData", args);
  const responseContent = yield getPacket(client, "getResponseContent", args);

  is(request.request.method, "POST", "Method is correct");
  is(postData.postData.text, "Hello world!", "Request body was logged");
  is(responseContent.content.text, TEST_DATA_JSON_CONTENT,
    "Response is correct");

  yield closeTabAndToolbox();
});

add_task(function* testFormSubmission() {
  let hud = yield loadPageAndGetHud(TEST_NETWORK_REQUEST_URI);

  let finishedRequest = waitForFinishedRequest();
  ContentTask.spawn(gBrowser.selectedBrowser, { }, `function()
  {
    let form = content.document.querySelector("form");
    form.submit();
  }`);
  let request = yield finishedRequest;

  ok(request, "testFormSubmission() was logged");

  let client = hud.ui.webConsoleClient;
  let args = [request.actor];
  const postData = yield getPacket(client, "getRequestPostData", args);
  const responseContent = yield getPacket(client, "getResponseContent", args);

  is(request.request.method, "POST", "Method is correct");
  isnot(postData.postData.text
    .indexOf("Content-Type: application/x-www-form-urlencoded"), -1,
    "Content-Type is correct");
  isnot(postData.postData.text
    .indexOf("Content-Length: 20"), -1, "Content-length is correct");
  isnot(postData.postData.text
    .indexOf("name=foo+bar&age=144"), -1, "Form data is correct");
  is(responseContent.content.text.indexOf("<!DOCTYPE HTML>"), 0,
    "Response body's beginning is okay");

  yield closeTabAndToolbox();
});

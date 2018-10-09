/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Check that the browser console gets session state is set correctly, and that
// it re-opens when restore is requested.

"use strict";

add_task(function* () {
  is(HUDService.getBrowserConsoleSessionState(), false, "Session state false by default");
  HUDService.storeBrowserConsoleSessionState();
  is(HUDService.getBrowserConsoleSessionState(), false,
    "Session state still not true even after setting (since Browser Console is closed)");

  yield HUDService.toggleBrowserConsole();
  HUDService.storeBrowserConsoleSessionState();
  is(HUDService.getBrowserConsoleSessionState(), true,
    "Session state true (since Browser Console is opened)");

  info("Closing the browser console and waiting for the session restore to reopen it")
  yield HUDService.toggleBrowserConsole();

  let opened = waitForBrowserConsole();
  HUDService.restoreBrowserConsoleSession();

  info("Waiting for the console to open after session restore")
  yield opened;
});

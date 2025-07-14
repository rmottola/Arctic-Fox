/*
 * Test the password manager context menu.
 */

"use strict";

Cu.import("resource://testing-common/LoginTestUtils.jsm", this);

// The hostname for the test URIs.
const TEST_HOSTNAME = "https://example.com";

/**
 * Initialize logins needed for the tests and disable autofill
 * for login forms for easier testing of manual fill.
 */
add_task(function* test_initialize() {
  Services.prefs.setBoolPref("signon.autofillForms", false);
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("signon.autofillForms");
    Services.prefs.clearUserPref("signon.schemeUpgrades");
  });
  for (let login of loginList()) {
    Services.logins.addLogin(login);
  }
});

/**
 * Check if the context menu is populated with the right
 * menuitems for the target password input field.
 */
add_task(function* test_context_menu_populate() {
  yield BrowserTestUtils.withNewTab({
    gBrowser,
    url: TEST_HOSTNAME + "/browser/toolkit/components/" +
         "passwordmgr/test/browser/multiple_forms.html",
  }, function* (browser) {
    let passwordInput = browser.contentWindow.document.getElementById("test-password-1");

    yield openPasswordContextMenu(browser, passwordInput);

    // Check the content of the password manager popup
    let popupMenu = document.getElementById("fill-login-popup");
    checkMenu(popupMenu, 3);

    let contextMenu = document.getElementById("contentAreaContextMenu");
    contextMenu.hidePopup();
  });
});

/**
 * Check if the password field is correctly filled when one
 * login menuitem is clicked.
 */
add_task(function* test_context_menu_password_fill() {
  // Set of element ids to check.
  let testSet = [
    {
      passwordInput: "test-password-1",
      unchangedFields: null,
    },
    {
      passwordInput: "test-password-2",
      unchangedFields: ["test-username-2"],
    },
    {
      passwordInput: "test-password-3",
      unchangedFields: ["test-username-3"],
    },
    {
      passwordInput: "test-password-4",
      unchangedFields: ["test-username-4"],
    },
    {
      passwordInput: "test-password-5",
      unchangedFields: ["test-username-5", "test-password2-5"],
    },
    {
      passwordInput: "test-password2-5",
      unchangedFields: ["test-username-5", "test-password-5"],
    },
    {
      passwordInput: "test-password-6",
      unchangedFields: ["test-username-6", "test-password2-6"],
    },
    {
      passwordInput: "test-password2-6",
      unchangedFields: ["test-username-6", "test-password-6"],
    },
    {
      passwordInput: "test-password-7",
      unchangedFields: null,
    },
  ];

  yield BrowserTestUtils.withNewTab({
    gBrowser,
    url: TEST_HOSTNAME + "/browser/toolkit/components/" +
         "passwordmgr/test/browser/multiple_forms.html",
  }, function* (browser) {
    for (let testCase of testSet) {
      let passwordInput = browser.contentWindow.document.getElementById(testCase.passwordInput);

      yield openPasswordContextMenu(browser, passwordInput);

      let popupMenu = document.getElementById("fill-login-popup");

      // Store the values of fields that should remain unchanged.
      let unchangedFieldsValues = null;
      if (testCase.unchangedFields) {
        unchangedFieldsValues = [];
        for (let fieldId of testCase.unchangedFields) {
          unchangedFieldsValues[fieldId] = browser.contentWindow.document.getElementById(fieldId).value;
        }
      }

      // Execute the default command of the first login menuitem found at the context menu.
      let firstLoginItem = popupMenu.getElementsByClassName("context-login-item")[0];
      firstLoginItem.doCommand();

      yield BrowserTestUtils.waitForEvent(passwordInput, "input", "Password input value changed");

      // Find the used login by it's username (Use only unique usernames in this test).
      let login = getLoginFromUsername(firstLoginItem.label);

      Assert.equal(login.password, passwordInput.value, "Password filled and correct.");

      // Check that the fields that should remain unchanged didn't got modified.
      if (testCase.unchangedFields) {
        Assert.ok(testCase.unchangedFields.every(fieldId => {
          return unchangedFieldsValues[fieldId] == browser.contentWindow.document.getElementById(fieldId).value;
        }), "Other fields were not changed.");
      }

      let contextMenu = document.getElementById("contentAreaContextMenu");
      contextMenu.hidePopup();
    }
  });
});

/**
 * Check if the password field is correctly filled when it's in an iframe.
 */
add_task(function* test_context_menu_iframe_fill() {
  Services.prefs.setBoolPref("signon.schemeUpgrades", true);
  yield BrowserTestUtils.withNewTab({
    gBrowser,
    url: TEST_HOSTNAME + "/browser/toolkit/components/" +
         "passwordmgr/test/browser/multiple_forms.html",
  }, function* (browser) {
    let iframe = browser.contentWindow.document.getElementById("test-iframe");
    let passwordInput = iframe.contentDocument.getElementById("form-basic-password");

    let contextMenuShownPromise = BrowserTestUtils.waitForEvent(window, "popupshown");
    let eventDetails = {type: "contextmenu", button: 2};

    // To click at the right point we have to take into account the iframe offset.
    let iframeRect = iframe.getBoundingClientRect();
    let inputRect = passwordInput.getBoundingClientRect();
    let clickPos = {
      offsetX: iframeRect.left + inputRect.width / 2,
      offsetY: iframeRect.top  + inputRect.height / 2,
    };

    // Synthesize a right mouse click over the password input element.
    BrowserTestUtils.synthesizeMouse(passwordInput, clickPos.offsetX, clickPos.offsetY, eventDetails, browser);
    yield contextMenuShownPromise;

    // Synthesize a mouse click over the fill login menu header.
    let popupHeader = document.getElementById("fill-login");
    let popupShownPromise = BrowserTestUtils.waitForEvent(popupHeader, "popupshown");
    EventUtils.synthesizeMouseAtCenter(popupHeader, {});
    yield popupShownPromise;

    let popupMenu = document.getElementById("fill-login-popup");

    // Stores the original value of username
    let usernameInput = iframe.contentDocument.getElementById("form-basic-username");
    let usernameOriginalValue = usernameInput.value;

    // Execute the command of the first login menuitem found at the context menu.
    let firstLoginItem = popupMenu.getElementsByClassName("context-login-item")[0];
    firstLoginItem.doCommand();

    yield BrowserTestUtils.waitForEvent(passwordInput, "input", "Password input value changed");

    // Find the used login by it's username.
    let login = getLoginFromUsername(firstLoginItem.label);

    Assert.equal(login.password, passwordInput.value, "Password filled and correct.");

    Assert.equal(usernameOriginalValue,
                 usernameInput.value,
                 "Username value was not changed.");

    let contextMenu = document.getElementById("contentAreaContextMenu");
    contextMenu.hidePopup();
  });
});

/**
 * Synthesize mouse clicks to open the password manager context menu popup
 * for a target password input element.
 */
function* openPasswordContextMenu(browser, passwordInput) {
  // Synthesize a right mouse click over the password input element.
  let contextMenuShownPromise = BrowserTestUtils.waitForEvent(window, "popupshown");
  let eventDetails = {type: "contextmenu", button: 2};
  BrowserTestUtils.synthesizeMouseAtCenter(passwordInput, eventDetails, browser);
  yield contextMenuShownPromise;

  // Synthesize a mouse click over the fill login menu header.
  let popupHeader = document.getElementById("fill-login");
  let popupShownPromise = BrowserTestUtils.waitForEvent(popupHeader, "popupshown");
  EventUtils.synthesizeMouseAtCenter(popupHeader, {});
  yield popupShownPromise;
}

/**
 * Check if every login that matches the page hostname are available at the context menu.
 * @param {Element} contextMenu
 * @param {Number} expectedCount - Number of logins expected in the context menu. Used to ensure
*                                  we continue testing something useful.
 */
function checkMenu(contextMenu, expectedCount) {
  let logins = loginList().filter(login => {
    return LoginHelper.isOriginMatching(login.hostname, TEST_HOSTNAME, {
      schemeUpgrades: Services.prefs.getBoolPref("signon.schemeUpgrades"),
    });
  });
  // Make an array of menuitems for easier comparison.
  let menuitems = [...contextMenu.getElementsByClassName("context-login-item")];
  Assert.equal(menuitems.length, expectedCount, "Expected number of menu items");
  Assert.ok(logins.every(l => menuitems.some(m => l.username == m.label)), "Every login have an item at the menu.");
}

/**
 * Search for a login by it's username.
 *
 * Only unique login/hostname combinations should be used at this test.
 */
function getLoginFromUsername(username) {
  return loginList().find(login => login.username == username);
}

/**
 * List of logins used for the test.
 *
 * We should only use unique usernames in this test,
 * because we need to search logins by username. There is one duplicate u+p combo
 * in order to test de-duping in the menu.
 */
function loginList() {
  return [
    LoginTestUtils.testData.formLogin({
      hostname: "https://example.com",
      formSubmitURL: "https://example.com",
      username: "username",
      password: "password",
    }),
    // Same as above but HTTP in order to test de-duping.
    LoginTestUtils.testData.formLogin({
      hostname: "http://example.com",
      formSubmitURL: "http://example.com",
      username: "username",
      password: "password",
    }),
    LoginTestUtils.testData.formLogin({
      hostname: "http://example.com",
      formSubmitURL: "http://example.com",
      username: "username1",
      password: "password1",
    }),
    LoginTestUtils.testData.formLogin({
      hostname: "https://example.com",
      formSubmitURL: "https://example.com",
      username: "username2",
      password: "password2",
    }),
    LoginTestUtils.testData.formLogin({
      hostname: "http://example.org",
      formSubmitURL: "http://example.org",
      username: "username-cross-origin",
      password: "password-cross-origin",
    }),
  ];
}

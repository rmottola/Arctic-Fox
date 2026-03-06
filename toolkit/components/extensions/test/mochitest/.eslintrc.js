{
  "extends": "../../.eslintrc",

  "globals": {
    "ChromeWorker": false,
    "onmessage": true,
    "sendAsyncMessage": false,

    "ExtensionTestUtils": false,
    "NetUtil": true,
    "webrequest_test": false,
    "XPCOMUtils": true,

    "waitForLoad": true,
    "promiseConsoleOutput": true,

    // Test harness globals
    "add_task": false,
    "info": false,
    "is": false,
    "ok": false,
    "SimpleTest": false,
    "SpecialPowers": true,
  },

  "env": {
    "browser": true,
    "webextensions": true,
  }
}

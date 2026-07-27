/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const TEST_URI = "http://example.com/browser/dom/tests/browser/test_largeAllocation.html";

function expectProcessCreated() {
  let os = Cc["@mozilla.org/observer-service;1"].getService(Ci.nsIObserverService);
  let kill; // A kill function which will disable the promise.
  let promise = new Promise((resolve, reject) => {
    let topic = "ipc:content-created";
    function observer() {
      os.removeObserver(observer, topic);
      ok(true, "Expect process created");
      resolve();
    }
    os.addObserver(observer, topic, /* weak = */ false);
    kill = () => {
      os.removeObserver(observer, topic);
      ok(true, "Expect process created killed");
      reject();
    };
  });
  promise.kill = kill;
  return promise;
}

function expectNoProcess() {
  let os = Cc["@mozilla.org/observer-service;1"].getService(Ci.nsIObserverService);
  let topic = "ipc:content-created";
  function observer() {
    ok(false, "A process was created!");
    os.removeObserver(observer, topic);
  }
  os.addObserver(observer, topic, /* weak = */ false);

  return () => os.removeObserver(observer, topic);
}

function getPID(aBrowser) {
  return ContentTask.spawn(aBrowser, null, () => {
    const appinfo = Components.classes["@mozilla.org/xre/app-info;1"]
            .getService(Components.interfaces.nsIXULRuntime);
    return appinfo.processID;
  });
}

function getInLAProc(aBrowser) {
  return ContentTask.spawn(aBrowser, null, () => {
    try {
      return docShell.inLargeAllocProcess;
    } catch (e) {
      // This must be a non-remote browser, which means it is not fresh
      return false;
    }
  });
}

add_task(function*() {
  // I'm terrible and put this set of tests into a single file, so I need a longer timeout
  requestLongerTimeout(2);

  yield SpecialPowers.pushPrefEnv({
    set: [
      ["dom.largeAllocationHeader.enabled", true],
    ]
  });

  // A toplevel tab should be able to navigate cross process!
  yield BrowserTestUtils.withNewTab("about:blank", function*(aBrowser) {
    ok(true, "Starting test 0");
    let pid1 = yield getPID(aBrowser);
    is(false, yield getInLAProc(aBrowser));

    let epc = expectProcessCreated();
    yield ContentTask.spawn(aBrowser, TEST_URI, TEST_URI => {
      content.document.location = TEST_URI;
    });

    // Wait for the new process to be created
    yield epc;

    let pid2 = yield getPID(aBrowser);

    isnot(pid1, pid2, "The pids should be different between the initial load and the new load");
    is(true, yield getInLAProc(aBrowser));
  });

  // When a Large-Allocation document is loaded in an iframe, the header should
  // be ignored, and the tab should stay in the current process.
  yield BrowserTestUtils.withNewTab("about:blank", function*(aBrowser) {
    ok(true, "Starting test 1");
    let pid1 = yield getPID(aBrowser);
    is(false, yield getInLAProc(aBrowser));

    // Fail the test if we create a process
    let stopExpectNoProcess = expectNoProcess();

    yield ContentTask.spawn(aBrowser, TEST_URI, TEST_URI => {
      content.document.body.innerHTML = `<iframe src='${TEST_URI}'></iframe>`;

      return new Promise(resolve => {
        content.document.body.querySelector('iframe').onload = () => {
          ok(true, "Iframe finished loading");
          resolve();
        };
      });
    });

    let pid2 = yield getPID(aBrowser);

    is(pid1, pid2, "The PID should not have changed");
    is(false, yield getInLAProc(aBrowser));

    stopExpectNoProcess();
  });

  // If you have an opener cross process navigation shouldn't work
  yield BrowserTestUtils.withNewTab("http://example.com", function*(aBrowser) {
    ok(true, "Starting test 2");
    let pid1 = yield getPID(aBrowser);
    is(false, yield getInLAProc(aBrowser));

    // Fail the test if we create a process
    let stopExpectNoProcess = expectNoProcess();

    let loaded = ContentTask.spawn(aBrowser, TEST_URI, TEST_URI => {
      content.document.body.innerHTML = '<button>CLICK ME</button>';

      return new Promise(resolve => {
        content.document.querySelector('button').onclick = e => {
          let w = content.window.open(TEST_URI, '_blank');
          w.onload = () => {
            ok(true, "Window finished loading");
            w.close();
            resolve();
          };
        };
      });
    });

    yield BrowserTestUtils.synthesizeMouseAtCenter("button", {}, aBrowser);

    yield loaded;

    let pid2 = yield getPID(aBrowser);

    is(pid1, pid2, "The PID should not have changed");
    is(false, yield getInLAProc(aBrowser));

    stopExpectNoProcess();
  });

  // Load Large-Allocation twice with about:blank load in between
  yield BrowserTestUtils.withNewTab("about:blank", function*(aBrowser) {
    ok(true, "Starting test 3");
    let pid1 = yield getPID(aBrowser);
    is(false, yield getInLAProc(aBrowser));

    let epc = expectProcessCreated();

    yield ContentTask.spawn(aBrowser, TEST_URI, TEST_URI => {
      content.document.location = TEST_URI;
    });

    yield epc;

    let pid2 = yield getPID(aBrowser);

    isnot(pid1, pid2);
    is(true, yield getInLAProc(aBrowser));

    yield BrowserTestUtils.browserLoaded(aBrowser);

    yield ContentTask.spawn(aBrowser, null, () => content.document.location = "about:blank");

    yield BrowserTestUtils.browserLoaded(aBrowser);

    let pid3 = yield getPID(aBrowser);

    // We should have been kicked out of the large-allocation process by the
    // load, meaning we're back in a non-fresh process
    is(false, yield getInLAProc(aBrowser));

    epc = expectProcessCreated();

    yield ContentTask.spawn(aBrowser, TEST_URI, TEST_URI => {
      content.document.location = TEST_URI;
    });

    yield epc;

    let pid4 = yield getPID(aBrowser);

    isnot(pid1, pid4);
    isnot(pid2, pid4);
  });

  // Load Large-Allocation then about:blank load, then back button press should load from bfcache.
  yield BrowserTestUtils.withNewTab("about:blank", function*(aBrowser) {
    ok(true, "Starting test 4");
    let pid1 = yield getPID(aBrowser);

    let epc = expectProcessCreated();

    yield ContentTask.spawn(aBrowser, TEST_URI, TEST_URI => {
      content.document.location = TEST_URI;
    });

    yield epc;

    let pid2 = yield getPID(aBrowser);

    isnot(pid1, pid2, "PIDs 1 and 2 should not match");
    is(true, yield getInLAProc(aBrowser));

    yield BrowserTestUtils.browserLoaded(aBrowser);

    // Switch to about:blank, so we can navigate back
    yield ContentTask.spawn(aBrowser, null, () => {
      content.document.location = "about:blank";
    });

    yield BrowserTestUtils.browserLoaded(aBrowser);

    let pid3 = yield getPID(aBrowser);

    // We should have been kicked out of the large-allocation process by the
    // load, meaning we're back in a non-large-allocation process.
    is(false, yield getInLAProc(aBrowser));

    epc = expectProcessCreated();

    // Navigate back to the previous page. As the large alloation process was
    // left, it won't be in bfcache and will have to be loaded fresh.
    yield ContentTask.spawn(aBrowser, TEST_URI, TEST_URI => {
      content.window.history.back();
    });

    yield epc;

    let pid4 = yield getPID(aBrowser);

    isnot(pid1, pid4, "PID 4 shouldn't match PID 1");
    isnot(pid2, pid4, "PID 4 shouldn't match PID 2");

  });

  yield BrowserTestUtils.withNewTab("about:blank", function*(aBrowser) {
    info("Starting test 7");
    yield SpecialPowers.pushPrefEnv({
      set: [
        ["dom.ipc.processCount.webLargeAllocation", 1]
      ],
    });

    // Loading the first Large-Allocation tab should succeed as normal
    let pid1 = yield getPID(aBrowser);
    is(false, yield getInLAProc(aBrowser));

    let ready = Promise.all([expectProcessCreated(),
                             BrowserTestUtils.browserLoaded(aBrowser)]);

    yield ContentTask.spawn(aBrowser, TEST_URI, TEST_URI => {
      content.document.location = TEST_URI;
    });

    yield ready;

    let pid2 = yield getPID(aBrowser);

    isnot(pid1, pid2, "PIDs 1 and 2 should not match");
    is(true, yield getInLAProc(aBrowser));

    yield BrowserTestUtils.withNewTab("about:blank", function*(aBrowser) {
      // The second one should load in a non-LA proc because the
      // webLargeAllocation processes have been exhausted.
      is(false, yield getInLAProc(aBrowser));

      let ready = Promise.all([BrowserTestUtils.browserLoaded(aBrowser)]);
      yield ContentTask.spawn(aBrowser, TEST_URI, TEST_URI => {
        content.document.location = TEST_URI;
      });
      yield ready;

      is(false, yield getInLAProc(aBrowser));
    });
  });
});

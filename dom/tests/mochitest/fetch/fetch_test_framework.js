function testScript(script) {
  // reroute.html should have set this variable if a service worker is present!
  if (!("isSWPresent" in window)) {
    window.isSWPresent = false;
  }

  function setupPrefs() {
    return new Promise(function(resolve, reject) {
      SpecialPowers.pushPrefEnv({
        "set": [["dom.requestcontext.enabled", true],
                ["dom.serviceWorkers.enabled", true],
                ["dom.serviceWorkers.testing.enabled", true],
                ["dom.serviceWorkers.exemptFromPerDomainMax", true]]
      }, resolve);
    });
  }

  function workerTest() {
    return new Promise(function(resolve, reject) {
      var worker = new Worker("worker_wrapper.js");
      worker.onmessage = function(event) {
        if (event.data.context != "Worker") {
          return;
        }
        if (event.data.type == 'finish') {
          resolve();
        } else if (event.data.type == 'status') {
          ok(event.data.status, event.data.context + ": " + event.data.msg);
        }
      }
      worker.onerror = function(event) {
        reject("Worker error: " + event.message);
      };

      worker.postMessage({ "script": script });
    });
  }

  function nestedWorkerTest() {
    return new Promise(function(resolve, reject) {
      var worker = new Worker("nested_worker_wrapper.js");
      worker.onmessage = function(event) {
        if (event.data.context != "NestedWorker") {
          return;
        }
        if (event.data.type == 'finish') {
          resolve();
        } else if (event.data.type == 'status') {
          ok(event.data.status, event.data.context + ": " + event.data.msg);
        }
      }
      worker.onerror = function(event) {
        reject("Nested Worker error: " + event.message);
      };

      worker.postMessage({ "script": script });
    });
  }

  function serviceWorkerTest() {
    var isB2G = !navigator.userAgent.includes("Android") &&
                /Mobile|Tablet/.test(navigator.userAgent);
    if (isB2G) {
      // TODO B2G doesn't support running service workers for now due to bug 1137683.
      dump("Skipping running the test in SW until bug 1137683 gets fixed.\n");
      return Promise.resolve();
    }
    return new Promise(function(resolve, reject) {
      function setupSW(registration) {
        var worker = registration.waiting ||
                     registration.active;

        window.addEventListener("message",function onMessage(event) {
          if (event.data.context != "ServiceWorker") {
            return;
          }
          if (event.data.type == 'finish') {
            window.removeEventListener("message", onMessage);
            registration.unregister()
              .then(resolve)
              .catch(reject);
          } else if (event.data.type == 'status') {
            ok(event.data.status, event.data.context + ": " + event.data.msg);
          }
        }, false);

        worker.onerror = reject;

        var iframe = document.createElement("iframe");
        iframe.src = "message_receiver.html";
        iframe.onload = function() {
          worker.postMessage({ script: script });
        };
        document.body.appendChild(iframe);
      }

      navigator.serviceWorker.register("worker_wrapper.js", {scope: "."})
        .then(function(registration) {
          if (registration.installing) {
            var done = false;
            registration.installing.onstatechange = function() {
              if (!done) {
                done = true;
                setupSW(registration);
              }
            };
          } else {
            setupSW(registration);
          }
        });
    });
  }

  function windowTest() {
    return new Promise(function(resolve, reject) {
      var scriptEl = document.createElement("script");
      scriptEl.setAttribute("src", script);
      scriptEl.onload = function() {
        runTest().then(resolve, reject);
      };
      document.body.appendChild(scriptEl);
    });
  }

  SimpleTest.waitForExplicitFinish();
  // We have to run the window, worker and service worker tests sequentially
  // since some tests set and compare cookies and running in parallel can lead
  // to conflicting values.
  setupPrefs()
    .then(function() {
      return windowTest();
    })
    .then(function() {
      return workerTest();
    })
    .then(function() {
      return nestedWorkerTest();
    })
    .then(function() {
      return serviceWorkerTest();
    })
    .catch(function(e) {
      ok(false, "Some test failed in " + script);
      info(e);
      info(e.message);
      return Promise.resolve();
    })
    .then(function() {
      if (parent && parent.finishTest) {
        parent.finishTest();
      } else {
        SimpleTest.finish();
      }
    });
}

// Utilities
// =========

// Helper that uses FileReader or FileReaderSync based on context and returns
// a Promise that resolves with the text or rejects with error.
function readAsText(blob) {
  if (typeof FileReader !== "undefined") {
    return new Promise(function(resolve, reject) {
      var fs = new FileReader();
      fs.onload = function() {
        resolve(fs.result);
      }
      fs.onerror = reject;
      fs.readAsText(blob);
    });
  } else {
    var fs = new FileReaderSync();
    return Promise.resolve(fs.readAsText(blob));
  }
}

function readAsArrayBuffer(blob) {
  if (typeof FileReader !== "undefined") {
    return new Promise(function(resolve, reject) {
      var fs = new FileReader();
      fs.onload = function() {
        resolve(fs.result);
      }
      fs.onerror = reject;
      fs.readAsArrayBuffer(blob);
    });
  } else {
    var fs = new FileReaderSync();
    return Promise.resolve(fs.readAsArrayBuffer(blob));
  }
}

function testScript(script) {
  function workerTest() {
    return new Promise(function(resolve, reject) {
      var worker = new Worker("worker_wrapper.js");
      worker.onmessage = function(event) {
        if (event.data.type == 'finish') {
          resolve();
        } else if (event.data.type == 'status') {
          ok(event.data.status, "Worker fetch test: " + event.data.msg);
        }
      }
      worker.onerror = function(event) {
        reject("Worker error: " + event.message);
      };

      worker.postMessage({ "script": script });
    });
  }

  function windowTest() {
    return new Promise(function(resolve, reject) {
      var scriptEl = document.createElement("script");
      scriptEl.setAttribute("src", script);
      scriptEl.onload = function() {
        runTest().then(resolve, reject);
      };
      document.body.appendChild(scriptEl);
    });
  }

  SimpleTest.waitForExplicitFinish();
  // We have to run the window and worker tests sequentially since some tests
  // set and compare cookies and running in parallel can lead to conflicting
  // values.
  windowTest()
    .then(function() {
      return workerTest();
    })
    .catch(function(e) {
      ok(false, "Some test failed in " + script);
      info(e);
      info(e.message);
      return Promise.resolve();
    })
    .then(function() {
      SimpleTest.finish();
    });
}


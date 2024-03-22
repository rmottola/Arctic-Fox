
let tempFile;
add_task(function* setup() {
  yield new Promise(resolve => {
    SpecialPowers.pushPrefEnv({"set": [["ui.tooltipDelay", 0]]}, resolve);
  });
  tempFile = createTempFile();
  registerCleanupFunction(function() {
    tempFile.remove(true);
  });
});

add_task(function* test_singlefile_selected() {
  yield do_test({value: true, result: "testfile_bug1251809"});
});

add_task(function* test_title_set() {
  yield do_test({title: "foo", result: "foo"});
});

add_task(function* test_nofile_selected() {
  yield do_test({result: "No file selected."});
});

add_task(function* test_multipleset_nofile_selected() {
  yield do_test({multiple: true, result: "No files selected."});
});

add_task(function* test_requiredset() {
  yield do_test({required: true, result: "Please select a file."});
});

function* do_test(test) {
  info(`starting test ${JSON.stringify(test)}`);

  let tab = yield BrowserTestUtils.openNewForegroundTab(gBrowser);

  yield new Promise(resolve => {
    EventUtils.synthesizeNativeMouseMove(tab.linkedBrowser, 300, 300, resolve);
  });

  ContentTask.spawn(tab.linkedBrowser, test, function*(test) {
    let doc = content.document;
    let input = doc.createElement("input");
    doc.body.appendChild(input);
    input.id = "test_input";
    input.setAttribute("style", "position: absolute; top: 0; left: 0;");
    input.type = "file";
    if (test.title) {
      input.setAttribute("title", test.title);
    }
    if (test.multiple) {
      input.multiple = true;
    }
    if (test.required) {
      input.required = true;
    }
  });

  if (test.value) {
    let MockFilePicker = SpecialPowers.MockFilePicker;
    MockFilePicker.init(window);
    MockFilePicker.returnValue = MockFilePicker.returnOK;
    MockFilePicker.displayDirectory = FileUtils.getDir("TmpD", [], false);
    MockFilePicker.returnFiles = [tempFile];

    try {
      // Open the File Picker dialog (MockFilePicker) to select
      // the files for the test.
      yield BrowserTestUtils.synthesizeMouseAtCenter("#test_input", {}, tab.linkedBrowser);
      yield ContentTask.spawn(tab.linkedBrowser, {}, function*() {
        let input = content.document.querySelector("#test_input");
        yield ContentTaskUtils.waitForCondition(() => input.files.length,
          "The input should have at least one file selected");
        info(`The input has ${input.files.length} file(s) selected.`);
      });
    } finally {
      MockFilePicker.cleanup();
    }
  }

  let awaitTooltipOpen = new Promise(resolve => {
    let tooltipId = Services.appinfo.browserTabsRemoteAutostart ?
                      "remoteBrowserTooltip" :
                      "aHTMLTooltip";
    let tooltip = document.getElementById(tooltipId);
    tooltip.addEventListener("popupshown", function onpopupshown(event) {
      tooltip.removeEventListener("popupshown", onpopupshown);
      resolve(event.target);
    });
  });
  yield new Promise(resolve => {
    EventUtils.synthesizeNativeMouseMove(tab.linkedBrowser, 100, 5, resolve);
  });
  yield new Promise(resolve => setTimeout(resolve, 100));
  yield new Promise(resolve => {
    EventUtils.synthesizeNativeMouseMove(tab.linkedBrowser, 110, 15, resolve);
  });
  let tooltip = yield awaitTooltipOpen;

  is(tooltip.getAttribute("label"), test.result, "tooltip label should match expectation");

  yield BrowserTestUtils.removeTab(tab);
}

function createTempFile() {
  let file = FileUtils.getDir("TmpD", [], false);
  file.append("testfile_bug1251809");
  file.create(Ci.nsIFile.NORMAL_FILE_TYPE, 0o644);
  return file;
}

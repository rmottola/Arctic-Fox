/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Test that the copy contextmenu has been added to the stack frames view.
 */

 const TAB_URL = EXAMPLE_URL + "doc_recursion-stack.html";
 let gTab, gPanel, gDebugger;
 let gFrames, gContextMenu;

 function test() {
   initDebugger(TAB_URL).then(([aTab,, aPanel]) => {
    gTab = aTab;
    gPanel = aPanel;
    gDebugger = gPanel.panelWin;
    gFrames = gDebugger.DebuggerView.StackFrames;

    waitForDebuggerEvents(gPanel, gDebugger.EVENTS.AFTER_FRAMES_REFILLED)
      .then(performTest);
    callInTab(gTab, "simpleCall");
   });
 }

 function performTest() {
   gContextMenu = gDebugger.document.getElementById("stackFramesContextMenu");
   is(gDebugger.gThreadClient.state, "paused",
     "Should only be getting stack frames while paused.");
   is(gFrames.itemCount, 1,
     "Should have only one frame.");
   ok(gContextMenu, "The stack frame's context menupopup is available.");

   once(gContextMenu, "popupshown").then(testContextMenu);
   EventUtils.synthesizeMouseAtCenter(gFrames.getItemAtIndex(0).prebuiltNode, {type: 'contextmenu', button: 2}, gDebugger);
 }

 function testContextMenu() {
   let document = gDebugger.document;
   ok(document.getElementById("copyStackMenuItem"),
    "#copyStackMenuItem found.");

   gContextMenu.hidePopup();
   resumeDebuggerThenCloseAndFinish(gPanel);
 }

 registerCleanupFunction(function() {
   gTab = null;
   gPanel = null;
   gDebugger = null;
   gFrames = null;
   gContextMenu = null;
 });

/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Check that a player's currentTime can be changed and that the AnimationsActor
// allows changing many players' currentTimes at once.

add_task(function*() {
  let {client, walker, animations} =
    yield initAnimationsFrontForUrl(MAIN_DOMAIN + "animation.html");

  yield testSetCurrentTime(walker, animations);
  yield testSetCurrentTimes(walker, animations);

  yield closeDebuggerClient(client);
  gBrowser.removeCurrentTab();
});

function* testSetCurrentTime(walker, animations) {
  info("Retrieve an animated node");
  let node = yield walker.querySelector(walker.rootNode, ".simple-animation");

  info("Retrieve the animation player for the node");
  let [player] = yield animations.getAnimationPlayersForNode(node);

  ok(player.setCurrentTime, "Player has the setCurrentTime method");

  info("Set the current time to currentTime + 5s");
  yield player.setCurrentTime(player.initialState.currentTime + 5000);

  let updatedState1 = yield player.getCurrentState();
  is(Math.round(updatedState1.currentTime - pausedState.currentTime), 5000,
    "The currentTime was updated to +5s");

  info("Set the current time to currentTime - 2s");
  yield player.setCurrentTime(updatedState1.currentTime - 2000);
  let updatedState2 = yield player.getCurrentState();
  is(Math.round(updatedState2.currentTime - updatedState1.currentTime), -2000,
    "The currentTime was updated to -2s");
}

function* testSetCurrentTimes(walker, animations) {
  ok(animations.setCurrentTimes, "The AnimationsActor has the right method");

  info("Retrieve multiple animated node and its animation players");

  let nodeMulti = yield walker.querySelector(walker.rootNode,
    ".multiple-animations");
  let players = (yield animations.getAnimationPlayersForNode(nodeMulti));

  ok(players.length > 1, "Node has more than 1 animation player");

  info("Try to set multiple current times at once");
  yield animations.setCurrentTimes(players, 500, true);

  info("Get the states of players and verify their correctness");
  for (let i = 0; i < players.length; i++) {
    let state = yield players[i].getCurrentState();
    is(state.playState, "paused", `Player ${i + 1} is paused`);
    is(state.currentTime, 500, `Player ${i + 1} has the right currentTime`);
  }
}

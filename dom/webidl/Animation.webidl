/* -*- Mode: IDL; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is
 * http://w3c.github.io/web-animations/#the-animation-interface
 *
 * Copyright © 2015 W3C® (MIT, ERCIM, Keio), All Rights Reserved. W3C
 * liability, trademark and document use rules apply.
 */

enum AnimationPlayState { "idle", "pending", "running", "paused", "finished" };

[Func="nsDocument::IsElementAnimateEnabled",
 Constructor (optional KeyframeEffectReadOnly? effect = null,
              optional AnimationTimeline? timeline = null)]
interface Animation : EventTarget {
  attribute DOMString id;
  // Bug 1049975: Make 'effect' writeable
  [Func="nsDocument::IsWebAnimationsEnabled", Pure]
  readonly attribute AnimationEffectReadOnly? effect;
  [Func="nsDocument::IsWebAnimationsEnabled"]
  readonly attribute AnimationTimeline? timeline;
  [BinaryName="startTimeAsDouble"]
  attribute double? startTime;
  [SetterThrows, BinaryName="currentTimeAsDouble"]
  attribute double? currentTime;

           attribute double             playbackRate;
  [BinaryName="playStateFromJS"]
  readonly attribute AnimationPlayState playState;
  [Func="nsDocument::IsWebAnimationsEnabled", Throws]
  readonly attribute Promise<Animation> ready;
  [Func="nsDocument::IsWebAnimationsEnabled", Throws]
  readonly attribute Promise<Animation> finished;
           attribute EventHandler       onfinish;
           attribute EventHandler       oncancel;
  void cancel ();
  [Throws]
  void finish ();
  [Throws, BinaryName="playFromJS"]
  void play ();
  [Throws, BinaryName="pauseFromJS"]
  void pause ();
  [Throws]
  void reverse ();
};

// Non-standard extensions
partial interface Animation {
  [ChromeOnly] readonly attribute boolean isRunningOnCompositor;
};

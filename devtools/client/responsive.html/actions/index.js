/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// This file lists all of the actions available in responsive design.  This
// central list of constants makes it easy to see all possible action names at
// a glance.  Please add a comment with each new action type.

createEnum([

  // The location of the page has changed.  This may be triggered by the user
  // directly entering a new URL, navigating with links, etc.
  "CHANGE_LOCATION",

  // Add an additional viewport to display the document.
  "ADD_VIEWPORT",

  // Resize the viewport.
  "RESIZE_VIEWPORT",

  // Rotate the viewport.
  "ROTATE_VIEWPORT",

], module.exports);

/**
 * Create a simple enum-like object with keys mirrored to values from an array.
 * This makes comparison to a specfic value simpler without having to repeat and
 * mis-type the value.
 */
function createEnum(array, target) {
  for (let key of array) {
    target[key] = key;
  }
  return target;
}

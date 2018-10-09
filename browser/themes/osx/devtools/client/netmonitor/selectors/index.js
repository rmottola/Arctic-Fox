/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { createSelector } = require("devtools/client/shared/vendor/reselect");

/**
 * Gets the total number of bytes representing the cumulated content size of
 * a set of requests. Returns 0 for an empty set.
 *
 * @param {array} items - an array of request items
 * @return {number} total bytes of requests
 */
function getContentSizeOfRequests(items) {
  if (!items.length) {
    return 0;
  }

  let result = 0;
  items.forEach((item) => {
    let size = item.attachment.contentSize;
    result += (typeof size == "number") ? size : 0;
  });

  return result;
}

function getTransferredSizeOfRequests(items) {
  if (!items.length) {
    return 0;
  }

  let result = 0;
  items.forEach((item) => {
    let size = item.attachment.transferredSize;
    result += (typeof size == "number") ? size : 0;
  });

  return result;
}

/**
 * Gets the total milliseconds for all requests. Returns null for an
 * empty set.
 *
 * @param {array} items - an array of request items
 * @return {object} total milliseconds for all requests
 */
function getMillisOfRequests(items) {
  if (!items.length) {
    return null;
  }

  const oldest = items.reduce((prev, curr) =>
    prev.attachment.startedMillis < curr.attachment.startedMillis ?
      prev : curr);
  const newest = items.reduce((prev, curr) =>
    prev.attachment.startedMillis > curr.attachment.startedMillis ?
      prev : curr);

  return newest.attachment.endedMillis - oldest.attachment.startedMillis;
}

const getDisplayedRequestsSummary = createSelector(
  (state) => state.requests.items,
  (requests) => ({
    count: requests.length,
    contentSize: getContentSizeOfRequests(requests),
    transferredSize: getTransferredSizeOfRequests(requests),
    millis: getMillisOfRequests(requests),
  })
);

function getDisplayedTimingMarker(state, marker) {
  let timingMarker = null;
  if (state.timingMarkers) {
    timingMarker = state.timingMarkers.get(marker);
  }    
  let firstRequestStartedMillis = null;
  if (state.requests.items.length) {
    firstRequestStartedMillis = state.requests
                                .items[state.requests.items.length - 1]
                                .attachment
                                .firstRequestStartedMillisNotPersistent;
  }
  if (timingMarker && firstRequestStartedMillis) {
    return timingMarker - firstRequestStartedMillis;
  } else {
    return -1;
  }
}

module.exports = {
  getDisplayedRequestsSummary,
  getDisplayedTimingMarker,
};

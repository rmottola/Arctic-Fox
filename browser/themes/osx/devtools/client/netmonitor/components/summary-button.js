/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* globals NetMonitorView */

"use strict";

const {
  CONTENT_SIZE_DECIMALS,
  REQUEST_TIME_DECIMALS,
} = require("../constants");
const { DOM, PropTypes } = require("devtools/client/shared/vendor/react");
const { connect } = require("devtools/client/shared/vendor/react-redux");
const { PluralForm } = require("devtools/shared/plural-form");
const { L10N } = require("../l10n");
const { 
  getDisplayedRequestsSummary,
  getDisplayedTimingMarker
} = require("../selectors/index");

const { button, span } = DOM;

function SummaryButton({
  summary,
  triggerSummary,
  timingMarkers
}) {
  let { count, contentSize, transferredSize, millis } = summary;
  let {
    DOMContentLoaded,
    load,
  } = timingMarkers;
  const text = (count === 0) ? L10N.getStr("networkMenu.empty") :
    PluralForm.get(count, L10N.getStr("networkMenu.summary2"))
    .replace("#1", count)
    .replace("#2", L10N.numberWithDecimals(contentSize / 1024,
      CONTENT_SIZE_DECIMALS))
    .replace("#3", L10N.numberWithDecimals(transferredSize / 1024,
      CONTENT_SIZE_DECIMALS))
    .replace("#4", L10N.numberWithDecimals(millis / 1000,
      REQUEST_TIME_DECIMALS))
    + ((DOMContentLoaded > -1)
        ? ", " + "DOMContentLoaded: " + L10N.getFormatStrWithNumbers("networkMenu.timeS", L10N.numberWithDecimals(DOMContentLoaded / 1000, REQUEST_TIME_DECIMALS))
        : "")
    + ((load > -1)
        ? ", " + "load: " + L10N.getFormatStrWithNumbers("networkMenu.timeS", L10N.numberWithDecimals(load / 1000, REQUEST_TIME_DECIMALS))
        : "");

  return button({
    id: "requests-menu-network-summary-button",
    className: "devtools-button",
    title: count ? text : L10N.getStr("netmonitor.toolbar.perf"),
    onClick: triggerSummary,
  },
  span({ className: "summary-info-icon" }),
  span({ className: "summary-info-text" }, text));
}

SummaryButton.propTypes = {
  summary: PropTypes.object.isRequired,
  timingMarkers: PropTypes.object.isRequired,
};

module.exports = connect(
  (state) => ({
    summary: getDisplayedRequestsSummary(state),
    timingMarkers: {
      DOMContentLoaded:
        getDisplayedTimingMarker(state, "firstDocumentDOMContentLoadedTimestamp"),
      load: getDisplayedTimingMarker(state, "firstDocumentLoadTimestamp"),
    },
  }),
  (dispatch) => ({
    triggerSummary: () => {
      NetMonitorView.toggleFrontendMode();
    },
  })
)(SummaryButton);

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const SEARCH_ENGINES = {
  "Google": {
    // This is the "2x" image designed for OS X retina resolution, Windows at 192dpi, etc.;
    // it will be scaled down as necessary on lower-dpi displays.
    image: "data:image/png;base64," +
           "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"
  },
  "DuckDuckGo": {
  	 image: "data:image/png;base64," +
           "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"
  }
};

// This global tracks if the page has been set up before, to prevent double inits
var gInitialized = false;
var gObserver = new MutationObserver(function (mutations) {
  for (let mutation of mutations) {
    if (mutation.attributeName == "searchEngineName") {
      setupSearchEngine();
      if (!gInitialized) {
        gInitialized = true;
      }
      return;
    }
  }
});

window.addEventListener("pageshow", function () {
  // Delay search engine setup, cause browser.js::BrowserOnAboutPageLoad runs
  // later and may use asynchronous getters.
  window.gObserver.observe(document.documentElement, { attributes: true });
  fitToWidth();
  window.addEventListener("resize", fitToWidth);

  // Ask chrome to update snippets.
  var event = new CustomEvent("AboutHomeLoad", {bubbles:true});
  document.dispatchEvent(event);
});

window.addEventListener("pagehide", function() {
  window.gObserver.disconnect();
  window.removeEventListener("resize", fitToWidth);
});

function onSearchSubmit(aEvent)
{
  let searchText = document.getElementById("searchText");
  let searchTerms = searchText.value;
  let engineName = document.documentElement.getAttribute("searchEngineName");

  if (engineName && searchTerms.length > 0) {
    // Send an event that will perform a search and Firefox Health Report will
    // record that a search from about:home has occurred.
    let eventData = {
      engineName: engineName,
      searchTerms: searchTerms,
      originalEvent: {
        target: {
          ownerDocument: null
        },
        shiftKey: aEvent.shiftKey,
        ctrlKey: aEvent.ctrlKey,
        metaKey: aEvent.metaKey,
        altKey: aEvent.altKey,
        button: aEvent.button,
      },
    };

    if (searchText.hasAttribute("selection-index")) {
      eventData.selection = {
        index: searchText.getAttribute("selection-index"),
        kind: searchText.getAttribute("selection-kind")
      };
    }

    eventData = JSON.stringify(eventData);

    let event = new CustomEvent("AboutHomeSearchEvent", {detail: eventData});
    document.dispatchEvent(event);
  }

  gSearchSuggestionController.addInputValueToFormHistory();

  if (aEvent) {
    aEvent.preventDefault();
  }
}


let gSearchSuggestionController;

function setupSearchEngine()
{
  // The "autofocus" attribute doesn't focus the form element
  // immediately when the element is first drawn, so the
  // attribute is also used for styling when the page first loads.
  let searchText = document.getElementById("searchText");
  searchText.addEventListener("blur", function searchText_onBlur() {
    searchText.removeEventListener("blur", searchText_onBlur);
    searchText.removeAttribute("autofocus");
  });
 
  let searchEngineName = document.documentElement.getAttribute("searchEngineName");
  let searchEngineInfo = SEARCH_ENGINES[searchEngineName];
  let logoElt = document.getElementById("searchEngineLogo");

  // Add search engine logo.
  if (searchEngineInfo && searchEngineInfo.image) {
    logoElt.parentNode.hidden = false;
    logoElt.src = searchEngineInfo.image;
    logoElt.alt = searchEngineName;
    searchText.placeholder = "";
  }
  else {
    logoElt.parentNode.hidden = true;
    searchText.placeholder = searchEngineName;
  }

  if (!gSearchSuggestionController) {
    gSearchSuggestionController =
      new SearchSuggestionUIController(searchText, searchText.parentNode,
                                       onSearchSubmit);
  }
  gSearchSuggestionController.engineName = searchEngineName;
}

function fitToWidth() {
  if (window.scrollMaxX != window.scrollMinX) {
    document.body.setAttribute("narrow", "true");
  } else if (document.body.hasAttribute("narrow")) {
    document.body.removeAttribute("narrow");
    fitToWidth();
  }
}

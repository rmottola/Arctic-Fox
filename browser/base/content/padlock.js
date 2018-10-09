let Cc = Components.classes;
let Ci = Components.interfaces;
let Cu = Components.utils;
Cu.import("resource://gre/modules/XPCOMUtils.jsm");

var padlock_PadLock =
{
    QueryInterface: XPCOMUtils.generateQI([Ci.nsIWebProgressListener,
                                           Ci.nsISupportsWeakReference]),
  onButtonClick: function(event) {
    event.stopPropagation();
    gIdentityHandler.handleMoreInfoClick(event);
  },
    onStateChange: function() {},
    onProgressChange: function() {},
    onLocationChange: function() {},
    onStatusChange: function() {},
    onSecurityChange: function(aCallerWebProgress, aRequestWithState, aState) {
        // aState is defined as a bitmask that may be extended in the future.
        // We filter out any unknown bits before testing for known values.
        const wpl = Ci.nsIWebProgressListener;
        const wpl_security_bits = wpl.STATE_IS_SECURE |
                                  wpl.STATE_IS_BROKEN |
                                  wpl.STATE_IS_INSECURE |
                                  wpl.STATE_IDENTITY_EV_TOPLEVEL |
                                  wpl.STATE_SECURE_HIGH |
                                  wpl.STATE_SECURE_MED |
                                  wpl.STATE_SECURE_LOW;
        var level;
        var is_insecure;
        var highlight_urlbar = false;

        switch (aState & wpl_security_bits) {
          case wpl.STATE_IS_SECURE | wpl.STATE_SECURE_HIGH | wpl.STATE_IDENTITY_EV_TOPLEVEL:
            level = "ev";
            is_insecure = "";
            highlight_urlbar = true;
            break;
          case wpl.STATE_IS_SECURE | wpl.STATE_SECURE_HIGH:
            level = "high";
            is_insecure = "";
            highlight_urlbar = true;
            break;
          case wpl.STATE_IS_SECURE | wpl.STATE_SECURE_MED:
          case wpl.STATE_IS_SECURE | wpl.STATE_SECURE_LOW:
            level = "low";
            is_insecure = "insecure";
            break;
          case wpl.STATE_IS_BROKEN | wpl.STATE_SECURE_LOW:
            level = "mixed";
            is_insecure = "insecure";
            highlight_urlbar = true;
            break;
          case wpl.STATE_IS_BROKEN:
            level = "broken";
            is_insecure = "insecure";
            highlight_urlbar = true;
            break;
          default: // should not be reached
            level = null;
            is_insecure = "insecure";
        }

        try {
          var proto = gBrowser.contentWindow.location.protocol;
          if (proto == "about:" || proto == "chrome:" || proto == "file:" ) {
            // do not warn when using local protocols
            is_insecure = false;
          }
        }
        catch (ex) {}
        
        let ub = document.getElementById("urlbar");
        if (ub) { // Only call if URL bar is present.
          if (highlight_urlbar) {
            ub.setAttribute("security_level", level);
          } else {
            ub.removeAttribute("security_level");
          }
        }

        try { // URL bar may be hidden
          padlock_PadLock.setPadlockLevel("padlock-ib", level);
          padlock_PadLock.setPadlockLevel("padlock-ib-left", level);
          padlock_PadLock.setPadlockLevel("padlock-ub-right", level);
        } catch(e) {}
        padlock_PadLock.setPadlockLevel("padlock-sb", level);
        padlock_PadLock.setPadlockLevel("padlock-tab", level);
    },
  setPadlockLevel: function(item, level) {
    let secbut = document.getElementById(item);
    var sectooltip = "";
    
    if (level) {
      secbut.setAttribute("level", level);
      secbut.hidden = false;
    } else {
      secbut.hidden = true;
      secbut.removeAttribute("level");
    }
    
    switch (level) {
      case "ev":
        sectooltip = "Extended Validated";
        break;
      case "high":
        sectooltip = "Secure";
        break;
      case "low":
        sectooltip = "Weak security";
        break;
      case "mixed":
        sectooltip = "Mixed mode (partially encrypted)";
        break;
      case "broken":
        sectooltip = "Not secure";
        break;
      default:
        sectooltip = "";
    }
    secbut.setAttribute("tooltiptext", sectooltip);
  },
  prefbranch : null,
  onLoad: function() {
    gBrowser.addProgressListener(padlock_PadLock);
    
    var prefService = Components.classes["@mozilla.org/preferences-service;1"].getService(Components.interfaces.nsIPrefService);
    padlock_PadLock.prefbranch = prefService.getBranch("browser.padlock.");
    padlock_PadLock.prefbranch.QueryInterface(Components.interfaces.nsIPrefBranch2);
    padlock_PadLock.usePrefs();
    padlock_PadLock.prefbranch.addObserver("", padlock_PadLock, false);
  },
  onUnLoad: function() {
    padlock_PadLock.prefbranch.removeObserver("", padlock_PadLock);
  },
  observe: function(subject, topic, data)
  {
    if (topic != "nsPref:changed")
      return;
    if (data != "style" && data != "urlbar_background" && data != "shown")
      return;
    padlock_PadLock.usePrefs();
  },
  usePrefs: function() {
    var prefval = padlock_PadLock.prefbranch.getIntPref("style");
    var position;
    var padstyle;
    if (prefval == 2) {
      position = "ib-left";
      padstyle = "modern";
    }
    else if (prefval == 3) {
      position = "ub-right";
      padstyle = "modern";
    }
    else if (prefval == 4) {
      position = "statbar";
      padstyle = "modern";
    }
    else if (prefval == 5) {
      position = "tabs-bar";
      padstyle = "modern";
    }
    else if (prefval == 6) {
      position = "ib-trans-bg";
      padstyle = "classic";
    }
    else if (prefval == 7) {
      position = "ib-left";
      padstyle = "classic";
    }
    else if (prefval == 8) {
      position = "ub-right";
      padstyle = "classic";
    }
    else if (prefval == 9) {
      position = "statbar";
      padstyle = "classic";
    }
    else if (prefval == 10) {
      position = "tabs-bar";
      padstyle = "classic";
    }
    else { // 1 or anything else_ default
      position = "ib-trans-bg";
      padstyle = "modern";
    }

    var colshow;
    var colprefval = padlock_PadLock.prefbranch.getIntPref("urlbar_background");
    switch (colprefval) {
      case 3: 
        colshow = "all";
        break;
      case 2: 
        colshow = "secure-mixed";
        break;
      case 1: 
        colshow = "secure-only";
        break;
      default: 
        colshow = ""; // 0 or anything else: no shading
    }
    try { // URL bar may be hidden
      document.getElementById("urlbar").setAttribute("https_color", colshow);
    } catch(e) {}

    var lockenabled = padlock_PadLock.prefbranch.getBoolPref("shown");
    var padshow = "";
    if (lockenabled) {
      padshow = position;
    }

    try { // URL bar may be hidden
      document.getElementById("padlock-ib").setAttribute("padshow", padshow);
      document.getElementById("padlock-ib-left").setAttribute("padshow", padshow);
      document.getElementById("padlock-ub-right").setAttribute("padshow", padshow);
    } catch(e) {}
    document.getElementById("padlock-sb").setAttribute("padshow", padshow);
    document.getElementById("padlock-tab").setAttribute("padshow", padshow);

    try { // URL bar may be hidden
      document.getElementById("padlock-ib").setAttribute("padstyle", padstyle);
      document.getElementById("padlock-ib-left").setAttribute("padstyle", padstyle);
      document.getElementById("padlock-ub-right").setAttribute("padstyle", padstyle);
    } catch(e) {}
    document.getElementById("padlock-sb").setAttribute("padstyle", padstyle);
    document.getElementById("padlock-tab").setAttribute("padstyle", padstyle);

  }
};

window.addEventListener("load", padlock_PadLock.onLoad, false );
window.addEventListener("unload", padlock_PadLock.onUnLoad, false );

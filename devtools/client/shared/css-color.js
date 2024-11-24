/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {Cc, Ci} = require("chrome");
const Services = require("Services");

const {cssColors} = require("devtools/client/shared/css-color-db");

const COLOR_UNIT_PREF = "devtools.defaultColorUnit";

const SPECIALVALUES = new Set([
  "currentcolor",
  "initial",
  "inherit",
  "transparent",
  "unset"
]);

/**
 * This module is used to convert between various color types.
 *
 * Usage:
 *   let {require} = Cu.import("resource://devtools/shared/Loader.jsm", {});
 *   let {colorUtils} = require("devtools/shared/css-color");
 *   let color = new colorUtils.CssColor("red");
 *
 *   color.authored === "red"
 *   color.hasAlpha === false
 *   color.valid === true
 *   color.transparent === false // transparent has a special status.
 *   color.name === "red"        // returns hex or rgba when no name available.
 *   color.hex === "#f00"        // returns shortHex when available else returns
 *                                  longHex. If alpha channel is present then we
 *                                  return this.rgba.
 *   color.longHex === "#ff0000" // If alpha channel is present then we return
 *                                  this.rgba.
 *   color.rgb === "rgb(255, 0, 0)" // If alpha channel is present
 *                                  // then we return this.rgba.
 *   color.rgba === "rgba(255, 0, 0, 1)"
 *   color.hsl === "hsl(0, 100%, 50%)"
 *   color.hsla === "hsla(0, 100%, 50%, 1)" // If alpha channel is present
 *                                             then we return this.rgba.
 *
 *   color.toString() === "#f00"; // Outputs the color type determined in the
 *                                   COLOR_UNIT_PREF constant (above).
 *   // Color objects can be reused
 *   color.newColor("green") === "#0f0"; // true
 *
 *   Valid values for COLOR_UNIT_PREF are contained in CssColor.COLORUNIT.
 */

function CssColor(colorValue) {
  this.newColor(colorValue);
}

module.exports.colorUtils = {
  CssColor: CssColor,
  rgbToHsl: rgbToHsl,
  setAlpha: setAlpha,
  classifyColor: classifyColor,
  rgbToColorName: rgbToColorName,
  colorToRGBA: colorToRGBA,
  isValidCSSColor: isValidCSSColor,
};

/**
 * Values used in COLOR_UNIT_PREF
 */
CssColor.COLORUNIT = {
  "authored": "authored",
  "hex": "hex",
  "name": "name",
  "rgb": "rgb",
  "hsl": "hsl"
};

CssColor.prototype = {
  _colorUnit: null,
  _colorUnitUppercase: false,

  // The value as-authored.
  authored: null,
  // A lower-cased copy of |authored|.
  lowerCased: null,

  get colorUnit() {
    if (this._colorUnit === null) {
      let defaultUnit = Services.prefs.getCharPref(COLOR_UNIT_PREF);
      this._colorUnit = CssColor.COLORUNIT[defaultUnit];
      this._colorUnitUppercase =
        (this.authored === this.authored.toUpperCase());
    }
    return this._colorUnit;
  },

  set colorUnit(unit) {
    this._colorUnit = unit;
  },

  /**
   * If the current color unit pref is "authored", then set the
   * default color unit from the given color.  Otherwise, leave the
   * color unit untouched.
   *
   * @param {String} color The color to use
   */
  setAuthoredUnitFromColor: function (color) {
    if (Services.prefs.getCharPref(COLOR_UNIT_PREF) ===
        CssColor.COLORUNIT.authored) {
      this._colorUnit = classifyColor(color);
      this._colorUnitUppercase = (color === color.toUpperCase());
    }
  },

  get hasAlpha() {
    if (!this.valid) {
      return false;
    }
    return this._getRGBATuple().a !== 1;
  },

  get valid() {
    return isValidCSSColor(this.authored);
  },

  /**
   * Return true for all transparent values e.g. rgba(0, 0, 0, 0).
   */
  get transparent() {
    try {
      let tuple = this._getRGBATuple();
      return !(tuple.r || tuple.g || tuple.b || tuple.a);
    } catch (e) {
      return false;
    }
  },

  get specialValue() {
    return SPECIALVALUES.has(this.lowerCased) ? this.authored : null;
  },

  get name() {
    let invalidOrSpecialValue = this._getInvalidOrSpecialValue();
    if (invalidOrSpecialValue !== false) {
      return invalidOrSpecialValue;
    }

    try {
      let tuple = this._getRGBATuple();

      if (tuple.a !== 1) {
        return this.rgb;
      }
      let {r, g, b} = tuple;
      return rgbToColorName(r, g, b);
    } catch (e) {
      return this.hex;
    }
  },

  get hex() {
    let invalidOrSpecialValue = this._getInvalidOrSpecialValue();
    if (invalidOrSpecialValue !== false) {
      return invalidOrSpecialValue;
    }
    if (this.hasAlpha) {
      return this.rgba;
    }

    let hex = this.longHex;
    if (hex.charAt(1) == hex.charAt(2) &&
        hex.charAt(3) == hex.charAt(4) &&
        hex.charAt(5) == hex.charAt(6)) {
      hex = "#" + hex.charAt(1) + hex.charAt(3) + hex.charAt(5);
    }
    return hex;
  },

  get longHex() {
    let invalidOrSpecialValue = this._getInvalidOrSpecialValue();
    if (invalidOrSpecialValue !== false) {
      return invalidOrSpecialValue;
    }
    if (this.hasAlpha) {
      return this.rgba;
    }

    let tuple = this._getRGBATuple();
    return "#" + ((1 << 24) + (tuple.r << 16) + (tuple.g << 8) +
                  (tuple.b << 0)).toString(16).substr(-6);
  },

  get rgb() {
    let invalidOrSpecialValue = this._getInvalidOrSpecialValue();
    if (invalidOrSpecialValue !== false) {
      return invalidOrSpecialValue;
    }
    if (!this.hasAlpha) {
      if (this.lowerCased.startsWith("rgb(")) {
        // The color is valid and begins with rgb(.
        return this.authored;
      }
      let tuple = this._getRGBATuple();
      return "rgb(" + tuple.r + ", " + tuple.g + ", " + tuple.b + ")";
    }
    return this.rgba;
  },

  get rgba() {
    let invalidOrSpecialValue = this._getInvalidOrSpecialValue();
    if (invalidOrSpecialValue !== false) {
      return invalidOrSpecialValue;
    }
    if (this.lowerCased.startsWith("rgba(")) {
      // The color is valid and begins with rgba(.
      return this.authored;
    }
    let components = this._getRGBATuple();
    return "rgba(" + components.r + ", " +
                     components.g + ", " +
                     components.b + ", " +
                     components.a + ")";
  },

  get hsl() {
    let invalidOrSpecialValue = this._getInvalidOrSpecialValue();
    if (invalidOrSpecialValue !== false) {
      return invalidOrSpecialValue;
    }
    if (this.lowerCased.startsWith("hsl(")) {
      // The color is valid and begins with hsl(.
      return this.authored;
    }
    if (this.hasAlpha) {
      return this.hsla;
    }
    return this._hsl();
  },

  get hsla() {
    let invalidOrSpecialValue = this._getInvalidOrSpecialValue();
    if (invalidOrSpecialValue !== false) {
      return invalidOrSpecialValue;
    }
    if (this.lowerCased.startsWith("hsla(")) {
      // The color is valid and begins with hsla(.
      return this.authored;
    }
    if (this.hasAlpha) {
      let a = this._getRGBATuple().a;
      return this._hsl(a);
    }
    return this._hsl(1);
  },

  /**
   * Check whether the current color value is in the special list e.g.
   * transparent or invalid.
   *
   * @return {String|Boolean}
   *         - If the current color is a special value e.g. "transparent" then
   *           return the color.
   *         - If the color is invalid return an empty string.
   *         - If the color is a regular color e.g. #F06 so we return false
   *           to indicate that the color is neither invalid or special.
   */
  _getInvalidOrSpecialValue: function () {
    if (this.specialValue) {
      return this.specialValue;
    }
    if (!this.valid) {
      return "";
    }
    return false;
  },

  /**
   * Change color
   *
   * @param  {String} color
   *         Any valid color string
   */
  newColor: function (color) {
    // Store a lower-cased version of the color to help with format
    // testing.  The original text is kept as well so it can be
    // returned when needed.
    this.lowerCased = color.toLowerCase();
    this.authored = color;
    return this;
  },

  nextColorUnit: function () {
    // Reorder the formats array to have the current format at the
    // front so we can cycle through.
    let formats = ["hex", "hsl", "rgb", "name"];
    let currentFormat = classifyColor(this.toString());
    let putOnEnd = formats.splice(0, formats.indexOf(currentFormat));
    formats = formats.concat(putOnEnd);
    let currentDisplayedColor = this[formats[0]];

    for (let format of formats) {
      if (this[format].toLowerCase() !== currentDisplayedColor.toLowerCase()) {
        this.colorUnit = CssColor.COLORUNIT[format];
        break;
      }
    }

    return this.toString();
  },

  /**
   * Return a string representing a color of type defined in COLOR_UNIT_PREF.
   */
  toString: function () {
    let color;

    switch (this.colorUnit) {
      case CssColor.COLORUNIT.authored:
        color = this.authored;
        break;
      case CssColor.COLORUNIT.hex:
        color = this.hex;
        break;
      case CssColor.COLORUNIT.hsl:
        color = this.hsl;
        break;
      case CssColor.COLORUNIT.name:
        color = this.name;
        break;
      case CssColor.COLORUNIT.rgb:
        color = this.rgb;
        break;
      default:
        color = this.rgb;
    }

    if (this._colorUnitUppercase &&
        this.colorUnit != CssColor.COLORUNIT.authored) {
      color = color.toUpperCase();
    }

    return color;
  },

  /**
   * Returns a RGBA 4-Tuple representation of a color or transparent as
   * appropriate.
   */
  _getRGBATuple: function () {
    let tuple = DOMUtils.colorToRGBA(this.authored);

    tuple.a = parseFloat(tuple.a.toFixed(1));

    return tuple;
  },

  _hsl: function (maybeAlpha) {
    if (this.lowerCased.startsWith("hsl(") && maybeAlpha === undefined) {
      // We can use it as-is.
      return this.authored;
    }

    let {r, g, b} = this._getRGBATuple();
    let [h, s, l] = rgbToHsl([r, g, b]);
    if (maybeAlpha !== undefined) {
      return "hsla(" + h + ", " + s + "%, " + l + "%, " + maybeAlpha + ")";
    }
    return "hsl(" + h + ", " + s + "%, " + l + "%)";
  },

  /**
   * This method allows comparison of CssColor objects using ===.
   */
  valueOf: function () {
    return this.rgba;
  },
};

/**
 * Convert rgb value to hsl
 *
 * @param {array} rgb
 *         Array of rgb values
 * @return {array}
 *         Array of hsl values.
 */
function rgbToHsl([r, g, b]) {
  r = r / 255;
  g = g / 255;
  b = b / 255;

  let max = Math.max(r, g, b);
  let min = Math.min(r, g, b);
  let h;
  let s;
  let l = (max + min) / 2;

  if (max == min) {
    h = s = 0;
  } else {
    let d = max - min;
    s = l > 0.5 ? d / (2 - max - min) : d / (max + min);

    switch (max) {
      case r:
        h = ((g - b) / d) % 6;
        break;
      case g:
        h = (b - r) / d + 2;
        break;
      case b:
        h = (r - g) / d + 4;
        break;
    }
    h *= 60;
    if (h < 0) {
      h += 360;
    }
  }

  return [Math.round(h), Math.round(s * 100), Math.round(l * 100)];
}

/**
 * Takes a color value of any type (hex, hsl, hsla, rgb, rgba)
 * and an alpha value to generate an rgba string with the correct
 * alpha value.
 *
 * @param  {String} colorValue
 *         Color in the form of hex, hsl, hsla, rgb, rgba.
 * @param  {Number} alpha
 *         Alpha value for the color, between 0 and 1.
 * @return {String}
 *         Converted color with `alpha` value in rgba form.
 */
function setAlpha(colorValue, alpha) {
  let color = new CssColor(colorValue);

  // Throw if the color supplied is not valid.
  if (!color.valid) {
    throw new Error("Invalid color.");
  }

  // If an invalid alpha valid, just set to 1.
  if (!(alpha >= 0 && alpha <= 1)) {
    alpha = 1;
  }

  let { r, g, b } = color._getRGBATuple();
  return "rgba(" + r + ", " + g + ", " + b + ", " + alpha + ")";
}

/**
 * Given a color, classify its type as one of the possible color
 * units, as known by |CssColor.colorUnit|.
 *
 * @param  {String} value
 *         The color, in any form accepted by CSS.
 * @return {String}
 *         The color classification, one of "rgb", "hsl", "hex", or "name".
 */
function classifyColor(value) {
  value = value.toLowerCase();
  if (value.startsWith("rgb(") || value.startsWith("rgba(")) {
    return CssColor.COLORUNIT.rgb;
  } else if (value.startsWith("hsl(") || value.startsWith("hsla(")) {
    return CssColor.COLORUNIT.hsl;
  } else if (/^#[0-9a-f]+$/.exec(value)) {
    return CssColor.COLORUNIT.hex;
  }
  return CssColor.COLORUNIT.name;
}

// This holds a map from colors back to color names for use by
// rgbToColorName.
var cssRGBMap;

/**
 * Given a color, return its name, if it has one.  Throws an exception
 * if the color does not have a name.
 *
 * @param {Number} r, g, b  The color components.
 * @return {String} the name of the color
 */
function rgbToColorName(r, g, b) {
  if (!cssRGBMap) {
    cssRGBMap = {};
    for (let name in cssColors) {
      let key = JSON.stringify(cssColors[name]);
      if (!(key in cssRGBMap)) {
        cssRGBMap[key] = name;
      }
    }
  }
  let value = cssRGBMap[JSON.stringify([r, g, b, 1])];
  if (!value) {
    throw new Error("no such color");
  }
  return value;
}

// Originally from dom/tests/mochitest/ajax/mochikit/MochiKit/Color.js.
function _hslValue(n1, n2, hue) {
  if (hue > 6.0) {
    hue -= 6.0;
  } else if (hue < 0.0) {
    hue += 6.0;
  }
  let val;
  if (hue < 1.0) {
    val = n1 + (n2 - n1) * hue;
  } else if (hue < 3.0) {
    val = n2;
  } else if (hue < 4.0) {
    val = n1 + (n2 - n1) * (4.0 - hue);
  } else {
    val = n1;
  }
  return val;
}

// Originally from dom/tests/mochitest/ajax/mochikit/MochiKit/Color.js.
function hslToRGB([hue, saturation, lightness]) {
  let red;
  let green;
  let blue;
  if (saturation === 0) {
    red = lightness;
    green = lightness;
    blue = lightness;
  } else {
    let m2;
    if (lightness <= 0.5) {
      m2 = lightness * (1.0 + saturation);
    } else {
      m2 = lightness + saturation - (lightness * saturation);
    }
    let m1 = (2.0 * lightness) - m2;
    let f = _hslValue;
    let h6 = hue * 6.0;
    red = f(m1, m2, h6 + 2);
    green = f(m1, m2, h6);
    blue = f(m1, m2, h6 - 2);
  }
  return [red, green, blue];
}

/**
 * A helper function to convert a hex string like "F0C" to a color.
 *
 * @param {String} name the color string
 * @return {Object} an object of the form {r, g, b, a}; or null if the
 *         name was not a valid color
 */
function hexToRGBA(name) {
  let r, g, b;

  if (name.length === 3) {
    let val = parseInt(name, 16);
    b = ((val & 15) << 4) + (val & 15);
    val >>= 4;
    g = ((val & 15) << 4) + (val & 15);
    val >>= 4;
    r = ((val & 15) << 4) + (val & 15);
  } else if (name.length === 6) {
    let val = parseInt(name, 16);
    b = val & 255;
    val >>= 8;
    g = val & 255;
    val >>= 8;
    r = val & 255;
  } else {
    return null;
  }

  return {r, g, b, a: 1};
}

/**
 * A helper function to clamp a value.
 *
 * @param {Number} value The value to clamp
 * @param {Number} min The minimum value
 * @param {Number} max The maximum value
 * @return {Number} A value between min and max
 */
function clamp(value, min, max) {
  if (value < min) {
    value = min;
  }
  if (value > max) {
    value = max;
  }
  return value;
}

/**
 * A helper function to get a token from a lexer, skipping comments
 * and whitespace.
 *
 * @param {CSSLexer} lexer The lexer
 * @return {CSSToken} The next non-whitespace, non-comment token; or
 * null at EOF.
 */
function getToken(lexer) {
  while (true) {
    let token = lexer.nextToken();
    if (!token || (token.tokenType !== "comment" &&
                   token.tokenType !== "whitespace")) {
      return token;
    }
  }
}

/**
 * A helper function to examine a token and ensure it is a comma.
 * Then fetch and return the next token.  Returns null if the
 * token was not a comma, or at EOF.
 *
 * @param {CSSLexer} lexer The lexer
 * @param {CSSToken} token A token to be examined
 * @return {CSSToken} The next non-whitespace, non-comment token; or
 * null if token was not a comma, or at EOF.
 */
function requireComma(lexer, token) {
  if (!token || token.tokenType !== "symbol" || token.text !== ",") {
    return null;
  }
  return getToken(lexer);
}

/**
 * A helper function to parse the first three arguments to hsl()
 * or hsla().
 *
 * @param {CSSLexer} lexer The lexer
 * @return {Array} An array of the form [r,g,b]; or null on error.
 */
function parseHsl(lexer) {
  let vals = [];

  let token = getToken(lexer);
  if (!token || token.tokenType !== "number") {
    return null;
  }
  let val = token.number % 60;
  if (val < 0) {
    val += 60;
  }
  vals.push(val / 60.0);

  for (let i = 0; i < 2; ++i) {
    token = requireComma(lexer, getToken(lexer));
    if (!token || token.tokenType !== "percentage") {
      return null;
    }
    vals.push(clamp(token.number, 0, 100));
  }

  return hslToRGB(vals).map((elt) => Math.trunc(elt * 255));
}

/**
 * A helper function to parse the first three arguments to rgb()
 * or rgba().
 *
 * @param {CSSLexer} lexer The lexer
 * @return {Array} An array of the form [r,g,b]; or null on error.
 */
function parseRgb(lexer) {
  let isPercentage = false;
  let vals = [];
  for (let i = 0; i < 3; ++i) {
    let token = getToken(lexer);
    if (i > 0) {
      token = requireComma(lexer, token);
    }
    if (!token) {
      return null;
    }

    /* Either all parameters are integers, or all are percentages, so
       check the first one to see.  */
    if (i === 0 && token.tokenType === "percentage") {
      isPercentage = true;
    }

    if (isPercentage) {
      if (token.tokenType !== "percentage") {
        return null;
      }
      vals.push(Math.round(255 * clamp(token.number, 0, 100)));
    } else {
      if (token.tokenType !== "number" || !token.isInteger) {
        return null;
      }
      vals.push(clamp(token.number, 0, 255));
    }
  }
  return vals;
}

/**
 * Convert a string representing a color to an object holding the
 * color's components.  Any valid CSS color form can be passed in.
 *
 * @param {String} name the color
 * @return {Object} an object of the form {r, g, b, a}; or null if the
 *         name was not a valid color
 */
function colorToRGBA(name) {
  name = name.trim().toLowerCase();

  if (name in cssColors) {
    let result = cssColors[name];
    return {r: result[0], g: result[1], b: result[2], a: result[3]};
  } else if (name === "transparent") {
    return {r: 0, g: 0, b: 0, a: 0};
  } else if (name === "currentcolor") {
    return {r: 0, g: 0, b: 0, a: 1};
  }

  let lexer = DOMUtils.getCSSLexer(name);

  let func = getToken(lexer);
  if (!func) {
    return null;
  }

  if (func.tokenType === "id" || func.tokenType === "hash") {
    if (getToken(lexer) !== null) {
      return null;
    }
    return hexToRGBA(func.text);
  }

  const expectedFunctions = ["rgba", "rgb", "hsla", "hsl"];
  if (!func || func.tokenType !== "function" ||
      !expectedFunctions.includes(func.text)) {
    return null;
  }

  let hsl = func.text === "hsl" || func.text === "hsla";
  let alpha = func.text === "rgba" || func.text === "hsla";

  let vals = hsl ? parseHsl(lexer) : parseRgb(lexer);
  if (!vals) {
    return null;
  }

  if (alpha) {
    let token = requireComma(lexer, getToken(lexer));
    if (!token || token.tokenType !== "number") {
      return null;
    }
    vals.push(clamp(token.number, 0, 1));
  } else {
    vals.push(1);
  }

  let parenToken = getToken(lexer);
  if (!parenToken || parenToken.tokenType !== "symbol" ||
      parenToken.text !== ")") {
    return null;
  }
  if (getToken(lexer) !== null) {
    return null;
  }

  return {r: vals[0], g: vals[1], b: vals[2], a: vals[3]};
}

/**
 * Check whether a string names a valid CSS color.
 *
 * @param {String} name The string to check
 * @return {Boolean} True if the string is a CSS color name.
 */
function isValidCSSColor(name) {
  return colorToRGBA(name) !== null;
}

loader.lazyGetter(this, "DOMUtils", function () {
  return Cc["@mozilla.org/inspector/dom-utils;1"].getService(Ci.inIDOMUtils);
});

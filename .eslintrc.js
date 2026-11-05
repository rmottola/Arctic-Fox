"use strict";

module.exports = {
  // When adding items to this file please check for effects on sub-directories.
  "plugins": [
    "mozilla"
  ],
  "rules": {
    "mozilla/avoid-removeChild": "error",
    "mozilla/import-globals": "warn",
    "mozilla/no-useless-parameters": "error",
  },
  "env": {
    "es6": true
  },
};

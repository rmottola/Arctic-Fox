/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * PMkit shim for 'sdk/ui/button', (c) JustOff, 2017 */
'use strict';

module.metadata = {
  'stability': 'experimental',
  'engines': {
    'Firefox': '> 27'
  }
};

const { Cu } = require('chrome');
const { on, off, emit } = require('../../event/core');

const { data } = require('sdk/self');

const { isObject, isNil } = require('../../lang/type');

const { getMostRecentBrowserWindow } = require('../../window/utils');
const { ignoreWindow } = require('../../private-browsing/utils');
const { buttons } = require('../buttons');

const { events: viewEvents } = require('./view/events');

const XUL_NS = 'http://www.mozilla.org/keymaster/gatekeeper/there.is.only.xul';

const views = new Map();

function getNode(id, window) {
  return !views.has(id) || ignoreWindow(window)
    ? null
    : buttons.getNode(id, window);
};

function getImage(icon, isInToolbar, pixelRatio) {
  let targetSize = (isInToolbar ? 18 : 32) * pixelRatio;
  let bestSize = 0;
  let image = icon;

  if (isObject(icon)) {
    for (let size of Object.keys(icon)) {
      size = +size;
      let offset = targetSize - size;

      if (offset === 0) {
        bestSize = size;
        break;
      }

      let delta = Math.abs(offset) - Math.abs(targetSize - bestSize);

      if (delta < 0)
        bestSize = size;
    }

    image = icon[bestSize];
  }

  if (image.indexOf('./') === 0)
    return data.url(image.substr(2));

  return image;
}

function nodeFor(id, window=getMostRecentBrowserWindow()) {
  return getNode(id, window);
};
exports.nodeFor = nodeFor;

function create(options) {
  let { id, label, icon, type, badge } = options;

  if (views.has(id))
    throw new Error('The ID "' + id + '" seems already used.');

  buttons.createButton({
    id: id,

    onBuild: function(document, _id) {
      let window = document.defaultView;

      let node = document.createElementNS(XUL_NS, 'toolbarbutton');

      let image = getImage(icon, true, window.devicePixelRatio);

      if (ignoreWindow(window))
        node.style.display = 'none';

      node.setAttribute('id', _id);
      node.setAttribute('class', 'toolbarbutton-1 chromeclass-toolbar-additional badged-button');
      node.setAttribute('type', type);
      node.setAttribute('label', label);
      node.setAttribute('tooltiptext', label);
      node.setAttribute('image', image);
      node.setAttribute('pmkit-button', 'true');

      views.set(id, {
        icon: icon,
        label: label
      });

      node.addEventListener('command', function(event) {
        if (views.has(id)) {
          emit(viewEvents, 'data', {
            type: 'click',
            target: id,
            window: event.view,
            checked: node.checked
          });
        }
      });

      return node;
    }
  });
};
exports.create = create;

function dispose(id) {
  if (!views.has(id)) return;

  views.delete(id);
  buttons.destroyButton(id);
}
exports.dispose = dispose;

function setIcon(id, window, icon) {
  let node = getNode(id, window);

  if (node) {
    let image = getImage(icon, true, window.devicePixelRatio);

    node.setAttribute('image', image);
  }
}
exports.setIcon = setIcon;

function setLabel(id, window, label) {
  let node = nodeFor(id, window);

  if (node) {
    node.setAttribute('label', label);
    node.setAttribute('tooltiptext', label);
  }
}
exports.setLabel = setLabel;

function setDisabled(id, window, disabled) {
  let node = nodeFor(id, window);

  if (node)
    node.disabled = disabled;
}
exports.setDisabled = setDisabled;

function setChecked(id, window, checked) {
  let node = nodeFor(id, window);

  if (node)
    node.checked = checked;
}
exports.setChecked = setChecked;

function setBadge(id, window, badge, color) {
  let node = nodeFor(id, window);

  if (node) {
    // `Array.from` is needed to handle unicode symbol properly:
    // '𝐀𝐁'.length is 4 where Array.from('𝐀𝐁').length is 2
    let text = isNil(badge)
                  ? ''
                  : Array.from(String(badge)).slice(0, 4).join('');

    node.setAttribute('badge', text);

    let badgeNode = node.ownerDocument.getAnonymousElementByAttribute(node,
                                        'class', 'toolbarbutton-badge');

    if (badgeNode)
      badgeNode.style.backgroundColor = isNil(color) ? '' : color;
  }
}
exports.setBadge = setBadge;

function click(id) {
  let node = nodeFor(id);

  if (node)
    node.click();
}
exports.click = click;

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

const Services = require("Services");
const { Ci } = require("chrome");
const { ShortLongString } = require("devtools/server/actors/string");
const {
  Front,
  FrontClassWithSpec,
  custom,
  types
} = require("devtools/shared/protocol.js");
const { makeInfallible } = require("devtools/shared/DevToolsUtils");
const { nodeSpec } = require("devtools/shared/specs/inspector");
const promise = require("promise");
const { Class } = require("sdk/core/heritage");
const object = require("sdk/util/object");

const HIDDEN_CLASS = "__fx-devtools-hide-shortcut__";

/**
 * Convenience API for building a list of attribute modifications
 * for the `modifyAttributes` request.
 */
const AttributeModificationList = Class({
  initialize: function (node) {
    this.node = node;
    this.modifications = [];
  },

  apply: function () {
    let ret = this.node.modifyAttributes(this.modifications);
    return ret;
  },

  destroy: function () {
    this.node = null;
    this.modification = null;
  },

  setAttributeNS: function (ns, name, value) {
    this.modifications.push({
      attributeNamespace: ns,
      attributeName: name,
      newValue: value
    });
  },

  setAttribute: function (name, value) {
    this.setAttributeNS(undefined, name, value);
  },

  removeAttributeNS: function (ns, name) {
    this.setAttributeNS(ns, name, undefined);
  },

  removeAttribute: function (name) {
    this.setAttributeNS(undefined, name, undefined);
  }
});

// A resolve that hits the main loop first.
function delayedResolve(value) {
  let deferred = promise.defer();
  Services.tm.mainThread.dispatch(makeInfallible(() => {
    deferred.resolve(value);
  }), 0);
  return deferred.promise;
}

/**
 * Client side of the node actor.
 *
 * Node fronts are strored in a tree that mirrors the DOM tree on the
 * server, but with a few key differences:
 *  - Not all children will be necessary loaded for each node.
 *  - The order of children isn't guaranteed to be the same as the DOM.
 * Children are stored in a doubly-linked list, to make addition/removal
 * and traversal quick.
 *
 * Due to the order/incompleteness of the child list, it is safe to use
 * the parent node from clients, but the `children` request should be used
 * to traverse children.
 */
const NodeFront = FrontClassWithSpec(nodeSpec, {
  initialize: function (conn, form, detail, ctx) {
    // The parent node
    this._parent = null;
    // The first child of this node.
    this._child = null;
    // The next sibling of this node.
    this._next = null;
    // The previous sibling of this node.
    this._prev = null;
    Front.prototype.initialize.call(this, conn, form, detail, ctx);
  },

  /**
   * Destroy a node front.  The node must have been removed from the
   * ownership tree before this is called, unless the whole walker front
   * is being destroyed.
   */
  destroy: function () {
    Front.prototype.destroy.call(this);
  },

  // Update the object given a form representation off the wire.
  form: function (form, detail, ctx) {
    if (detail === "actorid") {
      this.actorID = form;
      return;
    }
    // Shallow copy of the form.  We could just store a reference, but
    // eventually we'll want to update some of the data.
    this._form = object.merge(form);
    this._form.attrs = this._form.attrs ? this._form.attrs.slice() : [];

    if (form.parent) {
      // Get the owner actor for this actor (the walker), and find the
      // parent node of this actor from it, creating a standin node if
      // necessary.
      let parentNodeFront = ctx.marshallPool().ensureParentFront(form.parent);
      this.reparent(parentNodeFront);
    }

    if (form.singleTextChild) {
      this.singleTextChild =
        types.getType("domnode").read(form.singleTextChild, ctx);
    } else {
      this.singleTextChild = undefined;
    }
  },

  /**
   * Returns the parent NodeFront for this NodeFront.
   */
  parentNode: function () {
    return this._parent;
  },

  /**
   * Process a mutation entry as returned from the walker's `getMutations`
   * request.  Only tries to handle changes of the node's contents
   * themselves (character data and attribute changes), the walker itself
   * will keep the ownership tree up to date.
   */
  updateMutation: function (change) {
    if (change.type === "attributes") {
      // We'll need to lazily reparse the attributes after this change.
      this._attrMap = undefined;

      // Update any already-existing attributes.
      let found = false;
      for (let i = 0; i < this.attributes.length; i++) {
        let attr = this.attributes[i];
        if (attr.name == change.attributeName &&
            attr.namespace == change.attributeNamespace) {
          if (change.newValue !== null) {
            attr.value = change.newValue;
          } else {
            this.attributes.splice(i, 1);
          }
          found = true;
          break;
        }
      }
      // This is a new attribute. The null check is because of Bug 1192270,
      // in the case of a newly added then removed attribute
      if (!found && change.newValue !== null) {
        this.attributes.push({
          name: change.attributeName,
          namespace: change.attributeNamespace,
          value: change.newValue
        });
      }
    } else if (change.type === "characterData") {
      this._form.shortValue = change.newValue;
      this._form.incompleteValue = change.incompleteValue;
    } else if (change.type === "pseudoClassLock") {
      this._form.pseudoClassLocks = change.pseudoClassLocks;
    } else if (change.type === "events") {
      this._form.hasEventListeners = change.hasEventListeners;
    }
  },

  // Some accessors to make NodeFront feel more like an nsIDOMNode

  get id() {
    return this.getAttribute("id");
  },

  get nodeType() {
    return this._form.nodeType;
  },
  get namespaceURI() {
    return this._form.namespaceURI;
  },
  get nodeName() {
    return this._form.nodeName;
  },
  get doctypeString() {
    return "<!DOCTYPE " + this._form.name +
     (this._form.publicId ? " PUBLIC \"" + this._form.publicId + "\"" : "") +
     (this._form.systemId ? " \"" + this._form.systemId + "\"" : "") +
     ">";
  },

  get baseURI() {
    return this._form.baseURI;
  },

  get className() {
    return this.getAttribute("class") || "";
  },

  get hasChildren() {
    return this._form.numChildren > 0;
  },
  get numChildren() {
    return this._form.numChildren;
  },
  get hasEventListeners() {
    return this._form.hasEventListeners;
  },

  get isBeforePseudoElement() {
    return this._form.isBeforePseudoElement;
  },
  get isAfterPseudoElement() {
    return this._form.isAfterPseudoElement;
  },
  get isPseudoElement() {
    return this.isBeforePseudoElement || this.isAfterPseudoElement;
  },
  get isAnonymous() {
    return this._form.isAnonymous;
  },
  get isInHTMLDocument() {
    return this._form.isInHTMLDocument;
  },
  get tagName() {
    return this.nodeType === Ci.nsIDOMNode.ELEMENT_NODE ? this.nodeName : null;
  },
  get shortValue() {
    return this._form.shortValue;
  },
  get incompleteValue() {
    return !!this._form.incompleteValue;
  },

  get isDocumentElement() {
    return !!this._form.isDocumentElement;
  },

  // doctype properties
  get name() {
    return this._form.name;
  },
  get publicId() {
    return this._form.publicId;
  },
  get systemId() {
    return this._form.systemId;
  },

  getAttribute: function (name) {
    let attr = this._getAttribute(name);
    return attr ? attr.value : null;
  },
  hasAttribute: function (name) {
    this._cacheAttributes();
    return (name in this._attrMap);
  },

  get hidden() {
    let cls = this.getAttribute("class");
    return cls && cls.indexOf(HIDDEN_CLASS) > -1;
  },

  get attributes() {
    return this._form.attrs;
  },

  get pseudoClassLocks() {
    return this._form.pseudoClassLocks || [];
  },
  hasPseudoClassLock: function (pseudo) {
    return this.pseudoClassLocks.some(locked => locked === pseudo);
  },

  get isDisplayed() {
    // The NodeActor's form contains the isDisplayed information as a boolean
    // starting from FF32. Before that, the property is missing
    return "isDisplayed" in this._form ? this._form.isDisplayed : true;
  },

  get isTreeDisplayed() {
    let parent = this;
    while (parent) {
      if (!parent.isDisplayed) {
        return false;
      }
      parent = parent.parentNode();
    }
    return true;
  },

  getNodeValue: custom(function () {
    if (!this.incompleteValue) {
      return delayedResolve(new ShortLongString(this.shortValue));
    }

    return this._getNodeValue();
  }, {
    impl: "_getNodeValue"
  }),

  // Accessors for custom form properties.

  getFormProperty: function (name) {
    return this._form.props ? this._form.props[name] : null;
  },

  hasFormProperty: function (name) {
    return this._form.props ? (name in this._form.props) : null;
  },

  get formProperties() {
    return this._form.props;
  },

  /**
   * Return a new AttributeModificationList for this node.
   */
  startModifyingAttributes: function () {
    return AttributeModificationList(this);
  },

  _cacheAttributes: function () {
    if (typeof this._attrMap != "undefined") {
      return;
    }
    this._attrMap = {};
    for (let attr of this.attributes) {
      this._attrMap[attr.name] = attr;
    }
  },

  _getAttribute: function (name) {
    this._cacheAttributes();
    return this._attrMap[name] || undefined;
  },

  /**
   * Set this node's parent.  Note that the children saved in
   * this tree are unordered and incomplete, so shouldn't be used
   * instead of a `children` request.
   */
  reparent: function (parent) {
    if (this._parent === parent) {
      return;
    }

    if (this._parent && this._parent._child === this) {
      this._parent._child = this._next;
    }
    if (this._prev) {
      this._prev._next = this._next;
    }
    if (this._next) {
      this._next._prev = this._prev;
    }
    this._next = null;
    this._prev = null;
    this._parent = parent;
    if (!parent) {
      // Subtree is disconnected, we're done
      return;
    }
    this._next = parent._child;
    if (this._next) {
      this._next._prev = this;
    }
    parent._child = this;
  },

  /**
   * Return all the known children of this node.
   */
  treeChildren: function () {
    let ret = [];
    for (let child = this._child; child != null; child = child._next) {
      ret.push(child);
    }
    return ret;
  },

  /**
   * Do we use a local target?
   * Useful to know if a rawNode is available or not.
   *
   * This will, one day, be removed. External code should
   * not need to know if the target is remote or not.
   */
  isLocalToBeDeprecated: function () {
    return !!this.conn._transport._serverConnection;
  },

  /**
   * Get an nsIDOMNode for the given node front.  This only works locally,
   * and is only intended as a stopgap during the transition to the remote
   * protocol.  If you depend on this you're likely to break soon.
   */
  rawNode: function (rawNode) {
    if (!this.conn._transport._serverConnection) {
      console.warn("Tried to use rawNode on a remote connection.");
      return null;
    }
    let actor = this.conn._transport._serverConnection.getActor(this.actorID);
    if (!actor) {
      // Can happen if we try to get the raw node for an already-expired
      // actor.
      return null;
    }
    return actor.rawNode;
  }
});

exports.NodeFront = NodeFront;

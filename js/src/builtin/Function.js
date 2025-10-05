/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// ES7 draft (January 21, 2016) 19.2.3.2 Function.prototype.bind
function FunctionBind(thisArg, ...boundArgs) {
    // Step 1.
    var target = this;
    // Step 2.
    if (!IsCallable(target))
        ThrowTypeError(JSMSG_INCOMPATIBLE_PROTO, 'Function', 'bind', target);

    // Step 3 (implicit).
    // Step 4.
    var F;
    var argCount = boundArgs.length;
    switch (argCount) {
      case 0:
        F = bind_bindFunction0(target, thisArg, boundArgs);
        break;
      case 1:
        F = bind_bindFunction1(target, thisArg, boundArgs);
        break;
      case 2:
        F = bind_bindFunction2(target, thisArg, boundArgs);
        break;
      default:
        F = bind_bindFunctionN(target, thisArg, boundArgs);
    }

    // Step 5.
    var targetHasLength = callFunction(std_Object_hasOwnProperty, target, "length");

    // Step 6.
    var L;
    if (targetHasLength) {
        // Step 6.a.
        var targetLen = target.length;
        // Step 6.b.
        if (typeof targetLen !== 'number') {
            L = 0;
        } else {
            // Steps 6.b.i-ii.
            L = std_Math_max(0, ToInteger(targetLen) - argCount);
        }
    } else {
        // Step 7.
        L = 0;
    }

    // Step 9.
    var targetName = target.name;

    // Step 10.
    if (typeof targetName !== "string")
        targetName = "";

    // 9.2.11 SetFunctionName, Step 5.a.
    targetName = "bound " + targetName;

    // Steps 10-11, 15-16.
    _FinishBoundFunctionInit(F, target, L, targetName);

    // Ensure that the apply intrinsic has been cloned so it can be baked into
    // JIT code.
    var funApply = std_Function_apply;

    // Step 12.
    return F;
}
/**
 * bind_bindFunction{0,1,2} are special cases of the generic bind_bindFunctionN
 * below. They avoid the need to merge the lists of bound arguments and call
 * arguments to the bound function into a new list which is then used in a
 * destructuring call of the bound function.
 *
 * All three of these functions again have special-cases for call argument
 * counts between 0 and 5. For calls with 6+ arguments, all - bound and call -
 * arguments are copied into an array before invoking the generic call and
 * construct helper functions. This avoids having to use rest parameters and
 * destructuring in the fast path.
 *
 * All bind_bindFunction{X} functions have the same signature to enable simple
 * reading out of closed-over state by debugging functions.
 */
function bind_bindFunction0(fun, thisArg, boundArgs) {
    return function bound() {
        var a = arguments;
        var newTarget;
        if (_IsConstructing()) {
            newTarget = new.target;
            if (newTarget === bound)
                newTarget = fun;
            switch (a.length) {
              case 0:
                return constructContentFunction(fun, newTarget);
              case 1:
                return constructContentFunction(fun, newTarget, a[0]);
              case 2:
                return constructContentFunction(fun, newTarget, a[0], a[1]);
              case 3:
                return constructContentFunction(fun, newTarget, a[0], a[1], a[2]);
              case 4:
                return constructContentFunction(fun, newTarget, a[0], a[1], a[2], a[3]);
              case 5:
                return constructContentFunction(fun, newTarget, a[0], a[1], a[2], a[3], a[4]);
            }
        } else {
            switch (a.length) {
              case 0:
                return callContentFunction(fun, thisArg);
              case 1:
                return callContentFunction(fun, thisArg, a[0]);
              case 2:
                return callContentFunction(fun, thisArg, a[0], a[1]);
              case 3:
                return callContentFunction(fun, thisArg, a[0], a[1], a[2]);
              case 4:
                return callContentFunction(fun, thisArg, a[0], a[1], a[2], a[3]);
              case 5:
                return callContentFunction(fun, thisArg, a[0], a[1], a[2], a[3], a[4]);
            }
        }
        var callArgs = FUN_APPLY(bind_mapArguments, null, arguments);
        return bind_invokeFunctionN(fun, thisArg, newTarget, boundArgs, callArgs);
    };
}

function bind_bindFunction1(fun, thisArg, boundArgs) {
    var bound1 = boundArgs[0];
    return function bound() {
        var a = arguments;
        var newTarget;
        if (_IsConstructing()) {
            newTarget = new.target;
            if (newTarget === bound)
                newTarget = fun;
            switch (a.length) {
              case 0:
                return constructContentFunction(fun, newTarget, bound1);
              case 1:
                return constructContentFunction(fun, newTarget, bound1, a[0]);
              case 2:
                return constructContentFunction(fun, newTarget, bound1, a[0], a[1]);
              case 3:
                return constructContentFunction(fun, newTarget, bound1, a[0], a[1], a[2]);
              case 4:
                return constructContentFunction(fun, newTarget, bound1, a[0], a[1], a[2], a[3]);
              case 5:
                return constructContentFunction(fun, newTarget, bound1, a[0], a[1], a[2], a[3], a[4]);
            }
        } else {
            switch (a.length) {
              case 0:
                return callContentFunction(fun, thisArg, bound1);
              case 1:
                return callContentFunction(fun, thisArg, bound1, a[0]);
              case 2:
                return callContentFunction(fun, thisArg, bound1, a[0], a[1]);
              case 3:
                return callContentFunction(fun, thisArg, bound1, a[0], a[1], a[2]);
              case 4:
                return callContentFunction(fun, thisArg, bound1, a[0], a[1], a[2], a[3]);
              case 5:
                return callContentFunction(fun, thisArg, bound1, a[0], a[1], a[2], a[3], a[4]);
            }
        }
        var callArgs = FUN_APPLY(bind_mapArguments, null, arguments);
        return bind_invokeFunctionN(fun, thisArg, newTarget, boundArgs, callArgs);
    };
}

function bind_bindFunction2(fun, thisArg, boundArgs) {
    var bound1 = boundArgs[0];
    var bound2 = boundArgs[1];
    return function bound() {
        var a = arguments;
        var newTarget;
        if (_IsConstructing()) {
            newTarget = new.target;
            if (newTarget === bound)
                newTarget = fun;
            switch (a.length) {
              case 0:
                return constructContentFunction(fun, newTarget, bound1, bound2);
              case 1:
                return constructContentFunction(fun, newTarget, bound1, bound2, a[0]);
              case 2:
                return constructContentFunction(fun, newTarget, bound1, bound2, a[0], a[1]);
              case 3:
                return constructContentFunction(fun, newTarget, bound1, bound2, a[0], a[1], a[2]);
              case 4:
                return constructContentFunction(fun, newTarget, bound1, bound2, a[0], a[1], a[2], a[3]);
              case 5:
                return constructContentFunction(fun, newTarget, bound1, bound2, a[0], a[1], a[2], a[3], a[4]);
            }
        } else {
            switch (a.length) {
              case 0:
                return callContentFunction(fun, thisArg, bound1, bound2);
              case 1:
                return callContentFunction(fun, thisArg, bound1, bound2, a[0]);
              case 2:
                return callContentFunction(fun, thisArg, bound1, bound2, a[0], a[1]);
              case 3:
                return callContentFunction(fun, thisArg, bound1, bound2, a[0], a[1], a[2]);
              case 4:
                return callContentFunction(fun, thisArg, bound1, bound2, a[0], a[1], a[2], a[3]);
              case 5:
                return callContentFunction(fun, thisArg, bound1, bound2, a[0], a[1], a[2], a[3], a[4]);
            }
        }
        var callArgs = FUN_APPLY(bind_mapArguments, null, arguments);
        return bind_invokeFunctionN(fun, thisArg, newTarget, boundArgs, callArgs);
    };
}

function bind_bindFunctionN(fun, thisArg, boundArgs) {
    assert(boundArgs.length > 2, "Fast paths should be used for few-bound-args cases.");
    return function bound() {
        var newTarget;
        if (_IsConstructing()) {
            newTarget = new.target;
            if (newTarget === bound)
                newTarget = fun;
        }
        if (arguments.length === 0) {
            if (newTarget !== undefined)
                return bind_constructFunctionN(fun, newTarget, boundArgs);
            else
                return bind_applyFunctionN(fun, thisArg, boundArgs);
        }
        var callArgs = FUN_APPLY(bind_mapArguments, null, arguments);
        return bind_invokeFunctionN(fun, thisArg, newTarget, boundArgs, callArgs);
    };
}

function bind_mapArguments() {
    var len = arguments.length;
    var args = std_Array(len);
    for (var i = 0; i < len; i++)
        _DefineDataProperty(args, i, arguments[i]);
    return args;
}

function bind_invokeFunctionN(fun, thisArg, newTarget, boundArgs, callArgs) {
    var boundArgsCount = boundArgs.length;
    var callArgsCount = callArgs.length;
    var args = std_Array(boundArgsCount + callArgsCount);
    for (var i = 0; i < boundArgsCount; i++)
        _DefineDataProperty(args, i, boundArgs[i]);
    for (var i = 0; i < callArgsCount; i++)
        _DefineDataProperty(args, i + boundArgsCount, callArgs[i]);
    if (newTarget !== undefined)
        return bind_constructFunctionN(fun, newTarget, args);
    return bind_applyFunctionN(fun, thisArg, args);
}

function bind_applyFunctionN(fun, thisArg, a) {
    switch (a.length) {
      case 0:
        return callContentFunction(fun, thisArg);
      case 1:
        return callContentFunction(fun, thisArg, a[0]);
      case 2:
        return callContentFunction(fun, thisArg, a[0], a[1]);
      case 3:
        return callContentFunction(fun, thisArg, a[0], a[1], a[2]);
      case 4:
        return callContentFunction(fun, thisArg, a[0], a[1], a[2], a[3]);
      case 5:
        return callContentFunction(fun, thisArg, a[0], a[1], a[2], a[3], a[4]);
      case 6:
        return callContentFunction(fun, thisArg, a[0], a[1], a[2], a[3], a[4], a[5]);
      case 7:
        return callContentFunction(fun, thisArg, a[0], a[1], a[2], a[3], a[4], a[5], a[6]);
      case 8:
        return callContentFunction(fun, thisArg, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
      case 9:
        return callContentFunction(fun, thisArg, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8]);
      default:
        return FUN_APPLY(fun, thisArg, a);
    }
}

function bind_constructFunctionN(fun, newTarget, a) {
    switch (a.length) {
      case 1:
        return constructContentFunction(fun, newTarget, a[0]);
      case 2:
        return constructContentFunction(fun, newTarget, a[0], a[1]);
      case 3:
        return constructContentFunction(fun, newTarget, a[0], a[1], a[2]);
      case 4:
        return constructContentFunction(fun, newTarget, a[0], a[1], a[2], a[3]);
      case 5:
        return constructContentFunction(fun, newTarget, a[0], a[1], a[2], a[3], a[4]);
      case 6:
        return constructContentFunction(fun, newTarget, a[0], a[1], a[2], a[3], a[4], a[5]);
      case 7:
        return constructContentFunction(fun, newTarget, a[0], a[1], a[2], a[3], a[4], a[5], a[6]);
      case 8:
        return constructContentFunction(fun, newTarget, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
      case 9:
        return constructContentFunction(fun, newTarget, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8]);
      case 10:
        return constructContentFunction(fun, newTarget, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9]);
      case 11:
        return constructContentFunction(fun, newTarget, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10]);
      case 12:
        return constructContentFunction(fun, newTarget, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11]);
      default:
        assert(a.length !== 0,
               "bound function construction without args should be handled by caller");
        return _ConstructFunction(fun, newTarget, a);
    }
}

/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * JS object implementation.
 */

#include "jsobjinlines.h"

#include "mozilla/ArrayUtils.h"
#include "mozilla/MathAlgorithms.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/SizePrintfMacros.h"
#include "mozilla/TemplateLib.h"
#include "mozilla/UniquePtr.h"

#include <string.h>

#include "jsapi.h"
#include "jsarray.h"
#include "jsatom.h"
#include "jscntxt.h"
#include "jsfriendapi.h"
#include "jsfun.h"
#include "jsgc.h"
#include "jsiter.h"
#include "jsnum.h"
#include "jsopcode.h"
#include "jsprf.h"
#include "jsscript.h"
#include "jsstr.h"
#include "jstypes.h"
#include "jsutil.h"
#include "jswatchpoint.h"
#include "jswrapper.h"

#include "asmjs/AsmJSModule.h"
#include "builtin/Eval.h"
#include "builtin/Object.h"
#include "builtin/SymbolObject.h"
#include "frontend/BytecodeCompiler.h"
#include "gc/Marking.h"
#include "jit/BaselineJIT.h"
#include "js/MemoryMetrics.h"
#include "js/Proxy.h"
#include "vm/ArgumentsObject.h"
#include "vm/Interpreter.h"
#include "vm/ProxyObject.h"
#include "vm/RegExpStaticsObject.h"
#include "vm/Shape.h"
#include "vm/TypedArrayCommon.h"

#include "jsatominlines.h"
#include "jsboolinlines.h"
#include "jscntxtinlines.h"
#include "jscompartmentinlines.h"

#include "vm/ArrayObject-inl.h"
#include "vm/BooleanObject-inl.h"
#include "vm/Interpreter-inl.h"
#include "vm/NativeObject-inl.h"
#include "vm/NumberObject-inl.h"
#include "vm/Runtime-inl.h"
#include "vm/Shape-inl.h"
#include "vm/StringObject-inl.h"

using namespace js;
using namespace js::gc;

using mozilla::DebugOnly;
using mozilla::Maybe;
using mozilla::UniquePtr;

JS_FRIEND_API(JSObject*)
JS_ObjectToInnerObject(JSContext* cx, HandleObject obj)
{
    if (!obj) {
        JS_ReportErrorNumber(cx, GetErrorMessage, nullptr, JSMSG_INACTIVE);
        return nullptr;
    }
    return GetInnerObject(obj);
}

JS_FRIEND_API(JSObject*)
JS_ObjectToOuterObject(JSContext* cx, HandleObject obj)
{
    assertSameCompartment(cx, obj);
    return GetOuterObject(cx, obj);
}

JSObject*
js::NonNullObject(JSContext* cx, const Value& v)
{
    if (v.isPrimitive()) {
        RootedValue value(cx, v);
        UniquePtr<char[], JS::FreePolicy> bytes =
            DecompileValueGenerator(cx, JSDVG_SEARCH_STACK, value, NullPtr());
        if (!bytes)
            return nullptr;
        JS_ReportErrorNumber(cx, GetErrorMessage, nullptr, JSMSG_NOT_NONNULL_OBJECT, bytes.get());
        return nullptr;
    }
    return &v.toObject();
}

const char*
js::InformalValueTypeName(const Value& v)
{
    if (v.isObject())
        return v.toObject().getClass()->name;
    if (v.isString())
        return "string";
    if (v.isSymbol())
        return "symbol";
    if (v.isNumber())
        return "number";
    if (v.isBoolean())
        return "boolean";
    if (v.isNull())
        return "null";
    if (v.isUndefined())
        return "undefined";
    return "value";
}

bool
js::FromPropertyDescriptor(JSContext *cx, Handle<PropertyDescriptor> desc,
                           MutableHandleValue vp)
{
    if (!desc.object()) {
        vp.setUndefined();
        return true;
    }

    RootedObject obj(cx, NewBuiltinClassInstance<PlainObject>(cx));
    if (!obj)
        return false;

    const JSAtomState &names = cx->names();
    RootedValue v(cx);
    if (desc.hasConfigurable()) {
        v.setBoolean(desc.configurable());
        if (!DefineProperty(cx, obj, names.configurable, v))
            return false;
    }
    if (desc.hasEnumerable()) {
        v.setBoolean(desc.enumerable());
        if (!DefineProperty(cx, obj, names.enumerable, v))
            return false;
    }
    if (desc.hasValue()) {
        if (!DefineProperty(cx, obj, names.value, desc.value()))
            return false;
    }
    if (desc.hasWritable()) {
        v.setBoolean(desc.writable());
        if (!DefineProperty(cx, obj, names.writable, v))
            return false;
    }
    if (desc.hasGetterObject()) {
        if (JSObject *get = desc.getterObject())
            v.setObject(*get);
        else
            v.setUndefined();
        if (!DefineProperty(cx, obj, names.get, v))
            return false;
    }
    if (desc.hasSetterObject()) {
        if (JSObject *set = desc.setterObject())
            v.setObject(*set);
        else
            v.setUndefined();
        if (!DefineProperty(cx, obj, names.set, v))
            return false;
    }
    vp.setObject(*obj);
    return true;
}

bool
js::GetFirstArgumentAsObject(JSContext* cx, const CallArgs& args, const char* method,
                             MutableHandleObject objp)
{
    if (args.length() == 0) {
        JS_ReportErrorNumber(cx, GetErrorMessage, nullptr, JSMSG_MORE_ARGS_NEEDED,
                             method, "0", "s");
        return false;
    }

    HandleValue v = args[0];
    if (!v.isObject()) {
        UniquePtr<char[], JS::FreePolicy> bytes =
            DecompileValueGenerator(cx, JSDVG_SEARCH_STACK, v, NullPtr());
        if (!bytes)
            return false;
        JS_ReportErrorNumber(cx, GetErrorMessage, nullptr, JSMSG_UNEXPECTED_TYPE,
                             bytes.get(), "not an object");
        return false;
    }

    objp.set(&v.toObject());
    return true;
}

static bool
GetPropertyIfPresent(JSContext* cx, HandleObject obj, HandleId id, MutableHandleValue vp,
                     bool* foundp)
{
    if (!HasProperty(cx, obj, id, foundp))
        return false;
    if (!*foundp) {
        vp.setUndefined();
        return true;
    }

    return GetProperty(cx, obj, obj, id, vp);
}

bool
js::Throw(JSContext *cx, jsid id, unsigned errorNumber)
{
    MOZ_ASSERT(js_ErrorFormatString[errorNumber].argCount == 1);

    RootedValue idVal(cx, IdToValue(id));
    JSString* idstr = ValueToSource(cx, idVal);
    if (!idstr)
       return false;
    JSAutoByteString bytes(cx, idstr);
    if (!bytes)
        return false;
    JS_ReportErrorNumber(cx, GetErrorMessage, nullptr, errorNumber, bytes.ptr());
    return false;
}

bool
js::Throw(JSContext *cx, JSObject *obj, unsigned errorNumber)
{
    if (js_ErrorFormatString[errorNumber].argCount == 1) {
        RootedValue val(cx, ObjectValue(*obj));
        ReportValueErrorFlags(cx, JSREPORT_ERROR, errorNumber,
                              JSDVG_IGNORE_STACK, val, NullPtr(),
                              nullptr, nullptr);
    } else {
        MOZ_ASSERT(js_ErrorFormatString[errorNumber].argCount == 0);
        JS_ReportErrorNumber(cx, GetErrorMessage, nullptr, errorNumber);
    }
    return false;
}



/*** Standard-compliant property definition (used by Object.defineProperty) **********************/

static bool
DefinePropertyOnObject(JSContext *cx, HandleNativeObject obj, HandleId id,
                       Handle<PropertyDescriptor> desc, ObjectOpResult &result)
{
    /* 8.12.9 step 1. */
    RootedShape shape(cx);
    MOZ_ASSERT(!obj->getOps()->lookupProperty);
    if (!NativeLookupOwnProperty<CanGC>(cx, obj, id, &shape))
        return false;

    MOZ_ASSERT(!obj->getOps()->defineProperty);

    /* 8.12.9 steps 2-4. */
    if (!shape) {
        bool extensible;
        if (!IsExtensible(cx, obj, &extensible))
            return false;
        if (!extensible)
            return result.fail(JSMSG_OBJECT_NOT_EXTENSIBLE);

        if (desc.isGenericDescriptor() || desc.isDataDescriptor()) {
            MOZ_ASSERT(!obj->getOps()->defineProperty);
            RootedValue v(cx, desc.hasValue() ? desc.value() : UndefinedValue());
            unsigned attrs = desc.attributes() & (JSPROP_PERMANENT | JSPROP_ENUMERATE | JSPROP_READONLY);

            if (!desc.hasConfigurable())
                attrs |= JSPROP_PERMANENT;
            if (!desc.hasWritable())
                attrs |= JSPROP_READONLY;
            return NativeDefineProperty(cx, obj, id, v, nullptr, nullptr, attrs, result);
        }

        MOZ_ASSERT(desc.isAccessorDescriptor());

        unsigned attrs = desc.attributes() & (JSPROP_PERMANENT | JSPROP_ENUMERATE |
                                              JSPROP_SHARED | JSPROP_GETTER | JSPROP_SETTER);
        if (!desc.hasConfigurable())
            attrs |= JSPROP_PERMANENT;
        return NativeDefineProperty(cx, obj, id, UndefinedHandleValue,
                                    desc.getter(), desc.setter(), attrs, result);
    }

    /* 8.12.9 steps 5-6 (note 5 is merely a special case of 6). */
    RootedValue v(cx);

    bool shapeDataDescriptor = true,
         shapeAccessorDescriptor = false,
         shapeWritable = true,
         shapeConfigurable = true,
         shapeEnumerable = true,
         shapeHasDefaultGetter = true,
         shapeHasDefaultSetter = true,
         shapeHasGetterValue = false,
         shapeHasSetterValue = false;
    uint8_t shapeAttributes = GetShapeAttributes(obj, shape);
    if (!IsImplicitDenseOrTypedArrayElement(shape)) {
        shapeDataDescriptor = shape->isDataDescriptor();
        shapeAccessorDescriptor = shape->isAccessorDescriptor();
        shapeWritable = shape->writable();
        shapeConfigurable = shape->configurable();
        shapeEnumerable = shape->enumerable();
        shapeHasDefaultGetter = shape->hasDefaultGetter();
        shapeHasDefaultSetter = shape->hasDefaultSetter();
        shapeHasGetterValue = shape->hasGetterValue();
        shapeHasSetterValue = shape->hasSetterValue();
        shapeAttributes = shape->attributes();
    }

    do {
        if (desc.isAccessorDescriptor()) {
            if (!shapeAccessorDescriptor)
                break;

            if (desc.hasGetterObject()) {
                if (!shape->hasGetterValue() || desc.getterObject() != shape->getterObject())
                    break;
            }

            if (desc.hasSetterObject()) {
                if (!shape->hasSetterValue() || desc.setterObject() != shape->setterObject())
                    break;
            }
        } else {
            /*
             * Determine the current value of the property once, if the current
             * value might actually need to be used or preserved later.  NB: we
             * guard on whether the current property is a data descriptor to
             * avoid calling a getter; we won't need the value if it's not a
             * data descriptor.
             */
            if (IsImplicitDenseOrTypedArrayElement(shape)) {
                v = obj->getDenseOrTypedArrayElement(JSID_TO_INT(id));
            } else if (shape->isDataDescriptor()) {
                /*
                 * We must rule out a non-configurable js::SetterOp-guarded
                 * property becoming a writable unguarded data property, since
                 * such a property can have its value changed to one the getter
                 * and setter preclude.
                 *
                 * A desc lacking writable but with value is a data descriptor
                 * and we must reject it as if it had writable: true if current
                 * is writable.
                 */
                if (!shape->configurable() &&
                    (!shape->hasDefaultGetter() || !shape->hasDefaultSetter()) &&
                    desc.isDataDescriptor() &&
                    (desc.hasWritable() ? desc.writable() : shape->writable()))
                {
                    return result.fail(JSMSG_CANT_REDEFINE_PROP);
                }

                if (!NativeGetExistingProperty(cx, obj, obj, shape, &v))
                    return false;
            }

            if (desc.isDataDescriptor()) {
                if (!shapeDataDescriptor)
                    break;

                bool same;
                if (desc.hasValue()) {
                    if (!SameValue(cx, desc.value(), v, &same))
                        return false;
                    if (!same) {
                        /*
                         * Insist that a non-configurable js::GetterOp data
                         * property is frozen at exactly the last-got value.
                         *
                         * Duplicate the first part of the big conjunction that
                         * we tested above, rather than add a local bool flag.
                         * Likewise, don't try to keep shape->writable() in a
                         * flag we veto from true to false for non-configurable
                         * GetterOp-based data properties and test before the
                         * SameValue check later on in order to re-use that "if
                         * (!SameValue) return false" logic.
                         *
                         * This function is large and complex enough that it
                         * seems best to repeat a small bit of code and return
                         * result.fail() ASAP, instead of being clever.
                         */
                        if (!shapeConfigurable &&
                            (!shape->hasDefaultGetter() || !shape->hasDefaultSetter()))
                        {
                            return result.fail(JSMSG_CANT_REDEFINE_PROP);
                        }
                        break;
                    }
                }
                if (desc.hasWritable() && desc.writable() != shapeWritable)
                    break;
            } else {
                /* The only fields in desc will be handled below. */
                MOZ_ASSERT(desc.isGenericDescriptor());
            }
        }

        if (desc.hasConfigurable() && desc.configurable() != shapeConfigurable)
            break;
        if (desc.hasEnumerable() && desc.enumerable() != shapeEnumerable)
            break;

        /* The conditions imposed by step 5 or step 6 apply. */
        return result.succeed();
    } while (0);

    /* 8.12.9 step 7. */
    if (!shapeConfigurable) {
        if ((desc.hasConfigurable() && desc.configurable()) ||
            (desc.hasEnumerable() && desc.enumerable() != shape->enumerable())) {
            return result.fail(JSMSG_CANT_REDEFINE_PROP);
        }
    }

    bool callDelProperty = false;

    if (desc.isGenericDescriptor()) {
        /* 8.12.9 step 8, no validation required */
    } else if (desc.isDataDescriptor() != shapeDataDescriptor) {
        /* 8.12.9 step 9. */
        if (!shapeConfigurable)
            return result.fail(JSMSG_CANT_REDEFINE_PROP);
    } else if (desc.isDataDescriptor()) {
        /* 8.12.9 step 10. */
        MOZ_ASSERT(shapeDataDescriptor);
        if (!shapeConfigurable && !shape->writable()) {
            if (desc.hasWritable() && desc.writable())
                return result.fail(JSMSG_CANT_REDEFINE_PROP);
            if (desc.hasValue()) {
                bool same;
                if (!SameValue(cx, desc.value(), v, &same))
                    return false;
                if (!same)
                    return result.fail(JSMSG_CANT_REDEFINE_PROP);
            }
        }

        callDelProperty = !shapeHasDefaultGetter || !shapeHasDefaultSetter;
    } else {
        /* 8.12.9 step 11. */
        MOZ_ASSERT(desc.isAccessorDescriptor() && shape->isAccessorDescriptor());
        if (!shape->configurable()) {
            // The hasSetterValue() and hasGetterValue() calls below ought to
            // be redundant here, because accessor shapes should always have
            // both JSPROP_GETTER and JSPROP_SETTER. But this is not the case
            // currently; in particular Object.defineProperty(obj, key, {get: fn})
            // creates a property without JSPROP_SETTER (bug 1133315).
            if (desc.hasSetterObject() &&
                desc.setterObject() != (shape->hasSetterValue() ? shape->setterObject() : nullptr))
            {
                return result.fail(JSMSG_CANT_REDEFINE_PROP);
            }

            if (desc.hasGetterObject() &&
                desc.getterObject() != (shape->hasGetterValue() ? shape->getterObject() : nullptr))
            {
                return result.fail(JSMSG_CANT_REDEFINE_PROP);
            }
        }
    }

    /* 8.12.9 step 12. */
    unsigned attrs;
    GetterOp getter;
    SetterOp setter;
    if (desc.isGenericDescriptor()) {
        unsigned changed = 0;
        if (desc.hasConfigurable())
            changed |= JSPROP_PERMANENT;
        if (desc.hasEnumerable())
            changed |= JSPROP_ENUMERATE;

        attrs = (shapeAttributes & ~changed) | (desc.attributes() & changed);
        getter = IsImplicitDenseOrTypedArrayElement(shape) ? nullptr : shape->getter();
        setter = IsImplicitDenseOrTypedArrayElement(shape) ? nullptr : shape->setter();
    } else if (desc.isDataDescriptor()) {
        /* Watch out for accessor -> data transformations here. */
        unsigned changed = JSPROP_GETTER | JSPROP_SETTER | JSPROP_SHARED;
        unsigned descAttrs = desc.attributes();
        if (desc.hasConfigurable())
            changed |= JSPROP_PERMANENT;
        if (desc.hasEnumerable())
            changed |= JSPROP_ENUMERATE;

        if (desc.hasWritable()) {
            changed |= JSPROP_READONLY;
        } else if (!shapeDataDescriptor) {
            changed |= JSPROP_READONLY;
            descAttrs |= JSPROP_READONLY;
        }

        if (desc.hasValue())
            v = desc.value();
        attrs = (descAttrs & changed) | (shapeAttributes & ~changed);
        getter = nullptr;
        setter = nullptr;
    } else {
        MOZ_ASSERT(desc.isAccessorDescriptor());

        /* 8.12.9 step 12. */
        unsigned changed = 0;
        if (desc.hasConfigurable())
            changed |= JSPROP_PERMANENT;
        if (desc.hasEnumerable())
            changed |= JSPROP_ENUMERATE;
        if (desc.hasGetterObject())
            changed |= JSPROP_GETTER | JSPROP_SHARED | JSPROP_READONLY;
        if (desc.hasSetterObject())
            changed |= JSPROP_SETTER | JSPROP_SHARED | JSPROP_READONLY;

        attrs = (desc.attributes() & changed) | (shapeAttributes & ~changed);
        if (desc.hasGetterObject()) {
            getter = desc.getter();
        } else {
            getter = (shapeHasDefaultGetter && !shapeHasGetterValue)
                     ? nullptr
                     : shape->getter();
        }
        if (desc.hasSetterObject()) {
            setter = desc.setter();
        } else {
            setter = (shapeHasDefaultSetter && !shapeHasSetterValue)
                     ? nullptr
                     : shape->setter();
        }
    }

    /*
     * Since "data" properties implemented using native C functions may rely on
     * side effects during setting, we must make them aware that they have been
     * "assigned"; deleting the property before redefining it does the trick.
     * See bug 539766, where we ran into problems when we redefined
     * arguments.length without making the property aware that its value had
     * been changed (which would have happened if we had deleted it before
     * redefining it or we had invoked its setter to change its value).
     */
    if (callDelProperty) {
        ObjectOpResult ignored;
        if (!CallJSDeletePropertyOp(cx, obj->getClass()->delProperty, obj, id, ignored))
            return false;
    }

    return NativeDefineProperty(cx, obj, id, v, getter, setter, attrs, result);
}

/* ES6 20130308 draft 8.4.2.1 [[DefineOwnProperty]] */
static bool
DefinePropertyOnArray(JSContext *cx, Handle<ArrayObject*> arr, HandleId id,
                      Handle<PropertyDescriptor> desc, ObjectOpResult &result)
{
    /* Step 2. */
    if (id == NameToId(cx->names().length)) {
        // Canonicalize value, if necessary, before proceeding any further.  It
        // would be better if this were always/only done by ArraySetLength.
        // But canonicalization may throw a RangeError (or other exception, if
        // the value is an object with user-defined conversion semantics)
        // before other attributes are checked.  So as long as our internal
        // defineProperty hook doesn't match the ECMA one, this duplicate
        // checking can't be helped.
        RootedValue v(cx);
        if (desc.hasValue()) {
            uint32_t newLen;
            if (!CanonicalizeArrayLengthValue(cx, desc.value(), &newLen))
                return false;
            v.setNumber(newLen);
        } else {
            v.setNumber(arr->length());
        }

        if (desc.hasConfigurable() && desc.configurable())
            return result.fail(JSMSG_CANT_REDEFINE_PROP);
        if (desc.hasEnumerable() && desc.enumerable())
            return result.fail(JSMSG_CANT_REDEFINE_PROP);

        if (desc.isAccessorDescriptor())
            return result.fail(JSMSG_CANT_REDEFINE_PROP);

        unsigned attrs = arr->lookup(cx, id)->attributes();
        if (!arr->lengthIsWritable()) {
            if (desc.hasWritable() && desc.writable())
                return result.fail(JSMSG_CANT_REDEFINE_PROP);
        } else {
            if (desc.hasWritable() && !desc.writable())
                attrs = attrs | JSPROP_READONLY;
        }

        return ArraySetLength(cx, arr, id, attrs, v, result);
    }

    /* Step 3. */
    uint32_t index;
    if (IdIsIndex(id, &index)) {
        /* Step 3b. */
        uint32_t oldLen = arr->length();

        /* Steps 3a, 3e. */
        if (index >= oldLen && !arr->lengthIsWritable())
            return result.fail(JSMSG_CANT_APPEND_TO_ARRAY);

        /* Steps 3f-j. */
        return DefinePropertyOnObject(cx, arr, id, desc, result);
    }

    /* Step 4. */
    return DefinePropertyOnObject(cx, arr, id, desc, result);
}

// ES6 draft rev31 9.4.5.3 [[DefineOwnProperty]]
static bool
DefinePropertyOnTypedArray(JSContext *cx, HandleObject obj, HandleId id,
                           Handle<PropertyDescriptor> desc, ObjectOpResult &result)
{
    MOZ_ASSERT(IsAnyTypedArray(obj));
    // Steps 3.a-c.
    uint64_t index;
    if (IsTypedArrayIndex(id, &index)) {
        // These are all substeps of 3.c.
        // Steps i-vi.
        // We (wrongly) ignore out of range defines with a value.
        if (index >= AnyTypedArrayLength(obj))
            return result.succeed();

        // Step vii.
        if (desc.isAccessorDescriptor())
            return result.fail(JSMSG_CANT_REDEFINE_PROP);

        // Step viii.
        if (desc.hasConfigurable() && desc.configurable())
            return result.fail(JSMSG_CANT_REDEFINE_PROP);

        // Step ix.
        if (desc.hasEnumerable() && !desc.enumerable())
            return result.fail(JSMSG_CANT_REDEFINE_PROP);

        // Step x.
        if (desc.hasWritable() && !desc.writable())
            return result.fail(JSMSG_CANT_REDEFINE_PROP);

        // Step xi.
        if (desc.hasValue()) {
            double d;
            if (!ToNumber(cx, desc.value(), &d))
                return false;

            if (obj->is<TypedArrayObject>())
                TypedArrayObject::setElement(obj->as<TypedArrayObject>(), index, d);
            else
                SharedTypedArrayObject::setElement(obj->as<SharedTypedArrayObject>(), index, d);
        }

        // Step xii.
        return result.succeed();
    }

    // Step 4.
    return DefinePropertyOnObject(cx, obj.as<NativeObject>(), id, desc, result);
}

bool
js::StandardDefineProperty(JSContext *cx, HandleObject obj, HandleId id,
                           Handle<PropertyDescriptor> desc, ObjectOpResult &result)
{
    if (obj->is<ArrayObject>()) {
        Rooted<ArrayObject*> arr(cx, &obj->as<ArrayObject>());
        return DefinePropertyOnArray(cx, arr, id, desc, result);
    }

    if (IsAnyTypedArray(obj))
        return DefinePropertyOnTypedArray(cx, obj, id, desc, result);

    if (obj->is<UnboxedPlainObject>() && !UnboxedPlainObject::convertToNative(cx, obj))
        return false;

    if (obj->getOps()->lookupProperty) {
        if (obj->is<ProxyObject>()) {
            Rooted<PropertyDescriptor> pd(cx, desc);
            pd.object().set(obj);
            return Proxy::defineProperty(cx, obj, id, &pd, result);
        }
        return result.fail(JSMSG_OBJECT_NOT_EXTENSIBLE);
    }

    return DefinePropertyOnObject(cx, obj.as<NativeObject>(), id, desc, result);
}

bool
js::StandardDefineProperty(JSContext *cx, HandleObject obj, HandleId id,
                           Handle<PropertyDescriptor> desc)
{
    ObjectOpResult success;
    return StandardDefineProperty(cx, obj, id, desc, success) &&
           success.checkStrict(cx, obj, id);
}

bool
CheckCallable(JSContext *cx, JSObject *obj, const char *fieldName)
{
    if (obj && !obj->isCallable()) {
        JS_ReportErrorNumber(cx, GetErrorMessage, nullptr, JSMSG_BAD_GET_SET_FIELD,
                             fieldName);
        return false;
    }
    return true;
}

bool
js::ToPropertyDescriptor(JSContext *cx, HandleValue descval, bool checkAccessors,
                         MutableHandle<PropertyDescriptor> desc)
{
    // step 2
    if (!descval.isObject()) {
        JS_ReportErrorNumber(cx, GetErrorMessage, nullptr, JSMSG_NOT_NONNULL_OBJECT,
                             InformalValueTypeName(descval));
        return false;
    }
    RootedObject obj(cx, &descval.toObject());

    // step 3
    desc.clear();

    bool found = false;
    RootedId id(cx);
    RootedValue v(cx);
    unsigned attrs = 0;

    // step 4
    id = NameToId(cx->names().enumerable);
    if (!GetPropertyIfPresent(cx, obj, id, &v, &found))
        return false;
    if (found) {
        if (ToBoolean(v))
            attrs |= JSPROP_ENUMERATE;
    } else {
        attrs |= JSPROP_IGNORE_ENUMERATE;
    }

    // step 5
    id = NameToId(cx->names().configurable);
    if (!GetPropertyIfPresent(cx, obj, id, &v, &found))
        return false;
    if (found) {
        if (!ToBoolean(v))
            attrs |= JSPROP_PERMANENT;
    } else {
        attrs |= JSPROP_IGNORE_PERMANENT;
    }

    // step 6
    id = NameToId(cx->names().value);
    if (!GetPropertyIfPresent(cx, obj, id, &v, &found))
        return false;
    if (found)
        desc.value().set(v);
    else
        attrs |= JSPROP_IGNORE_VALUE;

    // step 7
    id = NameToId(cx->names().writable);
    if (!GetPropertyIfPresent(cx, obj, id, &v, &found))
        return false;
    if (found) {
        if (!ToBoolean(v))
            attrs |= JSPROP_READONLY;
    } else {
        attrs |= JSPROP_IGNORE_READONLY;
    }

    // step 8
    bool hasGetOrSet;
    id = NameToId(cx->names().get);
    if (!GetPropertyIfPresent(cx, obj, id, &v, &found))
        return false;
    hasGetOrSet = found;
    if (found) {
        if (v.isObject()) {
            if (checkAccessors && !CheckCallable(cx, &v.toObject(), js_getter_str))
                return false;
            desc.setGetterObject(&v.toObject());
        } else if (!v.isUndefined()) {
            JS_ReportErrorNumber(cx, GetErrorMessage, nullptr, JSMSG_BAD_GET_SET_FIELD,
                                 js_getter_str);
            return false;
        }
        attrs |= JSPROP_GETTER | JSPROP_SHARED;
    }

    // step 9
    id = NameToId(cx->names().set);
    if (!GetPropertyIfPresent(cx, obj, id, &v, &found))
        return false;
    hasGetOrSet |= found;
    if (found) {
        if (v.isObject()) {
            if (checkAccessors && !CheckCallable(cx, &v.toObject(), js_setter_str))
                return false;
            desc.setSetterObject(&v.toObject());
        } else if (!v.isUndefined()) {
            JS_ReportErrorNumber(cx, GetErrorMessage, nullptr, JSMSG_BAD_GET_SET_FIELD,
                                 js_setter_str);
            return false;
        }
        attrs |= JSPROP_SETTER | JSPROP_SHARED;
    }

    // step 10
    if (hasGetOrSet) {
        if (!(attrs & JSPROP_IGNORE_READONLY) || !(attrs & JSPROP_IGNORE_VALUE)) {
            JS_ReportErrorNumber(cx, GetErrorMessage, nullptr, JSMSG_INVALID_DESCRIPTOR);
            return false;
        }

        // By convention, these bits are not used on accessor descriptors.
        attrs &= ~(JSPROP_IGNORE_READONLY | JSPROP_IGNORE_VALUE);
    }

    desc.setAttributes(attrs);
    MOZ_ASSERT_IF(attrs & JSPROP_READONLY, !(attrs & (JSPROP_GETTER | JSPROP_SETTER)));
    MOZ_ASSERT_IF(attrs & (JSPROP_GETTER | JSPROP_SETTER), attrs & JSPROP_SHARED);
    return true;
}

bool
js::CheckPropertyDescriptorAccessors(JSContext *cx, Handle<PropertyDescriptor> desc)
{
    if (desc.hasGetterObject()) {
        if (!CheckCallable(cx, desc.getterObject(), js_getter_str))
            return false;
    }
    if (desc.hasSetterObject()) {
        if (!CheckCallable(cx, desc.setterObject(), js_setter_str))
            return false;
    }
    return true;
}

void
js::CompletePropertyDescriptor(MutableHandle<PropertyDescriptor> desc)
{
    if (desc.isGenericDescriptor() || desc.isDataDescriptor()) {
        if (!desc.hasValue())
            desc.value().setUndefined();
        if (!desc.hasWritable())
            desc.attributesRef() |= JSPROP_READONLY;
        desc.attributesRef() &= ~(JSPROP_IGNORE_READONLY | JSPROP_IGNORE_VALUE);
    } else {
        if (!desc.hasGetterObject())
            desc.setGetterObject(nullptr);
        if (!desc.hasSetterObject())
            desc.setSetterObject(nullptr);
        desc.attributesRef() |= JSPROP_GETTER | JSPROP_SETTER | JSPROP_SHARED;
    }
    if (!desc.hasEnumerable())
        desc.attributesRef() &= ~JSPROP_ENUMERATE;
    if (!desc.hasConfigurable())
        desc.attributesRef() |= JSPROP_PERMANENT;
    desc.attributesRef() &= ~(JSPROP_IGNORE_PERMANENT | JSPROP_IGNORE_ENUMERATE);
}

bool
js::ReadPropertyDescriptors(JSContext *cx, HandleObject props, bool checkAccessors,
                            AutoIdVector *ids, AutoPropertyDescriptorVector *descs)
{
    if (!GetPropertyKeys(cx, props, JSITER_OWNONLY | JSITER_SYMBOLS, ids))
        return false;

    RootedId id(cx);
    for (size_t i = 0, len = ids->length(); i < len; i++) {
        id = (*ids)[i];
        Rooted<PropertyDescriptor> desc(cx);
        RootedValue v(cx);
        if (!GetProperty(cx, props, props, id, &v) ||
            !ToPropertyDescriptor(cx, v, checkAccessors, &desc) ||
            !descs->append(desc))
        {
            return false;
        }
    }
    return true;
}

bool
js::DefineProperties(JSContext *cx, HandleObject obj, HandleObject props)
{
    AutoIdVector ids(cx);
    AutoPropertyDescriptorVector descs(cx);
    if (!ReadPropertyDescriptors(cx, props, true, &ids, &descs))
        return false;

    for (size_t i = 0, len = ids.length(); i < len; i++) {
        if (!StandardDefineProperty(cx, obj, ids[i], descs[i]))
            return false;
    }

    return true;
}

/*** Seal and freeze *****************************************************************************/

static unsigned
GetSealedOrFrozenAttributes(unsigned attrs, IntegrityLevel level)
{
    /* Make all attributes permanent; if freezing, make data attributes read-only. */
    if (level == IntegrityLevel::Frozen && !(attrs & (JSPROP_GETTER | JSPROP_SETTER)))
        return JSPROP_PERMANENT | JSPROP_READONLY;
    return JSPROP_PERMANENT;
}

/* ES6 draft rev 29 (6 Dec 2014) 7.3.13. */
bool
js::SetIntegrityLevel(JSContext* cx, HandleObject obj, IntegrityLevel level)
{
    assertSameCompartment(cx, obj);

    // Steps 3-5. (Steps 1-2 are redundant assertions.)
    if (!PreventExtensions(cx, obj))
        return false;

    // Steps 6-7.
    AutoIdVector keys(cx);
    if (!GetPropertyKeys(cx, obj, JSITER_HIDDEN | JSITER_OWNONLY | JSITER_SYMBOLS, &keys))
        return false;

    // PreventExtensions must sparsify dense objects, so we can assign to holes
    // without checks.
    MOZ_ASSERT_IF(obj->isNative(), obj->as<NativeObject>().getDenseCapacity() == 0);

    // Steps 8-9, loosely interpreted.
    if (obj->isNative() && !obj->as<NativeObject>().inDictionaryMode() && !IsAnyTypedArray(obj)) {
        HandleNativeObject nobj = obj.as<NativeObject>();

        // Seal/freeze non-dictionary objects by constructing a new shape
        // hierarchy mirroring the original one, which can be shared if many
        // objects with the same structure are sealed/frozen. If we use the
        // generic path below then any non-empty object will be converted to
        // dictionary mode.
        RootedShape last(cx, EmptyShape::getInitialShape(cx, nobj->getClass(),
                                                         nobj->getTaggedProto(),
                                                         nobj->numFixedSlots(),
                                                         nobj->lastProperty()->getObjectFlags()));
        if (!last)
            return false;

        // Get an in-order list of the shapes in this object.
        AutoShapeVector shapes(cx);
        for (Shape::Range<NoGC> r(nobj->lastProperty()); !r.empty(); r.popFront()) {
            if (!shapes.append(&r.front()))
                return false;
        }
        Reverse(shapes.begin(), shapes.end());

        for (size_t i = 0; i < shapes.length(); i++) {
            StackShape unrootedChild(shapes[i]);
            RootedGeneric<StackShape*> child(cx, &unrootedChild);
            child->attrs |= GetSealedOrFrozenAttributes(child->attrs, level);

            if (!JSID_IS_EMPTY(child->propid) && level == IntegrityLevel::Frozen)
                MarkTypePropertyNonWritable(cx, nobj, child->propid);

            last = cx->compartment()->propertyTree.getChild(cx, last, *child);
            if (!last)
                return false;
        }

        MOZ_ASSERT(nobj->lastProperty()->slotSpan() == last->slotSpan());
        JS_ALWAYS_TRUE(nobj->setLastProperty(cx, last));
    } else {
        RootedId id(cx);
        Rooted<PropertyDescriptor> desc(cx);

        const unsigned AllowConfigure = JSPROP_IGNORE_ENUMERATE | JSPROP_IGNORE_READONLY |
                                        JSPROP_IGNORE_VALUE;
        const unsigned AllowConfigureAndWritable = AllowConfigure & ~JSPROP_IGNORE_READONLY;

        // 8.a/9.a. The two different loops are merged here.
        for (size_t i = 0; i < keys.length(); i++) {
            id = keys[i];

            if (level == IntegrityLevel::Sealed) {
                // 8.a.i.
                desc.setAttributes(AllowConfigure | JSPROP_PERMANENT);
            } else {
                // 9.a.i-ii.
                Rooted<PropertyDescriptor> currentDesc(cx);
                if (!GetOwnPropertyDescriptor(cx, obj, id, &currentDesc))
                    return false;

                // 9.a.iii.
                if (!currentDesc.object())
                    continue;

                // 9.a.iii.1-2
                if (currentDesc.isAccessorDescriptor())
                    desc.setAttributes(AllowConfigure | JSPROP_PERMANENT);
                else
                    desc.setAttributes(AllowConfigureAndWritable | JSPROP_PERMANENT | JSPROP_READONLY);
            }

            desc.object().set(obj);

            // 8.a.i-ii. / 9.a.iii.3-4
            if (!StandardDefineProperty(cx, obj, id, desc))
                return false;
        }
    }

    // Ordinarily ArraySetLength handles this, but we're going behind its back
    // right now, so we must do this manually.  Neither the custom property
    // tree mutations nor the StandardDefineProperty call in the above code will
    // do this for us.
    //
    // ArraySetLength also implements the capacity <= length invariant for
    // arrays with non-writable length.  We don't need to do anything special
    // for that, because capacity was zeroed out by preventExtensions.  (See
    // the assertion before the if-else above.)
    if (level == IntegrityLevel::Frozen && obj->is<ArrayObject>()) {
        if (!obj->as<ArrayObject>().maybeCopyElementsForWrite(cx))
            return false;
        obj->as<ArrayObject>().getElementsHeader()->setNonwritableArrayLength();
    }

    return true;
}

// ES6 draft rev33 (12 Feb 2015) 7.3.15
bool
js::TestIntegrityLevel(JSContext* cx, HandleObject obj, IntegrityLevel level, bool* result)
{
    // Steps 3-6. (Steps 1-2 are redundant assertions.)
    bool status;
    if (!IsExtensible(cx, obj, &status))
        return false;
    if (status) {
        *result = false;
        return true;
    }

    // Steps 7-8.
    AutoIdVector props(cx);
    if (!GetPropertyKeys(cx, obj, JSITER_HIDDEN | JSITER_OWNONLY | JSITER_SYMBOLS, &props))
        return false;

    // Step 9.
    RootedId id(cx);
    Rooted<PropertyDescriptor> desc(cx);
    for (size_t i = 0, len = props.length(); i < len; i++) {
        id = props[i];

        // Steps 9.a-b.
        if (!GetOwnPropertyDescriptor(cx, obj, id, &desc))
            return false;

        // Step 9.c.
        if (!desc.object())
            continue;

        // Steps 9.c.i-ii.
        if (desc.configurable() ||
            (level == IntegrityLevel::Frozen && desc.isDataDescriptor() && desc.writable()))
        {
            *result = false;
            return true;
        }
    }

    // Step 10.
    *result = true;
    return true;
}


/* * */

/*
 * Get the GC kind to use for scripted 'new' on the given class.
 * FIXME bug 547327: estimate the size from the allocation site.
 */
static inline gc::AllocKind
NewObjectGCKind(const js::Class *clasp)
{
    if (clasp == &ArrayObject::class_)
        return gc::AllocKind::OBJECT8;
    if (clasp == &JSFunction::class_)
        return gc::AllocKind::OBJECT2;
    return gc::AllocKind::OBJECT4;
}

static inline JSObject *
NewObject(ExclusiveContext *cx, HandleObjectGroup group, gc::AllocKind kind,
          NewObjectKind newKind)
{
    const Class *clasp = group->clasp();

    MOZ_ASSERT(clasp != &ArrayObject::class_);
    MOZ_ASSERT_IF(clasp == &JSFunction::class_,
                  kind == JSFunction::FinalizeKind || kind == JSFunction::ExtendedFinalizeKind);

    // For objects which can have fixed data following the object, only use
    // enough fixed slots to cover the number of reserved slots in the object,
    // regardless of the allocation kind specified.
    size_t nfixed = ClassCanHaveFixedData(clasp)
                    ? GetGCKindSlots(gc::GetGCObjectKind(clasp), clasp)
                    : GetGCKindSlots(kind, clasp);

    RootedShape shape(cx, EmptyShape::getInitialShape(cx, clasp, group->proto(), nfixed));
    if (!shape)
        return nullptr;

    gc::InitialHeap heap = GetInitialHeap(newKind, clasp);
    JSObject* obj = JSObject::create(cx, kind, heap, shape, group);
    if (!obj)
        return nullptr;

    if (newKind == SingletonObject) {
        RootedObject nobj(cx, obj);
        if (!JSObject::setSingleton(cx, nobj))
            return nullptr;
        obj = nobj;
    }

    bool globalWithoutCustomTrace = clasp->trace == JS_GlobalObjectTraceHook &&
                                    !cx->compartment()->options().getTrace();
    if (clasp->trace && !globalWithoutCustomTrace)
        MOZ_RELEASE_ASSERT(clasp->flags & JSCLASS_IMPLEMENTS_BARRIERS);

    probes::CreateObject(cx, obj);
    return obj;
}

void
NewObjectCache::fillProto(EntryIndex entry, const Class* clasp, js::TaggedProto proto,
                          gc::AllocKind kind, NativeObject *obj)
{
    MOZ_ASSERT_IF(proto.isObject(), !proto.toObject()->is<GlobalObject>());
    MOZ_ASSERT(obj->getTaggedProto() == proto);
    return fill(entry, clasp, proto.raw(), kind, obj);
}

static bool
NewObjectWithTaggedProtoIsCachable(ExclusiveContext *cxArg, Handle<TaggedProto> proto,
                                   NewObjectKind newKind, const Class *clasp)
{
    return cxArg->isJSContext() &&
           proto.isObject() &&
           newKind == GenericObject &&
           clasp->isNative() &&
           !proto.toObject()->is<GlobalObject>();
}

JSObject *
js::NewObjectWithGivenTaggedProto(ExclusiveContext *cxArg, const Class *clasp,
                                  Handle<TaggedProto> proto,
                                  gc::AllocKind allocKind, NewObjectKind newKind)
{
    if (CanBeFinalizedInBackground(allocKind, clasp))
        allocKind = GetBackgroundAllocKind(allocKind);

    bool isCachable = NewObjectWithTaggedProtoIsCachable(cxArg, proto, newKind, clasp);
    if (isCachable) {
        JSContext* cx = cxArg->asJSContext();
        JSRuntime* rt = cx->runtime();
        NewObjectCache& cache = rt->newObjectCache;
        NewObjectCache::EntryIndex entry = -1;
        if (cache.lookupProto(clasp, proto.toObject(), allocKind, &entry)) {
            JSObject* obj = cache.newObjectFromHit(cx, entry, GetInitialHeap(newKind, clasp));
            if (obj)
                return obj;
        }
    }

    RootedObjectGroup group(cxArg, ObjectGroup::defaultNewGroup(cxArg, clasp, proto, nullptr));
    if (!group)
        return nullptr;

    RootedObject obj(cxArg, NewObject(cxArg, group, allocKind, newKind));
    if (!obj)
        return nullptr;

    if (isCachable && !obj->as<NativeObject>().hasDynamicSlots()) {
        NewObjectCache& cache = cxArg->asJSContext()->runtime()->newObjectCache;
        NewObjectCache::EntryIndex entry = -1;
        cache.lookupProto(clasp, proto.toObject(), allocKind, &entry);
        cache.fillProto(entry, clasp, proto, allocKind, &obj->as<NativeObject>());
    }

    return obj;
}

static JSProtoKey
ClassProtoKeyOrAnonymousOrNull(const js::Class* clasp)
{
    JSProtoKey key = JSCLASS_CACHED_PROTO_KEY(clasp);
    if (key != JSProto_Null)
        return key;
    if (clasp->flags & JSCLASS_IS_ANONYMOUS)
        return JSProto_Object;
    return JSProto_Null;
}

static inline bool
NativeGetPureInline(NativeObject* pobj, Shape* shape, Value* vp)
{
    if (shape->hasSlot()) {
        *vp = pobj->getSlot(shape->slot());
        MOZ_ASSERT(!vp->isMagic());
    } else {
        vp->setUndefined();
    }

    /* Fail if we have a custom getter. */
    return shape->hasDefaultGetter();
}

static bool
FindClassPrototype(ExclusiveContext* cx, MutableHandleObject protop, const Class* clasp)
{
    protop.set(nullptr);

    JSAtom* atom = Atomize(cx, clasp->name, strlen(clasp->name));
    if (!atom)
        return false;
    RootedId id(cx, AtomToId(atom));

    RootedObject pobj(cx);
    RootedShape shape(cx);
    if (!NativeLookupProperty<CanGC>(cx, cx->global(), id, &pobj, &shape))
        return false;

    RootedObject ctor(cx);
    if (shape && pobj->isNative()) {
        if (shape->hasSlot()) {
            RootedValue v(cx, pobj->as<NativeObject>().getSlot(shape->slot()));
            if (v.isObject())
                ctor = &v.toObject();
        }
    }

    if (ctor && ctor->is<JSFunction>()) {
        JSFunction* nctor = &ctor->as<JSFunction>();
        RootedValue v(cx);
        if (cx->isJSContext()) {
            if (!GetProperty(cx->asJSContext(), ctor, ctor, cx->names().prototype, &v))
                return false;
        } else {
            Shape* shape = nctor->lookup(cx, cx->names().prototype);
            if (!shape || !NativeGetPureInline(nctor, shape, v.address()))
                return false;
        }
        if (v.isObject())
            protop.set(&v.toObject());
    }
    return true;
}

// Find the appropriate proto for a class. There are three different ways to achieve this:
// 1. Built-in classes have a cached proto and anonymous classes get Object.prototype.
// 2. Lookup global[clasp->name].prototype
// 3. Fallback to Object.prototype
//
// Step 2 is in some circumstances an observable operation, which is probably wrong
// as a matter of specifications. It's legacy garbage that we're working to remove eventually.
static bool
FindProto(ExclusiveContext* cx, const js::Class* clasp, MutableHandleObject proto)
{
    JSProtoKey protoKey = ClassProtoKeyOrAnonymousOrNull(clasp);
    if (protoKey != JSProto_Null)
        return GetBuiltinPrototype(cx, protoKey, proto);

    if (!FindClassPrototype(cx, proto, clasp))
        return false;

    if (!proto) {
        // We're looking for the prototype of a class that is currently being
        // resolved; the global object's resolve hook is on the
        // stack. js::FindClassPrototype detects this goofy case and returns
        // true with proto null. Fall back on Object.prototype.
        MOZ_ASSERT(JSCLASS_CACHED_PROTO_KEY(clasp) == JSProto_Null);
        return GetBuiltinPrototype(cx, JSProto_Object, proto);
    }
    return true;
}

static bool
NewObjectWithClassProtoIsCachable(ExclusiveContext *cxArg,
                                  JSProtoKey protoKey, NewObjectKind newKind, const Class *clasp)
{
    return cxArg->isJSContext() &&
           protoKey != JSProto_Null &&
           newKind == GenericObject &&
           clasp->isNative();
}

JSObject *
js::NewObjectWithClassProtoCommon(ExclusiveContext *cxArg, const Class *clasp,
                                  HandleObject protoArg,
                                  gc::AllocKind allocKind, NewObjectKind newKind)
{
    if (protoArg) {
        return NewObjectWithGivenTaggedProto(cxArg, clasp, AsTaggedProto(protoArg),
                                             allocKind, newKind);
    }

    if (CanBeFinalizedInBackground(allocKind, clasp))
        allocKind = GetBackgroundAllocKind(allocKind);

    Handle<GlobalObject*> global = cxArg->global();

    /*
     * Use the object cache, except for classes without a cached proto key.
     * On these objects, FindProto will do a dynamic property lookup to get
     * global[className].prototype, where changes to either the className or
     * prototype property would render the cached lookup incorrect. For classes
     * with a proto key, the prototype created during class initialization is
     * stored in an immutable slot on the global (except for ClearScope, which
     * will flush the new object cache).
     */
    JSProtoKey protoKey = ClassProtoKeyOrAnonymousOrNull(clasp);

    bool isCachable = NewObjectWithClassProtoIsCachable(cxArg, protoKey, newKind, clasp);
    if (isCachable) {
        JSContext *cx = cxArg->asJSContext();
        JSRuntime *rt = cx->runtime();
        NewObjectCache &cache = rt->newObjectCache;
        NewObjectCache::EntryIndex entry = -1;
        if (cache.lookupGlobal(clasp, global, allocKind, &entry)) {
            JSObject *obj = cache.newObjectFromHit(cx, entry, GetInitialHeap(newKind, clasp));
            if (obj)
                return obj;
        }
    }

    RootedObject proto(cxArg, protoArg);
    if (!FindProto(cxArg, clasp, &proto))
        return nullptr;

    Rooted<TaggedProto> taggedProto(cxArg, TaggedProto(proto));
    RootedObjectGroup group(cxArg, ObjectGroup::defaultNewGroup(cxArg, clasp, taggedProto));
    if (!group)
        return nullptr;

    JSObject *obj = NewObject(cxArg, group, allocKind, newKind);
    if (!obj)
        return nullptr;

    if (isCachable && !obj->as<NativeObject>().hasDynamicSlots()) {
        NewObjectCache &cache = cxArg->asJSContext()->runtime()->newObjectCache;
        NewObjectCache::EntryIndex entry = -1;
        cache.lookupGlobal(clasp, global, allocKind, &entry);
        cache.fillGlobal(entry, clasp, global, allocKind,
                         &obj->as<NativeObject>());
    }

    return obj;
}

static bool
NewObjectWithGroupIsCachable(ExclusiveContext *cx, HandleObjectGroup group,
                             NewObjectKind newKind)
{
    return group->proto().isObject() &&
           newKind == GenericObject &&
           group->clasp()->isNative() &&
           (!group->newScript() || group->newScript()->analyzed()) &&
           cx->isJSContext();
}

/*
 * Create a plain object with the specified group. This bypasses getNewGroup to
 * avoid losing creation site information for objects made by scripted 'new'.
 */
JSObject *
js::NewObjectWithGroupCommon(ExclusiveContext *cx, HandleObjectGroup group,
                             gc::AllocKind allocKind, NewObjectKind newKind)
{
    MOZ_ASSERT(allocKind <= gc::AllocKind::OBJECT_LAST);
    if (CanBeFinalizedInBackground(allocKind, group->clasp()))
        allocKind = GetBackgroundAllocKind(allocKind);

    bool isCachable = NewObjectWithGroupIsCachable(cx, group, newKind);
    if (isCachable) {
        NewObjectCache &cache = cx->asJSContext()->runtime()->newObjectCache;
        NewObjectCache::EntryIndex entry = -1;
        if (cache.lookupGroup(group, allocKind, &entry)) {
            JSObject *obj = cache.newObjectFromHit(cx->asJSContext(), entry,
                                                   GetInitialHeap(newKind, group->clasp()));
            if (obj)
                return obj;
        }
    }

    JSObject *obj = NewObject(cx, group, allocKind, newKind);
    if (!obj)
        return nullptr;

    if (isCachable && !obj->as<NativeObject>().hasDynamicSlots()) {
        NewObjectCache &cache = cx->asJSContext()->runtime()->newObjectCache;
        NewObjectCache::EntryIndex entry = -1;
        cache.lookupGroup(group, allocKind, &entry);
        cache.fillGroup(entry, group, allocKind, &obj->as<NativeObject>());
    }

    return obj;
}

bool
js::NewObjectScriptedCall(JSContext* cx, MutableHandleObject pobj)
{
    jsbytecode* pc;
    RootedScript script(cx, cx->currentScript(&pc));
    gc::AllocKind allocKind = NewObjectGCKind(&PlainObject::class_);
    NewObjectKind newKind = GenericObject;
    if (script && ObjectGroup::useSingletonForAllocationSite(script, pc, &PlainObject::class_))
        newKind = SingletonObject;
    RootedObject obj(cx, NewBuiltinClassInstance<PlainObject>(cx, allocKind, newKind));
    if (!obj)
        return false;

    if (script) {
        /* Try to specialize the group of the object to the scripted call site. */
        if (!ObjectGroup::setAllocationSiteObjectGroup(cx, script, pc, obj, newKind == SingletonObject))
            return false;
    }

    pobj.set(obj);
    return true;
}

JSObject *
js::CreateThis(JSContext *cx, const Class *newclasp, HandleObject callee)
{
    RootedValue protov(cx);
    if (!GetProperty(cx, callee, callee, cx->names().prototype, &protov))
        return nullptr;

    RootedObject proto(cx, protov.isObjectOrNull() ? protov.toObjectOrNull() : nullptr);
    gc::AllocKind kind = NewObjectGCKind(newclasp);
    return NewObjectWithClassProto(cx, newclasp, proto, kind);
}

static inline JSObject *
CreateThisForFunctionWithGroup(JSContext *cx, HandleObjectGroup group,
                               NewObjectKind newKind)
{
    if (group->maybeUnboxedLayout() && newKind != SingletonObject)
        return UnboxedPlainObject::create(cx, group, newKind);

    if (TypeNewScript *newScript = group->newScript()) {
        if (newScript->analyzed()) {
            // The definite properties analysis has been performed for this
            // group, so get the shape and alloc kind to use from the
            // TypeNewScript's template.
            RootedPlainObject templateObject(cx, newScript->templateObject());
            MOZ_ASSERT(templateObject->group() == group);

            RootedPlainObject res(cx, CopyInitializerObject(cx, templateObject, newKind));
            if (!res)
                return nullptr;

            if (newKind == SingletonObject) {
                Rooted<TaggedProto> proto(cx, TaggedProto(templateObject->getProto()));
                if (!res->splicePrototype(cx, &PlainObject::class_, proto))
                    return nullptr;
            } else {
                res->setGroup(group);
            }
            return res;
        }

        // The initial objects registered with a TypeNewScript can't be in the
        // nursery.
        if (newKind == GenericObject)
            newKind = MaybeSingletonObject;

        // Not enough objects with this group have been created yet, so make a
        // plain object and register it with the group. Use the maximum number
        // of fixed slots, as is also required by the TypeNewScript.
        gc::AllocKind allocKind = GuessObjectGCKind(NativeObject::MAX_FIXED_SLOTS);
        PlainObject *res = NewObjectWithGroup<PlainObject>(cx, group, allocKind, newKind);
        if (!res)
            return nullptr;

        // Make sure group->newScript is still there.
        if (newKind != SingletonObject && group->newScript())
            group->newScript()->registerNewObject(res);

        return res;
    }

    gc::AllocKind allocKind = NewObjectGCKind(&PlainObject::class_);

    if (newKind == SingletonObject) {
        Rooted<TaggedProto> protoRoot(cx, group->proto());
        return NewObjectWithGivenTaggedProto(cx, &PlainObject::class_, protoRoot, allocKind, newKind);
    }
    return NewObjectWithGroup<PlainObject>(cx, group, allocKind, newKind);
}

JSObject *
js::CreateThisForFunctionWithProto(JSContext *cx, HandleObject callee, HandleObject proto,
                                   NewObjectKind newKind /* = GenericObject */)
{
    RootedObject res(cx);

    if (proto) {
        RootedObjectGroup group(cx, ObjectGroup::defaultNewGroup(cx, nullptr, TaggedProto(proto),
                                                                 &callee->as<JSFunction>()));
        if (!group)
            return nullptr;

        if (group->newScript() && !group->newScript()->analyzed()) {
            bool regenerate;
            if (!group->newScript()->maybeAnalyze(cx, group, &regenerate))
                return nullptr;
            if (regenerate) {
                // The script was analyzed successfully and may have changed
                // the new type table, so refetch the group.
                group = ObjectGroup::defaultNewGroup(cx, nullptr, TaggedProto(proto),
                                                     &callee->as<JSFunction>());
                MOZ_ASSERT(group && group->newScript());
            }
        }

        res = CreateThisForFunctionWithGroup(cx, group, newKind);
    } else {
        gc::AllocKind allocKind = NewObjectGCKind(&PlainObject::class_);
        res = NewObjectWithProto<PlainObject>(cx, proto, allocKind, newKind);
    }

    if (res) {
        JSScript* script = callee->as<JSFunction>().getOrCreateScript(cx);
        if (!script)
            return nullptr;
        TypeScript::SetThis(cx, script, TypeSet::ObjectType(res));
    }

    return res;
}

JSObject*
js::CreateThisForFunction(JSContext* cx, HandleObject callee, NewObjectKind newKind)
{
    RootedValue protov(cx);
    if (!GetProperty(cx, callee, callee, cx->names().prototype, &protov))
        return nullptr;
    RootedObject proto(cx);
    if (protov.isObject())
        proto = &protov.toObject();
    JSObject* obj = CreateThisForFunctionWithProto(cx, callee, proto, newKind);

    if (obj && newKind == SingletonObject) {
        RootedPlainObject nobj(cx, &obj->as<PlainObject>());

        /* Reshape the singleton before passing it as the 'this' value. */
        NativeObject::clear(cx, nobj);

        JSScript* calleeScript = callee->as<JSFunction>().nonLazyScript();
        TypeScript::SetThis(cx, calleeScript, TypeSet::ObjectType(nobj));

        return nobj;
    }

    return obj;
}

/* static */ bool
JSObject::nonNativeSetProperty(JSContext* cx, HandleObject obj, HandleObject receiver,
                               HandleId id, MutableHandleValue vp, ObjectOpResult &result)
{
    if (MOZ_UNLIKELY(obj->watched())) {
        WatchpointMap* wpmap = cx->compartment()->watchpointMap;
        if (wpmap && !wpmap->triggerWatchpoint(cx, obj, id, vp))
            return false;
    }
    return obj->getOps()->setProperty(cx, obj, receiver, id, vp, result);
}

/* static */ bool
JSObject::nonNativeSetElement(JSContext* cx, HandleObject obj, HandleObject receiver,
                              uint32_t index, MutableHandleValue vp, ObjectOpResult &result)
{
    RootedId id(cx);
    if (!IndexToId(cx, index, &id))
        return false;
    return nonNativeSetProperty(cx, obj, receiver, id, vp, result);
}

JS_FRIEND_API(bool)
JS_CopyPropertyFrom(JSContext* cx, HandleId id, HandleObject target,
                    HandleObject obj, PropertyCopyBehavior copyBehavior)
{
    // |obj| and |cx| are generally not same-compartment with |target| here.
    assertSameCompartment(cx, obj, id);
    Rooted<JSPropertyDescriptor> desc(cx);

    if (!GetOwnPropertyDescriptor(cx, obj, id, &desc))
        return false;
    MOZ_ASSERT(desc.object());

    // Silently skip JSGetterOp/JSSetterOp-implemented accessors.
    if (desc.getter() && !desc.hasGetterObject())
        return true;
    if (desc.setter() && !desc.hasSetterObject())
        return true;

    if (copyBehavior == MakeNonConfigurableIntoConfigurable) {
        // Mask off the JSPROP_PERMANENT bit.
        desc.attributesRef() &= ~JSPROP_PERMANENT;
    }

    JSAutoCompartment ac(cx, target);
    RootedId wrappedId(cx, id);
    if (!cx->compartment()->wrap(cx, &desc))
        return false;

    return StandardDefineProperty(cx, target, wrappedId, desc);
}

JS_FRIEND_API(bool)
JS_CopyPropertiesFrom(JSContext* cx, HandleObject target, HandleObject obj)
{
    JSAutoCompartment ac(cx, obj);

    AutoIdVector props(cx);
    if (!GetPropertyKeys(cx, obj, JSITER_OWNONLY | JSITER_HIDDEN | JSITER_SYMBOLS, &props))
        return false;

    for (size_t i = 0; i < props.length(); ++i) {
        if (!JS_CopyPropertyFrom(cx, props[i], target, obj))
            return false;
    }

    return true;
}

static bool
CopyProxyObject(JSContext* cx, Handle<ProxyObject*> from, Handle<ProxyObject*> to)
{
    MOZ_ASSERT(from->getClass() == to->getClass());

    if (from->is<WrapperObject>() &&
        (Wrapper::wrapperHandler(from)->flags() &
         Wrapper::CROSS_COMPARTMENT))
    {
        to->setCrossCompartmentPrivate(GetProxyPrivate(from));
    } else {
        RootedValue v(cx, GetProxyPrivate(from));
        if (!cx->compartment()->wrap(cx, &v))
            return false;
        to->setSameCompartmentPrivate(v);
    }

    RootedValue v(cx);
    for (size_t n = 0; n < PROXY_EXTRA_SLOTS; n++) {
        v = GetProxyExtra(from, n);
        if (!cx->compartment()->wrap(cx, &v))
            return false;
        SetProxyExtra(to, n, v);
    }

    return true;
}

JSObject*
js::CloneObject(JSContext* cx, HandleObject obj, Handle<js::TaggedProto> proto)
{
    if (!obj->isNative() && !obj->is<ProxyObject>()) {
        JS_ReportErrorNumber(cx, GetErrorMessage, nullptr,
                             JSMSG_CANT_CLONE_OBJECT);
        return nullptr;
    }

    RootedObject clone(cx);
    if (obj->isNative()) {
        clone = NewObjectWithGivenTaggedProto(cx, obj->getClass(), proto);
        if (!clone)
            return nullptr;

        if (clone->is<JSFunction>() && (obj->compartment() != clone->compartment())) {
            JS_ReportErrorNumber(cx, GetErrorMessage, nullptr,
                                 JSMSG_CANT_CLONE_OBJECT);
            return nullptr;
        }

        if (obj->as<NativeObject>().hasPrivate())
            clone->as<NativeObject>().setPrivate(obj->as<NativeObject>().getPrivate());
    } else {
        ProxyOptions options;
        options.setClass(obj->getClass());

        clone = ProxyObject::New(cx, GetProxyHandler(obj), JS::NullHandleValue, proto, options);
        if (!clone)
            return nullptr;

        if (!CopyProxyObject(cx, obj.as<ProxyObject>(), clone.as<ProxyObject>()))
            return nullptr;
    }

    return clone;
}

static bool
GetScriptArrayObjectElements(JSContext *cx, HandleArrayObject obj, AutoValueVector &values)
{
    MOZ_ASSERT(!obj->isSingleton());

    if (obj->nonProxyIsExtensible()) {
        MOZ_ASSERT(obj->slotSpan() == 0);

        if (!values.appendN(MagicValue(JS_ELEMENTS_HOLE), obj->getDenseInitializedLength()))
            return false;

        for (size_t i = 0; i < obj->getDenseInitializedLength(); i++)
            values[i].set(obj->getDenseElement(i));
    } else {
        // Call site objects are frozen before they escape to script, which
        // converts their dense elements into data properties.

        for (Shape::Range<NoGC> r(obj->lastProperty()); !r.empty(); r.popFront()) {
            Shape &shape = r.front();
            if (shape.propid() == NameToId(cx->names().length))
                continue;
            MOZ_ASSERT(shape.isDataDescriptor());

            // The 'raw' property is added before freezing call site objects.
            // After an XDR or deep clone the script object will no longer be
            // frozen, and the two objects will be connected again the first
            // time the JSOP_CALLSITEOBJ executes.
            if (shape.propid() == NameToId(cx->names().raw))
                continue;

            uint32_t index = JSID_TO_INT(shape.propid());
            while (index >= values.length()) {
                if (!values.append(MagicValue(JS_ELEMENTS_HOLE)))
                    return false;
            }

            values[index].set(obj->getSlot(shape.slot()));
        }
    }

    return true;
}

static bool
GetScriptPlainObjectProperties(JSContext *cx, HandleObject obj, AutoIdValueVector &properties)
{
    if (obj->is<PlainObject>()) {
        PlainObject *nobj = &obj->as<PlainObject>();

        if (!properties.appendN(IdValuePair(), nobj->slotSpan()))
            return false;

        for (Shape::Range<NoGC> r(nobj->lastProperty()); !r.empty(); r.popFront()) {
            Shape &shape = r.front();
            MOZ_ASSERT(shape.isDataDescriptor());
            uint32_t slot = shape.slot();
            properties[slot].get().id = shape.propid();
            properties[slot].get().value = nobj->getSlot(slot);
        }

        for (size_t i = 0; i < nobj->getDenseInitializedLength(); i++) {
            Value v = nobj->getDenseElement(i);
            if (!v.isMagic(JS_ELEMENTS_HOLE) && !properties.append(IdValuePair(INT_TO_JSID(i), v)))
                return false;
        }

        return true;
    }

    if (obj->is<UnboxedPlainObject>()) {
        UnboxedPlainObject *nobj = &obj->as<UnboxedPlainObject>();

        const UnboxedLayout &layout = nobj->layout();
        if (!properties.appendN(IdValuePair(), layout.properties().length()))
            return false;

        for (size_t i = 0; i < layout.properties().length(); i++) {
            const UnboxedLayout::Property &property = layout.properties()[i];
            properties[i].get().id = NameToId(property.name);
            properties[i].get().value = nobj->getValue(property);
        }

        return true;
    }

    MOZ_CRASH("Bad object kind");
}

static bool
DeepCloneValue(JSContext *cx, Value *vp, NewObjectKind newKind)
{
    if (vp->isObject()) {
        RootedObject obj(cx, &vp->toObject());
        obj = DeepCloneObjectLiteral(cx, obj, newKind);
        if (!obj)
            return false;
        vp->setObject(*obj);
    }
    return true;
}

JSObject *
js::DeepCloneObjectLiteral(JSContext *cx, HandleObject obj, NewObjectKind newKind)
{
    /* NB: Keep this in sync with XDRObjectLiteral. */
    MOZ_ASSERT_IF(obj->isSingleton(),
                  JS::CompartmentOptionsRef(cx).getSingletonsAsTemplates());
    MOZ_ASSERT(obj->is<PlainObject>() || obj->is<UnboxedPlainObject>() || obj->is<ArrayObject>());
    MOZ_ASSERT(cx->isInsideCurrentCompartment(obj));
    MOZ_ASSERT(newKind != SingletonObject);

    if (obj->is<ArrayObject>()) {
        HandleArrayObject aobj = obj.as<ArrayObject>();

        AutoValueVector values(cx);
        if (!GetScriptArrayObjectElements(cx, aobj, values))
            return nullptr;

        // Deep clone any elements.
        uint32_t initialized = aobj->getDenseInitializedLength();
        for (uint32_t i = 0; i < initialized; ++i) {
            if (!DeepCloneValue(cx, values[i].address(), newKind))
                return nullptr;
        }

        RootedArrayObject clone(cx, NewDenseUnallocatedArray(cx, aobj->length(),
                                                             NullPtr(), newKind));
        if (!clone || !clone->ensureElements(cx, values.length()))
            return nullptr;

        clone->setDenseInitializedLength(values.length());
        clone->initDenseElements(0, values.begin(), values.length());

        if (aobj->denseElementsAreCopyOnWrite()) {
            if (!ObjectElements::MakeElementsCopyOnWrite(cx, clone))
                return nullptr;
        } else {
            ObjectGroup::fixArrayGroup(cx, &clone->as<ArrayObject>());
        }

        return clone;
    }

    AutoIdValueVector properties(cx);
    if (!GetScriptPlainObjectProperties(cx, obj, properties))
        return nullptr;

    for (size_t i = 0; i < properties.length(); i++) {
        if (!DeepCloneValue(cx, &properties[i].get().value, newKind))
            return nullptr;
    }

    if (obj->isSingleton())
        newKind = SingletonObject;

    return ObjectGroup::newPlainObject(cx, properties.begin(), properties.length(), newKind);
}

static bool
InitializePropertiesFromCompatibleNativeObject(JSContext *cx,
                                               HandleNativeObject dst,
                                               HandleNativeObject src)
{
    assertSameCompartment(cx, src, dst);
    MOZ_ASSERT(src->getClass() == dst->getClass());
    MOZ_ASSERT(dst->lastProperty()->getObjectFlags() == 0);
    MOZ_ASSERT(!src->isSingleton());
    MOZ_ASSERT(src->numFixedSlots() == dst->numFixedSlots());

    if (!dst->ensureElements(cx, src->getDenseInitializedLength()))
        return false;

    uint32_t initialized = src->getDenseInitializedLength();
    for (uint32_t i = 0; i < initialized; ++i) {
        dst->setDenseInitializedLength(i + 1);
        dst->initDenseElement(i, src->getDenseElement(i));
    }

    MOZ_ASSERT(!src->hasPrivate());
    RootedShape shape(cx);
    if (src->getProto() == dst->getProto()) {
        shape = src->lastProperty();
    } else {
        // We need to generate a new shape for dst that has dst's proto but all
        // the property information from src.  Note that we asserted above that
        // dst's object flags are 0.
        shape = EmptyShape::getInitialShape(cx, dst->getClass(), dst->getTaggedProto(),
                                            dst->numFixedSlots(), 0);
        if (!shape)
            return false;

        // Get an in-order list of the shapes in the src object.
        AutoShapeVector shapes(cx);
        for (Shape::Range<NoGC> r(src->lastProperty()); !r.empty(); r.popFront()) {
            if (!shapes.append(&r.front()))
                return false;
        }
        Reverse(shapes.begin(), shapes.end());

        for (size_t i = 0; i < shapes.length(); i++) {
            StackShape unrootedChild(shapes[i]);
            RootedGeneric<StackShape*> child(cx, &unrootedChild);
            shape = cx->compartment()->propertyTree.getChild(cx, shape, *child);
            if (!shape)
                return false;
        }
    }
    size_t span = shape->slotSpan();
    if (!dst->setLastProperty(cx, shape))
        return false;
    for (size_t i = JSCLASS_RESERVED_SLOTS(src->getClass()); i < span; i++)
        dst->setSlot(i, src->getSlot(i));

    return true;
}

JS_FRIEND_API(bool)
JS_InitializePropertiesFromCompatibleNativeObject(JSContext *cx,
                                                  HandleObject dst,
                                                  HandleObject src)
{
    return InitializePropertiesFromCompatibleNativeObject(cx,
                                                          dst.as<NativeObject>(),
                                                          src.as<NativeObject>());
}

template<XDRMode mode>
bool
js::XDRObjectLiteral(XDRState<mode> *xdr, MutableHandleObject obj)
{
    /* NB: Keep this in sync with DeepCloneObjectLiteral. */

    JSContext *cx = xdr->cx();
    MOZ_ASSERT_IF(mode == XDR_ENCODE && obj->isSingleton(),
                  JS::CompartmentOptionsRef(cx).getSingletonsAsTemplates());

    // Distinguish between objects and array classes.
    uint32_t isArray = 0;
    {
        if (mode == XDR_ENCODE) {
            MOZ_ASSERT(obj->is<PlainObject>() ||
                       obj->is<UnboxedPlainObject>() ||
                       obj->is<ArrayObject>());
            isArray = obj->is<ArrayObject>() ? 1 : 0;
        }

        if (!xdr->codeUint32(&isArray))
            return false;
    }

    RootedValue tmpValue(cx), tmpIdValue(cx);
    RootedId tmpId(cx);

    if (isArray) {
        uint32_t length;
        RootedArrayObject aobj(cx);

        if (mode == XDR_ENCODE) {
            aobj = &obj->as<ArrayObject>();
            length = aobj->length();
        }

        if (!xdr->codeUint32(&length))
            return false;

        if (mode == XDR_DECODE) {
            obj.set(NewDenseUnallocatedArray(cx, length, NullPtr(), TenuredObject));
            if (!obj)
                return false;
            aobj = &obj->as<ArrayObject>();
        }

        AutoValueVector values(cx);
        if (mode == XDR_ENCODE && !GetScriptArrayObjectElements(cx, aobj, values))
            return false;

        uint32_t initialized;
        {
            if (mode == XDR_ENCODE)
                initialized = values.length();
            if (!xdr->codeUint32(&initialized))
                return false;
            if (mode == XDR_DECODE) {
                if (initialized) {
                    if (!aobj->ensureElements(cx, initialized))
                        return false;
                }
            }
        }

        // Recursively copy dense elements.
        for (unsigned i = 0; i < initialized; i++) {
            if (mode == XDR_ENCODE)
                tmpValue = values[i];

            if (!xdr->codeConstValue(&tmpValue))
                return false;

            if (mode == XDR_DECODE) {
                aobj->setDenseInitializedLength(i + 1);
                aobj->initDenseElement(i, tmpValue);
            }
        }

        uint32_t copyOnWrite;
        if (mode == XDR_ENCODE)
            copyOnWrite = aobj->denseElementsAreCopyOnWrite();
        if (!xdr->codeUint32(&copyOnWrite))
            return false;

        if (mode == XDR_DECODE) {
            if (copyOnWrite) {
                if (!ObjectElements::MakeElementsCopyOnWrite(cx, aobj))
                    return false;
            } else {
                ObjectGroup::fixArrayGroup(cx, aobj);
            }
        }

        return true;
    }

    // Code the properties in the object.
    AutoIdValueVector properties(cx);
    if (mode == XDR_ENCODE && !GetScriptPlainObjectProperties(cx, obj, properties))
        return false;

    uint32_t nproperties = properties.length();
    if (!xdr->codeUint32(&nproperties))
        return false;

    if (mode == XDR_DECODE && !properties.appendN(IdValuePair(), nproperties))
        return false;

    for (size_t i = 0; i < nproperties; i++) {
        if (mode == XDR_ENCODE) {
            tmpIdValue = IdToValue(properties[i].get().id);
            tmpValue = properties[i].get().value;
        }

        if (!xdr->codeConstValue(&tmpIdValue) || !xdr->codeConstValue(&tmpValue))
            return false;

        if (mode == XDR_DECODE) {
            if (!ValueToId<CanGC>(cx, tmpIdValue, &tmpId))
                return false;
            properties[i].get().id = tmpId;
            properties[i].get().value = tmpValue;
        }
    }

    // Code whether the object is a singleton.
    uint32_t isSingleton;
    if (mode == XDR_ENCODE)
        isSingleton = obj->isSingleton() ? 1 : 0;
    if (!xdr->codeUint32(&isSingleton))
        return false;

    if (mode == XDR_DECODE) {
        NewObjectKind newKind = isSingleton ? SingletonObject : TenuredObject;
        obj.set(ObjectGroup::newPlainObject(cx, properties.begin(), properties.length(), newKind));
        if (!obj)
            return false;
    }

    return true;
}

template bool
js::XDRObjectLiteral(XDRState<XDR_ENCODE> *xdr, MutableHandleObject obj);

template bool
js::XDRObjectLiteral(XDRState<XDR_DECODE> *xdr, MutableHandleObject obj);

JSObject *
js::CloneObjectLiteral(JSContext *cx, HandleObject srcObj)
{
    if (srcObj->is<PlainObject>()) {
        AllocKind kind = GetBackgroundAllocKind(GuessObjectGCKind(srcObj->as<PlainObject>().numFixedSlots()));
        MOZ_ASSERT_IF(srcObj->isTenured(), kind == srcObj->asTenured().getAllocKind());

        RootedObject proto(cx, cx->global()->getOrCreateObjectPrototype(cx));
        if (!proto)
            return nullptr;
        RootedObjectGroup group(cx, ObjectGroup::defaultNewGroup(cx, &PlainObject::class_,
                                                                 TaggedProto(proto)));
        if (!group)
            return nullptr;

        RootedPlainObject res(cx, NewObjectWithGroup<PlainObject>(cx, group, kind,
                                                                  MaybeSingletonObject));
        if (!res)
            return nullptr;

        // XXXbz Do we still need the reshape here?  We got "kind" off the
        // srcObj, no?  See bug 1143270.
        RootedShape newShape(cx, ReshapeForAllocKind(cx, srcObj->as<PlainObject>().lastProperty(),
                                                     TaggedProto(proto), kind));
        if (!newShape || !res->setLastProperty(cx, newShape))
            return nullptr;

        return res;
    }

    RootedArrayObject srcArray(cx, &srcObj->as<ArrayObject>());
    MOZ_ASSERT(srcArray->denseElementsAreCopyOnWrite());
    MOZ_ASSERT(srcArray->getElementsHeader()->ownerObject() == srcObj);

    size_t length = srcArray->as<ArrayObject>().length();
    RootedArrayObject res(cx, NewDenseFullyAllocatedArray(cx, length, NullPtr(),
                                                          MaybeSingletonObject));
    if (!res)
        return nullptr;

    RootedId id(cx);
    RootedValue value(cx);
    for (size_t i = 0; i < length; i++) {
        // The only markable values in copy on write arrays are atoms, which
        // can be freely copied between compartments.
        value = srcArray->getDenseElement(i);
        MOZ_ASSERT_IF(value.isMarkable(),
                      value.toGCThing()->isTenured() &&
                      cx->runtime()->isAtomsZone(value.toGCThing()->asTenured().zoneFromAnyThread()));

        id = INT_TO_JSID(i);
        if (!DefineProperty(cx, res, id, value, nullptr, nullptr, JSPROP_ENUMERATE))
            return nullptr;
    }

    if (!ObjectElements::MakeElementsCopyOnWrite(cx, res))
        return nullptr;

    return res;
}

void
NativeObject::fillInAfterSwap(JSContext* cx, const Vector<Value>& values, void* priv)
{
    // This object has just been swapped with some other object, and its shape
    // no longer reflects its allocated size. Correct this information and
    // fill the slots in with the specified values.
    MOZ_ASSERT(slotSpan() == values.length());

    // Make sure the shape's numFixedSlots() is correct.
    size_t nfixed = gc::GetGCKindSlots(asTenured().getAllocKind(), getClass());
    if (nfixed != shape_->numFixedSlots()) {
        if (!generateOwnShape(cx))
            CrashAtUnhandlableOOM("fillInAfterSwap");
        shape_->setNumFixedSlots(nfixed);
    }

    if (hasPrivate())
        setPrivate(priv);
    else
        MOZ_ASSERT(!priv);

    if (slots_) {
        js_free(slots_);
        slots_ = nullptr;
    }

    if (size_t ndynamic = dynamicSlotsCount(nfixed, values.length(), getClass())) {
        slots_ = cx->zone()->pod_malloc<HeapSlot>(ndynamic);
        if (!slots_)
            CrashAtUnhandlableOOM("fillInAfterSwap");
        Debug_SetSlotRangeToCrashOnTouch(slots_, ndynamic);
    }

    initSlotRange(0, values.begin(), values.length());
}

void
JSObject::fixDictionaryShapeAfterSwap()
{
    // Dictionary shapes can point back to their containing objects, so after
    // swapping the guts of those objects fix the pointers up.
    if (isNative() && as<NativeObject>().inDictionaryMode())
        as<NativeObject>().shape_->listp = &as<NativeObject>().shape_;
}

/* Use this method with extreme caution. It trades the guts of two objects. */
bool
JSObject::swap(JSContext* cx, HandleObject a, HandleObject b)
{
    // Ensure swap doesn't cause a finalizer to not be run.
    MOZ_ASSERT(IsBackgroundFinalized(a->asTenured().getAllocKind()) ==
               IsBackgroundFinalized(b->asTenured().getAllocKind()));
    MOZ_ASSERT(a->compartment() == b->compartment());

    AutoCompartment ac(cx, a);

    if (!a->getGroup(cx))
        CrashAtUnhandlableOOM("JSObject::swap");
    if (!b->getGroup(cx))
        CrashAtUnhandlableOOM("JSObject::swap");

    /*
     * Neither object may be in the nursery, but ensure we update any embedded
     * nursery pointers in either object.
     */
    MOZ_ASSERT(!IsInsideNursery(a) && !IsInsideNursery(b));
    cx->runtime()->gc.storeBuffer.putWholeCellFromMainThread(a);
    cx->runtime()->gc.storeBuffer.putWholeCellFromMainThread(b);

    unsigned r = NotifyGCPreSwap(a, b);

    // Do the fundamental swapping of the contents of two objects.
    MOZ_ASSERT(a->compartment() == b->compartment());
    MOZ_ASSERT(a->is<JSFunction>() == b->is<JSFunction>());

    // Don't try to swap functions with different sizes.
    MOZ_ASSERT_IF(a->is<JSFunction>(), a->tenuredSizeOfThis() == b->tenuredSizeOfThis());

    // Watch for oddball objects that have special organizational issues and
    // can't be swapped.
    MOZ_ASSERT(!a->is<RegExpObject>() && !b->is<RegExpObject>());
    MOZ_ASSERT(!a->is<ArrayObject>() && !b->is<ArrayObject>());
    MOZ_ASSERT(!a->is<ArrayBufferObject>() && !b->is<ArrayBufferObject>());
    MOZ_ASSERT(!a->is<TypedArrayObject>() && !b->is<TypedArrayObject>());
    MOZ_ASSERT(!a->is<TypedObject>() && !b->is<TypedObject>());

    if (a->tenuredSizeOfThis() == b->tenuredSizeOfThis()) {
        // When both objects are the same size, just do a plain swap of their
        // contents.
        size_t size = a->tenuredSizeOfThis();

        char tmp[mozilla::tl::Max<sizeof(JSFunction), sizeof(JSObject_Slots16)>::value];
        MOZ_ASSERT(size <= sizeof(tmp));

        js_memcpy(tmp, a, size);
        js_memcpy(a, b, size);
        js_memcpy(b, tmp, size);

        a->fixDictionaryShapeAfterSwap();
        b->fixDictionaryShapeAfterSwap();
    } else {
        // Avoid GC in here to avoid confusing the tracing code with our
        // intermediate state.
        AutoSuppressGC suppress(cx);

        // When the objects have different sizes, they will have different
        // numbers of fixed slots before and after the swap, so the slots for
        // native objects will need to be rearranged.
        NativeObject* na = a->isNative() ? &a->as<NativeObject>() : nullptr;
        NativeObject* nb = b->isNative() ? &b->as<NativeObject>() : nullptr;

        // Remember the original values from the objects.
        Vector<Value> avals(cx);
        void* apriv = nullptr;
        if (na) {
            apriv = na->hasPrivate() ? na->getPrivate() : nullptr;
            for (size_t i = 0; i < na->slotSpan(); i++) {
                if (!avals.append(na->getSlot(i)))
                    CrashAtUnhandlableOOM("JSObject::swap");
            }
        }
        Vector<Value> bvals(cx);
        void* bpriv = nullptr;
        if (nb) {
            bpriv = nb->hasPrivate() ? nb->getPrivate() : nullptr;
            for (size_t i = 0; i < nb->slotSpan(); i++) {
                if (!bvals.append(nb->getSlot(i)))
                    CrashAtUnhandlableOOM("JSObject::swap");
            }
        }

        // Swap the main fields of the objects, whether they are native objects or proxies.
        char tmp[sizeof(JSObject_Slots0)];
        js_memcpy(&tmp, a, sizeof tmp);
        js_memcpy(a, b, sizeof tmp);
        js_memcpy(b, &tmp, sizeof tmp);

        a->fixDictionaryShapeAfterSwap();
        b->fixDictionaryShapeAfterSwap();

        if (na)
            b->as<NativeObject>().fillInAfterSwap(cx, avals, apriv);
        if (nb)
            a->as<NativeObject>().fillInAfterSwap(cx, bvals, bpriv);
    }

    // Swapping the contents of two objects invalidates type sets which contain
    // either of the objects, so mark all such sets as unknown.
    MarkObjectGroupUnknownProperties(cx, a->group());
    MarkObjectGroupUnknownProperties(cx, b->group());

    /*
     * We need a write barrier here. If |a| was marked and |b| was not, then
     * after the swap, |b|'s guts would never be marked. The write barrier
     * solves this.
     *
     * Normally write barriers happen before the write. However, that's not
     * necessary here because nothing is being destroyed. We're just swapping.
     */
    JS::Zone *zone = a->zone();
    if (zone->needsIncrementalBarrier()) {
        a->markChildren(zone->barrierTracer());
        b->markChildren(zone->barrierTracer());
    }

    NotifyGCPostSwap(a, b, r);
    return true;
}

static bool
DefineStandardSlot(JSContext* cx, HandleObject obj, JSProtoKey key, JSAtom* atom,
                   HandleValue v, uint32_t attrs, bool& named)
{
    RootedId id(cx, AtomToId(atom));

    if (key != JSProto_Null) {
        /*
         * Initializing an actual standard class on a global object. If the
         * property is not yet present, force it into a new one bound to a
         * reserved slot. Otherwise, go through the normal property path.
         */
        Rooted<GlobalObject*> global(cx, &obj->as<GlobalObject>());

        if (!global->lookup(cx, id)) {
            global->setConstructorPropertySlot(key, v);

            uint32_t slot = GlobalObject::constructorPropertySlot(key);
            if (!NativeObject::addProperty(cx, global, id, nullptr, nullptr, slot, attrs, 0))
                return false;

            named = true;
            return true;
        }
    }

    named = DefineProperty(cx, obj, id, v, nullptr, nullptr, attrs);
    return named;
}

static void
SetClassObject(JSObject* obj, JSProtoKey key, JSObject* cobj, JSObject* proto)
{
    if (!obj->is<GlobalObject>())
        return;

    obj->as<GlobalObject>().setConstructor(key, ObjectOrNullValue(cobj));
    obj->as<GlobalObject>().setPrototype(key, ObjectOrNullValue(proto));
}

static void
ClearClassObject(JSObject* obj, JSProtoKey key)
{
    if (!obj->is<GlobalObject>())
        return;

    obj->as<GlobalObject>().setConstructor(key, UndefinedValue());
    obj->as<GlobalObject>().setPrototype(key, UndefinedValue());
}

static NativeObject*
DefineConstructorAndPrototype(JSContext* cx, HandleObject obj, JSProtoKey key, HandleAtom atom,
                              HandleObject protoProto, const Class* clasp,
                              Native constructor, unsigned nargs,
                              const JSPropertySpec* ps, const JSFunctionSpec* fs,
                              const JSPropertySpec* static_ps, const JSFunctionSpec* static_fs,
                              NativeObject** ctorp, AllocKind ctorKind)
{
    /*
     * Create a prototype object for this class.
     *
     * FIXME: lazy standard (built-in) class initialization and even older
     * eager boostrapping code rely on all of these properties:
     *
     * 1. NewObject attempting to compute a default prototype object when
     *    passed null for proto; and
     *
     * 2. NewObject tolerating no default prototype (null proto slot value)
     *    due to this js::InitClass call coming from js::InitFunctionClass on an
     *    otherwise-uninitialized global.
     *
     * 3. NewObject allocating a JSFunction-sized GC-thing when clasp is
     *    &JSFunction::class_, not a JSObject-sized (smaller) GC-thing.
     *
     * The JS_NewObjectForGivenProto and JS_NewObject APIs also allow clasp to
     * be &JSFunction::class_ (we could break compatibility easily). But
     * fixing (3) is not enough without addressing the bootstrapping dependency
     * on (1) and (2).
     */

    /*
     * Create the prototype object.  (GlobalObject::createBlankPrototype isn't
     * used because it won't let us use protoProto as the proto.
     */
    RootedNativeObject proto(cx, NewNativeObjectWithClassProto(cx, clasp, protoProto, SingletonObject));
    if (!proto)
        return nullptr;

    /* After this point, control must exit via label bad or out. */
    RootedNativeObject ctor(cx);
    bool named = false;
    bool cached = false;
    if (!constructor) {
        /*
         * Lacking a constructor, name the prototype (e.g., Math) unless this
         * class (a) is anonymous, i.e. for internal use only; (b) the class
         * of obj (the global object) is has a reserved slot indexed by key;
         * and (c) key is not the null key.
         */
        if (!(clasp->flags & JSCLASS_IS_ANONYMOUS) || !obj->is<GlobalObject>() ||
            key == JSProto_Null)
        {
            uint32_t attrs = (clasp->flags & JSCLASS_IS_ANONYMOUS)
                           ? JSPROP_READONLY | JSPROP_PERMANENT
                           : 0;
            RootedValue value(cx, ObjectValue(*proto));
            if (!DefineStandardSlot(cx, obj, key, atom, value, attrs, named))
                goto bad;
        }

        ctor = proto;
    } else {
        RootedFunction fun(cx, NewNativeConstructor(cx, constructor, nargs, atom, ctorKind));
        if (!fun)
            goto bad;

        /*
         * Set the class object early for standard class constructors. Type
         * inference may need to access these, and js::GetBuiltinPrototype will
         * fail if it tries to do a reentrant reconstruction of the class.
         */
        if (key != JSProto_Null) {
            SetClassObject(obj, key, fun, proto);
            cached = true;
        }

        RootedValue value(cx, ObjectValue(*fun));
        if (!DefineStandardSlot(cx, obj, key, atom, value, 0, named))
            goto bad;

        /*
         * Optionally construct the prototype object, before the class has
         * been fully initialized.  Allow the ctor to replace proto with a
         * different object, as is done for operator new.
         */
        ctor = fun;
        if (!LinkConstructorAndPrototype(cx, ctor, proto))
            goto bad;

        /* Bootstrap Function.prototype (see also JS_InitStandardClasses). */
        Rooted<TaggedProto> tagged(cx, TaggedProto(proto));
        if (ctor->getClass() == clasp && !ctor->splicePrototype(cx, clasp, tagged))
            goto bad;
    }

    if (!DefinePropertiesAndFunctions(cx, proto, ps, fs) ||
        (ctor != proto && !DefinePropertiesAndFunctions(cx, ctor, static_ps, static_fs)))
    {
        goto bad;
    }

    /* If this is a standard class, cache its prototype. */
    if (!cached && key != JSProto_Null)
        SetClassObject(obj, key, ctor, proto);

    if (ctorp)
        *ctorp = ctor;
    return proto;

bad:
    if (named) {
        ObjectOpResult ignored;
        RootedId id(cx, AtomToId(atom));

        // XXX FIXME - absurd to call this here; instead define the property last.
        DeleteProperty(cx, obj, id, ignored);
    }
    if (cached)
        ClearClassObject(obj, key);
    return nullptr;
}

NativeObject*
js::InitClass(JSContext *cx, HandleObject obj, HandleObject protoProto_,
              const Class* clasp, Native constructor, unsigned nargs,
              const JSPropertySpec *ps, const JSFunctionSpec *fs,
              const JSPropertySpec *static_ps, const JSFunctionSpec *static_fs,
              NativeObject **ctorp, AllocKind ctorKind)
{
    RootedObject protoProto(cx, protoProto_);

    /* Check function pointer members. */
    MOZ_ASSERT(clasp->addProperty != JS_PropertyStub);
    MOZ_ASSERT(clasp->getProperty != JS_PropertyStub);
    MOZ_ASSERT(clasp->setProperty != JS_StrictPropertyStub);

    RootedAtom atom(cx, Atomize(cx, clasp->name, strlen(clasp->name)));
    if (!atom)
        return nullptr;

    /*
     * All instances of the class will inherit properties from the prototype
     * object we are about to create (in DefineConstructorAndPrototype), which
     * in turn will inherit from protoProto.
     *
     * When initializing a standard class (other than Object), if protoProto is
     * null, default to Object.prototype. The engine's internal uses of
     * js::InitClass depend on this nicety.
     */
    JSProtoKey key = JSCLASS_CACHED_PROTO_KEY(clasp);
    if (key != JSProto_Null &&
        !protoProto &&
        !GetBuiltinPrototype(cx, JSProto_Object, &protoProto))
    {
        return nullptr;
    }

    return DefineConstructorAndPrototype(cx, obj, key, atom, protoProto, clasp, constructor, nargs,
                                         ps, fs, static_ps, static_fs, ctorp, ctorKind);
}

void
JSObject::fixupAfterMovingGC()
{
    // For copy-on-write objects that don't own their elements, fix up the
    // elements pointer if it points to inline elements in the owning object.
    if (is<NativeObject>()) {
        NativeObject& obj = as<NativeObject>();
        if (obj.denseElementsAreCopyOnWrite()) {
            NativeObject* owner = MaybeForwarded(obj.getElementsHeader()->ownerObject().get());
            if (owner != &obj && owner->hasFixedElements())
                obj.elements_ = owner->getElementsHeader()->elements();
            MOZ_ASSERT(!IsForwarded(obj.getElementsHeader()->ownerObject().get()));
        }
    }
}

bool
js::SetClassAndProto(JSContext* cx, HandleObject obj,
                     const Class* clasp, Handle<js::TaggedProto> proto)
{
    // Regenerate the object's shape. If the object is a proto (isDelegate()),
    // we also need to regenerate shapes for all of the objects along the old
    // prototype chain, in case any entries were filled by looking up through
    // obj. Stop when a non-native object is found, prototype lookups will not
    // be cached across these.
    //
    // How this shape change is done is very delicate; the change can be made
    // either by marking the object's prototype as uncacheable (such that the
    // JIT'ed ICs cannot assume the shape determines the prototype) or by just
    // generating a new shape for the object. Choosing the former is bad if the
    // object is on the prototype chain of other objects, as the uncacheable
    // prototype can inhibit iterator caches on those objects and slow down
    // prototype accesses. Choosing the latter is bad if there are many similar
    // objects to this one which will have their prototype mutated, as the
    // generateOwnShape forces the object into dictionary mode and similar
    // property lineages will be repeatedly cloned.
    //
    // :XXX: bug 707717 make this code less brittle.
    RootedObject oldproto(cx, obj);
    while (oldproto && oldproto->isNative()) {
        if (oldproto->isSingleton()) {
            if (!oldproto->as<NativeObject>().generateOwnShape(cx))
                return false;
        } else {
            if (!oldproto->setUncacheableProto(cx))
                return false;
        }
        if (!obj->isDelegate()) {
            // If |obj| is not a proto of another object, we don't need to
            // reshape the whole proto chain.
            MOZ_ASSERT(obj == oldproto);
            break;
        }
        oldproto = oldproto->getProto();
    }

    if (proto.isObject() && !proto.toObject()->setDelegate(cx))
        return false;

    if (obj->isSingleton()) {
        /*
         * Just splice the prototype, but mark the properties as unknown for
         * consistent behavior.
         */
        if (!obj->splicePrototype(cx, clasp, proto))
            return false;
        MarkObjectGroupUnknownProperties(cx, obj->group());
        return true;
    }

    if (proto.isObject()) {
        RootedObject protoObj(cx, proto.toObject());
        if (!JSObject::setNewGroupUnknown(cx, clasp, protoObj))
            return false;
    }

    ObjectGroup* group = ObjectGroup::defaultNewGroup(cx, clasp, proto);
    if (!group)
        return false;

    /*
     * Setting __proto__ on an object that has escaped and may be referenced by
     * other heap objects can only be done if the properties of both objects
     * are unknown. Type sets containing this object will contain the original
     * type but not the new type of the object, so we need to treat all such
     * type sets as unknown.
     */
    MarkObjectGroupUnknownProperties(cx, obj->group());
    MarkObjectGroupUnknownProperties(cx, group);

    obj->setGroup(group);

    return true;
}

static bool
MaybeResolveConstructor(ExclusiveContext* cxArg, Handle<GlobalObject*> global, JSProtoKey key)
{
    if (global->isStandardClassResolved(key))
        return true;
    if (!cxArg->shouldBeJSContext())
        return false;

    JSContext* cx = cxArg->asJSContext();
    return GlobalObject::resolveConstructor(cx, global, key);
}

bool
js::GetBuiltinConstructor(ExclusiveContext* cx, JSProtoKey key, MutableHandleObject objp)
{
    MOZ_ASSERT(key != JSProto_Null);
    Rooted<GlobalObject*> global(cx, cx->global());
    if (!MaybeResolveConstructor(cx, global, key))
        return false;

    objp.set(&global->getConstructor(key).toObject());
    return true;
}

bool
js::GetBuiltinPrototype(ExclusiveContext* cx, JSProtoKey key, MutableHandleObject protop)
{
    MOZ_ASSERT(key != JSProto_Null);
    Rooted<GlobalObject*> global(cx, cx->global());
    if (!MaybeResolveConstructor(cx, global, key))
        return false;

    protop.set(&global->getPrototype(key).toObject());
    return true;
}

bool
js::IsStandardPrototype(JSObject* obj, JSProtoKey key)
{
    GlobalObject& global = obj->global();
    Value v = global.getPrototype(key);
    return v.isObject() && obj == &v.toObject();
}

JSProtoKey
JS::IdentifyStandardInstance(JSObject* obj)
{
    // Note: The prototype shares its JSClass with instances.
    MOZ_ASSERT(!obj->is<CrossCompartmentWrapperObject>());
    JSProtoKey key = StandardProtoKeyOrNull(obj);
    if (key != JSProto_Null && !IsStandardPrototype(obj, key))
        return key;
    return JSProto_Null;
}

JSProtoKey
JS::IdentifyStandardPrototype(JSObject* obj)
{
    // Note: The prototype shares its JSClass with instances.
    MOZ_ASSERT(!obj->is<CrossCompartmentWrapperObject>());
    JSProtoKey key = StandardProtoKeyOrNull(obj);
    if (key != JSProto_Null && IsStandardPrototype(obj, key))
        return key;
    return JSProto_Null;
}

JSProtoKey
JS::IdentifyStandardInstanceOrPrototype(JSObject* obj)
{
    return StandardProtoKeyOrNull(obj);
}

JSProtoKey
JS::IdentifyStandardConstructor(JSObject* obj)
{
    // Note that NATIVE_CTOR does not imply that we are a standard constructor,
    // but the converse is true (at least until we start having self-hosted
    // constructors for standard classes). This lets us avoid a costly loop for
    // many functions (which, depending on the call site, may be the common case).
    if (!obj->is<JSFunction>() || !(obj->as<JSFunction>().flags() & JSFunction::NATIVE_CTOR))
        return JSProto_Null;

    GlobalObject& global = obj->global();
    for (size_t k = 0; k < JSProto_LIMIT; ++k) {
        JSProtoKey key = static_cast<JSProtoKey>(k);
        if (global.getConstructor(key) == ObjectValue(*obj))
            return key;
    }

    return JSProto_Null;
}

bool
JSObject::isCallable() const
{
    if (is<JSFunction>())
        return true;
    return callHook() != nullptr;
}

bool
JSObject::isConstructor() const
{
    if (is<JSFunction>()) {
        const JSFunction& fun = as<JSFunction>();
        return fun.isNativeConstructor() || fun.isInterpretedConstructor();
    }
    return constructHook() != nullptr;
}

JSNative
JSObject::callHook() const
{
    const js::Class* clasp = getClass();

    if (clasp->call)
        return clasp->call;

    if (is<js::ProxyObject>()) {
        const js::ProxyObject& p = as<js::ProxyObject>();
        if (p.handler()->isCallable(const_cast<JSObject*>(this)))
            return js::proxy_Call;
    }
    return nullptr;
}

JSNative
JSObject::constructHook() const
{
    const js::Class* clasp = getClass();

    if (clasp->construct)
        return clasp->construct;

    if (is<js::ProxyObject>()) {
        const js::ProxyObject& p = as<js::ProxyObject>();
        if (p.handler()->isConstructor(const_cast<JSObject*>(this)))
            return js::proxy_Construct;
    }
    return nullptr;
}

bool
js::LookupProperty(JSContext* cx, HandleObject obj, js::HandleId id,
                   MutableHandleObject objp, MutableHandleShape propp)
{
    /* NB: The logic of lookupProperty is implicitly reflected in
     *     BaselineIC.cpp's |EffectlesslyLookupProperty| logic.
     *     If this changes, please remember to update the logic there as well.
     */
    if (LookupPropertyOp op = obj->getOps()->lookupProperty)
        return op(cx, obj, id, objp, propp);
    return NativeLookupProperty<CanGC>(cx, obj.as<NativeObject>(), id, objp, propp);
}

bool
js::LookupName(JSContext* cx, HandlePropertyName name, HandleObject scopeChain,
               MutableHandleObject objp, MutableHandleObject pobjp, MutableHandleShape propp)
{
    RootedId id(cx, NameToId(name));

    for (RootedObject scope(cx, scopeChain); scope; scope = scope->enclosingScope()) {
        if (!LookupProperty(cx, scope, id, pobjp, propp))
            return false;
        if (propp) {
            objp.set(scope);
            return true;
        }
    }

    objp.set(nullptr);
    pobjp.set(nullptr);
    propp.set(nullptr);
    return true;
}

bool
js::LookupNameNoGC(JSContext* cx, PropertyName* name, JSObject* scopeChain,
                   JSObject** objp, JSObject** pobjp, Shape** propp)
{
    AutoAssertNoException nogc(cx);

    MOZ_ASSERT(!*objp && !*pobjp && !*propp);

    for (JSObject* scope = scopeChain; scope; scope = scope->enclosingScope()) {
        if (scope->getOps()->lookupProperty)
            return false;
        if (!LookupPropertyInline<NoGC>(cx, &scope->as<NativeObject>(), NameToId(name), pobjp, propp))
            return false;
        if (*propp) {
            *objp = scope;
            return true;
        }
    }

    return true;
}

bool
js::LookupNameWithGlobalDefault(JSContext* cx, HandlePropertyName name, HandleObject scopeChain,
                                MutableHandleObject objp)
{
    RootedId id(cx, NameToId(name));

    RootedObject pobj(cx);
    RootedShape shape(cx);

    RootedObject scope(cx, scopeChain);
    for (; !scope->is<GlobalObject>(); scope = scope->enclosingScope()) {
        if (!LookupProperty(cx, scope, id, &pobj, &shape))
            return false;
        if (shape)
            break;
    }

    objp.set(scope);
    return true;
}

bool
js::LookupNameUnqualified(JSContext* cx, HandlePropertyName name, HandleObject scopeChain,
                          MutableHandleObject objp)
{
    RootedId id(cx, NameToId(name));

    RootedObject pobj(cx);
    RootedShape shape(cx);

    RootedObject scope(cx, scopeChain);
    for (; !scope->isUnqualifiedVarObj(); scope = scope->enclosingScope()) {
        if (!LookupProperty(cx, scope, id, &pobj, &shape))
            return false;
        if (shape)
            break;
    }

    // See note above UninitializedLexicalObject.
    if (pobj == scope && IsUninitializedLexicalSlot(scope, shape)) {
        scope = UninitializedLexicalObject::create(cx, scope);
        if (!scope)
            return false;
    }

    objp.set(scope);
    return true;
}

bool
js::HasOwnProperty(JSContext* cx, HandleObject obj, HandleId id, bool* result)
{
    if (obj->is<ProxyObject>())
        return Proxy::hasOwn(cx, obj, id, result);

    if (GetOwnPropertyOp op = obj->getOps()->getOwnPropertyDescriptor) {
        Rooted<PropertyDescriptor> desc(cx);
        if (!op(cx, obj, id, &desc))
            return false;
        *result = !!desc.object();
        return true;
    }

    RootedShape shape(cx);
    if (!NativeLookupOwnProperty<CanGC>(cx, obj.as<NativeObject>(), id, &shape))
        return false;
    *result = (shape != nullptr);
    return true;
}

bool
js::LookupPropertyPure(ExclusiveContext* cx, JSObject* obj, jsid id, JSObject** objp,
                       Shape** propp)
{
    do {
        if (obj->isNative()) {
            /* Search for a native dense element, typed array element, or property. */

            if (JSID_IS_INT(id) && obj->as<NativeObject>().containsDenseElement(JSID_TO_INT(id))) {
                *objp = obj;
                MarkDenseOrTypedArrayElementFound<NoGC>(propp);
                return true;
            }

            if (IsAnyTypedArray(obj)) {
                uint64_t index;
                if (IsTypedArrayIndex(id, &index)) {
                    if (index < AnyTypedArrayLength(obj)) {
                        *objp = obj;
                        MarkDenseOrTypedArrayElementFound<NoGC>(propp);
                    } else {
                        *objp = nullptr;
                        *propp = nullptr;
                    }
                    return true;
                }
            }

            if (Shape* shape = obj->as<NativeObject>().lookupPure(id)) {
                *objp = obj;
                *propp = shape;
                return true;
            }

            // Fail if there's a resolve hook. We allow the JSFunction resolve hook
            // if we know it will never add a property with this name or str_resolve
            // with a non-integer property.
            do {
                const Class* clasp = obj->getClass();
                if (!clasp->resolve)
                    break;
                if (clasp->resolve == fun_resolve && !FunctionHasResolveHook(cx->names(), id))
                    break;
                if (clasp->resolve == str_resolve && !JSID_IS_INT(id))
                    break;
                return false;
            } while (0);
        } else {
            // Search for a property on an unboxed object. Other non-native objects
            // are not handled here.
            if (!obj->is<UnboxedPlainObject>())
                return false;
            if (obj->as<UnboxedPlainObject>().containsUnboxedOrExpandoProperty(cx, id)) {
                *objp = obj;
                MarkNonNativePropertyFound<NoGC>(propp);
                return true;
            }
        }

        obj = obj->getProto();
    } while (obj);

    *objp = nullptr;
    *propp = nullptr;
    return true;
}

bool
js::GetPropertyPure(ExclusiveContext* cx, JSObject* obj, jsid id, Value* vp)
{
    JSObject* pobj;
    Shape* shape;
    if (!LookupPropertyPure(cx, obj, id, &pobj, &shape))
        return false;
    return pobj->isNative() && NativeGetPureInline(&pobj->as<NativeObject>(), shape, vp);
}

bool
JSObject::reportReadOnly(JSContext* cx, jsid id, unsigned report)
{
    RootedValue val(cx, IdToValue(id));
    return ReportValueErrorFlags(cx, report, JSMSG_READ_ONLY,
                                 JSDVG_IGNORE_STACK, val, js::NullPtr(),
                                 nullptr, nullptr);
}

bool
JSObject::reportNotConfigurable(JSContext* cx, jsid id, unsigned report)
{
    RootedValue val(cx, IdToValue(id));
    return ReportValueErrorFlags(cx, report, JSMSG_CANT_DELETE,
                                 JSDVG_IGNORE_STACK, val, js::NullPtr(),
                                 nullptr, nullptr);
}

bool
JSObject::reportNotExtensible(JSContext* cx, unsigned report)
{
    RootedValue val(cx, ObjectValue(*this));
    return ReportValueErrorFlags(cx, report, JSMSG_OBJECT_NOT_EXTENSIBLE,
                                 JSDVG_IGNORE_STACK, val, js::NullPtr(),
                                 nullptr, nullptr);
}


/*** ES6 standard internal methods ***************************************************************/

bool
js::SetPrototype(JSContext *cx, HandleObject obj, HandleObject proto, JS::ObjectOpResult &result)
{
    /*
     * If |obj| has a "lazy" [[Prototype]], it is 1) a proxy 2) whose handler's
     * {get,set}Prototype and setImmutablePrototype methods mediate access to
     * |obj.[[Prototype]]|.  The Proxy subsystem is responsible for responding
     * to such attempts.
     */
    if (obj->hasLazyPrototype()) {
        MOZ_ASSERT(obj->is<ProxyObject>());
        return Proxy::setPrototype(cx, obj, proto, result);
    }

    /* Disallow mutation of immutable [[Prototype]]s. */
    if (obj->nonLazyPrototypeIsImmutable()) {
        return result.fail(JSMSG_CANT_SET_PROTO);
    }

    /*
     * Disallow mutating the [[Prototype]] on ArrayBuffer objects, which
     * due to their complicated delegate-object shenanigans can't easily
     * have a mutable [[Prototype]].
     */
    if (obj->is<ArrayBufferObject>()) {
        JS_ReportErrorNumber(cx, GetErrorMessage, nullptr, JSMSG_CANT_SET_PROTO_OF,
                             "incompatible ArrayBuffer");
        return false;
    }

    /*
     * Disallow mutating the [[Prototype]] on Typed Objects, per the spec.
     */
    if (obj->is<TypedObject>()) {
        JS_ReportErrorNumber(cx, GetErrorMessage, nullptr, JSMSG_CANT_SET_PROTO_OF,
                             "incompatible TypedObject");
        return false;
    }

    /*
     * Explicitly disallow mutating the [[Prototype]] of Location objects
     * for flash-related security reasons.
     */
    if (!strcmp(obj->getClass()->name, "Location")) {
        JS_ReportErrorNumber(cx, GetErrorMessage, nullptr, JSMSG_CANT_SET_PROTO_OF,
                             "incompatible Location object");
        return false;
    }

    /* ES6 9.1.2 step 5 forbids changing [[Prototype]] if not [[Extensible]]. */
    bool extensible;
    if (!IsExtensible(cx, obj, &extensible))
        return false;
    if (!extensible)
        return result.fail(JSMSG_CANT_SET_PROTO);

    /* ES6 9.1.2 step 6 forbids generating cyclical prototype chains. */
    RootedObject obj2(cx);
    for (obj2 = proto; obj2; ) {
        if (obj2 == obj)
            return result.fail(JSMSG_CANT_SET_PROTO_CYCLE);

        if (!GetPrototype(cx, obj2, &obj2))
            return false;
    }

    // Convert unboxed objects to their native representations before changing
    // their prototype/group, as they depend on the group for their layout.
    if (obj->is<UnboxedPlainObject>() && !UnboxedPlainObject::convertToNative(cx, obj))
        return false;

    Rooted<TaggedProto> taggedProto(cx, TaggedProto(proto));
    if (!SetClassAndProto(cx, obj, obj->getClass(), taggedProto))
        return false;

    return result.succeed();
}

bool
js::SetPrototype(JSContext *cx, HandleObject obj, HandleObject proto)
{
    ObjectOpResult result;
    return SetPrototype(cx, obj, proto, result) && result.checkStrict(cx, obj);
}

bool
js::PreventExtensions(JSContext *cx, HandleObject obj, ObjectOpResult &result)
{
    if (obj->is<ProxyObject>())
        return js::Proxy::preventExtensions(cx, obj, result);

    if (!obj->nonProxyIsExtensible())
        return result.succeed();

    // Force lazy properties to be resolved.
    AutoIdVector props(cx);
    if (!js::GetPropertyKeys(cx, obj, JSITER_HIDDEN | JSITER_OWNONLY, &props))
        return false;

    // Convert all dense elements to sparse properties. This will shrink the
    // initialized length and capacity of the object to zero and ensure that no
    // new dense elements can be added without calling growElements(), which
    // checks isExtensible().
    if (obj->isNative()) {
        if (!NativeObject::sparsifyDenseElements(cx, obj.as<NativeObject>()))
            return false;
    }

    if (!obj->setFlags(cx, BaseShape::NOT_EXTENSIBLE, JSObject::GENERATE_SHAPE))
        return false;
    return result.succeed();
}

bool
js::PreventExtensions(JSContext *cx, HandleObject obj)
{
    ObjectOpResult result;
    return PreventExtensions(cx, obj, result) && result.checkStrict(cx, obj);
}

bool
js::GetOwnPropertyDescriptor(JSContext* cx, HandleObject obj, HandleId id,
                             MutableHandle<PropertyDescriptor> desc)
{
    if (GetOwnPropertyOp op = obj->getOps()->getOwnPropertyDescriptor)
        return op(cx, obj, id, desc);

    RootedShape shape(cx);
    if (!NativeLookupOwnProperty<CanGC>(cx, obj.as<NativeObject>(), id, &shape))
        return false;
    if (!shape) {
        desc.object().set(nullptr);
        return true;
    }

    bool doGet = true;
    desc.setAttributes(GetShapeAttributes(obj, shape));
    if (desc.isAccessorDescriptor()) {
        MOZ_ASSERT(desc.isShared());
        doGet = false;

        // The result of GetOwnPropertyDescriptor() must be either undefined or
        // a complete property descriptor (per ES6 draft rev 32 (2015 Feb 2)
        // 6.1.7.3, Invariants of the Essential Internal Methods).
        //
        // It is an unfortunate fact that in SM, properties can exist that have
        // JSPROP_GETTER or JSPROP_SETTER but not both. In these cases, rather
        // than return true with desc incomplete, we fill out the missing
        // getter or setter with a null, following CompletePropertyDescriptor.
        if (desc.hasGetterObject()) {
            desc.setGetterObject(shape->getterObject());
        } else {
            desc.setGetterObject(nullptr);
            desc.attributesRef() |= JSPROP_GETTER;
        }
        if (desc.hasSetterObject()) {
            desc.setSetterObject(shape->setterObject());
        } else {
            desc.setSetterObject(nullptr);
            desc.attributesRef() |= JSPROP_SETTER;
        }
    } else {
        // This is either a straight-up data property or (rarely) a
        // property with a JSGetterOp/JSSetterOp. The latter must be
        // reported to the caller as a plain data property, so don't
        // populate desc.getter/setter, and mask away the SHARED bit.
        desc.attributesRef() &= ~JSPROP_SHARED;
    }

    RootedValue value(cx);
    if (doGet && !GetProperty(cx, obj, obj, id, &value))
        return false;

    desc.value().set(value);
    desc.object().set(obj);
    return true;
}

bool
js::DefineProperty(ExclusiveContext *cx, HandleObject obj, HandleId id, HandleValue value,
                   JSGetterOp getter, JSSetterOp setter, unsigned attrs,
                   ObjectOpResult &result)
{
    MOZ_ASSERT(getter != JS_PropertyStub);
    MOZ_ASSERT(setter != JS_StrictPropertyStub);
    MOZ_ASSERT(!(attrs & JSPROP_PROPOP_ACCESSORS));

    DefinePropertyOp op = obj->getOps()->defineProperty;
    if (op) {
        if (!cx->shouldBeJSContext())
            return false;
        return op(cx->asJSContext(), obj, id, value, getter, setter, attrs, result);
    }
    return NativeDefineProperty(cx, obj.as<NativeObject>(), id, value, getter, setter, attrs,
                                result);
}

bool
js::DefineProperty(ExclusiveContext *cx, HandleObject obj, PropertyName *name, HandleValue value,
                   JSGetterOp getter, JSSetterOp setter, unsigned attrs,
                   ObjectOpResult &result)
{
    RootedId id(cx, NameToId(name));
    return DefineProperty(cx, obj, id, value, getter, setter, attrs, result);
}

bool
js::DefineElement(ExclusiveContext *cx, HandleObject obj, uint32_t index, HandleValue value,
                  JSGetterOp getter, JSSetterOp setter, unsigned attrs,
                  ObjectOpResult &result)
{
    MOZ_ASSERT(getter != JS_PropertyStub);
    MOZ_ASSERT(setter != JS_StrictPropertyStub);

    RootedId id(cx);
    if (!IndexToId(cx, index, &id))
        return false;
    return DefineProperty(cx, obj, id, value, getter, setter, attrs, result);
}

bool
js::DefineProperty(ExclusiveContext *cx, HandleObject obj, HandleId id, HandleValue value,
                   JSGetterOp getter, JSSetterOp setter, unsigned attrs)
{
    ObjectOpResult result;
    if (!DefineProperty(cx, obj, id, value, getter, setter, attrs, result))
        return false;
    if (!result) {
        if (!cx->shouldBeJSContext())
            return false;
        result.reportError(cx->asJSContext(), obj, id);
        return false;
    }
    return true;
}

bool
js::DefineProperty(ExclusiveContext *cx, HandleObject obj, PropertyName *name, HandleValue value,
                   JSGetterOp getter, JSSetterOp setter, unsigned attrs)
{
    RootedId id(cx, NameToId(name));
    return DefineProperty(cx, obj, id, value, getter, setter, attrs);
}

bool
js::DefineElement(ExclusiveContext *cx, HandleObject obj, uint32_t index, HandleValue value,
                   JSGetterOp getter, JSSetterOp setter, unsigned attrs)
{
    MOZ_ASSERT(getter != JS_PropertyStub);
    MOZ_ASSERT(setter != JS_StrictPropertyStub);

    RootedId id(cx);
    if (!IndexToId(cx, index, &id))
        return false;
    return DefineProperty(cx, obj, id, value, getter, setter, attrs);
}

bool
js::SetProperty(JSContext *cx, HandleObject obj, HandleObject receiver, HandlePropertyName name,
                MutableHandleValue vp)
{
    RootedId id(cx, NameToId(name));
    return SetProperty(cx, obj, receiver, id, vp);
}

bool
js::PutProperty(JSContext *cx, HandleObject obj, HandlePropertyName name, MutableHandleValue value,
                bool strict)
{
    RootedId id(cx, NameToId(name));
    return PutProperty(cx, obj, id, value, strict);
}


/*** SpiderMonkey nonstandard internal methods ***************************************************/

bool
js::SetImmutablePrototype(ExclusiveContext* cx, HandleObject obj, bool* succeeded)
{
    if (obj->hasLazyPrototype()) {
        if (!cx->shouldBeJSContext())
            return false;
        return Proxy::setImmutablePrototype(cx->asJSContext(), obj, succeeded);
    }

    if (!obj->setFlags(cx, BaseShape::IMMUTABLE_PROTOTYPE))
        return false;
    *succeeded = true;
    return true;
}

bool
js::GetPropertyDescriptor(JSContext* cx, HandleObject obj, HandleId id,
                          MutableHandle<PropertyDescriptor> desc)
{
    RootedObject pobj(cx);

    for (pobj = obj; pobj;) {
        if (pobj->is<ProxyObject>())
            return Proxy::getPropertyDescriptor(cx, pobj, id, desc);

        if (!GetOwnPropertyDescriptor(cx, pobj, id, desc))
            return false;

        if (desc.object())
            return true;

        if (!GetPrototype(cx, pobj, &pobj))
            return false;
    }

    MOZ_ASSERT(!desc.object());
    return true;
}

bool
js::ToPrimitive(JSContext* cx, HandleObject obj, JSType hint, MutableHandleValue vp)
{
    bool ok;
    if (JSConvertOp op = obj->getClass()->convert)
        ok = op(cx, obj, hint, vp);
    else
        ok = JS::OrdinaryToPrimitive(cx, obj, hint, vp);
    MOZ_ASSERT_IF(ok, vp.isPrimitive());
    return ok;
}

bool
js::WatchGuts(JSContext* cx, JS::HandleObject origObj, JS::HandleId id, JS::HandleObject callable)
{
    RootedObject obj(cx, GetInnerObject(origObj));
    if (obj->isNative()) {
        // Use sparse indexes for watched objects, as dense elements can be
        // written to without checking the watchpoint map.
        if (!NativeObject::sparsifyDenseElements(cx, obj.as<NativeObject>()))
            return false;

        MarkTypePropertyNonData(cx, obj, id);
    }

    WatchpointMap* wpmap = cx->compartment()->watchpointMap;
    if (!wpmap) {
        wpmap = cx->runtime()->new_<WatchpointMap>();
        if (!wpmap || !wpmap->init()) {
            ReportOutOfMemory(cx);
            return false;
        }
        cx->compartment()->watchpointMap = wpmap;
    }

    return wpmap->watch(cx, obj, id, js::WatchHandler, callable);
}

bool
js::UnwatchGuts(JSContext* cx, JS::HandleObject origObj, JS::HandleId id)
{
    // Looking in the map for an unsupported object will never hit, so we don't
    // need to check for nativeness or watchable-ness here.
    RootedObject obj(cx, GetInnerObject(origObj));
    if (WatchpointMap* wpmap = cx->compartment()->watchpointMap)
        wpmap->unwatch(obj, id, nullptr, nullptr);
    return true;
}

bool
js::WatchProperty(JSContext* cx, HandleObject obj, HandleId id, HandleObject callable)
{
    if (WatchOp op = obj->getOps()->watch)
        return op(cx, obj, id, callable);

    if (!obj->isNative() || IsAnyTypedArray(obj)) {
        JS_ReportErrorNumber(cx, GetErrorMessage, nullptr, JSMSG_CANT_WATCH,
                             obj->getClass()->name);
        return false;
    }

    return WatchGuts(cx, obj, id, callable);
}

bool
js::UnwatchProperty(JSContext* cx, HandleObject obj, HandleId id)
{
    if (UnwatchOp op = obj->getOps()->unwatch)
        return op(cx, obj, id);

    return UnwatchGuts(cx, obj, id);
}

const char*
js::GetObjectClassName(JSContext* cx, HandleObject obj)
{
    assertSameCompartment(cx, obj);

    if (obj->is<ProxyObject>())
        return Proxy::className(cx, obj);

    return obj->getClass()->name;
}

bool
JSObject::callMethod(JSContext* cx, HandleId id, unsigned argc, Value* argv, MutableHandleValue vp)
{
    RootedValue fval(cx);
    RootedObject obj(cx, this);
    if (!GetProperty(cx, obj, obj, id, &fval))
        return false;
    return Invoke(cx, ObjectValue(*obj), fval, argc, argv, vp);
}


/* * */

bool
js::HasDataProperty(JSContext* cx, NativeObject* obj, jsid id, Value* vp)
{
    if (JSID_IS_INT(id) && obj->containsDenseElement(JSID_TO_INT(id))) {
        *vp = obj->getDenseElement(JSID_TO_INT(id));
        return true;
    }

    if (Shape* shape = obj->lookup(cx, id)) {
        if (shape->hasDefaultGetter() && shape->hasSlot()) {
            *vp = obj->getSlot(shape->slot());
            return true;
        }
    }

    return false;
}

/*
 * Gets |obj[id]|.  If that value's not callable, returns true and stores a
 * non-primitive value in *vp.  If it's callable, calls it with no arguments
 * and |obj| as |this|, returning the result in *vp.
 *
 * This is a mini-abstraction for ES5 8.12.8 [[DefaultValue]], either steps 1-2
 * or steps 3-4.
 */
static bool
MaybeCallMethod(JSContext* cx, HandleObject obj, HandleId id, MutableHandleValue vp)
{
    if (!GetProperty(cx, obj, obj, id, vp))
        return false;
    if (!IsCallable(vp)) {
        vp.setObject(*obj);
        return true;
    }
    return Invoke(cx, ObjectValue(*obj), vp, 0, nullptr, vp);
}

bool
JS::OrdinaryToPrimitive(JSContext* cx, HandleObject obj, JSType hint, MutableHandleValue vp)
{
    MOZ_ASSERT(hint == JSTYPE_NUMBER || hint == JSTYPE_STRING || hint == JSTYPE_VOID);

    Rooted<jsid> id(cx);

    const Class* clasp = obj->getClass();
    if (hint == JSTYPE_STRING) {
        id = NameToId(cx->names().toString);

        /* Optimize (new String(...)).toString(). */
        if (clasp == &StringObject::class_) {
            StringObject* nobj = &obj->as<StringObject>();
            if (ClassMethodIsNative(cx, nobj, &StringObject::class_, id, str_toString)) {
                vp.setString(nobj->unbox());
                return true;
            }
        }

        if (!MaybeCallMethod(cx, obj, id, vp))
            return false;
        if (vp.isPrimitive())
            return true;

        id = NameToId(cx->names().valueOf);
        if (!MaybeCallMethod(cx, obj, id, vp))
            return false;
        if (vp.isPrimitive())
            return true;
    } else {

        /* Optimize new String(...).valueOf(). */
        if (clasp == &StringObject::class_) {
            id = NameToId(cx->names().valueOf);
            StringObject* nobj = &obj->as<StringObject>();
            if (ClassMethodIsNative(cx, nobj, &StringObject::class_, id, str_toString)) {
                vp.setString(nobj->unbox());
                return true;
            }
        }

        /* Optimize new Number(...).valueOf(). */
        if (clasp == &NumberObject::class_) {
            id = NameToId(cx->names().valueOf);
            NumberObject* nobj = &obj->as<NumberObject>();
            if (ClassMethodIsNative(cx, nobj, &NumberObject::class_, id, num_valueOf)) {
                vp.setNumber(nobj->unbox());
                return true;
            }
        }

        id = NameToId(cx->names().valueOf);
        if (!MaybeCallMethod(cx, obj, id, vp))
            return false;
        if (vp.isPrimitive())
            return true;

        id = NameToId(cx->names().toString);
        if (!MaybeCallMethod(cx, obj, id, vp))
            return false;
        if (vp.isPrimitive())
            return true;
    }

    /* Avoid recursive death when decompiling in ReportValueError. */
    RootedString str(cx);
    if (hint == JSTYPE_STRING) {
        str = JS_InternString(cx, clasp->name);
        if (!str)
            return false;
    } else {
        str = nullptr;
    }

    RootedValue val(cx, ObjectValue(*obj));
    ReportValueError2(cx, JSMSG_CANT_CONVERT_TO, JSDVG_SEARCH_STACK, val, str,
                      hint == JSTYPE_VOID
                      ? "primitive type"
                      : hint == JSTYPE_STRING ? "string" : "number");
    return false;
}

bool
js::IsDelegate(JSContext* cx, HandleObject obj, const js::Value& v, bool* result)
{
    if (v.isPrimitive()) {
        *result = false;
        return true;
    }
    return IsDelegateOfObject(cx, obj, &v.toObject(), result);
}

bool
js::IsDelegateOfObject(JSContext* cx, HandleObject protoObj, JSObject* obj, bool* result)
{
    RootedObject obj2(cx, obj);
    for (;;) {
        if (!GetPrototype(cx, obj2, &obj2))
            return false;
        if (!obj2) {
            *result = false;
            return true;
        }
        if (obj2 == protoObj) {
            *result = true;
            return true;
        }
    }
}

JSObject*
js::GetBuiltinPrototypePure(GlobalObject* global, JSProtoKey protoKey)
{
    MOZ_ASSERT(JSProto_Null <= protoKey);
    MOZ_ASSERT(protoKey < JSProto_LIMIT);

    if (protoKey != JSProto_Null) {
        const Value& v = global->getPrototype(protoKey);
        if (v.isObject())
            return &v.toObject();
    }

    return nullptr;
}

JSObject*
js::PrimitiveToObject(JSContext* cx, const Value& v)
{
    if (v.isString()) {
        Rooted<JSString*> str(cx, v.toString());
        return StringObject::create(cx, str);
    }
    if (v.isNumber())
        return NumberObject::create(cx, v.toNumber());
    if (v.isBoolean())
        return BooleanObject::create(cx, v.toBoolean());
    MOZ_ASSERT(v.isSymbol());
    RootedSymbol symbol(cx, v.toSymbol());
    return SymbolObject::create(cx, symbol);
}

/*
 * Invokes the ES5 ToObject algorithm on vp, returning the result. If vp might
 * already be an object, use ToObject. reportCantConvert controls how null and
 * undefined errors are reported.
 *
 * Callers must handle the already-object case.
 */
JSObject*
js::ToObjectSlow(JSContext* cx, JS::HandleValue val, bool reportScanStack)
{
    MOZ_ASSERT(!val.isMagic());
    MOZ_ASSERT(!val.isObject());

    if (val.isNullOrUndefined()) {
        if (reportScanStack) {
            ReportIsNullOrUndefined(cx, JSDVG_SEARCH_STACK, val, NullPtr());
        } else {
            JS_ReportErrorNumber(cx, GetErrorMessage, nullptr, JSMSG_CANT_CONVERT_TO,
                                 val.isNull() ? "null" : "undefined", "object");
        }
        return nullptr;
    }

    return PrimitiveToObject(cx, val);
}

void
js::GetObjectSlotName(JSTracer *trc, char *buf, size_t bufsize)
{
    MOZ_ASSERT(trc->debugPrinter() == GetObjectSlotName);

    JSObject* obj = (JSObject*)trc->debugPrintArg();
    uint32_t slot = uint32_t(trc->debugPrintIndex());

    Shape* shape;
    if (obj->isNative()) {
        shape = obj->as<NativeObject>().lastProperty();
        while (shape && (!shape->hasSlot() || shape->slot() != slot))
            shape = shape->previous();
    } else {
        shape = nullptr;
    }

    if (!shape) {
        do {
            const char* slotname = nullptr;
            const char* pattern = nullptr;
            if (obj->is<GlobalObject>()) {
                pattern = "CLASS_OBJECT(%s)";
                if (false)
                    ;
#define TEST_SLOT_MATCHES_PROTOTYPE(name,code,init,clasp) \
                else if ((code) == slot) { slotname = js_##name##_str; }
                JS_FOR_EACH_PROTOTYPE(TEST_SLOT_MATCHES_PROTOTYPE)
#undef TEST_SLOT_MATCHES_PROTOTYPE
            } else {
                pattern = "%s";
                if (obj->is<ScopeObject>()) {
                    if (slot == ScopeObject::enclosingScopeSlot()) {
                        slotname = "enclosing_environment";
                    } else if (obj->is<CallObject>()) {
                        if (slot == CallObject::calleeSlot())
                            slotname = "callee_slot";
                    } else if (obj->is<DeclEnvObject>()) {
                        if (slot == DeclEnvObject::lambdaSlot())
                            slotname = "named_lambda";
                    } else if (obj->is<DynamicWithObject>()) {
                        if (slot == DynamicWithObject::objectSlot())
                            slotname = "with_object";
                        else if (slot == DynamicWithObject::thisSlot())
                            slotname = "with_this";
                    }
                }
            }

            if (slotname)
                JS_snprintf(buf, bufsize, pattern, slotname);
            else
                JS_snprintf(buf, bufsize, "**UNKNOWN SLOT %ld**", (long)slot);
        } while (false);
    } else {
        jsid propid = shape->propid();
        if (JSID_IS_INT(propid)) {
            JS_snprintf(buf, bufsize, "%ld", (long)JSID_TO_INT(propid));
        } else if (JSID_IS_ATOM(propid)) {
            PutEscapedString(buf, bufsize, JSID_TO_ATOM(propid), 0);
        } else if (JSID_IS_SYMBOL(propid)) {
            JS_snprintf(buf, bufsize, "**SYMBOL KEY**");
        } else {
            JS_snprintf(buf, bufsize, "**FINALIZED ATOM KEY**");
        }
    }
}

bool
js::ReportGetterOnlyAssignment(JSContext* cx, bool strict)
{
    return JS_ReportErrorFlagsAndNumber(cx,
                                        strict
                                        ? JSREPORT_ERROR
                                        : JSREPORT_WARNING | JSREPORT_STRICT,
                                        GetErrorMessage, nullptr,
                                        JSMSG_GETTER_ONLY);
}


/*** Debugging routines **************************************************************************/

#ifdef DEBUG

/*
 * Routines to print out values during debugging.  These are FRIEND_API to help
 * the debugger find them and to support temporarily hacking js::Dump* calls
 * into other code.
 */

static void
dumpValue(const Value& v)
{
    if (v.isNull())
        fprintf(stderr, "null");
    else if (v.isUndefined())
        fprintf(stderr, "undefined");
    else if (v.isInt32())
        fprintf(stderr, "%d", v.toInt32());
    else if (v.isDouble())
        fprintf(stderr, "%g", v.toDouble());
    else if (v.isString())
        v.toString()->dump();
    else if (v.isSymbol())
        v.toSymbol()->dump();
    else if (v.isObject() && v.toObject().is<JSFunction>()) {
        JSFunction* fun = &v.toObject().as<JSFunction>();
        if (fun->displayAtom()) {
            fputs("<function ", stderr);
            FileEscapedString(stderr, fun->displayAtom(), 0);
        } else {
            fputs("<unnamed function", stderr);
        }
        if (fun->hasScript()) {
            JSScript* script = fun->nonLazyScript();
            fprintf(stderr, " (%s:%" PRIuSIZE ")",
                    script->filename() ? script->filename() : "", script->lineno());
        }
        fprintf(stderr, " at %p>", (void*) fun);
    } else if (v.isObject()) {
        JSObject* obj = &v.toObject();
        const Class* clasp = obj->getClass();
        fprintf(stderr, "<%s%s at %p>",
                clasp->name,
                (clasp == &PlainObject::class_) ? "" : " object",
                (void*) obj);
    } else if (v.isBoolean()) {
        if (v.toBoolean())
            fprintf(stderr, "true");
        else
            fprintf(stderr, "false");
    } else if (v.isMagic()) {
        fprintf(stderr, "<invalid");
#ifdef DEBUG
        switch (v.whyMagic()) {
          case JS_ELEMENTS_HOLE:     fprintf(stderr, " elements hole");      break;
          case JS_NO_ITER_VALUE:     fprintf(stderr, " no iter value");      break;
          case JS_GENERATOR_CLOSING: fprintf(stderr, " generator closing");  break;
          case JS_OPTIMIZED_OUT:     fprintf(stderr, " optimized out");      break;
          default:                   fprintf(stderr, " ?!");                 break;
        }
#endif
        fprintf(stderr, ">");
    } else {
        fprintf(stderr, "unexpected value");
    }
}

JS_FRIEND_API(void)
js::DumpValue(const Value &val)
{
    dumpValue(val);
    fputc('\n', stderr);
}

JS_FRIEND_API(void)
js::DumpId(jsid id)
{
    fprintf(stderr, "jsid %p = ", (void*) JSID_BITS(id));
    dumpValue(IdToValue(id));
    fputc('\n', stderr);
}

static void
DumpProperty(NativeObject* obj, Shape& shape)
{
    jsid id = shape.propid();
    uint8_t attrs = shape.attributes();

    fprintf(stderr, "    ((js::Shape*) %p) ", (void*) &shape);
    if (attrs & JSPROP_ENUMERATE) fprintf(stderr, "enumerate ");
    if (attrs & JSPROP_READONLY) fprintf(stderr, "readonly ");
    if (attrs & JSPROP_PERMANENT) fprintf(stderr, "permanent ");
    if (attrs & JSPROP_SHARED) fprintf(stderr, "shared ");

    if (shape.hasGetterValue())
        fprintf(stderr, "getterValue=%p ", (void*) shape.getterObject());
    else if (!shape.hasDefaultGetter())
        fprintf(stderr, "getterOp=%p ", JS_FUNC_TO_DATA_PTR(void*, shape.getterOp()));

    if (shape.hasSetterValue())
        fprintf(stderr, "setterValue=%p ", (void*) shape.setterObject());
    else if (!shape.hasDefaultSetter())
        fprintf(stderr, "setterOp=%p ", JS_FUNC_TO_DATA_PTR(void*, shape.setterOp()));

    if (JSID_IS_ATOM(id) || JSID_IS_INT(id) || JSID_IS_SYMBOL(id))
        dumpValue(js::IdToValue(id));
    else
        fprintf(stderr, "unknown jsid %p", (void*) JSID_BITS(id));

    uint32_t slot = shape.hasSlot() ? shape.maybeSlot() : SHAPE_INVALID_SLOT;
    fprintf(stderr, ": slot %d", slot);
    if (shape.hasSlot()) {
        fprintf(stderr, " = ");
        dumpValue(obj->getSlot(slot));
    } else if (slot != SHAPE_INVALID_SLOT) {
        fprintf(stderr, " (INVALID!)");
    }
    fprintf(stderr, "\n");
}

bool
JSObject::uninlinedIsProxy() const
{
    return is<ProxyObject>();
}

void
JSObject::dump()
{
    JSObject* obj = this;
    fprintf(stderr, "object %p\n", (void*) obj);
    const Class* clasp = obj->getClass();
    fprintf(stderr, "class %p %s\n", (const void*)clasp, clasp->name);

    fprintf(stderr, "flags:");
    if (obj->isDelegate()) fprintf(stderr, " delegate");
    if (!obj->is<ProxyObject>() && !obj->nonProxyIsExtensible()) fprintf(stderr, " not_extensible");
    if (obj->isIndexed()) fprintf(stderr, " indexed");
    if (obj->isBoundFunction()) fprintf(stderr, " bound_function");
    if (obj->isQualifiedVarObj()) fprintf(stderr, " varobj");
    if (obj->isUnqualifiedVarObj()) fprintf(stderr, " unqualified_varobj");
    if (obj->watched()) fprintf(stderr, " watched");
    if (obj->isIteratedSingleton()) fprintf(stderr, " iterated_singleton");
    if (obj->isNewGroupUnknown()) fprintf(stderr, " new_type_unknown");
    if (obj->hasUncacheableProto()) fprintf(stderr, " has_uncacheable_proto");
    if (obj->hadElementsAccess()) fprintf(stderr, " had_elements_access");
    if (obj->wasNewScriptCleared()) fprintf(stderr, " new_script_cleared");

    if (obj->isNative()) {
        NativeObject* nobj = &obj->as<NativeObject>();
        if (nobj->inDictionaryMode())
            fprintf(stderr, " inDictionaryMode");
        if (nobj->hasShapeTable())
            fprintf(stderr, " hasShapeTable");
    }
    fprintf(stderr, "\n");

    if (obj->isNative()) {
        NativeObject* nobj = &obj->as<NativeObject>();
        uint32_t slots = nobj->getDenseInitializedLength();
        if (slots) {
            fprintf(stderr, "elements\n");
            for (uint32_t i = 0; i < slots; i++) {
                fprintf(stderr, " %3d: ", i);
                dumpValue(nobj->getDenseElement(i));
                fprintf(stderr, "\n");
                fflush(stderr);
            }
        }
    }

    fprintf(stderr, "proto ");
    TaggedProto proto = obj->getTaggedProto();
    if (proto.isLazy())
        fprintf(stderr, "<lazy>");
    else
        dumpValue(ObjectOrNullValue(proto.toObjectOrNull()));
    fputc('\n', stderr);

    if (clasp->flags & JSCLASS_HAS_PRIVATE)
        fprintf(stderr, "private %p\n", obj->as<NativeObject>().getPrivate());

    if (!obj->isNative())
        fprintf(stderr, "not native\n");

    uint32_t reservedEnd = JSCLASS_RESERVED_SLOTS(clasp);
    uint32_t slots = obj->isNative() ? obj->as<NativeObject>().slotSpan() : 0;
    uint32_t stop = obj->isNative() ? reservedEnd : slots;
    if (stop > 0)
        fprintf(stderr, obj->isNative() ? "reserved slots:\n" : "slots:\n");
    for (uint32_t i = 0; i < stop; i++) {
        fprintf(stderr, " %3d ", i);
        if (i < reservedEnd)
            fprintf(stderr, "(reserved) ");
        fprintf(stderr, "= ");
        dumpValue(obj->as<NativeObject>().getSlot(i));
        fputc('\n', stderr);
    }

    if (obj->isNative()) {
        fprintf(stderr, "properties:\n");
        Vector<Shape *, 8, SystemAllocPolicy> props;
        for (Shape::Range<NoGC> r(obj->as<NativeObject>().lastProperty()); !r.empty(); r.popFront())
            if (!props.append(&r.front())) {
                fprintf(stderr, "(OOM while appending properties)\n");
                break;
            }
        }
        for (size_t i = props.length(); i-- != 0;)
            DumpProperty(&obj->as<NativeObject>(), *props[i]);
    }
    fputc('\n', stderr);
}

static void
MaybeDumpObject(const char *name, JSObject *obj)
{
    if (obj) {
        fprintf(stderr, "  %s: ", name);
        dumpValue(ObjectValue(*obj));
        fputc('\n', stderr);
    }
}

static void
MaybeDumpValue(const char* name, const Value& v)
{
    if (!v.isNull()) {
        fprintf(stderr, "  %s: ", name);
        dumpValue(v);
        fputc('\n', stderr);
    }
}

JS_FRIEND_API(void)
js::DumpInterpreterFrame(JSContext *cx, InterpreterFrame *start)
{
    /* This should only called during live debugging. */
    ScriptFrameIter i(cx, ScriptFrameIter::GO_THROUGH_SAVED);
    if (!start) {
        if (i.done()) {
            fprintf(stderr, "no stack for cx = %p\n", (void*) cx);
            return;
        }
    } else {
        while (!i.done() && !i.isJit() && i.interpFrame() != start)
            ++i;

        if (i.done()) {
            fprintf(stderr, "fp = %p not found in cx = %p\n",
                    (void*)start, (void*)cx);
            return;
        }
    }

    for (; !i.done(); ++i) {
        if (i.isJit())
            fprintf(stderr, "JIT frame\n");
        else
            fprintf(stderr, "InterpreterFrame at %p\n", (void*) i.interpFrame());

        if (i.isFunctionFrame()) {
            fprintf(stderr, "callee fun: ");
            RootedValue v(cx);
            JSObject* fun = i.callee(cx);
            v.setObject(*fun);
            dumpValue(v);
        } else {
            fprintf(stderr, "global frame, no callee");
        }
        fputc('\n', stderr);

        fprintf(stderr, "file %s line %" PRIuSIZE "\n",
                i.script()->filename(), i.script()->lineno());

        if (jsbytecode* pc = i.pc()) {
            fprintf(stderr, "  pc = %p\n", pc);
            fprintf(stderr, "  current op: %s\n", js_CodeName[*pc]);
            MaybeDumpObject("staticScope", i.script()->getStaticBlockScope(pc));
        }
        MaybeDumpValue("this", i.thisv(cx));
        if (!i.isJit()) {
            fprintf(stderr, "  rval: ");
            dumpValue(i.interpFrame()->returnValue());
            fputc('\n', stderr);
        }

        fprintf(stderr, "  flags:");
        if (i.isConstructing())
            fprintf(stderr, " constructing");
        if (!i.isJit() && i.interpFrame()->isDebuggerEvalFrame())
            fprintf(stderr, " debugger eval");
        if (i.isEvalFrame())
            fprintf(stderr, " eval");
        fputc('\n', stderr);

        fprintf(stderr, "  scopeChain: (JSObject*) %p\n", (void*) i.scopeChain(cx));

        fputc('\n', stderr);
    }
}

#endif /* DEBUG */

JS_FRIEND_API(void)
js::DumpBacktrace(JSContext *cx)
{
    Sprinter sprinter(cx);
    sprinter.init();
    size_t depth = 0;
    for (AllFramesIter i(cx); !i.done(); ++i, ++depth) {
        const char* filename = JS_GetScriptFilename(i.script());
        unsigned line = PCToLineNumber(i.script(), i.pc());
        JSScript* script = i.script();
        sprinter.printf("#%d %14p   %s:%d (%p @ %d)\n",
                        depth, (i.isJit() ? 0 : i.interpFrame()), filename, line,
                        script, script->pcToOffset(i.pc()));
    }
    fprintf(stdout, "%s", sprinter.string());
#ifdef XP_WIN32
    if (IsDebuggerPresent()) {
        OutputDebugStringA(sprinter.string());
    }
#endif
}


/* * */

void
JSObject::addSizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf, JS::ClassInfo* info)
{
    if (is<NativeObject>() && as<NativeObject>().hasDynamicSlots())
        info->objectsMallocHeapSlots += mallocSizeOf(as<NativeObject>().slots_);

    if (is<NativeObject>() && as<NativeObject>().hasDynamicElements()) {
        js::ObjectElements* elements = as<NativeObject>().getElementsHeader();
        if (!elements->isCopyOnWrite() || elements->ownerObject() == this)
            info->objectsMallocHeapElementsNonAsmJS += mallocSizeOf(elements);
    }

    // Other things may be measured in the future if DMD indicates it is worthwhile.
    if (is<JSFunction>() ||
        is<PlainObject>() ||
        is<ArrayObject>() ||
        is<CallObject>() ||
        is<RegExpObject>() ||
        is<ProxyObject>())
    {
        // Do nothing.  But this function is hot, and we win by getting the
        // common cases out of the way early.  Some stats on the most common
        // classes, as measured during a vanilla browser session:
        // - (53.7%, 53.7%): Function
        // - (18.0%, 71.7%): Object
        // - (16.9%, 88.6%): Array
        // - ( 3.9%, 92.5%): Call
        // - ( 2.8%, 95.3%): RegExp
        // - ( 1.0%, 96.4%): Proxy

    } else if (is<ArgumentsObject>()) {
        info->objectsMallocHeapMisc += as<ArgumentsObject>().sizeOfMisc(mallocSizeOf);
    } else if (is<RegExpStaticsObject>()) {
        info->objectsMallocHeapMisc += as<RegExpStaticsObject>().sizeOfData(mallocSizeOf);
    } else if (is<PropertyIteratorObject>()) {
        info->objectsMallocHeapMisc += as<PropertyIteratorObject>().sizeOfMisc(mallocSizeOf);
    } else if (is<ArrayBufferObject>()) {
        ArrayBufferObject::addSizeOfExcludingThis(this, mallocSizeOf, info);
    } else if (is<SharedArrayBufferObject>()) {
        SharedArrayBufferObject::addSizeOfExcludingThis(this, mallocSizeOf, info);
    } else if (is<AsmJSModuleObject>()) {
        as<AsmJSModuleObject>().addSizeOfMisc(mallocSizeOf, &info->objectsNonHeapCodeAsmJS,
                                              &info->objectsMallocHeapMisc);
#ifdef JS_HAS_CTYPES
    } else {
        // This must be the last case.
        info->objectsMallocHeapMisc +=
            js::SizeOfDataIfCDataObject(mallocSizeOf, const_cast<JSObject*>(this));
#endif
    }
}

void
JSObject::markChildren(JSTracer *trc)
{
    MarkObjectGroup(trc, &group_, "group");

    const Class *clasp = group_->clasp();
    if (clasp->trace)
        clasp->trace(trc, this);

    if (clasp->isNative()) {
        NativeObject *nobj = &as<NativeObject>();

        MarkShape(trc, &nobj->shape_, "shape");

        MarkObjectSlots(trc, nobj, 0, nobj->slotSpan());

        do {
            if (nobj->denseElementsAreCopyOnWrite()) {
                HeapPtrNativeObject& owner = nobj->getElementsHeader()->ownerObject();
                if (owner != nobj) {
                    MarkObject(trc, &owner, "objectElementsOwner");
                    break;
                }
            }

            gc::MarkArraySlots(trc,
                               nobj->getDenseInitializedLength(),
                               nobj->getDenseElementsAllowCopyOnWrite(),
                               "objectElements");
        } while (false);
    }
}

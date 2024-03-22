/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/CacheIR.h"

#include "jit/BaselineIC.h"
#include "jit/IonCaches.h"

#include "jsobjinlines.h"

#include "vm/UnboxedObject-inl.h"

using namespace js;
using namespace js::jit;

using mozilla::Maybe;

GetPropIRGenerator::GetPropIRGenerator(JSContext* cx, jsbytecode* pc, HandleValue val, HandlePropertyName name,
                                       MutableHandleValue res)
  : cx_(cx),
    pc_(pc),
    val_(val),
    name_(name),
    res_(res),
    emitted_(false),
    preliminaryObjectAction_(PreliminaryObjectAction::None)
{}

static void
EmitLoadSlotResult(CacheIRWriter& writer, ObjOperandId holderOp, NativeObject* holder,
                   Shape* shape)
{
    if (holder->isFixedSlot(shape->slot())) {
        writer.loadFixedSlotResult(holderOp, NativeObject::getFixedSlotOffset(shape->slot()));
    } else {
        size_t dynamicSlotOffset = holder->dynamicSlotIndex(shape->slot()) * sizeof(Value);
        writer.loadDynamicSlotResult(holderOp, dynamicSlotOffset);
    }
}

bool
GetPropIRGenerator::tryAttachStub(Maybe<CacheIRWriter>& writer)
{
    AutoAssertNoPendingException aanpe(cx_);
    JS::AutoCheckCannotGC nogc;

    MOZ_ASSERT(!emitted_);

    writer.emplace();
    ValOperandId valId(writer->setInputOperandId(0));

    if (val_.isObject()) {
        RootedObject obj(cx_, &val_.toObject());
        ObjOperandId objId = writer->guardIsObject(valId);

        if (!emitted_ && !tryAttachNative(*writer, obj, objId))
            return false;
        if (!emitted_ && !tryAttachUnboxedExpando(*writer, obj, objId))
            return false;
    }

    return true;
}

static bool
IsCacheableNoProperty(JSContext* cx, JSObject* obj, JSObject* holder, Shape* shape, jsid id,
                      jsbytecode* pc)
{
    if (shape)
        return false;

    MOZ_ASSERT(!holder);

    // If we're doing a name lookup, we have to throw a ReferenceError.
    if (*pc == JSOP_GETXPROP)
        return false;

    return CheckHasNoSuchProperty(cx, obj, JSID_TO_ATOM(id)->asPropertyName());
}

enum NativeGetPropCacheability {
    CanAttachNone,
    CanAttachReadSlot,
};

static NativeGetPropCacheability
CanAttachNativeGetProp(JSContext* cx, HandleObject obj, HandleId id,
                       MutableHandleNativeObject holder, MutableHandleShape shape,
                       jsbytecode* pc, bool skipArrayLen = false)
{
    MOZ_ASSERT(JSID_IS_STRING(id) || JSID_IS_SYMBOL(id));

    // The lookup needs to be universally pure, otherwise we risk calling hooks out
    // of turn. We don't mind doing this even when purity isn't required, because we
    // only miss out on shape hashification, which is only a temporary perf cost.
    // The limits were arbitrarily set, anyways.
    JSObject* baseHolder = nullptr;
    if (!LookupPropertyPure(cx, obj, id, &baseHolder, shape.address()))
        return CanAttachNone;

    MOZ_ASSERT(!holder);
    if (baseHolder) {
        if (!baseHolder->isNative())
            return CanAttachNone;
        holder.set(&baseHolder->as<NativeObject>());
    }

    if (IsCacheableGetPropReadSlotForIonOrCacheIR(obj, holder, shape) ||
        IsCacheableNoProperty(cx, obj, holder, shape, id, pc))
    {
        return CanAttachReadSlot;
    }

    return CanAttachNone;
}

static void
GeneratePrototypeGuards(CacheIRWriter& writer, JSObject* obj, JSObject* holder, ObjOperandId objId)
{
    // The guards here protect against the effects of JSObject::swap(). If the
    // prototype chain is directly altered, then TI will toss the jitcode, so we
    // don't have to worry about it, and any other change to the holder, or
    // adding a shadowing property will result in reshaping the holder, and thus
    // the failure of the shape guard.
    MOZ_ASSERT(obj != holder);

    if (obj->hasUncacheableProto()) {
        // If the shape does not imply the proto, emit an explicit proto guard.
        writer.guardProto(objId, obj->getProto());
    }

    JSObject* pobj = IsCacheableDOMProxy(obj)
                     ? obj->getTaggedProto().toObjectOrNull()
                     : obj->getProto();
    if (!pobj)
        return;

    while (pobj != holder) {
        if (pobj->hasUncacheableProto()) {
            ObjOperandId protoId = writer.loadObject(pobj);
            if (pobj->isSingleton()) {
                // Singletons can have their group's |proto| mutated directly.
                writer.guardProto(protoId, pobj->getProto());
            } else {
                writer.guardGroup(protoId, pobj->group());
            }
        }
        pobj = pobj->getProto();
    }
}

static void
TestMatchingReceiver(CacheIRWriter& writer, JSObject* obj, Shape* shape, ObjOperandId objId)
{
    if (obj->is<UnboxedPlainObject>()) {
        writer.guardGroup(objId, obj->group());

        if (UnboxedExpandoObject* expando = obj->as<UnboxedPlainObject>().maybeExpando()) {
            ObjOperandId expandoId = writer.guardAndLoadUnboxedExpando(objId);
            writer.guardShape(expandoId, expando->lastProperty());
        } else {
            writer.guardNoUnboxedExpando(objId);
        }
    } else if (obj->is<UnboxedArrayObject>() || obj->is<TypedObject>()) {
        writer.guardGroup(objId, obj->group());
    } else {
        Shape* shape = obj->maybeShape();
        MOZ_ASSERT(shape);
        writer.guardShape(objId, shape);
    }
}

static void
EmitReadSlotResult(CacheIRWriter& writer, JSObject* obj, JSObject* holder,
                   Shape* shape, ObjOperandId objId)
{
    TestMatchingReceiver(writer, obj, shape, objId);

    ObjOperandId holderId;
    if (obj != holder) {
        GeneratePrototypeGuards(writer, obj, holder, objId);

        if (holder) {
            // Guard on the holder's shape.
            holderId = writer.loadObject(holder);
            writer.guardShape(holderId, holder->as<NativeObject>().lastProperty());
        } else {
            // The property does not exist. Guard on everything in the prototype
            // chain. This is guaranteed to see only Native objects because of
            // CanAttachNativeGetProp().
            JSObject* proto = obj->getTaggedProto().toObjectOrNull();
            ObjOperandId lastObjId = objId;
            while (proto) {
                ObjOperandId protoId = writer.loadProto(lastObjId);
                writer.guardShape(protoId, proto->as<NativeObject>().lastProperty());
                proto = proto->getProto();
                lastObjId = protoId;
            }
        }
    } else if (obj->is<UnboxedPlainObject>()) {
        holder = obj->as<UnboxedPlainObject>().maybeExpando();
        holderId = writer.loadUnboxedExpando(objId);
    } else {
        holderId = objId;
    }

    // Slot access.
    if (holder) {
        MOZ_ASSERT(holderId.valid());
        EmitLoadSlotResult(writer, holderId, &holder->as<NativeObject>(), shape);
    } else {
        MOZ_ASSERT(!holderId.valid());
        writer.loadUndefinedResult();
    }
}

bool
GetPropIRGenerator::tryAttachNative(CacheIRWriter& writer, HandleObject obj, ObjOperandId objId)
{
    MOZ_ASSERT(!emitted_);

    RootedShape shape(cx_);
    RootedNativeObject holder(cx_);

    RootedId id(cx_, NameToId(name_));
    NativeGetPropCacheability type = CanAttachNativeGetProp(cx_, obj, id, &holder, &shape, pc_);
    if (type == CanAttachNone)
        return true;

    emitted_ = true;

    switch (type) {
      case CanAttachReadSlot:
        if (holder) {
            EnsureTrackPropertyTypes(cx_, holder, NameToId(name_));
            if (obj == holder) {
                // See the comment in StripPreliminaryObjectStubs.
                if (IsPreliminaryObject(obj))
                    preliminaryObjectAction_ = PreliminaryObjectAction::NotePreliminary;
                else
                    preliminaryObjectAction_ = PreliminaryObjectAction::Unlink;
            }
        }
        EmitReadSlotResult(writer, obj, holder, shape, objId);
        break;
      default:
        MOZ_CRASH("Bad NativeGetPropCacheability");
    }

    return true;
}

bool
GetPropIRGenerator::tryAttachUnboxedExpando(CacheIRWriter& writer, HandleObject obj, ObjOperandId objId)
{
    MOZ_ASSERT(!emitted_);

    if (!obj->is<UnboxedPlainObject>())
        return true;

    UnboxedExpandoObject* expando = obj->as<UnboxedPlainObject>().maybeExpando();
    if (!expando)
        return true;

    Shape* shape = expando->lookup(cx_, NameToId(name_));
    if (!shape || !shape->hasDefaultGetter() || !shape->hasSlot())
        return true;

    emitted_ = true;

    EmitReadSlotResult(writer, obj, obj, shape, objId);
    return true;
}

/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef builtin_Promise_h
#define builtin_Promise_h

#include "builtin/SelfHostingDefines.h"
#include "vm/NativeObject.h"

namespace js {

class AutoSetNewObjectMetadata;

class PromiseObject : public NativeObject
{
  public:
    static const unsigned RESERVED_SLOTS = 12;
    static const Class class_;
    static const Class protoClass_;
    static PromiseObject* create(JSContext* cx, HandleObject executor,
                                 HandleObject proto = nullptr);

    JS::PromiseState state() {
        int32_t state = getFixedSlot(PROMISE_STATE_SLOT).toInt32();
        MOZ_ASSERT(state >= 0 && state <= int32_t(JS::PromiseState::Rejected));
        return JS::PromiseState(state);
    }
    Value value()  {
        MOZ_ASSERT(state() == JS::PromiseState::Fulfilled);
        return getFixedSlot(PROMISE_RESULT_SLOT);
    }
    Value reason() {
        MOZ_ASSERT(state() == JS::PromiseState::Rejected);
        return getFixedSlot(PROMISE_RESULT_SLOT);
    }

    MOZ_MUST_USE bool resolve(JSContext* cx, HandleValue resolutionValue);
    MOZ_MUST_USE bool reject(JSContext* cx, HandleValue rejectionValue);

    void onSettled(JSContext* cx);

    double allocationTime() { return getFixedSlot(PROMISE_ALLOCATION_TIME_SLOT).toNumber(); }
    double resolutionTime() { return getFixedSlot(PROMISE_RESOLUTION_TIME_SLOT).toNumber(); }
    JSObject* allocationSite() {
        return getFixedSlot(PROMISE_ALLOCATION_SITE_SLOT).toObjectOrNull();
    }
    JSObject* resolutionSite() {
        return getFixedSlot(PROMISE_RESOLUTION_SITE_SLOT).toObjectOrNull();
    }
    double lifetime();
    double timeToResolution() {
        MOZ_ASSERT(state() != JS::PromiseState::Pending);
        return resolutionTime() - allocationTime();
    }
    MOZ_MUST_USE bool dependentPromises(JSContext* cx, MutableHandle<GCVector<Value>> values);
    uint64_t getID();
    bool markedAsUncaught() {
        return getFixedSlot(PROMISE_IS_HANDLED_SLOT).toInt32() != PROMISE_IS_HANDLED_STATE_HANDLED;
    }
    void markAsReported() {
        MOZ_ASSERT(getFixedSlot(PROMISE_IS_HANDLED_SLOT).toInt32() ==
                   PROMISE_IS_HANDLED_STATE_UNHANDLED);
        setFixedSlot(PROMISE_IS_HANDLED_SLOT, Int32Value(PROMISE_IS_HANDLED_STATE_REPORTED));
    }
};

/**
 * Tells the embedding to enqueue a Promise reaction job, based on six
 * parameters:
 * reaction handler - The callback to invoke for this job.
   argument - The first and only argument to pass to the handler.
   resolve - The Promise cabability's resolve hook, called upon normal
             completion of the handler.
   reject -  The Promise cabability's reject hook, called if the handler
             throws.
   promise - The associated Promise, or null for some internal uses.
   objectFromIncumbentGlobal - An object from the global that was the
                               incumbent global when the Promise reaction job
                               was created (not enqueued). Not the global
                               itself because unwrapping that might unwrap an
                               inner to an outer window, which we never want
                               to happen.
 */
bool EnqueuePromiseReactionJob(JSContext* cx, HandleValue handler, HandleValue handlerArg,
                               HandleObject resolve, HandleObject reject,
                               HandleObject promise, HandleObject objectFromIncumbentGlobal);

/**
 * Tells the embedding to enqueue a Promise resolve thenable job, based on six
 * parameters:
 * promiseToResolve - The promise to resolve, obviously.
 * thenable - The thenable to resolve the Promise with.
 * then - The `then` function to invoke with the `thenable` as the receiver.
 */
bool EnqueuePromiseResolveThenableJob(JSContext* cx, HandleValue promiseToResolve,
                                      HandleValue thenable, HandleValue then);

} // namespace js

#endif /* builtin_Promise_h */

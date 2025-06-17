/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 *
 * Copyright 2016 Mozilla Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef wasm_compartment_h
#define wasm_compartment_h

#include "asmjs/WasmJS.h"

namespace js {

class WasmActivation;

namespace wasm {

// wasm::Compartment lives in JSCompartment and contains the wasm-related
// per-compartment state. wasm::Compartment tracks every live instance in the
// compartment and must be notified, via registerInstance(), of any new
// WasmInstanceObject.

class Compartment
{
    using InstanceObjectSet = GCHashSet<ReadBarriered<WasmInstanceObject*>,
                                        MovableCellHasher<ReadBarriered<WasmInstanceObject*>>,
                                        SystemAllocPolicy>;
    using WeakInstanceObjectSet = JS::WeakCache<InstanceObjectSet>;

    WeakInstanceObjectSet instances_;
    size_t                activationCount_;
    bool                  profilingEnabled_;

    friend class js::WasmActivation;

  public:
    explicit Compartment(Zone* zone);
    ~Compartment();
    void trace(JSTracer* trc);

    // Before a WasmInstanceObject can be considered fully constructed and
    // valid, it must be registered with the Compartment. If this method fails,
    // an error has been reported and the instance object must be abandoned.

    bool registerInstance(JSContext* cx, HandleWasmInstanceObject instanceObj);

    // Return a weak set of all live instances in the compartment.

    const WeakInstanceObjectSet& instances() const { return instances_; }

    // To ensure profiling is enabled (so that wasm frames are not lost in
    // profiling callstacks), ensureProfilingState must be called before calling
    // the first wasm function in a compartment.

    bool ensureProfilingState(JSContext* cx);

    // about:memory reporting

    void addSizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf, size_t* compartmentTables);
};

} // namespace wasm
} // namespace js

#endif // wasm_compartment_h

/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 *
 * Copyright 2015 Mozilla Foundation
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

#ifndef wasm_compile_h
#define wasm_compile_h

#include "asmjs/WasmJS.h"
#include "asmjs/WasmModule.h"

namespace js {
namespace wasm {

// Compile the given WebAssembly bytecode with the given assumptions, settings
// and filename into a wasm::Module.

struct CompileArgs
{
    Assumptions assumptions;
    UniqueChars filename;
    bool alwaysBaseline;

    CompileArgs(Assumptions&& assumptions, UniqueChars filename)
      : assumptions(Move(assumptions)),
        filename(Move(filename)),
        alwaysBaseline(false)
    {}

    // If CompileArgs is constructed without arguments, initFromContext() must
    // be called to complete initialization.
    CompileArgs() = default;
    bool initFromContext(ExclusiveContext* cx, UniqueChars filename);
};

SharedModule
Compile(const ShareableBytes& bytecode, CompileArgs&& args, UniqueChars* error);

}  // namespace wasm
}  // namespace js

#endif // namespace wasm_compile_h

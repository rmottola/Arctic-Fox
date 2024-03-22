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

#ifndef wasm_generator_h
#define wasm_generator_h

#include "asmjs/WasmBinary.h"
#include "asmjs/WasmIonCompile.h"
#include "asmjs/WasmModule.h"
#include "jit/MacroAssembler.h"

namespace js {
namespace wasm {

class FunctionGenerator;

// A slow function describes a function that took longer than msThreshold to
// validate and compile.

struct SlowFunction
{
    SlowFunction(uint32_t index, unsigned ms, unsigned lineOrBytecode)
     : index(index), ms(ms), lineOrBytecode(lineOrBytecode)
    {}

    static const unsigned msThreshold = 250;

    uint32_t index;
    unsigned ms;
    unsigned lineOrBytecode;
};
typedef Vector<SlowFunction> SlowFunctionVector;

// The ModuleGeneratorData holds all the state shared between the
// ModuleGenerator and ModuleGeneratorThreadView. The ModuleGeneratorData
// is encapsulated by ModuleGenerator/ModuleGeneratorThreadView classes which
// present a race-free interface to the code in each thread assuming any given
// element is initialized by the ModuleGenerator thread before an index to that
// element is written to Bytes sent to a ModuleGeneratorThreadView thread.
// Once created, the Vectors are never resized.

struct TableModuleGeneratorData
{
    uint32_t globalDataOffset;
    uint32_t numElems;
    Uint32Vector elemFuncIndices;

    TableModuleGeneratorData()
      : globalDataOffset(0), numElems(0)
    {}
    TableModuleGeneratorData(TableModuleGeneratorData&& rhs)
      : globalDataOffset(rhs.globalDataOffset), numElems(rhs.numElems),
        elemFuncIndices(Move(rhs.elemFuncIndices))
    {}
};

typedef Vector<TableModuleGeneratorData, 0, SystemAllocPolicy> TableModuleGeneratorDataVector;

struct ImportModuleGeneratorData
{
    const DeclaredSig* sig;
    uint32_t globalDataOffset;

    ImportModuleGeneratorData() : sig(nullptr), globalDataOffset(0) {}
    explicit ImportModuleGeneratorData(const DeclaredSig* sig) : sig(sig), globalDataOffset(0) {}
};

typedef Vector<ImportModuleGeneratorData, 0, SystemAllocPolicy> ImportModuleGeneratorDataVector;

struct AsmJSGlobalVariable
{
    ExprType type;
    unsigned globalDataOffset;
    bool isConst;
    AsmJSGlobalVariable(ExprType type, unsigned offset, bool isConst)
      : type(type), globalDataOffset(offset), isConst(isConst)
    {}
};

typedef Vector<AsmJSGlobalVariable, 0, SystemAllocPolicy> AsmJSGlobalVariableVector;

struct ModuleGeneratorData
{
    CompileArgs                     args;
    ModuleKind                      kind;
    uint32_t                        numTableElems;
    mozilla::Atomic<uint32_t>       minHeapLength;

    DeclaredSigVector               sigs;
    TableModuleGeneratorDataVector  sigToTable;
    DeclaredSigPtrVector            funcSigs;
    ImportModuleGeneratorDataVector imports;
    AsmJSGlobalVariableVector       globals;

    uint32_t funcSigIndex(uint32_t funcIndex) const {
        return funcSigs[funcIndex] - sigs.begin();
    }

    explicit ModuleGeneratorData(ExclusiveContext* cx, ModuleKind kind = ModuleKind::Wasm)
      : args(cx), kind(kind), numTableElems(0), minHeapLength(0)
    {}
};

typedef UniquePtr<ModuleGeneratorData> UniqueModuleGeneratorData;

// The ModuleGeneratorThreadView class presents a restricted, read-only view of
// the shared state needed by helper threads. There is only one
// ModuleGeneratorThreadView object owned by ModuleGenerator and referenced by
// all compile tasks.

class ModuleGeneratorThreadView
{
    const ModuleGeneratorData& shared_;

  public:
    explicit ModuleGeneratorThreadView(const ModuleGeneratorData& shared)
      : shared_(shared)
    {}
    CompileArgs args() const {
        return shared_.args;
    }
    bool isAsmJS() const {
        return shared_.kind == ModuleKind::AsmJS;
    }
    uint32_t numTableElems() const {
        MOZ_ASSERT(!isAsmJS());
        return shared_.numTableElems;
    }
    uint32_t minHeapLength() const {
        return shared_.minHeapLength;
    }
    const DeclaredSig& sig(uint32_t sigIndex) const {
        return shared_.sigs[sigIndex];
    }
    const TableModuleGeneratorData& sigToTable(uint32_t sigIndex) const {
        return shared_.sigToTable[sigIndex];
    }
    const DeclaredSig& funcSig(uint32_t funcIndex) const {
        MOZ_ASSERT(shared_.funcSigs[funcIndex]);
        return *shared_.funcSigs[funcIndex];
    }
    const ImportModuleGeneratorData& import(uint32_t importIndex) const {
        MOZ_ASSERT(shared_.imports[importIndex].sig);
        return shared_.imports[importIndex];
    }
    const AsmJSGlobalVariable& globalVar(uint32_t globalIndex) const {
        return shared_.globals[globalIndex];
    }
};

// A ModuleGenerator encapsulates the creation of a wasm module. During the
// lifetime of a ModuleGenerator, a sequence of FunctionGenerators are created
// and destroyed to compile the individual function bodies. After generating all
// functions, ModuleGenerator::finish() must be called to complete the
// compilation and extract the resulting wasm module.

class MOZ_STACK_CLASS ModuleGenerator
{
    typedef UniquePtr<ModuleGeneratorThreadView> UniqueModuleGeneratorThreadView;
    typedef HashMap<uint32_t, uint32_t> FuncIndexMap;

    ExclusiveContext*               cx_;
    jit::JitContext                 jcx_;

    // Data handed back to the caller in finish()
    UniqueModuleData                module_;
    UniqueExportMap                 exportMap_;
    SlowFunctionVector              slowFuncs_;

    // Data scoped to the ModuleGenerator's lifetime
    UniqueModuleGeneratorData       shared_;
    uint32_t                        numSigs_;
    LifoAlloc                       lifo_;
    jit::TempAllocator              alloc_;
    jit::MacroAssembler             masm_;
    Uint32Vector                    funcIndexToCodeRange_;
    FuncIndexMap                    funcIndexToExport_;
    uint32_t                        lastPatchedCallsite_;
    uint32_t                        startOfUnpatchedBranches_;
    JumpSiteArray                   jumpThunks_;

    // Parallel compilation
    bool                            parallel_;
    uint32_t                        outstanding_;
    UniqueModuleGeneratorThreadView threadView_;
    Vector<IonCompileTask>          tasks_;
    Vector<IonCompileTask*>         freeTasks_;

    // Assertions
    FunctionGenerator*              activeFunc_;
    bool                            finishedFuncs_;

    bool finishOutstandingTask();
    bool funcIsDefined(uint32_t funcIndex) const;
    uint32_t funcEntry(uint32_t funcIndex) const;
    bool convertOutOfRangeBranchesToThunks();
    bool finishTask(IonCompileTask* task);
    bool finishCodegen(StaticLinkData* link);
    bool finishStaticLinkData(uint8_t* code, uint32_t codeBytes, StaticLinkData* link);
    bool addImport(const Sig& sig, uint32_t globalDataOffset);
    bool startedFuncDefs() const { return !!threadView_; }
    bool allocateGlobalBytes(uint32_t bytes, uint32_t align, uint32_t* globalDataOffset);

  public:
    explicit ModuleGenerator(ExclusiveContext* cx);
    ~ModuleGenerator();

    bool init(UniqueModuleGeneratorData shared, UniqueChars filename);

    bool isAsmJS() const { return module_->kind == ModuleKind::AsmJS; }
    CompileArgs args() const { return module_->compileArgs; }
    jit::MacroAssembler& masm() { return masm_; }

    // Heap usage:
    void initHeapUsage(HeapUsage heapUsage);
    bool usesHeap() const;

    // Signatures:
    uint32_t numSigs() const { return numSigs_; }
    const DeclaredSig& sig(uint32_t sigIndex) const;

    // Function declarations:
    uint32_t numFuncSigs() const { return module_->numFuncs; }
    const DeclaredSig& funcSig(uint32_t funcIndex) const;

    // Imports:
    uint32_t numImports() const;
    const ImportModuleGeneratorData& import(uint32_t index) const;

    // Exports:
    bool declareExport(UniqueChars fieldName, uint32_t funcIndex, uint32_t* exportIndex = nullptr);
    uint32_t numExports() const;
    bool addMemoryExport(UniqueChars fieldName);

    // Function definitions:
    bool startFuncDefs();
    bool startFuncDef(uint32_t lineOrBytecode, FunctionGenerator* fg);
    bool finishFuncDef(uint32_t funcIndex, unsigned generateTime, FunctionGenerator* fg);
    bool finishFuncDefs();

    // Function-pointer tables:
    static const uint32_t BadIndirectCall = UINT32_MAX;

    // asm.js lazy initialization:
    void initSig(uint32_t sigIndex, Sig&& sig);
    bool initFuncSig(uint32_t funcIndex, uint32_t sigIndex);
    bool initImport(uint32_t importIndex, uint32_t sigIndex);
    bool initSigTableLength(uint32_t sigIndex, uint32_t numElems);
    void initSigTableElems(uint32_t sigIndex, Uint32Vector&& elemFuncIndices);
    void bumpMinHeapLength(uint32_t newMinHeapLength);

    // asm.js global variables:
    bool allocateGlobalVar(ValType type, bool isConst, uint32_t* index);
    const AsmJSGlobalVariable& globalVar(unsigned index) const { return shared_->globals[index]; }

    // Return a ModuleData object which may be used to construct a Module, the
    // StaticLinkData required to call Module::staticallyLink, and the list of
    // functions that took a long time to compile.
    bool finish(CacheableCharsVector&& prettyFuncNames,
                UniqueModuleData* module,
                UniqueStaticLinkData* staticLinkData,
                UniqueExportMap* exportMap,
                SlowFunctionVector* slowFuncs);
};

// A FunctionGenerator encapsulates the generation of a single function body.
// ModuleGenerator::startFunc must be called after construction and before doing
// anything else. After the body is complete, ModuleGenerator::finishFunc must
// be called before the FunctionGenerator is destroyed and the next function is
// started.

class MOZ_STACK_CLASS FunctionGenerator
{
    friend class ModuleGenerator;

    ModuleGenerator* m_;
    IonCompileTask*  task_;

    // Data created during function generation, then handed over to the
    // FuncBytes in ModuleGenerator::finishFunc().
    Bytes            bytes_;
    Uint32Vector     callSiteLineNums_;

    uint32_t lineOrBytecode_;

  public:
    FunctionGenerator()
      : m_(nullptr), task_(nullptr), lineOrBytecode_(0)
    {}

    Bytes& bytes() {
        return bytes_;
    }
    bool addCallSiteLineNum(uint32_t lineno) {
        return callSiteLineNums_.append(lineno);
    }
};

} // namespace wasm
} // namespace js

#endif // wasm_generator_h

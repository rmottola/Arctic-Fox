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
#include "asmjs/WasmCompile.h"
#include "asmjs/WasmModule.h"
#include "jit/MacroAssembler.h"

namespace js {
namespace wasm {

class FunctionGenerator;

// The ModuleGeneratorData holds all the state shared between the
// ModuleGenerator thread and background compile threads. The background
// threads are given a read-only view of the ModuleGeneratorData and the
// ModuleGenerator is careful to initialize, and never subsequently mutate,
// any given datum before being read by a background thread. In particular,
// once created, the Vectors are never resized.

struct TableGenDesc
{
    uint32_t globalDataOffset;
    uint32_t numElems;

    TableGenDesc()
      : globalDataOffset(0), numElems(0)
    {}
};

typedef Vector<TableGenDesc, 0, SystemAllocPolicy> TableGenDescVector;

struct FuncImportGenDesc
{
    const DeclaredSig* sig;
    uint32_t globalDataOffset;

    FuncImportGenDesc() : sig(nullptr), globalDataOffset(0) {}
    explicit FuncImportGenDesc(const DeclaredSig* sig) : sig(sig), globalDataOffset(0) {}
};

typedef Vector<FuncImportGenDesc, 0, SystemAllocPolicy> FuncImportGenDescVector;

struct ModuleGeneratorData
{
    ModuleKind                kind;
    SignalUsage               usesSignal;
    MemoryUsage               memoryUsage;
    mozilla::Atomic<uint32_t> minMemoryLength;
    uint32_t                  maxMemoryLength;

    DeclaredSigVector         sigs;
    DeclaredSigPtrVector      funcSigs;
    FuncImportGenDescVector   funcImports;
    GlobalDescVector          globals;

    TableGenDesc              wasmTable;
    TableGenDescVector        asmJSSigToTable;

    uint32_t funcSigIndex(uint32_t funcIndex) const {
        return funcSigs[funcIndex] - sigs.begin();
    }

    explicit ModuleGeneratorData(SignalUsage usesSignal, ModuleKind kind = ModuleKind::Wasm)
      : kind(kind),
        usesSignal(usesSignal),
        memoryUsage(MemoryUsage::None),
        minMemoryLength(0),
        maxMemoryLength(UINT32_MAX)
    {}
};

typedef UniquePtr<ModuleGeneratorData> UniqueModuleGeneratorData;

// A ModuleGenerator encapsulates the creation of a wasm module. During the
// lifetime of a ModuleGenerator, a sequence of FunctionGenerators are created
// and destroyed to compile the individual function bodies. After generating all
// functions, ModuleGenerator::finish() must be called to complete the
// compilation and extract the resulting wasm module.

class MOZ_STACK_CLASS ModuleGenerator
{
    typedef HashMap<uint32_t, uint32_t, DefaultHasher<uint32_t>, SystemAllocPolicy> FuncIndexMap;
    typedef Vector<IonCompileTask, 0, SystemAllocPolicy> IonCompileTaskVector;
    typedef Vector<IonCompileTask*, 0, SystemAllocPolicy> IonCompileTaskPtrVector;

    // Constant parameters
    bool                            alwaysBaseline_;

    // Data that is moved into the result of finish()
    LinkData                        linkData_;
    MutableMetadata                 metadata_;
    ExportMap                       exportMap_;
    DataSegmentVector               dataSegments_;
    ElemSegmentVector               elemSegments_;

    // Data scoped to the ModuleGenerator's lifetime
    UniqueModuleGeneratorData       shared_;
    uint32_t                        numSigs_;
    LifoAlloc                       lifo_;
    jit::JitContext                 jcx_;
    jit::TempAllocator              masmAlloc_;
    jit::MacroAssembler             masm_;
    Uint32Vector                    funcIndexToCodeRange_;
    FuncIndexMap                    funcIndexToExport_;
    uint32_t                        lastPatchedCallsite_;
    uint32_t                        startOfUnpatchedBranches_;
    JumpSiteArray                   jumpThunks_;

    // Parallel compilation
    bool                            parallel_;
    uint32_t                        outstanding_;
    IonCompileTaskVector            tasks_;
    IonCompileTaskPtrVector         freeTasks_;

    // Assertions
    DebugOnly<FunctionGenerator*>   activeFunc_;
    DebugOnly<bool>                 startedFuncDefs_;
    DebugOnly<bool>                 finishedFuncDefs_;

    MOZ_MUST_USE bool finishOutstandingTask();
    bool funcIsDefined(uint32_t funcIndex) const;
    const CodeRange& funcCodeRange(uint32_t funcIndex) const;
    MOZ_MUST_USE bool convertOutOfRangeBranchesToThunks();
    MOZ_MUST_USE bool finishTask(IonCompileTask* task);
    MOZ_MUST_USE bool finishCodegen();
    MOZ_MUST_USE bool finishLinkData(Bytes& code);
    MOZ_MUST_USE bool addFuncImport(const Sig& sig, uint32_t globalDataOffset);
    MOZ_MUST_USE bool allocateGlobalBytes(uint32_t bytes, uint32_t align, uint32_t* globalDataOff);

  public:
    explicit ModuleGenerator();
    ~ModuleGenerator();

    MOZ_MUST_USE bool init(UniqueModuleGeneratorData shared, CompileArgs&& args,
                           Metadata* maybeAsmJSMetadata = nullptr);

    bool isAsmJS() const { return metadata_->kind == ModuleKind::AsmJS; }
    SignalUsage usesSignal() const { return metadata_->assumptions.usesSignal; }
    jit::MacroAssembler& masm() { return masm_; }

    // Memory:
    bool usesMemory() const { return UsesMemory(shared_->memoryUsage); }
    uint32_t minMemoryLength() const { return shared_->minMemoryLength; }

    // Signatures:
    uint32_t numSigs() const { return numSigs_; }
    const DeclaredSig& sig(uint32_t sigIndex) const;

    // Function declarations:
    uint32_t numFuncSigs() const { return shared_->funcSigs.length(); }
    const DeclaredSig& funcSig(uint32_t funcIndex) const;

    // Globals:
    MOZ_MUST_USE bool allocateGlobal(ValType type, bool isConst, uint32_t* index);
    const GlobalDesc& global(unsigned index) const { return shared_->globals[index]; }

    // Imports:
    uint32_t numFuncImports() const;
    const FuncImportGenDesc& funcImport(uint32_t funcImportIndex) const;

    // Exports:
    MOZ_MUST_USE bool declareExport(UniqueChars fieldName, uint32_t funcIndex,
                                    uint32_t* exportIndex = nullptr);
    uint32_t numExports() const;
    MOZ_MUST_USE bool addMemoryExport(UniqueChars fieldName);

    // Function definitions:
    MOZ_MUST_USE bool startFuncDefs();
    MOZ_MUST_USE bool startFuncDef(uint32_t lineOrBytecode, FunctionGenerator* fg);
    MOZ_MUST_USE bool finishFuncDef(uint32_t funcIndex, FunctionGenerator* fg);
    MOZ_MUST_USE bool finishFuncDefs();

    // Segments:
    MOZ_MUST_USE bool addDataSegment(DataSegment s) { return dataSegments_.append(s); }
    MOZ_MUST_USE bool addElemSegment(Uint32Vector&& elemFuncIndices);

    // Function names:
    void setFuncNames(NameInBytecodeVector&& funcNames);

    // asm.js lazy initialization:
    void initSig(uint32_t sigIndex, Sig&& sig);
    void initFuncSig(uint32_t funcIndex, uint32_t sigIndex);
    MOZ_MUST_USE bool initImport(uint32_t importIndex, uint32_t sigIndex);
    MOZ_MUST_USE bool initSigTableLength(uint32_t sigIndex, uint32_t numElems);
    MOZ_MUST_USE bool initSigTableElems(uint32_t sigIndex, Uint32Vector&& elemFuncIndices);
    void initMemoryUsage(MemoryUsage memoryUsage);
    void bumpMinMemoryLength(uint32_t newMinMemoryLength);

    // Finish compilation, provided the list of imports and source bytecode.
    // Both these Vectors may be empty (viz., b/c asm.js does different things
    // for imports and source).
    UniqueModule finish(ImportVector&& imports, const ShareableBytes& bytecode);
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
    bool             usesSimd_;
    bool             usesAtomics_;

    // Data created during function generation, then handed over to the
    // FuncBytes in ModuleGenerator::finishFunc().
    Bytes            bytes_;
    Uint32Vector     callSiteLineNums_;

    uint32_t lineOrBytecode_;

  public:
    FunctionGenerator()
      : m_(nullptr), task_(nullptr), usesSimd_(false), usesAtomics_(false), lineOrBytecode_(0)
    {}

    bool usesSimd() const {
        return usesSimd_;
    }
    void setUsesSimd() {
        usesSimd_ = true;
    }

    bool usesAtomics() const {
        return usesAtomics_;
    }
    void setUsesAtomics() {
        usesAtomics_ = true;
    }

    bool usesSignalsForInterrupts() const {
        return m_->usesSignal().forInterrupt;
    }

    Bytes& bytes() {
        return bytes_;
    }
    MOZ_MUST_USE bool addCallSiteLineNum(uint32_t lineno) {
        return callSiteLineNums_.append(lineno);
    }
};

} // namespace wasm
} // namespace js

#endif // wasm_generator_h

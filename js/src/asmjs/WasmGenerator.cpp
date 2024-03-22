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

#include "asmjs/WasmGenerator.h"

#include "mozilla/EnumeratedRange.h"

#include "asmjs/WasmStubs.h"

#include "jit/MacroAssembler-inl.h"

using namespace js;
using namespace js::jit;
using namespace js::wasm;

using mozilla::MakeEnumeratedRange;

// ****************************************************************************
// ModuleGenerator

static const unsigned GENERATOR_LIFO_DEFAULT_CHUNK_SIZE = 4 * 1024;
static const unsigned COMPILATION_LIFO_DEFAULT_CHUNK_SIZE = 64 * 1024;

const unsigned ModuleGenerator::BadIndirectCall;

ModuleGenerator::ModuleGenerator(ExclusiveContext* cx)
  : cx_(cx),
    jcx_(CompileRuntime::get(cx->compartment()->runtimeFromAnyThread())),
    slowFuncs_(cx),
    numSigs_(0),
    lifo_(GENERATOR_LIFO_DEFAULT_CHUNK_SIZE),
    alloc_(&lifo_),
    masm_(MacroAssembler::AsmJSToken(), alloc_),
    funcIndexToExport_(cx),
    lastPatchedCallsite_(0),
    startOfUnpatchedBranches_(0),
    parallel_(false),
    outstanding_(0),
    tasks_(cx),
    freeTasks_(cx),
    activeFunc_(nullptr),
    finishedFuncs_(false)
{
    MOZ_ASSERT(IsCompilingAsmJS());
}

ModuleGenerator::~ModuleGenerator()
{
    if (parallel_) {
        // Wait for any outstanding jobs to fail or complete.
        if (outstanding_) {
            AutoLockHelperThreadState lock;
            while (true) {
                IonCompileTaskVector& worklist = HelperThreadState().wasmWorklist();
                MOZ_ASSERT(outstanding_ >= worklist.length());
                outstanding_ -= worklist.length();
                worklist.clear();

                IonCompileTaskVector& finished = HelperThreadState().wasmFinishedList();
                MOZ_ASSERT(outstanding_ >= finished.length());
                outstanding_ -= finished.length();
                finished.clear();

                uint32_t numFailed = HelperThreadState().harvestFailedWasmJobs();
                MOZ_ASSERT(outstanding_ >= numFailed);
                outstanding_ -= numFailed;

                if (!outstanding_)
                    break;

                HelperThreadState().wait(GlobalHelperThreadState::CONSUMER);
            }
        }

        MOZ_ASSERT(HelperThreadState().wasmCompilationInProgress);
        HelperThreadState().wasmCompilationInProgress = false;
    } else {
        MOZ_ASSERT(!outstanding_);
    }
}

static bool
ParallelCompilationEnabled(ExclusiveContext* cx)
{
    // Since there are a fixed number of helper threads and one is already being
    // consumed by this parsing task, ensure that there another free thread to
    // avoid deadlock. (Note: there is at most one thread used for parsing so we
    // don't have to worry about general dining philosophers.)
    if (HelperThreadState().threadCount <= 1 || !CanUseExtraThreads())
        return false;

    // If 'cx' isn't a JSContext, then we are already off the main thread so
    // off-thread compilation must be enabled.
    return !cx->isJSContext() || cx->asJSContext()->runtime()->canUseOffthreadIonCompilation();
}

bool
ModuleGenerator::init(UniqueModuleGeneratorData shared, UniqueChars filename)
{
    if (!funcIndexToExport_.init())
        return false;

    module_ = MakeUnique<ModuleData>();
    if (!module_)
        return false;

    module_->globalBytes = InitialGlobalDataBytes;
    module_->compileArgs = shared->args;
    module_->kind = shared->kind;
    module_->heapUsage = HeapUsage::None;
    module_->filename = Move(filename);

    exportMap_ = MakeUnique<ExportMap>();
    if (!exportMap_)
        return false;

    shared_ = Move(shared);

    // For asm.js, the Vectors in ModuleGeneratorData are max-sized reservations
    // and will be initialized in a linear order via init* functions as the
    // module is generated. For wasm, the Vectors are correctly-sized and
    // already initialized.

    if (module_->kind == ModuleKind::Wasm) {
        numSigs_ = shared_->sigs.length();
        module_->numFuncs = shared_->funcSigs.length();
        module_->globalBytes = AlignBytes(module_->globalBytes, sizeof(void*));

        for (ImportModuleGeneratorData& import : shared_->imports) {
            MOZ_ASSERT(!import.globalDataOffset);
            import.globalDataOffset = module_->globalBytes;
            module_->globalBytes += Module::SizeOfImportExit;
            if (!addImport(*import.sig, import.globalDataOffset))
                return false;
        }

        MOZ_ASSERT(module_->globalBytes % sizeof(void*) == 0);

        for (TableModuleGeneratorData& table : shared_->sigToTable) {
            MOZ_ASSERT(table.numElems == table.elemFuncIndices.length());
            if (!table.numElems)
                continue;
            MOZ_ASSERT(!table.globalDataOffset);
            table.globalDataOffset = module_->globalBytes;
            module_->globalBytes += table.numElems * sizeof(void*);
        }
    }

    return true;
}

bool
ModuleGenerator::finishOutstandingTask()
{
    MOZ_ASSERT(parallel_);

    IonCompileTask* task = nullptr;
    {
        AutoLockHelperThreadState lock;
        while (true) {
            MOZ_ASSERT(outstanding_ > 0);

            if (HelperThreadState().wasmFailed())
                return false;

            if (!HelperThreadState().wasmFinishedList().empty()) {
                outstanding_--;
                task = HelperThreadState().wasmFinishedList().popCopy();
                break;
            }

            HelperThreadState().wait(GlobalHelperThreadState::CONSUMER);
        }
    }

    return finishTask(task);
}

static const uint32_t BadCodeRange = UINT32_MAX;

bool
ModuleGenerator::funcIsDefined(uint32_t funcIndex) const
{
    return funcIndex < funcIndexToCodeRange_.length() &&
           funcIndexToCodeRange_[funcIndex] != BadCodeRange;
}

uint32_t
ModuleGenerator::funcEntry(uint32_t funcIndex) const
{
    MOZ_ASSERT(funcIsDefined(funcIndex));
    return module_->codeRanges[funcIndexToCodeRange_[funcIndex]].funcNonProfilingEntry();
}

static uint32_t
JumpRange()
{
    return Min(JitOptions.jumpThreshold, JumpImmediateRange);
}

typedef HashMap<uint32_t, uint32_t> OffsetMap;

bool
ModuleGenerator::convertOutOfRangeBranchesToThunks()
{
    masm_.haltingAlign(CodeAlignment);

    // Create thunks for callsites that have gone out of range. Use a map to
    // create one thunk for each callee since there is often high reuse.

    OffsetMap alreadyThunked(cx_);
    if (!alreadyThunked.init())
        return false;

    for (; lastPatchedCallsite_ < masm_.callSites().length(); lastPatchedCallsite_++) {
        const CallSiteAndTarget& cs = masm_.callSites()[lastPatchedCallsite_];
        if (!cs.isInternal())
            continue;

        uint32_t callerOffset = cs.returnAddressOffset();
        MOZ_RELEASE_ASSERT(callerOffset < INT32_MAX);

        if (funcIsDefined(cs.targetIndex())) {
            uint32_t calleeOffset = funcEntry(cs.targetIndex());
            MOZ_RELEASE_ASSERT(calleeOffset < INT32_MAX);

            if (uint32_t(abs(int32_t(calleeOffset) - int32_t(callerOffset))) < JumpRange()) {
                masm_.patchCall(callerOffset, calleeOffset);
                continue;
            }
        }

        OffsetMap::AddPtr p = alreadyThunked.lookupForAdd(cs.targetIndex());
        if (!p) {
            Offsets offsets;
            offsets.begin = masm_.currentOffset();
            uint32_t thunkOffset = masm_.thunkWithPatch().offset();
            if (masm_.oom())
                return false;
            offsets.end = masm_.currentOffset();

            if (!module_->codeRanges.emplaceBack(CodeRange::CallThunk, offsets))
                return false;
            if (!module_->callThunks.emplaceBack(thunkOffset, cs.targetIndex()))
                return false;
            if (!alreadyThunked.add(p, cs.targetIndex(), offsets.begin))
                return false;
        }

        masm_.patchCall(callerOffset, p->value());
    }

    // Create thunks for jumps to stubs. Stubs are always generated at the end
    // so unconditionally thunk all existing jump sites.

    for (JumpTarget target : MakeEnumeratedRange(JumpTarget::Limit)) {
        if (masm_.jumpSites()[target].empty())
            continue;

        for (uint32_t jumpSite : masm_.jumpSites()[target]) {
            RepatchLabel label;
            label.use(jumpSite);
            masm_.bind(&label);
        }

        Offsets offsets;
        offsets.begin = masm_.currentOffset();
        uint32_t thunkOffset = masm_.thunkWithPatch().offset();
        if (masm_.oom())
            return false;
        offsets.end = masm_.currentOffset();

        if (!module_->codeRanges.emplaceBack(CodeRange::Inline, offsets))
            return false;
        if (!jumpThunks_[target].append(thunkOffset))
            return false;
    }

    // Unlike callsites, which need to be persisted in the Module, we can simply
    // flush jump sites after each patching pass.
    masm_.clearJumpSites();

    return true;
}

bool
ModuleGenerator::finishTask(IonCompileTask* task)
{
    const FuncBytes& func = task->func();
    FuncCompileResults& results = task->results();

    // Before merging in the new function's code, if jumps/calls in a previous
    // function's body might go out of range, patch these to thunks which have
    // full range.
    if ((masm_.size() - startOfUnpatchedBranches_) + results.masm().size() > JumpRange()) {
        startOfUnpatchedBranches_ = masm_.size();
        if (!convertOutOfRangeBranchesToThunks())
            return false;
    }

    // Offset the recorded FuncOffsets by the offset of the function in the
    // whole module's code segment.
    uint32_t offsetInWhole = masm_.size();
    results.offsets().offsetBy(offsetInWhole);

    // Add the CodeRange for this function.
    uint32_t funcCodeRangeIndex = module_->codeRanges.length();
    if (!module_->codeRanges.emplaceBack(func.index(), func.lineOrBytecode(), results.offsets()))
        return false;

    // Maintain a mapping from function index to CodeRange index.
    if (func.index() >= funcIndexToCodeRange_.length()) {
        uint32_t n = func.index() - funcIndexToCodeRange_.length() + 1;
        if (!funcIndexToCodeRange_.appendN(BadCodeRange, n))
            return false;
    }
    MOZ_ASSERT(!funcIsDefined(func.index()));
    funcIndexToCodeRange_[func.index()] = funcCodeRangeIndex;

    // Merge the compiled results into the whole-module masm.
    mozilla::DebugOnly<size_t> sizeBefore = masm_.size();
    if (!masm_.asmMergeWith(results.masm()))
        return false;
    MOZ_ASSERT(masm_.size() == offsetInWhole + results.masm().size());

    // Keep a record of slow functions for printing in the final console message.
    unsigned totalTime = func.generateTime() + results.compileTime();
    if (totalTime >= SlowFunction::msThreshold) {
        if (!slowFuncs_.emplaceBack(func.index(), totalTime, func.lineOrBytecode()))
            return false;
    }

    freeTasks_.infallibleAppend(task);
    return true;
}

bool
ModuleGenerator::finishCodegen(StaticLinkData* link)
{
    uint32_t offsetInWhole = masm_.size();

    // Generate stubs in a separate MacroAssembler since, otherwise, for modules
    // larger than the JumpImmediateRange, even local uses of Label will fail
    // due to the large absolute offsets temporarily stored by Label::bind().

    Vector<Offsets> entries(cx_);
    Vector<ProfilingOffsets> interpExits(cx_);
    Vector<ProfilingOffsets> jitExits(cx_);
    EnumeratedArray<JumpTarget, JumpTarget::Limit, Offsets> jumpTargets;
    ProfilingOffsets badIndirectCallExit;
    Offsets interruptExit;

    {
        TempAllocator alloc(&lifo_);
        MacroAssembler masm(MacroAssembler::AsmJSToken(), alloc);

        if (!entries.resize(numExports()))
            return false;
        for (uint32_t i = 0; i < numExports(); i++) {
            uint32_t target = exportMap_->exportFuncIndices[i];
            const Sig& sig = module_->exports[i].sig();
            entries[i] = GenerateEntry(masm, target, sig, usesHeap());
        }

        if (!interpExits.resize(numImports()))
            return false;
        if (!jitExits.resize(numImports()))
            return false;
        for (uint32_t i = 0; i < numImports(); i++) {
            interpExits[i] = GenerateInterpExit(masm, module_->imports[i], i);
            jitExits[i] = GenerateJitExit(masm, module_->imports[i], usesHeap());
        }

        for (JumpTarget target : MakeEnumeratedRange(JumpTarget::Limit))
            jumpTargets[target] = GenerateJumpTarget(masm, target);

        badIndirectCallExit = GenerateBadIndirectCallExit(masm);
        interruptExit = GenerateInterruptStub(masm);

        if (masm.oom() || !masm_.asmMergeWith(masm))
            return false;
    }

    // Adjust each of the resulting Offsets (to account for being merged into
    // masm_) and then create code ranges for all the stubs.

    for (uint32_t i = 0; i < numExports(); i++) {
        entries[i].offsetBy(offsetInWhole);
        module_->exports[i].initStubOffset(entries[i].begin);
        if (!module_->codeRanges.emplaceBack(CodeRange::Entry, entries[i]))
            return false;
    }

    for (uint32_t i = 0; i < numImports(); i++) {
        interpExits[i].offsetBy(offsetInWhole);
        module_->imports[i].initInterpExitOffset(interpExits[i].begin);
        if (!module_->codeRanges.emplaceBack(CodeRange::ImportInterpExit, interpExits[i]))
            return false;

        jitExits[i].offsetBy(offsetInWhole);
        module_->imports[i].initJitExitOffset(jitExits[i].begin);
        if (!module_->codeRanges.emplaceBack(CodeRange::ImportJitExit, jitExits[i]))
            return false;
    }

    for (JumpTarget target : MakeEnumeratedRange(JumpTarget::Limit)) {
        jumpTargets[target].offsetBy(offsetInWhole);
        if (!module_->codeRanges.emplaceBack(CodeRange::Inline, jumpTargets[target]))
            return false;
    }

    badIndirectCallExit.offsetBy(offsetInWhole);
    if (!module_->codeRanges.emplaceBack(CodeRange::ErrorExit, badIndirectCallExit))
        return false;

    interruptExit.offsetBy(offsetInWhole);
    if (!module_->codeRanges.emplaceBack(CodeRange::Inline, interruptExit))
        return false;

    // Fill in StaticLinkData with the offsets of these stubs.

    link->pod.outOfBoundsOffset = jumpTargets[JumpTarget::OutOfBounds].begin;
    link->pod.interruptOffset = interruptExit.begin;

    for (uint32_t sigIndex = 0; sigIndex < numSigs_; sigIndex++) {
        const TableModuleGeneratorData& table = shared_->sigToTable[sigIndex];
        if (table.elemFuncIndices.empty())
            continue;

        Uint32Vector elemOffsets;
        if (!elemOffsets.resize(table.elemFuncIndices.length()))
            return false;

        for (size_t i = 0; i < table.elemFuncIndices.length(); i++) {
            uint32_t funcIndex = table.elemFuncIndices[i];
            if (funcIndex == BadIndirectCall)
                elemOffsets[i] = badIndirectCallExit.begin;
            else
                elemOffsets[i] = funcEntry(funcIndex);
        }

        if (!link->funcPtrTables.emplaceBack(table.globalDataOffset, Move(elemOffsets)))
            return false;
    }

    // Only call convertOutOfRangeBranchesToThunks after all other codegen that may
    // emit new jumps to JumpTargets has finished.

    if (!convertOutOfRangeBranchesToThunks())
        return false;

    // Now that all thunks have been generated, patch all the thunks.

    for (CallThunk& callThunk : module_->callThunks) {
        uint32_t funcIndex = callThunk.u.funcIndex;
        callThunk.u.codeRangeIndex = funcIndexToCodeRange_[funcIndex];
        masm_.patchThunk(callThunk.offset, funcEntry(funcIndex));
    }

    for (JumpTarget target : MakeEnumeratedRange(JumpTarget::Limit)) {
        for (uint32_t thunkOffset : jumpThunks_[target])
            masm_.patchThunk(thunkOffset, jumpTargets[target].begin);
    }

    // Code-generation is complete!

    masm_.finish();
    return !masm_.oom();
}

bool
ModuleGenerator::finishStaticLinkData(uint8_t* code, uint32_t codeBytes, StaticLinkData* link)
{
    // Add links to absolute addresses identified symbolically.
    StaticLinkData::SymbolicLinkArray& symbolicLinks = link->symbolicLinks;
    for (size_t i = 0; i < masm_.numAsmJSAbsoluteAddresses(); i++) {
        AsmJSAbsoluteAddress src = masm_.asmJSAbsoluteAddress(i);
        if (!symbolicLinks[src.target].append(src.patchAt.offset()))
            return false;
    }

    // Relative link metadata: absolute addresses that refer to another point within
    // the asm.js module.

    // CodeLabels are used for switch cases and loads from floating-point /
    // SIMD values in the constant pool.
    for (size_t i = 0; i < masm_.numCodeLabels(); i++) {
        CodeLabel cl = masm_.codeLabel(i);
        StaticLinkData::InternalLink inLink(StaticLinkData::InternalLink::CodeLabel);
        inLink.patchAtOffset = masm_.labelToPatchOffset(*cl.patchAt());
        inLink.targetOffset = cl.target()->offset();
        if (!link->internalLinks.append(inLink))
            return false;
    }

#if defined(JS_CODEGEN_X86)
    // Global data accesses in x86 need to be patched with the absolute
    // address of the global. Globals are allocated sequentially after the
    // code section so we can just use an InternalLink.
    for (size_t i = 0; i < masm_.numAsmJSGlobalAccesses(); i++) {
        AsmJSGlobalAccess a = masm_.asmJSGlobalAccess(i);
        StaticLinkData::InternalLink inLink(StaticLinkData::InternalLink::RawPointer);
        inLink.patchAtOffset = masm_.labelToPatchOffset(a.patchAt);
        inLink.targetOffset = codeBytes + a.globalDataOffset;
        if (!link->internalLinks.append(inLink))
            return false;
    }
#endif

#if defined(JS_CODEGEN_X64)
    // Global data accesses on x64 use rip-relative addressing and thus do
    // not need patching after deserialization.
    uint8_t* globalData = code + codeBytes;
    for (size_t i = 0; i < masm_.numAsmJSGlobalAccesses(); i++) {
        AsmJSGlobalAccess a = masm_.asmJSGlobalAccess(i);
        masm_.patchAsmJSGlobalAccess(a.patchAt, code, globalData, a.globalDataOffset);
    }
#endif

    return true;
}

bool
ModuleGenerator::addImport(const Sig& sig, uint32_t globalDataOffset)
{
    Sig copy;
    if (!copy.clone(sig))
        return false;

    return module_->imports.emplaceBack(Move(copy), globalDataOffset);
}

bool
ModuleGenerator::allocateGlobalBytes(uint32_t bytes, uint32_t align, uint32_t* globalDataOffset)
{
    uint32_t globalBytes = module_->globalBytes;

    uint32_t pad = ComputeByteAlignment(globalBytes, align);
    if (UINT32_MAX - globalBytes < pad + bytes)
        return false;

    globalBytes += pad;
    *globalDataOffset = globalBytes;
    globalBytes += bytes;

    module_->globalBytes = globalBytes;
    return true;
}

bool
ModuleGenerator::allocateGlobalVar(ValType type, bool isConst, uint32_t* index)
{
    MOZ_ASSERT(!startedFuncDefs());
    unsigned width = 0;
    switch (type) {
      case ValType::I32:
      case ValType::F32:
        width = 4;
        break;
      case ValType::I64:
      case ValType::F64:
        width = 8;
        break;
      case ValType::I32x4:
      case ValType::F32x4:
      case ValType::B32x4:
        width = 16;
        break;
      case ValType::Limit:
        MOZ_CRASH("Limit");
        break;
    }

    uint32_t offset;
    if (!allocateGlobalBytes(width, width, &offset))
        return false;

    *index = shared_->globals.length();
    return shared_->globals.append(AsmJSGlobalVariable(ToExprType(type), offset, isConst));
}

void
ModuleGenerator::initHeapUsage(HeapUsage heapUsage)
{
    MOZ_ASSERT(module_->heapUsage == HeapUsage::None);
    module_->heapUsage = heapUsage;
}

bool
ModuleGenerator::usesHeap() const
{
    return UsesHeap(module_->heapUsage);
}

void
ModuleGenerator::initSig(uint32_t sigIndex, Sig&& sig)
{
    MOZ_ASSERT(isAsmJS());
    MOZ_ASSERT(sigIndex == numSigs_);
    numSigs_++;

    MOZ_ASSERT(shared_->sigs[sigIndex] == Sig());
    shared_->sigs[sigIndex] = Move(sig);
}

const DeclaredSig&
ModuleGenerator::sig(uint32_t index) const
{
    MOZ_ASSERT(index < numSigs_);
    return shared_->sigs[index];
}

bool
ModuleGenerator::initFuncSig(uint32_t funcIndex, uint32_t sigIndex)
{
    MOZ_ASSERT(isAsmJS());
    MOZ_ASSERT(funcIndex == module_->numFuncs);
    MOZ_ASSERT(!shared_->funcSigs[funcIndex]);

    module_->numFuncs++;
    shared_->funcSigs[funcIndex] = &shared_->sigs[sigIndex];
    return true;
}

void
ModuleGenerator::bumpMinHeapLength(uint32_t newMinHeapLength)
{
    MOZ_ASSERT(isAsmJS());
    MOZ_ASSERT(newMinHeapLength >= shared_->minHeapLength);

    shared_->minHeapLength = newMinHeapLength;
}

const DeclaredSig&
ModuleGenerator::funcSig(uint32_t funcIndex) const
{
    MOZ_ASSERT(shared_->funcSigs[funcIndex]);
    return *shared_->funcSigs[funcIndex];
}

bool
ModuleGenerator::initImport(uint32_t importIndex, uint32_t sigIndex)
{
    MOZ_ASSERT(isAsmJS());

    uint32_t globalDataOffset;
    if (!allocateGlobalBytes(Module::SizeOfImportExit, sizeof(void*), &globalDataOffset))
        return false;

    MOZ_ASSERT(importIndex == module_->imports.length());
    if (!addImport(sig(sigIndex), globalDataOffset))
        return false;

    ImportModuleGeneratorData& import = shared_->imports[importIndex];
    MOZ_ASSERT(!import.sig);
    import.sig = &shared_->sigs[sigIndex];
    import.globalDataOffset = globalDataOffset;
    return true;
}

uint32_t
ModuleGenerator::numImports() const
{
    return module_->imports.length();
}

const ImportModuleGeneratorData&
ModuleGenerator::import(uint32_t index) const
{
    MOZ_ASSERT(shared_->imports[index].sig);
    return shared_->imports[index];
}

bool
ModuleGenerator::declareExport(UniqueChars fieldName, uint32_t funcIndex, uint32_t* exportIndex)
{
    if (!exportMap_->fieldNames.append(Move(fieldName)))
        return false;

    FuncIndexMap::AddPtr p = funcIndexToExport_.lookupForAdd(funcIndex);
    if (p) {
        if (exportIndex)
            *exportIndex = p->value();
        return exportMap_->fieldsToExports.append(p->value());
    }

    uint32_t newExportIndex = module_->exports.length();
    MOZ_ASSERT(newExportIndex < MaxExports);

    if (exportIndex)
        *exportIndex = newExportIndex;

    Sig copy;
    if (!copy.clone(funcSig(funcIndex)))
        return false;

    return module_->exports.append(Move(copy)) &&
           funcIndexToExport_.add(p, funcIndex, newExportIndex) &&
           exportMap_->fieldsToExports.append(newExportIndex) &&
           exportMap_->exportFuncIndices.append(funcIndex);
}

uint32_t
ModuleGenerator::numExports() const
{
    return module_->exports.length();
}

bool
ModuleGenerator::addMemoryExport(UniqueChars fieldName)
{
    return exportMap_->fieldNames.append(Move(fieldName)) &&
           exportMap_->fieldsToExports.append(MemoryExport);
}

bool
ModuleGenerator::startFuncDefs()
{
    MOZ_ASSERT(!startedFuncDefs());
    threadView_ = MakeUnique<ModuleGeneratorThreadView>(*shared_);
    if (!threadView_)
        return false;

    uint32_t numTasks;
    if (ParallelCompilationEnabled(cx_) &&
        HelperThreadState().wasmCompilationInProgress.compareExchange(false, true))
    {
#ifdef DEBUG
        {
            AutoLockHelperThreadState lock;
            MOZ_ASSERT(!HelperThreadState().wasmFailed());
            MOZ_ASSERT(HelperThreadState().wasmWorklist().empty());
            MOZ_ASSERT(HelperThreadState().wasmFinishedList().empty());
        }
#endif

        parallel_ = true;
        numTasks = HelperThreadState().maxWasmCompilationThreads();
    } else {
        numTasks = 1;
    }

    if (!tasks_.initCapacity(numTasks))
        return false;
    JSRuntime* rt = cx_->compartment()->runtimeFromAnyThread();
    for (size_t i = 0; i < numTasks; i++)
        tasks_.infallibleEmplaceBack(rt, *threadView_, COMPILATION_LIFO_DEFAULT_CHUNK_SIZE);

    if (!freeTasks_.reserve(numTasks))
        return false;
    for (size_t i = 0; i < numTasks; i++)
        freeTasks_.infallibleAppend(&tasks_[i]);

    MOZ_ASSERT(startedFuncDefs());
    return true;
}

bool
ModuleGenerator::startFuncDef(uint32_t lineOrBytecode, FunctionGenerator* fg)
{
    MOZ_ASSERT(startedFuncDefs());
    MOZ_ASSERT(!activeFunc_);
    MOZ_ASSERT(!finishedFuncs_);

    if (freeTasks_.empty() && !finishOutstandingTask())
        return false;

    IonCompileTask* task = freeTasks_.popCopy();

    task->reset(&fg->bytes_);
    fg->bytes_.clear();
    fg->lineOrBytecode_ = lineOrBytecode;
    fg->m_ = this;
    fg->task_ = task;
    activeFunc_ = fg;
    return true;
}

bool
ModuleGenerator::finishFuncDef(uint32_t funcIndex, unsigned generateTime, FunctionGenerator* fg)
{
    MOZ_ASSERT(activeFunc_ == fg);

    auto func = js::MakeUnique<FuncBytes>(Move(fg->bytes_),
                                          funcIndex,
                                          funcSig(funcIndex),
                                          fg->lineOrBytecode_,
                                          Move(fg->callSiteLineNums_),
                                          generateTime);
    if (!func)
        return false;

    fg->task_->init(Move(func));

    if (parallel_) {
        if (!StartOffThreadWasmCompile(cx_, fg->task_))
            return false;
        outstanding_++;
    } else {
        if (!IonCompileFunction(fg->task_))
            return false;
        if (!finishTask(fg->task_))
            return false;
    }

    fg->m_ = nullptr;
    fg->task_ = nullptr;
    activeFunc_ = nullptr;
    return true;
}

bool
ModuleGenerator::finishFuncDefs()
{
    MOZ_ASSERT(startedFuncDefs());
    MOZ_ASSERT(!activeFunc_);
    MOZ_ASSERT(!finishedFuncs_);

    while (outstanding_ > 0) {
        if (!finishOutstandingTask())
            return false;
    }

    for (uint32_t funcIndex = 0; funcIndex < funcIndexToCodeRange_.length(); funcIndex++)
        MOZ_ASSERT(funcIsDefined(funcIndex));

    module_->functionBytes = masm_.size();
    finishedFuncs_ = true;
    return true;
}

bool
ModuleGenerator::initSigTableLength(uint32_t sigIndex, uint32_t numElems)
{
    MOZ_ASSERT(isAsmJS());
    MOZ_ASSERT(numElems != 0);
    MOZ_ASSERT(numElems <= MaxTableElems);

    uint32_t globalDataOffset;
    if (!allocateGlobalBytes(numElems * sizeof(void*), sizeof(void*), &globalDataOffset))
        return false;

    TableModuleGeneratorData& table = shared_->sigToTable[sigIndex];
    MOZ_ASSERT(table.numElems == 0);
    table.numElems = numElems;
    table.globalDataOffset = globalDataOffset;
    return true;
}

void
ModuleGenerator::initSigTableElems(uint32_t sigIndex, Uint32Vector&& elemFuncIndices)
{
    MOZ_ASSERT(isAsmJS());
    MOZ_ASSERT(!elemFuncIndices.empty());

    TableModuleGeneratorData& table = shared_->sigToTable[sigIndex];
    MOZ_ASSERT(table.numElems == elemFuncIndices.length());

    MOZ_ASSERT(table.elemFuncIndices.empty());
    table.elemFuncIndices = Move(elemFuncIndices);
}

bool
ModuleGenerator::finish(CacheableCharsVector&& prettyFuncNames,
                        UniqueModuleData* module,
                        UniqueStaticLinkData* linkData,
                        UniqueExportMap* exportMap,
                        SlowFunctionVector* slowFuncs)
{
    MOZ_ASSERT(!activeFunc_);
    MOZ_ASSERT(finishedFuncs_);

    UniqueStaticLinkData link = MakeUnique<StaticLinkData>();
    if (!link)
        return false;

    if (!finishCodegen(link.get()))
        return false;

    module_->prettyFuncNames = Move(prettyFuncNames);

    // Start global data on a new page so JIT code may be given independent
    // protection flags. Note assumption that global data starts right after
    // code below.
    module_->codeBytes = AlignBytes(masm_.bytesNeeded(), gc::SystemPageSize());

    // Inflate the global bytes up to page size so that the total bytes are a
    // page size (as required by the allocator functions).
    module_->globalBytes = AlignBytes(module_->globalBytes, gc::SystemPageSize());

    // Allocate the code (guarded by a UniquePtr until it is given to the Module).
    module_->code = AllocateCode(cx_, module_->totalBytes());
    if (!module_->code)
        return false;

    // Delay flushing until Module::dynamicallyLink. The flush-inhibited range
    // is set by executableCopy.
    AutoFlushICache afc("ModuleGenerator::finish", /* inhibit = */ true);
    masm_.executableCopy(module_->code.get());

    // c.f. JitCode::copyFrom
    MOZ_ASSERT(masm_.jumpRelocationTableBytes() == 0);
    MOZ_ASSERT(masm_.dataRelocationTableBytes() == 0);
    MOZ_ASSERT(masm_.preBarrierTableBytes() == 0);
    MOZ_ASSERT(!masm_.hasSelfReference());

    // Convert the CallSiteAndTargetVector (needed during generation) to a
    // CallSiteVector (what is stored in the Module).
    if (!module_->callSites.appendAll(masm_.callSites()))
        return false;

    // The MacroAssembler has accumulated all the heap accesses during codegen.
    module_->heapAccesses = masm_.extractHeapAccesses();

    if (!finishStaticLinkData(module_->code.get(), module_->codeBytes, link.get()))
        return false;

    *module = Move(module_);
    *linkData = Move(link);
    *exportMap = Move(exportMap_);
    *slowFuncs = Move(slowFuncs_);
    return true;
}

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

#ifndef wasm_module_h
#define wasm_module_h

#include "mozilla/LinkedList.h"

#include "asmjs/WasmTypes.h"
#include "gc/Barrier.h"
#include "vm/MallocProvider.h"
#include "vm/NativeObject.h"

namespace js {

class AsmJSModule;
class WasmActivation;
class WasmModuleObject;
namespace jit { struct BaselineScript; }

namespace wasm {

// A wasm Module and everything it contains must support serialization,
// deserialization and cloning. Some data can be simply copied as raw bytes and,
// as a convention, is stored in an inline CacheablePod struct. Everything else
// should implement the below methods which are called recusively by the
// containing Module. See comments for these methods in wasm::Module.

#define WASM_DECLARE_SERIALIZABLE(Type)                                         \
    size_t serializedSize() const;                                              \
    uint8_t* serialize(uint8_t* cursor) const;                                  \
    const uint8_t* deserialize(ExclusiveContext* cx, const uint8_t* cursor);    \
    bool clone(JSContext* cx, Type* out) const;                                 \
    size_t sizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf) const;

// The StaticLinkData contains all the metadata necessary to perform
// Module::staticallyLink but is not necessary afterwards.

struct StaticLinkData
{
    struct InternalLink {
        enum Kind {
            RawPointer,
            CodeLabel,
            InstructionImmediate
        };
        uint32_t patchAtOffset;
        uint32_t targetOffset;

        InternalLink() = default;
        explicit InternalLink(Kind kind);
        bool isRawPointerPatch();
    };
    typedef Vector<InternalLink, 0, SystemAllocPolicy> InternalLinkVector;

    struct SymbolicLinkArray : EnumeratedArray<SymbolicAddress, SymbolicAddress::Limit, Uint32Vector> {
        WASM_DECLARE_SERIALIZABLE(SymbolicLinkArray)
    };

    struct FuncPtrTable {
        uint32_t globalDataOffset;
        Uint32Vector elemOffsets;
        FuncPtrTable(uint32_t globalDataOffset, Uint32Vector&& elemOffsets)
          : globalDataOffset(globalDataOffset), elemOffsets(Move(elemOffsets))
        {}
        FuncPtrTable(FuncPtrTable&& rhs)
          : globalDataOffset(rhs.globalDataOffset), elemOffsets(Move(rhs.elemOffsets))
        {}
        FuncPtrTable() = default;
        WASM_DECLARE_SERIALIZABLE(FuncPtrTable)
    };
    typedef Vector<FuncPtrTable, 0, SystemAllocPolicy> FuncPtrTableVector;

    struct CacheablePod {
        uint32_t        interruptOffset;
        uint32_t        outOfBoundsOffset;
    } pod;
    InternalLinkVector  internalLinks;
    SymbolicLinkArray   symbolicLinks;
    FuncPtrTableVector  funcPtrTables;

    WASM_DECLARE_SERIALIZABLE(StaticLinkData)
};

typedef UniquePtr<StaticLinkData> UniqueStaticLinkData;

// An Export represents a single function inside a wasm Module that has been
// exported one or more times.

class Export
{
    Sig sig_;
    struct CacheablePod {
        uint32_t stubOffset_;
    } pod;

  public:
    Export() = default;
    explicit Export(Sig&& sig)
      : sig_(Move(sig))
    {
        pod.stubOffset_ = UINT32_MAX;
    }
    Export(Export&& rhs)
      : sig_(Move(rhs.sig_)),
        pod(rhs.pod)
    {}

    void initStubOffset(uint32_t stubOffset) {
        MOZ_ASSERT(pod.stubOffset_ == UINT32_MAX);
        pod.stubOffset_ = stubOffset;
    }

    uint32_t stubOffset() const {
        return pod.stubOffset_;
    }
    const Sig& sig() const {
        return sig_;
    }

    WASM_DECLARE_SERIALIZABLE(Export)
};

typedef Vector<Export, 0, SystemAllocPolicy> ExportVector;

// An Import describes a wasm module import. Currently, only functions can be
// imported in wasm. A function import includes the signature used within the
// module to call it.

class Import
{
    Sig sig_;
    struct CacheablePod {
        uint32_t exitGlobalDataOffset_;
        uint32_t interpExitCodeOffset_;
        uint32_t jitExitCodeOffset_;
    } pod;

  public:
    Import() {}
    Import(Import&& rhs) : sig_(Move(rhs.sig_)), pod(rhs.pod) {}
    Import(Sig&& sig, uint32_t exitGlobalDataOffset)
      : sig_(Move(sig))
    {
        pod.exitGlobalDataOffset_ = exitGlobalDataOffset;
        pod.interpExitCodeOffset_ = 0;
        pod.jitExitCodeOffset_ = 0;
    }

    void initInterpExitOffset(uint32_t off) {
        MOZ_ASSERT(!pod.interpExitCodeOffset_);
        pod.interpExitCodeOffset_ = off;
    }
    void initJitExitOffset(uint32_t off) {
        MOZ_ASSERT(!pod.jitExitCodeOffset_);
        pod.jitExitCodeOffset_ = off;
    }

    const Sig& sig() const {
        return sig_;
    }
    uint32_t exitGlobalDataOffset() const {
        return pod.exitGlobalDataOffset_;
    }
    uint32_t interpExitCodeOffset() const {
        return pod.interpExitCodeOffset_;
    }
    uint32_t jitExitCodeOffset() const {
        return pod.jitExitCodeOffset_;
    }

    WASM_DECLARE_SERIALIZABLE(Import)
};

typedef Vector<Import, 0, SystemAllocPolicy> ImportVector;

// A CodeRange describes a single contiguous range of code within a wasm
// module's code segment. A CodeRange describes what the code does and, for
// function bodies, the name and source coordinates of the function.

class CodeRange
{
    // All fields are treated as cacheable POD:
    uint32_t funcIndex_;
    uint32_t funcLineOrBytecode_;
    uint32_t begin_;
    uint32_t profilingReturn_;
    uint32_t end_;
    union {
        struct {
            uint8_t kind_;
            uint8_t beginToEntry_;
            uint8_t profilingJumpToProfilingReturn_;
            uint8_t profilingEpilogueToProfilingReturn_;
        } func;
        uint8_t kind_;
    } u;

    void assertValid();

  public:
    enum Kind { Function, Entry, ImportJitExit, ImportInterpExit, ErrorExit, Inline, CallThunk };

    CodeRange() = default;
    CodeRange(Kind kind, Offsets offsets);
    CodeRange(Kind kind, ProfilingOffsets offsets);
    CodeRange(uint32_t funcIndex, uint32_t lineOrBytecode, FuncOffsets offsets);

    // All CodeRanges have a begin and end.

    uint32_t begin() const {
        return begin_;
    }
    uint32_t end() const {
        return end_;
    }

    // Other fields are only available for certain CodeRange::Kinds.

    Kind kind() const { return Kind(u.kind_); }

    // Every CodeRange except entry and inline stubs has a profiling return
    // which is used for asynchronous profiling to determine the frame pointer.

    uint32_t profilingReturn() const {
        MOZ_ASSERT(isFunction() || isImportExit());
        return profilingReturn_;
    }

    // Functions have offsets which allow patching to selectively execute
    // profiling prologues/epilogues.

    bool isFunction() const {
        return kind() == Function;
    }
    bool isImportExit() const {
        return kind() == ImportJitExit || kind() == ImportInterpExit;
    }
    bool isErrorExit() const {
        return kind() == ErrorExit;
    }
    uint32_t funcProfilingEntry() const {
        MOZ_ASSERT(isFunction());
        return begin();
    }
    uint32_t funcNonProfilingEntry() const {
        MOZ_ASSERT(isFunction());
        return begin_ + u.func.beginToEntry_;
    }
    uint32_t functionProfilingJump() const {
        MOZ_ASSERT(isFunction());
        return profilingReturn_ - u.func.profilingJumpToProfilingReturn_;
    }
    uint32_t funcProfilingEpilogue() const {
        MOZ_ASSERT(isFunction());
        return profilingReturn_ - u.func.profilingEpilogueToProfilingReturn_;
    }
    uint32_t funcIndex() const {
        MOZ_ASSERT(isFunction());
        return funcIndex_;
    }
    uint32_t funcLineOrBytecode() const {
        MOZ_ASSERT(isFunction());
        return funcLineOrBytecode_;
    }

    // A sorted array of CodeRanges can be looked up via BinarySearch and PC.

    struct PC {
        size_t offset;
        explicit PC(size_t offset) : offset(offset) {}
        bool operator==(const CodeRange& rhs) const {
            return offset >= rhs.begin() && offset < rhs.end();
        }
        bool operator<(const CodeRange& rhs) const {
            return offset < rhs.begin();
        }
    };
};

typedef Vector<CodeRange, 0, SystemAllocPolicy> CodeRangeVector;

// A CallThunk describes the offset and target of thunks so that they may be
// patched at runtime when profiling is toggled. Thunks are emitted to connect
// callsites that are too far away from callees to fit in a single call
// instruction's relative offset.

struct CallThunk
{
    uint32_t offset;
    union {
        uint32_t funcIndex;
        uint32_t codeRangeIndex;
    } u;

    CallThunk(uint32_t offset, uint32_t funcIndex) : offset(offset) { u.funcIndex = funcIndex; }
    CallThunk() = default;
};

typedef Vector<CallThunk, 0, SystemAllocPolicy> CallThunkVector;

// CacheableChars is used to cacheably store UniqueChars.

struct CacheableChars : UniqueChars
{
    CacheableChars() = default;
    explicit CacheableChars(char* ptr) : UniqueChars(ptr) {}
    MOZ_IMPLICIT CacheableChars(UniqueChars&& rhs) : UniqueChars(Move(rhs)) {}
    CacheableChars(CacheableChars&& rhs) : UniqueChars(Move(rhs)) {}
    void operator=(CacheableChars&& rhs) { UniqueChars::operator=(Move(rhs)); }
    WASM_DECLARE_SERIALIZABLE(CacheableChars)
};

typedef Vector<CacheableChars, 0, SystemAllocPolicy> CacheableCharsVector;

// The ExportMap describes how Exports are mapped to the fields of the export
// object. This allows a single Export to be used in multiple fields.
// The 'fieldNames' vector provides the list of names of the module's exports.
// For each field in fieldNames, 'fieldsToExports' provides either:
//  - the sentinel value MemoryExport indicating an export of linear memory; or
//  - the index of an export (both into the module's ExportVector and the
//    ExportMap's exportFuncIndices vector).
// Lastly, the 'exportFuncIndices' vector provides, for each exported function,
// the internal index of the function.

static const uint32_t MemoryExport = UINT32_MAX;

struct ExportMap
{
    CacheableCharsVector fieldNames;
    Uint32Vector fieldsToExports;
    Uint32Vector exportFuncIndices;

    WASM_DECLARE_SERIALIZABLE(ExportMap)
};

typedef UniquePtr<ExportMap> UniqueExportMap;

// A UniqueCodePtr owns allocated executable code. Code passed to the Module
// constructor must be allocated via AllocateCode.

class CodeDeleter
{
    uint32_t bytes_;
  public:
    CodeDeleter() : bytes_(0) {}
    explicit CodeDeleter(uint32_t bytes) : bytes_(bytes) {}
    void operator()(uint8_t* p);
};
typedef UniquePtr<uint8_t, CodeDeleter> UniqueCodePtr;

UniqueCodePtr
AllocateCode(ExclusiveContext* cx, size_t bytes);

// A wasm module can either use no heap, a unshared heap (ArrayBuffer) or shared
// heap (SharedArrayBuffer).

enum class HeapUsage
{
    None = false,
    Unshared = 1,
    Shared = 2
};

static inline bool
UsesHeap(HeapUsage heapUsage)
{
    return bool(heapUsage);
}

// ModuleCacheablePod holds the trivially-memcpy()able serializable portion of
// ModuleData.

struct ModuleCacheablePod
{
    uint32_t              functionBytes;
    uint32_t              codeBytes;
    uint32_t              globalBytes;
    uint32_t              numFuncs;
    ModuleKind            kind;
    HeapUsage             heapUsage;
    CompileArgs           compileArgs;

    uint32_t totalBytes() const { return codeBytes + globalBytes; }
};

// ModuleData holds the guts of a Module. ModuleData is mutably built up by
// ModuleGenerator and then handed over to the Module constructor in finish(),
// where it is stored immutably.

struct ModuleData : ModuleCacheablePod
{
    ModuleData() : loadedFromCache(false) { mozilla::PodZero(&pod()); }
    ModuleCacheablePod& pod() { return *this; }
    const ModuleCacheablePod& pod() const { return *this; }

    UniqueCodePtr         code;
    ImportVector          imports;
    ExportVector          exports;
    HeapAccessVector      heapAccesses;
    CodeRangeVector       codeRanges;
    CallSiteVector        callSites;
    CallThunkVector       callThunks;
    CacheableCharsVector  prettyFuncNames;
    CacheableChars        filename;
    bool                  loadedFromCache;

    WASM_DECLARE_SERIALIZABLE(ModuleData);
};

typedef UniquePtr<ModuleData> UniqueModuleData;

// Module represents a compiled WebAssembly module which lives until the last
// reference to any exported functions is dropped. Modules must be wrapped by a
// rooted JSObject immediately after creation so that Module::trace() is called
// during GC. Modules are created after compilation completes and start in a
// a fully unlinked state. After creation, a module must be first statically
// linked and then dynamically linked:
//
//  - Static linking patches code or global data that relies on absolute
//    addresses. Static linking should happen after a module is serialized into
//    a cache file so that the cached code is stored unlinked and ready to be
//    statically linked after deserialization.
//
//  - Dynamic linking patches code or global data that relies on the address of
//    the heap and imports of a module. A module may only be dynamically linked
//    once. However, a dynamically-linked module may be cloned so that the clone
//    can be independently dynamically linked.
//
// Once fully dynamically linked, a Module can have its exports invoked via
// callExport().

class Module : public mozilla::LinkedListElement<Module>
{
    typedef UniquePtr<const ModuleData> UniqueConstModuleData;
    struct ImportExit {
        void* code;
        jit::BaselineScript* baselineScript;
        HeapPtrFunction fun;
        static_assert(sizeof(HeapPtrFunction) == sizeof(void*), "for JIT access");
    };
    struct EntryArg {
        uint64_t lo;
        uint64_t hi;
    };
    typedef int32_t (*EntryFuncPtr)(EntryArg* args, uint8_t* global);
    struct FuncPtrTable {
        uint32_t globalDataOffset;
        uint32_t numElems;
        explicit FuncPtrTable(const StaticLinkData::FuncPtrTable& table)
          : globalDataOffset(table.globalDataOffset),
            numElems(table.elemOffsets.length())
        {}
    };
    typedef Vector<FuncPtrTable, 0, SystemAllocPolicy> FuncPtrTableVector;
    typedef Vector<CacheableChars, 0, SystemAllocPolicy> FuncLabelVector;
    typedef RelocatablePtrArrayBufferObjectMaybeShared BufferPtr;
    typedef HeapPtr<WasmModuleObject*> ModuleObjectPtr;

    // Initialized when constructed:
    const UniqueConstModuleData  module_;

    // Initialized during staticallyLink:
    bool                         staticallyLinked_;
    uint8_t*                     interrupt_;
    uint8_t*                     outOfBounds_;
    FuncPtrTableVector           funcPtrTables_;

    // Initialized during dynamicallyLink:
    bool                         dynamicallyLinked_;
    BufferPtr                    heap_;

    // Mutated after dynamicallyLink:
    bool                         profilingEnabled_;
    FuncLabelVector              funcLabels_;

    // Back pointer to the JS object.
    ModuleObjectPtr              ownerObject_;

    // Possibly stored copy of the bytes from which this module was compiled.
    Bytes                        source_;

    uint8_t* rawHeapPtr() const;
    uint8_t*& rawHeapPtr();
    WasmActivation*& activation();
    void specializeToHeap(ArrayBufferObjectMaybeShared* heap);
    void despecializeFromHeap(ArrayBufferObjectMaybeShared* heap);
    bool sendCodeRangesToProfiler(JSContext* cx);
    MOZ_MUST_USE bool setProfilingEnabled(JSContext* cx, bool enabled);
    ImportExit& importToExit(const Import& import);

    friend class js::WasmActivation;

  protected:
    const ModuleData& base() const { return *module_; }
    bool clone(JSContext* cx, const StaticLinkData& link, Module* clone) const;

  public:
    static const unsigned SizeOfImportExit = sizeof(ImportExit);
    static const unsigned OffsetOfImportExitFun = offsetof(ImportExit, fun);
    static const unsigned SizeOfEntryArg = sizeof(EntryArg);

    explicit Module(UniqueModuleData module);
    virtual ~Module();
    virtual void trace(JSTracer* trc);
    virtual void readBarrier();
    virtual void addSizeOfMisc(MallocSizeOf mallocSizeOf, size_t* code, size_t* data);

    void setOwner(WasmModuleObject* owner) { MOZ_ASSERT(!ownerObject_); ownerObject_ = owner; }
    inline const HeapPtr<WasmModuleObject*>& owner() const;

    void setSource(Bytes&& source) { source_ = Move(source); }

    uint8_t* code() const { return module_->code.get(); }
    uint32_t codeBytes() const { return module_->codeBytes; }
    uint8_t* globalData() const { return code() + module_->codeBytes; }
    uint32_t globalBytes() const { return module_->globalBytes; }
    HeapUsage heapUsage() const { return module_->heapUsage; }
    bool usesHeap() const { return UsesHeap(module_->heapUsage); }
    bool hasSharedHeap() const { return module_->heapUsage == HeapUsage::Shared; }
    CompileArgs compileArgs() const { return module_->compileArgs; }
    const ImportVector& imports() const { return module_->imports; }
    const ExportVector& exports() const { return module_->exports; }
    const CodeRangeVector& codeRanges() const { return module_->codeRanges; }
    const char* filename() const { return module_->filename.get(); }
    bool loadedFromCache() const { return module_->loadedFromCache; }
    bool staticallyLinked() const { return staticallyLinked_; }
    bool dynamicallyLinked() const { return dynamicallyLinked_; }

    // Some wasm::Module's have the most-derived type AsmJSModule. The
    // AsmJSModule stores the extra metadata necessary to implement asm.js (JS)
    // semantics. The asAsmJS() member may be used as a checked downcast when
    // isAsmJS() is true.

    bool isAsmJS() const { return module_->kind == ModuleKind::AsmJS; }
    AsmJSModule& asAsmJS() { MOZ_ASSERT(isAsmJS()); return *(AsmJSModule*)this; }
    const AsmJSModule& asAsmJS() const { MOZ_ASSERT(isAsmJS()); return *(const AsmJSModule*)this; }
    virtual bool mutedErrors() const;
    virtual const char16_t* displayURL() const;
    virtual ScriptSource* maybeScriptSource() const { return nullptr; }

    // The range [0, functionBytes) is a subrange of [0, codeBytes) that
    // contains only function body code, not the stub code. This distinction is
    // used by the async interrupt handler to only interrupt when the pc is in
    // function code which, in turn, simplifies reasoning about how stubs
    // enter/exit.

    bool containsFunctionPC(void* pc) const;
    bool containsCodePC(void* pc) const;
    const CallSite* lookupCallSite(void* returnAddress) const;
    const CodeRange* lookupCodeRange(void* pc) const;
    const HeapAccess* lookupHeapAccess(void* pc) const;

    // This function transitions the module from an unlinked state to a
    // statically-linked state. The given StaticLinkData must have come from the
    // compilation of this module.

    bool staticallyLink(ExclusiveContext* cx, const StaticLinkData& link);

    // This function transitions the module from a statically-linked state to a
    // dynamically-linked state. If this module usesHeap(), a non-null heap
    // buffer must be given. The given import vector must match the module's
    // ImportVector. The function returns a new export object for this module.

    bool dynamicallyLink(JSContext* cx,
                         Handle<WasmModuleObject*> moduleObj,
                         Handle<ArrayBufferObjectMaybeShared*> heap,
                         Handle<FunctionVector> imports,
                         const ExportMap& exportMap,
                         MutableHandleObject exportObj);

    // The wasm heap, established by dynamicallyLink.

    SharedMem<uint8_t*> heap() const;
    size_t heapLength() const;

    // The exports of a wasm module are called by preparing an array of
    // arguments (coerced to the corresponding types of the Export signature)
    // and calling the export's entry trampoline.

    bool callExport(JSContext* cx, uint32_t exportIndex, CallArgs args);

    // Initially, calls to imports in wasm code call out through the generic
    // callImport method. If the imported callee gets JIT compiled and the types
    // match up, callImport will patch the code to instead call through a thunk
    // directly into the JIT code. If the JIT code is released, the Module must
    // be notified so it can go back to the generic callImport.

    bool callImport(JSContext* cx, uint32_t importIndex, unsigned argc, const uint64_t* argv,
                    MutableHandleValue rval);
    void deoptimizeImportExit(uint32_t importIndex);

    // At runtime, when $pc is in wasm function code (containsFunctionPC($pc)),
    // $pc may be moved abruptly to interrupt() or outOfBounds() by a signal
    // handler or SetContext() from another thread.

    uint8_t* interrupt() const { MOZ_ASSERT(staticallyLinked_); return interrupt_; }
    uint8_t* outOfBounds() const { MOZ_ASSERT(staticallyLinked_); return outOfBounds_; }

    // Every function has an associated display atom which is either the pretty
    // name given by the asm.js function name or wasm symbols or something
    // generated from the function index.

    const char* prettyFuncName(uint32_t funcIndex) const;
    const char* getFuncName(JSContext* cx, uint32_t funcIndex, UniqueChars* owner) const;
    JSAtom* getFuncAtom(JSContext* cx, uint32_t funcIndex) const;

    // If debuggerObservesAsmJS was true when the module was compiled, render
    // the binary to a new source string.

    JSString* createText(JSContext* cx);

    // Each Module has a profilingEnabled state which is updated to match
    // SPSProfiler::enabled() on the next Module::callExport when there are no
    // frames from the Module on the stack. The ProfilingFrameIterator only
    // shows frames for Module activations that have profilingEnabled.

    bool profilingEnabled() const { return profilingEnabled_; }
    const char* profilingLabel(uint32_t funcIndex) const;
};

typedef UniquePtr<Module> UniqueModule;

// Tests to query and access the exported JS functions produced by
// Module::createExportObject.

extern bool
IsExportedFunction(JSFunction* fun);

extern WasmModuleObject*
ExportedFunctionToModuleObject(JSFunction* fun);

extern uint32_t
ExportedFunctionToIndex(JSFunction* fun);

} // namespace wasm

// An WasmModuleObject is an internal object (i.e., not exposed directly to user
// code) which traces and owns a wasm::Module. The WasmModuleObject is
// referenced by the extended slots of exported JSFunctions and serves to keep
// the wasm::Module alive until its last GC reference is dead.

class WasmModuleObject : public NativeObject
{
    static const unsigned MODULE_SLOT = 0;
    static const ClassOps classOps_;

    bool hasModule() const;
    static void finalize(FreeOp* fop, JSObject* obj);
    static void trace(JSTracer* trc, JSObject* obj);
  public:
    static const unsigned RESERVED_SLOTS = 1;
    static WasmModuleObject* create(ExclusiveContext* cx);
    bool init(wasm::Module* module);
    wasm::Module& module() const;
    void addSizeOfMisc(mozilla::MallocSizeOf mallocSizeOf, size_t* code, size_t* data);
    static const Class class_;
};

inline const HeapPtr<WasmModuleObject*>&
wasm::Module::owner() const {
    MOZ_ASSERT(&ownerObject_->module() == this);
    return ownerObject_;
}

using WasmModuleObjectVector = GCVector<WasmModuleObject*>;

} // namespace js

#endif // wasm_module_h

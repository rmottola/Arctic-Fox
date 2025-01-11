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

#include "asmjs/WasmCode.h"
#include "js/TypeDecls.h"

namespace js {

class ArrayBufferObjectMaybeShared;
class WasmInstanceObject;

namespace wasm {

// LinkData contains all the metadata necessary to patch all the locations
// that depend on the absolute address of a CodeSegment.
//
// LinkData is built incrementing by ModuleGenerator and then stored immutably
// in Module.

struct LinkDataCacheablePod
{
    uint32_t functionCodeLength;
    uint32_t globalDataLength;
    uint32_t interruptOffset;
    uint32_t outOfBoundsOffset;
    uint32_t unalignedAccessOffset;

    LinkDataCacheablePod() { mozilla::PodZero(this); }
};

struct LinkData : LinkDataCacheablePod
{
    LinkDataCacheablePod& pod() { return *this; }
    const LinkDataCacheablePod& pod() const { return *this; }

    struct InternalLink {
        enum Kind {
            RawPointer,
            CodeLabel,
            InstructionImmediate
        };
        MOZ_INIT_OUTSIDE_CTOR uint32_t patchAtOffset;
        MOZ_INIT_OUTSIDE_CTOR uint32_t targetOffset;

        InternalLink() = default;
        explicit InternalLink(Kind kind);
        bool isRawPointerPatch();
    };
    typedef Vector<InternalLink, 0, SystemAllocPolicy> InternalLinkVector;

    struct SymbolicLinkArray : EnumeratedArray<SymbolicAddress, SymbolicAddress::Limit, Uint32Vector> {
        WASM_DECLARE_SERIALIZABLE(SymbolicLinkArray)
    };

    InternalLinkVector  internalLinks;
    SymbolicLinkArray   symbolicLinks;

    WASM_DECLARE_SERIALIZABLE(LinkData)
};

typedef UniquePtr<LinkData> UniqueLinkData;
typedef UniquePtr<const LinkData> UniqueConstLinkData;

// Import describes a single wasm import. An ImportVector describes all
// of a single module's imports.
//
// ImportVector is built incrementally by ModuleGenerator and then stored
// immutably by Module.

struct Import
{
    CacheableChars module;
    CacheableChars func;
    DefinitionKind kind;

    Import() = default;
    Import(UniqueChars&& module, UniqueChars&& func, DefinitionKind kind)
      : module(Move(module)), func(Move(func)), kind(kind)
    {}

    WASM_DECLARE_SERIALIZABLE(Import)
};

typedef Vector<Import, 0, SystemAllocPolicy> ImportVector;

// ExportMap describes all of a single module's exports. The ExportMap describes
// how the Exports (stored in Metadata) are mapped to the fields of the export
// object produced by instantiation. The 'fieldNames' vector provides the list
// of names of the module's exports. For each field name, 'fieldsToExports'
// provides either:
//  - the sentinel value MemoryExport indicating an export of linear memory; or
//  - the index of an export into the ExportVector in Metadata
//
// ExportMap is built incrementally by ModuleGenerator and then stored immutably
// by Module.

static const uint32_t MemoryExport = UINT32_MAX;

struct ExportMap
{
    CacheableCharsVector fieldNames;
    Uint32Vector fieldsToExports;

    WASM_DECLARE_SERIALIZABLE(ExportMap)
};

// DataSegment describes the offset of a data segment in the bytecode that is
// to be copied at a given offset into linear memory upon instantiation.

struct DataSegment
{
    uint32_t memoryOffset;
    uint32_t bytecodeOffset;
    uint32_t length;
};

typedef Vector<DataSegment, 0, SystemAllocPolicy> DataSegmentVector;

// ElemSegment represents an element segment in the module where each element's
// function index has been translated to its offset in the code section.

struct ElemSegment
{
    uint32_t globalDataOffset;
    Uint32Vector elems;

    ElemSegment() = default;
    ElemSegment(uint32_t globalDataOffset, Uint32Vector&& elems)
      : globalDataOffset(globalDataOffset), elems(Move(elems))
    {}

    WASM_DECLARE_SERIALIZABLE(ElemSegment)
};

typedef Vector<ElemSegment, 0, SystemAllocPolicy> ElemSegmentVector;

// Module represents a compiled wasm module and primarily provides two
// operations: instantiation and serialization. A Module can be instantiated any
// number of times to produce new Instance objects. A Module can be serialized
// any number of times such that the serialized bytes can be deserialized later
// to produce a new, equivalent Module.
//
// Since fully linked-and-instantiated code (represented by CodeSegment) cannot
// be shared between instances, Module stores an unlinked, uninstantiated copy
// of the code (represented by the Bytes) and creates a new CodeSegment each
// time it is instantiated. In the future, Module will store a shareable,
// immutable CodeSegment that can be shared by all its instances.

class Module
{
    const Bytes             code_;
    const LinkData          linkData_;
    const ImportVector      imports_;
    const ExportMap         exportMap_;
    const DataSegmentVector dataSegments_;
    const ElemSegmentVector elemSegments_;
    const SharedMetadata    metadata_;
    const SharedBytes       bytecode_;

    bool instantiateMemory(JSContext* cx, MutableHandleWasmMemoryObject memory) const;
    bool instantiateTable(JSContext* cx, const CodeSegment& cs) const;

  public:
    Module(Bytes&& code,
           LinkData&& linkData,
           ImportVector&& imports,
           ExportMap&& exportMap,
           DataSegmentVector&& dataSegments,
           ElemSegmentVector&& elemSegments,
           const Metadata& metadata,
           const ShareableBytes& bytecode)
      : code_(Move(code)),
        linkData_(Move(linkData)),
        imports_(Move(imports)),
        exportMap_(Move(exportMap)),
        dataSegments_(Move(dataSegments)),
        elemSegments_(Move(elemSegments)),
        metadata_(&metadata),
        bytecode_(&bytecode)
    {}

    const Metadata& metadata() const { return *metadata_; }
    const ImportVector& imports() const { return imports_; }

    // Instantiate this module with the given imports:

    bool instantiate(JSContext* cx,
                     Handle<FunctionVector> funcImports,
                     HandleWasmMemoryObject memoryImport,
                     HandleWasmInstanceObject instanceObj) const;

    // Structured clone support:

    size_t serializedSize() const;
    uint8_t* serialize(uint8_t* cursor) const;
    static const uint8_t* deserialize(const uint8_t* cursor, UniquePtr<Module>* module,
                                      Metadata* maybeMetadata = nullptr);

    // about:memory reporting:

    void addSizeOfMisc(MallocSizeOf mallocSizeOf,
                       Metadata::SeenSet* seenMetadata,
                       ShareableBytes::SeenSet* seenBytes,
                       size_t* code, size_t* data) const;
};

typedef UniquePtr<Module> UniqueModule;

// These accessors are used to implemented the special asm.js semantics of
// exported wasm functions:

extern bool
IsExportedFunction(JSFunction* fun);

extern Instance&
ExportedFunctionToInstance(JSFunction* fun);

extern uint32_t
ExportedFunctionToExportIndex(JSFunction* fun);

} // namespace wasm
} // namespace js

#endif // wasm_module_h

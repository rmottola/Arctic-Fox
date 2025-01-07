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

#include "asmjs/WasmModule.h"

#include "mozilla/Atomics.h"

#include "asmjs/WasmInstance.h"
#include "asmjs/WasmJS.h"
#include "asmjs/WasmSerialize.h"

#include "vm/ArrayBufferObject-inl.h"

using namespace js;
using namespace js::wasm;

#if defined(JS_CODEGEN_MIPS32) || defined(JS_CODEGEN_MIPS64)
// On MIPS, CodeLabels are instruction immediates so InternalLinks only
// patch instruction immediates.
LinkData::InternalLink::InternalLink(Kind kind)
{
    MOZ_ASSERT(kind == CodeLabel || kind == InstructionImmediate);
}

bool
LinkData::InternalLink::isRawPointerPatch()
{
    return false;
}
#else
// On the rest, CodeLabels are raw pointers so InternalLinks only patch
// raw pointers.
LinkData::InternalLink::InternalLink(Kind kind)
{
    MOZ_ASSERT(kind == CodeLabel || kind == RawPointer);
}

bool
LinkData::InternalLink::isRawPointerPatch()
{
    return true;
}
#endif

size_t
LinkData::SymbolicLinkArray::serializedSize() const
{
    size_t size = 0;
    for (const Uint32Vector& offsets : *this)
        size += SerializedPodVectorSize(offsets);
    return size;
}

uint8_t*
LinkData::SymbolicLinkArray::serialize(uint8_t* cursor) const
{
    for (const Uint32Vector& offsets : *this)
        cursor = SerializePodVector(cursor, offsets);
    return cursor;
}

const uint8_t*
LinkData::SymbolicLinkArray::deserialize(ExclusiveContext* cx, const uint8_t* cursor)
{
    for (Uint32Vector& offsets : *this) {
        cursor = DeserializePodVector(cx, cursor, &offsets);
        if (!cursor)
            return nullptr;
    }
    return cursor;
}

size_t
LinkData::SymbolicLinkArray::sizeOfExcludingThis(MallocSizeOf mallocSizeOf) const
{
    size_t size = 0;
    for (const Uint32Vector& offsets : *this)
        size += offsets.sizeOfExcludingThis(mallocSizeOf);
    return size;
}

size_t
LinkData::FuncTable::serializedSize() const
{
    return sizeof(globalDataOffset) +
           SerializedPodVectorSize(elemOffsets);
}

uint8_t*
LinkData::FuncTable::serialize(uint8_t* cursor) const
{
    cursor = WriteBytes(cursor, &globalDataOffset, sizeof(globalDataOffset));
    cursor = SerializePodVector(cursor, elemOffsets);
    return cursor;
}

const uint8_t*
LinkData::FuncTable::deserialize(ExclusiveContext* cx, const uint8_t* cursor)
{
    (cursor = ReadBytes(cursor, &globalDataOffset, sizeof(globalDataOffset))) &&
    (cursor = DeserializePodVector(cx, cursor, &elemOffsets));
    return cursor;
}

size_t
LinkData::FuncTable::sizeOfExcludingThis(MallocSizeOf mallocSizeOf) const
{
    return elemOffsets.sizeOfExcludingThis(mallocSizeOf);
}

size_t
LinkData::serializedSize() const
{
    return sizeof(pod()) +
           SerializedPodVectorSize(internalLinks) +
           symbolicLinks.serializedSize() +
           SerializedVectorSize(funcTables);
}

uint8_t*
LinkData::serialize(uint8_t* cursor) const
{
    cursor = WriteBytes(cursor, &pod(), sizeof(pod()));
    cursor = SerializePodVector(cursor, internalLinks);
    cursor = symbolicLinks.serialize(cursor);
    cursor = SerializeVector(cursor, funcTables);
    return cursor;
}

const uint8_t*
LinkData::deserialize(ExclusiveContext* cx, const uint8_t* cursor)
{
    (cursor = ReadBytes(cursor, &pod(), sizeof(pod()))) &&
    (cursor = DeserializePodVector(cx, cursor, &internalLinks)) &&
    (cursor = symbolicLinks.deserialize(cx, cursor)) &&
    (cursor = DeserializeVector(cx, cursor, &funcTables));
    return cursor;
}

size_t
LinkData::sizeOfExcludingThis(MallocSizeOf mallocSizeOf) const
{
    return internalLinks.sizeOfExcludingThis(mallocSizeOf) +
           symbolicLinks.sizeOfExcludingThis(mallocSizeOf) +
           SizeOfVectorExcludingThis(funcTables, mallocSizeOf);
}

size_t
ImportName::serializedSize() const
{
    return module.serializedSize() +
           func.serializedSize();
}

uint8_t*
ImportName::serialize(uint8_t* cursor) const
{
    cursor = module.serialize(cursor);
    cursor = func.serialize(cursor);
    return cursor;
}

const uint8_t*
ImportName::deserialize(ExclusiveContext* cx, const uint8_t* cursor)
{
    (cursor = module.deserialize(cx, cursor)) &&
    (cursor = func.deserialize(cx, cursor));
    return cursor;
}

size_t
ImportName::sizeOfExcludingThis(MallocSizeOf mallocSizeOf) const
{
    return module.sizeOfExcludingThis(mallocSizeOf) +
           func.sizeOfExcludingThis(mallocSizeOf);
}

size_t
ExportMap::serializedSize() const
{
    return SerializedVectorSize(fieldNames) +
           SerializedPodVectorSize(fieldsToExports);
}

uint8_t*
ExportMap::serialize(uint8_t* cursor) const
{
    cursor = SerializeVector(cursor, fieldNames);
    cursor = SerializePodVector(cursor, fieldsToExports);
    return cursor;
}

const uint8_t*
ExportMap::deserialize(ExclusiveContext* cx, const uint8_t* cursor)
{
    (cursor = DeserializeVector(cx, cursor, &fieldNames)) &&
    (cursor = DeserializePodVector(cx, cursor, &fieldsToExports));
    return cursor;
}

size_t
ExportMap::sizeOfExcludingThis(MallocSizeOf mallocSizeOf) const
{
    return SizeOfVectorExcludingThis(fieldNames, mallocSizeOf) &&
           fieldsToExports.sizeOfExcludingThis(mallocSizeOf);
}

size_t
Module::serializedSize() const
{
    return SerializedPodVectorSize(code_) +
           linkData_.serializedSize() +
           SerializedVectorSize(importNames_) +
           exportMap_.serializedSize() +
           SerializedPodVectorSize(dataSegments_) +
           metadata_->serializedSize() +
           SerializedPodVectorSize(bytecode_->bytes);
}

uint8_t*
Module::serialize(uint8_t* cursor) const
{
    cursor = SerializePodVector(cursor, code_);
    cursor = linkData_.serialize(cursor);
    cursor = SerializeVector(cursor, importNames_);
    cursor = exportMap_.serialize(cursor);
    cursor = SerializePodVector(cursor, dataSegments_);
    cursor = metadata_->serialize(cursor);
    cursor = SerializePodVector(cursor, bytecode_->bytes);
    return cursor;
}

/* static */ const uint8_t*
Module::deserialize(ExclusiveContext* cx, const uint8_t* cursor, UniquePtr<Module>* module,
                    Metadata* maybeMetadata)
{
    Bytes code;
    cursor = DeserializePodVector(cx, cursor, &code);
    if (!cursor)
        return nullptr;

    LinkData linkData;
    cursor = linkData.deserialize(cx, cursor);
    if (!cursor)
        return nullptr;

    ImportNameVector importNames;
    cursor = DeserializeVector(cx, cursor, &importNames);
    if (!cursor)
        return nullptr;

    ExportMap exportMap;
    cursor = exportMap.deserialize(cx, cursor);
    if (!cursor)
        return nullptr;

    DataSegmentVector dataSegments;
    cursor = DeserializePodVector(cx, cursor, &dataSegments);
    if (!cursor)
        return nullptr;

    MutableMetadata metadata;
    if (maybeMetadata) {
        metadata = maybeMetadata;
    } else {
        metadata = cx->new_<Metadata>();
        if (!metadata)
            return nullptr;
    }
    cursor = metadata->deserialize(cx, cursor);
    if (!cursor)
        return nullptr;
    MOZ_RELEASE_ASSERT(!!maybeMetadata == metadata->isAsmJS());

    MutableBytes bytecode = cx->new_<ShareableBytes>();
    if (!bytecode)
        return nullptr;
    cursor = DeserializePodVector(cx, cursor, &bytecode->bytes);
    if (!cursor)
        return nullptr;

    *module = cx->make_unique<Module>(Move(code),
                                      Move(linkData),
                                      Move(importNames),
                                      Move(exportMap),
                                      Move(dataSegments),
                                      *metadata,
                                      *bytecode);
    if (!*module)
        return nullptr;

    return cursor;
}

/* virtual */ void
Module::addSizeOfMisc(MallocSizeOf mallocSizeOf,
                      Metadata::SeenSet* seenMetadata,
                      ShareableBytes::SeenSet* seenBytes,
                      size_t* code,
                      size_t* data) const
{
    *data += mallocSizeOf(this) +
             code_.sizeOfExcludingThis(mallocSizeOf) +
             linkData_.sizeOfExcludingThis(mallocSizeOf) +
             importNames_.sizeOfExcludingThis(mallocSizeOf) +
             exportMap_.sizeOfExcludingThis(mallocSizeOf) +
             dataSegments_.sizeOfExcludingThis(mallocSizeOf) +
             metadata_->sizeOfIncludingThisIfNotSeen(mallocSizeOf, seenMetadata) +
             bytecode_->sizeOfIncludingThisIfNotSeen(mallocSizeOf, seenBytes);
}

bool
Module::instantiate(JSContext* cx,
                    Handle<FunctionVector> funcImports,
                    Handle<ArrayBufferObjectMaybeShared*> asmJSHeap,
                    MutableHandleWasmInstanceObject instanceObj) const
{
    MOZ_ASSERT(funcImports.length() == metadata_->imports.length());
    MOZ_ASSERT_IF(asmJSHeap, metadata_->isAsmJS());

    // asm.js module instantiation supplies its own heap, but for wasm, create
    // and initialize the heap if one is requested.

    Rooted<ArrayBufferObjectMaybeShared*> heap(cx, asmJSHeap);
    if (metadata_->usesHeap() && !heap) {
        MOZ_ASSERT(!metadata_->isAsmJS());
        bool signalsForOOB = metadata_->compileArgs.useSignalHandlersForOOB;
        heap = ArrayBufferObject::createForWasm(cx, metadata_->initialHeapLength, signalsForOOB);
        if (!heap)
            return false;
    }

    uint8_t* memoryBase = heap ? heap->dataPointerEither().unwrap(/* code patching */) : nullptr;
    uint32_t memoryLength = heap ? heap->byteLength() : 0;

    const uint8_t* bytecode = bytecode_->begin();
    for (const DataSegment& seg : dataSegments_)
        memcpy(memoryBase + seg.memoryOffset, bytecode + seg.bytecodeOffset, seg.length);

    // Create a new, specialized CodeSegment for the new Instance (for now).

    auto cs = CodeSegment::create(cx, code_, linkData_, *metadata_, memoryBase, memoryLength);
    if (!cs)
        return false;

    // To support viewing the source of an instance (Instance::createText), the
    // instance must hold onto a ref of the bytecode (keeping it alive). This
    // wastes memory for most users, so we try to only save the source when a
    // developer actually cares: when the compartment is debuggable (which is
    // true when the web console is open) or a names section is implied (since
    // this going to be stripped for non-developer builds).
    const ShareableBytes* maybeBytecode = nullptr;
    if (cx->compartment()->isDebuggee() || !metadata_->funcNames.empty())
        maybeBytecode = bytecode_.get();

    // Store a summary of LinkData::FuncTableVector, only as much is needed
    // for runtime toggling of profiling mode. Currently, only asm.js has typed
    // function tables.
    TypedFuncTableVector typedFuncTables;
    if (metadata_->isAsmJS()) {
        if (!typedFuncTables.reserve(linkData_.funcTables.length()))
            return false;
        for (const LinkData::FuncTable& tbl : linkData_.funcTables)
            typedFuncTables.infallibleEmplaceBack(tbl.globalDataOffset, tbl.elemOffsets.length());
    }

    return Instance::create(cx, Move(cs), *metadata_, maybeBytecode, Move(typedFuncTables),
                            heap, funcImports, exportMap_, instanceObj);
}

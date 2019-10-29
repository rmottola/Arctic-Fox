/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/x64/Lowering-x64.h"

#include "jit/MIR.h"
#include "jit/x64/Assembler-x64.h"

#include "jit/shared/Lowering-shared-inl.h"

using namespace js;
using namespace js::jit;

void
LIRGeneratorX64::useBoxFixed(LInstruction *lir, size_t n, MDefinition *mir, Register reg1, Register)
{
    MOZ_ASSERT(mir->type() == MIRType_Value);

    ensureDefined(mir);
    lir->setOperand(n, LUse(reg1, mir->virtualRegister()));
}

LAllocation
LIRGeneratorX64::useByteOpRegister(MDefinition* mir)
{
    return useRegister(mir);
}

LAllocation
LIRGeneratorX64::useByteOpRegisterOrNonDoubleConstant(MDefinition* mir)
{
    return useRegisterOrNonDoubleConstant(mir);
}

LDefinition
LIRGeneratorX64::tempByteOpRegister()
{
    return temp();
}

LDefinition
LIRGeneratorX64::tempToUnbox()
{
    return temp();
}

void
LIRGeneratorX64::visitBox(MBox* box)
{
    MDefinition* opd = box->getOperand(0);

    // If the operand is a constant, emit near its uses.
    if (opd->isConstant() && box->canEmitAtUses()) {
        emitAtUses(box);
        return;
    }

    if (opd->isConstant()) {
        define(new(alloc()) LValue(opd->toConstant()->value()), box, LDefinition(LDefinition::BOX));
    } else {
        LBox* ins = new(alloc()) LBox(opd->type(), useRegister(opd));
        define(ins, box, LDefinition(LDefinition::BOX));
    }
}

void
LIRGeneratorX64::visitUnbox(MUnbox* unbox)
{
    MDefinition* box = unbox->getOperand(0);

    if (box->type() == MIRType_ObjectOrNull) {
        LUnboxObjectOrNull* lir = new(alloc()) LUnboxObjectOrNull(useRegisterAtStart(box));
        if (unbox->fallible())
            assignSnapshot(lir, unbox->bailoutKind());
        defineReuseInput(lir, unbox, 0);
        return;
    }

    MOZ_ASSERT(box->type() == MIRType_Value);

    LUnboxBase* lir;
    if (IsFloatingPointType(unbox->type())) {
        lir = new(alloc()) LUnboxFloatingPoint(useRegisterAtStart(box), unbox->type());
    } else if (unbox->fallible()) {
        // If the unbox is fallible, load the Value in a register first to
        // avoid multiple loads.
        lir = new(alloc()) LUnbox(useRegisterAtStart(box));
    } else {
        lir = new(alloc()) LUnbox(useAtStart(box));
    }

    if (unbox->fallible())
        assignSnapshot(lir, unbox->bailoutKind());

    define(lir, unbox);
}

void
LIRGeneratorX64::visitReturn(MReturn* ret)
{
    MDefinition* opd = ret->getOperand(0);
    MOZ_ASSERT(opd->type() == MIRType_Value);

    LReturn* ins = new(alloc()) LReturn;
    ins->setOperand(0, useFixed(opd, JSReturnReg));
    add(ins);
}

void
LIRGeneratorX64::defineUntypedPhi(MPhi* phi, size_t lirIndex)
{
    defineTypedPhi(phi, lirIndex);
}

void
LIRGeneratorX64::lowerUntypedPhiInput(MPhi* phi, uint32_t inputPosition, LBlock* block, size_t lirIndex)
{
    lowerTypedPhiInput(phi, inputPosition, block, lirIndex);
}

void
LIRGeneratorX64::visitCompareExchangeTypedArrayElement(MCompareExchangeTypedArrayElement *ins)
{
    lowerCompareExchangeTypedArrayElement(ins, /* useI386ByteRegisters = */ false);
}

void
LIRGeneratorX64::visitAtomicTypedArrayElementBinop(MAtomicTypedArrayElementBinop *ins)
{
    lowerAtomicTypedArrayElementBinop(ins, /* useI386ByteRegisters = */ false);
}

void
LIRGeneratorX64::visitAsmJSUnsignedToDouble(MAsmJSUnsignedToDouble *ins)
{
    MOZ_ASSERT(ins->input()->type() == MIRType_Int32);
    LAsmJSUInt32ToDouble* lir = new(alloc()) LAsmJSUInt32ToDouble(useRegisterAtStart(ins->input()));
    define(lir, ins);
}

void
LIRGeneratorX64::visitAsmJSUnsignedToFloat32(MAsmJSUnsignedToFloat32 *ins)
{
    MOZ_ASSERT(ins->input()->type() == MIRType_Int32);
    LAsmJSUInt32ToFloat32* lir = new(alloc()) LAsmJSUInt32ToFloat32(useRegisterAtStart(ins->input()));
    define(lir, ins);
}

void
LIRGeneratorX64::visitAsmJSLoadHeap(MAsmJSLoadHeap *ins)
{
    MDefinition* ptr = ins->ptr();
    MOZ_ASSERT(ptr->type() == MIRType_Int32);

    // For simplicity, require a register if we're going to emit a bounds-check
    // branch, so that we don't have special cases for constants.
    LAllocation ptrAlloc = gen->needsAsmJSBoundsCheckBranch(ins)
                           ? useRegisterAtStart(ptr)
                           : useRegisterOrZeroAtStart(ptr);

    define(new(alloc()) LAsmJSLoadHeap(ptrAlloc), ins);
}

void
LIRGeneratorX64::visitAsmJSStoreHeap(MAsmJSStoreHeap *ins)
{
    MDefinition *ptr = ins->ptr();
    MOZ_ASSERT(ptr->type() == MIRType_Int32);

    // For simplicity, require a register if we're going to emit a bounds-check
    // branch, so that we don't have special cases for constants.
    LAllocation ptrAlloc = gen->needsAsmJSBoundsCheckBranch(ins)
                           ? useRegisterAtStart(ptr)
                           : useRegisterOrZeroAtStart(ptr);

    LAsmJSStoreHeap* lir = nullptr;  // initialize to silence GCC warning
    switch (ins->accessType()) {
      case Scalar::Int8:
      case Scalar::Uint8:
      case Scalar::Int16:
      case Scalar::Uint16:
      case Scalar::Int32:
      case Scalar::Uint32:
        lir = new(alloc()) LAsmJSStoreHeap(ptrAlloc, useRegisterOrConstantAtStart(ins->value()));
        break;
      case Scalar::Float32:
      case Scalar::Float64:
      case Scalar::Float32x4:
      case Scalar::Int32x4:
        lir = new(alloc()) LAsmJSStoreHeap(ptrAlloc, useRegisterAtStart(ins->value()));
        break;
      case Scalar::Uint8Clamped:
      case Scalar::MaxTypedArrayViewType:
        MOZ_CRASH("unexpected array type");
    }
    add(lir, ins);
}

void
LIRGeneratorX64::visitAsmJSCompareExchangeHeap(MAsmJSCompareExchangeHeap *ins)
{
    MDefinition *ptr = ins->ptr();
    MOZ_ASSERT(ptr->type() == MIRType_Int32);

    const LAllocation oldval = useRegister(ins->oldValue());
    const LAllocation newval = useRegister(ins->newValue());

    LAsmJSCompareExchangeHeap *lir =
        new(alloc()) LAsmJSCompareExchangeHeap(useRegister(ptr), oldval, newval);

    defineFixed(lir, ins, LAllocation(AnyRegister(eax)));
}

void
LIRGeneratorX64::visitAsmJSAtomicBinopHeap(MAsmJSAtomicBinopHeap *ins)
{
    MDefinition *ptr = ins->ptr();
    MOZ_ASSERT(ptr->type() == MIRType_Int32);

    // Register allocation:
    //
    // For ADD and SUB we'll use XADD (with word and byte ops as appropriate):
    //
    //    movl       value, output
    //    lock xaddl output, mem
    //
    // For AND/OR/XOR we need to use a CMPXCHG loop:
    //
    //    movl          *mem, eax
    // L: mov           eax, temp
    //    andl          value, temp
    //    lock cmpxchg  temp, mem  ; reads eax also
    //    jnz           L
    //    ; result in eax
    //
    // Note the placement of L, cmpxchg will update eax with *mem if
    // *mem does not have the expected value, so reloading it at the
    // top of the loop would be redundant.
    //
    // We want to fix eax as the output.  We also need a temp for
    // the intermediate value.
    //
    // There are optimization opportunities:
    //  - when the result is unused, Bug #1077014.

    bool bitOp = !(ins->operation() == AtomicFetchAddOp || ins->operation() == AtomicFetchSubOp);
    LAllocation value = useRegister(ins->value());
    LDefinition tempDef = bitOp ? temp() : LDefinition::BogusTemp();

    LAsmJSAtomicBinopHeap *lir =
        new(alloc()) LAsmJSAtomicBinopHeap(useRegister(ptr), value, tempDef);

    defineFixed(lir, ins, LAllocation(AnyRegister(eax)));
}

void
LIRGeneratorX64::visitAsmJSLoadFuncPtr(MAsmJSLoadFuncPtr* ins)
{
    define(new(alloc()) LAsmJSLoadFuncPtr(useRegister(ins->index()), temp()), ins);
}

void
LIRGeneratorX64::visitSubstr(MSubstr* ins)
{
    LSubstr* lir = new (alloc()) LSubstr(useRegister(ins->string()),
                                         useRegister(ins->begin()),
                                         useRegister(ins->length()),
                                         temp(),
                                         temp(),
                                         tempByteOpRegister());
    define(lir, ins);
    assignSafepoint(lir, ins);
}

void
LIRGeneratorX64::visitStoreTypedArrayElementStatic(MStoreTypedArrayElementStatic* ins)
{
    MOZ_CRASH("NYI");
}

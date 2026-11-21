/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/MathAlgorithms.h"

#include "jit/Lowering.h"
#include "jit/osxppc/Assembler-ppc.h"
#include "jit/MIR.h"

#include "jit/shared/Lowering-shared-inl.h"

using namespace js;
using namespace js::jit;

using mozilla::FloorLog2;

void
LIRGeneratorPPC::useBoxFixed(LInstruction *lir, size_t n, MDefinition *mir, Register reg1,
                              Register reg2, bool useAtStart)
{
    MOZ_ASSERT(mir->type() == MIRType_Value);
    MOZ_ASSERT(reg1 != reg2);

    ensureDefined(mir);
    lir->setOperand(n, LUse(reg1, mir->virtualRegister(), useAtStart));
    lir->setOperand(n + 1, LUse(reg2, VirtualRegisterOfPayload(mir), useAtStart));
}

LAllocation
LIRGeneratorPPC::useByteOpRegister(MDefinition *mir)
{
    return useRegister(mir);
}

LAllocation
LIRGeneratorPPC::useByteOpRegisterOrNonDoubleConstant(MDefinition *mir)
{
    return useRegisterOrNonDoubleConstant(mir);
}

LDefinition
LIRGeneratorPPC::tempByteOpRegister()
{
	return temp();
}

void
LIRGeneratorPPC::lowerConstantDouble(double d, MInstruction *mir)
{
    define(new(alloc()) LDouble(d), mir);
}

void
LIRGeneratorPPC::lowerConstantFloat32(float d, MInstruction *mir)
{
    define(new(alloc()) LFloat32(d), mir);
}

void
LIRGeneratorPPC::visitConstant(MConstant *ins)
{
    if (ins->type() == MIRType_Double)
        lowerConstantDouble(ins->value().toDouble(), ins);
    else if (ins->type() == MIRType_Float32)
        lowerConstantFloat32(ins->value().toDouble(), ins);
    else if (ins->canEmitAtUses())
        emitAtUses(ins);
	else LIRGeneratorShared::visitConstant(ins);
}

void
LIRGeneratorPPC::visitBox(MBox *box)
{
    MDefinition *inner = box->getOperand(0);

    // If the box wrapped a double, it needs a new register.
    if (IsFloatingPointType(inner->type())) {
        defineBox(new(alloc()) LBoxFloatingPoint(useRegisterAtStart(inner),
                                                 tempCopy(inner, 0), inner->type()), box);
        return;
    }

    if (box->canEmitAtUses()) {
        emitAtUses(box);
        return;
    }

    if (inner->isConstant()) {
        defineBox(new(alloc()) LValue(inner->toConstant()->value()), box);
        return;
    }

    LBox *lir = new(alloc()) LBox(use(inner), inner->type());

    // Otherwise, we should not define a new register for the payload portion
    // of the output, so bypass defineBox().
    // As of Fx38, getVirtualRegister is infallible (see bug 1107774).
    uint32_t vreg = getVirtualRegister();

    // Note that because we're using BogusTemp(), we do not change the type of
    // the definition. We also do not define the first output as "TYPE",
    // because it has no corresponding payload at (vreg + 1). Also note that
    // although we copy the input's original type for the payload half of the
    // definition, this is only for clarity. BogusTemp() definitions are
    // ignored.
    lir->setDef(0, LDefinition(vreg, LDefinition::GENERAL));
    lir->setDef(1, LDefinition::BogusTemp());
    box->setVirtualRegister(vreg);
    add(lir);
}

void
LIRGeneratorPPC::visitUnbox(MUnbox *unbox)
{
    MDefinition *inner = unbox->getOperand(0);

	// Fast path for objects.
    if (inner->type() == MIRType_ObjectOrNull) {
        LUnboxObjectOrNull* lir = new(alloc()) LUnboxObjectOrNull(useRegisterAtStart(inner));
        if (unbox->fallible())
            assignSnapshot(lir, unbox->bailoutKind());
        defineReuseInput(lir, unbox, 0);
        return;
    }

    // An unbox on PPC reads in a type tag (either in memory or a register) and
    // a payload. Unlike most instructions consuming a box, we ask for the type
    // second, so that the result can re-use the first input.
    MOZ_ASSERT(inner->type() == MIRType_Value);

    ensureDefined(inner);

    if (IsFloatingPointType(unbox->type())) {
        LUnboxFloatingPoint *lir = new(alloc()) LUnboxFloatingPoint(unbox->type());
        if (unbox->fallible())
        	assignSnapshot(lir, unbox->bailoutKind()); // infallible
        useBox(lir, LUnboxFloatingPoint::Input, inner);
        define(lir, unbox);
        return;
    }

    // Swap the order we use the box pieces so we can re-use the payload
    // register.
    LUnbox *lir = new(alloc()) LUnbox;
    lir->setOperand(0, usePayloadInRegisterAtStart(inner));
    lir->setOperand(1, useType(inner, LUse::REGISTER));

    if (unbox->fallible())
    	assignSnapshot(lir, unbox->bailoutKind());

    // Types and payloads form two separate intervals. If the type becomes dead
    // before the payload, it could be used as a Value without the type being
    // recoverable. Unbox's purpose is to eagerly kill the definition of a type
    // tag, so keeping both alive (for the purpose of gcmaps) is unappealing.
    // Instead, we create a new virtual register.
    defineReuseInput(lir, unbox, 0);
}

void
LIRGeneratorPPC::visitReturn(MReturn *ret)
{
    MDefinition *opd = ret->getOperand(0);
    MOZ_ASSERT(opd->type() == MIRType_Value);

    LReturn *ins = new(alloc()) LReturn;
    ins->setOperand(0, LUse(JSReturnReg_Type));
    ins->setOperand(1, LUse(JSReturnReg_Data));
    fillBoxUses(ins, 0, opd);
    add(ins);
}

// x = !y
void
LIRGeneratorPPC::lowerForALU(LInstructionHelper<1, 1, 0> *ins,
                              MDefinition *mir, MDefinition *input)
{
    ins->setOperand(0, useRegister(input));
    define(ins, mir,
                  LDefinition(LDefinition::TypeFrom(mir->type()), LDefinition::REGISTER));
}

// z = x+y
void
LIRGeneratorPPC::lowerForALU(LInstructionHelper<1, 2, 0> *ins, MDefinition *mir,
                              MDefinition *lhs, MDefinition *rhs)
{
    ins->setOperand(0, useRegister(lhs));
    ins->setOperand(1, useRegisterOrConstant(rhs));
    define(ins, mir,
                  LDefinition(LDefinition::TypeFrom(mir->type()), LDefinition::REGISTER));
}

void
LIRGeneratorPPC::lowerForFPU(LInstructionHelper<1, 1, 0> *ins, MDefinition *mir,
                              MDefinition *input)
{
    ins->setOperand(0, useRegister(input));
    define(ins, mir,
                  LDefinition(LDefinition::TypeFrom(mir->type()), LDefinition::REGISTER));
}

template <size_t Temps> void
LIRGeneratorPPC::lowerForFPU(LInstructionHelper<1, 2, Temps> *ins, MDefinition *mir,
                              MDefinition *lhs, MDefinition *rhs)
{
    ins->setOperand(0, useRegister(lhs));
    ins->setOperand(1, useRegister(rhs));
    define(ins, mir,
                  LDefinition(LDefinition::TypeFrom(mir->type()), LDefinition::REGISTER));
}

// eh?
void
LIRGeneratorPPC::lowerForFPU(LInstructionHelper<1, 2, 0> *ins, MDefinition *mir,
                              MDefinition *lhs, MDefinition *rhs)
{
    ins->setOperand(0, useRegister(lhs));
    ins->setOperand(1, useRegister(rhs));
    define(ins, mir,
                  LDefinition(LDefinition::TypeFrom(mir->type()), LDefinition::REGISTER));
}
// double eh?
void
LIRGeneratorPPC::lowerForFPU(LInstructionHelper<1, 2, 1> *ins, MDefinition *mir,
                              MDefinition *lhs, MDefinition *rhs)
{
	// XXX: This comes from js::jit::LIRGenerator::visitSimdShuffle
	MOZ_CRASH();
}

void
LIRGeneratorPPC::lowerForBitAndAndBranch(LBitAndAndBranch *baab, MInstruction *mir,
                                          MDefinition *lhs, MDefinition *rhs)
{
    baab->setOperand(0, useRegisterAtStart(lhs));
    baab->setOperand(1, useRegisterOrConstantAtStart(rhs));
    add(baab, mir);
}

void
LIRGeneratorPPC::defineUntypedPhi(MPhi *phi, size_t lirIndex)
{
    LPhi *type = current->getPhi(lirIndex + VREG_TYPE_OFFSET);
    LPhi *payload = current->getPhi(lirIndex + VREG_DATA_OFFSET);

    uint32_t typeVreg = getVirtualRegister();
    phi->setVirtualRegister(typeVreg);

    uint32_t payloadVreg = getVirtualRegister();
    MOZ_ASSERT(typeVreg + 1 == payloadVreg);

    type->setDef(0, LDefinition(typeVreg, LDefinition::TYPE));
    payload->setDef(0, LDefinition(payloadVreg, LDefinition::PAYLOAD));
    annotate(type);
    annotate(payload);
}

void
LIRGeneratorPPC::lowerUntypedPhiInput(MPhi *phi, uint32_t inputPosition,
                                       LBlock *block, size_t lirIndex)
{
	// WHAT THE HELL IS THIS
    MDefinition *operand = phi->getOperand(inputPosition);
    LPhi *type = block->getPhi(lirIndex + VREG_TYPE_OFFSET);
    LPhi *payload = block->getPhi(lirIndex + VREG_DATA_OFFSET);
    type->setOperand(inputPosition, LUse(operand->virtualRegister() + VREG_TYPE_OFFSET,
                                         LUse::ANY));
    payload->setOperand(inputPosition, LUse(VirtualRegisterOfPayload(operand), LUse::ANY));
}

void
LIRGeneratorPPC::lowerForShift(LInstructionHelper<1, 2, 0> *ins, MDefinition *mir,
                                MDefinition *lhs, MDefinition *rhs)
{
    ins->setOperand(0, useRegister(lhs));
    ins->setOperand(1, useRegisterOrConstant(rhs));
    define(ins, mir);
}

void
LIRGeneratorPPC::lowerDivI(MDiv *div)
{
    if (div->isUnsigned()) {
        lowerUDiv(div);
        return;
    }

    // Division instructions are slow. Division by constant denominators can be
    // rewritten to use other instructions.
    if (div->rhs()->isConstant()) {
        int32_t rhs = div->rhs()->toConstant()->value().toInt32();
        // Check for division by a positive power of two, which is an easy and
        // important case to optimize. Note that other optimizations are also
        // possible; division by negative powers of two can be optimized in a
        // similar manner as positive powers of two, and division by other
        // constants can be optimized by a reciprocal multiplication technique.
        int32_t shift = FloorLog2(rhs);
        if (rhs > 0 && 1 << shift == rhs) {
            LDivPowTwoI *lir = new(alloc()) LDivPowTwoI(useRegister(div->lhs()), shift, temp());
            if (div->fallible())
            	assignSnapshot(lir, Bailout_DoubleOutput);
            define(lir, div);
            return;
        }
    }

    LDivI *lir = new(alloc()) LDivI(useRegister(div->lhs()), useRegister(div->rhs()), temp());
    if (div->fallible())
    	assignSnapshot(lir, Bailout_DoubleOutput);
    define(lir, div);
}

void
LIRGeneratorPPC::lowerMulI(MMul *mul, MDefinition *lhs, MDefinition *rhs)
{
    LMulI *lir = new(alloc()) LMulI;
    if (mul->fallible())
    	assignSnapshot(lir, Bailout_DoubleOutput);
    lowerForALU(lir, mul, lhs, rhs);
}

void
LIRGeneratorPPC::lowerModI(MMod *mod)
{
    if (mod->isUnsigned()) {
        lowerUMod(mod);
        return;
    }

    if (mod->rhs()->isConstant()) {
        int32_t rhs = mod->rhs()->toConstant()->value().toInt32();
        int32_t shift = FloorLog2(rhs);
        if (rhs > 0 && 1 << shift == rhs) {
            LModPowTwoI *lir = new(alloc()) LModPowTwoI(useRegister(mod->lhs()), shift);
            if (mod->fallible())
            	assignSnapshot(lir, Bailout_DoubleOutput);
            define(lir, mod);
            return;
        }
    }
    LModI *lir = new(alloc()) LModI(useRegister(mod->lhs()), useRegister(mod->rhs()),
                           temp(LDefinition::GENERAL));

    if (mod->fallible())
    	assignSnapshot(lir, Bailout_DoubleOutput);
    define(lir, mod);
}

void
LIRGeneratorPPC::visitPowHalf(MPowHalf *ins)
{
    MDefinition *input = ins->input();
    MOZ_ASSERT(input->type() == MIRType_Double);
    LPowHalfD *lir = new(alloc()) LPowHalfD(useRegisterAtStart(input));
    defineReuseInput(lir, ins, 0);
}

LTableSwitch *
LIRGeneratorPPC::newLTableSwitch(const LAllocation &in, const LDefinition &inputCopy,
                                  MTableSwitch *tableswitch)
{
    return new(alloc()) LTableSwitch(in, inputCopy, temp(), tableswitch);
}

LTableSwitchV *
LIRGeneratorPPC::newLTableSwitchV(MTableSwitch *tableswitch)
{
    return new(alloc()) LTableSwitchV(temp(), tempDouble(), temp(), tableswitch);
}

void
LIRGeneratorPPC::visitGuardShape(MGuardShape *ins)
{
    MOZ_ASSERT(ins->obj()->type() == MIRType_Object);

    LDefinition tempObj = temp(LDefinition::OBJECT);
    LGuardShape *guard = new(alloc()) LGuardShape(useRegister(ins->obj()), tempObj);
    assignSnapshot(guard, ins->bailoutKind());
    add(guard, ins);
    redefine(ins, ins->obj());
}

void
LIRGeneratorPPC::visitGuardObjectGroup(MGuardObjectGroup *ins)
{
    MOZ_ASSERT(ins->obj()->type() == MIRType_Object);

    LDefinition tempObj = temp(LDefinition::OBJECT);
    LGuardObjectGroup *guard = new(alloc()) LGuardObjectGroup(useRegister(ins->obj()), tempObj);
    assignSnapshot(guard, ins->bailoutKind());
    add(guard, ins);
    redefine(ins, ins->obj());
}

void
LIRGeneratorPPC::lowerUrshD(MUrsh *mir)
{
    MDefinition *lhs = mir->lhs();
    MDefinition *rhs = mir->rhs();

    MOZ_ASSERT(lhs->type() == MIRType_Int32);
    MOZ_ASSERT(rhs->type() == MIRType_Int32);

    LUrshD *lir = new(alloc()) LUrshD(useRegister(lhs), useRegisterOrConstant(rhs), temp());
    define(lir, mir);
}

void
LIRGeneratorPPC::visitAsmJSNeg(MAsmJSNeg *ins)
{
    if (ins->type() == MIRType_Int32)
        define(new(alloc()) LNegI(useRegisterAtStart(ins->input())), ins);
    else if (ins->type() == MIRType_Float32)
        define(new(alloc()) LNegF(useRegisterAtStart(ins->input())), ins);
    else {
    	MOZ_ASSERT(ins->type() == MIRType_Double);
    	define(new(alloc()) LNegD(useRegisterAtStart(ins->input())), ins);
    }
}

void
LIRGeneratorPPC::lowerUDiv(MDiv *div)
{
    MDefinition *lhs = div->getOperand(0);
    MDefinition *rhs = div->getOperand(1);

    LUDivOrMod *lir = new(alloc()) LUDivOrMod;
    lir->setOperand(0, useRegister(lhs));
    lir->setOperand(1, useRegister(rhs));
    if (div->fallible())
    	assignSnapshot(lir, Bailout_DoubleOutput);
    define(lir, div);
}

void
LIRGeneratorPPC::lowerUMod(MMod *mod)
{
    MDefinition *lhs = mod->getOperand(0);
    MDefinition *rhs = mod->getOperand(1);

    LUDivOrMod *lir = new(alloc()) LUDivOrMod;
    lir->setOperand(0, useRegister(lhs));
    lir->setOperand(1, useRegister(rhs));
    if (mod->fallible())
    	assignSnapshot(lir, Bailout_DoubleOutput);
    define(lir, mod);
}

void
LIRGeneratorPPC::visitAsmJSUnsignedToDouble(MAsmJSUnsignedToDouble *ins)
{
    MOZ_ASSERT(ins->input()->type() == MIRType_Int32);
    LAsmJSUInt32ToDouble *lir = new(alloc()) LAsmJSUInt32ToDouble(useRegisterAtStart(ins->input()));
    define(lir, ins);
}

void
LIRGeneratorPPC::visitAsmJSUnsignedToFloat32(MAsmJSUnsignedToFloat32 *ins)
{
    MOZ_ASSERT(ins->input()->type() == MIRType_Int32);
    LAsmJSUInt32ToFloat32 *lir = new(alloc()) LAsmJSUInt32ToFloat32(useRegisterAtStart(ins->input()));
    define(lir, ins);
}

void
LIRGeneratorPPC::visitAsmJSLoadHeap(MAsmJSLoadHeap *ins)
{
    MDefinition *ptr = ins->ptr();
    MOZ_ASSERT(ptr->type() == MIRType_Int32);
    LAllocation ptrAlloc;

    // For PowerPC it would be better to keep the pointer in a register
    // if bounds checking is needed.
    if (ptr->isConstant() && !ins->needsBoundsCheck()) {
        int32_t ptrValue = ptr->toConstant()->value().toInt32();
        // A bounds check is only skipped for a positive index.
        MOZ_ASSERT(ptrValue >= 0);
        ptrAlloc = LAllocation(ptr->toConstant()->vp());
    } else
        ptrAlloc = useRegisterAtStart(ptr);

    define(new(alloc()) LAsmJSLoadHeap(ptrAlloc), ins);
}

void
LIRGeneratorPPC::visitAsmJSStoreHeap(MAsmJSStoreHeap *ins)
{
    MDefinition *ptr = ins->ptr();
    MOZ_ASSERT(ptr->type() == MIRType_Int32);
    LAllocation ptrAlloc;

    if (ptr->isConstant() && !ins->needsBoundsCheck()) {
        MOZ_ASSERT(ptr->toConstant()->value().toInt32() >= 0);
        ptrAlloc = LAllocation(ptr->toConstant()->vp());
    } else
        ptrAlloc = useRegisterAtStart(ptr);

    add(new(alloc()) LAsmJSStoreHeap(ptrAlloc, useRegisterAtStart(ins->value())), ins);
}

void
LIRGeneratorPPC::visitAsmJSLoadFuncPtr(MAsmJSLoadFuncPtr *ins)
{
    define(new(alloc()) LAsmJSLoadFuncPtr(useRegister(ins->index())), ins);
}

void
LIRGeneratorPPC::lowerTruncateDToInt32(MTruncateToInt32 *ins)
{
    MDefinition *opd = ins->input();
    MOZ_ASSERT(opd->type() == MIRType_Double);

    define(new(alloc()) LTruncateDToInt32(useRegister(opd), LDefinition::BogusTemp()), ins);
}

void
LIRGeneratorPPC::lowerTruncateFToInt32(MTruncateToInt32 *ins)
{
    MDefinition *opd = ins->input();
    MOZ_ASSERT(opd->type() == MIRType_Float32);

    define(new(alloc()) LTruncateFToInt32(useRegister(opd), LDefinition::BogusTemp()), ins);
}

void
LIRGeneratorPPC::visitSubstr(MSubstr *ins)
{
    LSubstr *lir = new (alloc()) LSubstr(useRegister(ins->string()),
                                         useRegister(ins->begin()),
                                         useRegister(ins->length()),
                                         temp(),
                                         temp(),
                                         tempByteOpRegister());
    define(lir, ins);
    assignSafepoint(lir, ins);
}

void
LIRGeneratorPPC::visitStoreTypedArrayElementStatic(MStoreTypedArrayElementStatic *ins)
{
    MOZ_CRASH("NYI");
}

void
LIRGeneratorPPC::visitSimdBinaryArith(MSimdBinaryArith* ins)
{
    MOZ_CRASH("NYI");
}

void
LIRGeneratorPPC::visitSimdSelect(MSimdSelect* ins)
{
    MOZ_CRASH("NYI");
}

void
LIRGeneratorPPC::visitSimdSplatX4(MSimdSplatX4 *ins)
{
    MOZ_CRASH("NYI");
}

void
LIRGeneratorPPC::visitSimdValueX4(MSimdValueX4 *ins)
{
    MOZ_CRASH("NYI");
}

void
LIRGeneratorPPC::visitCompareExchangeTypedArrayElement(MCompareExchangeTypedArrayElement* ins)
{
    MOZ_CRASH("NYI");
}

void
LIRGeneratorPPC::visitAtomicExchangeTypedArrayElement(MAtomicExchangeTypedArrayElement* ins)
{
    MOZ_CRASH("NYI");
}

void
LIRGeneratorPPC::visitAsmJSCompareExchangeHeap(MAsmJSCompareExchangeHeap* ins)
{
    MOZ_CRASH("NYI");
}

void
LIRGeneratorPPC::visitAsmJSAtomicExchangeHeap(MAsmJSAtomicExchangeHeap* ins)
{
    MOZ_CRASH("NYI");
}

void
LIRGeneratorPPC::visitAsmJSAtomicBinopHeap(MAsmJSAtomicBinopHeap* ins)
{
    MOZ_CRASH("NYI");
}

void
LIRGeneratorPPC::visitAtomicTypedArrayElementBinop(MAtomicTypedArrayElementBinop *ins)
{
    MOZ_CRASH("NYI");
}

void
LIRGeneratorPPC::visitRandom(MRandom* ins)
{
    LRandom *lir = new(alloc()) LRandom(temp(),
                                        temp(),
                                        temp(),
                                        temp(),
                                        temp());
    defineFixed(lir, ins, LFloatReg(ReturnDoubleReg));
}

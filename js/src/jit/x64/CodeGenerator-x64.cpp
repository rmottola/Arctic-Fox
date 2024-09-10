/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/x64/CodeGenerator-x64.h"

#include "mozilla/MathAlgorithms.h"

#include "jit/IonCaches.h"
#include "jit/MIR.h"

#include "jsscriptinlines.h"

#include "jit/MacroAssembler-inl.h"
#include "jit/shared/CodeGenerator-shared-inl.h"

using namespace js;
using namespace js::jit;

CodeGeneratorX64::CodeGeneratorX64(MIRGenerator* gen, LIRGraph* graph, MacroAssembler* masm)
  : CodeGeneratorX86Shared(gen, graph, masm)
{
}

ValueOperand
CodeGeneratorX64::ToValue(LInstruction* ins, size_t pos)
{
    return ValueOperand(ToRegister(ins->getOperand(pos)));
}

ValueOperand
CodeGeneratorX64::ToOutValue(LInstruction* ins)
{
    return ValueOperand(ToRegister(ins->getDef(0)));
}

ValueOperand
CodeGeneratorX64::ToTempValue(LInstruction* ins, size_t pos)
{
    return ValueOperand(ToRegister(ins->getTemp(pos)));
}

FrameSizeClass
FrameSizeClass::FromDepth(uint32_t frameDepth)
{
    return FrameSizeClass::None();
}

FrameSizeClass
FrameSizeClass::ClassLimit()
{
    return FrameSizeClass(0);
}

uint32_t
FrameSizeClass::frameSize() const
{
    MOZ_CRASH("x64 does not use frame size classes");
}

void
CodeGeneratorX64::visitValue(LValue* value)
{
    LDefinition* reg = value->getDef(0);
    masm.moveValue(value->value(), ToRegister(reg));
}

void
CodeGeneratorX64::visitBox(LBox* box)
{
    const LAllocation* in = box->getOperand(0);
    const LDefinition* result = box->getDef(0);

    if (IsFloatingPointType(box->type())) {
        ScratchDoubleScope scratch(masm);
        FloatRegister reg = ToFloatRegister(in);
        if (box->type() == MIRType::Float32) {
            masm.convertFloat32ToDouble(reg, scratch);
            reg = scratch;
        }
        masm.vmovq(reg, ToRegister(result));
    } else {
        masm.boxValue(ValueTypeFromMIRType(box->type()), ToRegister(in), ToRegister(result));
    }
}

void
CodeGeneratorX64::visitUnbox(LUnbox* unbox)
{
    MUnbox* mir = unbox->mir();

    if (mir->fallible()) {
        const ValueOperand value = ToValue(unbox, LUnbox::Input);
        Assembler::Condition cond;
        switch (mir->type()) {
          case MIRType::Int32:
            cond = masm.testInt32(Assembler::NotEqual, value);
            break;
          case MIRType::Boolean:
            cond = masm.testBoolean(Assembler::NotEqual, value);
            break;
          case MIRType::Object:
            cond = masm.testObject(Assembler::NotEqual, value);
            break;
          case MIRType::String:
            cond = masm.testString(Assembler::NotEqual, value);
            break;
          case MIRType::Symbol:
            cond = masm.testSymbol(Assembler::NotEqual, value);
            break;
          default:
            MOZ_CRASH("Given MIRType cannot be unboxed.");
        }
        bailoutIf(cond, unbox->snapshot());
    }

    Operand input = ToOperand(unbox->getOperand(LUnbox::Input));
    Register result = ToRegister(unbox->output());
    switch (mir->type()) {
      case MIRType::Int32:
        masm.unboxInt32(input, result);
        break;
      case MIRType::Boolean:
        masm.unboxBoolean(input, result);
        break;
      case MIRType::Object:
        masm.unboxObject(input, result);
        break;
      case MIRType::String:
        masm.unboxString(input, result);
        break;
      case MIRType::Symbol:
        masm.unboxSymbol(input, result);
        break;
      default:
        MOZ_CRASH("Given MIRType cannot be unboxed.");
    }
}

void
CodeGeneratorX64::visitCompareB(LCompareB* lir)
{
    MCompare* mir = lir->mir();

    const ValueOperand lhs = ToValue(lir, LCompareB::Lhs);
    const LAllocation* rhs = lir->rhs();
    const Register output = ToRegister(lir->output());

    MOZ_ASSERT(mir->jsop() == JSOP_STRICTEQ || mir->jsop() == JSOP_STRICTNE);

    // Load boxed boolean in ScratchReg.
    ScratchRegisterScope scratch(masm);
    if (rhs->isConstant())
        masm.moveValue(rhs->toConstant()->toJSValue(), scratch);
    else
        masm.boxValue(JSVAL_TYPE_BOOLEAN, ToRegister(rhs), scratch);

    // Perform the comparison.
    masm.cmpPtr(lhs.valueReg(), scratch);
    masm.emitSet(JSOpToCondition(mir->compareType(), mir->jsop()), output);
}

void
CodeGeneratorX64::visitCompareBAndBranch(LCompareBAndBranch* lir)
{
    MCompare* mir = lir->cmpMir();

    const ValueOperand lhs = ToValue(lir, LCompareBAndBranch::Lhs);
    const LAllocation* rhs = lir->rhs();

    MOZ_ASSERT(mir->jsop() == JSOP_STRICTEQ || mir->jsop() == JSOP_STRICTNE);

    // Load boxed boolean in ScratchReg.
    ScratchRegisterScope scratch(masm);
    if (rhs->isConstant())
        masm.moveValue(rhs->toConstant()->toJSValue(), scratch);
    else
        masm.boxValue(JSVAL_TYPE_BOOLEAN, ToRegister(rhs), scratch);

    // Perform the comparison.
    masm.cmpPtr(lhs.valueReg(), scratch);
    emitBranch(JSOpToCondition(mir->compareType(), mir->jsop()), lir->ifTrue(), lir->ifFalse());
}

void
CodeGeneratorX64::visitCompareBitwise(LCompareBitwise* lir)
{
    MCompare* mir = lir->mir();
    const ValueOperand lhs = ToValue(lir, LCompareBitwise::LhsInput);
    const ValueOperand rhs = ToValue(lir, LCompareBitwise::RhsInput);
    const Register output = ToRegister(lir->output());

    MOZ_ASSERT(IsEqualityOp(mir->jsop()));

    masm.cmpPtr(lhs.valueReg(), rhs.valueReg());
    masm.emitSet(JSOpToCondition(mir->compareType(), mir->jsop()), output);
}

void
CodeGeneratorX64::visitCompareBitwiseAndBranch(LCompareBitwiseAndBranch* lir)
{
    MCompare* mir = lir->cmpMir();

    const ValueOperand lhs = ToValue(lir, LCompareBitwiseAndBranch::LhsInput);
    const ValueOperand rhs = ToValue(lir, LCompareBitwiseAndBranch::RhsInput);

    MOZ_ASSERT(mir->jsop() == JSOP_EQ || mir->jsop() == JSOP_STRICTEQ ||
               mir->jsop() == JSOP_NE || mir->jsop() == JSOP_STRICTNE);

    masm.cmpPtr(lhs.valueReg(), rhs.valueReg());
    emitBranch(JSOpToCondition(mir->compareType(), mir->jsop()), lir->ifTrue(), lir->ifFalse());
}

void
CodeGeneratorX64::visitCompare64(LCompare64* lir)
{
    MCompare* mir = lir->mir();
    MOZ_ASSERT(mir->compareType() == MCompare::Compare_Int64 ||
               mir->compareType() == MCompare::Compare_UInt64);

    Register lhs = ToRegister(lir->getOperand(0));
    const LAllocation* rhs = lir->getOperand(1);

    if (rhs->isConstant())
        masm.cmpPtr(lhs, ImmWord(ToInt64(rhs)));
    else
        masm.cmpPtr(lhs, ToOperand(rhs));

    bool isSigned = mir->compareType() == MCompare::Compare_Int64;
    masm.emitSet(JSOpToCondition(lir->jsop(), isSigned), ToRegister(lir->output()));
}

void
CodeGeneratorX64::visitCompare64AndBranch(LCompare64AndBranch* lir)
{
    MCompare* mir = lir->cmpMir();
    MOZ_ASSERT(mir->compareType() == MCompare::Compare_Int64 ||
               mir->compareType() == MCompare::Compare_UInt64);

    Register lhs = ToRegister(lir->getOperand(0));
    const LAllocation* rhs = lir->getOperand(1);

    if (rhs->isConstant())
        masm.cmpPtr(lhs, ImmWord(ToInt64(rhs)));
    else
        masm.cmpPtr(lhs, ToOperand(rhs));

    bool isSigned = mir->compareType() == MCompare::Compare_Int64;
    emitBranch(JSOpToCondition(lir->jsop(), isSigned), lir->ifTrue(), lir->ifFalse());
}

void
CodeGeneratorX64::visitBitOpI64(LBitOpI64* lir)
{
    Register lhs = ToRegister(lir->getOperand(0));
    const LAllocation* rhs = lir->getOperand(1);

    switch (lir->bitop()) {
      case JSOP_BITOR:
        if (rhs->isConstant())
            masm.or64(Imm64(ToInt64(rhs)), Register64(lhs));
        else
            masm.orq(ToOperand(rhs), lhs);
        break;
      case JSOP_BITXOR:
        if (rhs->isConstant())
            masm.xor64(Imm64(ToInt64(rhs)), Register64(lhs));
        else
            masm.xorq(ToOperand(rhs), lhs);
        break;
      case JSOP_BITAND:
        if (rhs->isConstant())
            masm.and64(Imm64(ToInt64(rhs)), Register64(lhs));
        else
            masm.andq(ToOperand(rhs), lhs);
        break;
      default:
        MOZ_CRASH("unexpected binary opcode");
    }
}

void
CodeGeneratorX64::visitShiftI64(LShiftI64* lir)
{
    Register lhs = ToRegister(lir->getOperand(0));
    const LAllocation* rhs = lir->getOperand(1);

    if (rhs->isConstant()) {
        int32_t shift = int32_t(ToInt64(rhs) & 0x3F);
        switch (lir->bitop()) {
          case JSOP_LSH:
            if (shift)
                masm.shlq(Imm32(shift), lhs);
            break;
          case JSOP_RSH:
            if (shift)
                masm.sarq(Imm32(shift), lhs);
            break;
          case JSOP_URSH:
            if (shift)
                masm.shrq(Imm32(shift), lhs);
            break;
          default:
            MOZ_CRASH("Unexpected shift op");
        }
    } else {
        MOZ_ASSERT(ToRegister(rhs) == ecx);
        switch (lir->bitop()) {
          case JSOP_LSH:
            masm.shlq_cl(lhs);
            break;
          case JSOP_RSH:
            masm.sarq_cl(lhs);
            break;
          case JSOP_URSH:
            masm.shrq_cl(lhs);
            break;
          default:
            MOZ_CRASH("Unexpected shift op");
        }
    }
}

void
CodeGeneratorX64::visitRotate64(LRotate64* lir)
{
    MRotate* mir = lir->mir();
    Register input = ToRegister(lir->input());
    const LAllocation* count = lir->count();

    if (count->isConstant()) {
        int32_t c = int32_t(ToInt64(count) & 0x3F);
        if (!c)
            return;
        if (mir->isLeftRotate())
            masm.rolq(Imm32(c), input);
        else
            masm.rorq(Imm32(c), input);
    } else {
        MOZ_ASSERT(ToRegister(count) == ecx);
        if (mir->isLeftRotate())
            masm.rolq_cl(input);
        else
            masm.rorq_cl(input);
    }
}

void
CodeGeneratorX64::visitAddI64(LAddI64* lir)
{
    Register lhs = ToRegister(lir->getOperand(0));
    const LAllocation* rhs = lir->getOperand(1);

    MOZ_ASSERT(ToRegister(lir->getDef(0)) == lhs);

    if (rhs->isConstant())
        masm.addPtr(ImmWord(ToInt64(rhs)), lhs);
    else
        masm.addq(ToOperand(rhs), lhs);
}

void
CodeGeneratorX64::visitSubI64(LSubI64* lir)
{
    Register lhs = ToRegister(lir->getOperand(0));
    const LAllocation* rhs = lir->getOperand(1);

    MOZ_ASSERT(ToRegister(lir->getDef(0)) == lhs);

    if (rhs->isConstant())
        masm.subPtr(ImmWord(ToInt64(rhs)), lhs);
    else
        masm.subq(ToOperand(rhs), lhs);
}

void
CodeGeneratorX64::visitMulI64(LMulI64* lir)
{
    Register lhs = ToRegister(lir->getOperand(0));
    const LAllocation* rhs = lir->getOperand(1);

    MOZ_ASSERT(ToRegister(lir->getDef(0)) == lhs);

    if (rhs->isConstant()) {
        int64_t constant = ToInt64(rhs);
        switch (constant) {
          case -1:
            masm.negq(lhs);
            return;
          case 0:
            masm.xorl(lhs, lhs);
            return;
          case 1:
            // nop
            return;
          case 2:
            masm.addq(lhs, lhs);
            return;
          default:
            if (constant > 0) {
                // Use shift if constant is power of 2.
                int32_t shift = mozilla::FloorLog2(constant);
                if (int64_t(1) << shift == constant) {
                    masm.shlq(Imm32(shift), lhs);
                    return;
                }
            }
            masm.mul64(Imm64(constant), Register64(lhs));
        }
    } else {
        masm.imulq(ToOperand(rhs), lhs);
    }
}

void
CodeGeneratorX64::visitDivOrModI64(LDivOrModI64* lir)
{
    Register lhs = ToRegister(lir->lhs());
    Register rhs = ToRegister(lir->rhs());
    Register output = ToRegister(lir->output());

    MOZ_ASSERT_IF(lhs != rhs, rhs != rax);
    MOZ_ASSERT(rhs != rdx);
    MOZ_ASSERT_IF(output == rax, ToRegister(lir->remainder()) == rdx);
    MOZ_ASSERT_IF(output == rdx, ToRegister(lir->remainder()) == rax);

    Label done;

    // Put the lhs in rax.
    if (lhs != rax)
        masm.mov(lhs, rax);

    // Handle divide by zero. For now match asm.js and return 0, but
    // eventually this should trap.
    if (lir->canBeDivideByZero()) {
        Label nonZero;
        masm.branchTestPtr(Assembler::NonZero, rhs, rhs, &nonZero);
        masm.xorl(output, output);
        masm.jump(&done);
        masm.bind(&nonZero);
    }

    // Handle an integer overflow exception from INT64_MIN / -1. Eventually
    // signed integer division should trap, instead of returning the
    // LHS (INT64_MIN).
    if (lir->canBeNegativeOverflow()) {
        Label notmin;
        masm.branchPtr(Assembler::NotEqual, lhs, ImmWord(INT64_MIN), &notmin);
        masm.branchPtr(Assembler::NotEqual, rhs, ImmWord(-1), &notmin);
        if (lir->mir()->isMod()) {
            masm.xorl(output, output);
        } else {
            if (lhs != output)
                masm.mov(lhs, output);
        }
        masm.jump(&done);
        masm.bind(&notmin);
    }

    // Sign extend the lhs into rdx to make rdx:rax.
    masm.cqo();
    masm.idivq(rhs);

    masm.bind(&done);
}

void
CodeGeneratorX64::visitUDivOrMod64(LUDivOrMod64* lir)
{
    Register lhs = ToRegister(lir->lhs());
    Register rhs = ToRegister(lir->rhs());
    Register output = ToRegister(lir->output());

    MOZ_ASSERT_IF(lhs != rhs, rhs != rax);
    MOZ_ASSERT(rhs != rdx);
    MOZ_ASSERT_IF(output == rax, ToRegister(lir->remainder()) == rdx);
    MOZ_ASSERT_IF(output == rdx, ToRegister(lir->remainder()) == rax);

    // Put the lhs in rax.
    if (lhs != rax)
        masm.mov(lhs, rax);

    Label done;

    // Prevent divide by zero. For now match asm.js and return 0, but
    // eventually this should trap.
    if (lir->canBeDivideByZero()) {
        Label nonZero;
        masm.branchTestPtr(Assembler::NonZero, rhs, rhs, &nonZero);
        masm.xorl(output, output);
        masm.jump(&done);
        masm.bind(&nonZero);
    }

    // Zero extend the lhs into rdx to make (rdx:rax).
    masm.xorl(rdx, rdx);
    masm.udivq(rhs);

    masm.bind(&done);
}

void
CodeGeneratorX64::visitAsmSelectI64(LAsmSelectI64* lir)
{
    MOZ_ASSERT(lir->mir()->type() == MIRType::Int64);

    Register cond = ToRegister(lir->condExpr());
    Operand falseExpr = ToOperand(lir->falseExpr());

    Register out = ToRegister(lir->output());
    MOZ_ASSERT(ToRegister(lir->trueExpr()) == out, "true expr is reused for input");

    masm.test32(cond, cond);
    masm.cmovzq(falseExpr, out);
}

void
CodeGeneratorX64::visitAsmReinterpretFromI64(LAsmReinterpretFromI64* lir)
{
    MOZ_ASSERT(lir->mir()->type() == MIRType::Double);
    MOZ_ASSERT(lir->mir()->input()->type() == MIRType::Int64);
    masm.vmovq(ToRegister(lir->input()), ToFloatRegister(lir->output()));
}

void
CodeGeneratorX64::visitAsmReinterpretToI64(LAsmReinterpretToI64* lir)
{
    MOZ_ASSERT(lir->mir()->type() == MIRType::Int64);
    MOZ_ASSERT(lir->mir()->input()->type() == MIRType::Double);
    masm.vmovq(ToFloatRegister(lir->input()), ToRegister(lir->output()));
}

void
CodeGeneratorX64::visitAsmJSUInt32ToDouble(LAsmJSUInt32ToDouble* lir)
{
    masm.convertUInt32ToDouble(ToRegister(lir->input()), ToFloatRegister(lir->output()));
}

void
CodeGeneratorX64::visitAsmJSUInt32ToFloat32(LAsmJSUInt32ToFloat32* lir)
{
    masm.convertUInt32ToFloat32(ToRegister(lir->input()), ToFloatRegister(lir->output()));
}

void
CodeGeneratorX64::visitLoadTypedArrayElementStatic(LLoadTypedArrayElementStatic* ins)
{
    MOZ_CRASH("NYI");
}

void
CodeGeneratorX64::visitStoreTypedArrayElementStatic(LStoreTypedArrayElementStatic* ins)
{
    MOZ_CRASH("NYI");
}

void
CodeGeneratorX64::visitAsmJSCall(LAsmJSCall* ins)
{
    emitAsmJSCall(ins);
}

void
CodeGeneratorX64::memoryBarrier(MemoryBarrierBits barrier)
{
    if (barrier & MembarStoreLoad)
        masm.storeLoadFence();
}

void
CodeGeneratorX64::loadSimd(Scalar::Type type, unsigned numElems, const Operand& srcAddr,
                           FloatRegister out)
{
    switch (type) {
      case Scalar::Float32x4: {
        switch (numElems) {
          // In memory-to-register mode, movss zeroes out the high lanes.
          case 1: masm.loadFloat32(srcAddr, out); break;
          // See comment above, which also applies to movsd.
          case 2: masm.loadDouble(srcAddr, out); break;
          case 4: masm.loadUnalignedFloat32x4(srcAddr, out); break;
          default: MOZ_CRASH("unexpected size for partial load");
        }
        break;
      }
      case Scalar::Int32x4: {
        switch (numElems) {
          // In memory-to-register mode, movd zeroes out the high lanes.
          case 1: masm.vmovd(srcAddr, out); break;
          // See comment above, which also applies to movq.
          case 2: masm.vmovq(srcAddr, out); break;
          case 4: masm.loadUnalignedInt32x4(srcAddr, out); break;
          default: MOZ_CRASH("unexpected size for partial load");
        }
        break;
      }
      case Scalar::Int8:
      case Scalar::Uint8:
      case Scalar::Int16:
      case Scalar::Uint16:
      case Scalar::Int32:
      case Scalar::Uint32:
      case Scalar::Float32:
      case Scalar::Float64:
      case Scalar::Uint8Clamped:
      case Scalar::MaxTypedArrayViewType:
        MOZ_CRASH("should only handle SIMD types");
    }
}

void
CodeGeneratorX64::emitSimdLoad(LAsmJSLoadHeap* ins)
{
    const MAsmJSLoadHeap* mir = ins->mir();
    Scalar::Type type = mir->accessType();
    FloatRegister out = ToFloatRegister(ins->output());
    const LAllocation* ptr = ins->ptr();
    Operand srcAddr = ptr->isBogus()
                      ? Operand(HeapReg, mir->offset())
                      : Operand(HeapReg, ToRegister(ptr), TimesOne, mir->offset());

    uint32_t maybeCmpOffset = maybeEmitThrowingAsmJSBoundsCheck(mir, mir, ptr);

    unsigned numElems = mir->numSimdElems();
    if (numElems == 3) {
        MOZ_ASSERT(type == Scalar::Int32x4 || type == Scalar::Float32x4);

        Operand srcAddrZ =
            ptr->isBogus()
            ? Operand(HeapReg, 2 * sizeof(float) + mir->offset())
            : Operand(HeapReg, ToRegister(ptr), TimesOne, 2 * sizeof(float) + mir->offset());

        // Load XY
        uint32_t before = masm.size();
        loadSimd(type, 2, srcAddr, out);
        uint32_t after = masm.size();
        verifyHeapAccessDisassembly(before, after, /*isLoad=*/true, type, 2, srcAddr,
                                    *ins->output()->output());
        masm.append(wasm::HeapAccess(before, wasm::HeapAccess::Throw, maybeCmpOffset));

        // Load Z (W is zeroed)
        // This is still in bounds, as we've checked with a manual bounds check
        // or we had enough space for sure when removing the bounds check.
        before = after;
        loadSimd(type, 1, srcAddrZ, ScratchSimd128Reg);
        after = masm.size();
        verifyHeapAccessDisassembly(before, after, /*isLoad=*/true, type, 1, srcAddrZ,
                                    LFloatReg(ScratchSimd128Reg));
        masm.append(wasm::HeapAccess(before, wasm::HeapAccess::Throw,
                                     wasm::HeapAccess::NoLengthCheck, 8));

        // Move ZW atop XY
        masm.vmovlhps(ScratchSimd128Reg, out, out);
    } else {
        uint32_t before = masm.size();
        loadSimd(type, numElems, srcAddr, out);
        uint32_t after = masm.size();
        verifyHeapAccessDisassembly(before, after, /*isLoad=*/true, type, numElems, srcAddr, *ins->output()->output());
        masm.append(wasm::HeapAccess(before, wasm::HeapAccess::Throw, maybeCmpOffset));
    }

    if (maybeCmpOffset != wasm::HeapAccess::NoLengthCheck)
        cleanupAfterAsmJSBoundsCheckBranch(mir, ToRegister(ptr));
}

void
CodeGeneratorX64::visitAsmJSLoadHeap(LAsmJSLoadHeap* ins)
{
    const MAsmJSLoadHeap* mir = ins->mir();
    Scalar::Type accessType = mir->accessType();

    if (Scalar::isSimdType(accessType))
        return emitSimdLoad(ins);

    const LAllocation* ptr = ins->ptr();
    const LDefinition* out = ins->output();
    Operand srcAddr = ptr->isBogus()
                      ? Operand(HeapReg, mir->offset())
                      : Operand(HeapReg, ToRegister(ptr), TimesOne, mir->offset());

    memoryBarrier(mir->barrierBefore());

    OutOfLineLoadTypedArrayOutOfBounds* ool;
    uint32_t maybeCmpOffset = maybeEmitAsmJSLoadBoundsCheck(mir, ins, &ool);

    uint32_t before = masm.size();
    switch (accessType) {
      case Scalar::Int8:      masm.movsbl(srcAddr, ToRegister(out)); break;
      case Scalar::Uint8:     masm.movzbl(srcAddr, ToRegister(out)); break;
      case Scalar::Int16:     masm.movswl(srcAddr, ToRegister(out)); break;
      case Scalar::Uint16:    masm.movzwl(srcAddr, ToRegister(out)); break;
      case Scalar::Int32:
      case Scalar::Uint32:    masm.movl(srcAddr, ToRegister(out)); break;
      case Scalar::Float32:   masm.loadFloat32(srcAddr, ToFloatRegister(out)); break;
      case Scalar::Float64:   masm.loadDouble(srcAddr, ToFloatRegister(out)); break;
      case Scalar::Float32x4:
      case Scalar::Int32x4:   MOZ_CRASH("SIMD loads should be handled in emitSimdLoad");
      case Scalar::Uint8Clamped:
      case Scalar::MaxTypedArrayViewType:
          MOZ_CRASH("unexpected array type");
    }
    uint32_t after = masm.size();

    verifyHeapAccessDisassembly(before, after, /*isLoad=*/true, accessType, 0, srcAddr, *out->output());

    if (ool) {
        cleanupAfterAsmJSBoundsCheckBranch(mir, ToRegister(ptr));
        masm.bind(ool->rejoin());
    }

    memoryBarrier(mir->barrierAfter());

    masm.append(wasm::HeapAccess(before, wasm::HeapAccess::CarryOn, maybeCmpOffset));
}

void
CodeGeneratorX64::storeSimd(Scalar::Type type, unsigned numElems, FloatRegister in,
                            const Operand& dstAddr)
{
    switch (type) {
      case Scalar::Float32x4: {
        switch (numElems) {
          // In memory-to-register mode, movss zeroes out the high lanes.
          case 1: masm.storeFloat32(in, dstAddr); break;
          // See comment above, which also applies to movsd.
          case 2: masm.storeDouble(in, dstAddr); break;
          case 4: masm.storeUnalignedFloat32x4(in, dstAddr); break;
          default: MOZ_CRASH("unexpected size for partial load");
        }
        break;
      }
      case Scalar::Int32x4: {
        switch (numElems) {
          // In memory-to-register mode, movd zeroes out the high lanes.
          case 1: masm.vmovd(in, dstAddr); break;
          // See comment above, which also applies to movq.
          case 2: masm.vmovq(in, dstAddr); break;
          case 4: masm.storeUnalignedInt32x4(in, dstAddr); break;
          default: MOZ_CRASH("unexpected size for partial load");
        }
        break;
      }
      case Scalar::Int8:
      case Scalar::Uint8:
      case Scalar::Int16:
      case Scalar::Uint16:
      case Scalar::Int32:
      case Scalar::Uint32:
      case Scalar::Float32:
      case Scalar::Float64:
      case Scalar::Uint8Clamped:
      case Scalar::MaxTypedArrayViewType:
        MOZ_CRASH("should only handle SIMD types");
    }
}

void
CodeGeneratorX64::emitSimdStore(LAsmJSStoreHeap* ins)
{
    const MAsmJSStoreHeap* mir = ins->mir();
    Scalar::Type type = mir->accessType();
    FloatRegister in = ToFloatRegister(ins->value());
    const LAllocation* ptr = ins->ptr();
    Operand dstAddr = ptr->isBogus()
                      ? Operand(HeapReg, mir->offset())
                      : Operand(HeapReg, ToRegister(ptr), TimesOne, mir->offset());

    uint32_t maybeCmpOffset = maybeEmitThrowingAsmJSBoundsCheck(mir, mir, ptr);

    unsigned numElems = mir->numSimdElems();
    if (numElems == 3) {
        MOZ_ASSERT(type == Scalar::Int32x4 || type == Scalar::Float32x4);

        Operand dstAddrZ =
            ptr->isBogus()
            ? Operand(HeapReg, 2 * sizeof(float) + mir->offset())
            : Operand(HeapReg, ToRegister(ptr), TimesOne, 2 * sizeof(float) + mir->offset());

        // It's possible that the Z could be out of bounds when the XY is in
        // bounds. To avoid storing the XY before the exception is thrown, we
        // store the Z first, and record its offset in the HeapAccess so
        // that the signal handler knows to check the bounds of the full
        // access, rather than just the Z.
        masm.vmovhlps(in, ScratchSimd128Reg, ScratchSimd128Reg);
        uint32_t before = masm.size();
        storeSimd(type, 1, ScratchSimd128Reg, dstAddrZ);
        uint32_t after = masm.size();
        verifyHeapAccessDisassembly(before, after, /*isLoad=*/false, type, 1, dstAddrZ,
                                    LFloatReg(ScratchSimd128Reg));
        masm.append(wasm::HeapAccess(before, wasm::HeapAccess::Throw, maybeCmpOffset, 8));

        // Store XY
        before = after;
        storeSimd(type, 2, in, dstAddr);
        after = masm.size();
        verifyHeapAccessDisassembly(before, after, /*isLoad=*/false, type, 2, dstAddr, *ins->value());
        masm.append(wasm::HeapAccess(before, wasm::HeapAccess::Throw));
    } else {
        uint32_t before = masm.size();
        storeSimd(type, numElems, in, dstAddr);
        uint32_t after = masm.size();
        verifyHeapAccessDisassembly(before, after, /*isLoad=*/false, type, numElems, dstAddr, *ins->value());
        masm.append(wasm::HeapAccess(before, wasm::HeapAccess::Throw, maybeCmpOffset));
    }

    if (maybeCmpOffset != wasm::HeapAccess::NoLengthCheck)
        cleanupAfterAsmJSBoundsCheckBranch(mir, ToRegister(ptr));
}

void
CodeGeneratorX64::visitAsmJSStoreHeap(LAsmJSStoreHeap* ins)
{
    const MAsmJSStoreHeap* mir = ins->mir();
    Scalar::Type accessType = mir->accessType();

    if (Scalar::isSimdType(accessType))
        return emitSimdStore(ins);

    const LAllocation* value = ins->value();
    const LAllocation* ptr = ins->ptr();
    Operand dstAddr = ptr->isBogus()
                      ? Operand(HeapReg, mir->offset())
                      : Operand(HeapReg, ToRegister(ptr), TimesOne, mir->offset());

    memoryBarrier(mir->barrierBefore());

    Label* rejoin;
    uint32_t maybeCmpOffset = maybeEmitAsmJSStoreBoundsCheck(mir, ins, &rejoin);

    uint32_t before = masm.size();
    if (value->isConstant()) {
        switch (accessType) {
          case Scalar::Int8:
          case Scalar::Uint8:        masm.movb(Imm32(ToInt32(value)), dstAddr); break;
          case Scalar::Int16:
          case Scalar::Uint16:       masm.movw(Imm32(ToInt32(value)), dstAddr); break;
          case Scalar::Int32:
          case Scalar::Uint32:       masm.movl(Imm32(ToInt32(value)), dstAddr); break;
          case Scalar::Float32:
          case Scalar::Float64:
          case Scalar::Float32x4:
          case Scalar::Int32x4:
          case Scalar::Uint8Clamped:
          case Scalar::MaxTypedArrayViewType:
              MOZ_CRASH("unexpected array type");
        }
    } else {
        switch (accessType) {
          case Scalar::Int8:
          case Scalar::Uint8:        masm.movb(ToRegister(value), dstAddr); break;
          case Scalar::Int16:
          case Scalar::Uint16:       masm.movw(ToRegister(value), dstAddr); break;
          case Scalar::Int32:
          case Scalar::Uint32:       masm.movl(ToRegister(value), dstAddr); break;
          case Scalar::Float32:      masm.storeFloat32(ToFloatRegister(value), dstAddr); break;
          case Scalar::Float64:      masm.storeDouble(ToFloatRegister(value), dstAddr); break;
          case Scalar::Float32x4:
          case Scalar::Int32x4:      MOZ_CRASH("SIMD stores must be handled in emitSimdStore");
          case Scalar::Uint8Clamped:
          case Scalar::MaxTypedArrayViewType:
              MOZ_CRASH("unexpected array type");
        }
    }
    uint32_t after = masm.size();

    verifyHeapAccessDisassembly(before, after, /*isLoad=*/false, accessType, 0, dstAddr, *value);

    if (rejoin) {
        cleanupAfterAsmJSBoundsCheckBranch(mir, ToRegister(ptr));
        masm.bind(rejoin);
    }

    memoryBarrier(mir->barrierAfter());

    masm.append(wasm::HeapAccess(before, wasm::HeapAccess::CarryOn, maybeCmpOffset));
}

void
CodeGeneratorX64::visitAsmJSCompareExchangeHeap(LAsmJSCompareExchangeHeap* ins)
{
    MAsmJSCompareExchangeHeap* mir = ins->mir();
    Scalar::Type accessType = mir->accessType();
    const LAllocation* ptr = ins->ptr();

    MOZ_ASSERT(ins->addrTemp()->isBogusTemp());
    MOZ_ASSERT(ptr->isRegister());
    BaseIndex srcAddr(HeapReg, ToRegister(ptr), TimesOne, mir->offset());

    Register oldval = ToRegister(ins->oldValue());
    Register newval = ToRegister(ins->newValue());

    // Note that we can't use the same machinery as normal asm.js loads/stores
    // since signal-handler bounds checking is not yet implemented for atomic accesses.
    uint32_t maybeCmpOffset = wasm::HeapAccess::NoLengthCheck;
    if (mir->needsBoundsCheck()) {
        maybeCmpOffset = masm.cmp32WithPatch(ToRegister(ptr), Imm32(-mir->endOffset())).offset();
        masm.j(Assembler::Above, wasm::JumpTarget::OutOfBounds);
    }
    uint32_t before = masm.size();
    masm.compareExchangeToTypedIntArray(accessType == Scalar::Uint32 ? Scalar::Int32 : accessType,
                                        srcAddr,
                                        oldval,
                                        newval,
                                        InvalidReg,
                                        ToAnyRegister(ins->output()));
    MOZ_ASSERT(mir->offset() == 0,
               "The AsmJS signal handler doesn't yet support emulating "
               "atomic accesses in the case of a fault from an unwrapped offset");
    masm.append(wasm::HeapAccess(before, wasm::HeapAccess::Throw, maybeCmpOffset));
}

void
CodeGeneratorX64::visitAsmJSAtomicExchangeHeap(LAsmJSAtomicExchangeHeap* ins)
{
    MAsmJSAtomicExchangeHeap* mir = ins->mir();
    Scalar::Type accessType = mir->accessType();
    const LAllocation* ptr = ins->ptr();

    MOZ_ASSERT(ins->addrTemp()->isBogusTemp());
    MOZ_ASSERT(ptr->isRegister());
    MOZ_ASSERT(accessType <= Scalar::Uint32);

    BaseIndex srcAddr(HeapReg, ToRegister(ptr), TimesOne, mir->offset());
    Register value = ToRegister(ins->value());

    // Note that we can't use the same machinery as normal asm.js loads/stores
    // since signal-handler bounds checking is not yet implemented for atomic accesses.
    uint32_t maybeCmpOffset = wasm::HeapAccess::NoLengthCheck;
    if (mir->needsBoundsCheck()) {
        maybeCmpOffset = masm.cmp32WithPatch(ToRegister(ptr), Imm32(-mir->endOffset())).offset();
        masm.j(Assembler::Above, wasm::JumpTarget::OutOfBounds);
    }
    uint32_t before = masm.size();
    masm.atomicExchangeToTypedIntArray(accessType == Scalar::Uint32 ? Scalar::Int32 : accessType,
                                       srcAddr,
                                       value,
                                       InvalidReg,
                                       ToAnyRegister(ins->output()));
    MOZ_ASSERT(mir->offset() == 0,
               "The AsmJS signal handler doesn't yet support emulating "
               "atomic accesses in the case of a fault from an unwrapped offset");
    masm.append(wasm::HeapAccess(before, wasm::HeapAccess::Throw, maybeCmpOffset));
}

void
CodeGeneratorX64::visitAsmJSAtomicBinopHeap(LAsmJSAtomicBinopHeap* ins)
{
    MOZ_ASSERT(ins->mir()->hasUses());
    MOZ_ASSERT(ins->addrTemp()->isBogusTemp());

    MAsmJSAtomicBinopHeap* mir = ins->mir();
    Scalar::Type accessType = mir->accessType();
    Register ptrReg = ToRegister(ins->ptr());
    Register temp = ins->temp()->isBogusTemp() ? InvalidReg : ToRegister(ins->temp());
    const LAllocation* value = ins->value();
    AtomicOp op = mir->operation();

    BaseIndex srcAddr(HeapReg, ptrReg, TimesOne, mir->offset());

    // Note that we can't use the same machinery as normal asm.js loads/stores
    // since signal-handler bounds checking is not yet implemented for atomic accesses.
    uint32_t maybeCmpOffset = wasm::HeapAccess::NoLengthCheck;
    if (mir->needsBoundsCheck()) {
        maybeCmpOffset = masm.cmp32WithPatch(ptrReg, Imm32(-mir->endOffset())).offset();
        masm.j(Assembler::Above, wasm::JumpTarget::OutOfBounds);
    }
    uint32_t before = masm.size();
    if (value->isConstant()) {
        atomicBinopToTypedIntArray(op, accessType == Scalar::Uint32 ? Scalar::Int32 : accessType,
                                   Imm32(ToInt32(value)),
                                   srcAddr,
                                   temp,
                                   InvalidReg,
                                   ToAnyRegister(ins->output()));
    } else {
        atomicBinopToTypedIntArray(op, accessType == Scalar::Uint32 ? Scalar::Int32 : accessType,
                                   ToRegister(value),
                                   srcAddr,
                                   temp,
                                   InvalidReg,
                                   ToAnyRegister(ins->output()));
    }
    MOZ_ASSERT(mir->offset() == 0,
               "The AsmJS signal handler doesn't yet support emulating "
               "atomic accesses in the case of a fault from an unwrapped offset");
    masm.append(wasm::HeapAccess(before, wasm::HeapAccess::Throw, maybeCmpOffset));
}

void
CodeGeneratorX64::visitAsmJSAtomicBinopHeapForEffect(LAsmJSAtomicBinopHeapForEffect* ins)
{
    MOZ_ASSERT(!ins->mir()->hasUses());
    MOZ_ASSERT(ins->addrTemp()->isBogusTemp());

    MAsmJSAtomicBinopHeap* mir = ins->mir();
    Scalar::Type accessType = mir->accessType();
    Register ptrReg = ToRegister(ins->ptr());
    const LAllocation* value = ins->value();
    AtomicOp op = mir->operation();

    BaseIndex srcAddr(HeapReg, ptrReg, TimesOne, mir->offset());

    // Note that we can't use the same machinery as normal asm.js loads/stores
    // since signal-handler bounds checking is not yet implemented for atomic accesses.
    uint32_t maybeCmpOffset = wasm::HeapAccess::NoLengthCheck;
    if (mir->needsBoundsCheck()) {
        maybeCmpOffset = masm.cmp32WithPatch(ptrReg, Imm32(-mir->endOffset())).offset();
        masm.j(Assembler::Above, wasm::JumpTarget::OutOfBounds);
    }

    uint32_t before = masm.size();
    if (value->isConstant())
        atomicBinopToTypedIntArray(op, accessType, Imm32(ToInt32(value)), srcAddr);
    else
        atomicBinopToTypedIntArray(op, accessType, ToRegister(value), srcAddr);
    MOZ_ASSERT(mir->offset() == 0,
               "The AsmJS signal handler doesn't yet support emulating "
               "atomic accesses in the case of a fault from an unwrapped offset");
    masm.append(wasm::HeapAccess(before, wasm::HeapAccess::Throw, maybeCmpOffset));
}

void
CodeGeneratorX64::visitAsmJSLoadGlobalVar(LAsmJSLoadGlobalVar* ins)
{
    MAsmJSLoadGlobalVar* mir = ins->mir();

    MIRType type = mir->type();
    MOZ_ASSERT(IsNumberType(type) || IsSimdType(type));

    CodeOffset label;
    switch (type) {
      case MIRType::Int32:
        label = masm.loadRipRelativeInt32(ToRegister(ins->output()));
        break;
      case MIRType::Float32:
        label = masm.loadRipRelativeFloat32(ToFloatRegister(ins->output()));
        break;
      case MIRType::Double:
        label = masm.loadRipRelativeDouble(ToFloatRegister(ins->output()));
        break;
      // Aligned access: code is aligned on PageSize + there is padding
      // before the global data section.
      case MIRType::Int32x4:
      case MIRType::Bool32x4:
        label = masm.loadRipRelativeInt32x4(ToFloatRegister(ins->output()));
        break;
      case MIRType::Float32x4:
        label = masm.loadRipRelativeFloat32x4(ToFloatRegister(ins->output()));
        break;
      default:
        MOZ_CRASH("unexpected type in visitAsmJSLoadGlobalVar");
    }

    masm.append(AsmJSGlobalAccess(label, mir->globalDataOffset()));
}

void
CodeGeneratorX64::visitAsmJSStoreGlobalVar(LAsmJSStoreGlobalVar* ins)
{
    MAsmJSStoreGlobalVar* mir = ins->mir();

    MIRType type = mir->value()->type();
    MOZ_ASSERT(IsNumberType(type) || IsSimdType(type));

    CodeOffset label;
    switch (type) {
      case MIRType::Int32:
        label = masm.storeRipRelativeInt32(ToRegister(ins->value()));
        break;
      case MIRType::Float32:
        label = masm.storeRipRelativeFloat32(ToFloatRegister(ins->value()));
        break;
      case MIRType::Double:
        label = masm.storeRipRelativeDouble(ToFloatRegister(ins->value()));
        break;
      // Aligned access: code is aligned on PageSize + there is padding
      // before the global data section.
      case MIRType::Int32x4:
      case MIRType::Bool32x4:
        label = masm.storeRipRelativeInt32x4(ToFloatRegister(ins->value()));
        break;
      case MIRType::Float32x4:
        label = masm.storeRipRelativeFloat32x4(ToFloatRegister(ins->value()));
        break;
      default:
        MOZ_CRASH("unexpected type in visitAsmJSStoreGlobalVar");
    }

    masm.append(AsmJSGlobalAccess(label, mir->globalDataOffset()));
}

void
CodeGeneratorX64::visitAsmJSLoadFuncPtr(LAsmJSLoadFuncPtr* ins)
{
    MAsmJSLoadFuncPtr* mir = ins->mir();

    Register index = ToRegister(ins->index());
    Register tmp = ToRegister(ins->temp());
    Register out = ToRegister(ins->output());

    if (mir->hasLimit()) {
        masm.branch32(Assembler::Condition::AboveOrEqual, index, Imm32(mir->limit()),
                      wasm::JumpTarget::OutOfBounds);
    }

    if (mir->alwaysThrow()) {
        masm.jump(wasm::JumpTarget::BadIndirectCall);
    } else {
        CodeOffset label = masm.leaRipRelative(tmp);
        masm.loadPtr(Operand(tmp, index, TimesEight, 0), out);
        masm.append(AsmJSGlobalAccess(label, mir->globalDataOffset()));
    }
}

void
CodeGeneratorX64::visitAsmJSLoadFFIFunc(LAsmJSLoadFFIFunc* ins)
{
    MAsmJSLoadFFIFunc* mir = ins->mir();

    CodeOffset label = masm.loadRipRelativeInt64(ToRegister(ins->output()));
    masm.append(AsmJSGlobalAccess(label, mir->globalDataOffset()));
}

void
CodeGeneratorX64::visitTruncateDToInt32(LTruncateDToInt32* ins)
{
    FloatRegister input = ToFloatRegister(ins->input());
    Register output = ToRegister(ins->output());

    // On x64, branchTruncateDouble uses vcvttsd2sq. Unlike the x86
    // implementation, this should handle most doubles and we can just
    // call a stub if it fails.
    emitTruncateDouble(input, output, ins->mir());
}

void
CodeGeneratorX64::visitTruncateFToInt32(LTruncateFToInt32* ins)
{
    FloatRegister input = ToFloatRegister(ins->input());
    Register output = ToRegister(ins->output());

    // On x64, branchTruncateFloat32 uses vcvttss2sq. Unlike the x86
    // implementation, this should handle most floats and we can just
    // call a stub if it fails.
    emitTruncateFloat32(input, output, ins->mir());
}

void
CodeGeneratorX64::visitWrapInt64ToInt32(LWrapInt64ToInt32* lir)
{
    const LAllocation* input = lir->getOperand(0);
    Register output = ToRegister(lir->output());

    masm.movl(ToOperand(input), output);
}

void
CodeGeneratorX64::visitExtendInt32ToInt64(LExtendInt32ToInt64* lir)
{
    const LAllocation* input = lir->getOperand(0);
    Register output = ToRegister(lir->output());

    if (lir->mir()->isUnsigned())
        masm.movl(ToOperand(input), output);
    else
        masm.movslq(ToOperand(input), output);
}

void
CodeGeneratorX64::visitWasmTruncateToInt64(LWasmTruncateToInt64* lir)
{
    FloatRegister input = ToFloatRegister(lir->input());
    Register output = ToRegister(lir->output());

    MWasmTruncateToInt64* mir = lir->mir();
    MIRType inputType = mir->input()->type();

    auto* ool = new(alloc()) OutOfLineWasmTruncateCheck(mir, input);
    addOutOfLineCode(ool, mir);

    if (mir->isUnsigned()) {
        FloatRegister tempDouble = ToFloatRegister(lir->temp());

        // If the input < INT64_MAX, vcvttsd2sq will do the right thing, so
        // we use it directly. Else, we subtract INT64_MAX, convert to int64,
        // and then add INT64_MAX to the result.
        if (inputType == MIRType::Double) {
            Label isLarge;
            masm.loadConstantDouble(double(0x8000000000000000), ScratchDoubleReg);
            masm.branchDouble(Assembler::DoubleGreaterThanOrEqual, input, ScratchDoubleReg, &isLarge);
            masm.vcvttsd2sq(input, output);
            masm.branchTestPtr(Assembler::Signed, output, output, ool->entry());
            masm.jump(ool->rejoin());

            masm.bind(&isLarge);
            masm.moveDouble(input, tempDouble);
            masm.subDouble(ScratchDoubleReg, tempDouble);
            masm.vcvttsd2sq(tempDouble, output);
            masm.branchTestPtr(Assembler::Signed, output, output, ool->entry());
            masm.or64(Imm64(0x8000000000000000), Register64(output));
        } else {
            MOZ_ASSERT(inputType == MIRType::Float32);

            Label isLarge;
            masm.loadConstantFloat32(float(0x8000000000000000), ScratchDoubleReg);
            masm.branchFloat(Assembler::DoubleGreaterThanOrEqual, input, ScratchDoubleReg, &isLarge);
            masm.vcvttss2sq(input, output);
            masm.branchTestPtr(Assembler::Signed, output, output, ool->entry());
            masm.jump(ool->rejoin());

            masm.bind(&isLarge);
            masm.moveFloat32(input, tempDouble);
            masm.vsubss(ScratchDoubleReg, tempDouble, tempDouble);
            masm.vcvttss2sq(tempDouble, output);
            masm.branchTestPtr(Assembler::Signed, output, output, ool->entry());
            masm.or64(Imm64(0x8000000000000000), Register64(output));
        }
    } else {
        if (inputType == MIRType::Double) {
            masm.vcvttsd2sq(input, output);
            masm.cmpq(Imm32(1), output);
            masm.j(Assembler::Overflow, ool->entry());
        } else {
            MOZ_ASSERT(inputType == MIRType::Float32);
            masm.vcvttss2sq(input, output);
            masm.cmpq(Imm32(1), output);
            masm.j(Assembler::Overflow, ool->entry());
        }
    }

    masm.bind(ool->rejoin());
}

void
CodeGeneratorX64::visitWasmTruncateToInt32(LWasmTruncateToInt32* lir)
{
    auto input = ToFloatRegister(lir->input());
    auto output = ToRegister(lir->output());

    MWasmTruncateToInt32* mir = lir->mir();
    MIRType fromType = mir->input()->type();

    auto* ool = new (alloc()) OutOfLineWasmTruncateCheck(mir, input);
    addOutOfLineCode(ool, mir);

    if (mir->isUnsigned()) {
        if (fromType == MIRType::Double)
            masm.vcvttsd2sq(input, output);
        else if (fromType == MIRType::Float32)
            masm.vcvttss2sq(input, output);
        else
            MOZ_CRASH("unexpected type in visitWasmTruncateToInt32");

        // Check that the result is in the uint32_t range.
        ScratchRegisterScope scratch(masm);
        masm.move32(Imm32(0xffffffff), scratch);
        masm.cmpq(scratch, output);
        masm.j(Assembler::Above, ool->entry());
        return;
    }

    emitWasmSignedTruncateToInt32(ool, output);

    masm.bind(ool->rejoin());
}

void
CodeGeneratorX64::visitInt64ToFloatingPoint(LInt64ToFloatingPoint* lir)
{
    Register input = ToRegister(lir->input());
    FloatRegister output = ToFloatRegister(lir->output());

    MIRType outputType = lir->mir()->type();
    MOZ_ASSERT(outputType == MIRType::Double || outputType == MIRType::Float32);

    // Zero the output register to break dependencies, see convertInt32ToDouble.
    if (outputType == MIRType::Double)
        masm.zeroDouble(output);
    else
        masm.zeroFloat32(output);

    Label done;
    if (lir->mir()->isUnsigned()) {
        // If the input is unsigned, we use cvtsq2sd or vcvtsq2ss directly.
        // Else, we divide by 2, convert to double or float, and multiply the
        // result by 2.
        if (outputType == MIRType::Double) {
            Label isSigned;
            masm.branchTestPtr(Assembler::Signed, input, input, &isSigned);
            masm.vcvtsq2sd(input, output, output);
            masm.jump(&done);

            masm.bind(&isSigned);
            ScratchRegisterScope scratch(masm);
            masm.mov(input, scratch);
            masm.rshiftPtr(Imm32(1), scratch);
            masm.vcvtsq2sd(scratch, output, output);
            masm.vaddsd(output, output, output);
        } else {
            Label isSigned;
            masm.branchTestPtr(Assembler::Signed, input, input, &isSigned);
            masm.vcvtsq2ss(input, output, output);
            masm.jump(&done);

            masm.bind(&isSigned);
            ScratchRegisterScope scratch(masm);
            masm.mov(input, scratch);
            masm.rshiftPtr(Imm32(1), scratch);
            masm.vcvtsq2ss(scratch, output, output);
            masm.vaddss(output, output, output);
        }
    } else {
        if (outputType == MIRType::Double)
            masm.vcvtsq2sd(input, output, output);
        else
            masm.vcvtsq2ss(input, output, output);
    }
    masm.bind(&done);
}

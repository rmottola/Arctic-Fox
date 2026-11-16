/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jsiter.h"

#include "jit/BaselineJIT.h"
#include "jit/BaselineIC.h"
#include "jit/BaselineCompiler.h"
#include "jit/Linker.h"
#include "jit/SharedICHelpers.h"
#include "jit/MacroAssembler-inl.h"

#include "jsboolinlines.h"

using namespace js;
using namespace js::jit;

#define PPC_BC(x,y) BufferOffset bo_##y = masm._bc(0, x)
#define PPC_B(y) BufferOffset bo_##y = masm._b(0)
#define PPC_BB(y) masm.bindSS(bo_##y)
#define LOAD_CTR() {\
	masm.lwz(tempRegister, stackPointerRegister, 0);\
	masm.x_mtctr(tempRegister);\
	masm.addi(stackPointerRegister, stackPointerRegister, 4);\
}

namespace js {
namespace jit {

// ICCompare_Int32

bool
ICCompare_Int32::Compiler::generateStubCode(MacroAssembler &masm)
{
    // Guard that R0 is an integer and R1 is an integer.
    masm.x_li32(r0, JSVAL_TAG_INT32);
    masm.xor_(r12, R0.typeReg(), r0);
    masm.xor_(r0,  R1.typeReg(), r0);
    masm.or__rc(r0, r0, r12); // r0 == R0.typeReg() == R1.typeReg()
    PPC_BC(Assembler::NonZero, failure);

    // Compare payload regs of R0 and R1, moving 1 to R0 if they are the
    // same and 0 if they are not.
    LOAD_CTR();
    Assembler::Condition cond = JSOpToCondition(op, /* signed = */true);
    masm.ma_cmp_set(cond, R0.payloadReg(), R1.payloadReg(), R0.payloadReg());

    // Result is implicitly boxed already.
    masm.tagValue(JSVAL_TYPE_BOOLEAN, R0.payloadReg(), R0);
    masm.bctr();

    // In the failure case, jump to the next stub.
    PPC_BB(failure);
    EmitStubGuardFailure(masm);

    return true;
}

bool
ICCompare_Double::Compiler::generateStubCode(MacroAssembler &masm)
{
    Label failure;
    // Short but not worth it.
    masm.ensureDouble(R0, FloatReg0, &failure);
    masm.ensureDouble(R1, FloatReg1, &failure);

    LOAD_CTR();
    Assembler::DoubleCondition doubleCond = JSOpToDoubleCondition(op);
    masm.ma_cmp_set(doubleCond, FloatReg0, FloatReg1, R0.scratchReg());
    masm.tagValue(JSVAL_TYPE_BOOLEAN, R0.scratchReg(), R0);
    masm.bctr();

    // Failure case - jump to next stub
    masm.bind(&failure);
    EmitStubGuardFailure(masm);
    return true;
}

// ICBinaryArith_Int32

#define PPC_BC_O(x) {\
	masm.x_mcrxr(cr0);\
	bo_##x = masm._bc(0, Assembler::GreaterThan);\
}

bool
ICBinaryArith_Int32::Compiler::generateStubCode(MacroAssembler &masm)
{
    // Guard that R0 is an integer and R1 is an integer.
    masm.x_li32(r0, JSVAL_TAG_INT32);
    masm.xor_(r12, R0.typeReg(), r0);
    masm.xor_(r0,  R1.typeReg(), r0);
    masm.or__rc(r0, r0, r12); // r0 == R0.typeReg() == R1.typeReg()
    PPC_BC(Assembler::NonZero, failure);

    Register scratchReg = R2.payloadReg();

    BufferOffset bo_failure1, bo_failure2, bo_failure3, bo_failure4, bo_failure5,
    	bo_failure6, bo_failure7, bo_failure8, bo_failure9;
    switch(op_) {
      // Because this can overflow, we must use the overflow-enabled math
      // instructions. (Hi, TraceMonkey!)
      // However, we can try to optimize for the non-failure case, if we
      // think that's very likely (probably not for div).
      case JSOP_ADD:
        // Speculatively load CTR, but don't remove it from the stack yet.
        masm.lwz(tempRegister, stackPointerRegister, 0);
        masm.x_mtctr(tempRegister);
        
        masm.addo(scratchReg, R0.payloadReg(), R1.payloadReg());

        // Just jump to failure on overflow.  R0 and R1 are preserved,
        // so we can just jump to the next stub.
        PPC_BC_O(failure1);

        // Box the result and return. We know R0.typeReg() already contains
        // the integer tag, so we just need to move the result value into
        // place.
        masm.addi(stackPointerRegister, stackPointerRegister, 4); // pop return addy
        masm.x_mr(R0.payloadReg(), scratchReg);
        break;
      case JSOP_SUB:
        masm.lwz(tempRegister, stackPointerRegister, 0);
        masm.x_mtctr(tempRegister);

        // Remember that subfo has weird operand order.
        masm.subfo(scratchReg, R1.payloadReg(), R0.payloadReg()); // D=B-A
        PPC_BC_O(failure2);
        
        masm.addi(stackPointerRegister, stackPointerRegister, 4); // pop return addy
        masm.x_mr(R0.payloadReg(), scratchReg);
        break;
      case JSOP_MUL: {
        masm.lwz(tempRegister, stackPointerRegister, 0);
        masm.x_mtctr(tempRegister);

        masm.mullwo(scratchReg, R0.payloadReg(), R1.payloadReg());
        // If zero, it could be -0.
        masm.cmpwi(cr1, scratchReg, 0);
        PPC_BC_O(failure3);
        BufferOffset bo_notNegZero = masm._bc(0, Assembler::NotEqual, cr1, Assembler::LikelyB);
        
        // Zero result is -0 if exactly one of lhs or rhs is negative.
        masm.cmpwi(cr0, R0.payloadReg(), 0);
        masm.cmpwi(cr1, R1.payloadReg(), 0);
        masm.crxor(0, 0, 4); // XOR the LT bits
        bo_failure9 = masm._bc(0, Assembler::LessThan);
        
        PPC_BB(notNegZero);
        masm.addi(stackPointerRegister, stackPointerRegister, 4); // pop return addy
        masm.x_mr(R0.payloadReg(), scratchReg);
        break;
      }
      case JSOP_DIV:
      case JSOP_MOD: {
        // divwo will automatically set the Overflow bit if INT_MIN/-1 is
        // performed, or if we divide by zero. So we only need to check for
        // possible negative zero.

        // Check for 0/x with x<0 (results in -0).
        // Save the result of this compare in cr1; we may use it again.
        // For that reason, do a signed compare.
        masm.cmpwi(cr1, R0.payloadReg(), 0);
        BufferOffset bo_ok = masm._bc(0, Assembler::NotEqual, cr1, Assembler::LikelyB);
        masm.cmpwi(R1.payloadReg(), 0); 
        bo_failure4 = masm._bc(0, Assembler::LessThanOrEqual); // might as well fail now

        // Passed. Continue with the division.
        PPC_BB(ok);
        masm.divwo(scratchReg, R0.payloadReg(), R1.payloadReg());
        PPC_BC_O(failure5);
        
        // We need to check the remainder to know if the result is not
        // integral. If scratch * R1 != R0, then there is a remainder.
        // We know it can't overflow, so mullw is sufficient.
        masm.mullw(r12, scratchReg, R1.payloadReg());
        masm.cmpw(r12, R0.payloadReg());

        if (op_ == JSOP_DIV) {
            // We don't need the actual remainder, just if there is one.
            // Result is a double if the remainder != 0.
            bo_failure6 = masm._bc(0, Assembler::NotEqual);
            LOAD_CTR();
            masm.tagValue(JSVAL_TYPE_INT32, scratchReg, R0);
        } else {
            // If X % Y == 0 and X < 0, the result is -0.
            BufferOffset done = masm._bc(0, Assembler::NotEqual);
            // Re-use the comparison from above.
            bo_failure7 = masm._bc(0, Assembler::LessThan, cr1);
            masm.bindSS(done);
            LOAD_CTR();
            // Now compute the remainder and store it.
            masm.subf(R0.payloadReg(), r12, R0.payloadReg());
        }
        break;
      }
      case JSOP_BITOR:
        LOAD_CTR();
        masm.or_(R0.payloadReg(), R1.payloadReg(), R0.payloadReg());
        break;
      case JSOP_BITXOR:
        LOAD_CTR();
        masm.xor_(R0.payloadReg(), R1.payloadReg(), R0.payloadReg());
        break;
      case JSOP_BITAND:
        LOAD_CTR();
        masm.and_(R0.payloadReg(), R1.payloadReg(), R0.payloadReg());
        break;
      case JSOP_LSH:
        // Some PowerPC implementations may merrily try to shift by
        // more than 0x1f.
        LOAD_CTR();
        masm.andi_rc(r0, R1.payloadReg(), 0x1f);
        masm.slw(R0.payloadReg(), R0.payloadReg(), r0);
        break;
      case JSOP_RSH:
        LOAD_CTR();
        masm.andi_rc(r0, R1.payloadReg(), 0x1f);
        masm.sraw(R0.payloadReg(), R0.payloadReg(), r0);
        break;
      case JSOP_URSH:
        if (allowDouble_) {
            // Load CTR now; this can't fail.
            LOAD_CTR();
        }
        masm.andi_rc(scratchReg, R1.payloadReg(), 0x1f);
        masm.srw(scratchReg, R0.payloadReg(), scratchReg);
        // Check for negative sign. Use a signed compare here.
        masm.cmpwi(scratchReg, 0);
        if (allowDouble_) {
            PPC_BC(Assembler::LessThan, toUint);

            // Move result and box for return.
            masm.x_mr(R0.payloadReg(), scratchReg);
            masm.bctr();

            PPC_BB(toUint);
            masm.convertUInt32ToDouble(scratchReg, ScratchFloat32Reg);
            masm.boxDouble(ScratchFloat32Reg, R0);
        } else {
            bo_failure8 = masm._bc(0, Assembler::LessThan);
            // Move result for return.
            LOAD_CTR();
            masm.x_mr(R0.payloadReg(), scratchReg);
        }
        break;
      default:
        MOZ_CRASH("Unhandled op for BinaryArith_Int32.");
        return false;
    }

    masm.bctr();

    PPC_BB(failure);
    PPC_BB(failure1);
    PPC_BB(failure2);
    PPC_BB(failure3);
    PPC_BB(failure4);
    PPC_BB(failure5);
    PPC_BB(failure6);
    PPC_BB(failure7);
    PPC_BB(failure8);
    PPC_BB(failure9);
    EmitStubGuardFailure(masm);

    return true;
}

bool
ICUnaryArith_Int32::Compiler::generateStubCode(MacroAssembler &masm)
{
    BufferOffset bo_failure2;

    // Guard on int.
    masm.x_li32(r0, JSVAL_TAG_INT32);
    masm.cmplw(R0.typeReg(), r0);
    PPC_BC(Assembler::NotEqual, failure);

    switch (op) {
      case JSOP_BITNOT:
        // We need to xori and xoris both.
        LOAD_CTR();
        masm.xori(R0.payloadReg(), R0.payloadReg(), -1);
        masm.xoris(R0.payloadReg(), R0.payloadReg(), -1);
        break;
      case JSOP_NEG:
        // Guard against 0 and MIN_INT, since both result in a double.
        masm.cmplwi(cr0, R0.payloadReg(), 0);
        masm.x_li32(r0, 0x7fffffff);
        masm.cmplw(cr1, R0.payloadReg(), r0);
        masm.cror(2, 2, 6);
        bo_failure2 = masm._bc(0, Assembler::Equal);

        // Compile -x as 0 - x.
        LOAD_CTR();
        masm.neg(R0.payloadReg(), R0.payloadReg());
        break;
      default:
        MOZ_CRASH("Unexpected op");
        return false;
    }

    masm.bctr();

    PPC_BB(failure);
    PPC_BB(failure2);
    EmitStubGuardFailure(masm);
    return true;
}

} // namespace jit
} // namespace js

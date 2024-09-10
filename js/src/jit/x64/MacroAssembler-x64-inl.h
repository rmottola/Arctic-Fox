/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_x64_MacroAssembler_x64_inl_h
#define jit_x64_MacroAssembler_x64_inl_h

#include "jit/x64/MacroAssembler-x64.h"

#include "jit/x86-shared/MacroAssembler-x86-shared-inl.h"

namespace js {
namespace jit {

//{{{ check_macroassembler_style
// ===============================================================

void
MacroAssembler::move64(Imm64 imm, Register64 dest)
{
    movq(ImmWord(imm.value), dest.reg);
}

void
MacroAssembler::move64(Register64 src, Register64 dest)
{
    movq(src.reg, dest.reg);
}

void
MacroAssembler::andPtr(Register src, Register dest)
{
    andq(src, dest);
}

void
MacroAssembler::andPtr(Imm32 imm, Register dest)
{
    andq(imm, dest);
}

void
MacroAssembler::and64(Imm64 imm, Register64 dest)
{
    if (INT32_MIN <= int64_t(imm.value) && int64_t(imm.value) <= INT32_MAX) {
        andq(Imm32(imm.value), dest.reg);
    } else {
        ScratchRegisterScope scratch(*this);
        movq(ImmWord(uintptr_t(imm.value)), scratch);
        andq(scratch, dest.reg);
    }
}

void
MacroAssembler::or64(Imm64 imm, Register64 dest)
{
    if (INT32_MIN <= int64_t(imm.value) && int64_t(imm.value) <= INT32_MAX) {
        orq(Imm32(imm.value), dest.reg);
    } else {
        ScratchRegisterScope scratch(*this);
        movq(ImmWord(uintptr_t(imm.value)), scratch);
        orq(scratch, dest.reg);
    }
}

void
MacroAssembler::xor64(Imm64 imm, Register64 dest)
{
    if (INT32_MIN <= int64_t(imm.value) && int64_t(imm.value) <= INT32_MAX) {
        xorq(Imm32(imm.value), dest.reg);
    } else {
        ScratchRegisterScope scratch(*this);
        movq(ImmWord(uintptr_t(imm.value)), scratch);
        xorq(scratch, dest.reg);
    }
}

void
MacroAssembler::orPtr(Register src, Register dest)
{
    orq(src, dest);
}

void
MacroAssembler::orPtr(Imm32 imm, Register dest)
{
    orq(imm, dest);
}

void
MacroAssembler::or64(Register64 src, Register64 dest)
{
    orq(src.reg, dest.reg);
}

void
MacroAssembler::xor64(Register64 src, Register64 dest)
{
    xorq(src.reg, dest.reg);
}

void
MacroAssembler::xorPtr(Register src, Register dest)
{
    xorq(src, dest);
}

void
MacroAssembler::xorPtr(Imm32 imm, Register dest)
{
    xorq(imm, dest);
}

// ===============================================================
// Arithmetic functions

void
MacroAssembler::addPtr(Register src, Register dest)
{
    addq(src, dest);
}

void
MacroAssembler::addPtr(Imm32 imm, Register dest)
{
    addq(imm, dest);
}

void
MacroAssembler::addPtr(ImmWord imm, Register dest)
{
    ScratchRegisterScope scratch(*this);
    MOZ_ASSERT(dest != scratch);
    if ((intptr_t)imm.value <= INT32_MAX && (intptr_t)imm.value >= INT32_MIN) {
        addq(Imm32((int32_t)imm.value), dest);
    } else {
        mov(imm, scratch);
        addq(scratch, dest);
    }
}

void
MacroAssembler::addPtr(Imm32 imm, const Address& dest)
{
    addq(imm, Operand(dest));
}

void
MacroAssembler::addPtr(Imm32 imm, const AbsoluteAddress& dest)
{
    addq(imm, Operand(dest));
}

void
MacroAssembler::addPtr(const Address& src, Register dest)
{
    addq(Operand(src), dest);
}

void
MacroAssembler::add64(Register64 src, Register64 dest)
{
    addq(src.reg, dest.reg);
}

void
MacroAssembler::add64(Imm32 imm, Register64 dest)
{
    addq(imm, dest.reg);
}

void
MacroAssembler::subPtr(Register src, Register dest)
{
    subq(src, dest);
}

void
MacroAssembler::subPtr(Register src, const Address& dest)
{
    subq(src, Operand(dest));
}

void
MacroAssembler::subPtr(Imm32 imm, Register dest)
{
    subq(imm, dest);
}

void
MacroAssembler::subPtr(ImmWord imm, Register dest)
{
    ScratchRegisterScope scratch(*this);
    MOZ_ASSERT(dest != scratch);
    if ((intptr_t)imm.value <= INT32_MAX && (intptr_t)imm.value >= INT32_MIN) {
        subq(Imm32((int32_t)imm.value), dest);
    } else {
        mov(imm, scratch);
        subq(scratch, dest);
    }
}

void
MacroAssembler::subPtr(const Address& addr, Register dest)
{
    subq(Operand(addr), dest);
}

void
MacroAssembler::mul64(Imm64 imm, const Register64& dest)
{
    movq(ImmWord(uintptr_t(imm.value)), ScratchReg);
    imulq(ScratchReg, dest.reg);
}

void
MacroAssembler::mulBy3(Register src, Register dest)
{
    lea(Operand(src, src, TimesTwo), dest);
}

void
MacroAssembler::mulDoublePtr(ImmPtr imm, Register temp, FloatRegister dest)
{
    movq(imm, ScratchReg);
    vmulsd(Operand(ScratchReg, 0), dest, dest);
}

void
MacroAssembler::inc64(AbsoluteAddress dest)
{
    if (X86Encoding::IsAddressImmediate(dest.addr)) {
        addPtr(Imm32(1), dest);
    } else {
        ScratchRegisterScope scratch(*this);
        mov(ImmPtr(dest.addr), scratch);
        addPtr(Imm32(1), Address(scratch, 0));
    }
}

// ===============================================================
// Shift functions

void
MacroAssembler::lshiftPtr(Imm32 imm, Register dest)
{
    shlq(imm, dest);
}

void
MacroAssembler::lshift64(Imm32 imm, Register64 dest)
{
    shlq(imm, dest.reg);
}

void
MacroAssembler::rshiftPtr(Imm32 imm, Register dest)
{
    shrq(imm, dest);
}

void
MacroAssembler::rshiftPtrArithmetic(Imm32 imm, Register dest)
{
    sarq(imm, dest);
}

void
MacroAssembler::rshift64(Imm32 imm, Register64 dest)
{
    shrq(imm, dest.reg);
}

// ===============================================================
// Branch functions

void
MacroAssembler::branch32(Condition cond, const AbsoluteAddress& lhs, Register rhs, Label* label)
{
    if (X86Encoding::IsAddressImmediate(lhs.addr)) {
        branch32(cond, Operand(lhs), rhs, label);
    } else {
        ScratchRegisterScope scratch(*this);
        mov(ImmPtr(lhs.addr), scratch);
        branch32(cond, Address(scratch, 0), rhs, label);
    }
}
void
MacroAssembler::branch32(Condition cond, const AbsoluteAddress& lhs, Imm32 rhs, Label* label)
{
    if (X86Encoding::IsAddressImmediate(lhs.addr)) {
        branch32(cond, Operand(lhs), rhs, label);
    } else {
        ScratchRegisterScope scratch(*this);
        mov(ImmPtr(lhs.addr), scratch);
        branch32(cond, Address(scratch, 0), rhs, label);
    }
}

void
MacroAssembler::branch32(Condition cond, wasm::SymbolicAddress lhs, Imm32 rhs, Label* label)
{
    ScratchRegisterScope scratch(*this);
    mov(lhs, scratch);
    branch32(cond, Address(scratch, 0), rhs, label);
}

void
MacroAssembler::branch64(Condition cond, const Address& lhs, Imm64 val, Label* label)
{
    MOZ_ASSERT(cond == Assembler::NotEqual,
               "other condition codes not supported");

    branchPtr(cond, lhs, ImmWord(val.value), label);
}

void
MacroAssembler::branch64(Condition cond, const Address& lhs, const Address& rhs, Register scratch,
                         Label* label)
{
    MOZ_ASSERT(cond == Assembler::NotEqual,
               "other condition codes not supported");
    MOZ_ASSERT(lhs.base != scratch);
    MOZ_ASSERT(rhs.base != scratch);

    loadPtr(rhs, scratch);
    branchPtr(cond, lhs, scratch, label);
}

void
MacroAssembler::branchPtr(Condition cond, const AbsoluteAddress& lhs, Register rhs, Label* label)
{
    ScratchRegisterScope scratch(*this);
    MOZ_ASSERT(rhs != scratch);
    if (X86Encoding::IsAddressImmediate(lhs.addr)) {
        branchPtrImpl(cond, Operand(lhs), rhs, label);
    } else {
        mov(ImmPtr(lhs.addr), scratch);
        branchPtrImpl(cond, Operand(scratch, 0x0), rhs, label);
    }
}

void
MacroAssembler::branchPtr(Condition cond, const AbsoluteAddress& lhs, ImmWord rhs, Label* label)
{
    if (X86Encoding::IsAddressImmediate(lhs.addr)) {
        branchPtrImpl(cond, Operand(lhs), rhs, label);
    } else {
        ScratchRegisterScope scratch(*this);
        mov(ImmPtr(lhs.addr), scratch);
        branchPtrImpl(cond, Operand(scratch, 0x0), rhs, label);
    }
}

void
MacroAssembler::branchPtr(Condition cond, wasm::SymbolicAddress lhs, Register rhs, Label* label)
{
    ScratchRegisterScope scratch(*this);
    MOZ_ASSERT(rhs != scratch);
    mov(lhs, scratch);
    branchPtrImpl(cond, Operand(scratch, 0x0), rhs, label);
}

void
MacroAssembler::branchPrivatePtr(Condition cond, const Address& lhs, Register rhs, Label* label)
{
    ScratchRegisterScope scratch(*this);
    if (rhs != scratch)
        movePtr(rhs, scratch);
    // Instead of unboxing lhs, box rhs and do direct comparison with lhs.
    rshiftPtr(Imm32(1), scratch);
    branchPtr(cond, lhs, scratch, label);
}

void
MacroAssembler::branchTruncateFloat32(FloatRegister src, Register dest, Label* fail)
{
    vcvttss2sq(src, dest);

    // Same trick as for Doubles
    cmpPtr(dest, Imm32(1));
    j(Assembler::Overflow, fail);

    movl(dest, dest); // Zero upper 32-bits.
}

void
MacroAssembler::branchTruncateDouble(FloatRegister src, Register dest, Label* fail)
{
    vcvttsd2sq(src, dest);

    // vcvttsd2sq returns 0x8000000000000000 on failure. Test for it by
    // subtracting 1 and testing overflow (this avoids the need to
    // materialize that value in a register).
    cmpPtr(dest, Imm32(1));
    j(Assembler::Overflow, fail);

    movl(dest, dest); // Zero upper 32-bits.
}

void
MacroAssembler::branchTest32(Condition cond, const AbsoluteAddress& lhs, Imm32 rhs, Label* label)
{
    if (X86Encoding::IsAddressImmediate(lhs.addr)) {
        test32(Operand(lhs), rhs);
    } else {
        ScratchRegisterScope scratch(*this);
        mov(ImmPtr(lhs.addr), scratch);
        test32(Operand(scratch, 0), rhs);
    }
    j(cond, label);
}

void
MacroAssembler::branchTest64(Condition cond, Register64 lhs, Register64 rhs, Register temp,
                             Label* label)
{
    branchTestPtr(cond, lhs.reg, rhs.reg, label);
}

void
MacroAssembler::branchTestBooleanTruthy(bool truthy, const ValueOperand& value, Label* label)
{
    test32(value.valueReg(), value.valueReg());
    j(truthy ? NonZero : Zero, label);
}

void
MacroAssembler::branchTestMagic(Condition cond, const Address& valaddr, JSWhyMagic why, Label* label)
{
    uint64_t magic = MagicValue(why).asRawBits();
    cmpPtr(valaddr, ImmWord(magic));
    j(cond, label);
}

//}}} check_macroassembler_style
// ===============================================================

void
MacroAssemblerX64::incrementInt32Value(const Address& addr)
{
    asMasm().addPtr(Imm32(1), addr);
}

void
MacroAssemblerX64::unboxValue(const ValueOperand& src, AnyRegister dest)
{
    if (dest.isFloat()) {
        Label notInt32, end;
        asMasm().branchTestInt32(Assembler::NotEqual, src, &notInt32);
        convertInt32ToDouble(src.valueReg(), dest.fpu());
        jump(&end);
        bind(&notInt32);
        unboxDouble(src, dest.fpu());
        bind(&end);
    } else {
        unboxNonDouble(src, dest.gpr());
    }
}

template <typename T>
void
MacroAssemblerX64::loadInt32OrDouble(const T& src, FloatRegister dest)
{
    Label notInt32, end;
    asMasm().branchTestInt32(Assembler::NotEqual, src, &notInt32);
    convertInt32ToDouble(src, dest);
    jump(&end);
    bind(&notInt32);
    loadDouble(src, dest);
    bind(&end);
}

// If source is a double, load it into dest. If source is int32,
// convert it to double. Else, branch to failure.
void
MacroAssemblerX64::ensureDouble(const ValueOperand& source, FloatRegister dest, Label* failure)
{
    Label isDouble, done;
    Register tag = splitTagForTest(source);
    asMasm().branchTestDouble(Assembler::Equal, tag, &isDouble);
    asMasm().branchTestInt32(Assembler::NotEqual, tag, failure);

    ScratchRegisterScope scratch(asMasm());
    unboxInt32(source, scratch);
    convertInt32ToDouble(scratch, dest);
    jump(&done);

    bind(&isDouble);
    unboxDouble(source, dest);

    bind(&done);
}

} // namespace jit
} // namespace js

#endif /* jit_x64_MacroAssembler_x64_inl_h */

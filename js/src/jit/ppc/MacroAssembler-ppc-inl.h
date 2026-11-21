/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_mips32_MacroAssembler_ppc_inl_h
#define jit_mips32_MacroAssembler_ppc_inl_h

#include "jit/osxppc/MacroAssembler-ppc.h"

namespace js {
namespace jit {

//{{{ check_macroassembler_style
// ===============================================================
// Logical instructions
// MacroAssembler:: -> MacroAssembler::

// XXX. Convert these to direct assembler calls, since the idea is for speed here.
// XXX. 64 bit equivalents for G5?

void
MacroAssembler::not32(Register reg)
{
	ispew("not32(reg)");
	// OPPCC appendix A p.540
    nor(reg, reg, reg);
}

void
MacroAssembler::and32(Register src, Register dest)
{
	ispew("and32(reg, reg)");
    ma_and(dest, dest, src);
}

void
MacroAssembler::and32(Imm32 imm, Register dest)
{
	ispew("and32(imm, reg)");
    ma_and(dest, imm);
}

void
MacroAssembler::and32(Imm32 imm, const Address &dest)
{
	ispew("[[ and32(imm, adr)");
    load32(dest, addressTempRegister);
    ma_and(addressTempRegister, imm);
    store32(addressTempRegister, dest);
    ispew("   and32(imm, adr) ]]");
}

void
MacroAssembler::and32(const Address &src, Register dest)
{
	ispew("[[ and32(adr, reg)");
    load32(src, tempRegister);
    ma_and(dest, tempRegister);
    ispew("   and32(adr, reg) ]]");
}

void
MacroAssembler::andPtr(Imm32 imm, Register dest)
{
	ispew("andPtr(imm, reg)");
    ma_and(dest, imm);
}

void
MacroAssembler::andPtr(Register src, Register dest)
{
	ispew("andPtr(reg, reg)");
    ma_and(dest, src);
}

void
MacroAssembler::and64(Imm64 imm, Register64 dest)
{
// This could be optimized if one of the 32-bit halves were 0xffffffff.
// XXX G5
	ispew("and64(imm64, reg64)");
    and32(Imm32(imm.value & 0xFFFFFFFFL), dest.low);
    and32(Imm32((imm.value >> 32) & 0xFFFFFFFFL), dest.high);
}

void
MacroAssembler::or32(Register src, Register dest)
{
	ispew("or32(reg, reg)");
    ma_or(dest, src);
}

void
MacroAssembler::or32(Imm32 imm, Register dest)
{
	ispew("or32(imm, reg)");
    ma_or(dest, imm);
}

void
MacroAssembler::or32(Imm32 imm, const Address &dest)
{
	ispew("[[ or32(imm, adr)");
    load32(dest, addressTempRegister);
    ma_or(addressTempRegister, imm);
    store32(addressTempRegister, dest);
	ispew("   or32(imm, adr) ]]");
}

void
MacroAssembler::orPtr(Register src, Register dest)
{
	ispew("orPtr(reg, reg)");
    ma_or(dest, src);
}

void
MacroAssembler::orPtr(Imm32 imm, Register dest)
{
	ispew("orPtr(imm, reg)");
    ma_or(dest, imm);
}

void
MacroAssembler::or64(Register64 src, Register64 dest)
{
// XXX. On G5, or rx,rx,rx is already a 64-bit operation.
    ispew("or64(reg64, reg64)");
    or32(src.low, dest.low);
    or32(src.high, dest.high);
}

void
MacroAssembler::xor32(Imm32 imm, Register dest)
{
	ispew("xor32(imm, reg)");
    ma_xor(dest, imm);
}

void
MacroAssembler::xor64(Register64 src, Register64 dest)
{
// XXX G5
    ma_xor(dest.low, src.low);
    ma_xor(dest.high, src.high);
}

void
MacroAssembler::xorPtr(Imm32 imm, Register dest)
{
	ispew("xorPtr(imm, reg)");
    ma_xor(dest, imm);
}

void
MacroAssembler::xorPtr(Register src, Register dest)
{
	ispew("xorPtr(reg, reg)");
    ma_xor(dest, src);
}

// ===============================================================
// Arithmetic functions

#if(0)
void
MacroAssembler::add64(Imm32 imm, Register64 dest)
{
    ispew("[[ add64(imm, r64)"); 
    // This is pretty simple because it's a 32-bit integer, so we just do add with carry.
    x_li32(tempRegister, imm.value);
    addc(dest.low, dest.low, tempRegister); /* XXX: optimize to addic if possible */
    addze(dest.high, dest.high);
    ispew("   add64(imm, r64) ]]");
}
#endif

void
MacroAssembler::add64(Register64 src, Register64 dest)
{
// XXX: rldimi should be able to turn an r64 into a real 64 bit register
// then srd and and can do it back
    ispew("[[ add64(r64, r64)");
    // Just do add with carry.
    addc(dest.low, dest.low, src.low);
    adde(dest.high, dest.high, src.high);
    ispew("   add64(r64, r64) ]]");
}

void
MacroAssembler::sub32(Imm32 imm, Register dest)
{
        ispew("sub32(imm, reg)");
    ma_subu(dest, dest, imm);
}

void
MacroAssembler::sub32(Register src, Register dest)
{
        ispew("sub32(reg, reg)");
    ma_subu(dest, dest, src);
}

void
MacroAssembler::sub32(const Address& src, Register dest)
{
    ispew("[[ sub32(adr, reg)");
    MOZ_ASSERT(dest != tempRegister);
    load32(src, tempRegister);
    ma_subu(dest, dest, tempRegister); // keep MIPS operand order
    ispew("   sub32(adr, reg) ]]");
}


// ===============================================================
// Shift functions

void
MacroAssembler::lshiftPtr(Imm32 imm, Register dest)
{
    ispew("lshiftPtr(imm, reg)");
    MOZ_ASSERT(imm.value < 32);
    x_slwi(dest, dest, imm.value);
}

void
MacroAssembler::lshift64(Imm32 imm, Register64 dest)
{
	ispew("lshift64(imm, reg64)");
	MOZ_ASSERT(imm.value < 32);

	// Left rotate high 32 first.
	x_slwi(dest.high, dest.high, imm.value);
	// In low 32, rotate the bits *right* to get the mask
	// to |or| with the rotated high 32.
	x_srwi(addressTempRegister, dest.low, 32-imm.value);
	or_(dest.high, dest.high, addressTempRegister);
	// Left rotate low 32.
	x_slwi(dest.low, dest.low, imm.value);
}

void
MacroAssembler::rshiftPtr(Imm32 imm, Register dest)
{
    ispew("rshiftPtr(imm, reg)");
    MOZ_ASSERT(imm.value < 32);
    x_srwi(dest, dest, imm.value);
}

void
MacroAssembler::rshiftPtrArithmetic(Imm32 imm, Register dest)
{
    ispew("rshiftPtrArithmetic(imm, reg)");
    MOZ_ASSERT(imm.value < 32);
    srawi(dest, dest, imm.value);
}

void
MacroAssembler::rshift64(Imm32 imm, Register64 dest)
{
    ispew("rshift64(imm, reg64)");
    MOZ_ASSERT(imm.value < 32);

	// Reverse of above. Right rotate low 32 first.
	x_srwi(dest.low, dest.low, imm.value);
	// Left rotate high 32 to get the bits to add to low 32.
	x_slwi(addressTempRegister, dest.high, 32 - imm.value);
	or_(dest.low, addressTempRegister, dest.low);
	// Right rotate high 32.
	x_srwi(dest.high, dest.high, imm.value);
}

//}}} check_macroassembler_style
// ===============================================================

} // namespace jit
} // namespace js

#endif /* jit_mips32_MacroAssembler_mips32_inl_h */

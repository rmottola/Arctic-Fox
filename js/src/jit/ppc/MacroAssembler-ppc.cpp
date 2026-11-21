/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*

   IonPower (C)2015 Contributors to TenFourFox. All rights reserved.
 
   Authors: Cameron Kaiser <classilla@floodgap.com>
   with thanks to Ben Stuhl and David Kilbridge 
   and the authors of the ARM and MIPS ports
 
 */

#include "jit/osxppc/MacroAssembler-ppc.h"

#include "mozilla/DebugOnly.h"
#include "mozilla/MathAlgorithms.h"

#include "jit/Bailouts.h"
#include "jit/BaselineFrame.h"
#include "jit/SharedICRegisters.h"
#include "jit/JitFrames.h"
#include "jit/MoveEmitter.h"
#include "jit/MacroAssembler-inl.h"

using namespace js;
using namespace jit;

using mozilla::Abs;

static const int32_t PAYLOAD_OFFSET = NUNBOX32_PAYLOAD_OFFSET;
static const int32_t TAG_OFFSET = NUNBOX32_TYPE_OFFSET;

// From -ppc.h
    void MacroAssemblerPPCCompat::Push2(const Register &rr0, const Register &rr1) {
        asMasm().adjustFrame(8); push2(rr0, rr1);
    }
    void MacroAssemblerPPCCompat::Push3(const Register &rr0, const Register &rr1, const Register &rr2) {
        asMasm().adjustFrame(12); push3(rr0, rr1, rr2);
    }
    void MacroAssemblerPPCCompat::Push4(const Register &rr0, const Register &rr1, const Register &rr2,
        const Register &rr3) {
        asMasm().adjustFrame(16); push4(rr0, rr1, rr2, rr3);
    }
    void MacroAssemblerPPCCompat::Push5(const Register &rr0, const Register &rr1, const Register &rr2,
        const Register &rr3, const Register &rr4) {
        asMasm().adjustFrame(20); push5(rr0, rr1, rr2, rr3, rr4);
    }
    void MacroAssemblerPPCCompat::Pop2(const Register &rr0, const Register &rr1) {
        asMasm().adjustFrame(-8); pop2(rr0, rr1);
    }
    void MacroAssemblerPPCCompat::Pop3(const Register &rr0, const Register &rr1, const Register &rr2) {
        asMasm().adjustFrame(-12); pop3(rr0, rr1, rr2);
    }
    void MacroAssemblerPPCCompat::Pop4(const Register &rr0, const Register &rr1, const Register &rr2,
        const Register &rr3) {
        asMasm().adjustFrame(-16); pop4(rr0, rr1, rr2, rr3);
    }
    void MacroAssemblerPPCCompat::Pop5(const Register &rr0, const Register &rr1, const Register &rr2,
        const Register &rr3, const Register &rr4) {
        asMasm().adjustFrame(-20); pop5(rr0, rr1, rr2, rr3, rr4);
    }
    void MacroAssembler::Push(Register reg) { // "fake LR" supported
        ma_push(reg);
        adjustFrame(sizeof(intptr_t));
    }
    void MacroAssembler::Push(const Imm32 imm) {
        ma_li32(tempRegister, imm);
        ma_push(tempRegister);
        adjustFrame(sizeof(intptr_t));
    }
    void MacroAssembler::Push(const ImmWord imm) {
    	x_li32(tempRegister, imm.value);
        ma_push(tempRegister);
        adjustFrame(sizeof(intptr_t));
    }
    void MacroAssembler::Push(const ImmPtr imm) {
        Push(ImmWord(uintptr_t(imm.value)));
    }
    void MacroAssembler::Push(const ImmGCPtr ptr) {
        ma_li32(tempRegister, ptr);
        ma_push(tempRegister);
        adjustFrame(sizeof(intptr_t));
    }
    void MacroAssembler::Push(FloatRegister f) {
        ma_push(f);
        adjustFrame(sizeof(double));
    }
    
    void MacroAssembler::Pop(Register reg) { // "fake LR" supported
        ma_pop(reg);
        adjustFrame(-sizeof(intptr_t));
    }
    void MacroAssembler::Pop(const ValueOperand &val) {
    	popValue(val);
    	adjustFrame(-sizeof(Value));
    }

void
MacroAssemblerPPC::convertBoolToInt32(Register src, Register dest)
{
	ispew("convertBoolToInt32(reg, reg)");
	
    // Although native PPC boolean is word-size, preserve Ion's expectations
    // by masking off the higher-order bits.
    andi_rc(dest, src, 0xff);
}

void
MacroAssemblerPPC::convertInt32ToDouble(Register src, FloatRegister dest)
{
	ispew("convertInt32ToDouble(reg, fpr)");
	MOZ_ASSERT(src != tempRegister);
	
	// No GPR<->FPR moves! Use the library routine.
	dangerous_convertInt32ToDouble(src, dest,
		(dest == fpTempRegister) ? fpConversionRegister : // Baseline
			fpTempRegister);
}

void
MacroAssemblerPPC::convertInt32ToDouble(const Address &addr, FloatRegister dest)
{
	ispew("[[ convertInt32ToDouble(adr, fpr) (ASSUMING ADR IS JSVAL)");
	lwz(addressTempRegister, addr.base, addr.offset+4);

	dangerous_convertInt32ToDouble(addressTempRegister, dest,
		(dest == fpTempRegister) ? fpConversionRegister : // Baseline
			fpTempRegister);
	ispew("   convertInt32ToDouble(adr, fpr) ]]");
}

void
MacroAssemblerPPC::convertInt32ToDouble(const BaseIndex& src, FloatRegister dest)
{
    ispew("convertInt32ToDouble(bi, fpr)");
    computeScaledAddress(src, addressTempRegister);
    convertInt32ToDouble(Address(addressTempRegister, src.offset), dest);
}

void
MacroAssemblerPPC::convertUInt32ToDouble(Register src, FloatRegister dest)
{
	// This is almost the same as convertInt, except the zero constant
	// is 0x4330 0000 0000 0000 (not 8000), and we don't xoris the sign.
	// See also OPPCC chapter 8 p.157.
	ispew("[[ convertUInt32ToDouble(reg, fpr)");
	
	// It's possible to call this for temp registers, so use spares.
	FloatRegister fpTemp = (dest == fpTempRegister) ? fpConversionRegister
		: fpTempRegister;
	Register temp = (src == tempRegister) ? addressTempRegister 
		: tempRegister;
		
	// Build temporary frame with zero constant.
	x_lis(temp, 0x4330);
	stwu(temp, stackPointerRegister, -8); // cracked on G5
	x_lis(temp, 0x0000); // not 8000
	
	// (2nd G5 dispatch group)
	stw(src, stackPointerRegister, 4);
	// Don't flip integer value's sign; use as integer component directly.
	// stack now has 0x4330 0000 <src>
#ifdef _PPC970_
	// G5 and POWER4+ do better if the lfd and the stws aren't in the
	// same dispatch group.
	x_nop();
	x_nop();
	x_nop();
#endif

	// (3rd G5 dispatch group)
	// Load intermediate float.
	lfd(dest, stackPointerRegister, 0);
#ifdef _PPC970_
	x_nop();
	x_nop();
	x_nop();
#endif

	// (4th G5 dispatch group)
	stw(temp, stackPointerRegister, 4);
	// stack now has 0x4330 0000 0000 0000
#ifdef _PPC970_
	x_nop();
	x_nop();
	x_nop();
#endif

	// (Final G5 dispatch group)
	// Load zero float and normalize with a subtraction operation.
	lfd(fpTemp, stackPointerRegister, 0);
	fsub(dest, dest, fpTemp);
	// Destroy temporary frame.
	addi(stackPointerRegister, stackPointerRegister, 8);
	
	ispew("   convertUInt32ToDouble(reg, fpr) ]]");
}

void
MacroAssemblerPPC::mul64(Imm64 imm, const Register64& dest)
{
    // LOW32  = LOW(LOW(dest) * LOW(imm));
    // HIGH32 = LOW(HIGH(dest) * LOW(imm)) [multiply imm into upper bits]
    //        + LOW(LOW(dest) * HIGH(imm)) [multiply dest into upper bits]
    //        + HIGH(LOW(dest) * LOW(imm)) [carry]
    
    /* G5-64:
       convert Register64 to a single 64 bit register
       convert imm to another 64 bit register
         li r0,32
         sld rA,dest.high,r0
         li32 r12,imm.high
         sld rB,r12,r0
         or rA,rA,dest.low
         li32 r0,imm.low  << use or32 so we can optimize
         or rB,rB,r0      <<
         li r12,-1
         mulld dest.lo,rA,rB
         li r0,32
         srd dest.high,dest.lo,r0
         and dest.lo,dest.lo,r12
    */
    ispew("[[ mul64(imm64, r64)");
    
    // Compute both low and high (LOW(dest) * LOW(imm)).
    // Don't use overflow (we assume overflow). Instead, use mullw and mulhwu.
    x_li32(tempRegister, (uint32_t)(imm.value & LOW_32_MASK));
    mullw(addressTempRegister, tempRegister, dest.low); // LOW32
    mulhwu(tempRegister, tempRegister, dest.low); // base of HIGH32
    // Push addressTempRegister so we can get it back at the end.
    // XXX: This would be better with a third temp register.
    stwu(addressTempRegister, stackPointerRegister, -4);
    
    // Now we need to add the two LOW() terms. These can be regular mullws.
    // Do the HIGH(dest) part first so that we can use dest.high as
    // its own accumulator.
    x_li32(addressTempRegister, (uint32_t)(imm.value & LOW_32_MASK));
    mullw(dest.high, dest.high, addressTempRegister); // LOW(HIGH(dest) * LOW(imm))
    add(dest.high, dest.high, tempRegister); // tempRegister is now free
    x_li32(addressTempRegister, (uint32_t)((imm.value >> 32) & LOW_32_MASK));
    mullw(tempRegister, dest.low, addressTempRegister); // LOW(low(dest) * HIGH(imm))
    
    // Start getting dest.low off the stack.
    lwz(dest.low, stackPointerRegister, 0);
    // Done.
    add(dest.high, dest.high, tempRegister);
    addi(stackPointerRegister, stackPointerRegister, 4);
    
    ispew("   mul64(imm64, r64) ]]");

// Future expansion
#if(0)
    if (((imm.value >> 32) & LOW_32_MASK) == 5) {
        // Optimized case for Math.random().

        // HIGH(dest) += LOW(LOW(dest) * HIGH(imm));
        as_sll(ScratchRegister, dest.low, 2);
        as_addu(ScratchRegister, ScratchRegister, dest.low);
        as_addu(dest.high, dest.high, ScratchRegister);

        // LOW(dest) = mflo;
        as_mflo(dest.low);
    }
#endif
}

static const double TO_DOUBLE_HIGH_SCALE = 0x100000000;

void
MacroAssemblerPPC::convertUInt64ToDouble(Register64 src, Register temp, FloatRegister dest)
{
// XXX: G5-64 has fcfid. Push the register pair, load into an FPR, fcfid to dest.
    convertUInt32ToDouble(src.high, dest);
    ma_lid(ScratchDoubleReg, TO_DOUBLE_HIGH_SCALE);
    mulDouble(ScratchDoubleReg, dest);
    convertUInt32ToDouble(src.low, ScratchDoubleReg);
    addDouble(ScratchDoubleReg, dest);
}

void
MacroAssemblerPPC::convertUInt32ToFloat32(Register src, FloatRegister dest)
{
    // Since PPC FPRs are both float and double, just call the other routine.
    convertUInt32ToDouble(src, dest);
    frsp(dest, dest);
}

void
MacroAssemblerPPC::convertDoubleToFloat32(FloatRegister src, FloatRegister dest)
{
    ispew("convertDoubleToFloat32(fpr, fpr)");
    frsp(dest, src);
}

// Truncate src to 32 bits and move to dest. If not representable in 32
// bits, branch to the failure label.
void
MacroAssemblerPPC::branchTruncateDouble(FloatRegister src, Register dest,
                                         Label *fail)
{
	ispew("[[ branchTruncateDouble(fpreg, reg, l)");
	MOZ_ASSERT(src != fpTempRegister);
	MOZ_ASSERT(dest != tempRegister);
	
	// Again, no GPR<->FPR moves! So, back to the stack.
	// Turn into a fixed-point integer (i.e., truncate).
	//
	// TODO: Is fctiwz_rc with FPSCR exceptions faster than checking constants??
	// See below. 
	// XXX: It looks like it is: see FPSCR VXCVI and the CodeGenerator.

	fctiwz(fpTempRegister, src);
	// Stuff in a temporary frame (cracked on G5).
	stfdu(fpTempRegister, stackPointerRegister, -8);
	// Load this constant here (see below); this spaces the dispatch
	// group out and can be parallel. Even if the stfdu leads the dispatch
	// group, it's cracked, so two slots, and this load must use two, and
	// the lwz will not go in the branch slot.
	x_li32(tempRegister, 0x7fffffff);
	// Pull out the lower 32 bits. This is the result.
	lwz(dest, stackPointerRegister, 4);
	
	// We don't care if a truncation occurred. In fact, all we care is
	// that the integer result fits in 32 bits. Fortunately, fctiwz will
	// tip us off: if src > 2^31-1, then dest becomes 0x7fffffff, the
	// largest 32-bit positive integer. If src < -2^31, then dest becomes
	// 0x80000000, the largest 32-bit negative integer. So we just test
	// for those two values. If either value is found, fail-branch. Use
	// unsigned compares, since these are logical values.
	
	// (tempRegister was loaded already)
	cmplw(dest, tempRegister);
	// Destroy the temporary frame before testing, since we might branch.
	// The cmplw will execute in parallel for "free."
	addi(stackPointerRegister, stackPointerRegister, 8);
	bc(Equal, fail);
	
	x_li32(tempRegister, 0x80000000); // sign extends!
	cmplw(dest, tempRegister);
	bc(Equal, fail);
	ispew("  branchTruncateDouble(fpreg, reg, l) ]]");
}

// Convert src to an integer and move to dest. If the result is not representable
// as an integer (i.e., not integral, or out of range), branch.
void
MacroAssemblerPPC::convertDoubleToInt32(FloatRegister src, Register dest,
                                         Label *fail, bool negativeZeroCheck)
{
	ispew("[[ convertDoubleToInt32(fpr, reg, l, bool)");
	MOZ_ASSERT(dest != tempRegister);
	
	// Turn into a fixed-point integer (i.e., truncate). Set FX for any
	// exception (inexact or bad conversion), which becomes CR1+LT with
	// fctiwz_rc. FI is not sticky, so we need not whack it, but XX is.
	// On G5, all of these instructions are separate dispatch groups.
	// Worse still, mtfsb* requires FPSCR to be serialized, so clear as
	// few bits as possible. mtfsfi wouldn't be any more efficient here.
	mtfsb0(6);  // whack XX
	mtfsb0(23); // whack VXCVI
	mtfsb0(0);  // then whack summary FX
	
	fp2int(src, dest, false);
	
	// Test and branch if inexact (i.e., if "less than").
	bc(LessThan, fail, cr1);
	
	// If negativeZeroCheck is true, we need to also branch to the
	// failure label if the result is -0 (if false, we don't care).
        // fctiwz. will merrily convert -0 because, well, it's zero!
	if (negativeZeroCheck) {
		ispew("<< checking for negativeZero >>");
		
		// If it's not zero, don't bother.
		and__rc(tempRegister, dest, dest);
		BufferOffset nonZero = _bc(0, NonZero, cr0, LikelyB);
		
		// FP negative zero is 0x8000 0000 0000 0000 in the IEEE 754
		// standard, so test the upper 32 bits by extracting it on the
		// stack (similar to our various breakDouble iteractions). We have
		// no constant to compare against, so this is the best option.
		stfdu(src, stackPointerRegister, -8);
#ifdef _PPC970_
		x_nop();
		x_nop(); // stfdu is cracked
#endif
		lwz(tempRegister, stackPointerRegister, 0); // upper 32 bits
		and__rc(addressTempRegister, tempRegister, tempRegister);
		addi(stackPointerRegister, stackPointerRegister, 8);
		bc(NonZero, fail);
		
		bindSS(nonZero);
	}
	ispew("  convertDoubleToInt32(fpr, reg, l, bool) ]]");
}

void
MacroAssemblerPPC::convertFloat32ToInt32(FloatRegister src, Register dest,
                                          Label *fail, bool negativeZeroCheck)
{
	convertDoubleToInt32(src, dest, fail, negativeZeroCheck);
}

void
MacroAssemblerPPC::convertFloat32ToDouble(FloatRegister src, FloatRegister dest)
{
	// Nothing to do, except if they are not the same.
	ispew("convertFloat32ToDouble(fpr, fpr)");
	if (src != dest)
		fmr(dest, src);
}

void
MacroAssemblerPPC::branchTruncateFloat32(FloatRegister src, Register dest,
                                          Label *fail)
{
	branchTruncateDouble(src, dest, fail);
}

void
MacroAssemblerPPC::convertInt32ToFloat32(Register src, FloatRegister dest)
{
	convertInt32ToDouble(src, dest);
    frsp(dest, dest);
}

void
MacroAssemblerPPC::convertInt32ToFloat32(const Address &src, FloatRegister dest)
{
	convertInt32ToDouble(src, dest);
    frsp(dest, dest);
}

void
MacroAssemblerPPC::addDouble(FloatRegister src, FloatRegister dest)
{
	ispew("addDouble(fpr, fpr)");
    fadd(dest, dest, src);
}

void
MacroAssemblerPPC::subDouble(FloatRegister src, FloatRegister dest)
{
	ispew("subDouble(fpr, fpr)");
    fsub(dest, dest, src); // T = A-B (not like subf)
}

void
MacroAssemblerPPC::mulDouble(FloatRegister src, FloatRegister dest)
{
	ispew("mulDouble(fpr, fpr)");
    fmul(dest, dest, src);
}

void
MacroAssemblerPPC::divDouble(FloatRegister src, FloatRegister dest)
{
	ispew("divDouble(fpr, fpr)");
    fdiv(dest, dest, src);
}

void
MacroAssemblerPPC::negateDouble(FloatRegister reg)
{
	ispew("negateDouble(fpr)");
    fneg(reg, reg);
}

void
MacroAssemblerPPC::inc64(AbsoluteAddress dest)
{
	ispew("[[ inc64(aadr)");
	// Get the absolute address, increment its second word (big endian!),
	// and then the FIRST word if needed.
	// TODO: G5 should kick ass here, but dest might not be aligned.
	// Consider using aligned doubleword instructions if dest is aligned
	// to doubleword (ld/addi/nop*/std would work). Right now this is only
	// used infrequently in one place.

	// (First G5 dispatch group.) Load effective address.
	x_li32(addressTempRegister, (uint32_t)dest.addr); // 2 inst
	
	// Get the LSB and increment it, setting or clearing carry.
	lwz(tempRegister, addressTempRegister, 4);
	addic(tempRegister, tempRegister, 1);
	
	// (Second G5 dispatch group.) Store it.
	stw(tempRegister, addressTempRegister, 4);
#ifdef _PPC970_
	// Keep the stw and the following lwz in separate groups.
	x_nop();
	x_nop();
	x_nop();
#endif

	// (Third G5 dispatch group.) Load MSB and increment it with carry
	// by adding zero. This way we don't need to branch!
	lwz(tempRegister, addressTempRegister, 0);
	addze(tempRegister, tempRegister);
#ifdef _PPC970_
	// Force the next stw into another dispatch group.
	x_nop();
	x_nop();
#endif

	// (Final G5 dispatch group.) Store it; done.
	stw(tempRegister, addressTempRegister, 0);
	ispew("   inc64(aadr) ]]");
}

void
MacroAssemblerPPC::ma_li32(Register dest, ImmGCPtr ptr)
{
	m_buffer.ensureSpace(8); // make sure this doesn't get broken up
    writeDataRelocation(ptr);
    ma_li32Patchable(dest, Imm32(uintptr_t(ptr.value)));
}

void
MacroAssemblerPPC::ma_li32(Register dest, AbsoluteLabel *label)
{
    MOZ_ASSERT(!label->bound());
    // Thread the patch list through the unpatched address word in the
    // instruction stream.
    m_buffer.ensureSpace(2 * sizeof(uint32_t));
    BufferOffset bo = m_buffer.nextOffset();
    // Don't use ma_li32Patchable in case we jump pages.
    x_p_li32(dest, label->prev());
    label->setPrev(bo.getOffset());
}

void
MacroAssemblerPPC::ma_li32(Register dest, Imm32 imm)
{
	x_li32(dest, imm.value);
}

void
MacroAssemblerPPC::ma_li32(Register dest, CodeOffset *label)
{
    m_buffer.ensureSpace(2 * sizeof(uint32_t));
    BufferOffset bo = m_buffer.nextOffset();
    x_p_li32(dest, -2); // don't use ma_li32Patchable in case we jump pages.
    label->bind(bo.getOffset());
}

void
MacroAssemblerPPC::ma_li32Patchable(Register dest, Imm32 imm)
{
    m_buffer.ensureSpace(2 * sizeof(uint32_t));
    x_p_li32(dest, imm.value);
}

void
MacroAssemblerPPC::ma_li32Patchable(Register dest, ImmPtr imm)
{
    return ma_li32Patchable(dest, Imm32(int32_t(imm.value)));
}

// We use these abstracted forms of |and|,|or|,etc. to avoid all that mucking about
// with immediate quantities. rc = set Rc bit (irrelevant for andi_rc).
// Only |and| implements the Rc bit, because it's the only one where it matters
// to Ion conditionals. Raw instructions required for the remainder. Sorry. :P
void
MacroAssemblerPPC::ma_and(Register rd, Register rs, bool rc)
{
    if (rc)
    	and__rc(rd, rd, rs);
    else
    	and_(rd, rd, rs);
}

void
MacroAssemblerPPC::ma_and(Register rd, Register rs, Register rt, bool rc)
{
	if (rc)
		and__rc(rd, rs, rt);
	else
		and_(rd, rs, rt);
}

void
MacroAssemblerPPC::ma_and(Register rd, Imm32 imm)
{
    ma_and(rd, rd, imm);
}

void
MacroAssemblerPPC::ma_and(Register rd, Register rs, Imm32 imm)
{
    if (Imm16::IsInUnsignedRange(imm.value)) {
        andi_rc(rd, rs, imm.value);
    } else if (!(imm.value & 0x0000FFFF)) {
    	andis_rc(rd, rs, (imm.value >> 16));
    } else {
    	MOZ_ASSERT(rs != tempRegister);
        x_li32(tempRegister, imm.value);
        and__rc(rd, rs, tempRegister);
    }
}

void
MacroAssemblerPPC::ma_or(Register rd, Register rs)
{
    or_(rd, rd, rs);
}

void
MacroAssemblerPPC::ma_or(Register rd, Register rs, Register rt)
{
    or_(rd, rs, rt);
}

void
MacroAssemblerPPC::ma_or(Register rd, Imm32 imm)
{
    ma_or(rd, rd, imm);
}

void
MacroAssemblerPPC::ma_or(Register rd, Register rs, Imm32 imm)
{
    if (Imm16::IsInUnsignedRange(imm.value)) {
        ori(rd, rs, imm.value);
    } else if (!(imm.value && 0x0000FFFF)) {
    	oris(rd, rs, (imm.value >> 16));
    } else {
    	MOZ_ASSERT(rs != tempRegister);
        x_li32(tempRegister, imm.value);
        or_(rd, rs, tempRegister);
    }
}

void
MacroAssemblerPPC::ma_xor(Register rd, Register rs)
{
    xor_(rd, rd, rs);
}

void
MacroAssemblerPPC::ma_xor(Register rd, Register rs, Register rt)
{
    xor_(rd, rs, rt);
}

void
MacroAssemblerPPC::ma_xor(Register rd, Imm32 imm)
{
    ma_xor(rd, rd, imm);
}

void
MacroAssemblerPPC::ma_xor(Register rd, Register rs, Imm32 imm)
{
    if (Imm16::IsInUnsignedRange(imm.value)) {
        xori(rd, rs, imm.value);
    } else if (!(imm.value && 0x0000FFFF)) {
    	xoris(rd, rs, (imm.value >> 16));
    } else {
    	MOZ_ASSERT(rs != tempRegister);
        x_li32(tempRegister, imm.value);
        xor_(rd, rs, tempRegister);
    }
}

// Arithmetic.

void
MacroAssemblerPPC::ma_addu(Register rd, Register rs, Imm32 imm)
{
	MOZ_ASSERT(rs != tempRegister); // it would be li, then!
    if (Imm16::IsInSignedRange(imm.value)) {
        addi(rd, rs, imm.value);
    } else if (!(imm.value && 0x0000FFFF)) {
    	addis(rd, rs, (imm.value >> 16));
    } else {
        x_li32(tempRegister, imm.value);
        add(rd, rs, tempRegister);
    }
}

void
MacroAssemblerPPC::ma_addu(Register rd, Register rs)
{
    add(rd, rd, rs);
}

void
MacroAssemblerPPC::ma_addu(Register rd, Imm32 imm)
{
    ma_addu(rd, rd, imm);
}

// If rs + rt overflows, go to the failure label.
void
MacroAssemblerPPC::ma_addTestOverflow(Register rd, Register rs, Register rt, Label *overflow)
{
	addo(rd, rs, rt);
	bc(Overflow, overflow);
}

void
MacroAssemblerPPC::ma_addTestOverflow(Register rd, Register rs, Imm32 imm, Label *overflow)
{
	MOZ_ASSERT(rs != tempRegister);
	x_li32(tempRegister, imm.value);
	ma_addTestOverflow(rd, rs, tempRegister, overflow);
}

// Subtract.
// MIPS operand order is T = A - B, whereas in PPC it's T = B - A.
// Keep MIPS order here for simplicity at the MacroAssembler level.
void
MacroAssemblerPPC::ma_subu(Register rd, Register rs, Register rt)
{
    subf(rd, rt, rs); //as_subu(rd, rs, rt);
}

void
MacroAssemblerPPC::ma_subu(Register rd, Register rs, Imm32 imm)
{
	MOZ_ASSERT(rs != tempRegister);
    if (Imm16::IsInSignedRange(-imm.value)) {
        addi(rd, rs, -imm.value);
    } else {
        x_li32(tempRegister, imm.value);
        subf(rd, tempRegister, rs); //as_subu(rd, rs, ScratchRegister);
    }
}

void
MacroAssemblerPPC::ma_subu(Register rd, Imm32 imm)
{
    ma_subu(rd, rd, imm);
}

void
MacroAssemblerPPC::ma_subTestOverflow(Register rd, Register rs, Register rt, Label *overflow)
{
    subfo(rd, rt, rs); //ma_subu(rd, rs, rt);
    bc(Overflow, overflow);
}

void
MacroAssemblerPPC::ma_subTestOverflow(Register rd, Register rs, Imm32 imm, Label *overflow)
{
    if (imm.value != INT32_MIN) {
        ma_addTestOverflow(rd, rs, Imm32(-imm.value), overflow);
    } else {
    	MOZ_ASSERT(rs != tempRegister);
        x_li32(tempRegister, imm.value);
        ma_subTestOverflow(rd, rs, tempRegister, overflow);
    }
}

void
MacroAssemblerPPC::ma_mult(Register rs, Imm32 imm)
{
	if (Imm16::IsInSignedRange(imm.value)) {
		if (rs != tempRegister &&
				(imm.value == 3 || imm.value == 5 || imm.value == 10 || imm.value == 12)) {
			// Ensure rs is a different register for *3 and *5.
			// This can save an entire cycle.
			// (Hey, on a G3, that could really mean something!)
			x_mr(tempRegister, rs);
			x_sr_mulli(rs, tempRegister, imm.value);
		} else {
			x_sr_mulli(rs, rs, imm.value);
		}
	} else {
		MOZ_ASSERT(rs != tempRegister);
		x_li32(tempRegister, imm.value);
		mullw(rs, rs, tempRegister);
	}
}

void
MacroAssemblerPPC::ma_mul_branch_overflow(Register rd, Register rs, Register rt, Label *overflow)
{
	mullwo(rd, rs, rt);
	bc(Overflow, overflow);
}

void
MacroAssemblerPPC::ma_mul_branch_overflow(Register rd, Register rs, Imm32 imm, Label *overflow)
{
    x_li32(tempRegister, imm.value);
    ma_mul_branch_overflow(rd, rs, tempRegister, overflow);
}

void
MacroAssemblerPPC::ma_div_branch_overflow(Register rd, Register rs, Register rt, Label *overflow)
{
	// Save the original multiplier just in case.
	Register orreg = rt;
	Register prreg = rs;
	if (rd == rt) {
		MOZ_ASSERT(rt != addressTempRegister);
		x_mr(addressTempRegister, rt);
		orreg = addressTempRegister;
	}
	if (rd == rs) {
		MOZ_ASSERT(rs != tempRegister);
		x_mr(tempRegister, rs);
		prreg = tempRegister;
	}
	// MIPS: LO = rs/rt
	divwo(rd, rs, rt);
	bc(Overflow, overflow);
	// If the remainder is non-zero, branch to overflow as well.
	MOZ_ASSERT(rs != tempRegister);
	MOZ_ASSERT(rd != tempRegister);
	// This must be 32-bit representable or the divwo would have already failed.
	mullw(addressTempRegister, rd, orreg);
	subf_rc(tempRegister, addressTempRegister, prreg);
	bc(NonZero, overflow);
}

void
MacroAssemblerPPC::ma_div_branch_overflow(Register rd, Register rs, Imm32 imm, Label *overflow)
{
    x_li32(tempRegister, imm.value);
    ma_div_branch_overflow(rd, rs, tempRegister, overflow);
}

// Memory.

void
MacroAssemblerPPC::ma_load(Register dest, Address address,
                            LoadStoreSize size, LoadStoreExtension extension, bool swapped)
{
    bool indexed = false;
    MOZ_ASSERT(address.base != tempRegister); // "mscdfr0"

    if (!Imm16::IsInSignedRange(address.offset) || swapped) { // All byte-swap instructions are indexed.
    	// Need indexed instructions. Put the offset into temp.
    	// We can clobber it safely with the load.
        x_li32(tempRegister, address.offset);
        indexed = true;
    }

    switch (size) {
      case SizeByte:
      	if (indexed)
      		lbzx(dest, address.base, tempRegister);
      	else
      		lbz(dest, address.base, address.offset);
        if (ZeroExtend != extension)
        	extsb(dest, dest);
        break;
      case SizeHalfWord:
      	// We can use lha for sign extension and save an instruction.
      	if (swapped) {
      		lhbrx(dest, address.base, tempRegister);
      		if (ZeroExtend != extension) extsh(dest, dest);
      	} 
      	else if (indexed && extension == ZeroExtend) // indexed, not swapped, zero extension
      		lhzx(dest, address.base, tempRegister);
      	else if (indexed) // indexed, not swapped, sign extension
      		lhax(dest, address.base, tempRegister);
      	else if (extension == ZeroExtend) // not indexed, not swapped, zero extension
      		lhz(dest, address.base, address.offset);
      	else // not swapped, not indexed, not zero extension
      		lha(dest, address.base, address.offset);
        break;
      case SizeWord:
      	if (swapped)
      		lwbrx(dest, address.base, tempRegister);
      	else if (indexed)
      		lwzx(dest, address.base, tempRegister);
      	else
      		lwz(dest, address.base, address.offset);
        break;
      default:
        MOZ_CRASH("Invalid argument for ma_load");
    }
}

void
MacroAssemblerPPC::ma_load(Register dest, const BaseIndex &src,
                            LoadStoreSize size, LoadStoreExtension extension, bool swapped)
{
    computeScaledAddress(src, addressTempRegister);
    ma_load(dest, Address(addressTempRegister, src.offset), size, extension, swapped);
}

void
MacroAssemblerPPC::ma_store(Register data, Address address, LoadStoreSize size,
                             bool swapped)
{
// The situations can be:
// Address base reg can never be tempRegister (assert this)
//
// Address base reg is addressTempRegister and data is tempRegister
// -> in this case recruit the emergency temp for offset
// 
// Data and address base reg are not temp registers
// -> tempRegister for offset
//
// Data is tempRegister, address base reg is not temp register
// -> addressTempRegister for offset
//
// Data is addressTempRegister, address base reg is not temp register
// -> tempRegister for offset

    // data can be tempRegister sometimes.
    Register scratch = (data == tempRegister) ? addressTempRegister : tempRegister;
    bool indexed = false;
    MOZ_ASSERT(address.base != tempRegister); // "mscdfr0"

    if (!Imm16::IsInSignedRange(address.offset) || swapped) {
    	// Need indexed instructions. Put the offset into a temp register.
    	if (address.base == addressTempRegister) // This is possible!
    		scratch = emergencyTempRegister;
        x_li32(scratch, address.offset);
        indexed = true;
    }

    switch (size) {
      case SizeByte:
      	if (indexed)
      		stbx(data, address.base, scratch);
      	else
        	stb(data, address.base, address.offset);
        break;
      case SizeHalfWord:
        if (swapped)
                sthbrx(data, address.base, scratch);
      	else if (indexed)
      		sthx(data, address.base, scratch);
      	else
        	sth(data, address.base, address.offset);
        break;
      case SizeWord:
        if (swapped)
        	stwbrx(data, address.base, scratch);
      	else if (indexed)
      		stwx(data, address.base, scratch);
      	else
        	stw(data, address.base, address.offset);
        break;
      default:
        MOZ_CRASH("Invalid argument for ma_store");
    }
}

void
MacroAssemblerPPC::ma_store(Register data, const BaseIndex &dest,
                             LoadStoreSize size, bool swapped)
{
    computeScaledAddress(dest, addressTempRegister);
    ma_store(data, Address(addressTempRegister, dest.offset), size, swapped);
}

void
MacroAssemblerPPC::ma_store(Imm32 imm, const BaseIndex &dest,
                             LoadStoreSize size, bool swapped)
{
    computeEffectiveAddress(dest, addressTempRegister);
    x_li32(tempRegister, imm.value);
    // We can use addressTempRegister as the base because we know the offset
    // will fit (so ma_store won't need it as another temporary register).
    ma_store(tempRegister, Address(addressTempRegister, 0), size, swapped);
}

void
MacroAssemblerPPC::computeScaledAddress(const BaseIndex &address, Register dest)
{
    int32_t shift = Imm32::ShiftOf(address.scale).value;
    if (shift) {
    	MOZ_ASSERT(address.index != tempRegister);
    	MOZ_ASSERT(address.base != tempRegister);
        x_slwi(tempRegister, address.index, shift);
        add(dest, address.base, tempRegister);
    } else {
        add(dest, address.base, address.index);
    }
}

// Shortcuts for when we know we're transferring 32 bits of data.
// Held over from MIPS to make the code translation smoother, but mostly
// extraneous.
void
MacroAssemblerPPC::ma_lw(Register data, Address address)
{
    ma_load(data, address, SizeWord);
}

void
MacroAssemblerPPC::ma_sw(Register data, Address address)
{
    ma_store(data, address, SizeWord);
}

void
MacroAssemblerPPC::ma_sw(Imm32 imm, Address address)
{
    MOZ_ASSERT(address.base != tempRegister);
    x_li32(tempRegister, imm.value);

    if (Imm16::IsInSignedRange(address.offset)) {
        stw(tempRegister, address.base, address.offset);
    } else {
        MOZ_ASSERT(address.base != addressTempRegister);

        x_li32(addressTempRegister, address.offset);
        stwx(tempRegister, address.base, addressTempRegister);
    }
}

void
MacroAssemblerPPC::ma_sw(Register data, BaseIndex &address)
{
    ma_store(data, address, SizeWord);
}

// These are the cheater pops and pushes that handle "fake LR."
void
MacroAssemblerPPC::ma_pop(Register r)
{
	Register rr = (r == lr) ? tempRegister : r;
	lwz(rr, stackPointerRegister, 0);
	if (r == lr) {
		// Naughty!
		x_mtlr(rr); // new dispatch group on G5
	}
	// XXX. Not needed if r is sp?
	addi(stackPointerRegister, stackPointerRegister, 4);
}

void
MacroAssemblerPPC::ma_push(Register r)
{
	Register rr = r;
	if (r == lr) {
		// Naughty!
		x_mflr(tempRegister); // new dispatch group on G5
		rr = tempRegister;
	}
	stwu(rr, stackPointerRegister, -4); // cracked on G5
}

BufferOffset
MacroAssemblerPPC::_b(int32_t off, BranchAddressType bat, LinkBit lb)
{
	return Assembler::b(off, bat, lb);
}
BufferOffset
MacroAssemblerPPC::_bc(int16_t off, Assembler::Condition cond, CRegisterID cr, LikelyBit lkb, LinkBit lb)
{
	return Assembler::bc(off, cond, cr, lkb, lb);
}
BufferOffset
MacroAssemblerPPC::_bc(int16_t off, Assembler::DoubleCondition cond, CRegisterID cr, LikelyBit lkb, LinkBit lb)
{
	return Assembler::bc(off, cond, cr, lkb, lb);
}

// IF YOU CHANGE THIS: change PPC_B_STANZA_LENGTH in MacroAssembler-ppc.h
void
MacroAssemblerPPC::b(Label *l, bool is_call)
{
	// For future expansion.
	MOZ_ASSERT(!is_call);
	
	// Our calls are designed so that the return address is always after the bl/bctrl.
	// Thus, we can always have the option of a short call.
	m_buffer.ensureSpace(PPC_B_STANZA_LENGTH + 4);
	
	if (l->bound()) {
		// Label is already bound, emit an appropriate stanza.
		// This means that the code was already generated in this buffer.
		BufferOffset bo = nextOffset();
		int32_t offs = l->offset();
		int32_t moffs = bo.getOffset();
		MOZ_ASSERT(moffs >= offs);
		
		moffs = offs - (moffs + PPC_B_STANZA_LENGTH - 4);
		if (JOffImm26::IsInRange(moffs)) {
			// Emit a four instruction short jump.
			x_nop();
			x_nop();
			x_nop();
			Assembler::b(moffs, RelativeBranch, (is_call) ? LinkB : DontLinkB);
		} else {
			// Emit a four instruction long jump.
			addLongJump(nextOffset());
            // The offset could be short, so make it patchable.
			x_p_li32(tempRegister, offs);
			x_mtctr(r0);
			bctr((is_call) ? LinkB : DontLinkB);
		}
		MOZ_ASSERT_IF(!oom(), (nextOffset().getOffset() - bo.getOffset()) == PPC_B_STANZA_LENGTH);
		return;
	}

	// Second word holds a pointer to the next branch in label's chain.
	int32_t nextInChain = l->used() ? l->offset() : LabelBase::INVALID_OFFSET;

	BufferOffset bo = x_trap();
#if DEBUG
	JitSpew(JitSpew_Codegen, "-------- --- %08x <<< b offset %08x", nextInChain, bo.getOffset());
#endif
	writeInst(nextInChain);
        if (MOZ_LIKELY(!oom())) l->use(bo.getOffset());
    x_nop(); // Spacer
    
    Assembler::b(4, RelativeBranch, (is_call) ? LinkB : DontLinkB); // Will be patched by bind()
    // If we're out of memory, we generated defective code already.
    MOZ_ASSERT_IF(!oom(),
        (nextOffset().getOffset() - bo.getOffset()) == PPC_B_STANZA_LENGTH);
}
void
MacroAssemblerPPC::bl(Label *l) // OMIGOSH I'M LAZY
{
	b(l, true);
}

// IF YOU CHANGE THIS: change PPC_BC_STANZA_LENGTH in MacroAssembler-ppc.h
void
MacroAssemblerPPC::bc(uint32_t rawcond, Label *l)
{
	m_buffer.ensureSpace(PPC_BC_STANZA_LENGTH + 4); // paranoia strikes deep in the heartland
	
	if (l->bound()) {
		// Label is already bound, emit an appropriate stanza.
		// This means that the code was already generated in this buffer.
		BufferOffset bo = nextOffset();
		int32_t offs = l->offset();
		int32_t moffs = bo.getOffset();
		MOZ_ASSERT(moffs >= offs);
		
		moffs = offs - (moffs + PPC_BC_STANZA_LENGTH - 4);
		if (BOffImm16::IsInSignedRange(moffs)) {
			// Emit a five instruction short conditional.
			x_nop();
			x_nop();
			x_nop();
			x_nop();
			Assembler::bc(moffs, rawcond);
		} else {
			// Emit a five instruction long conditional, inverting the bit sense for the first branch.
			MOZ_ASSERT(!(rawcond & 1)); // Always and similar conditions not yet handled
			rawcond ^= 8; // flip 00x00 (see OPPCC p.372)
			
			Assembler::bc(PPC_BC_STANZA_LENGTH, rawcond);
			addLongJump(nextOffset());
			// The offset could be "short," so make it patchable.
			x_p_li32(tempRegister, offs);
			x_mtctr(tempRegister);
			bctr();
		}
		MOZ_ASSERT_IF(!oom(), (nextOffset().getOffset() - bo.getOffset()) == PPC_BC_STANZA_LENGTH);
		return;
	}

	// Second word holds a pointer to the next branch in label's chain.
	int32_t nextInChain = l->used() ? l->offset() : LabelBase::INVALID_OFFSET;

	BufferOffset bo = x_trap();
#if DEBUG
	JitSpew(JitSpew_Codegen, "-------- --- %08x <<< bc %04x offset", nextInChain, rawcond);
#endif
	writeInst(nextInChain);
        if (MOZ_LIKELY(!oom())) l->use(bo.getOffset());
	
	// Leave sufficient space in case this gets patched long. We need two more nops.
    x_nop();
    x_nop();
    
	// Finally, emit a dummy bc with the right bits set so that bind() will fix it later.
    Assembler::bc(4, rawcond);
    MOZ_ASSERT_IF(!oom(),
        (nextOffset().getOffset() - bo.getOffset()) == PPC_BC_STANZA_LENGTH);
}
void
MacroAssemblerPPC::bc(Condition cond, Label *l, CRegisterID cr)
{
	bc(computeConditionCode(cond, cr), l);
}
void
MacroAssemblerPPC::bc(DoubleCondition cond, Label *l, CRegisterID cr)
{
	bc(computeConditionCode(cond, cr), l);
}

// Floating point instructions.
void
MacroAssemblerPPC::ma_lis(FloatRegister dest, float value)
{
	// Fugly.
    uint32_t fasi = mozilla::BitwiseCast<uint32_t>(value);

    x_li32(tempRegister, fasi); // probably two instructions
    // Remember, no GPR<->FPR moves, so we have to put this on the stack.
    stwu(tempRegister, stackPointerRegister, -4); // cracked
#if _PPC970_
	// Keep the stwu and the lfs separate in case the stwu ends up leading a dispatch group.
	x_nop();
	x_nop();
#endif
	lfs(dest, stackPointerRegister, 0);
	addi(stackPointerRegister, stackPointerRegister, 4);
}

void
MacroAssemblerPPC::ma_lid(FloatRegister dest, double value)
{
    struct DoubleStruct {
        uint32_t hi;
        uint32_t lo;
    } ;
    DoubleStruct intStruct = mozilla::BitwiseCast<DoubleStruct>(value);
    // Like ma_lis, but with two pieces. Push lo, then hi (endianness!).

    addi(stackPointerRegister, stackPointerRegister, -8);
    x_li32(tempRegister, intStruct.hi);
    x_li32(addressTempRegister, intStruct.lo);
    stw(tempRegister, stackPointerRegister, 0);
    stw(addressTempRegister, stackPointerRegister, 4);
#if _PPC970_
    // stw is not cracked, so, sadly, three.
    x_nop();
    x_nop();
    x_nop();
#endif
    lfd(dest, stackPointerRegister, 0);
    addi(stackPointerRegister, stackPointerRegister, 8);
}

void
MacroAssemblerPPC::ma_liNegZero(FloatRegister dest)
{
	ma_lis(dest, -0.0);
}

void
MacroAssemblerPPC::ma_mv(FloatRegister src, ValueOperand dest)
{
	// Remember: big endian!
	stfdu(src, stackPointerRegister, -8); // cracked on G5
#if _PPC970_
	x_nop();
	x_nop();
#endif
	lwz(dest.typeReg(), stackPointerRegister, 0);
	lwz(dest.payloadReg(), stackPointerRegister, 4);
	addi(stackPointerRegister, stackPointerRegister, 8);
}

void
MacroAssemblerPPC::ma_mv(ValueOperand src, FloatRegister dest)
{
	addi(stackPointerRegister, stackPointerRegister, -8);
	// Endianness!
	stw(src.typeReg(), stackPointerRegister, 0);
	stw(src.payloadReg(), stackPointerRegister, 4);
#if _PPC970_
	// Hard to say where they will end up, so assume the worst on instruction timing.
	x_nop();
	x_nop();
	x_nop();
#endif
	lfd(dest, stackPointerRegister, 0);
	addi(stackPointerRegister, stackPointerRegister, 8);
}

void
MacroAssemblerPPC::ma_ls(FloatRegister ft, Address address)
{
    if (Imm16::IsInSignedRange(address.offset)) {
        lfs(ft, address.base, address.offset);
    } else {
        MOZ_ASSERT(address.base != addressTempRegister);
        x_li32(addressTempRegister, address.offset);
        lfsx(ft, address.base, addressTempRegister);
    }
}

void
MacroAssemblerPPC::ma_ld(FloatRegister ft, Address address)
{
    if (Imm16::IsInSignedRange(address.offset)) {
        lfd(ft, address.base, address.offset);
    } else {
        MOZ_ASSERT(address.base != addressTempRegister);
        x_li32(addressTempRegister, address.offset);
        lfdx(ft, address.base, addressTempRegister);
    }
}

void
MacroAssemblerPPC::ma_sd(FloatRegister ft, Address address)
{
    if (Imm16::IsInSignedRange(address.offset)) {
        stfd(ft, address.base, address.offset);
    } else {
        MOZ_ASSERT(address.base != tempRegister);
        x_li32(tempRegister, address.offset);
        stfdx(ft, address.base, tempRegister);
    }
}

void
MacroAssemblerPPC::ma_sd(FloatRegister ft, BaseIndex address)
{
    computeScaledAddress(address, addressTempRegister);
    ma_sd(ft, Address(addressTempRegister, address.offset));
}

void
MacroAssemblerPPC::ma_ss(FloatRegister ft, Address address)
{
    if (Imm16::IsInSignedRange(address.offset)) {
        stfs(ft, address.base, address.offset);
    } else {
        MOZ_ASSERT(address.base != tempRegister);
        x_li32(tempRegister, address.offset);
        stfsx(ft, address.base, tempRegister);
    }
}

void
MacroAssemblerPPC::ma_ss(FloatRegister ft, BaseIndex address)
{
    MOZ_ASSERT(address.base != addressTempRegister);
    computeScaledAddress(address, addressTempRegister);
    ma_ss(ft, Address(addressTempRegister, address.offset));
}

void
MacroAssemblerPPC::ma_pop(FloatRegister fs)
{
	// Always double precision
	lfd(fs, stackPointerRegister, 0);
	addi(stackPointerRegister, stackPointerRegister, 8);
}

void
MacroAssemblerPPC::ma_push(FloatRegister fs)
{
	// Always double precision
	stfdu(fs, stackPointerRegister, -8); // cracked on G5
}

Assembler::Condition
MacroAssemblerPPC::ma_cmp(Register lhs, Register rhs, Assembler::Condition c)
{
	// Replaces cmp32.
	// DO NOT CALL computeConditionCode here. We don't have enough context.
	// Work with the raw condition only.
	
	MOZ_ASSERT(!(c & ConditionOnlyXER) && c != Always);
	
	if ((c == Zero || c == NonZero || c == Signed || c == NotSigned) && (lhs == rhs)) {
		// If same register, equality may cause this to pass, so we bit test or explicitly
		// compare to immediate zero for these special cases.
		// If different registers, invariably the rhs is already zero, so we
		// simply allow it to be interpreted as Equal/NotEqual/LessThan etc. as usual.
		// Usually this comes from ma_cmp(reg, 0, c) below.
		cmpwi(lhs, 0);
	} else if (PPC_USE_UNSIGNED_COMPARE(c)) {
		cmplw(lhs, rhs);
	} else {
		cmpw(lhs, rhs);
	}
	// For future expansion.
	return c;
}

// XXX: Consider a version for ImmTag/ImmType that is always unsigned.
Assembler::Condition
MacroAssemblerPPC::ma_cmp(Register lhs, Imm32 rhs, Condition c)
{
	MOZ_ASSERT(!(c & ConditionOnlyXER) && c != Always);
	
	// Because lhs could be either tempRegister or addressTempRegister
	// depending on what's loading it, we have to handle both situations.
	// (Damn r0 restrictions.)
	Register t = (lhs == tempRegister) ? addressTempRegister : tempRegister;
	
	if (PPC_USE_UNSIGNED_COMPARE(c)) {
		if (!Imm16::IsInUnsignedRange(rhs.value)) {	
			x_li32(t, rhs.value);
			return ma_cmp(lhs, t, c);
		}
		cmplwi(lhs, rhs.value);
	} else {
		if (!Imm16::IsInSignedRange(rhs.value)) {
			x_li32(t, rhs.value); // sign extends
			return ma_cmp(lhs, t, c);
		}
		cmpwi(lhs, rhs.value);
	}
	// For future expansion.
	return c;
}

Assembler::Condition
MacroAssemblerPPC::ma_cmp(Register lhs, Address rhs, Assembler::Condition c)
{
	Register t = (lhs == tempRegister) ? addressTempRegister : tempRegister;
	ma_lw(t, rhs);
	return ma_cmp(lhs, t, c);
}
Assembler::Condition
MacroAssemblerPPC::ma_cmp(Register lhs, ImmPtr rhs, Assembler::Condition c)
{
	return ma_cmp(lhs, Imm32((uint32_t)rhs.value), c);
}
Assembler::Condition
MacroAssemblerPPC::ma_cmp(Register lhs, ImmGCPtr rhs, Assembler::Condition c)
{
	// ImmGCPtrs must always be patchable, so optimization is not helpful.
	Register t = (lhs == tempRegister) ? addressTempRegister : tempRegister;
	ma_li32(t, rhs);
	return ma_cmp(lhs, t, c);
}
Assembler::Condition
MacroAssemblerPPC::ma_cmp(Address lhs, Imm32 rhs, Assembler::Condition c)
{
	ma_lw(addressTempRegister, lhs);
	return ma_cmp(addressTempRegister, rhs, c);
}
Assembler::Condition
MacroAssemblerPPC::ma_cmp(Address lhs, Register rhs, Assembler::Condition c)
{
	Register t = (rhs == tempRegister) ? addressTempRegister : tempRegister;
	ma_lw(t, lhs);
	return ma_cmp(t, rhs, c);
}
Assembler::Condition
MacroAssemblerPPC::ma_cmp(Address lhs, ImmGCPtr rhs, Assembler::Condition c)
{
	ma_lw(addressTempRegister, lhs);
	ma_li32(tempRegister, rhs); // MUST BE PATCHABLE
	return ma_cmp(addressTempRegister, tempRegister, c);
}

Assembler::DoubleCondition
MacroAssemblerPPC::ma_fcmp(FloatRegister lhs, FloatRegister rhs, Assembler::DoubleCondition c)
{
	fcmpu(lhs, rhs);
	// For future expansion.
	return c;
}

// In the below, CR0 is assumed.
void
MacroAssemblerPPC::ma_cr_set_slow(Assembler::Condition c, Register dest)
{
	// If the result of the condition is true, set dest to 1; else
	// set it to zero.
	//
	// General case, which is branchy and sucks.

	x_li(dest, 1);
	BufferOffset end = _bc(0, c);
	x_li(dest, 0);
	
	bindSS(end);
}
void
MacroAssemblerPPC::ma_cr_set_slow(Assembler::DoubleCondition c, Register dest)
{
	// If the result of the condition is true, set dest to 1; else
	// set it to zero.
	// Almost exactly the same, just for doubles.
	//
	// General case, which is branchy and sucks.

	x_li(dest, 1);
	BufferOffset end = _bc(0, c);
	x_li(dest, 0);
	
        bindSS(end);
}
void
MacroAssemblerPPC::ma_cr_set(Assembler::Condition cond, Register dest)
{
	// Fast paths.
	// Extract the relevant bits from CR0 as int.
	if (cond == Assembler::Equal) {
		MFCR0(r0);
		rlwinm(dest, r0, 3, 31, 31); // get CR0[EQ]
	} else if (cond == Assembler::NotEqual) {
		// Same, but inverted with the xori (flip the bit).
		MFCR0(r0);
		rlwinm(r0, r0, 3, 31, 31); // get CR0[EQ]
		xori(dest, r0, 1); // flip sign into the payload reg
	} else if (cond == Assembler::GreaterThan) {
		MFCR0(r0);
		rlwinm(dest, r0, 2, 31, 31); // get CR0[GT]
	} else if (cond == Assembler::LessThanOrEqual) {
		// Inverse (not reverse).
		MFCR0(r0);
		rlwinm(r0, r0, 2, 31, 31); // get CR0[GT]
		xori(dest, r0, 1); // flip sign into the payload reg
	} else if (cond == Assembler::LessThan) {
		MFCR0(r0);
		rlwinm(dest, r0, 1, 31, 31); // get CR0[LT]
	} else if (cond == Assembler::GreaterThanOrEqual) {
		MFCR0(r0);
		rlwinm(r0, r0, 1, 31, 31); // get CR0[LT]
		xori(dest, r0, 1); // flip sign into the payload reg
	} else {
		// Use the branched version to cover other things.
		ma_cr_set_slow(cond, dest);
	}
}
void
MacroAssemblerPPC::ma_cr_set(Assembler::DoubleCondition cond, Register dest)
{
	bool isUnordered;

	// Check for simple ordered/unordered before checking synthetic codes.
	if (cond == Assembler::DoubleUnordered) {
		MFCR0(r0);
		rlwinm(dest, r0, 4, 31, 31); // get CR0[FU]. FU! FUUUUUUUUUU-
	} else if (cond == Assembler::DoubleOrdered) {
		// Same, but with the xori (flip the bit).
		MFCR0(r0);
		rlwinm(r0, r0, 4, 31, 31);
		xori(dest, r0, 1); // flip sign into the payload reg
	} else {
		// Possibly a synthetic condition code.
		// Evaluate the same way we do for the branched form and
		// emit needed condregops for the tests to succeed.

		Assembler::DoubleCondition baseDCond = Assembler::computeConditionCode(cond);
		if (baseDCond == Assembler::DoubleEqual) {
			MFCR0(r0);
			rlwinm(dest, r0, 3, 31, 31); // get CR0[FE]
		} else if (baseDCond == Assembler::DoubleNotEqual) {
			MFCR0(r0);
			rlwinm(r0, r0, 3, 31, 31); // get CR0[FE]
			xori(dest, r0, 1); // flip sign into the payload reg
		} else if (baseDCond == Assembler::DoubleGreaterThan) {
			MFCR0(r0);
			rlwinm(dest, r0, 2, 31, 31); // get CR0[FG]
		} else if (baseDCond == Assembler::DoubleLessThanOrEqual) {
			MFCR0(r0);
			rlwinm(r0, r0, 2, 31, 31); // get CR0[FG]
			xori(dest, r0, 1); // flip sign into the payload reg
		} else if (baseDCond == Assembler::DoubleLessThan) {
			MFCR0(r0);
			rlwinm(dest, r0, 1, 31, 31); // get CR0[FL]
		} else if (baseDCond == Assembler::DoubleGreaterThanOrEqual) {
			MFCR0(r0);
			rlwinm(r0, r0, 1, 31, 31); // get CR0[FL]
			xori(dest, r0, 1); // flip sign into the payload reg
		} else {
			// Use the branched version to cover other things.
			ma_cr_set_slow(baseDCond, dest);
		}
	}
}

void
MacroAssemblerPPC::ma_cmp_set(Assembler::Condition cond, Register lhs, Register rhs, Register dest) {
	// Fast paths, PowerPC Compiler Writer's Guide, Appendix D et al. These
	// avoid use of CR, which could be slow (on G5, mfcr is microcoded).
	// Due to possibly constrained register usage, we don't use the optimals,
	// especially since we may be called with at least one temporary register.
	// TODO: Add subfe and subfze to support unsigned Above/Below, though
	// these are probably used a lot less.
	
	ispew("ma_cmp_set(cond, reg, reg, reg)");
	MOZ_ASSERT(lhs != tempRegister);
	MOZ_ASSERT(rhs != tempRegister);
	MOZ_ASSERT(dest != tempRegister);
	
	if (cond == Assembler::Equal) {
		subf(r0, rhs, lhs); // p.141
		cntlzw(r0, r0);
		x_srwi(dest, r0, 5);
	} else if (cond == Assembler::NotEqual) {
		subf(r0, lhs, rhs); // p.58
		subf(dest, rhs, lhs);
		or_(dest, dest, r0);
		rlwinm(dest, dest, 1, 31, 31);
	} else if (cond == Assembler::LessThan) { // SIGNED
		subfc(r0, rhs, lhs); // p.200
		eqv(dest, rhs, lhs);
		x_srwi(r0, dest, 31);
		addze(dest, r0);
		rlwinm(dest, dest, 0, 31, 31);
	} else if (cond == Assembler::GreaterThan) { // SIGNED
		// Reverse (not inverse).
		subfc(r0, lhs, rhs);
		eqv(dest, lhs, rhs);
		x_srwi(r0, dest, 31);
		addze(dest, r0);
		rlwinm(dest, dest, 0, 31, 31);
	} else if (cond == Assembler::LessThanOrEqual) { // SIGNED
		// We need to recruit the emergency temporary register for the next two tests
		// in case r12 is in use.
		x_srwi(r0, lhs, 31); // p.200
		srawi(emergencyTempRegister, rhs, 31);
		subfc(dest, lhs, rhs); // We can clobber dest here.
		adde(dest, emergencyTempRegister, r0);
	} else if (cond == Assembler::GreaterThanOrEqual) { // SIGNED
		// Reverse (not inverse).
		x_srwi(r0, rhs, 31);
		srawi(emergencyTempRegister, lhs, 31);
		subfc(dest, rhs, lhs); // We can clobber dest here.
		adde(dest, emergencyTempRegister, r0);
	} else {
		// Use the branched version as a slow path for any condition.
		ma_cr_set(ma_cmp(lhs, rhs, cond), dest);
	}
}

void
MacroAssemblerPPC::ma_cmp_set(Assembler::Condition cond, Address lhs, ImmPtr rhs, Register dest) {
	MOZ_ASSERT(lhs.base != tempRegister);
	MOZ_ASSERT(lhs.base != addressTempRegister);

	// Damn. Recruit a temporary register and spill it. r3 is good enough.
	stwu(r3, stackPointerRegister, -4);
	ma_lw(r3, lhs);
	x_li32(addressTempRegister, (uint32_t)rhs.value);
	ma_cmp_set(cond, r3, addressTempRegister, dest);
	lwz(r3, stackPointerRegister, 0);
	addi(stackPointerRegister, stackPointerRegister, 4);
}

void
MacroAssemblerPPC::ma_cmp_set(Assembler::Condition cond, Register lhs, Imm32 rhs, Register dest) {
	MOZ_ASSERT(lhs != tempRegister);

	x_li32(addressTempRegister, rhs.value);
	ma_cmp_set(cond, lhs, addressTempRegister, dest);
}
void
MacroAssemblerPPC::ma_cmp_set(Assembler::Condition cond, Register lhs, ImmPtr rhs, Register dest) {
	MOZ_ASSERT(lhs != addressTempRegister);

	x_li32(addressTempRegister, (uint32_t)rhs.value);
	ma_cmp_set(cond, lhs, addressTempRegister, dest);
}
void
MacroAssemblerPPC::ma_cmp_set(Assembler::Condition cond, Register lhs, Address rhs, Register dest) {
	MOZ_ASSERT(lhs != tempRegister);
	MOZ_ASSERT(lhs != addressTempRegister);
	MOZ_ASSERT(rhs.base != tempRegister);
	MOZ_ASSERT(rhs.base != addressTempRegister);

	ma_lw(addressTempRegister, rhs);
	ma_cmp_set(cond, lhs, addressTempRegister, dest);
}

void
MacroAssemblerPPC::ma_cmp_set(Assembler::DoubleCondition cond, FloatRegister lhs, FloatRegister rhs, Register dest) {
	ma_cr_set(ma_fcmp(lhs, rhs, cond), dest);
}

MacroAssembler&
MacroAssemblerPPC::asMasm()
{
    return *static_cast<MacroAssembler*>(this);
}

const MacroAssembler&
MacroAssemblerPPC::asMasm() const
{
    return *static_cast<const MacroAssembler*>(this);
}

#if(0)
bool
MacroAssemblerPPCCompat::buildFakeExitFrame(Register scratch, uint32_t *offset)
{
	ispew("[[ buildFakeExitFrame(reg, offs)");
    mozilla::DebugOnly<uint32_t> initialDepth = asMasm().framePushed();

    CodeLabel cl;
    ma_li32(scratch, cl.dest());

    uint32_t descriptor = MakeFrameDescriptor(asMasm().framePushed(), JitFrame_IonJS);
    asMasm().Push(Imm32(descriptor));
    asMasm().Push(scratch);

    bind(cl.src());
    *offset = currentOffset();

    MOZ_ASSERT(asMasm().framePushed() == initialDepth + ExitFrameLayout::Size());
    ispew("   buildFakeExitFrame(reg, offs) ]]");
    return addCodeLabel(cl);
}
#endif

bool
MacroAssemblerPPCCompat::buildOOLFakeExitFrame(void *fakeReturnAddr)
{
	ispew("[[ buildOOLFakeExitFrame(void *)");
    uint32_t descriptor = MakeFrameDescriptor(asMasm().framePushed(), JitFrame_IonJS);

    asMasm().Push(Imm32(descriptor)); // descriptor_
    asMasm().Push(ImmPtr(fakeReturnAddr));

	ispew("   buildOOLFakeExitFrame(void *) ]]");
    return true;
}

void
MacroAssemblerPPCCompat::callWithExitFrame(Label *target)
{
	ispew("[[ callWithExitFrame(l)");
    uint32_t descriptor = MakeFrameDescriptor(asMasm().framePushed(), JitFrame_IonJS);
    asMasm().Push(Imm32(descriptor)); // descriptor

    ma_callJitHalfPush(target);
    ispew("   callWithExitFrame(l) ]]");
}

void
MacroAssemblerPPCCompat::callWithExitFrame(JitCode *target)
{
	ispew("[[ callWithExitFrame(jitcode)");
    uint32_t descriptor = MakeFrameDescriptor(asMasm().framePushed(), JitFrame_IonJS);
    asMasm().Push(Imm32(descriptor)); // descriptor

    addPendingJump(m_buffer.nextOffset(), ImmPtr(target->raw()), Relocation::JITCODE);
    ma_li32Patchable(addressTempRegister, ImmPtr(target->raw()));
    ma_callJitHalfPush(addressTempRegister);
    ispew("   callWithExitFrame(jitcode) ]]");
}

void
MacroAssemblerPPCCompat::callWithExitFrame(JitCode *target, Register dynStack)
{
	ispew("[[ callWithExitFrame(jitcode, reg)");
    ma_addu(dynStack, dynStack, Imm32(asMasm().framePushed()));
    makeFrameDescriptor(dynStack, JitFrame_IonJS);
    asMasm().Push(dynStack); // descriptor

    addPendingJump(m_buffer.nextOffset(), ImmPtr(target->raw()), Relocation::JITCODE);
    ma_li32Patchable(addressTempRegister, ImmPtr(target->raw()));
    ma_callJitHalfPush(addressTempRegister);
    ispew("   callWithExitFrame(jitcode, reg) ]]");
}

void
MacroAssemblerPPCCompat::callJit(Register callee)
{
	ma_callJit(callee);
}
void
MacroAssemblerPPCCompat::callJitFromAsmJS(Register callee) // XXX
{
    ma_callJitNoPush(callee);

    // The Ion ABI has the callee pop the return address off the stack.
    // The asm.js caller assumes that the call leaves sp unchanged, so bump
    // the stack.
    subPtr(Imm32(sizeof(void*)), stackPointerRegister);
}

void
MacroAssembler::reserveStack(uint32_t amount)
{
    if (amount)
        ma_subu(stackPointerRegister, stackPointerRegister, Imm32(amount));
    adjustFrame(amount);
}

void
MacroAssembler::PushRegsInMask(LiveRegisterSet set)
{    
	// TODO: We don't need two reserveStacks.
	int32_t diffF = set.fpus().size() * sizeof(double);
	int32_t diffG = set.gprs().size() * sizeof(intptr_t);
	reserveStack(diffG);
	for (GeneralRegisterBackwardIterator iter(set.gprs()); iter.more(); iter++) {
	    diffG -= sizeof(intptr_t);
	    if (*iter == lr) { // "Fake LR"
	    	x_mflr(tempRegister);
	    	stw(tempRegister, stackPointerRegister, diffG);
	    } else
	    storePtr(*iter, Address(StackPointer, diffG));
	}
	MOZ_ASSERT(diffG == 0);
	reserveStack(diffF);
	for (FloatRegisterBackwardIterator iter(set.fpus()); iter.more(); iter++) {
	    diffF -= sizeof(double);
	    storeDouble(*iter, Address(StackPointer, diffF));
	}
	MOZ_ASSERT(diffF == 0);
}

void
MacroAssembler::PopRegsInMaskIgnore(LiveRegisterSet set, LiveRegisterSet ignore)
{
	int32_t diffG = set.gprs().size() * sizeof(intptr_t);
	int32_t diffF = set.fpus().size() * sizeof(double);
	const int32_t reservedG = diffG;
	const int32_t reservedF = diffF;

	// TODO: We don't need two freeStacks.
	{
	    for (FloatRegisterBackwardIterator iter(set.fpus()); iter.more(); iter++) {
	        diffF -= sizeof(double);
	        if (!ignore.has(*iter))
	            loadDouble(Address(StackPointer, diffF), *iter);
	    }
	    freeStack(reservedF);
	}
	MOZ_ASSERT(diffF == 0);
	{
	    for (GeneralRegisterBackwardIterator iter(set.gprs()); iter.more(); iter++) {
	        diffG -= sizeof(intptr_t);
	        if (!ignore.has(*iter)) {
	        	if (*iter == lr) { // Fake "LR"
	        		lwz(tempRegister, stackPointerRegister, diffG);
	        		x_mtlr(tempRegister);
	        	} else
	            loadPtr(Address(StackPointer, diffG), *iter);
	        }
	    }
	    freeStack(reservedG);
	}
	MOZ_ASSERT(diffG == 0);
}

void
MacroAssemblerPPCCompat::add32(Register src, Register dest)
{
	ispew("add32(reg, reg)");
    add(dest, dest, src);
}

void
MacroAssemblerPPCCompat::add32(Imm32 imm, Register dest)
{
	ispew("add32(imm, reg)");
    ma_addu(dest, dest, imm);
}

void
MacroAssemblerPPCCompat::add32(Imm32 imm, const Address &dest)
{
	ispew("[[ add32(imm, adr)");
    load32(dest, addressTempRegister);
    ma_addu(addressTempRegister, imm);
    store32(addressTempRegister, dest);
    ispew("   add32(imm, adr) ]]");
}

#if (0)
// Moved to MacroAssembler-ppc-inl.h
void
MacroAssemblerPPCCompat::add64(Imm32 imm, Register64 dest)
{
    ispew("[[ add64(imm, r64)");
    // This is pretty simple because it's a 32-bit integer, so we just do add with carry.
    x_li32(tempRegister, imm.value);
    addc(dest.low, dest.low, tempRegister); /* XXX: optimize to addic if possible */
    addze(dest.high, dest.high);
    ispew("   add64(imm, r64) ]]");
}

void
MacroAssemblerPPCCompat::sub32(Imm32 imm, Register dest)
{
	ispew("sub32(imm, reg)");
    ma_subu(dest, dest, imm);
}

void
MacroAssemblerPPCCompat::sub32(Register src, Register dest)
{
	ispew("sub32(reg, reg)");
    ma_subu(dest, dest, src);
}
#endif

void
MacroAssemblerPPCCompat::addPtr(Register src, Register dest)
{
	ispew("addPtr(reg, reg");
    ma_addu(dest, src);
}

void
MacroAssemblerPPCCompat::addPtr(const Address &src, Register dest)
{
	ispew("[[ addPtr(adr, reg)");
    loadPtr(src, tempRegister);
    ma_addu(dest, tempRegister);
    ispew("   addPtr(adr, reg)");
}

void
MacroAssemblerPPCCompat::subPtr(Register src, Register dest)
{
	ispew("subPtr(reg, reg)");
    ma_subu(dest, dest, src);
}

void
MacroAssemblerPPCCompat::move32(Imm32 imm, Register dest)
{
	ispew("move32(imm, reg)");
    ma_li32(dest, imm);
}

void
MacroAssemblerPPCCompat::move32(Register src, Register dest)
{
	ispew("move32(reg, reg)");
    x_mr(dest, src);
}

void
MacroAssemblerPPCCompat::movePtr(Register src, Register dest)
{
	ispew("movePtr(reg, reg)");
    x_mr(dest, src);
}
void
MacroAssemblerPPCCompat::movePtr(ImmWord imm, Register dest)
{
	ispew("movePtr(immw, reg)");
    ma_li32(dest, Imm32(imm.value));
}

void
MacroAssemblerPPCCompat::movePtr(ImmGCPtr imm, Register dest)
{
	ispew("movePtr(immgcptr, reg)");
    ma_li32(dest, imm);
}

#if(0)
void
MacroAssemblerPPCCompat::movePtr(ImmMaybeNurseryPtr imm, Register dest)
{
	ispew("movePtr(nursery, reg)");
    movePtr(noteMaybeNurseryPtr(imm), dest);
}
#endif
void
MacroAssemblerPPCCompat::movePtr(ImmPtr imm, Register dest)
{
	ispew("movePtr(immptr, reg)");
    movePtr(ImmWord(uintptr_t(imm.value)), dest);
}
void
MacroAssemblerPPCCompat::movePtr(wasm::SymbolicAddress imm, Register dest)
{
	ispew("movePtr(wasmsa, reg)");
    append(AsmJSAbsoluteLink(CodeOffset(nextOffset().getOffset()), imm));
    ma_li32Patchable(dest, Imm32(-1));
}

void
MacroAssemblerPPCCompat::load8ZeroExtend(const Address &address, Register dest)
{
	ispew("load8ZeroExtend(adr, reg)");
    ma_load(dest, address, SizeByte, ZeroExtend);
}

void
MacroAssemblerPPCCompat::load8ZeroExtend(const BaseIndex &src, Register dest)
{
	ispew("load8ZeroExtend(bi, reg)");
    ma_load(dest, src, SizeByte, ZeroExtend);
}

void
MacroAssemblerPPCCompat::load8SignExtend(const Address &address, Register dest)
{
	ispew("load8SignExtend(adr, reg)");
    ma_load(dest, address, SizeByte, SignExtend);
}

void
MacroAssemblerPPCCompat::load8SignExtend(const BaseIndex &src, Register dest)
{
	ispew("load8SignExtend(bi, reg)");
    ma_load(dest, src, SizeByte, SignExtend);
}

void
MacroAssemblerPPCCompat::load16ZeroExtend(const Address &address, Register dest)
{
	ispew("load16ZeroExtend(adr, reg)");
    ma_load(dest, address, SizeHalfWord, ZeroExtend);
}

void
MacroAssemblerPPCCompat::load16ZeroExtend(const BaseIndex &src, Register dest)
{
	ispew("load16ZeroExtend(bi, reg)");
    ma_load(dest, src, SizeHalfWord, ZeroExtend);
}

void
MacroAssemblerPPCCompat::load16SignExtend(const Address &address, Register dest)
{
	ispew("load16SignExtend(adr, reg)");
    ma_load(dest, address, SizeHalfWord, SignExtend);
}

void
MacroAssemblerPPCCompat::load16SignExtend(const BaseIndex &src, Register dest)
{
	ispew("load16SignExtend(bi, reg)");
    ma_load(dest, src, SizeHalfWord, SignExtend);
}

void
MacroAssemblerPPCCompat::load32(const Address &address, Register dest)
{
	ispew("load32(adr, reg)");
    ma_lw(dest, address);
}

void
MacroAssemblerPPCCompat::load32(const BaseIndex &address, Register dest)
{
	ispew("load32(bi, reg)");
    ma_load(dest, address, SizeWord);
}

void
MacroAssemblerPPCCompat::load32(AbsoluteAddress address, Register dest)
{
	ispew("load32(aadr, reg");
    x_li32(addressTempRegister, (uint32_t)address.addr);
    lwz(dest, addressTempRegister, 0);
}

void
MacroAssemblerPPCCompat::loadPtr(const Address &address, Register dest)
{
	ispew("loadPtr(adr, reg)");
    ma_lw(dest, address);
}

void
MacroAssemblerPPCCompat::loadPtr(const BaseIndex &src, Register dest)
{
	ispew("loadPtr(bi, reg)");
    load32(src, dest);
}

void
MacroAssemblerPPCCompat::loadPtr(AbsoluteAddress address, Register dest)
{
	ispew("loadPtr(aadr, reg");
	load32(address, dest);
}

void
MacroAssemblerPPCCompat::loadPtr(wasm::SymbolicAddress address, Register dest)
{
	ispew("loadPtr(wasmsa, reg)");
    movePtr(address, tempRegister);
    loadPtr(Address(tempRegister, 0x0), dest);
}

void
MacroAssemblerPPCCompat::loadPrivate(const Address &address, Register dest)
{
	ispew("loadPrivate(adr, reg)");
	MOZ_ASSERT(PAYLOAD_OFFSET == 4); // wtf if not
    ma_lw(dest, Address(address.base, address.offset + PAYLOAD_OFFSET));
}

void
MacroAssemblerPPCCompat::loadDouble(const Address &address, FloatRegister dest)
{
	ispew("loadDouble(adr, fpr)");
    ma_ld(dest, address);
}

void
MacroAssemblerPPCCompat::loadDouble(const BaseIndex &src, FloatRegister dest)
{
	ispew("loadDouble(bi, fpr)");
    computeScaledAddress(src, addressTempRegister);
    ma_ld(dest, Address(addressTempRegister, src.offset));
}

void
MacroAssemblerPPCCompat::loadFloatAsDouble(const Address &address, FloatRegister dest)
{
	ispew("loadFloatAsDouble(adr, fpr)");
    ma_ls(dest, address);
    // The FPU automatically converts to double precision for us.
}

void
MacroAssemblerPPCCompat::loadFloatAsDouble(const BaseIndex &src, FloatRegister dest)
{
	ispew("loadFloat32(bi, fpr)");
    loadFloat32(src, dest);
	// The FPU automatically converts to double precision for us.
}

void
MacroAssemblerPPCCompat::loadFloat32(const Address &address, FloatRegister dest)
{
	ispew("loadFloat32(adr, fpr)");
    ma_ls(dest, address);
}

void
MacroAssemblerPPCCompat::loadFloat32(const BaseIndex &src, FloatRegister dest)
{
	ispew("loadFloat32(bi, fpr)");
    computeScaledAddress(src, addressTempRegister);
    ma_ls(dest, Address(addressTempRegister, src.offset));
}

void
MacroAssemblerPPCCompat::store8(Imm32 imm, const Address &address)
{
	ispew("store8(imm, adr)");
    ma_li32(tempRegister, imm);
    ma_store(tempRegister, address, SizeByte);
}

void
MacroAssemblerPPCCompat::store8(Register src, const Address &address)
{
	ispew("store8(reg, adr)");
    ma_store(src, address, SizeByte);
}

void
MacroAssemblerPPCCompat::store8(Imm32 imm, const BaseIndex &dest)
{
	ispew("store8(imm, bi)");
    ma_store(imm, dest, SizeByte);
}

void
MacroAssemblerPPCCompat::store8(Register src, const BaseIndex &dest)
{
	ispew("store8(reg, bi)");
    ma_store(src, dest, SizeByte);
}

void
MacroAssemblerPPCCompat::store16(Imm32 imm, const Address &address)
{
	ispew("store16(imm, adr)");
    ma_li32(tempRegister, imm);
    ma_store(tempRegister, address, SizeHalfWord);
}

void
MacroAssemblerPPCCompat::store16(Register src, const Address &address)
{
	ispew("store16(reg, adr)");
    ma_store(src, address, SizeHalfWord);
}

void
MacroAssemblerPPCCompat::store16(Imm32 imm, const BaseIndex &dest)
{
	ispew("store16(imm, bi)");
    ma_store(imm, dest, SizeHalfWord);
}

void
MacroAssemblerPPCCompat::store16(Register src, const BaseIndex &address)
{
	ispew("store16(reg, bi)");
    ma_store(src, address, SizeHalfWord);
}

void
MacroAssemblerPPCCompat::store16Swapped(Imm32 imm, const Address &address)
{
	ispew("store16Swapped(imm, adr)");
    ma_li32(tempRegister, imm);
    ma_store(tempRegister, address, SizeHalfWord, true);
}

void
MacroAssemblerPPCCompat::store16Swapped(Register src, const Address &address)
{
	ispew("store16Swapped(reg, adr)");
    ma_store(src, address, SizeHalfWord, true);
}

void
MacroAssemblerPPCCompat::store16Swapped(Imm32 imm, const BaseIndex &dest)
{
	ispew("store16Swapped(imm, bi)");
    ma_store(imm, dest, SizeHalfWord, true);
}

void
MacroAssemblerPPCCompat::store16Swapped(Register src, const BaseIndex &address)
{
	ispew("store16Swapped(reg, bi)");
    ma_store(src, address, SizeHalfWord, true);
}

void
MacroAssemblerPPCCompat::store32(Register src, AbsoluteAddress address)
{
	ispew("storePtr(reg, aadr)");
    storePtr(src, address);
}

void
MacroAssemblerPPCCompat::store32(Register src, const Address &address)
{
	ispew("storePtr(reg, adr");
    storePtr(src, address);
}

void
MacroAssemblerPPCCompat::store32(Imm32 src, const Address &address)
{
	ispew("store32(imm, adr)");
    move32(src, tempRegister);
    storePtr(tempRegister, address);
}

void
MacroAssemblerPPCCompat::store32(Imm32 imm, const BaseIndex &dest)
{
	ispew("store32(imm, bi)");
    ma_store(imm, dest, SizeWord);
}

void
MacroAssemblerPPCCompat::store32(Register src, const BaseIndex &dest)
{
	ispew("store32(reg, bi)");
    ma_store(src, dest, SizeWord);
}

void
MacroAssemblerPPCCompat::store32ByteSwapped(Register src, AbsoluteAddress address)
{
	ispew("store32ByteSwapped(reg, aadr)");
    // We need both temp registers live.
    // If this assertion does not hold, consider encoding zero for rA (see OPPCC)
    MOZ_ASSERT(src != addressTempRegister);
    MOZ_ASSERT(src != tempRegister);
    x_li32(addressTempRegister, (uint32_t)address.addr);
    ma_store(src, Address(addressTempRegister, 0), SizeWord, true);
}

void
MacroAssemblerPPCCompat::store32ByteSwapped(Register src, const Address &address)
{
	ispew("store32ByteSwapped(reg, adr");
    ma_store(src, address, SizeWord, true);
}

void
MacroAssemblerPPCCompat::store32ByteSwapped(Imm32 src, const Address &address)
{
	ispew("store32ByteSwapped(imm, adr)");
    move32(src, tempRegister);
    ma_store(tempRegister, address, SizeWord, true);
}

void
MacroAssemblerPPCCompat::store32ByteSwapped(Imm32 imm, const BaseIndex &dest)
{
	ispew("store32ByteSwapped(imm, bi)");
    ma_store(imm, dest, SizeWord, true);
}

void
MacroAssemblerPPCCompat::store32ByteSwapped(Register src, const BaseIndex &dest)
{
	ispew("store32ByteSwapped(reg, bi)");
    ma_store(src, dest, SizeWord, true);
}

template <typename T>
void
MacroAssemblerPPCCompat::storePtr(ImmWord imm, T address)
{
	ispew("storePtr(immw, T)");
    ma_li32(tempRegister, Imm32(imm.value));
    ma_sw(tempRegister, address);
}

template void MacroAssemblerPPCCompat::storePtr<Address>(ImmWord imm, Address address);
template void MacroAssemblerPPCCompat::storePtr<BaseIndex>(ImmWord imm, BaseIndex address);

template <typename T>
void
MacroAssemblerPPCCompat::storePtr(ImmPtr imm, T address)
{
	ispew("storePtr(immptr, T)");
    storePtr(ImmWord(uintptr_t(imm.value)), address);
}

template void MacroAssemblerPPCCompat::storePtr<Address>(ImmPtr imm, Address address);
template void MacroAssemblerPPCCompat::storePtr<BaseIndex>(ImmPtr imm, BaseIndex address);

template <typename T>
void
MacroAssemblerPPCCompat::storePtr(ImmGCPtr imm, T address)
{
	ispew("storePtr(immgcptr, T)");
    ma_li32(tempRegister, imm);
    ma_sw(tempRegister, address);
}

template void MacroAssemblerPPCCompat::storePtr<Address>(ImmGCPtr imm, Address address);
template void MacroAssemblerPPCCompat::storePtr<BaseIndex>(ImmGCPtr imm, BaseIndex address);

void
MacroAssemblerPPCCompat::storePtr(Register src, const Address &address)
{
	ispew("storePtr(reg, adr)");
    ma_sw(src, address);
}

void
MacroAssemblerPPCCompat::storePtr(Register src, const BaseIndex &address)
{
	ispew("storePtr(reg, bi)");
    ma_store(src, address, SizeWord);
}

void
MacroAssemblerPPCCompat::storePtr(Register src, AbsoluteAddress dest)
{
	ispew("storePtr(reg, aadr)");
    MOZ_ASSERT(src != addressTempRegister);
    x_li32(addressTempRegister, (uint32_t)dest.addr);
    stw(src, addressTempRegister, 0);
}

void
MacroAssemblerPPCCompat::clampIntToUint8(Register srcdest)
{
	ispew("[[ clampIntToUint8(reg)");
	// If < 0, return 0; if > 255, return 255; else return same.
	//
	// Exploit this property for a branchless version:
	// max(a,0) = (a + abs(a)) / 2 (do this first)
	// min(a,255) = (a + 255 - abs(a-255)) / 2
	// Use srcdest as temporary work area.

	// Save the original value first.
	x_mr(tempRegister, srcdest);

	// Next, compute max(original,0), starting with abs(original).
	// Absolute value routine from PowerPC Compiler Writer's Guide, p.50.
	srawi(srcdest, tempRegister, 31);
	xor_(addressTempRegister, srcdest, tempRegister);
	subf(srcdest, srcdest, addressTempRegister);
	// Finally, add original value and divide by 2. Leave in srcdest.
	add(srcdest, srcdest, tempRegister);
	srawi(srcdest, srcdest, 1);

	// Now for min(srcdest, 255). First, compute abs(a-255) into adrTemp.
	// We can clobber tempRegister safely now since we don't need the
	// original value anymore, but we need to push srcdest since we need
	// three working registers for the integer absolute value.
	//
	x_li32(addressTempRegister, 255);
	stwu(srcdest, stackPointerRegister, -4); // cracked on G5, don't care
	subf(addressTempRegister, addressTempRegister, srcdest); // T=B-A
	// Okay to clobber dest now ...
	srawi(srcdest, addressTempRegister, 31);
	xor_(tempRegister, srcdest, addressTempRegister);
	subf(addressTempRegister, srcdest, tempRegister);
	// Now 255 - addressTempRegister. Get srcdest back between instructions.
	x_li32(tempRegister, 255);
	// (There were adequate instructions between this lwz and the stwu to
	// keep them in separate G5 branch groups, so no nops are needed.)
	lwz(srcdest, stackPointerRegister, 0);
	subf(addressTempRegister, addressTempRegister, tempRegister); // T=B-A
	// Add srcdest back to addressTempRegister, and divide by 2.
	addi(stackPointerRegister, stackPointerRegister, 4);
	add(srcdest, srcdest, addressTempRegister);
	srawi(srcdest, srcdest, 1);

	// Ta-daaa!
	ispew("   clampIntToUint8(reg) ]]");
}

void
MacroAssembler::clampDoubleToUint8(FloatRegister input, Register output)
{
    ispew("[[ clampDoubleToUint8(fpr, reg)");
    MOZ_ASSERT(input != fpTempRegister);
    BufferOffset positive, done;

    loadConstantDouble(255, fpConversionRegister); // used below

    // <= 0 or NaN --> 0
    fsub(fpTempRegister, fpConversionRegister, fpConversionRegister);
    positive = _bc(0,
        ma_fcmp(input, fpTempRegister, DoubleGreaterThan));
    x_li(output, 0);
    done = _b(0);

    bindSS(positive);

    // We have a positive ordered value, so it's safe to use fsel. However,
    // the spec requires round to nearest ties to even, so we need a whole
    // special rounding function (can't use fp2int).
    fsub(fpTempRegister, input, fpConversionRegister);
    mtfsfi(7,0); // mode 0x00 // this is its own group
    // If (input - 255) <= 0, use input. If (input - 255) > 0, use 255.
    fsel(input, fpTempRegister, fpConversionRegister, input);
    // Now round. See fp2int for the rest of this. It is simplified here.
    fctiw(fpTempRegister, input); // new dispatch group
    stfdu(fpTempRegister, stackPointerRegister, -8);
    // No nop needed, the scheduling above forces groups apart.
    lwz(output, stackPointerRegister, 4);
    addi(stackPointerRegister, stackPointerRegister, 8);
    bindSS(done);
    ispew("   clampDoubleToUint8(fpr, reg) ]]");
}

void
MacroAssemblerPPCCompat::subPtr(Imm32 imm, const Register dest)
{
	ispew("subPtr(imm, reg)");
    ma_subu(dest, dest, imm);
}

void
MacroAssemblerPPCCompat::subPtr(const Address &addr, const Register dest)
{
	ispew("subPtr(adr, reg)");
    loadPtr(addr, tempRegister);
    subPtr(tempRegister, dest);
}

void
MacroAssemblerPPCCompat::subPtr(Register src, const Address &dest)
{
	ispew("subPtr(reg, adr)");
    loadPtr(dest, addressTempRegister);
    subPtr(src, addressTempRegister);
    storePtr(addressTempRegister, dest);
}

void
MacroAssemblerPPCCompat::addPtr(Imm32 imm, const Register dest)
{
	ispew("addPtr(imm, reg)");
    ma_addu(dest, imm);
}

void
MacroAssemblerPPCCompat::addPtr(Imm32 imm, const Address &dest)
{
	ispew("[[ addPtr(imm, adr)");
    loadPtr(dest, addressTempRegister);
    addPtr(imm, addressTempRegister);
    storePtr(addressTempRegister, dest);
    ispew("   addPtr(imm, adr) ]]");
}

void
MacroAssemblerPPCCompat::branchDouble(DoubleCondition cond, FloatRegister lhs,
                                       FloatRegister rhs, Label *label)
{
	ispew("branchDouble(cond, fpr, fpr, l)");
	ma_fcmp(lhs, rhs, cond); // CR0
	bc(cond, label);
}

void
MacroAssemblerPPCCompat::branchFloat(DoubleCondition cond, FloatRegister lhs,
                                      FloatRegister rhs, Label *label)
{
	ispew("branchFloat(cond, fpr, fpr, l)");
	branchDouble(cond, lhs, rhs, label);
}

// higher level tag testing code
Operand
MacroAssemblerPPC::ToPayload(Operand base)
{
	MOZ_ASSERT(PAYLOAD_OFFSET == 4); // big endian!!!
    return Operand(Register::FromCode(base.base()), base.disp() + PAYLOAD_OFFSET);
}

Operand
MacroAssemblerPPC::ToType(Operand base)
{
	MOZ_ASSERT(TAG_OFFSET == 0); // big endian!!!
    return Operand(Register::FromCode(base.base()), base.disp() + TAG_OFFSET);
}

void
MacroAssemblerPPCCompat::branchTestGCThing(Condition cond, const Address &address, Label *label)
{
	ispew("branchTestGCThing(cond, adr, l)");
    MOZ_ASSERT(cond == Equal || cond == NotEqual);
    Condition newc = (cond == Equal) ? AboveOrEqual : Below;
    extractTag(address, addressTempRegister);
    bc(ma_cmp(addressTempRegister, ImmTag(JSVAL_LOWER_INCL_TAG_OF_GCTHING_SET), newc), label);
}
void
MacroAssemblerPPCCompat::branchTestGCThing(Condition cond, const BaseIndex &src, Label *label)
{
	ispew("branchTestGCThing(cond, bi, l)");
    MOZ_ASSERT(cond == Equal || cond == NotEqual);
    Condition newc = (cond == Equal) ? AboveOrEqual : Below;
    extractTag(src, addressTempRegister);
    bc(ma_cmp(addressTempRegister, ImmTag(JSVAL_LOWER_INCL_TAG_OF_GCTHING_SET), newc), label);
}

void
MacroAssemblerPPCCompat::branchTestPrimitive(Condition cond, const ValueOperand &value,
                                              Label *label)
{
    branchTestPrimitive(cond, value.typeReg(), label);
}
void
MacroAssemblerPPCCompat::branchTestPrimitive(Condition cond, Register tag, Label *label)
{
	ispew("branchTestPrimitive(cond, reg, l)");
    MOZ_ASSERT(cond == Equal || cond == NotEqual);
    Condition newc = (cond == Equal) ? Below : AboveOrEqual;
    bc(ma_cmp(tag, ImmTag(JSVAL_UPPER_EXCL_TAG_OF_PRIMITIVE_SET), newc), label);
}

void
MacroAssemblerPPCCompat::branchTestInt32(Condition cond, const ValueOperand &value, Label *label)
{
	ispew("branchTestInt32(cond, vo, l)");
    branchTestInt32(cond, value.typeReg(), label);
}
void
MacroAssemblerPPCCompat::branchTestInt32(Condition cond, Register tag, Label *label)
{
	ispew("branchTestInt32(cond, reg, l)");
    MOZ_ASSERT(cond == Equal || cond == NotEqual);
    bc(ma_cmp(tag, ImmType(JSVAL_TYPE_INT32), (cond)), label);
}
void
MacroAssemblerPPCCompat::branchTestInt32(Condition cond, const Address &address, Label *label)
{
	ispew("branchTestInt32(cond, adr, l)");
    extractTag(address, addressTempRegister);
    branchTestInt32(cond, addressTempRegister, label);
}
void
MacroAssemblerPPCCompat::branchTestInt32(Condition cond, const BaseIndex &src, Label *label)
{
	ispew("branchTestInt32(cond, bi, l)");
    extractTag(src, addressTempRegister);
    branchTestInt32(cond, addressTempRegister, label);
}

void
MacroAssemblerPPCCompat::branchTestBoolean(Condition cond, const ValueOperand &value,
                                             Label *label)
{
	branchTestBoolean(cond, value.typeReg(), label);
}
void
MacroAssemblerPPCCompat::branchTestBoolean(Condition cond, Register tag, Label *label)
{
    ispew("branchTestBoolean(cond, reg, l)");
    MOZ_ASSERT(cond == Equal || cond == NotEqual);
    bc(ma_cmp(tag, ImmType(JSVAL_TYPE_BOOLEAN), (cond)), label);
}
void
MacroAssemblerPPCCompat::branchTestBoolean(Condition cond, const BaseIndex &src, Label *label)
{
	ispew("branchTestBoolean(cond, bi, l)");
    extractTag(src, addressTempRegister);
    branchTestBoolean(cond, addressTempRegister, label);
}
void
MacroAssemblerPPCCompat::branchTestBoolean(Condition cond, const Address &address, Label *label)
{
	ispew("branchTestBoolean(cond, adr, l)");
    extractTag(address, addressTempRegister);
    branchTestBoolean(cond, addressTempRegister, label);
}

void
MacroAssemblerPPCCompat::branchTestDouble(Condition cond, const ValueOperand &value, Label *label)
{
	branchTestDouble(cond, value.typeReg(), label);
}
void
MacroAssemblerPPCCompat::branchTestDouble(Condition cond, Register tag, Label *label)
{
	ispew("branchTestDouble(cond, reg, l)");
    MOZ_ASSERT(cond == Assembler::Equal || cond == NotEqual);
    Condition actual = (cond == Equal) ? Below : AboveOrEqual;
    bc(ma_cmp(tag, ImmTag(JSVAL_TAG_CLEAR), actual), label);
}
void
MacroAssemblerPPCCompat::branchTestDouble(Condition cond, const Address &address, Label *label)
{
	ispew("branchTestDouble(cond, adr, l)");
    extractTag(address, addressTempRegister);
    branchTestDouble(cond, addressTempRegister, label);
}
void
MacroAssemblerPPCCompat::branchTestDouble(Condition cond, const BaseIndex &src, Label *label)
{
	ispew("branchTestDouble(cond, bi, l)");
    extractTag(src, addressTempRegister);
    branchTestDouble(cond, addressTempRegister, label);
}

void
MacroAssemblerPPCCompat::branchTestNull(Condition cond, const ValueOperand &value, Label *label)
{
	branchTestNull(cond, value.typeReg(), label);
}
void
MacroAssemblerPPCCompat::branchTestNull(Condition cond, Register tag, Label *label)
{
	ispew("branchTestNull(cond, reg, l)");
    MOZ_ASSERT(cond == Equal || cond == NotEqual);
    bc(ma_cmp(tag, ImmTag(JSVAL_TAG_NULL), (cond)), label);
}
void
MacroAssemblerPPCCompat::branchTestNull(Condition cond, const Address &src, Label *label)
{
	ispew("branchTestNull(cond, adr, l)");
    extractTag(src, addressTempRegister);
    branchTestNull(cond, addressTempRegister, label);
}
void
MacroAssemblerPPCCompat::branchTestNull(Condition cond, const BaseIndex &src, Label *label)
{
	ispew("branchTestNull(cond, bi, l)");
    extractTag(src, addressTempRegister);
    branchTestNull(cond, addressTempRegister, label);
}
void
MacroAssemblerPPCCompat::testNullSet(Condition cond, const ValueOperand &value, Register dest)
{
	ispew("testNullSet(cond, vo, reg)");
    MOZ_ASSERT(cond == Equal || cond == NotEqual);
    ma_cr_set(ma_cmp(value.typeReg(), ImmType(JSVAL_TYPE_NULL), (cond)), dest);
}

void
MacroAssemblerPPCCompat::branchTestObject(Condition cond, const ValueOperand &value, Label *label)
{
    branchTestObject(cond, value.typeReg(), label);
}
void
MacroAssemblerPPCCompat::branchTestObject(Condition cond, Register tag, Label *label)
{
	ispew("branchTestObject(cond, reg, l)");
    MOZ_ASSERT(cond == Equal || cond == NotEqual);
    bc(ma_cmp(tag, ImmTag(JSVAL_TAG_OBJECT), (cond)), label);
}
void
MacroAssemblerPPCCompat::branchTestObject(Condition cond, const BaseIndex &src, Label *label)
{
	ispew("branchTestObject(cond, bi, l)");
    extractTag(src, addressTempRegister);
    branchTestObject(cond, addressTempRegister, label);
}
void
MacroAssemblerPPCCompat::branchTestObject(Condition cond, const Address& address, Label* label)
{
	ispew("branchTestObject(cond, adr, l)");
    MOZ_ASSERT(cond == Equal || cond == NotEqual);
    extractTag(address, addressTempRegister);
    branchTestObject(cond, addressTempRegister, label);
}

void
MacroAssemblerPPCCompat::testObjectSet(Condition cond, const ValueOperand &value, Register dest)
{
	ispew("testObjectSet(cond, vo, reg)");
    MOZ_ASSERT(cond == Equal || cond == NotEqual);
    ma_cr_set(ma_cmp(value.typeReg(), ImmType(JSVAL_TYPE_OBJECT), (cond)), dest);
}

void
MacroAssemblerPPCCompat::branchTestString(Condition cond, const ValueOperand &value, Label *label)
{
    branchTestString(cond, value.typeReg(), label);
}
void
MacroAssemblerPPCCompat::branchTestString(Condition cond, Register tag, Label *label)
{
	ispew("branchTestString(cond, reg, l)");
    MOZ_ASSERT(cond == Equal || cond == NotEqual);
    bc(ma_cmp(tag, ImmTag(JSVAL_TAG_STRING), (cond)), label);
}
void
MacroAssemblerPPCCompat::branchTestString(Condition cond, const BaseIndex &src, Label *label)
{
	ispew("branchTestString(cond, bi, l)");
    extractTag(src, addressTempRegister);
    branchTestString(cond, addressTempRegister, label);
}

void
MacroAssemblerPPCCompat::branchTestSymbol(Condition cond, const ValueOperand &value, Label *label)
{
    branchTestSymbol(cond, value.typeReg(), label);
}
void
MacroAssemblerPPCCompat::branchTestSymbol(Condition cond, const Register &tag, Label *label)
{
	ispew("branchTestSymbol(cond, reg, l)");
    MOZ_ASSERT(cond == Equal || cond == NotEqual);
    bc(ma_cmp(tag, ImmTag(JSVAL_TAG_SYMBOL), cond), label);
}
void
MacroAssemblerPPCCompat::branchTestSymbol(Condition cond, const BaseIndex &src, Label *label)
{
	ispew("branchTestSymbol(cond, bi, l)");
    extractTag(src, addressTempRegister);
    branchTestSymbol(cond, addressTempRegister, label);
}

void
MacroAssemblerPPCCompat::branchTestUndefined(Condition cond, const ValueOperand &value,
                                              Label *label)
{
    branchTestUndefined(cond, value.typeReg(), label);
}
void
MacroAssemblerPPCCompat::branchTestUndefined(Condition cond, Register tag, Label *label)
{
	ispew("branchTestUndefined(cond, reg, l)");
    MOZ_ASSERT(cond == Equal || cond == NotEqual);
    bc(ma_cmp(tag, ImmTag(JSVAL_TAG_UNDEFINED), (cond)), label);
}
void
MacroAssemblerPPCCompat::branchTestUndefined(Condition cond, const BaseIndex &src, Label *label)
{
	ispew("branchTestUndefined(cond, bi, l)");
    extractTag(src, addressTempRegister);
    branchTestUndefined(cond, addressTempRegister, label);
}
void
MacroAssemblerPPCCompat::branchTestUndefined(Condition cond, const Address &address, Label *label)
{
	ispew("branchTestUndefined(cond, adr, l)");
    extractTag(address, addressTempRegister);
    branchTestUndefined(cond, addressTempRegister, label);
}
void
MacroAssemblerPPCCompat::testUndefinedSet(Condition cond, const ValueOperand &value, Register dest)
{
	ispew("testUndefinedSet(cond, vo, reg)");
    MOZ_ASSERT(cond == Equal || cond == NotEqual);
    ma_cmp_set((cond), value.typeReg(), ImmType(JSVAL_TYPE_UNDEFINED), dest);
}

void
MacroAssemblerPPCCompat::branchTestNumber(Condition cond, const ValueOperand &value, Label *label)
{
    branchTestNumber(cond, value.typeReg(), label);
}
void
MacroAssemblerPPCCompat::branchTestNumber(Condition cond, Register tag, Label *label)
{
	ispew("branchTestNumber(cond, reg, l)");
    MOZ_ASSERT(cond == Equal || cond == NotEqual);
    bc(ma_cmp(tag, ImmTag(JSVAL_UPPER_INCL_TAG_OF_NUMBER_SET),
         cond == Equal ? BelowOrEqual : Above), label);
}

void
MacroAssemblerPPCCompat::branchTestMagic(Condition cond, const ValueOperand &value, Label *label)
{
    branchTestMagic(cond, value.typeReg(), label);
}
void
MacroAssemblerPPCCompat::branchTestMagic(Condition cond, Register tag, Label *label)
{
	ispew("branchTestMagic(cond, reg, l)");
    MOZ_ASSERT(cond == Equal || cond == NotEqual);
    bc(ma_cmp(tag, ImmTag(JSVAL_TAG_MAGIC), (cond)), label);
}
void
MacroAssemblerPPCCompat::branchTestMagic(Condition cond, const Address &address, Label *label)
{
	ispew("branchTestMagic(cond, adr, l)");
    extractTag(address, addressTempRegister);
    branchTestMagic(cond, addressTempRegister, label);
}
void
MacroAssemblerPPCCompat::branchTestMagic(Condition cond, const BaseIndex &src, Label *label)
{
	ispew("branchTestMagic(cond, bi, l)");
    extractTag(src, addressTempRegister);
    branchTestMagic(cond, addressTempRegister, label);
}

void
MacroAssemblerPPCCompat::branchTestValue(Condition cond, const ValueOperand &value,
                                          const Value &v, Label *label)
{
	ispew("branchTestValue(cond, vo, v, l)");
    moveData(v, tempRegister);
    // Unsigned comparisons??
	cmpw(value.payloadReg(), tempRegister);

    if (cond == Equal) {
        BufferOffset done = _bc(0, NotEqual);
        bc(ma_cmp(value.typeReg(), Imm32(getType(v)), Equal), label); 

        bindSS(done);
    } else {
        MOZ_ASSERT(cond == NotEqual);
        bc(NotEqual, label);
        bc(ma_cmp(value.typeReg(), Imm32(getType(v)), NotEqual), label);
    }
}
void
MacroAssemblerPPCCompat::branchTestValue(Condition cond, const Address &valaddr,
                                          const ValueOperand &value, Label *label)
{
	ispew("branchTestValue(cond, adr, vo, l)");
    MOZ_ASSERT(cond == Equal || cond == NotEqual);

    // Test tag.
    ma_lw(tempRegister, Address(valaddr.base, valaddr.offset + TAG_OFFSET));
    branchPtr(cond, tempRegister, value.typeReg(), label);

    // Test payload.
    ma_lw(tempRegister, Address(valaddr.base, valaddr.offset + PAYLOAD_OFFSET));
    branchPtr(cond, tempRegister, value.payloadReg(), label);
}

// Unboxing.
void
MacroAssemblerPPCCompat::unboxNonDouble(const ValueOperand &operand, Register dest)
{
	ispew("unboxNonDouble(vo, reg)");
    if (operand.payloadReg() != dest)
        x_mr(dest, operand.payloadReg());
}

void
MacroAssemblerPPCCompat::unboxNonDouble(const Address &src, Register dest)
{
	ispew("unboxNonDouble(adr, reg)");
    ma_lw(dest, Address(src.base, src.offset + PAYLOAD_OFFSET));
}

void
MacroAssemblerPPCCompat::unboxInt32(const ValueOperand &operand, Register dest)
{
	ispew("unboxInt32(vo, reg)");
	if (operand.payloadReg() != dest)
    	x_mr(dest, operand.payloadReg());
}

void
MacroAssemblerPPCCompat::unboxInt32(const Address &src, Register dest)
{
	ispew("unboxInt32(adr, reg)");
    ma_lw(dest, Address(src.base, src.offset + PAYLOAD_OFFSET));
}

void
MacroAssemblerPPCCompat::unboxBoolean(const ValueOperand &operand, Register dest)
{
	ispew("unboxBoolean(vo, reg)");
	if (operand.payloadReg() != dest)
    	x_mr(dest, operand.payloadReg());
}

void
MacroAssemblerPPCCompat::unboxBoolean(const Address &src, Register dest)
{
	ispew("unboxBoolean(adr, reg)");
    ma_lw(dest, Address(src.base, src.offset + PAYLOAD_OFFSET));
}

void
MacroAssemblerPPCCompat::unboxDouble(const ValueOperand &operand, FloatRegister dest)
{
	ispew("[[ unboxDouble(vo, fpr)");
	
	// ENDIAN!
	stwu(operand.payloadReg(), stackPointerRegister, -4);
	stwu(operand.typeReg(), stackPointerRegister, -4); // both cracked
#if _PPC970_
	x_nop();
	x_nop();
#endif
	lfd(dest, stackPointerRegister, 0);
	addi(stackPointerRegister, stackPointerRegister, 8);
	
	ispew("   unboxDouble(vo, fpr) ]]");
}

void
MacroAssemblerPPCCompat::unboxDouble(const Address &src, FloatRegister dest)
{
	ispew("[[ unboxDouble(adr, fpr)");
    ma_lw(tempRegister, Address(src.base, src.offset + PAYLOAD_OFFSET));
    stwu(tempRegister, stackPointerRegister, -4);
    // If the stack register was the source, we have to add another 4 here
    // -- we just pushed!
    if (src.base == stackPointerRegister)
        ma_lw(tempRegister, Address(src.base, src.offset + TAG_OFFSET + 4));
    else
        ma_lw(tempRegister, Address(src.base, src.offset + TAG_OFFSET));
    stwu(tempRegister, stackPointerRegister, -4);
#if _PPC970_
    x_nop();
    x_nop();
#endif
	lfd(dest, stackPointerRegister, 0);
	addi(stackPointerRegister, stackPointerRegister, 8);
	
	ispew("   unboxDouble(adr, fpr) ]]");
}

void
MacroAssemblerPPCCompat::unboxString(const ValueOperand &operand, Register dest)
{
	ispew("unboxString(vo, reg)");
    x_mr(dest, operand.payloadReg());
}

void
MacroAssemblerPPCCompat::unboxString(const Address &src, Register dest)
{
	ispew("unboxString(adr, reg)");
    ma_lw(dest, Address(src.base, src.offset + PAYLOAD_OFFSET));
}

void
MacroAssemblerPPCCompat::unboxObject(const ValueOperand &src, Register dest)
{
	ispew("unboxObject(vo, reg)");
    x_mr(dest, src.payloadReg());
}

void
MacroAssemblerPPCCompat::unboxObject(const Address &src, Register dest)
{
	ispew("unboxObject(adr, reg)");
    ma_lw(dest, Address(src.base, src.offset + PAYLOAD_OFFSET));
}

void
MacroAssemblerPPCCompat::unboxObject(const BaseIndex &bi, Register dest)
{
	ispew("unboxObject(bi, reg)");
	
	MOZ_ASSERT(bi.base != addressTempRegister);
	computeScaledAddress(bi, addressTempRegister);
	ma_lw(dest, Address(addressTempRegister, PAYLOAD_OFFSET));
}

void
MacroAssemblerPPCCompat::unboxValue(const ValueOperand &src, AnyRegister dest)
{
    ispew("[[ unboxValue(vo, anyreg)");
    if (dest.isFloat()) {
        BufferOffset notInt32, end;
        
        notInt32 = _bc(0,
            ma_cmp(src.typeReg(), ImmType(JSVAL_TYPE_INT32),
                Assembler::NotEqual));
        convertInt32ToDouble(src.payloadReg(), dest.fpu());
        end = _b(0);
        
        bindSS(notInt32);
        unboxDouble(src, dest.fpu());
        bindSS(end);
    } else if (src.payloadReg() != dest.gpr()) {
        x_mr(dest.gpr(), src.payloadReg());
    }
    ispew("   unboxValue(vo, anyreg) ]]");
}

void
MacroAssemblerPPCCompat::unboxPrivate(const ValueOperand &src, Register dest)
{
	ispew("unboxPrivate(vo, reg)");
    x_mr(dest, src.payloadReg());
}

void
MacroAssemblerPPCCompat::boxDouble(FloatRegister src, const ValueOperand &dest)
{
	ispew("boxDouble(fpr, vo)");
	ma_mv(src, dest);
}

void
MacroAssemblerPPCCompat::boxNonDouble(JSValueType type, Register src,
                                       const ValueOperand &dest)
{
	ispew("boxNonDouble(jsvaluetype, reg, vo)");
    if (src != dest.payloadReg())
        x_mr(dest.payloadReg(), src);
    ma_li32(dest.typeReg(), ImmType(type));
}

void
MacroAssemblerPPCCompat::boolValueToDouble(const ValueOperand &operand, FloatRegister dest)
{
	ispew("[[ boolValueToDouble(vo, fpr)");
    convertBoolToInt32(operand.payloadReg(), addressTempRegister);
    convertInt32ToDouble(addressTempRegister, dest);
    ispew("   boolValueToDouble(vo, fpr) ]]");
}

void
MacroAssemblerPPCCompat::int32ValueToDouble(const ValueOperand &operand,
                                             FloatRegister dest)
{
    convertInt32ToDouble(operand.payloadReg(), dest);
}

void
MacroAssemblerPPCCompat::boolValueToFloat32(const ValueOperand &operand,
                                             FloatRegister dest)
{
	ispew("[[ boolValueToFloat32(vo, fpr)");
    convertBoolToInt32(addressTempRegister, operand.payloadReg());
    convertInt32ToFloat32(addressTempRegister, dest);
    ispew("   boolValueToFloat32(vo, fpr) ]]");
}

void
MacroAssemblerPPCCompat::int32ValueToFloat32(const ValueOperand &operand,
                                              FloatRegister dest)
{
    convertInt32ToFloat32(operand.payloadReg(), dest);
}

void
MacroAssemblerPPCCompat::loadConstantFloat32(float f, FloatRegister dest)
{
	ispew("loadConstantFloat32(float, fpr)");
    ma_lis(dest, f);
}

void
MacroAssemblerPPCCompat::loadInt32OrDouble(const Address &src, FloatRegister dest)
{
    ispew("[[ loadInt32OrDouble(adr, fpr)");
    BufferOffset notInt32, end;
    
    // If it's an int, convert it to double.
    ma_lw(tempRegister, Address(src.base, src.offset + TAG_OFFSET));
    notInt32 = _bc(0,
        ma_cmp(tempRegister, ImmType(JSVAL_TYPE_INT32), Assembler::NotEqual));
    ma_lw(tempRegister, Address(src.base, src.offset + PAYLOAD_OFFSET));
    // XXX: tempRegister is heavily used by the conversion routines, so this is simpler for now.
    x_mr(addressTempRegister, tempRegister);
    convertInt32ToDouble(addressTempRegister, dest);
    end = _b(0);

    // Not an int, so just load as double.
    bindSS(notInt32);
    ma_ld(dest, src);
    bindSS(end);
    ispew("   loadInt32OrDouble(adr, fpr) ]]");
}

void
MacroAssemblerPPCCompat::loadInt32OrDouble(Register base, Register index,
                                            FloatRegister dest, int32_t shift)
{
    ispew("[[ loadInt32OrDouble(reg, reg, fpr, imm)");
    BufferOffset notInt32, end;

    // If it's an int, convert it to double.
    computeScaledAddress(BaseIndex(base, index, ShiftToScale(shift)), addressTempRegister);
    // Since we only have one scratch, we need to stomp over it with the tag.
    load32(Address(addressTempRegister, TAG_OFFSET), tempRegister);
    notInt32 = _bc(0,
        ma_cmp(tempRegister, ImmType(JSVAL_TYPE_INT32), Assembler::NotEqual));

    computeScaledAddress(BaseIndex(base, index, ShiftToScale(shift)), addressTempRegister);
    load32(Address(addressTempRegister, PAYLOAD_OFFSET), tempRegister);
    // XXX: tempRegister is heavily used by the conversion routines, so this is simpler for now.
    x_mr(addressTempRegister, tempRegister);
    convertInt32ToDouble(addressTempRegister, dest);
    end = _b(0);

    // Not an int, so just load as double.
    bindSS(notInt32);
    // First, recompute the offset that had been stored in the scratch register
    // since the scratch register was overwritten loading in the type.
    computeScaledAddress(BaseIndex(base, index, ShiftToScale(shift)), addressTempRegister);
    loadDouble(Address(addressTempRegister, 0), dest);
    bindSS(end);
    ispew("   loadInt32OrDouble(reg, reg, fpr, imm) ]]");
}

void
MacroAssemblerPPCCompat::loadConstantDouble(double dp, FloatRegister dest)
{
	ispew("loadConstantDouble(double, fpr)");
    ma_lid(dest, dp);
}

void
MacroAssemblerPPCCompat::branchTestInt32Truthy(bool b, const ValueOperand &value, Label *label)
{
	ispew("branchTestInt32Truthy(bool, vo, l)");
    and__rc(tempRegister, value.payloadReg(), value.payloadReg());
    bc(b ? NonZero : Zero, label);
}

void
MacroAssemblerPPCCompat::branchTestStringTruthy(bool b, const ValueOperand &value, Label *label)
{
	ispew("branchTestStringTruthy(bool, vo, l)");
    Register string = value.payloadReg();
    ma_lw(tempRegister, Address(string, JSString::offsetOfLength()));
    cmpwi(tempRegister, 0);
    bc(b ? NotEqual : Equal, label);
}

void
MacroAssemblerPPCCompat::branchTestDoubleTruthy(bool b, FloatRegister value, Label *label)
{
	ispew("branchTestDoubleTruthy(bool, fpr, l)");
    MOZ_ASSERT(value != fpTempRegister);
    ma_lis(fpTempRegister, 0.0);
    DoubleCondition cond = b ? DoubleNotEqual : DoubleEqualOrUnordered;
    fcmpu(fpTempRegister, value);
    bc(cond, label);
}

void
MacroAssemblerPPCCompat::branchTestBooleanTruthy(bool b, const ValueOperand &operand,
                                                  Label *label)
{
    branchTestInt32Truthy(b, operand, label); // Same type on PPC.
}

Register
MacroAssemblerPPCCompat::extractObject(const Address &address, Register scratch)
{
    ma_lw(scratch, Address(address.base, address.offset + PAYLOAD_OFFSET));
    return scratch;
}

Register
MacroAssemblerPPCCompat::extractTag(const Address &address, Register scratch)
{
    ma_lw(scratch, Address(address.base, address.offset + TAG_OFFSET));
    return scratch;
}

Register
MacroAssemblerPPCCompat::extractTag(const BaseIndex &address, Register scratch)
{
    computeScaledAddress(address, scratch);
    return extractTag(Address(scratch, address.offset), scratch);
}


uint32_t
MacroAssemblerPPCCompat::getType(const Value &val)
{
    jsval_layout jv = JSVAL_TO_IMPL(val);
    return jv.s.tag;
}

// ENDIAN!
void
MacroAssemblerPPCCompat::loadUnboxedValue(Address address, MIRType type, AnyRegister dest)
{
    ispew("loadUnboxedValue(adr, mirtype, anyreg)");

    if (dest.isFloat())
        // loadInt32OrDouble already knows to correct for endianness.
        loadInt32OrDouble(address, dest.fpu());
    else
        // Correct the address to include the payload offset.
        ma_lw(dest.gpr(), Address(address.base, address.offset + PAYLOAD_OFFSET));
}

// ENDIAN!
void
MacroAssemblerPPCCompat::loadUnboxedValue(BaseIndex address, MIRType type, AnyRegister dest)
{
	ispew("loadUnboxedValue(bi, mirtype, anyreg)");

    if (dest.isFloat())
        // loadInt32OrDouble already knows to correct for endianness.
        loadInt32OrDouble(address.base, address.index, dest.fpu(), address.scale);
    else {
        // Correct the base index to include the payload offset.
        MOZ_ASSERT(address.base != addressTempRegister);

        computeScaledAddress(address, addressTempRegister);
    	ma_lw(dest.gpr(), Address(addressTempRegister, PAYLOAD_OFFSET));
    }
}

// ENDIAN!
template <typename T>
void
MacroAssemblerPPCCompat::storeUnboxedValue(ConstantOrRegister value, MIRType valueType, const T &dest,
                                            MIRType slotType)
{
	ispew("[[ storeUnboxedValue(cor, mirtype, T, mirtype)");
    if (valueType == MIRType_Double) {
        storeDouble(value.reg().typedReg().fpu(), dest);
        return;
    }

    // Store the type tag if needed.
    if (valueType != slotType)
        storeTypeTag(ImmType(ValueTypeFromMIRType(valueType)), dest);

    // Store the payload. storePayload uses the corrected offset.
    if (value.constant())
        storePayload(value.value(), dest);
    else
        storePayload(value.reg().typedReg().gpr(), dest);
    ispew("   storeUnboxedValue(cor, mirtype, T, mirtype) ]]");
}

template void
MacroAssemblerPPCCompat::storeUnboxedValue(ConstantOrRegister value, MIRType valueType, const Address &dest,
                                            MIRType slotType);

template void
MacroAssemblerPPCCompat::storeUnboxedValue(ConstantOrRegister value, MIRType valueType, const BaseIndex &dest,
                                            MIRType slotType);

void
MacroAssemblerPPCCompat::moveData(const Value &val, Register data)
{
	ispew("moveData(v, reg)");
    jsval_layout jv = JSVAL_TO_IMPL(val);
    if (val.isMarkable())
        ma_li32(data, ImmGCPtr(reinterpret_cast<gc::Cell *>(val.toGCThing())));
    else
        ma_li32(data, Imm32(jv.s.payload.i32));
}

void
MacroAssemblerPPCCompat::moveValue(const Value &val, Register type, Register data)
{
	ispew("moveValue(v, reg, reg)");
    MOZ_ASSERT(type != data);
    ma_li32(type, Imm32(getType(val)));
    moveData(val, data);
}
void
MacroAssemblerPPCCompat::moveValue(const Value &val, const ValueOperand &dest)
{
    moveValue(val, dest.typeReg(), dest.payloadReg());
}

// See also jit::PatchBackedge.
CodeOffsetJump
MacroAssemblerPPCCompat::backedgeJump(RepatchLabel *label, Label* documentation)
{
	ispew("[[ backedgeJump(rl)");
    // Only one branch per label.
    MOZ_ASSERT(!label->used());
    uint32_t dest = label->bound() ? label->offset() : LabelBase::INVALID_OFFSET;
    BufferOffset bo = nextOffset();
    if (MOZ_UNLIKELY(oom())) return;
    label->use(bo.getOffset());

    // Backedges are short jumps when bound, but can become long when patched.
    m_buffer.ensureSpace(40); // paranoia
    if (label->bound()) {
        int32_t offset = label->offset() - bo.getOffset();
        MOZ_ASSERT(JOffImm26::IsInRange(offset));
        Assembler::b(offset);
    } else {
        // Jump to the first stanza by default to jump to the loop header.
        // PatchBackedge may be able to help us later.
        Assembler::b(4);
    }
    
    // Stanza 1: loop header.
    x_p_li32(tempRegister, dest);
    x_mtctr(tempRegister);
    bctr();
    // Stanza 2: interrupt loop target (AsmJS only).
    x_p_li32(tempRegister, dest);
    x_mtctr(tempRegister);
    bctr();
    ispew("   backedgeJump(rl) ]]");
    return CodeOffsetJump(bo.getOffset());
}

// XXX: Not fully implemented (see CodeGeneratorShared::jumpToBlock()).
CodeOffsetJump
MacroAssemblerPPCCompat::jumpWithPatch(RepatchLabel *label, Condition cond, Label* documentation)
{
	// Since these are patchable, no sense in optimizing them.
	ispew("[[ jumpWithPatch(rl, c, l)");
    // Only one branch per label.
    MOZ_ASSERT(!label->used());
    MOZ_ASSERT(cond == Always); // Not tested (but should work).
    
    // Put some extra space in, just in case bcctr() generates additional instructions.
    m_buffer.ensureSpace(((cond == Always) ? 5 : 8) * sizeof(uint32_t));
    uint32_t dest = label->bound() ? label->offset() : LabelBase::INVALID_OFFSET;

    BufferOffset bo = nextOffset();
    label->use(bo.getOffset());
    addLongJump(bo);
    x_p_li32(tempRegister, dest);
    x_mtctr(tempRegister);
    
    // If always, use bctr(). If not, use bcctr() with the appropriate condition.
    // Assume the comparison already occurred.
    if (cond == Always) {
    	bctr();
    } else {
    	bcctr(cond);
    }
    ispew("   jumpWithPatch(rl, c, l) ]]");
    return CodeOffsetJump(bo.getOffset());
}

CodeOffsetJump
MacroAssemblerPPCCompat::jumpWithPatch(RepatchLabel *label, Label* documentation)
{
	return jumpWithPatch(label, Always, documentation);
}

/////////////////////////////////////////////////////////////////
// X86/X64-common/ARM/MIPS/IonPower interface.
/////////////////////////////////////////////////////////////////
void
MacroAssemblerPPCCompat::storeValue(ValueOperand val, Operand dst)
{
	ispew("storeValue(vo, o)");
    storeValue(val, Address(Register::FromCode(dst.base()), dst.disp()));
}

void
MacroAssemblerPPCCompat::storeValue(ValueOperand val, const BaseIndex &dest)
{
	ispew("storeValue(vo, bi)");
    computeScaledAddress(dest, addressTempRegister);
    storeValue(val, Address(addressTempRegister, dest.offset));
}

void
MacroAssemblerPPCCompat::storeValue(JSValueType type, Register reg, BaseIndex dest)
{
	ispew("storeValue(jsvaluetype, reg, bi)");
	MOZ_ASSERT(dest.base != tempRegister);
	MOZ_ASSERT(dest.base != addressTempRegister);
    computeScaledAddress(dest, addressTempRegister);

    int32_t offset = dest.offset;
    if (!Imm16::IsInSignedRange(offset)) {
    	x_li32(tempRegister, offset);
    	add(addressTempRegister, tempRegister, addressTempRegister);
        offset = 0;
    }
    storeValue(type, reg, Address(addressTempRegister, offset));
}

void
MacroAssemblerPPCCompat::storeValue(ValueOperand val, const Address &dest)
{
	ispew("storeValue(vo, adr)");
    ma_sw(val.payloadReg(), Address(dest.base, dest.offset + PAYLOAD_OFFSET));
    ma_sw(val.typeReg(), Address(dest.base, dest.offset + TAG_OFFSET));
}

void
MacroAssemblerPPCCompat::storeValue(JSValueType type, Register reg, Address dest)
{
	ispew("storeValue(jsvaluetype, reg, adr)");
    MOZ_ASSERT(dest.base != tempRegister);

    ma_sw(reg, Address(dest.base, dest.offset + PAYLOAD_OFFSET));
    ma_li32(tempRegister, ImmTag(JSVAL_TYPE_TO_TAG(type)));
    ma_sw(tempRegister, Address(dest.base, dest.offset + TAG_OFFSET));
}

void
MacroAssemblerPPCCompat::storeValue(const Value &val, Address dest)
{
	ispew("storeValue(v, adr)");
    MOZ_ASSERT(dest.base != tempRegister);

    ma_li32(tempRegister, Imm32(getType(val)));
    ma_sw(tempRegister, Address(dest.base, dest.offset + TAG_OFFSET));
    moveData(val, tempRegister);
    ma_sw(tempRegister, Address(dest.base, dest.offset + PAYLOAD_OFFSET));
}

void
MacroAssemblerPPCCompat::storeValue(const Value &val, BaseIndex dest)
{
	ispew("storeValue(v, bi)");
	MOZ_ASSERT(dest.base != tempRegister);
	MOZ_ASSERT(dest.base != addressTempRegister);
	
    computeScaledAddress(dest, addressTempRegister);

    int32_t offset = dest.offset;
    if (!Imm16::IsInSignedRange(offset)) {
        x_li32(tempRegister, offset);
        add(addressTempRegister, tempRegister, addressTempRegister);
        offset = 0;
    }
    storeValue(val, Address(addressTempRegister, offset));
}

void
MacroAssemblerPPCCompat::loadValue(const BaseIndex &addr, ValueOperand val)
{
	ispew("loadValue(bi, vo)");
    computeScaledAddress(addr, addressTempRegister);
    loadValue(Address(addressTempRegister, addr.offset), val);
}

void
MacroAssemblerPPCCompat::loadValue(Address src, ValueOperand val)
{
	ispew("loadValue(adr, vo)");
	
    // Ensure that loading the payload does not erase the pointer to the
    // Value in memory.
    if (src.base != val.payloadReg()) {
        ma_lw(val.payloadReg(), Address(src.base, src.offset + PAYLOAD_OFFSET));
        ma_lw(val.typeReg(), Address(src.base, src.offset + TAG_OFFSET));
    } else {
        ma_lw(val.typeReg(), Address(src.base, src.offset + TAG_OFFSET));
        ma_lw(val.payloadReg(), Address(src.base, src.offset + PAYLOAD_OFFSET));
    }
}

void
MacroAssemblerPPCCompat::tagValue(JSValueType type, Register payload, ValueOperand dest)
{
	ispew("tagValue(jsvaluetype, reg, vo)");
    MOZ_ASSERT(dest.typeReg() != dest.payloadReg());
    if (payload == dest.typeReg()) {
        // Sub-optimal case since a temp register is now required.
        MOZ_ASSERT(payload != tempRegister);

        x_mr(tempRegister, payload);
        ma_li32(dest.typeReg(), ImmType(type));
        x_mr(dest.payloadReg(), tempRegister);
        return;
    }
    
    ma_li32(dest.typeReg(), ImmType(type));
    if (payload != dest.payloadReg())
        x_mr(dest.payloadReg(), payload);
}

void
MacroAssemblerPPCCompat::pushValue(ValueOperand val)
{
	ispew("pushValue(vo)");
	
	// Endian! Payload on first.
	stwu(val.payloadReg(), stackPointerRegister, -4);
	stwu(val.typeReg(), stackPointerRegister, -4);
}

void
MacroAssemblerPPCCompat::pushValue(const Address &addr)
{
	ispew("pushValue(adr)");
	
    // Allocate stack slots for type and payload. One for each.
    ma_subu(stackPointerRegister, stackPointerRegister, Imm32(sizeof(Value)));
    // Store type and payload.
    ma_lw(tempRegister, Address(addr.base, addr.offset + TAG_OFFSET));
    ma_sw(tempRegister, Address(stackPointerRegister, TAG_OFFSET));
    ma_lw(tempRegister, Address(addr.base, addr.offset + PAYLOAD_OFFSET));
    ma_sw(tempRegister, Address(stackPointerRegister, PAYLOAD_OFFSET));
}

void
MacroAssemblerPPCCompat::popValue(ValueOperand val)
{
	ispew("popValue(vo)");
	
    // Load payload and type.
    lwz(val.payloadReg(), stackPointerRegister, PAYLOAD_OFFSET);
    lwz(val.typeReg(), stackPointerRegister, TAG_OFFSET);
    // Free stack.
    addi(stackPointerRegister, stackPointerRegister, 8);
}

void
MacroAssemblerPPCCompat::storePayload(const Value &val, Address dest)
{
	ispew("storePayload(v, adr)");
	
    moveData(val, addressTempRegister);
    ma_sw(addressTempRegister, Address(dest.base, dest.offset + PAYLOAD_OFFSET));
}

void
MacroAssemblerPPCCompat::storePayload(Register src, Address dest)
{
	ispew("storePayload(reg, adr");
    ma_sw(src, Address(dest.base, dest.offset + PAYLOAD_OFFSET));
}

void
MacroAssemblerPPCCompat::storePayload(const Value &val, const BaseIndex &dest)
{
	ispew("storePayload(v, bi)");
	MOZ_ASSERT(dest.base != tempRegister);
	MOZ_ASSERT(dest.base != addressTempRegister);
    MOZ_ASSERT(dest.offset == 0);

    computeScaledAddress(dest, addressTempRegister);
    moveData(val, tempRegister);
    stw(tempRegister, addressTempRegister, NUNBOX32_PAYLOAD_OFFSET);
}

void
MacroAssemblerPPCCompat::storePayload(Register src, const BaseIndex &dest)
{
	ispew("storePayload(reg, bi)");
    MOZ_ASSERT(dest.offset == 0);

    computeScaledAddress(dest, addressTempRegister);
    stw(src, addressTempRegister, NUNBOX32_PAYLOAD_OFFSET);
}

void
MacroAssemblerPPCCompat::storeTypeTag(ImmTag tag, Address dest)
{
    ma_li32(tempRegister, tag);
    ma_sw(tempRegister, Address(dest.base, dest.offset + TAG_OFFSET));
}

void
MacroAssemblerPPCCompat::storeTypeTag(ImmTag tag, const BaseIndex &dest)
{
	ispew("storeTypeTag(immt, bi)");
    MOZ_ASSERT(dest.offset == 0);

    computeScaledAddress(dest, addressTempRegister);
    ma_li32(tempRegister, tag);
    stw(tempRegister, addressTempRegister, TAG_OFFSET);
}

/* XXX We should only need (and use) callJit */

void
MacroAssemblerPPC::ma_callJitNoPush(const Register r)
{
	ispew("!!!>>> ma_callJitNoPush <<<!!!");
	ma_callJit(r);
}

void
MacroAssemblerPPC::ma_callJitHalfPush(const Register r)
{
	ispew("!!!>>> ma_callJitHalfPush <<<!!!");
	ma_callJit(r);
}

void
MacroAssemblerPPC::ma_callJit(const Register r)
{
	ispew("ma_callJit(reg)");
	
	// This is highly troublesome on PowerPC because the normal ABI expects
	// the *callee* to save LR, and the instruction set is written with this
	// in mind (MIPS gets around this problem by saving $ra in the delay slot,
	// but that's an ugly hack). Since we don't have a reliable way of writing the
	// prologue of the JitCode we call to do this, we can't use LR for calls
	// to generated code!
	//
	// Instead, we simply compute a return address and store that, and return to
	// that instead, so that we basically act like x86.
	
	CodeLabel returnPoint;
	
	x_mtctr(r); // new dispatch group
	ma_li32(tempRegister, returnPoint.patchAt()); // push computed return address
	stwu(tempRegister, stackPointerRegister, -4);
	// This is guaranteed to be in a different dispatch group, so no nops needed.
	bctr(DontLinkB);
	
	// Return point is here.
	bind(returnPoint.target());
	addCodeLabel(returnPoint);
}

void
MacroAssemblerPPC::ma_callJitHalfPush(Label *label) // XXX probably should change the name.
{
	ispew("!!!>>> ma_callJitHalfPush (label) <<<!!!");
	
	// Same limitation applies.
	CodeLabel returnPoint;
	
	ma_li32(tempRegister, returnPoint.patchAt()); // push computed return address
	stwu(tempRegister, stackPointerRegister, -4);
	b(label);	
	
	// Return point is here.
	bind(returnPoint.target());
	addCodeLabel(returnPoint);
}

/* Although these two functions call fixed addresses, optimizing them is
   not very helpful because they are used quite rarely. */
void
MacroAssemblerPPC::ma_call(ImmPtr dest)
{
	ispew("ma_call(immptr)");
    ma_li32Patchable(tempRegister, dest);
    ma_callJit(tempRegister);
}

void
MacroAssemblerPPC::ma_jump(ImmPtr dest)
{
	ispew("ma_jump(immptr)");
    ma_li32Patchable(tempRegister, dest);
    x_mtctr(tempRegister);
	bctr(DontLinkB);
}

/* Assume these calls and returns are JitCode, because anything calling a PowerOpen ABI
   routine would go through callWithABI() and blr(). */
   
/* As of Firefox 42 or something, these are now part of the MacroAssembler, not the
   platform-specific one. */
   
CodeOffset
MacroAssembler::call(const Register r) {
	ispew("call -> callJit");
	ma_callJit(r);
        return CodeOffset(currentOffset());
}

CodeOffset
MacroAssembler::call(Label *l) {
	ispew("call(l) -> callJit");
	ma_callJitHalfPush(l); // XXX
        return CodeOffset(currentOffset());
}

/* Based on:
   x_p_li32                             ; 2
   mtctr        ; call -> callJit       ; 3
   x_p_li32                             ; 5
   stwu                                 ; 6
   bctr                                 ; 7 words
*/
#define PPC_PATCHABLE_CALL_OFFSET 28

CodeOffset
MacroAssembler::callWithPatch()
{
    ispew("[[ callWithPatch()");
    // Don't use ma_li32 in case we jump pages.
    m_buffer.ensureSpace(PPC_PATCHABLE_CALL_OFFSET + 4);
#if DEBUG
    uint32_t j = currentOffset();
#endif
    addLongJump(nextOffset());
    x_p_li32(tempRegister, -3);

    ispew("   callWithPatch() ]]");
#if DEBUG
    CodeOffset k = call(tempRegister);
    MOZ_ASSERT((currentOffset() - j) == PPC_PATCHABLE_CALL_OFFSET);
    return k;
#else
    return call(tempRegister);
#endif
}

void
MacroAssembler::patchCall(uint32_t callerOffset, uint32_t calleeOffset)
{
#if DEBUG
    JitSpew(JitSpew_Codegen, "::patchCall 0x%08x -> 0x%08x", callerOffset, calleeOffset);
#endif
    /* This should be the first lis/ori in the call above. */
    BufferOffset lis(callerOffset - PPC_PATCHABLE_CALL_OFFSET);
    Assembler::PatchInstructionImmediate((uint8_t*)editSrc(lis), PatchedImmPtr((const void*)calleeOffset));
}

uint32_t
MacroAssembler::pushFakeReturnAddress(Register scratch)
{
    CodeLabel cl;

    ma_li32(scratch, cl.patchAt());
    Push(scratch);
    bind(cl.target());
    uint32_t retAddr = currentOffset();

    addCodeLabel(cl);
    return retAddr;
}

/* Formerly in -ppc.h */
    void MacroAssembler::call(ImmWord imm) {
        call(ImmPtr((void*)imm.value));
    }
    void MacroAssembler::call(ImmPtr imm) {
        BufferOffset bo = m_buffer.nextOffset();
        addPendingJump(bo, imm, Relocation::HARDCODED);
        ma_call(imm);
    }
    void MacroAssembler::call(wasm::SymbolicAddress imm) { // XXX
        movePtr(imm, CallReg);
        call(CallReg);
    }
    void MacroAssembler::call(JitCode *c) {
        BufferOffset bo = m_buffer.nextOffset();
        addPendingJump(bo, ImmPtr(c->raw()), Relocation::JITCODE);
        ma_li32Patchable(addressTempRegister, Imm32((uint32_t)c->raw()));
        ma_callJitHalfPush(addressTempRegister); // XXX
    }
    void MacroAssembler::callAndPushReturnAddress(Register reg) {
    	ma_callJit(reg);
    }
    void MacroAssembler::callAndPushReturnAddress(Label *label) {
    	ma_callJitHalfPush(label); // XXX
    }

void
MacroAssemblerPPCCompat::retn(Imm32 n)
{
	ispew("retn");
	// The return address is on top of the stack, not LR.
	
	lwz(tempRegister, stackPointerRegister, 0);
	x_mtctr(tempRegister); // LR is now restored, new dispatch group
	addi(stackPointerRegister, stackPointerRegister, n.value);
	// Branch.
	bctr(DontLinkB);
}

void
MacroAssemblerPPCCompat::breakpoint()
{
// This is always external and should always be marked. We never call this ourselves.
	ispew("assumeUnreachable");
    x_trap();
}

void
MacroAssemblerPPCCompat::ensureDouble(const ValueOperand &source, FloatRegister dest,
                                       Label *failure)
{
    ispew("ensureDouble(vo, fpr, l)");
	
    BufferOffset isDouble, done;
    isDouble = _bc(0,
        ma_cmp(source.typeReg(), ImmTag(JSVAL_TAG_CLEAR), Assembler::Below));
    branchTestInt32(Assembler::NotEqual, source.typeReg(), failure);

    convertInt32ToDouble(source.payloadReg(), dest);
    done = _b(0);

    bindSS(isDouble);
    unboxDouble(source, dest);
    bindSS(done);
}

void
MacroAssembler::setupUnalignedABICall(Register scratch)
{
	setupABICall();
	// The actual stack alignment occurs just prior to the call,
	// so this is otherwise a no-op now.
}

void
MacroAssemblerPPCCompat::checkStackAlignment()
{
	// This is a no-op for PowerPC; we assume unalignment of the stack
	// right up until we branch.
}

void
MacroAssembler::alignFrameForICArguments(AfterICSaveLive &aic)
{
	// Exists for MIPS. We don't use this.
}

void
MacroAssembler::restoreFrameAlignmentForICArguments(AfterICSaveLive &aic)
{
	// Exists for MIPS. We don't use this.
}

// Non-volatile registers for saving r1 and LR because we assume the
// callee will merrily wax them and we don't really have a linkage area.
static const Register stackSave = r16; // Reserved in Architecture-ppc.h.
static const Register returnSave = r18; // Reserved in Architecture-ppc.h.

void
MacroAssembler::callWithABIPre(uint32_t *stackAdjust, bool callFromAsmJS)
{
    MOZ_ASSERT(inCall_);
    MOZ_ASSERT(!callFromAsmJS); // should NEVER happen
    
    // stackAdjust is always zero, because stack realignment occurs without
    // Ion being aware of it.
    *stackAdjust = 0;

	ispew("-- Resolving movements --");
    // Position all arguments.
    {
        enoughMemory_ = enoughMemory_ && moveResolver_.resolve();
        if (!enoughMemory_)
            return;

        MoveEmitter emitter(*this);
        emitter.emit(moveResolver_);
        emitter.finish();
    }
    ispew("-- Movements resolved --");

	// Forcibly align the stack. (We'll add a dummy stack frame in a moment.)
	andi_rc(tempRegister, stackPointerRegister, 4);
	// Save the old stack pointer so we can get it back after the call without
	// screwing up the ABI-compliant routine's ability to walk stack frames.
	x_mr(stackSave, stackPointerRegister);
	subf(stackPointerRegister, tempRegister, stackPointerRegister);
	andi_rc(tempRegister, stackPointerRegister, 8);
	subf(stackPointerRegister, tempRegister, stackPointerRegister);
	// Pull down 512 bytes for a generous stack frame that should accommodate anything.
	x_subi(stackPointerRegister, stackPointerRegister, 512);
	
	// Store the trampoline SP as the phony backreference. This lets the called routine
	// skip anything Ion or Baseline pushed on the stack (the trampoline saved this for
	// us way back when).
	stw(returnSave, stackPointerRegister, 0);
	// Now that it's saved, put LR there since we have no true linkage area.
	x_mflr(returnSave);
}

void
MacroAssembler::callWithABIPost(uint32_t stackAdjust, MoveOp::Type result)
{
	MOZ_ASSERT(!stackAdjust);

	// Get back LR.
	x_mtlr(returnSave);
	// Get back the trampoline SP for future calls.
	lwz(returnSave, stackPointerRegister, 0);
	// Get back the old SP prior to the forcible alignment.
	x_mr(stackPointerRegister, stackSave);

#if DEBUG
    MOZ_ASSERT(inCall_);
    inCall_ = false;
#endif
}

void
MacroAssemblerPPCCompat::callWithABIOptimized(void *fun, MoveOp::Type result)
{
/* As of Firefox 43 Ion tries to do this itself by calling call(), but our call()s are
   not ABI compliant, so it just makes a big mess. See MacroAssembler::callWithABINoProfiler(). */
   
	uint32_t stackAdjust = 0;
	ispew("vv -- callWithABI(void *, result) -- vv");

    if ((uint32_t)fun & 0xfc000000) { // won't fit in 26 bits
        x_li32(addressTempRegister, (uint32_t)fun);
        x_mtctr(addressTempRegister); // Load CTR here to avoid nops later.
    }
    asMasm().callWithABIPre(&stackAdjust);
    if ((uint32_t)fun & 0xfc000000) { // won't fit in 26 bits
	bctr(LinkB);	
    } else {
        _b((uint32_t)fun, AbsoluteBranch, LinkB);
    }
    asMasm().callWithABIPost(stackAdjust, result);

    ispew("^^ -- callWithABI(void *, result) -- ^^");
}

void
MacroAssembler::callWithABINoProfiler(const Address &fun, MoveOp::Type result)
{
    uint32_t stackAdjust = 0;
	ispew("vv -- callWithABI(adr, result) -- vv");
	
    ma_lw(addressTempRegister, Address(fun.base, fun.offset));
    x_mtctr(addressTempRegister);
    callWithABIPre(&stackAdjust);
	bctr(LinkB);    
    callWithABIPost(stackAdjust, result);
    
    ispew("^^ -- callWithABI(adr, result) -- ^^");
}

void
MacroAssembler::callWithABINoProfiler(Register fun, MoveOp::Type result)
{
    uint32_t stackAdjust = 0;
	ispew("vv -- callWithABI(reg, result) -- vv");
	
	x_mtctr(fun);
    callWithABIPre(&stackAdjust);
	bctr(LinkB);
    callWithABIPost(stackAdjust, result);
    
    ispew("^^ -- callWithABI(reg, result) -- ^^");
}

void
MacroAssemblerPPCCompat::handleFailureWithHandlerTail(void *handler)
{
    ispew("vv -- handleFailureWithHandlerTail(void *) -- vv");
    // Call an ABI-compliant handler function, reserving adequate space on the stack
    // for a non-ABI compliant exception frame which we will then analyse.
	
    int size = (sizeof(ResumeFromException) + 16) & ~15;
    x_subi(stackPointerRegister, stackPointerRegister, size);
    // This is going to be our argument, so just grab r3 now and save a MoveEmitter.
    x_mr(r3, stackPointerRegister);

    // Ask for an exception handler.
    asMasm().setupUnalignedABICall(r4);
    asMasm().passABIArg(r3);
    asMasm().callWithABI(handler);

    BufferOffset entryFrame, catch_, finally, return_, bailout;

    // Now, analyse the returned frame.
    // Get the type.
    lwz(r3, stackPointerRegister, offsetof(ResumeFromException, kind));
    // Branch.
    catch_ = _bc(0,
        ma_cmp(r3, Imm32(ResumeFromException::RESUME_CATCH), Equal));
    entryFrame = _bc(0,
        ma_cmp(r3, Imm32(ResumeFromException::RESUME_ENTRY_FRAME), Equal));
    bailout = _bc(0,
        ma_cmp(r3, Imm32(ResumeFromException::RESUME_BAILOUT), Equal));
    finally = _bc(0,
        ma_cmp(r3, Imm32(ResumeFromException::RESUME_FINALLY), Equal));
    return_ = _bc(0,
        ma_cmp(r3, Imm32(ResumeFromException::RESUME_FORCED_RETURN), Equal));
    x_trap(); // Oops.

    // Entry frame case:
    // No exception handler. Load the error value, load the new stack pointer
    // and return from the entry frame.
    bindSS(entryFrame);
    lwz(stackPointerRegister, stackPointerRegister, offsetof(ResumeFromException, stackPointer));
    lwz(r3, stackPointerRegister, 0);
    x_mtctr(r3); // new dispatch group; always JitCode (but could be trampoline)
    moveValue(MagicValue(JS_ION_ERROR), JSReturnOperand);
    addi(stackPointerRegister, stackPointerRegister, 4);
    bctr();
	
    // Catch handler case:
    // If we found a catch handler, this must be a baseline frame. Restore
    // state and jump to the catch block.
    bindSS(catch_);
    lwz(r3, stackPointerRegister, offsetof(ResumeFromException, target));
    x_mtctr(r3); // force a new dispatch group here for G5
    // Then get the old BaselineFrameReg,
    lwz(BaselineFrameReg, stackPointerRegister, offsetof(ResumeFromException, framePointer));
    // get the old stack pointer,
    lwz(stackPointerRegister, stackPointerRegister, offsetof(ResumeFromException, stackPointer));
    // and branch.
    bctr();
	
    // Finally block case:
    // If we found a finally block, this must be a baseline frame. Push
    // two values expected by JSOP_RETSUB: BooleanValue(true) and the
    // exception.
    bindSS(finally);
    ValueOperand exception = ValueOperand(r4, r5);
    loadValue(Address(sp, offsetof(ResumeFromException, exception)), exception);

    // Then prepare to branch,
    lwz(r3, stackPointerRegister, offsetof(ResumeFromException, target));
    x_mtctr(r3); // force a new dispatch group here for G5
    // get the old BaselineFrameReg,
    lwz(BaselineFrameReg, stackPointerRegister, offsetof(ResumeFromException, framePointer));
    // get the old stack pointer,
    lwz(stackPointerRegister, stackPointerRegister, offsetof(ResumeFromException, stackPointer));
    // and finally push Boolean true and the exception, which will confidently keep the
    // bctr away from the mtctr for G5. TODO: pipeline using multi-push.
    pushValue(BooleanValue(true));
    pushValue(exception);
    // Bye.
    bctr();

    // Last but not least: forced return case.
    // Only used in debug mode. Return BaselineFrame->returnValue() to the
    // caller.
    bindSS(return_);
    // Get the old BaselineFrameReg,
    lwz(BaselineFrameReg, stackPointerRegister, offsetof(ResumeFromException, framePointer));
    // get the old stack pointer,
    lwz(stackPointerRegister, stackPointerRegister, offsetof(ResumeFromException, stackPointer));
    // load our return value,
    loadValue(Address(BaselineFrameReg, BaselineFrame::reverseOffsetOfReturnValue()),
              JSReturnOperand);
    // remove the stack frame,
    x_mr(stackPointerRegister, BaselineFrameReg);
    // and jet.
    pop(BaselineFrameReg);
    // If profiling is enabled, then update the lastProfilingFrame to refer
    // to the caller frame before returning.
    {
    	Label skipProfilingInstrumentation;
    	
    	// Is the profiler enabled?
    	AbsoluteAddress addressOfEnabled(GetJitContext()->runtime->spsProfiler().addressOfEnabled());
    	// This is a short branch, but this is a slow path of a slow path,
        // and it's not worth micro-optimizing.
    	branch32(Assembler::Equal, addressOfEnabled, Imm32(0), &skipProfilingInstrumentation);
        profilerExitFrame();
        bind(&skipProfilingInstrumentation);
    }
    ret();

    // If we are bailing out to baseline to handle an exception, jump to
    // the bailout tail stub.
    bindSS(bailout);
    lwz(r4, stackPointerRegister, offsetof(ResumeFromException, target));
    x_mtctr(r4); // new dispatch group
    x_li32(r3, BAILOUT_RETURN_OK);
    lwz(r5, stackPointerRegister, offsetof(ResumeFromException, bailoutInfo));
    // Bye.
    bctr();

    ispew("^^ -- handleFailureWithHandlerTail(); -- ^^");
}

CodeOffset
MacroAssemblerPPCCompat::toggledJump(Label *label)
{
	ispew("[[ toggledJump(l)");

    m_buffer.ensureSpace(24); // make sure this isn't broken up.
    CodeOffset ret(nextOffset().getOffset());
    x_nop(); // This will be patched by ToggleToJmp.
    b(label);
    
    // This should always be a branch+4 bytes long, including the nop.
    // If we're out of memory, however, we're probably already screwed.
    MOZ_ASSERT_IF(!oom(), (nextOffset().getOffset() - ret.offset()) ==
        (PPC_B_STANZA_LENGTH + 4));
    ispew("  toggledJump(l) ]]");
    return ret;
}

/* Toggled calls are only used by the Baseline JIT, and then only in Debug mode;
   see Assembler::ToggleCall and Trampoline::generateDebugTrapHandler. Unlike all
   our other calls, these actually do set and branch with LR. */
CodeOffset
MacroAssemblerPPCCompat::toggledCall(JitCode *target, bool enabled)
{
	ispew("[[ toggledCall(jitcode, bool)");
	
#if DEBUG
	// Currently this code assumes that it will only ever call the debug trap
	// handler, and that this code is executed after the handler was created.
	JitCode *handler = GetJitContext()->runtime->jitRuntime()->debugTrapHandler(GetJitContext()->cx);
	MOZ_ASSERT(handler == target);
#endif
	
	// This call is always long.
    BufferOffset bo = nextOffset();
    CodeOffset offset(bo.getOffset());
    addPendingJump(bo, ImmPtr(target->raw()), Relocation::JITCODE);
    ma_li32Patchable(tempRegister, ImmPtr(target->raw()));
    if (enabled) {
    	// XXX. Not much we can do here unless we want to bloat all toggled
    	// calls on the G5 (not appealing either).
        x_mtctr(tempRegister);
        // Actually DO branch with LR.
        bctr(LinkB);
    } else {
        x_nop();
        x_nop();
    }
    
    // This should always be 16 bytes long (if we're out of memory, we're
    // going to fail this anyway).
    MOZ_ASSERT_IF(!oom(),
        nextOffset().getOffset() - offset.offset() == ToggledCallSize(nullptr));
    ispew("   toggledCall(jitcode, bool) ]]");
    return offset;
}

void
MacroAssemblerPPCCompat::branchTest32(Condition cond, Register lhs, Register rhs, Label *label)
{
	ispew("[[ branchTest32(cond, reg, reg, l)");
	MOZ_ASSERT(cond == Zero || cond == NonZero || cond == Signed || cond == NotSigned);
	if (cond == Zero || cond == NonZero) {
		and__rc(tempRegister, lhs, rhs);
		bc(cond, label);
	} else if (cond == Signed || cond == NotSigned) {
		MOZ_ASSERT(lhs == rhs);
		cmpwi(lhs, 0); // less than: signed, greater than: not signed
		bc(cond, label);
	} else {
		// XXX, for future expansion
		bc(ma_cmp(lhs, rhs, cond), label);
	}
	ispew("   branchTest32(cond, reg, reg, l) ]]");
}

void
MacroAssemblerPPCCompat::branchTest64(Condition cond, Register64 lhs, Register64 rhs,
                                       Register temp, Label* label)
{
	ispew("branchTest64(cond, r64, r64, reg, l)");
    if (cond == Assembler::Zero) { // Hmmm.
        MOZ_ASSERT(lhs.low == rhs.low);
        MOZ_ASSERT(lhs.high == rhs.high);
        or_(tempRegister, lhs.low, lhs.high);
        branchTest32(cond, tempRegister, tempRegister, label);
    } else {
        MOZ_CRASH("Unsupported condition");
    }
}

void
MacroAssemblerPPCCompat::moveValue(const ValueOperand &src, const ValueOperand &dest)
{
	ispew("moveValue(vo, vo)");
    	
	Register s0 = src.typeReg(), d0 = dest.typeReg(),
		s1 = src.payloadReg(), d1 = dest.payloadReg();

	// Either one or both of the source registers could be the same as a
	// destination register.
	if (s1 == d0) {
		if (s0 == d1) {
			// If both are, this is just a swap of two registers.
			MOZ_ASSERT(d1 != tempRegister);
			MOZ_ASSERT(d0 != tempRegister);
			move32(d1, tempRegister);
			move32(d0, d1);
			move32(tempRegister, d0);
			return;
		}
		// If only one is, copy that source first.
		mozilla::Swap(s0, s1);
		mozilla::Swap(d0, d1);
	}

	if (s0 != d0)
		move32(s0, d0);
	if (s1 != d1)
		move32(s1, d1);
}

void
MacroAssemblerPPCCompat::branchPtrInNurseryRange(Condition cond, Register ptr, Register temp,
                                                  Label *label)
{
	ispew("[[ branchPtrInNurseryRange(cond, reg, reg, l)");
    MOZ_ASSERT(cond == Assembler::Equal || cond == Assembler::NotEqual);
    MOZ_ASSERT(ptr != temp);
    MOZ_ASSERT(ptr != addressTempRegister);

    const Nursery &nursery = GetJitContext()->runtime->gcNursery();
    movePtr(ImmWord(-ptrdiff_t(nursery.start())), addressTempRegister);
    addPtr(ptr, addressTempRegister);
    branchPtr(cond == Assembler::Equal ? Assembler::Below : Assembler::AboveOrEqual,
              addressTempRegister, Imm32(nursery.nurserySize()), label);
    ispew("   branchPtrInNurseryRange(cond, reg, reg, l) ]]");
}

void
MacroAssemblerPPCCompat::branchValueIsNurseryObject(Condition cond, ValueOperand value,
                                                     Register temp, Label *label)
{
    ispew("[[ branchValueIsNurseryObject(cond, vo, reg, l)");
    MOZ_ASSERT(cond == Assembler::Equal || cond == Assembler::NotEqual);
    BufferOffset done;

    if (cond == Assembler::Equal) {
        // The branch is always short.
        done = _bc(0,
            ma_cmp(value.typeReg(), ImmType(JSVAL_TYPE_OBJECT),
                Assembler::NotEqual));
    } else 
        branchTestObject(Assembler::NotEqual, value, label);
    branchPtrInNurseryRange(cond, value.payloadReg(), temp, label);

    if (cond == Assembler::Equal)
        bindSS(done);
    ispew("   branchValueIsNurseryObject(cond, vo, reg, l)");
}

void
MacroAssemblerPPCCompat::profilerEnterFrame(Register framePtr, Register scratch)
{
    AbsoluteAddress activation(GetJitContext()->runtime->addressOfProfilingActivation());
    loadPtr(activation, scratch);
    storePtr(framePtr, Address(scratch, JitActivation::offsetOfLastProfilingFrame()));
    storePtr(ImmPtr(nullptr), Address(scratch, JitActivation::offsetOfLastProfilingCallSite()));
}

void
MacroAssemblerPPCCompat::profilerExitFrame()
{
    branch(GetJitContext()->runtime->jitRuntime()->getProfilerExitFrameTail());
}

// irregexp
// 12 -> 21
void
MacroAssemblerPPC::load16ZeroExtendSwapped(
		const Address &address, const Register &dest)
{
	ispew("load16ZeroExtendSwapped(adr, reg)");
	ma_load(dest, address, SizeHalfWord, ZeroExtend, true);
} 

void
MacroAssemblerPPC::load16ZeroExtendSwapped(
		const BaseIndex &src, const Register &dest)
{
	ispew("load16ZeroExtendSwapped(bi, reg)");
	ma_load(dest, src, SizeHalfWord, ZeroExtend, true);
}

void
MacroAssemblerPPC::load16SignExtendSwapped(
		const Address &address, const Register &dest)
{
	ispew("load16SignExtendSwapped(adr, reg)");
	ma_load(dest, address, SizeHalfWord, SignExtend, true);
} 

void
MacroAssemblerPPC::load16SignExtendSwapped(
		const BaseIndex &src, const Register &dest)
{
	ispew("load16SignExtendSwapped(bi, reg)");
	ma_load(dest, src, SizeHalfWord, SignExtend, true);
}

// 4321 -> 1234
void
MacroAssemblerPPC::load32ByteSwapped(const Address &address, Register dest)
{
	ispew("load32ByteSwapped(adr, reg)");
	ma_load(dest, address, SizeWord, ZeroExtend, true);
}

void
MacroAssemblerPPC::load32ByteSwapped(const BaseIndex &src, Register dest)
{
	ispew("load32ByteSwapped(bi, reg)");
	ma_load(dest, src, SizeWord, ZeroExtend, true);
}

// 4321 -> 2143
void
MacroAssemblerPPC::load32WordSwapped(const Address &address, Register dest)
{
	ispew("load32WordSwapped(adr, reg)");
	ma_load(dest, address, SizeWord); // NOT swapped
	rlwinm(dest, dest, 16, 0, 31); // rotlwi
}

void
MacroAssemblerPPC::load32WordSwapped(const BaseIndex &src, Register dest)
{
	ispew("load32WordSwapped(bi, reg)");
	ma_load(dest, src, SizeWord); // NOT swapped
	rlwinm(dest, dest, 16, 0, 31); // rotlwi
}

void
MacroAssemblerPPC::point5Double(FloatRegister fpr)
{
    x_lis(tempRegister, 0x3F00); // 0.5 = 0x3f000000
    stwu(tempRegister, stackPointerRegister, -4);
#if _PPC970_
    // Two nops are the minimum to break the stwu and lfs apart.
    x_nop();
    x_nop();
#endif
    lfs(fpr, stackPointerRegister, 0);
    addi(stackPointerRegister, stackPointerRegister, 4);
}


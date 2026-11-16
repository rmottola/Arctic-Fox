/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_ppc_MacroAssembler_ppc_h
#define jit_ppc_MacroAssembler_ppc_h

/*

   IonPower (C)2015 Contributors to TenFourFox. All rights reserved.
 
   Authors: Cameron Kaiser <classilla@floodgap.com>
   with thanks to Ben Stuhl and David Kilbridge 
   and the authors of the ARM and MIPS ports
 
 */

#include "jsopcode.h"

#include "jit/AtomicOp.h"
#include "jit/IonCaches.h"
#include "jit/JitFrames.h"
#include "jit/osxppc/Assembler-ppc.h"
#include "jit/MoveResolver.h"
#define PPC_USE_UNSIGNED_COMPARE(x) (x & Assembler::ConditionUnsigned)

#if _PPC970_
#define MFCR0(x) mfocrf(x,cr0)
#else
#define MFCR0(x) mfcr(x)
#endif

namespace js {
namespace jit {

enum LoadStoreSize
{
    SizeByte = 8,
    SizeHalfWord = 16,
    SizeWord = 32,
    SizeDouble = 64
};

enum LoadStoreExtension
{
    ZeroExtend = 0,
    SignExtend = 1
};

enum JumpKind
{
    LongJump = 0,
    ShortJump = 1
};

struct ImmTag : public Imm32
{
    ImmTag(JSValueTag mask)
      : Imm32(int32_t(mask))
    { }
};

struct ImmType : public ImmTag
{
    ImmType(JSValueType type)
      : ImmTag(JSVAL_TYPE_TO_TAG(type))
    { }
};

static const ValueOperand JSReturnOperand = ValueOperand(JSReturnReg_Type, JSReturnReg_Data);

static Register CallReg = r12; // This appears to be specific to AsmJS.
static const int defaultShift = 3;
static const uint32_t LOW_32_MASK = 0xffffffff; // (1LL << 32) - 1;
static const int32_t LOW_32_OFFSET = 4;
static const int32_t HIGH_32_OFFSET = 0;
static_assert(1 << defaultShift == sizeof(JS::Value), "The defaultShift is wrong");

class MacroAssemblerPPC : public Assembler
{
  protected:
    // Perform a downcast. Should be removed by Bug 996602.
    MacroAssembler& asMasm();
    const MacroAssembler& asMasm() const;

  public:
    // higher level tag testing code
    Operand ToPayload(Operand base);
    Address ToPayload(Address base) {
        return ToPayload(Operand(base)).toAddress();
    }
    Operand ToType(Operand base);
    Address ToType(Address base) {
        return ToType(Operand(base)).toAddress();
    }

    void convertBoolToInt32(Register source, Register dest);
    void convertInt32ToDouble(Register src, FloatRegister dest);
    void convertInt32ToDouble(const Address &src, FloatRegister dest);
    void convertInt32ToDouble(const BaseIndex& src, FloatRegister dest);
    void convertUInt32ToDouble(Register src, FloatRegister dest);
    void mul64(Imm64 imm, const Register64& dest);
    void convertUInt64ToDouble(Register64 src, Register temp, FloatRegister dest);
    void mulDoublePtr(ImmPtr imm, Register temp, FloatRegister dest) {
        ispew("[[ mulDoublePtr(immptr, reg, fpr)");
        // mscdfr0!
        x_li32(addressTempRegister, imm.value);
        lfd(fpTempRegister, addressTempRegister, 0);
        fmul(dest, fpTempRegister, dest);
        ispew("   mulDoublePtr(immptr, reg, fpr) ]]");
    }
    void convertUInt32ToFloat32(Register src, FloatRegister dest);
    void convertDoubleToFloat32(FloatRegister src, FloatRegister dest);
    void branchTruncateDouble(FloatRegister src, Register dest, Label *fail);
    void convertDoubleToInt32(FloatRegister src, Register dest, Label *fail,
                              bool negativeZeroCheck = true);
    void convertFloat32ToInt32(FloatRegister src, Register dest, Label *fail,
                               bool negativeZeroCheck = true);

    void convertFloat32ToDouble(FloatRegister src, FloatRegister dest);
    void branchTruncateFloat32(FloatRegister src, Register dest, Label *fail);
    void convertInt32ToFloat32(Register src, FloatRegister dest);
    void convertInt32ToFloat32(const Address &src, FloatRegister dest);


    void addDouble(FloatRegister src, FloatRegister dest);
    void subDouble(FloatRegister src, FloatRegister dest);
    void mulDouble(FloatRegister src, FloatRegister dest);
    void divDouble(FloatRegister src, FloatRegister dest);

    void negateDouble(FloatRegister reg);
    void inc64(AbsoluteAddress dest);

  public:
    //
	// Convenience functions that cover over dancing around with immediates.
	//
	
    void ma_li32(Register dest, ImmGCPtr ptr);

    void ma_li32(Register dest, AbsoluteLabel *label);

    void ma_li32(Register dest, Imm32 imm);
    void ma_li32(Register dest, CodeOffset *label);
    void ma_li32Patchable(Register dest, Imm32 imm);
    void ma_li32Patchable(Register dest, ImmPtr imm);

    // and
    void ma_and(Register rd, Register rs, bool rc = false);
    void ma_and(Register rd, Register rs, Register rt, bool rc = false);
    void ma_and(Register rd, Imm32 imm); // rc is always true
    void ma_and(Register rd, Register rs, Imm32 imm); // rc is always true

    // or
    void ma_or(Register rd, Register rs);
    void ma_or(Register rd, Register rs, Register rt);
    void ma_or(Register rd, Imm32 imm);
    void ma_or(Register rd, Register rs, Imm32 imm);

    // xor
    void ma_xor(Register rd, Register rs);
    void ma_xor(Register rd, Register rs, Register rt);
    void ma_xor(Register rd, Imm32 imm);
    void ma_xor(Register rd, Register rs, Imm32 imm);

    // load (essentially the old load32)
    void ma_load(Register dest, Address address, LoadStoreSize size = SizeWord,
                 LoadStoreExtension extension = SignExtend,
                 bool swapped = false);
    void ma_load(Register dest, const BaseIndex &src, LoadStoreSize size = SizeWord,
                 LoadStoreExtension extension = SignExtend,
                 bool swapped = false);

    // store (essentially the old store32)
    void ma_store(Register data, Address address, LoadStoreSize size = SizeWord,
                  bool swapped = false);
    void ma_store(Register data, const BaseIndex &dest, LoadStoreSize size = SizeWord,
                  bool swapped = false);
    void ma_store(Imm32 imm, const BaseIndex &dest, LoadStoreSize size = SizeWord,
                  bool swapped = false);

    // arithmetic based ops
    // add
    void ma_addu(Register rd, Register rs, Imm32 imm);
    void ma_addu(Register rd, Register rs);
    void ma_addu(Register rd, Imm32 imm);
    void ma_addTestOverflow(Register rd, Register rs, Register rt, Label *overflow);
    void ma_addTestOverflow(Register rd, Register rs, Imm32 imm, Label *overflow);

    // subtract
    // Since we're heavily based on MIPS, this veneer handles conversion to MIPS arg order.
    void ma_subu(Register rd, Register rs, Register rt);
    void ma_subu(Register rd, Register rs, Imm32 imm);
    void ma_subu(Register rd, Imm32 imm);
    void ma_subTestOverflow(Register rd, Register rs, Register rt, Label *overflow);
    void ma_subTestOverflow(Register rd, Register rs, Imm32 imm, Label *overflow);

    // integer multiply
    void ma_mult(Register rs, Imm32 imm);
    void ma_mul_branch_overflow(Register rd, Register rs, Register rt, Label *overflow);
    void ma_mul_branch_overflow(Register rd, Register rs, Imm32 imm, Label *overflow);

    // integer divide
    void ma_div_branch_overflow(Register rd, Register rs, Register rt, Label *overflow);
    void ma_div_branch_overflow(Register rd, Register rs, Imm32 imm, Label *overflow);

	// PPC doesn't implement ma_mod_mask; it doesn't pay off.

    // memory
    // (shortcut for when we know we're transferring 32 bits of data)
    void ma_lw(Register data, Address address);

    void ma_sw(Register data, Address address);
    void ma_sw(Imm32 imm, Address address);
    void ma_sw(Register data, BaseIndex &address);

	// These know to turn "false" lr into a real m[ft]spr first.
    void ma_pop(Register r);
    void ma_push(Register r);

    void computeScaledAddress(const BaseIndex &address, Register dest);

    void computeEffectiveAddress(const Address &address, Register dest) {
        ma_addu(dest, address.base, Imm32(address.offset));
    }

    void computeEffectiveAddress(const BaseIndex &address, Register dest) {
        computeScaledAddress(address, dest);
        if (address.offset) {
            ma_addu(dest, dest, Imm32(address.offset));
        }
    }
    
    // Raw branches (just calls into the Assembler).
    BufferOffset _b(int32_t off, BranchAddressType bat = RelativeBranch, LinkBit lb = DontLinkB);
    BufferOffset _bc(int16_t off, Assembler::Condition cond, CRegisterID cr = cr0, LikelyBit lkb = NotLikelyB, LinkBit lb = DontLinkB);
    BufferOffset _bc(int16_t off, Assembler::DoubleCondition cond, CRegisterID = cr0, LikelyBit lkb = NotLikelyB, LinkBit lb = DontLinkB);    
    
    // Larger scale PowerPC branches. These actually emit stanzas.
#define PPC_B_STANZA_LENGTH 16
    void b(Label *l, bool is_call = false);
#define PPC_BC_STANZA_LENGTH 20
    void bc(Condition cond, Label *l, CRegisterID cr = cr0);
    void bc(DoubleCondition cond, Label *l, CRegisterID cr = cr0);
    void bc(uint32_t rawcond, Label *l);
    
    // Laziness
    void bl(Label *l);
    void bc(Condition cond, Label l, CRegisterID cr = cr0);

    // Floating point instructions.
    void ma_lis(FloatRegister dest, float value);
    void ma_lid(FloatRegister dest, double value);
    void ma_liNegZero(FloatRegister dest);

    void ma_mv(FloatRegister src, ValueOperand dest);
    void ma_mv(ValueOperand src, FloatRegister dest);

    void ma_ls(FloatRegister fd, Address address);
    void ma_ld(FloatRegister fd, Address address);
    void ma_sd(FloatRegister fd, Address address);
    void ma_sd(FloatRegister fd, BaseIndex address);
    void ma_ss(FloatRegister fd, Address address);
    void ma_ss(FloatRegister fd, BaseIndex address);

    void ma_pop(FloatRegister fs);
    void ma_push(FloatRegister fs);

    void fp2int(const FloatRegister &src, const Register &dest, bool crswap) {
        // Emulate the Intel cvttsd2si instruction, mostly for
        // IonMacroAssembler which uses it in some strange places.
        ispew("fp2int(fpr, reg, bool)");
        MOZ_ASSERT(src != fpTempRegister);

        // fctiwz. (not fctiwz) is also a dispatch group unto itself.
        fctiwz_rc(fpTempRegister, src);

        if (crswap) {
			// This code must require the branch in cr1.
            mcrf(cr0, cr1);
        }
        // Push on the stack so we can pull it off in pieces.
        // cracked on G5
        stfdu(fpTempRegister, stackPointerRegister, -8);
#ifdef _PPC970_
        // G5 and POWER4+ do better if the stfd and the lwz aren't in the
        // same dispatch group.
        x_nop();
#endif

        // Next dispatch group.
        // Pull out the lower 32 bits. This is the result.
        lwz(dest, stackPointerRegister, 4);
        // Remove the temporary frame.
        addi(stackPointerRegister, stackPointerRegister, 8);
}

  protected:
    // This has reduced safeties, and is an internal convenience only.
    void dangerous_convertInt32ToDouble(const Register &src, const FloatRegister &dest, const FloatRegister &tempfp) {
        // PowerPC has no GPR<->FPR moves, so we need a memory intermediate.
        // Since we might not have a linkage area, we do this on the stack.
        // See also OPPCC chapter 8 p.156.
        // Because Baseline expects to use f0, our normal temporary FPR,
        // we dedicate f2 to conversions (except when it isn't).
        MOZ_ASSERT(src != tempRegister);

        // Build zero double-precision FP constant.
        x_lis(tempRegister, 0x4330);  // 1 instruction
        // cracked on G5
        stwu(tempRegister, stackPointerRegister, -8);
        x_lis(tempRegister, 0x8000); // 1 instruction

        // 2nd G5 dispatch group
        stw(tempRegister, stackPointerRegister, 4); 
#ifdef _PPC970_
        // G5 and POWER4+ do better if the lfd and the stws aren't in the
        // same dispatch group.
        x_nop();
        x_nop();
        x_nop();
#endif
        
        // 3rd G5 dispatch group
        // Build intermediate float from zero constant.
        lfd(tempfp, stackPointerRegister, 0);
        // Flip sign of integer value and use as integer component.
        xoris(tempRegister, src, 0x8000);
#ifdef _PPC970_
        // G5 and POWER4+ do better if the lfd and the stws aren't in the
        // same dispatch group.
        x_nop();
        x_nop();
#endif

        // 4rd G5 dispatch group
        stw(tempRegister, stackPointerRegister, 4);
#ifdef _PPC970_
        x_nop();
        x_nop();
        x_nop();
#endif

        // Load and normalize with a subtraction operation.
        lfd(dest, stackPointerRegister, 0);
        fsub(dest, dest, tempfp);

        // Destroy temporary frame.
        addi(stackPointerRegister, stackPointerRegister, 8);
    }
  public:
  	// Used by Baseline
    Assembler::Condition ma_cmp(Register lhs, Register rhs, Assembler::Condition c);
    Assembler::Condition ma_cmp(Register lhs, Imm32 rhs, Assembler::Condition c);
    Assembler::DoubleCondition ma_fcmp(FloatRegister lhs, FloatRegister rhs, Assembler::DoubleCondition c);

	// Used by Ion
	Assembler::Condition ma_cmp(Register lhs, Address rhs, Assembler::Condition c);
	Assembler::Condition ma_cmp(Register lhs, ImmPtr rhs, Assembler::Condition c);
	Assembler::Condition ma_cmp(Register lhs, ImmGCPtr rhs, Assembler::Condition c);
	Assembler::Condition ma_cmp(Address lhs, Imm32 rhs, Assembler::Condition c);
	Assembler::Condition ma_cmp(Address lhs, Register rhs, Assembler::Condition c);
	Assembler::Condition ma_cmp(Address lhs, ImmGCPtr rhs, Assembler::Condition c);
	
  public:
	// Since this descends from the MIPS port, we held over a lot of the callJit
	// variants, but we use a highly simplified ABI that does not require stack
	// alignment. Thus, they're all just ma_callJit() under the hood, and we should
	// probably remove the others once it's clear they are no longer needed. XXX
    void ma_callJit(const Register reg);
    void ma_callJitNoPush(const Register reg);
    void ma_callJitHalfPush(const Register reg);
    void ma_callJitHalfPush(Label *label);

    void ma_call(ImmPtr dest);

    void ma_jump(ImmPtr dest);

	// These extract an already set condition register (i.e., CR0).
	void ma_cr_set_slow(Assembler::Condition c, Register dest);
	void ma_cr_set(Assembler::Condition c, Register dest);
	void ma_cr_set_slow(Assembler::DoubleCondition c, Register dest);
	void ma_cr_set(Assembler::DoubleCondition c, Register dest);
	// These try to avoid CR if a comparison has not yet been done, and can avoid serialization.
	void ma_cmp_set(Assembler::Condition cond, Address lhs, ImmPtr rhs, Register dest);
	void ma_cmp_set(Assembler::Condition cond, Register lhs, Imm32 rhs, Register dest);
	void ma_cmp_set(Assembler::Condition cond, Register lhs, ImmPtr rhs, Register dest);
	void ma_cmp_set(Assembler::Condition cond, Register lhs, Address rhs, Register dest);
	void ma_cmp_set(Assembler::Condition cond, Register lhs, Register rhs, Register dest);
	void ma_cmp_set(Assembler::DoubleCondition cond, FloatRegister lhs, FloatRegister rhs, Register dest);

    // Big-endian support for irregexp/typed arrays. These just call ma_load with the swapped flag.
    void load16ZeroExtendSwapped(const Address &address, const Register &dest);
    void load16ZeroExtendSwapped(const BaseIndex &src, const Register &dest);
    void load16SignExtendSwapped(const Address &address, const Register &dest);
    void load16SignExtendSwapped(const BaseIndex &src, const Register &dest);
    void load32ByteSwapped(const Address &address, Register dest);
    void load32ByteSwapped(const BaseIndex &src, Register dest);
    // These require rlwinm magic.
    void load32WordSwapped(const Address &address, Register dest);
    void load32WordSwapped(const BaseIndex &src, Register dest);	

    // Convenience (faster than loadConstantDouble with 0.5).
    void point5Double(FloatRegister reg); // for our convenience
};

class MacroAssemblerPPCCompat : public MacroAssemblerPPC
{
  public:
  	MacroAssemblerPPCCompat() { }
  	
    void j(Label *dest) {
        b(dest);
    }
    void j(Condition c, Label *l) {
    	bc(c, l);
    }

	// This set of x86-isms makes me puke.
    void mov(Register src, Register dest) {
        x_mr(dest, src);
    }
    void mov(ImmWord imm, Register dest) {
        ma_li32(dest, Imm32(imm.value));
    }
    void mov(ImmPtr imm, Register dest) {
        mov(ImmWord(uintptr_t(imm.value)), dest);
    }
    void mov(Register src, Address dest) {
        MOZ_CRASH("NYI-IC");
    }
    void mov(Address src, Register dest) {
        MOZ_CRASH("NYI-IC");
    }

    void branch(JitCode *c) {
        BufferOffset bo = m_buffer.nextOffset();
        addPendingJump(bo, ImmPtr(c->raw()), Relocation::JITCODE);
        ma_li32Patchable(tempRegister, Imm32((uint32_t)c->raw()));
        branch(tempRegister);
    }
    void branch(const Register reg) {
        x_mtctr(reg);
#if defined(_PPC970_)
		// Sigh.
		x_nop();
		x_nop();
		x_nop();
		x_nop();
#endif
		bctr(DontLinkB);        
    }
    
    void nop() {
        x_nop();
    }
    
    void ret() {
    	retn(Imm32(4));
    }
    void retn(Imm32 n);
    void push(Imm32 imm) {
        x_li32(tempRegister, imm.value);
        ma_push(tempRegister);
    }
    void push(ImmWord imm) {
        x_li32(tempRegister, imm.value);
        ma_push(tempRegister);
    }
    void push(ImmGCPtr imm) {
        ma_li32(tempRegister, imm); // since this is GC
        ma_push(tempRegister);
    }
    void push(const Address &address) {
        ma_lw(tempRegister, address);
        ma_push(tempRegister);
    }
    void push(Register reg) { // Use ma_push for the "fake LR" fiction.
        ma_push(reg);
    }
    void push(FloatRegister reg) {
        ma_push(reg);
    }
    void pop(Register reg) { // Same.
        ma_pop(reg);
    }
    void pop(FloatRegister reg) {
        ma_pop(reg);
    }

    // Optimized multi-push routines. These avoid cracking on G5. The
    // sp gets serialized slightly here, but the stores can occur in
    // a superscalar manner since the effective addresses are independent.
    // Storing into the red zone is unsafe for this purpose, so we don't,
    // and SysV ABI doesn't have one anyway.
    //
    // Since we're the only consumers of this routine, we don't do fake LR here,
    // so let's assert on that.
    
    void push2(const Register &rr0, const Register &rr1) {
    	MOZ_ASSERT(rr0 != lr);
    	MOZ_ASSERT(rr1 != lr);
        addi(stackPointerRegister, stackPointerRegister, -8);
        stw(rr0, stackPointerRegister, 4);
        stw(rr1, stackPointerRegister, 0);
    }
    void push3(const Register &rr0, const Register &rr1, const Register &rr2) {
    	MOZ_ASSERT(rr0 != lr);
    	MOZ_ASSERT(rr1 != lr);
    	MOZ_ASSERT(rr2 != lr);
        addi(stackPointerRegister, stackPointerRegister, -12);
        stw(rr0, stackPointerRegister, 8);
        stw(rr1, stackPointerRegister, 4);
        stw(rr2, stackPointerRegister, 0);
    }
    void push4(const Register &rr0, const Register &rr1, const Register &rr2,
        const Register &rr3) {
    	MOZ_ASSERT(rr0 != lr);
    	MOZ_ASSERT(rr1 != lr);
    	MOZ_ASSERT(rr2 != lr);
    	MOZ_ASSERT(rr3 != lr);
        addi(stackPointerRegister, stackPointerRegister, -16);
        stw(rr0, stackPointerRegister, 12);
        stw(rr1, stackPointerRegister, 8);
        stw(rr2, stackPointerRegister, 4);
        stw(rr3, stackPointerRegister, 0);
    }
    void push5(const Register &rr0, const Register &rr1, const Register &rr2,
        const Register &rr3, const Register &rr4) {
    	MOZ_ASSERT(rr0 != lr);
    	MOZ_ASSERT(rr1 != lr);
    	MOZ_ASSERT(rr2 != lr);
    	MOZ_ASSERT(rr3 != lr);
    	MOZ_ASSERT(rr4 != lr);
        addi(stackPointerRegister, stackPointerRegister, -20);
        stw(rr0, stackPointerRegister, 16);
        stw(rr1, stackPointerRegister, 12);
        stw(rr2, stackPointerRegister, 8);
        stw(rr3, stackPointerRegister, 4);
        stw(rr4, stackPointerRegister, 0);
    }

    // And, similarly, multipop.
    void pop2(const Register &rr0, const Register &rr1) {
    	MOZ_ASSERT(rr0 != lr);
    	MOZ_ASSERT(rr1 != lr);
        lwz(rr0, stackPointerRegister, 0);
        lwz(rr1, stackPointerRegister, 4);
        addi(stackPointerRegister, stackPointerRegister, 8);
    }
    void pop3(const Register &rr0, const Register &rr1, const Register &rr2) {
    	MOZ_ASSERT(rr0 != lr);
    	MOZ_ASSERT(rr1 != lr);
    	MOZ_ASSERT(rr2 != lr);
        lwz(rr0, stackPointerRegister, 0);
        lwz(rr1, stackPointerRegister, 4);
        lwz(rr2, stackPointerRegister, 8);
        addi(stackPointerRegister, stackPointerRegister, 12);
    }
    void pop4(const Register &rr0, const Register &rr1, const Register &rr2,
        const Register &rr3) {
    	MOZ_ASSERT(rr0 != lr);
    	MOZ_ASSERT(rr1 != lr);
    	MOZ_ASSERT(rr2 != lr);
    	MOZ_ASSERT(rr3 != lr);
        lwz(rr0, stackPointerRegister, 0);
        lwz(rr1, stackPointerRegister, 4);
        lwz(rr2, stackPointerRegister, 8);
        lwz(rr3, stackPointerRegister, 12);
        addi(stackPointerRegister, stackPointerRegister, 16);
    }
    void pop5(const Register &rr0, const Register &rr1, const Register &rr2,
        const Register &rr3, const Register &rr4) {
    	MOZ_ASSERT(rr0 != lr);
    	MOZ_ASSERT(rr1 != lr);
    	MOZ_ASSERT(rr2 != lr);
    	MOZ_ASSERT(rr3 != lr);
    	MOZ_ASSERT(rr4 != lr);
        lwz(rr0, stackPointerRegister, 0);
        lwz(rr1, stackPointerRegister, 4);
        lwz(rr2, stackPointerRegister, 8);
        lwz(rr3, stackPointerRegister, 12);
        lwz(rr4, stackPointerRegister, 16);
        addi(stackPointerRegister, stackPointerRegister, 20);
    }

	// Moved to -ppc.cpp because they need to call the parent MacroAssembler.
    void Push2(const Register &rr0, const Register &rr1);
    void Push3(const Register &rr0, const Register &rr1, const Register &rr2);
    void Push4(const Register &rr0, const Register &rr1, const Register &rr2, const Register &rr3);
    void Push5(const Register &rr0, const Register &rr1, const Register &rr2, const Register &rr3, const Register &rr4);
    void Pop2(const Register &rr0, const Register &rr1);
    void Pop3(const Register &rr0, const Register &rr1, const Register &rr2);
    void Pop4(const Register &rr0, const Register &rr1, const Register &rr2, const Register &rr3);
    void Pop5(const Register &rr0, const Register &rr1, const Register &rr2, const Register &rr3, const Register &rr4);

    CodeOffset toggledJump(Label *label);
    CodeOffset toggledCall(JitCode *target, bool enabled);
    static size_t ToggledCallSize(uint8_t *code) {
        // Always a long call. There is no leading nop like in a toggled jump.
        return 4 * sizeof(uint32_t);
    }

    CodeOffset pushWithPatch(ImmWord imm) {
        CodeOffset label = movWithPatch(imm, tempRegister);
        ma_push(tempRegister);
        return label;
    }

    CodeOffset movWithPatch(ImmWord imm, Register dest) {
        CodeOffset label = CodeOffset(currentOffset());
        ma_li32Patchable(dest, Imm32(imm.value));
        return label;
    }
    CodeOffset movWithPatch(ImmPtr imm, Register dest) {
        return movWithPatch(ImmWord(uintptr_t(imm.value)), dest);
    }

    void jump(Label *label) {
        b(label);
    }
    void jump(Register reg) {
    	branch(reg);
    }
    void jump(const Address &address) {
        ma_lw(tempRegister, address);
        branch(tempRegister);
    }
    void jump(JitCode *code) {
    	branch(code);
    }

    void neg32(Register reg) {
        neg(reg, reg);
    }
    void negl(Register reg) {
        neg(reg, reg);
    }

    // Returns the register containing the type tag.
    Register splitTagForTest(const ValueOperand &value) {
        return value.typeReg();
    }

    void branchTestGCThing(Condition cond, const Address &address, Label *label);
    void branchTestGCThing(Condition cond, const BaseIndex &src, Label *label);

    void branchTestPrimitive(Condition cond, const ValueOperand &value, Label *label);
    void branchTestPrimitive(Condition cond, Register tag, Label *label);

    void branchTestValue(Condition cond, const ValueOperand &value, const Value &v, Label *label);
    void branchTestValue(Condition cond, const Address &valaddr, const ValueOperand &value,
                         Label *label);

    // unboxing code
    void unboxNonDouble(const ValueOperand &operand, Register dest);
    void unboxNonDouble(const Address &src, Register dest);
    void unboxInt32(const ValueOperand &operand, Register dest);
    void unboxInt32(const Address &src, Register dest);
    void unboxBoolean(const ValueOperand &operand, Register dest);
    void unboxBoolean(const Address &src, Register dest);
    void unboxDouble(const ValueOperand &operand, FloatRegister dest);
    void unboxDouble(const Address &src, FloatRegister dest);
    void unboxString(const ValueOperand &operand, Register dest);
    void unboxString(const Address &src, Register dest);
    void unboxObject(const ValueOperand &src, Register dest);
    void unboxObject(const Address &src, Register dest);
    void unboxObject(const BaseIndex &bi, Register dest);
    void unboxValue(const ValueOperand &src, AnyRegister dest);
    void unboxPrivate(const ValueOperand &src, Register dest);

    void notBoolean(const ValueOperand &val) {
        xori(val.payloadReg(), val.payloadReg(), 1);
    }

    // boxing code
    void boxDouble(FloatRegister src, const ValueOperand &dest);
    void boxNonDouble(JSValueType type, Register src, const ValueOperand &dest);

    // Extended unboxing API. If the payload is already in a register, returns
    // that register. Otherwise, provides a move to the given scratch register,
    // and returns that.
    Register extractObject(const Address &address, Register scratch);
    Register extractObject(const ValueOperand &value, Register scratch) {
        return value.payloadReg();
    }
    Register extractInt32(const ValueOperand &value, Register scratch) {
        return value.payloadReg();
    }
    Register extractBoolean(const ValueOperand &value, Register scratch) {
        return value.payloadReg();
    }
    Register extractTag(const Address &address, Register scratch);
    Register extractTag(const BaseIndex &address, Register scratch);
    Register extractTag(const ValueOperand &value, Register scratch) {
        return value.typeReg();
    }

    void boolValueToDouble(const ValueOperand &operand, FloatRegister dest);
    void int32ValueToDouble(const ValueOperand &operand, FloatRegister dest);
    void loadInt32OrDouble(const Address &address, FloatRegister dest);
    void loadInt32OrDouble(Register base, Register index,
                           FloatRegister dest, int32_t shift = defaultShift);
    void loadConstantDouble(double dp, FloatRegister dest);

    void boolValueToFloat32(const ValueOperand &operand, FloatRegister dest);
    void int32ValueToFloat32(const ValueOperand &operand, FloatRegister dest);
    void loadConstantFloat32(float f, FloatRegister dest);

    void branchTestInt32(Condition cond, const ValueOperand &value, Label *label);
    void branchTestInt32(Condition cond, Register tag, Label *label);
    void branchTestInt32(Condition cond, const Address &address, Label *label);
    void branchTestInt32(Condition cond, const BaseIndex &src, Label *label);

    void branchTestBoolean(Condition cond, const ValueOperand &value, Label *label);
    void branchTestBoolean(Condition cond, Register tag, Label *label);
    void branchTestBoolean(Condition cond, const BaseIndex &src, Label *label);
    void branchTestBoolean(Condition cond, const Address &address, Label *label);

    void branch32(Condition cond, Register lhs, Register rhs, Label *label) {
    	ispew("branch32(cond, reg, reg, l)");
    	bc(ma_cmp(lhs, rhs, cond), label);
    }
    void branch32(Condition cond, Register lhs, Imm32 imm, Label *label) {
    	ispew("branch32(cond, reg, imm, l)");
    	bc(ma_cmp(lhs, imm, cond), label);
    }
    void branch32(Condition cond, Register lhs, ImmWord immw, Label *label) {
    	ispew("branch32(cond, reg, immw, l)");
    	bc(ma_cmp(lhs, Imm32(immw.value), cond), label);
    }
    void branch32(Condition cond, const Operand &lhs, Register rhs, Label *label) {
    	ispew("branch32(cond, o, reg, l)");
        if (lhs.getTag() == Operand::REG) {
        	bc(ma_cmp(lhs.toReg(), rhs, cond), label);
        } else {
            branch32(cond, lhs.toAddress(), rhs, label);
        }
    }
    void branch32(Condition cond, const Operand &lhs, Imm32 rhs, Label *label) {
    	ispew("branch32(cond, o, imm, l)");
        if (lhs.getTag() == Operand::REG) {
        	bc(ma_cmp(lhs.toReg(), rhs, cond), label);
        } else {
            branch32(cond, lhs.toAddress(), rhs, label);
        }
    }
    void branch32(Condition cond, const Address &lhs, Register rhs, Label *label) {
    	ispew("branch32(cond, adr, reg, l)");
        ma_lw(tempRegister, lhs);
        bc(ma_cmp(tempRegister, rhs, cond), label);
    }
    void branch32(Condition cond, const Address &lhs, Imm32 rhs, Label *label) {
    	ispew("branch32(cond, adr, imm, l)");
        ma_lw(tempRegister, lhs);
        bc(ma_cmp(tempRegister, rhs, cond), label);
    }
    void branch32(Condition cond, const BaseIndex &lhs, Imm32 rhs, Label *label) {
    	ispew("[[ branch32(cond, bi, imm, l)");
        load32(lhs, tempRegister);
        bc(ma_cmp(tempRegister, rhs, cond), label);
        ispew("   branch32(cond, bi, imm, l) ]]");
    }

    // XXX: Use unsigned comparisons?
    void branchPtr(Condition cond, Register lhs, Register rhs, Label *label) {
    	branch32(cond, lhs, rhs, label);
    }
    void branchPtr(Condition cond, Register lhs, Imm32 imm, Label *label) {
    	branch32(cond, lhs, imm, label);
    }
    void branchPtr(Condition cond, Register lhs, ImmGCPtr ptr, Label *label) {
    	ispew("[[ branchPtr(cond, reg, immgcptr, l)");
        ma_li32(tempRegister, ptr);
        branch32(cond, lhs, tempRegister, label);
        ispew("   branchPtr(cond, reg, immgcptr, l) ]]");
    }
    void branchPtr(Condition cond, Register lhs, ImmWord imm, Label *label) {
    	branch32(cond, lhs, Imm32(imm.value), label);
    }
    void branchPtr(Condition cond, Register lhs, ImmPtr imm, Label *label) {
        branchPtr(cond, lhs, ImmWord(uintptr_t(imm.value)), label);
    }
#if(0)
    void branchPtr(Condition cond, Register lhs, AsmJSImmPtr imm, Label *label) {
    	ispew("[[ branchPtr(cond, reg, asmjsimmptr, l)");
        movePtr(imm, tempRegister);
        branchPtr(cond, lhs, tempRegister, label);
        ispew("   branchPtr(cond, reg, asmjsimmptr, l) ]]");
    }
#endif
    void branchPtr(Condition cond, const Address &lhs, Register rhs, Label *label) {
        branch32(cond, lhs, rhs, label);
    }
    void decBranchPtr(Condition cond, Register lhs, Imm32 imm, Label *label) {
        subPtr(imm, lhs);
        branch32(cond, lhs, Imm32(0), label);
    }

    void branchPrivatePtr(Condition cond, const Address &lhs, ImmPtr ptr, Label *label) {
        branchPtr(cond, lhs, ptr, label);
    }
    void branchPrivatePtr(Condition cond, const Address &lhs, Register ptr, Label *label) {
        branchPtr(cond, lhs, ptr, label);
    }
    void branchPrivatePtr(Condition cond, Register lhs, ImmWord ptr, Label *label) {
        branchPtr(cond, lhs, ptr, label);
    }

    void branchTestDouble(Condition cond, const ValueOperand &value, Label *label);
    void branchTestDouble(Condition cond, Register tag, Label *label);
    void branchTestDouble(Condition cond, const Address &address, Label *label);
    void branchTestDouble(Condition cond, const BaseIndex &src, Label *label);

    void branchTestNull(Condition cond, const ValueOperand &value, Label *label);
    void branchTestNull(Condition cond, Register tag, Label *label);
    void branchTestNull(Condition cond, const Address &src, Label *label);
    void branchTestNull(Condition cond, const BaseIndex &src, Label *label);
    void testNullSet(Condition cond, const ValueOperand &value, Register dest);

    void branchTestObject(Condition cond, const ValueOperand &value, Label *label);
    void branchTestObject(Condition cond, Register tag, Label *label);
    void branchTestObject(Condition cond, const BaseIndex &src, Label *label);
    void branchTestObject(Condition cond, const Address& address, Label* label);
    void testObjectSet(Condition cond, const ValueOperand &value, Register dest);

    void branchTestString(Condition cond, const ValueOperand &value, Label *label);
    void branchTestString(Condition cond, Register tag, Label *label);
    void branchTestString(Condition cond, const BaseIndex &src, Label *label);

    void branchTestSymbol(Condition cond, const ValueOperand &value, Label *label);
    void branchTestSymbol(Condition cond, const Register &tag, Label *label);
    void branchTestSymbol(Condition cond, const BaseIndex &src, Label *label);

    void branchTestUndefined(Condition cond, const ValueOperand &value, Label *label);
    void branchTestUndefined(Condition cond, Register tag, Label *label);
    void branchTestUndefined(Condition cond, const BaseIndex &src, Label *label);
    void branchTestUndefined(Condition cond, const Address &address, Label *label);
    void testUndefinedSet(Condition cond, const ValueOperand &value, Register dest);

    void branchTestNumber(Condition cond, const ValueOperand &value, Label *label);
    void branchTestNumber(Condition cond, Register tag, Label *label);

    void branchTestMagic(Condition cond, const ValueOperand &value, Label *label);
    void branchTestMagic(Condition cond, Register tag, Label *label);
    void branchTestMagic(Condition cond, const Address &address, Label *label);
    void branchTestMagic(Condition cond, const BaseIndex &src, Label *label);

    void branchTestMagicValue(Condition cond, const ValueOperand &val, JSWhyMagic why,
                              Label *label) {
        ispew("branchTestMagicValue(cond, vo, jswhymagic, l)");
        MOZ_ASSERT(cond == Equal || cond == NotEqual);
        branchTestValue(cond, val, MagicValue(why), label);
    }

    void branchTestInt32Truthy(bool b, const ValueOperand &value, Label *label);

    void branchTestStringTruthy(bool b, const ValueOperand &value, Label *label);

    void branchTestDoubleTruthy(bool b, FloatRegister value, Label *label);

    void branchTestBooleanTruthy(bool b, const ValueOperand &operand, Label *label);

    void branchTest32(Condition cond, Register lhs, Register rhs, Label *label);
    
    void branchTest32(Condition cond, Register lhs, Imm32 imm, Label *label) {
        MOZ_ASSERT(lhs != addressTempRegister);
        ma_li32(addressTempRegister, imm); // safe, since no address load.
        branchTest32(cond, lhs, addressTempRegister, label);
    }
    void branchTest32(Condition cond, const Address &address, Imm32 imm, Label *label) {
        ma_lw(tempRegister, address);
        branchTest32(cond, tempRegister, imm, label);
    }
    void branchTest32(Condition cond, AbsoluteAddress address, Imm32 imm, Label *label) {
        loadPtr(address, tempRegister);
        branchTest32(cond, tempRegister, imm, label);
    }
    void branchTest64(Condition cond, Register64 lhs, Register64 rhs, Register temp,
                      Label* label);
    void branchTestPtr(Condition cond, Register lhs, Register rhs, Label *label) {
        branchTest32(cond, lhs, rhs, label);
    }
    void branchTestPtr(Condition cond, Register lhs, const Imm32 rhs, Label *label) {
        branchTest32(cond, lhs, rhs, label);
    }
    void branchTestPtr(Condition cond, const Address &lhs, Imm32 imm, Label *label) {
        branchTest32(cond, lhs, imm, label);
    }
    
protected:
    uint32_t getType(const Value &val);
    void moveData(const Value &val, Register data);
public:
    void moveValue(const Value &val, Register type, Register data);

    CodeOffsetJump backedgeJump(RepatchLabel *label, Label* documentation = nullptr);
    CodeOffsetJump jumpWithPatch(RepatchLabel *label, Condition cond = Always, Label* documentation = nullptr);
    CodeOffsetJump jumpWithPatch(RepatchLabel *label, Label* documentation);

    template <typename T>
    CodeOffsetJump branchPtrWithPatch(Condition cond, Register reg, T ptr, RepatchLabel *label) {
    	ispew("[[ branchPtrWithPatch(cond, reg, T, rl)");
    	MOZ_ASSERT(reg != tempRegister);
        Label skipJump;
    	
    	m_buffer.ensureSpace(16 + 20 + 20); // at least as long as longest full stanza
        movePtr(ptr, tempRegister); // should be enough in case T is a BaseIndex
        
        bc(ma_cmp(reg, tempRegister, InvertCondition(cond)), &skipJump); // SHORT
        CodeOffsetJump off = jumpWithPatch(label);
        bind(&skipJump);
        
        ispew("   branchPtrWithPatch(cond, reg, T, rl) ]]");
        return off;
    }

    template <typename T>
    CodeOffsetJump branchPtrWithPatch(Condition cond, Address addr, T ptr, RepatchLabel *label) {
    	ispew("[[ branchPtrWithPatch(cond, adr, T, rl)");
        Label skipJump;
    	
    	m_buffer.ensureSpace(24 + 20 + 20); // at least as long as longest full stanza
        loadPtr(addr, tempRegister);
        movePtr(ptr, addressTempRegister);
        
        bc(ma_cmp(tempRegister, addressTempRegister, InvertCondition(cond)), &skipJump); // SHORT
        CodeOffsetJump off = jumpWithPatch(label);
        bind(&skipJump);
        
        ispew("   branchPtrWithPatch(cond, adr, T, rl) ]]");
        return off;
    }
    void branchPtr(Condition cond, Address addr, ImmGCPtr ptr, Label *label) {
    	ispew("[[ branchPtr(cond, adr, immgcptr, l)");
        ma_lw(tempRegister, addr);
        ma_li32(addressTempRegister, ptr);
        bc(ma_cmp(tempRegister, addressTempRegister, cond), label);
        ispew("   branchPtr(cond, adr, immgcptr, l) ]]");
    }
    void branchPtr(Condition cond, Address addr, ImmWord ptr, Label *label) {
    	ispew("branchPtr(cond, adr, immw, l)");
        ma_lw(tempRegister, addr);
        bc(ma_cmp(tempRegister, Imm32(ptr.value), cond), label);
    }
    void branchPtr(Condition cond, Address addr, ImmPtr ptr, Label *label) {
        branchPtr(cond, addr, ImmWord(uintptr_t(ptr.value)), label);
    }
    void branchPtr(Condition cond, AbsoluteAddress addr, Register ptr, Label *label) {
    	ispew("branchPtr(cond, aadr, reg, l)");
    	MOZ_ASSERT(ptr != tempRegister);
    	MOZ_ASSERT(ptr != addressTempRegister);
    	
        loadPtr(addr, tempRegister);
        bc(ma_cmp(tempRegister, ptr, cond), label);
    }
    void branchPtr(Condition cond, AbsoluteAddress addr, ImmWord ptr, Label *label) {
    	ispew("branchPtr(cond, aadr, immw, l)");
        loadPtr(addr, tempRegister);
        bc(ma_cmp(tempRegister, Imm32(ptr.value), cond), label);
    }
    void branchPtr(Condition cond, wasm::SymbolicAddress addr, Register ptr,
                   Label *label) { // XXX
        ispew("branchPtr(cond, wasmsa, reg, l)");
    	MOZ_ASSERT(ptr != tempRegister);
    	MOZ_ASSERT(ptr != addressTempRegister);
    	
        loadPtr(addr, tempRegister);
        bc(ma_cmp(tempRegister, ptr, cond), label);
    }
    void branch32(Condition cond, AbsoluteAddress lhs, Imm32 rhs, Label *label) {
    	ispew("branch32(cond, aadr, imm, l)");
        loadPtr(lhs, tempRegister);
        bc(ma_cmp(tempRegister, rhs, cond), label);
    }
    void branch32(Condition cond, AbsoluteAddress lhs, Register rhs, Label *label) {
    	ispew("branch32(cond, aadr, reg, l)");
    	MOZ_ASSERT(rhs != tempRegister);
    	
        loadPtr(lhs, tempRegister);
        bc(ma_cmp(tempRegister, rhs, cond), label);
    }
    void branch32(Condition cond, wasm::SymbolicAddress addr, Imm32 imm,
                  Label* label) { // XXX
        ispew("XXX branch32(cond, wasmsa, imm, l)");
        loadPtr(addr, tempRegister);
        bc(ma_cmp(tempRegister, imm, cond), label);
    }

    void loadUnboxedValue(Address address, MIRType type, AnyRegister dest);
    void loadUnboxedValue(BaseIndex address, MIRType type, AnyRegister dest);

    template <typename T>
    void storeUnboxedValue(ConstantOrRegister value, MIRType valueType, const T &dest,
                           MIRType slotType);

    template <typename T>
    void storeUnboxedPayload(ValueOperand value, T address, size_t nbytes) {
        switch (nbytes) {
          case 4:
            storePtr(value.payloadReg(), address);
            return;
          case 1:
            store8(value.payloadReg(), address);
            return;
          default: MOZ_CRASH("Bad payload width");
        }
    }

    void moveValue(const Value &val, const ValueOperand &dest);
    void moveValue(const ValueOperand &src, const ValueOperand &dest);

    void storeValue(ValueOperand val, Operand dst);
    void storeValue(ValueOperand val, const BaseIndex &dest);
    void storeValue(JSValueType type, Register reg, BaseIndex dest);
    void storeValue(ValueOperand val, const Address &dest);
    void storeValue(JSValueType type, Register reg, Address dest);
    void storeValue(const Value &val, Address dest);
    void storeValue(const Value &val, BaseIndex dest);

    void loadValue(Address src, ValueOperand val);
    void loadValue(Operand dest, ValueOperand val) {
        loadValue(dest.toAddress(), val);
    }
    void loadValue(const BaseIndex &addr, ValueOperand val);
    void tagValue(JSValueType type, Register payload, ValueOperand dest);

    void pushValue(ValueOperand val);
    void popValue(ValueOperand val);
    void pushValue(const Value &val) {
    	ispew("pushValue(v)");
        jsval_layout jv = JSVAL_TO_IMPL(val);
        // Payload FIRST! ENDIAN!
        if (val.isMarkable())
            push(ImmGCPtr(reinterpret_cast<gc::Cell *>(val.toGCThing())));
        else
            push(Imm32(jv.s.payload.i32));
        push(Imm32(jv.s.tag));
    }
    void pushValue(JSValueType type, Register reg) {
    	ispew("pushValue(jsvaluetype, reg)");
    	// Payload FIRST! ENDIAN!
        ma_push(reg);
        push(ImmTag(JSVAL_TYPE_TO_TAG(type)));
    }
    void pushValue(const Address &addr);
    void storePayload(const Value &val, Address dest);
    void storePayload(Register src, Address dest);
    void storePayload(const Value &val, const BaseIndex &dest);
    void storePayload(Register src, const BaseIndex &dest);
    void storeTypeTag(ImmTag tag, Address dest);
    void storeTypeTag(ImmTag tag, const BaseIndex &dest);

    void makeFrameDescriptor(Register frameSizeReg, FrameType type) {
        x_slwi(frameSizeReg, frameSizeReg, FRAMESIZE_SHIFT);
        ma_or(frameSizeReg, frameSizeReg, Imm32(type));
    }

    void handleFailureWithHandlerTail(void *handler);

    /////////////////////////////////////////////////////////////////
    // Common interface.
    /////////////////////////////////////////////////////////////////
  public:
    // The following functions are exposed for use in platform-shared code.

    template<typename T>
    void compareExchange8SignExtend(const T &mem, Register oldval, Register newval, Register output)
    {
        MOZ_CRASH("NYI");
    }
    template<typename T>
    void compareExchange8ZeroExtend(const T &mem, Register oldval, Register newval, Register output)
    {
        MOZ_CRASH("NYI");
    }
    template<typename T>
    void compareExchange16SignExtend(const T &mem, Register oldval, Register newval, Register output)
    {
        MOZ_CRASH("NYI");
    }
    template<typename T>
    void compareExchange16ZeroExtend(const T &mem, Register oldval, Register newval, Register output)
    {
        MOZ_CRASH("NYI");
    }
    template<typename T>
    void compareExchange32(const T &mem, Register oldval, Register newval, Register output)
    {
        MOZ_CRASH("NYI");
    }

    template<typename T>
    void atomicExchange8SignExtend(const T& mem, Register value, Register output)
    {
        MOZ_CRASH("NYI");
    }
    template<typename T>
    void atomicExchange8ZeroExtend(const T& mem, Register value, Register output)
    {
        MOZ_CRASH("NYI");
    }
    template<typename T>
    void atomicExchange16SignExtend(const T& mem, Register value, Register output)
    {
        MOZ_CRASH("NYI");
    }
    template<typename T>
    void atomicExchange16ZeroExtend(const T& mem, Register value, Register output)
    {
        MOZ_CRASH("NYI");
    }
    template<typename T>
    void atomicExchange32(const T& mem, Register value, Register output)
    {
        MOZ_CRASH("NYI");
    }

    template<typename T, typename S>
    void atomicFetchAdd8SignExtend(const S &value, const T &mem, Register temp, Register output) {
        MOZ_CRASH("NYI");
    }
    template<typename T, typename S>
    void atomicFetchAdd8ZeroExtend(const S &value, const T &mem, Register temp, Register output) {
        MOZ_CRASH("NYI");
    }
    template<typename T, typename S>
    void atomicFetchAdd16SignExtend(const S &value, const T &mem, Register temp, Register output) {
        MOZ_CRASH("NYI");
    }
    template<typename T, typename S>
    void atomicFetchAdd16ZeroExtend(const S &value, const T &mem, Register temp, Register output) {
        MOZ_CRASH("NYI");
    }
    template<typename T, typename S>
    void atomicFetchAdd32(const S &value, const T &mem, Register temp, Register output) {
        MOZ_CRASH("NYI");
    }
    template <typename T, typename S>
    void atomicAdd8(const T& value, const S& mem) {
        MOZ_CRASH("NYI");
    }
    template <typename T, typename S>
    void atomicAdd16(const T& value, const S& mem) {
        MOZ_CRASH("NYI");
    }
    template <typename T, typename S>
    void atomicAdd32(const T& value, const S& mem) {
        MOZ_CRASH("NYI");
    }

    template<typename T, typename S>
    void atomicFetchSub8SignExtend(const S &value, const T &mem, Register temp, Register output) {
        MOZ_CRASH("NYI");
    }
    template<typename T, typename S>
    void atomicFetchSub8ZeroExtend(const S &value, const T &mem, Register temp, Register output) {
        MOZ_CRASH("NYI");
    }
    template<typename T, typename S>
    void atomicFetchSub16SignExtend(const S &value, const T &mem, Register temp, Register output) {
        MOZ_CRASH("NYI");
    }
    template<typename T, typename S>
    void atomicFetchSub16ZeroExtend(const S &value, const T &mem, Register temp, Register output) {
        MOZ_CRASH("NYI");
    }
    template<typename T, typename S>
    void atomicFetchSub32(const S &value, const T &mem, Register temp, Register output) {
        MOZ_CRASH("NYI");
    }
    template <typename T, typename S> void atomicSub8(const T& value, const S& mem) {
        MOZ_CRASH("NYI");
    }
    template <typename T, typename S> void atomicSub16(const T& value, const S& mem) {
        MOZ_CRASH("NYI");
    }
    template <typename T, typename S> void atomicSub32(const T& value, const S& mem) {
        MOZ_CRASH("NYI");
    }

    template<typename T, typename S>
    void atomicFetchAnd8SignExtend(const S &value, const T &mem, Register temp, Register output) {
        MOZ_CRASH("NYI");
    }
    template<typename T, typename S>
    void atomicFetchAnd8ZeroExtend(const S &value, const T &mem, Register temp, Register output) {
        MOZ_CRASH("NYI");
    }
    template<typename T, typename S>
    void atomicFetchAnd16SignExtend(const S &value, const T &mem, Register temp, Register output) {
        MOZ_CRASH("NYI");
    }
    template<typename T, typename S>
    void atomicFetchAnd16ZeroExtend(const S &value, const T &mem, Register temp, Register output) {
        MOZ_CRASH("NYI");
    }
    template<typename T, typename S>
    void atomicFetchAnd32(const S &value, const T &mem, Register temp, Register output) {
        MOZ_CRASH("NYI");
    }
    template <typename T, typename S>
    void atomicAnd8(const T& value, const S& mem) {
        MOZ_CRASH("NYI");
    }
    template <typename T, typename S>
    void atomicAnd16(const T& value, const S& mem) {
        MOZ_CRASH("NYI");
    }
    template <typename T, typename S>
    void atomicAnd32(const T& value, const S& mem) {
        MOZ_CRASH("NYI");
    }

    template<typename T, typename S>
    void atomicFetchOr8SignExtend(const S &value, const T &mem, Register temp, Register output) {
        MOZ_CRASH("NYI");
    }
    template<typename T, typename S>
    void atomicFetchOr8ZeroExtend(const S &value, const T &mem, Register temp, Register output) {
        MOZ_CRASH("NYI");
    }
    template<typename T, typename S>
    void atomicFetchOr16SignExtend(const S &value, const T &mem, Register temp, Register output) {
        MOZ_CRASH("NYI");
    }
    template<typename T, typename S>
    void atomicFetchOr16ZeroExtend(const S &value, const T &mem, Register temp, Register output) {
        MOZ_CRASH("NYI");
    }
    template<typename T, typename S>
    void atomicFetchOr32(const S &value, const T &mem, Register temp, Register output) {
        MOZ_CRASH("NYI");
    }
    template <typename T, typename S>
    void atomicOr8(const T& value, const S& mem) {
        MOZ_CRASH("NYI");
    }
    template <typename T, typename S>
    void atomicOr16(const T& value, const S& mem) {
        MOZ_CRASH("NYI");
    }
    template <typename T, typename S>
    void atomicOr32(const T& value, const S& mem) {
        MOZ_CRASH("NYI");
    }

    template<typename T, typename S>
    void atomicFetchXor8SignExtend(const S &value, const T &mem, Register temp, Register output) {
        MOZ_CRASH("NYI");
    }
    template<typename T, typename S>
    void atomicFetchXor8ZeroExtend(const S &value, const T &mem, Register temp, Register output) {
        MOZ_CRASH("NYI");
    }
    template<typename T, typename S>
    void atomicFetchXor16SignExtend(const S &value, const T &mem, Register temp, Register output) {
        MOZ_CRASH("NYI");
    }
    template<typename T, typename S>
    void atomicFetchXor16ZeroExtend(const S &value, const T &mem, Register temp, Register output) {
        MOZ_CRASH("NYI");
    }
    template<typename T, typename S>
    void atomicFetchXor32(const S &value, const T &mem, Register temp, Register output) {
        MOZ_CRASH("NYI");
    }
    template <typename T, typename S>
    void atomicXor8(const T& value, const S& mem) {
        MOZ_CRASH("NYI");
    }
    template <typename T, typename S>
    void atomicXor16(const T& value, const S& mem) {
        MOZ_CRASH("NYI");
    }
    template <typename T, typename S>
    void atomicXor32(const T& value, const S& mem) {
        MOZ_CRASH("NYI");
    }

    void callWithExitFrame(Label *target);
    void callWithExitFrame(JitCode *target);
    void callWithExitFrame(JitCode *target, Register dynStack);

    void callJit(Register callee);
    void callJitFromAsmJS(Register callee);

    void add32(Register src, Register dest);
    void add32(Imm32 imm, Register dest);
    void add32(Imm32 imm, const Address &dest);
    void add64(Imm32 imm, Register64 dest);
#if(0)
    void sub32(Imm32 imm, Register dest);
    void sub32(Register src, Register dest);
#endif

    void incrementInt32Value(const Address &addr) {
        add32(Imm32(1), ToPayload(addr));
    }

	void branchAdd32(Condition cond, Register src, Register dest, Label *overflow) {
		ispew("branchAdd32(cond, reg, reg, l)");
		MOZ_ASSERT(cond == Overflow);
		
		// Save a load, ride a cowboy. Wait, what?
		addo(dest, src, dest); // No addio
		bc(Overflow, overflow); 
	}
    template <typename T>
    void branchAdd32(Condition cond, T src, Register dest, Label *overflow) {
    	ispew("branchAdd32(cond, T, reg, l)");
    	MOZ_ASSERT(cond == Overflow);
    	MOZ_ASSERT(dest != tempRegister);
    	
    	load32(src, tempRegister);
    	branchAdd32(cond, tempRegister, dest, overflow);
    }
    void branchSub32(Condition cond, Register src, Register dest, Label *l) {
    	ispew("branchSub32(cond, reg, reg, l)");
    	MOZ_ASSERT(cond == Overflow || cond == NonZero || cond == Zero);
    	
    	// Save a load, ride a ... never mind.
    	if (cond == Overflow)
    		subfo(dest, src, dest); // Operand order!!
    	else
    		subf_rc(dest, src, dest);
    	bc(cond, l);
    }
    template <typename T>
    void branchSub32(Condition cond, T src, Register dest, Label *l) {
    	ispew("branchSub32(cond, T, reg, l)");
    	MOZ_ASSERT(cond == Overflow || cond == NonZero || cond == Zero);
    	MOZ_ASSERT(dest != tempRegister);
    	
    	load32(src, tempRegister);
    	branchSub32(cond, tempRegister, dest, l);
    }

    void addPtr(Register src, Register dest);
    void subPtr(Register src, Register dest);
    void addPtr(const Address &src, Register dest);

    void move32(Imm32 imm, Register dest);
    void move32(Register src, Register dest);
    void move64(Register64 src, Register64 dest) {
    	ispew("move64(r64, r64)");
    	x_mr(dest.low, src.low);
    	x_mr(dest.high, src.high);
    }

    void movePtr(Register src, Register dest);
    void movePtr(ImmWord imm, Register dest);
    void movePtr(ImmPtr imm, Register dest);
    void movePtr(wasm::SymbolicAddress imm, Register dest);
    void movePtr(ImmGCPtr imm, Register dest);
#if(0)
    void movePtr(ImmMaybeNurseryPtr imm, Register dest);
#endif

    void load8SignExtend(const Address &address, Register dest);
    void load8SignExtend(const BaseIndex &src, Register dest);

    void load8ZeroExtend(const Address &address, Register dest);
    void load8ZeroExtend(const BaseIndex &src, Register dest);

    void load16SignExtend(const Address &address, Register dest);
    void load16SignExtend(const BaseIndex &src, Register dest);

    void load16ZeroExtend(const Address &address, Register dest);
    void load16ZeroExtend(const BaseIndex &src, Register dest);

	void load32(const Imm32 &imm, const Register &dest) {
		x_li32(dest, imm.value);
	}
    void load32(const Address &address, Register dest);
    void load32(const BaseIndex &address, Register dest);
    void load32(AbsoluteAddress address, Register dest);
    void load32(wasm::SymbolicAddress address, Register dest) { MOZ_CRASH(); }
    void load64(const Address& address, Register64 dest) {
        ispew("load64(adr, r64)");
        load32(Address(address.base, address.offset + LOW_32_OFFSET), dest.low);
        load32(Address(address.base, address.offset + HIGH_32_OFFSET), dest.high);
    }
    void branch64(Condition cond, const Address& lhs, Imm64 val, Label* label) {
    	// XXX: G5 should kick ass here
        MOZ_ASSERT(cond == Assembler::NotEqual,
                   "other condition codes not supported");

        branch32(cond, lhs, val.firstHalf(), label);
        branch32(cond, Address(lhs.base, lhs.offset + sizeof(uint32_t)), val.secondHalf(), label);
    }

    void branch64(Condition cond, const Address& lhs, const Address& rhs, Register scratch,
                  Label* label)
    {
    	// XXX: ditto
        MOZ_ASSERT(cond == Assembler::NotEqual,
                   "other condition codes not supported");
        MOZ_ASSERT(lhs.base != scratch);
        MOZ_ASSERT(rhs.base != scratch);

        load32(rhs, scratch);
        branch32(cond, lhs, scratch, label);

        load32(Address(rhs.base, rhs.offset + sizeof(uint32_t)), scratch);
        branch32(cond, Address(lhs.base, lhs.offset + sizeof(uint32_t)), scratch, label);
    }

    void loadPtr(const Address &address, Register dest);
    void loadPtr(const BaseIndex &src, Register dest);
    void loadPtr(AbsoluteAddress address, Register dest);
    void loadPtr(wasm::SymbolicAddress address, Register dest);

    void loadPrivate(const Address &address, Register dest);

    void loadInt32x1(const Address& addr, FloatRegister dest) { MOZ_CRASH("NYI"); }
    void loadInt32x1(const BaseIndex& addr, FloatRegister dest) { MOZ_CRASH("NYI"); }
    void loadInt32x2(const Address& addr, FloatRegister dest) { MOZ_CRASH("NYI"); }
    void loadInt32x2(const BaseIndex& addr, FloatRegister dest) { MOZ_CRASH("NYI"); }
    void loadInt32x3(const Address& src, FloatRegister dest) { MOZ_CRASH("NYI"); }
    void loadInt32x3(const BaseIndex& src, FloatRegister dest) { MOZ_CRASH("NYI"); }
    void storeInt32x1(FloatRegister src, const Address& dest) { MOZ_CRASH("NYI"); }
    void storeInt32x1(FloatRegister src, const BaseIndex& dest) { MOZ_CRASH("NYI"); }
    void storeInt32x2(FloatRegister src, const Address& dest) { MOZ_CRASH("NYI"); }
    void storeInt32x2(FloatRegister src, const BaseIndex& dest) { MOZ_CRASH("NYI"); }
    void storeInt32x3(FloatRegister src, const Address& dest) { MOZ_CRASH("NYI"); }
    void storeInt32x3(FloatRegister src, const BaseIndex& dest) { MOZ_CRASH("NYI"); }
    void loadAlignedInt32x4(const Address& addr, FloatRegister dest) { MOZ_CRASH("NYI"); }
    void storeAlignedInt32x4(FloatRegister src, Address addr) { MOZ_CRASH("NYI"); }
    void loadUnalignedInt32x4(const Address& addr, FloatRegister dest) { MOZ_CRASH("NYI"); }
    void loadUnalignedInt32x4(const BaseIndex& addr, FloatRegister dest) { MOZ_CRASH("NYI"); }
    void storeUnalignedInt32x4(FloatRegister src, Address addr) { MOZ_CRASH("NYI"); }
    void storeUnalignedInt32x4(FloatRegister src, BaseIndex addr) { MOZ_CRASH("NYI"); }

    void loadFloat32x3(const Address& src, FloatRegister dest) { MOZ_CRASH("NYI"); }
    void loadFloat32x3(const BaseIndex& src, FloatRegister dest) { MOZ_CRASH("NYI"); }
    void storeFloat32x3(FloatRegister src, const Address& dest) { MOZ_CRASH("NYI"); }
    void storeFloat32x3(FloatRegister src, const BaseIndex& dest) { MOZ_CRASH("NYI"); }
    void loadAlignedFloat32x4(const Address& addr, FloatRegister dest) { MOZ_CRASH("NYI"); }
    void storeAlignedFloat32x4(FloatRegister src, Address addr) { MOZ_CRASH("NYI"); }
    void loadUnalignedFloat32x4(const Address& addr, FloatRegister dest) { MOZ_CRASH("NYI"); }
    void loadUnalignedFloat32x4(const BaseIndex& addr, FloatRegister dest) { MOZ_CRASH("NYI"); }
    void storeUnalignedFloat32x4(FloatRegister src, Address addr) { MOZ_CRASH("NYI"); }
    void storeUnalignedFloat32x4(FloatRegister src, BaseIndex addr) { MOZ_CRASH("NYI"); }

    void loadDouble(const Address &addr, FloatRegister dest);
    void loadDouble(const BaseIndex &src, FloatRegister dest);

    // Load a float value into a register, then expand it to a double.
    void loadFloatAsDouble(const Address &addr, FloatRegister dest);
    void loadFloatAsDouble(const BaseIndex &src, FloatRegister dest);

    void loadFloat32(const Address &addr, FloatRegister dest);
    void loadFloat32(const BaseIndex &src, FloatRegister dest);

    void store8(Register src, const Address &address);
    void store8(Imm32 imm, const Address &address);
    void store8(Register src, const BaseIndex &address);
    void store8(Imm32 imm, const BaseIndex &address);

    void store16(Register src, const Address &address);
    void store16(Imm32 imm, const Address &address);
    void store16(Register src, const BaseIndex &address);
    void store16(Imm32 imm, const BaseIndex &address);
    void store16Swapped(Register src, const Address &address);
    void store16Swapped(Imm32 imm, const Address &address);
    void store16Swapped(Register src, const BaseIndex &address);
    void store16Swapped(Imm32 imm, const BaseIndex &address);

    void store32(Register src, AbsoluteAddress address);
    void store32(Register src, const Address &address);
    void store32(Register src, const BaseIndex &address);
    void store32(Imm32 src, const Address &address);
    void store32(Imm32 src, const BaseIndex &address);
    void store32ByteSwapped(Register src, AbsoluteAddress address);
    void store32ByteSwapped(Register src, const Address &address);
    void store32ByteSwapped(Register src, const BaseIndex &address);
    void store32ByteSwapped(Imm32 src, const Address &address);
    void store32ByteSwapped(Imm32 src, const BaseIndex &address);

	// XXX?
    // NOTE: This will use second scratch on MIPS. Only ARM needs the
    // implementation without second scratch.
    void store32_NoSecondScratch(Imm32 src, const Address &address) {
        store32(src, address);
    }

    void store64(Register64 src, Address address) {
    	ispew("store64(r64, adr)");
    	MOZ_ASSERT(LOW_32_OFFSET == 4);
        store32(src.low, Address(address.base, address.offset + LOW_32_OFFSET));
        store32(src.high, Address(address.base, address.offset + HIGH_32_OFFSET));
    }

    template <typename T> void storePtr(ImmWord imm, T address);
    template <typename T> void storePtr(ImmPtr imm, T address);
    template <typename T> void storePtr(ImmGCPtr imm, T address);
    void storePtr(Register src, const Address &address);
    void storePtr(Register src, const BaseIndex &address);
    void storePtr(Register src, AbsoluteAddress dest);
    void storeDouble(FloatRegister src, Address addr) {
    	ispew("storeDouble(fpr, adr)");
        ma_sd(src, addr);
    }
    void storeDouble(FloatRegister src, BaseIndex addr) {
    	ispew("storeDouble(fpr, bi)");
        MOZ_ASSERT(addr.offset == 0);
        ma_sd(src, addr);
    }
    void moveDouble(FloatRegister src, FloatRegister dest) {
    	ispew("moveDouble(fpr, fpr)");
    	fmr(dest, src);
    }

    void storeFloat32(FloatRegister src, Address addr) {
    	ispew("storeFloat32(fpr, adr)");
        ma_ss(src, addr);
    }
    void storeFloat32(FloatRegister src, BaseIndex addr) {
    	ispew("storeFloat32(fpr, bi)");
        ma_ss(src, addr);
    }

    void zeroDouble(FloatRegister reg) {
    	ispew("[[ zeroDouble(fpr)");
    	// Load 32 zeroes into an FPR, then go double precision.
    	
    	x_li(tempRegister, 0);
    	stwu(tempRegister, stackPointerRegister, -4); // cracked
#ifdef _PPC970_
		// We just need two nops so that even if the (cracked) stwu
                // leaks into the lfd dispatch group, it's still separated.
		x_nop();
		x_nop();
#endif
		lfs(reg, stackPointerRegister, 0);
		addi(stackPointerRegister, stackPointerRegister, 4);
		ispew("   zeroDouble(fpr) ]]");
    }

    void clampIntToUint8(Register reg);

    void subPtr(Imm32 imm, const Register dest);
    void subPtr(const Address &addr, const Register dest);
    void subPtr(Register src, const Address &dest);
    void addPtr(Imm32 imm, const Register dest);
    void addPtr(Imm32 imm, const Address &dest);
    void addPtr(ImmWord imm, const Register dest) {
        addPtr(Imm32(imm.value), dest);
    }
    void addPtr(ImmPtr imm, const Register dest) {
        addPtr(ImmWord(uintptr_t(imm.value)), dest);
    }
    void mulBy3(const Register &src, const Register &dest) {
    	ispew("mulBy3(reg, reg)");
    	// Look familiar?!
        add(dest, src, src);
        add(dest, dest, src);
    }

    void breakpoint();

    void branchDouble(DoubleCondition cond, FloatRegister lhs, FloatRegister rhs,
                      Label *label);

    void branchFloat(DoubleCondition cond, FloatRegister lhs, FloatRegister rhs,
                     Label *label);

    void checkStackAlignment();

    // If source is a double, load it into dest. If source is int32,
    // convert it to double. Else, branch to failure.
    void ensureDouble(const ValueOperand &source, FloatRegister dest, Label *failure);

    template <typename T1, typename T2>
    void cmpPtrSet(Assembler::Condition cond, T1 lhs, T2 rhs, Register dest)
    {
    	ispew("cmpPtrSet(cond, T1, T2, reg)");
        ma_cmp_set(cond, lhs, rhs, dest);
    }

    template <typename T1, typename T2>
    void cmp32Set(Assembler::Condition cond, T1 lhs, T2 rhs, Register dest)
    {
    	ispew("cmp32Set(cond, T1, T2, reg)");
        ma_cmp_set(cond, lhs, rhs, dest);
    }

  protected:
    bool buildOOLFakeExitFrame(void *fakeReturnAddr);

  public:
  	// The VMWrapper needs to call these, or else they'd be private.
    void callWithABIPre(uint32_t *stackAdjust, bool callFromAsmJS = false);
    void callWithABIPost(uint32_t stackAdjust, MoveOp::Type result);
 
    // Emits a call to a C/C++ function, resolving all argument moves.
    // This works around the fact our call()s are not ABI compliant.
    void callWithABIOptimized(void *fun, MoveOp::Type result = MoveOp::GENERAL);

    CodeOffset labelForPatch() {
        return CodeOffset(nextOffset().getOffset());
    }

    void memIntToValue(Address Source, Address Dest) {
    	ispew("[[ memIntToValue(adr, adr)");
        load32(Source, addressTempRegister);
        storeValue(JSVAL_TYPE_INT32, addressTempRegister, Dest);
        ispew("   memIntToValue(adr, adr) ]]");
    }

    void lea(Operand addr, Register dest) {
    	ispew("lea(o, reg)");
        ma_addu(dest, addr.baseReg(), Imm32(addr.disp()));
    }

    void abiret() {
    	ispew("abiret()");
    	blr();
    }

    void ma_storeImm(Imm32 imm, const Address &addr) {
        ma_sw(imm, addr);
    }

    BufferOffset ma_BoundsCheck(Register bounded) {
        BufferOffset bo = m_buffer.nextOffset();
        ma_li32Patchable(bounded, Imm32(0));
        return bo;
    }

    void moveFloat32(FloatRegister src, FloatRegister dest) {
    	ispew("moveFloat32(fpr, fpr)");
        fmr(dest, src);
    }

    void branchPtrInNurseryRange(Condition cond, Register ptr, Register temp, Label *label);
    void branchValueIsNurseryObject(Condition cond, ValueOperand value, Register temp,
                                    Label *label);

    void loadAsmJSActivation(Register dest) {
    	ispew("loadAsmJSActivation(reg)");
        loadPtr(Address(GlobalReg, wasm::ActivationGlobalDataOffset - AsmJSGlobalRegBias), dest);
    }
    void loadAsmJSHeapRegisterFromGlobalData() {
    	ispew("loadAsmJSHeapRegisterFromGlobalData()");
        MOZ_ASSERT(Imm16::IsInSignedRange(wasm::HeapGlobalDataOffset - AsmJSGlobalRegBias));
        loadPtr(Address(GlobalReg, wasm::HeapGlobalDataOffset - AsmJSGlobalRegBias), HeapReg);
    }

    // Instrumentation for entering and leaving the profiler.
    void profilerEnterFrame(Register framePtr, Register scratch);
    void profilerExitFrame();
};

typedef MacroAssemblerPPCCompat MacroAssemblerSpecific;

} // namespace jit
} // namespace js

#endif /* jit_ppc_MacroAssembler_ppc_h */

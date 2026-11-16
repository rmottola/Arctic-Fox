/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_ppc_Assembler_ppc_h
#define jit_ppc_Assembler_ppc_h

/*

   IonPower (C)2015 Contributors to TenFourFox. All rights reserved.
 
   Authors: Cameron Kaiser <classilla@floodgap.com>
   with thanks to Ben Stuhl and David Kilbridge 
   and the authors of the ARM and MIPS ports
 
 */

#include "mozilla/ArrayUtils.h"
#include "mozilla/Attributes.h"
#include "mozilla/MathAlgorithms.h"

#include "jit/CompactBuffer.h"
#include "jit/IonCode.h"
#include "jit/JitCompartment.h"
#include "jit/JitSpewer.h"
#include "jit/osxppc/Architecture-ppc.h"
#include "jit/shared/Assembler-shared.h"
#include "jit/shared/IonAssemblerBuffer.h"

#if DEBUG
#define ispew(x)    JitSpew(JitSpew_Codegen, "== " x " ==")
#else
#define ispew(x)    ;
#endif

namespace js {
namespace jit {

static MOZ_CONSTEXPR_VAR Register r0 = { Registers::r0 };
static MOZ_CONSTEXPR_VAR Register r1 = { Registers::r1 };
static MOZ_CONSTEXPR_VAR Register sp = { Registers::r1 };
static MOZ_CONSTEXPR_VAR Register r2 = { Registers::r2 };
static MOZ_CONSTEXPR_VAR Register r3 = { Registers::r3 };
static MOZ_CONSTEXPR_VAR Register r4 = { Registers::r4 };
static MOZ_CONSTEXPR_VAR Register r5 = { Registers::r5 };
static MOZ_CONSTEXPR_VAR Register r6 = { Registers::r6 };
static MOZ_CONSTEXPR_VAR Register r7 = { Registers::r7 };
static MOZ_CONSTEXPR_VAR Register r8 = { Registers::r8 };
static MOZ_CONSTEXPR_VAR Register r9 = { Registers::r9 };
static MOZ_CONSTEXPR_VAR Register r10 = { Registers::r10 };
static MOZ_CONSTEXPR_VAR Register lr  = { Registers::lr }; /* LIE! */
static MOZ_CONSTEXPR_VAR Register r12 = { Registers::r12 };
static MOZ_CONSTEXPR_VAR Register r13 = { Registers::r13 };
static MOZ_CONSTEXPR_VAR Register r14 = { Registers::r14 };
static MOZ_CONSTEXPR_VAR Register r15 = { Registers::r15 };
static MOZ_CONSTEXPR_VAR Register r16 = { Registers::r16 };
static MOZ_CONSTEXPR_VAR Register r17 = { Registers::r17 };
static MOZ_CONSTEXPR_VAR Register r18 = { Registers::r18 };
static MOZ_CONSTEXPR_VAR Register r19 = { Registers::r19 };
static MOZ_CONSTEXPR_VAR Register r20 = { Registers::r20 };
static MOZ_CONSTEXPR_VAR Register r21 = { Registers::r21 };
static MOZ_CONSTEXPR_VAR Register r22 = { Registers::r22 };
static MOZ_CONSTEXPR_VAR Register r23 = { Registers::r23 };
static MOZ_CONSTEXPR_VAR Register r24 = { Registers::r24 };
static MOZ_CONSTEXPR_VAR Register r25 = { Registers::r25 };
static MOZ_CONSTEXPR_VAR Register r26 = { Registers::r26 };
static MOZ_CONSTEXPR_VAR Register r27 = { Registers::r27 };
static MOZ_CONSTEXPR_VAR Register r28 = { Registers::r28 };
static MOZ_CONSTEXPR_VAR Register r29 = { Registers::r29 };
static MOZ_CONSTEXPR_VAR Register r30 = { Registers::r30 };
static MOZ_CONSTEXPR_VAR Register r31 = { Registers::r31 };

static MOZ_CONSTEXPR_VAR FloatRegister f0 = { FloatRegisters::f0 };
static MOZ_CONSTEXPR_VAR FloatRegister f1 = { FloatRegisters::f1 };
static MOZ_CONSTEXPR_VAR FloatRegister f2 = { FloatRegisters::f2 };
static MOZ_CONSTEXPR_VAR FloatRegister f3 = { FloatRegisters::f3 };
// The rest of the FPRs are the business of the allocator, not the assembler.
// SPRs and CRs are defined in their respective enums (see Architecture-ppc.h).

// Old JM holdovers for convenience.
static MOZ_CONSTEXPR_VAR Register stackPointerRegister = r1;
static MOZ_CONSTEXPR_VAR Register tempRegister = r0;
static MOZ_CONSTEXPR_VAR Register addressTempRegister = r12;
// The OS X ABI documentation recommends r2 for this, not r11 like we used to.
static MOZ_CONSTEXPR_VAR Register emergencyTempRegister = r2;
static MOZ_CONSTEXPR_VAR FloatRegister fpTempRegister = f0;
static MOZ_CONSTEXPR_VAR FloatRegister fpConversionRegister = f2;

// Use the same assignments as PPCBC for simplicity.
static MOZ_CONSTEXPR_VAR Register OsrFrameReg = r6;
static MOZ_CONSTEXPR_VAR Register ArgumentsRectifierReg = r19;
static MOZ_CONSTEXPR_VAR Register CallTempReg0 = r8;
static MOZ_CONSTEXPR_VAR Register CallTempReg1 = r9;
static MOZ_CONSTEXPR_VAR Register CallTempReg2 = r10;
static MOZ_CONSTEXPR_VAR Register CallTempReg3 = r7;
static MOZ_CONSTEXPR_VAR Register CallTempReg4 = r5; // Bad things! Try not to use these!
static MOZ_CONSTEXPR_VAR Register CallTempReg5 = r6;

// irregexp
static MOZ_CONSTEXPR_VAR Register IntArgReg0 = r3;
static MOZ_CONSTEXPR_VAR Register IntArgReg1 = r4;
static MOZ_CONSTEXPR_VAR Register IntArgReg2 = r5;
static MOZ_CONSTEXPR_VAR Register IntArgReg3 = r6;

static MOZ_CONSTEXPR_VAR Register GlobalReg = r20; // used by AsmJS. Allocatable, but non-volatile.
static MOZ_CONSTEXPR_VAR Register HeapReg = r21; // Ditto.

// These are defined, but not actually used, at least by us (see GetTempRegForIntArg).
static MOZ_CONSTEXPR_VAR Register CallTempNonArgRegs[] = { r10, r9, r8, r7 };
static const uint32_t NumCallTempNonArgRegs = mozilla::ArrayLength(CallTempNonArgRegs);

class ABIArgGenerator
{
	uint32_t stackOffset_;
	uint32_t usedGPRs_;
	uint32_t usedFPRs_;
    ABIArg current_;

  public:
    ABIArgGenerator();
    ABIArg next(MIRType argType);
    ABIArg &current() { return current_; }

    uint32_t stackBytesConsumedSoFar() const { return stackOffset_; }

    static const Register NonArgReturnReg0;
    static const Register NonArgReturnReg1;
    static const Register NonArg_VolatileReg;
    static const Register NonReturn_VolatileReg0;
    static const Register NonReturn_VolatileReg1;
};

static MOZ_CONSTEXPR_VAR Register PreBarrierReg = r4;

static MOZ_CONSTEXPR_VAR Register InvalidReg = { Registers::invalid_reg };
static MOZ_CONSTEXPR_VAR FloatRegister InvalidFloatReg;

static MOZ_CONSTEXPR_VAR Register StackPointer = sp;
static MOZ_CONSTEXPR_VAR Register FramePointer = InvalidReg;

// All return registers must be allocatable.
static MOZ_CONSTEXPR_VAR Register JSReturnReg_Type = r6;
static MOZ_CONSTEXPR_VAR Register JSReturnReg_Data = r5;
static MOZ_CONSTEXPR_VAR Register ReturnReg = r3;
static MOZ_CONSTEXPR_VAR FloatRegister ReturnFloat32Reg = f1;
static MOZ_CONSTEXPR_VAR FloatRegister ReturnDoubleReg = f1;

// Gawd, Mozilla. Must FPRs be vector registers in all your damn architectures?
static MOZ_CONSTEXPR_VAR FloatRegister ReturnSimdReg = InvalidFloatReg;
static MOZ_CONSTEXPR_VAR FloatRegister ReturnSimd128Reg = InvalidFloatReg;
static MOZ_CONSTEXPR_VAR FloatRegister ReturnInt32x4Reg = InvalidFloatReg;
static MOZ_CONSTEXPR_VAR FloatRegister ReturnFloat32x4Reg = InvalidFloatReg;
static MOZ_CONSTEXPR_VAR FloatRegister ScratchSimdReg = InvalidFloatReg;
static MOZ_CONSTEXPR_VAR FloatRegister ScratchSimd128Reg = InvalidFloatReg;

static MOZ_CONSTEXPR_VAR FloatRegister ScratchFloat32Reg = f0;
static MOZ_CONSTEXPR_VAR FloatRegister ScratchDoubleReg = f0;
static MOZ_CONSTEXPR_VAR FloatRegister SecondScratchFloat32Reg = f2;
static MOZ_CONSTEXPR_VAR FloatRegister SecondScratchDoubleReg = f2;

// A bias applied to the GlobalReg to allow the use of instructions with small
// negative immediate offsets which doubles the range of global data that can be
// accessed with a single instruction. (XXX)
static const int32_t AsmJSGlobalRegBias = 32768;

// Registers used in the GenerateFFIIonExit Enable Activation block. (Mirror MIPS.)
static MOZ_CONSTEXPR_VAR Register AsmJSIonExitRegCallee = r7;
static MOZ_CONSTEXPR_VAR Register AsmJSIonExitRegE0 = r3;
static MOZ_CONSTEXPR_VAR Register AsmJSIonExitRegE1 = r4;
static MOZ_CONSTEXPR_VAR Register AsmJSIonExitRegE2 = r5;
static MOZ_CONSTEXPR_VAR Register AsmJSIonExitRegE3 = r6;

// Registers used in the GenerateFFIIonExit Disable Activation block.
static MOZ_CONSTEXPR_VAR Register AsmJSIonExitRegReturnData = JSReturnReg_Data;
static MOZ_CONSTEXPR_VAR Register AsmJSIonExitRegReturnType = JSReturnReg_Type;
static MOZ_CONSTEXPR_VAR Register AsmJSIonExitRegD0 = r3;
static MOZ_CONSTEXPR_VAR Register AsmJSIonExitRegD1 = r4;
static MOZ_CONSTEXPR_VAR Register AsmJSIonExitRegD2 = r7;

// Try to favour word alignment, though we consider the ABI stack-smashed in Ion code.
static const uint32_t ABIStackAlignment = 4;
static const uint32_t CodeAlignment = 4;
// As of Mozilla 40, the JIT requires Value alignment at minimum, which is stupid and unnecessary.
static const uint32_t JitStackAlignment = 8;
static const uint32_t JitStackValueAlignment = 1;

// Future.
static const bool SupportsSimd = false;
static const uint32_t SimdStackAlignment = 16;
static const uint32_t SimdMemoryAlignment = 16; // damn AltiVec restrictions
static const uint32_t AsmJSStackAlignment = 16; // wtf wtf wtf

static const Scale ScalePointer = TimesFour;

enum PPCOpcodes {
	// Some we don't use yet (but we will).
    PPC_add     = 0x7C000214, // add
    PPC_addc    = 0x7C000014, // add carrying
    PPC_adde    = 0x7C000114, // add extended
    PPC_addo    = 0x7C000614, // add & OE=1 (can set OV)
    PPC_addi    = 0x38000000, // add immediate
    PPC_addic   = 0x30000000, // add immediate carrying
    PPC_addis   = 0x3C000000, // add immediate shifted
    PPC_addme   = 0x7C0001D4, // add -1 extended
    PPC_addze   = 0x7C000194, // add zero extended
    PPC_and_    = 0x7C000038, // and
    PPC_andc    = 0x7C000078, // and with compliment
    PPC_andi    = 0x70000000, // and immediate
    PPC_andis   = 0x74000000, // and immediate shifted
    PPC_b       = 0x48000000, // branch
    PPC_bc      = 0x40000000, // branch conditional
    PPC_bctr    = 0x4E800420, // branch to CTR (+/- LR)
    PPC_bcctr   = 0x4C000420, // branch conditional to count register
    PPC_blr     = 0x4E800020, // branch to link register
    PPC_cmpw    = 0x7C000000, // compare
    PPC_cmpwi   = 0x2C000000, // compare immediate
    PPC_cmplw   = 0x7C000040, // compare logical
    PPC_cmplwi  = 0x28000000, // compare logical immediate
    PPC_cntlzw  = 0x7C000034, // count leading zeroes
    PPC_crand   = 0x4C000202, // condition register and
    PPC_crandc  = 0x4C000102, // condition register and-with-complement
    PPC_cror    = 0x4C000382, // condition register or
    PPC_crorc   = 0x4C000342, // condition register or-with-complement
    PPC_crxor   = 0x4C000182, // condition register xor
    PPC_divw    = 0x7C0003D6, // integer divide
    PPC_divwo   = 0x7C0007D6, // integer divide & OE=1 (can set OV)
    PPC_divwu   = 0x7C000396, // integer divide unsigned
    PPC_divwuo  = 0x7C000796, // integer divide unsigned & OE=1 (can set OV)
    PPC_eqv     = 0x7C000238, // equivalence operator
    PPC_extsb   = 0x7C000774, // extend sign byte
    PPC_extsh   = 0x7C000734, // extend sign halfword
    PPC_extsw   = 0x7C0007B4, // extend sign word
    PPC_fabs    = 0xFC000210, // floating absolute value (double precision)
    PPC_fadd    = 0xFC00002A, // floating add (double precision)
    PPC_fadds   = 0xEC00002A, // floating add (single precision)
    PPC_fcfid   = 0xFC00069C, // floating convert from integer doubleword
    PPC_fctiw   = 0xFC00001C, // floating convert to integer (to -Inf)
    PPC_fctiwz  = 0xFC00001E, // floating convert to integer (to zero)
    PPC_fcmpu   = 0xFC000000, // floating compare unordered
    PPC_fdiv    = 0xFC000024, // floating divide (double precision)
    PPC_fdivs   = 0xEC000024, // floating divide (single precision)
    PPC_fmr     = 0xFC000090, // floating move register
    PPC_fmul    = 0xFC000032, // floating multiply (double precision)
    PPC_fmuls   = 0xEC000032, // floating multiply (single precision)
    PPC_fneg    = 0xFC000050, // floating negate
    PPC_frsp    = 0xFC000018, // convert to single precision
    PPC_fsel    = 0xFC00002E, // floating point select
    PPC_fsub    = 0xFC000028, // floating subtract (double precision)
    PPC_fsubs   = 0xEC000028, // floating subtract (single precision)
    PPC_fsqrt   = 0xFC00002C, // floating square root (G5 only) (double)
    PPC_frsqrte = 0xFC000034, // floating reciprocal square root estimate
    PPC_fnmsub  = 0xFC00003C, // floating fused negative multiply-subtract
    PPC_fmadd   = 0xFC00003A, // floating fused multiply-add
    PPC_lbz     = 0x88000000, // load byte and zero
    PPC_lbzx    = 0x7C0000AE, // load byte and zero indexed
    PPC_ld      = 0xE8000000, // load doubleword
    PPC_ldx     = 0x7C00002A, // load doubleword indexed
    PPC_lfd     = 0xC8000000, // load floating point double
    PPC_lfdx    = 0x7C0004AE, // load floating-point double indexed
    PPC_lfs     = 0xC0000000, // load single precision float
    PPC_lfsx    = 0x7C00042E, // load single precision float indexed
    PPC_lha     = 0xA8000000, // load halfword algebraic
    PPC_lhax    = 0x7C0002AE, // load halfword algebraic indexed
    PPC_lhz     = 0xA0000000, // load halfword and zero
    PPC_lhzx    = 0x7C00022E, // load halfword and zero indexed
    PPC_lhbrx   = 0x7C00062C, // load hw and zero indexed (byte swapped)
    PPC_lwz     = 0x80000000, // load word and zero
    PPC_lwzx    = 0x7C00002E, // load word and zero indexed
    PPC_lwbrx   = 0x7C00042C, // load word and zero indexed (byte swapped)
    PPC_mcrxr   = 0x7C000400, // move XER[0-3] to CR[0-3] (G4 and earlier)
    PPC_mcrf    = 0x4C000000, // move CR[0-3] to CR[0-3]
    PPC_mcrfs   = 0xFC000080, // move FPSCR fields to CR
    PPC_mfcr    = 0x7C000026, // move from condition register
    PPC_mffs    = 0xFC00048E, // move from fpscr to fpr
    PPC_mfspr   = 0x7C0002A6, // move from spr (special purpose register)
    PPC_mtcrf   = 0x7C000120, // move to condition register field
    PPC_mtfsb0  = 0xFC00008C, // move zero bit into FPSCR
    PPC_mtfsb1  = 0xFC00004C, // move one bit into FPSCR
    PPC_mtfsfi  = 0xFC00010C, // move 4-bit immediate into FPSCR field
    PPC_mtspr   = 0x7C0003A6, // move to spr
    PPC_mulhw   = 0x7C000096, // multiply high signed
    PPC_mulhwu  = 0x7C000016, // multiply high unsigned
    PPC_mulli   = 0x1C000000, // multiply low immediate
    PPC_mullw   = 0x7C0001D6, // multiply low word
    PPC_mullwo  = 0x7C0005D6, // multiply low word with overflow
    PPC_nand    = 0x7C0003B8, // nand
    PPC_neg     = 0x7C0000D0, // negate
    PPC_nor     = 0x7C0000F8, // nor
    PPC_or_     = 0x7C000378, // or
    PPC_ori     = 0x60000000, // or immediate
    PPC_oris    = 0x64000000, // or immediate shifted
    PPC_rlwimi  = 0x50000000, // rotate left word imm then mask insert
    PPC_rlwinm  = 0x54000000, // rotate left word then and with mask
    PPC_rldicl  = 0x78000000, // rotate left doubleword immediate then clear left
    PPC_rldicr  = 0x78000004, // rotate left doubleword immediate then clear right
    PPC_rldimi  = 0x7800000C, // rotate left doubleword immediate then mask insert
    PPC_sld     = 0x7C000036, // shift left doubleword
    PPC_slw     = 0x7C000030, // shift left word
    PPC_srad    = 0x7C000634, // shift right algebraic doubleword (sign ext)
    PPC_sradi   = 0x7C000674, // shift right algebraic doubleword immediate
    PPC_sraw    = 0x7C000630, // shift right algebraic word (sign ext)
    PPC_srawi   = 0x7C000670, // shift right algebraic word immediate
    PPC_srd     = 0x7C000436, // shift right doubleword (zero ext)
    PPC_srw     = 0x7C000430, // shift right word (zero ext)
    PPC_stb     = 0x98000000, // store byte
    PPC_stbx    = 0x7C0001AE, // store byte indexed
    PPC_std     = 0xF8000000, // store doubleword
    PPC_stdu    = 0xF8000001, // store doubleword with update
    PPC_stdux   = 0x7C00016A, // store doubleword with update indexed
    PPC_stdx    = 0x7C00012A, // store doubleword indexed
    PPC_stfd    = 0xD8000000, // store floating-point double
    PPC_stfdu   = 0xDC000000, // store floating-point double with update
    PPC_stfdx   = 0x7C0005AE, // store floating-point double indexed
    PPC_stfs    = 0xD0000000, // store floating-point single
    PPC_stfsx   = 0x7C00052E, // store floating-point single indexed
    PPC_sth     = 0xB0000000, // store halfword
    PPC_sthx    = 0x7C00032E, // store halfword indexed
    PPC_sthbrx  = 0x7C00072C, // store halfword indexed (byte swapped)
    PPC_stw     = 0x90000000, // store word
    PPC_stwu    = 0x94000000, // store word with update
    PPC_stwux   = 0x7C00016E, // store word with update indexed
    PPC_stwx    = 0x7C00012E, // store word indexed
    PPC_stwbrx  = 0x7C00052C, // store word indexed (byte swapped)
    PPC_subf    = 0x7C000050, // subtract from
    PPC_subfc   = 0x7C000010, // subtract from with carry
    PPC_subfe   = 0x7C000110, // subtract from extended
    PPC_subfze  = 0x7C000190, // subtract from zero extended
    PPC_subfo   = 0x7C000450, // subtract from with overflow
#ifdef __APPLE__
    PPC_trap    = 0x7FE00008, // trap word (extended from tw 31,r0,r0)
#else
#error Specify the trap word for your PPC operating system
#endif
    PPC_xor_    = 0x7C000278, // xor
    PPC_xori    = 0x68000000, // xor immediate
    PPC_xoris   = 0x6C000000, // xor immediate shifted

    // simplified mnemonics
    PPC_mr = PPC_or_,
    PPC_not = PPC_nor,
    PPC_nop = PPC_ori,
        
    PPC_MAJOR_OPCODE_MASK = 0xFC000000 // AND with this to get some idea of the opcode
};

class Instruction;
class InstImm;
class MacroAssemblerPPC;
class Operand;

// A BOffImm16 is a 16 bit (signed or unsigned) immediate that is used for branches.
class BOffImm16
{
    int32_t data;

  public:
    uint32_t encode() {
        MOZ_ASSERT(!isInvalid());
        return (uint32_t)data & 0xFFFC;
    }
    int32_t decode() {
        MOZ_ASSERT(!isInvalid());
        return data;
    }

    explicit BOffImm16(int offset)
    {
        MOZ_ASSERT((offset & 0x3) == 0);
        MOZ_ASSERT(IsInRange(offset) || IsInSignedRange(offset));
        data = offset;
    }
    static bool IsInRange(int offset) {
    	if (offset > 65535)
    		return false;
    	if (offset < 0)
    		return false;
    	return true;
    }
    static bool IsInSignedRange(int offset) {
        if (offset > 32767)
            return false;
        if (offset < -32767)
            return false;
        return true;
    }
    static const int32_t INVALID = 0x00020000;
    BOffImm16()
      : data(INVALID)
    { }

    bool isInvalid() {
        return data == INVALID;
    }
    Instruction *getDest(Instruction *src);

    BOffImm16(InstImm inst);
};

// A JOffImm26 is a 26 bit signed immediate that is used for unconditional jumps.
class JOffImm26
{
    int32_t data;

  public:
    uint32_t encode() {
        MOZ_ASSERT(!isInvalid());
        return (uint32_t)data & 0x03FFFFFC;
    }
    int32_t decode() {
        MOZ_ASSERT(!isInvalid());
        return data;
    }

    explicit JOffImm26(int offset)
    {
        MOZ_ASSERT((offset & 0x3) == 0);
        MOZ_ASSERT(IsInRange(offset));
        data = offset;
    }
    static bool IsInRange(int offset) {
        if (offset < -33554431)
            return false;
        if (offset > 33554431)
            return false;
        return true;
    }
    static const int32_t INVALID = 0x20000000;
    JOffImm26()
      : data(INVALID)
    { }

    bool isInvalid() {
        return data == INVALID;
    }
    Instruction *getDest(Instruction *src);

};

class Imm16
{
    int32_t value;

  public:
    Imm16();
    Imm16(uint32_t imm)
      : value(imm)
    {
    }
    uint32_t encode() {
        return (uint32_t)value & 0xffff;
    }
    int32_t decodeSigned() {
        return value;
    }
    uint32_t decodeUnsigned() {
        return value;
    }
    static bool IsInSignedRange(int32_t imm) {
        return imm >= INT16_MIN  && imm <= INT16_MAX;
    }
    static bool IsInUnsignedRange(uint32_t imm) {
        return imm <= UINT16_MAX ;
    }
    static Imm16 Lower (Imm32 imm) {
        return Imm16(imm.value & 0xffff);
    }
    static Imm16 Upper (Imm32 imm) {
        return Imm16((imm.value >> 16) & 0xffff);
    }
};

class Operand
{
  public:
    enum Tag {
        REG,
        FREG,
        MEM
    };

  private:
    Tag tag : 3;
    uint32_t reg : 5; // XXX. This really should be Register::Code, but then float regs ...
    int32_t offset;

  public:
    Operand (Register reg_)
      : tag(REG), reg(reg_.code())
    { }

    Operand (FloatRegister freg)
      : tag(FREG), reg(freg.code())
    { }

    Operand (Register base, Imm32 off)
      : tag(MEM), reg(base.code()), offset(off.value)
    { }

    Operand (Register base, int32_t off)
      : tag(MEM), reg(base.code()), offset(off)
    { }

    Operand (const Address &addr)
      : tag(MEM), reg(addr.base.code()), offset(addr.offset)
    { }

    Tag getTag() const {
        return tag;
    }

    Register toReg() const {
        MOZ_ASSERT(tag == REG);
        return Register::FromCode((Register::Code)reg);
    }

    FloatRegister toFReg() const {
        MOZ_ASSERT(tag == FREG);
        return FloatRegister::FromCode((FloatRegister::Code)reg);
    }

    void toAddr(Register *r, Imm32 *dest) const {
        MOZ_ASSERT(tag == MEM);
        *r = Register::FromCode((Register::Code)reg);
        *dest = Imm32(offset);
    }
    Address toAddress() const {
        MOZ_ASSERT(tag == MEM);
        return Address(Register::FromCode((Register::Code)reg), offset);
    }
    int32_t disp() const {
        MOZ_ASSERT(tag == MEM);
        return offset;
    }

    int32_t base() const {
        MOZ_ASSERT(tag == MEM);
        return reg;
    }
    Register baseReg() const {
        MOZ_ASSERT(tag == MEM);
        return Register::FromCode((Register::Code)reg);
    }
};

inline Imm32
Imm64::firstHalf() const
{
    return hi(); // ENDIAN!
}

inline Imm32
Imm64::secondHalf() const
{
    return low(); // ENDIAN!
}

void
PatchJump(CodeLocationJump &jump_, CodeLocationLabel label,
          ReprotectCode reprotect = DontReprotect);

void
PatchBackedge(CodeLocationJump &jump_, CodeLocationLabel label, JitRuntime::BackedgeTarget target);

class Assembler;
typedef js::jit::AssemblerBuffer<1024, Instruction> PPCBuffer;

class PPCBufferWithExecutableCopy : public PPCBuffer
{
  public:
    void executableCopy(uint8_t* buffer) {
        if (this->oom())
            return;

        for (Slice* cur = head; cur != nullptr; cur = cur->getNext()) {
            memcpy(buffer, &cur->instructions, cur->length());
            buffer += cur->length();
        }
    }
};

class Assembler : public AssemblerShared
{
  public:
    enum BranchBits {
        BranchOnClear = 0x04,
        BranchOnSet = 0x0c,
        BranchOptionMask = 0x0f,
        BranchOptionInvert = 0x08 // XOR with this to invert the sense of a Condition
    };

    enum Condition {
        // Bit flag for unsigned comparisons (remember that you have to
        // choose the type of comparison at the compare step, not the
        // branch). We just mask this bit off, but the MacroAsssembler
        // may use it as a flag. This is a synthetic code.
        ConditionUnsigned   = 0x100,        // Computation only
        ConditionUnsignedHandled = 0x2ff,	// Mask off bit 8 but not 9 or 0-7

        // XER-only codes. We need to have XER in the CR using mcrxr or
        // an equivalent first, but we don't need to check CR itself.
        // This is a synthetic code.
        ConditionOnlyXER    = 0x200,        // Computation only
        ConditionXERCA      = 0x22c,        // same as EQ bit
        ConditionXERNCA     = 0x224,
        ConditionXEROV      = 0x21c,        // same as GT bit

    	// These are off pp370-1 in OPPCC. The top nybble is the offset
    	// to the CR field (the x in BIF*4+x), and the bottom is the BO.
    	// Synthetic condition flags sit in the MSB.
        Equal = 0x2c,
        NotEqual = 0x24,
        GreaterThan = 0x1c,
        GreaterThanOrEqual = 0x04, 
        LessThan = 0x0c,
        LessThanOrEqual = 0x14,
        Above = GreaterThan | ConditionUnsigned,
        AboveOrEqual = GreaterThanOrEqual | ConditionUnsigned,
        Below = LessThan | ConditionUnsigned,
        BelowOrEqual = LessThanOrEqual | ConditionUnsigned,
        Overflow = ConditionXEROV,
        Signed = LessThan,
        NotSigned = GreaterThan,
        Zero = Equal,
        NonZero = NotEqual,
        Always = 0x1f,
        
        // This is specific to the SO bits in the CR, not the general overflow
        // condition in the way Ion conceives of it.
        SOBit = 0x3c,
        NSOBit = 0x34,
    };

    enum DoubleCondition {
        DoubleConditionUnordered = 0x100,    // Computation only. This is also synthetic.
        
        // These conditions will only evaluate to true if the comparison is ordered - i.e. neither operand is NaN.
        DoubleOrdered = 0x34,
        DoubleEqual = 0x2c,
        DoubleNotEqual = 0x24,
        DoubleGreaterThan = 0x1c,
        DoubleGreaterThanOrEqual = 0x04,
        DoubleLessThan = 0x0c,
        DoubleLessThanOrEqual = 0x14,
        // If either operand is NaN, these conditions always evaluate to true.
        // Except for DoubleUnordered, synthetic condition flags sit in the MSB
        // and are masked off by us but may be used by the MacroAssembler.
        DoubleUnordered = 0x3c,
        DoubleEqualOrUnordered = DoubleEqual | DoubleConditionUnordered,
        DoubleNotEqualOrUnordered = DoubleNotEqual | DoubleConditionUnordered,
        DoubleGreaterThanOrUnordered = DoubleGreaterThan | DoubleConditionUnordered,
        DoubleGreaterThanOrEqualOrUnordered = DoubleGreaterThanOrEqual | DoubleConditionUnordered,
        DoubleLessThanOrUnordered = DoubleLessThan | DoubleConditionUnordered,
        DoubleLessThanOrEqualOrUnordered = DoubleLessThanOrEqual | DoubleConditionUnordered,
    };
    
    enum LinkBit {
    	DontLinkB = 0,
    	LinkB = 1,
    };
    
    enum LikelyBit {
    	NotLikelyB = 0,
    	LikelyB = 1,
    };

	enum BranchAddressType {
		RelativeBranch = 0,
		AbsoluteBranch = 2,
	};
	
    BufferOffset nextOffset() {
        return m_buffer.nextOffset();
    }

  protected:
    Instruction * editSrc (BufferOffset bo) {
        return m_buffer.getInst(bo);
    }
  public:
    uint32_t actualOffset(uint32_t) const;
    uint32_t actualIndex(uint32_t) const;
    static uint8_t *PatchableJumpAddress(JitCode *code, uint32_t index);
  protected:

    // structure for fixing up pc-relative loads/jumps when a the machine code
    // gets moved (executable copy, gc, etc.)
    struct RelativePatch
    {
        // the offset within the code buffer where the value is loaded that
        // we want to fix-up
        BufferOffset offset;
        void *target;
        Relocation::Kind kind;

        RelativePatch(BufferOffset offset, void *target, Relocation::Kind kind)
          : offset(offset),
            target(target),
            kind(kind)
        { }
    };

    js::Vector<CodeLabel, 0, SystemAllocPolicy> codeLabels_;
    js::Vector<RelativePatch, 8, SystemAllocPolicy> jumps_;
    js::Vector<uint32_t, 8, SystemAllocPolicy> longJumps_;

    CompactBufferWriter jumpRelocations_;
    CompactBufferWriter dataRelocations_;
    CompactBufferWriter relocations_;
    CompactBufferWriter preBarriers_;

    PPCBufferWithExecutableCopy m_buffer;

    private:
        char const * nGPR(Register rreg)
        {
        	uint32_t reg = rreg.code();
            MOZ_ASSERT(reg <= 31);
            //MOZ_ASSERT(reg >= 0);
            static char const *names[] = {
                "r0",  "sp",  "r2",  "r3",  "r4",  "r5",  "r6",  "r7",
                "r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15",
                "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",
                "r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31"
            };
            return names[reg];
        }

        char const * nFPR(FloatRegister freg)
        {
        	uint32_t reg = freg.code();
            MOZ_ASSERT(reg <= 31);
            //MOZ_ASSERT(reg >= 0);
            static char const *names[] = {
                "f0",  "f1",  "f2",  "f3",  "f4",  "f5",  "f6",  "f7",
                "f8",  "f9",  "f10", "f11", "f12", "f13", "f14", "f15",
                "f16", "f17", "f18", "f19", "f20", "f21", "f22", "f23",
                "f24", "f25", "f26", "f27", "f28", "f29", "f30", "f31"
            };
            return names[reg];
        }
        
        char const * nCR(CRegisterID reg)
        {
            MOZ_ASSERT(reg <= 7);
            MOZ_ASSERT(reg >= 0);
            static char const *names[] = {
                "cr0", "cr1", "cr2", "cr3", "cr4", "cr5", "cr6", "cr7"
            };
            return names[reg];
        }

        char const * nSPR(SPRegisterID reg)
        {
            // XXX: we don't handle VRSAVE with this, but we don't use it yet.
            MOZ_ASSERT(reg >= 1);
            MOZ_ASSERT(reg <= 9);
            static char const *names[] = {
                "", "xer", "", "", "", "", "", "", "lr", "ctr"
            };
            return names[reg];
        }

	public: // used by MacroAssembler
        // Which absolute bit number does a condition register + Condition pair
        // refer to?
        static uint8_t crBit(CRegisterID cr, Condition cond)
        {
            return (cr << 2) + ((cond & 0xf0) >> 4);
        }
        
        static uint8_t crBit(CRegisterID cr, DoubleCondition cond)
        {
            return (cr << 2) + ((cond & 0xf0) >> 4);
        }

  public:
    Assembler()
      : m_buffer(),
        isFinished(false)
    { }

    static Condition InvertCondition(Condition cond);
    static DoubleCondition InvertCondition(DoubleCondition cond);

    // MacroAssemblers hold onto gcthings, so they are traced by the GC.
    void trace(JSTracer *trc);
    void writeRelocation(BufferOffset src) {
        jumpRelocations_.writeUnsigned(src.getOffset());
    }

    // As opposed to the x86/x64 version, the data relocation must be executed
    // beforehand to recover the pointer, not after.
    void writeDataRelocation(ImmGCPtr ptr) {
        if (ptr.value) {
        	if (gc::IsInsideNursery(ptr.value))
        		embedsNurseryPointers_ = true;
            dataRelocations_.writeUnsigned(nextOffset().getOffset());
        }
    }
    void writePrebarrierOffset(CodeOffset label) {
        preBarriers_.writeUnsigned(label.offset());
    }

  public:
    static uintptr_t GetPointer(uint8_t *);

    bool oom() const;

    void setPrinter(Sprinter *sp) {
    }

	static const Register getStackPointer() {
		// This is stupid.
		return StackPointer;
	}
	
  private:
    bool isFinished;
  public:
#if defined(DEBUG)
	void spew_with_address(const char *fmt, uint32_t ins, ...);
#endif
    void finish();
    void executableCopy(void *buffer);
    void copyJumpRelocationTable(uint8_t *dest);
    void copyDataRelocationTable(uint8_t *dest);
    void copyPreBarrierTable(uint8_t *dest);

    void addCodeLabel(CodeLabel label);
    size_t numCodeLabels() const {
        return codeLabels_.length();
    }
    CodeLabel codeLabel(size_t i) {
        return codeLabels_[i];
    }

    // Size of the instruction stream, in bytes.
    size_t size() const;
    // Size of the jump relocation table, in bytes.
    size_t jumpRelocationTableBytes() const;
    size_t dataRelocationTableBytes() const;
    size_t preBarrierTableBytes() const;

    // Size of the data table, in bytes.
    size_t bytesNeeded() const;

    // Write a blob of binary into the instruction stream *or*
    // into a destination address. If dest is nullptr (the default), then the
    // instruction gets written into the instruction stream. If dest is not null
    // it is interpreted as a pointer to the location that we want the
    // instruction to be written.
    BufferOffset writeInst(uint32_t x, uint32_t *dest = nullptr);
    // A static variant for the cases where we don't want to have an assembler
    // object at all. Normally, you would use the dummy (nullptr) object.
    static void WriteInstStatic(uint32_t x, uint32_t *dest);

  public:
    BufferOffset align(int alignment, bool useTrap = false);
  	uint32_t computeConditionCode(Condition op, CRegisterID cr = cr0);
  	uint32_t computeConditionCode(DoubleCondition op, CRegisterID cr = cr0);
    
    // To preserve compatibility with older code, opcode functions emit raw instructions
    // (i.e., no as_* prefix). 
    BufferOffset x_nop();

    // Branch and jump instructions.
    BufferOffset b(JOffImm26 off, BranchAddressType bat = RelativeBranch, LinkBit lb = DontLinkB);
    BufferOffset b(int32_t off, BranchAddressType bat = RelativeBranch, LinkBit lb = DontLinkB); // stubs into the above
    BufferOffset blr(LinkBit lb = DontLinkB);
    BufferOffset bctr(LinkBit lb = DontLinkB);
    
    // Conditional branches.
    BufferOffset bc(BOffImm16 off, Condition cond, CRegisterID cr = cr0, LikelyBit lkb = NotLikelyB, LinkBit lb = DontLinkB);
    BufferOffset bc(int16_t off, Condition cond, CRegisterID cr = cr0, LikelyBit lkb = NotLikelyB, LinkBit lb = DontLinkB);
    BufferOffset bc(BOffImm16 off, DoubleCondition cond, CRegisterID = cr0, LikelyBit lkb = NotLikelyB, LinkBit lb = DontLinkB);
    BufferOffset bc(int16_t off, DoubleCondition cond, CRegisterID = cr0, LikelyBit lkb = NotLikelyB, LinkBit lb = DontLinkB);
    BufferOffset bcctr(Condition cond, CRegisterID cr = cr0, LikelyBit lkb = NotLikelyB, LinkBit lb = DontLinkB);
    BufferOffset bcctr(DoubleCondition cond, CRegisterID cr = cr0, LikelyBit lkb = NotLikelyB, LinkBit lb = DontLinkB);
    
    BufferOffset bc(int16_t off, uint32_t op, LikelyBit lkb = NotLikelyB, LinkBit lb = DontLinkB);
    BufferOffset bcctr(uint32_t op, LikelyBit lkb = NotLikelyB, LinkBit lb = DontLinkB);

	// SPR operations.
	BufferOffset mtspr(SPRegisterID spr, Register ra);
	BufferOffset mfspr(Register rd, SPRegisterID spr);
	
	// CR operations.
#define DEF_CRCR(op) BufferOffset op(uint8_t t, uint8_t a, uint8_t b);

        DEF_CRCR(crand)
        DEF_CRCR(crandc)
        DEF_CRCR(cror)
        DEF_CRCR(crorc)
        DEF_CRCR(crxor)
#undef DEF_CRCR
	BufferOffset mtcrf(uint32_t mask, Register rs);
	BufferOffset mfcr(Register rd);
	BufferOffset mfocrf(Register rd, CRegisterID crfs); // G5 only
	BufferOffset x_mcrxr(CRegisterID crt, Register temp = r12); // emulated on G5, EEEK!
	
	// GPR operations and load-stores.
	BufferOffset neg(Register rd, Register rs);
	
	BufferOffset cmpw(CRegisterID cr, Register ra, Register rb);
	BufferOffset cmpwi(CRegisterID cr, Register ra, int16_t im);
	BufferOffset cmplw(CRegisterID cr, Register ra, Register rb);
	BufferOffset cmplwi(CRegisterID cr, Register ra, int16_t im);
	BufferOffset cmpw(Register ra, Register rb); // implied cr0
	BufferOffset cmpwi(Register ra, int16_t im);
	BufferOffset cmplw(Register ra, Register rb);
	BufferOffset cmplwi(Register ra, int16_t im);
	
	BufferOffset srawi(Register id, Register rs, uint8_t n);
	
	BufferOffset rlwinm(Register rd, Register rs, uint8_t sh, uint8_t mb, uint8_t me);
	BufferOffset rlwimi(Register rd, Register rs, uint8_t sh, uint8_t mb, uint8_t me); // cracked on G5
	
#define DEF_ALU2(op) BufferOffset op(Register rd, Register ra, Register rb); \
                     BufferOffset op##_rc(Register rd, Register ra, Register rb);         
        DEF_ALU2(add)
        DEF_ALU2(addc)
        DEF_ALU2(adde)
        DEF_ALU2(addo)
        DEF_ALU2(subf)
        DEF_ALU2(subfc)
        DEF_ALU2(subfe)
        DEF_ALU2(subfo)
        DEF_ALU2(divw)
        DEF_ALU2(divwo)
        DEF_ALU2(divwu)
        DEF_ALU2(divwuo)
        DEF_ALU2(mullw)
        DEF_ALU2(mulhw)
        DEF_ALU2(mulhwu)
        DEF_ALU2(mullwo)
        DEF_ALU2(eqv) // NB: Implemented differently.
#undef DEF_ALU2

#define DEF_ALUI(op) BufferOffset op(Register rd, Register ra, int16_t im); \
                     BufferOffset op##_rc(Register rd, Register ra, int16_t im);
        DEF_ALUI(addi)
        DEF_ALUI(addic)
        DEF_ALUI(addis)
        // NB: mulli is usually strength-reduced, since it can take up to five
        // cycles in the worst case. See x_sr_mulli.
        DEF_ALUI(mulli)
#undef DEF_ALUI

#define DEF_ALUE(op) BufferOffset op(Register rd, Register ra); \
                     BufferOffset op##_rc(Register rd, Register ra);
        DEF_ALUE(addme)
        DEF_ALUE(addze)
        DEF_ALUE(subfze)
        DEF_ALUE(cntlzw) // NB: In this case, rd = ra and ra = rs, but no biggie here.
#undef DEF_ALUE

#define DEF_BITALU2(op) BufferOffset op(Register rd, Register rs, Register rb); \
                        BufferOffset op##_rc(Register rd, Register rs, Register rb);
        DEF_BITALU2(andc)
        DEF_BITALU2(nand)
        DEF_BITALU2(nor)
        DEF_BITALU2(slw)
        DEF_BITALU2(srw)
        DEF_BITALU2(sraw)
        DEF_BITALU2(sld)
        DEF_BITALU2(srd)
        DEF_BITALU2(srad)
        DEF_BITALU2(and_) // NB: See terminal _ constants above. This will have and_ and and__rc.
        DEF_BITALU2(or_)
        DEF_BITALU2(xor_)
#undef DEF_BITALU2

#define DEF_BITALUI(op) BufferOffset op(Register rd, Register ra, uint16_t im); \
                        BufferOffset op##_rc(Register rd, Register ra, uint16_t im);
        DEF_BITALUI(ori)
        DEF_BITALUI(oris)
        DEF_BITALUI(xori)
        DEF_BITALUI(xoris)
        // There is no Rc-less version of andi/andis.
        BufferOffset andi_rc(Register rd, Register ra, uint16_t im);
        BufferOffset andis_rc(Register rd, Register ra, uint16_t im);
#undef DEF_BITALUI
        
#define DEF_ALUEXT(op) BufferOffset op(Register rd, Register rs); \
                       BufferOffset op##_rc(Register rd, Register rs);
        DEF_ALUEXT(extsb)
        DEF_ALUEXT(extsh)
        DEF_ALUEXT(extsw)
#undef DEF_ALUEXT

#define DEF_MEMd(op) BufferOffset op(Register rd, Register rb, int16_t off);
        DEF_MEMd(lbz)
        DEF_MEMd(lha)
        DEF_MEMd(lhz)
        DEF_MEMd(lwz)
        DEF_MEMd(ld)

        DEF_MEMd(stb)
        DEF_MEMd(stw)
        DEF_MEMd(stwu)
        DEF_MEMd(sth)
        DEF_MEMd(std)
        DEF_MEMd(stdu)
#undef DEF_MEMd

#define DEF_MEMx(op) BufferOffset op(Register rd, Register ra, Register rb);
        DEF_MEMx(lbzx)
        DEF_MEMx(lhax)
        DEF_MEMx(lhzx)
        DEF_MEMx(lhbrx)
        DEF_MEMx(lwzx)
        DEF_MEMx(lwbrx)
        DEF_MEMx(ldx)

        DEF_MEMx(stbx)
        DEF_MEMx(stwx)
        DEF_MEMx(stwux)
        DEF_MEMx(stwbrx)
        DEF_MEMx(sthx)
        DEF_MEMx(sthbrx)
        DEF_MEMx(stdx)
        DEF_MEMx(stdux)
#undef DEF_MEMx

    // FPR operations and load-stores.
    BufferOffset fcmpu(CRegisterID cr, FloatRegister ra, FloatRegister rb);
    BufferOffset fcmpu(FloatRegister ra, FloatRegister rb); // implied cr0
#define DEF_FPUAC(op) BufferOffset op(FloatRegister rd, FloatRegister ra, FloatRegister rc); \
                      BufferOffset op##_rc(FloatRegister rd, FloatRegister ra, FloatRegister rc);
        DEF_FPUAC(fmul)
        DEF_FPUAC(fmuls)
#undef DEF_FPUAC

#define DEF_FPUAB(op) BufferOffset op(FloatRegister rd, FloatRegister ra, FloatRegister rc); \
                      BufferOffset op##_rc(FloatRegister rd, FloatRegister ra, FloatRegister rc);
        DEF_FPUAB(fadd)
        DEF_FPUAB(fdiv)
        DEF_FPUAB(fsub)
        DEF_FPUAB(fadds)
        DEF_FPUAB(fdivs)
        DEF_FPUAB(fsubs)
#undef DEF_FPUAB

#define DEF_FPUDS(op) BufferOffset op(FloatRegister rd, FloatRegister rs); \
                      BufferOffset op##_rc(FloatRegister rd, FloatRegister rs);
        DEF_FPUDS(fabs)
        DEF_FPUDS(fneg)
        DEF_FPUDS(fmr)
        DEF_FPUDS(fcfid)
        DEF_FPUDS(fctiw)
        DEF_FPUDS(fctiwz)
        DEF_FPUDS(frsp)
        DEF_FPUDS(frsqrte)

        // G5 only
        DEF_FPUDS(fsqrt)
#undef DEF_FPUDS

// In Ion, the semantics for this macro are now corrected compared to JM/PPCBC. 
// (See OPPCC p.432, etc.)
#define DEF_FPUABC(op) BufferOffset op(FloatRegister rd, FloatRegister ra, FloatRegister rc, FloatRegister rb); \
                       BufferOffset op##_rc(FloatRegister rd, FloatRegister ra, FloatRegister rc, FloatRegister rb);
        DEF_FPUABC(fmadd)
        DEF_FPUABC(fnmsub)
        DEF_FPUABC(fsel)
#undef DEF_FPUABC

#define DEF_FMEMd(op) BufferOffset op(FloatRegister rd, Register rb, int16_t off);
        DEF_FMEMd(lfd)
        DEF_FMEMd(lfs)
        DEF_FMEMd(stfd)
        DEF_FMEMd(stfs)
        DEF_FMEMd(stfdu)
#undef DEF_FMEMd

#define DEF_FMEMx(op) BufferOffset op(FloatRegister rd, Register ra, Register rb);
        DEF_FMEMx(lfdx)
        DEF_FMEMx(lfsx)
        DEF_FMEMx(stfdx)
        DEF_FMEMx(stfsx)
#undef DEF_FMEMx

// convert SPRid to 10-bit split encoding (OPPCC appendix A, p.514)
#define PPC_SPR(x) (((int)x>>5) | ((int)x & 31)<<5)

	BufferOffset mtfsb0(uint8_t bt);
	BufferOffset mtfsb1(uint8_t bt);
	BufferOffset mtfsfi(uint8_t fi, uint8_t imm);
	BufferOffset mcrf(CRegisterID bt, CRegisterID bs);
	BufferOffset mcrfs(CRegisterID bf, uint8_t bfa);

	// Conveniences and generally accepted alternate mnemonics.
	BufferOffset x_trap();
	BufferOffset x_mtrap(); // Codegen for marking traps in output.
	BufferOffset x_mr(Register rd, Register ra);
	BufferOffset x_beq(CRegisterID cr, int16_t off, LikelyBit lkb = NotLikelyB, LinkBit lb = DontLinkB);
	BufferOffset x_bne(CRegisterID cr, int16_t off, LikelyBit lkb = NotLikelyB, LinkBit lb = DontLinkB);
	BufferOffset x_bdnz(int16_t off, LikelyBit lkb = NotLikelyB, LinkBit lb = DontLinkB);
	BufferOffset x_mtctr(Register ra);
	BufferOffset x_mtlr(Register ra);
	BufferOffset x_mflr(Register rd);
	BufferOffset x_mtcr(Register rs);
	BufferOffset x_insertbits0_15(Register rd, Register rs);
	BufferOffset x_bit_value(Register rd, Register rs, unsigned bit);
	BufferOffset x_slwi(Register rd, Register rs, int n);
	BufferOffset x_srwi(Register rd, Register rs, int n);
	BufferOffset x_subi(Register rd, Register ra, int16_t im);
	BufferOffset x_sr_mulli(Register rd, Register ra, int16_t im);

	// Large loads.	
	BufferOffset x_li(Register rd, int16_t im);
	BufferOffset x_lis(Register rd, int16_t im);
	BufferOffset x_p_li32(Register rd, int32_t im);
	BufferOffset x_li32(Register rd, int32_t im);

    // Label operations.
    void bind(Label *label, BufferOffset boff = BufferOffset());
    void bind(RepatchLabel *label);
    void bind(CodeOffset *label) { label->bind(currentOffset()); }
    uint32_t currentOffset() {
        return nextOffset().getOffset();
    }
    void retarget(Label *label, Label *target);
    void Bind(uint8_t *rawCode, CodeOffset *label, const void *address);

    // Fast fixed branches.
#define SHORT_LABEL(w) BufferOffset w = nextOffset()
#define SHORT_LABEL_MASM(w) BufferOffset w = masm.nextOffset()
    // Binds instruction i to offset s as a fixed short branch.
    void bindS(BufferOffset s, BufferOffset i);
    void bindSS(BufferOffset i); // s is implied as the current offset.

    // See Bind.
    size_t labelOffsetToPatchOffset(size_t offset) {
        return actualOffset(offset);
    }

    void call(Label *label);
    void call(void *target);

    static void TraceJumpRelocations(JSTracer *trc, JitCode *code, CompactBufferReader &reader);
    static void TraceDataRelocations(JSTracer *trc, JitCode *code, CompactBufferReader &reader);
    /*
    static void FixupNurseryObjects(JSContext* cx, JitCode* code, CompactBufferReader& reader,
                                    const ObjectVector& nurseryObjects);
    */

    static bool SupportsFloatingPoint() {
        return true;
    }
    static bool SupportsSimd() {
        return false;
    }

  protected:
    void bind(InstImm *inst, uint32_t branch, uint32_t target);
    void addPendingJump(BufferOffset src, ImmPtr target, Relocation::Kind kind) {
        enoughMemory_ &= jumps_.append(RelativePatch(src, target.value, kind));
        if (kind == Relocation::JITCODE)
            writeRelocation(src);
    }

    void addLongJump(BufferOffset src) {
        enoughMemory_ &= longJumps_.append(src.getOffset());
    }

  public:
    size_t numLongJumps() const {
        return longJumps_.length();
    }
    uint32_t longJump(size_t i) {
        return longJumps_[i];
    }

    // Copy the assembly code to the given buffer, and perform any pending
    // relocations relying on the target address.
    void executableCopy(uint8_t *buffer);

    void flushBuffer() {
    }

    BufferOffset haltingAlign(int alignment);
    BufferOffset nopAlign(int alignment);

    static uint32_t PatchWrite_NearCallSize();
    static uint32_t NopSize() { return 4; }

    static uint32_t ExtractLisOriValue(Instruction *inst0, Instruction *inst1);
    static void UpdateLisOriValue(Instruction *inst0, Instruction *inst1, uint32_t value);
    static void WriteLisOriInstructions(Instruction *inst, Instruction *inst1,
                                        Register reg, uint32_t value);

    static void PatchWrite_NearCall(CodeLocationLabel start, CodeLocationLabel toCall);
    static void PatchDataWithValueCheck(CodeLocationLabel label, PatchedImmPtr newValue,
                                        PatchedImmPtr expectedValue);
    static void PatchDataWithValueCheck(CodeLocationLabel label, ImmPtr newValue,
                                        ImmPtr expectedValue);
    static void PatchWrite_Imm32(CodeLocationLabel label, Imm32 imm);

    static void PatchInstructionImmediate(uint8_t *code, PatchedImmPtr imm);

    static uint8_t *NextInstruction(uint8_t *instruction, uint32_t *count = nullptr);

    static void ToggleToJmp(CodeLocationLabel inst_);
    static void ToggleToCmp(CodeLocationLabel inst_);

    static void ToggleCall(CodeLocationLabel inst_, bool enabled);

    static void UpdateBoundsCheck(uint32_t logHeapSize, Instruction *inst);
    void processCodeLabels(uint8_t *rawCode);
    static int32_t ExtractCodeLabelOffset(uint8_t *code);

    bool bailed() {
        return m_buffer.bail();
    }
    
    void verifyHeapAccessDisassembly(uint32_t begin, uint32_t end,
                                     const Disassembler::HeapAccess& heapAccess)
    {
        // Implement this if we implement a disassembler.
    }

    // Blah
    bool asmMergeWith(const Assembler& other) { MOZ_CRASH(); return false; }
    void retargetWithOffset(size_t baseOffset, const LabelBase* label,
                            Label* target) { MOZ_CRASH(); }
}; // Assembler

static const uint32_t OpcodeShift = 26;
static const uint32_t OpcodeBits = 6;

class Instruction
{
  protected:
    uint32_t data;

  public:
    Instruction (uint32_t data_) : data(data_) { }
    Instruction (PPCOpcodes op_) : data((uint32_t)op_) { }

    uint32_t encode() const {
        return data;
    }

	// Quickies
    void makeOp_nop() {
        data = PPC_nop;
    }
    void makeOp_mtctr(Register r) {
    	data = PPC_mtspr | ((uint32_t)r.code())<<21 | PPC_SPR(ctr)<<11 ;
    }
    void makeOp_bctr(Assembler::LinkBit l = Assembler::DontLinkB) {
    	data = PPC_bctr | l;
    }

    void setData(uint32_t data) {
        this->data = data;
    }

    const Instruction & operator=(const Instruction &src) {
        data = src.data;
        return *this;
    }

    uint32_t extractOpcode() {
        return (data & PPC_MAJOR_OPCODE_MASK); // ">> 26"
    }
    bool isOpcode(uint32_t op) {
    	return (extractOpcode() == (op & PPC_MAJOR_OPCODE_MASK));
    }
  	void assertOpcode(uint32_t op) {
		MOZ_ASSERT(isOpcode(op));
	}

    // Get the next instruction in the instruction stream.
    // This will do neat things like ignore constant pools and their guards,
    // if we ever implement those again.
    Instruction *next();

    // Sometimes the backend wants a uint32_t (or a pointer to it) rather than
    // an instruction.  raw() just coerces this into a pointer to a uint32_t.
    const uint32_t *raw() const { return &data; }
    uint32_t size() const { return 4; }
}; // Instruction

class InstImm : public Instruction
{
  // Valid for reg/reg/imm instructions only and bc.
  // XXX: Assert that at some point.
  public:
  	InstImm (PPCOpcodes op) : Instruction(op) { }
  	
    void setBOffImm16(BOffImm16 off) {
    	data = (data & 0xFFFF0000) | off.encode();
    }
    void setImm16(Imm16 off) {
    	data = (data & 0xFFFF0000) | off.encode();
    }
    uint32_t extractImm16Value() {
    	return (data & 0x0000FFFF);
    }
    void setUpperReg(Register ru) {
    	// Mask out upper register field and put in the new one.
    	// For addi/addis, this is the DESTINATION.
    	// For ori/oris, this is the SOURCE.
    	// For bc, this is BO.
    	data = (data & 0xFC1FFFFF) | ((uint32_t)ru.code() << 21);
    }
    void setLowerReg(Register rl) {
    	// Mask out lower register field and put in the new one.
    	// For addi/addis, this is the SOURCE. (For li/lis, this should be ZERO.)
    	// For ori/oris, this is the DESTINATION.
    	// For bc, this is BI.
    	data = (data & 0xFFE0FFFF) | ((uint32_t)rl.code() << 16);
    }
}; // InstImm

// If this assert is not satisfied, we can't use Instruction to patch in-place.
static_assert(sizeof(Instruction) == 4, "sizeof(Instruction) must be 32-bit word.");

static const uint32_t NumIntArgRegs = 8;

static inline bool
GetIntArgReg(uint32_t usedArgSlots, Register *out)
{
    if (usedArgSlots < NumIntArgRegs) {
    	// Argregs start at r3!
        *out = Register::FromCode((Register::Code)(3 + usedArgSlots));
        return true;
    }
    return false;
}

// Get a register in which we plan to put a quantity that will be used as an
// integer argument. Because we have so many argument registers on PowerPC, this
// essentially stubs into GetIntArgReg because failure is incredibly improbable.
static inline bool
GetTempRegForIntArg(uint32_t usedIntArgs, uint32_t usedFloatArgs, Register *out)
{
    // NB: this implementation can't properly determine yet which regs are used if there are
    // float arguments.
    MOZ_ASSERT(usedFloatArgs == 0);

    if (GetIntArgReg(usedIntArgs, out))
        return true;
    return false;
}

static inline uint32_t
GetArgStackDisp(uint32_t usedArgSlots)
{
#if(0)
    // NYI. This situation should never occur.
    MOZ_ASSERT(usedArgSlots >= NumIntArgRegs);
    return usedArgSlots * sizeof(intptr_t);
#else
    MOZ_CRASH("unexpected spill to stack");
    return 0;
#endif
}

// For possible future expansion.
static const uint32_t PPC_STANZA_LENGTH = 16;

} // namespace jit
} // namespace js

// Convenience macros from JM/PPCBC.

// whether a (Trusted)Imm32 fits in an unsigned immediate value
#define PPC_IMM_OK_U(x) (MOZ_LIKELY(((x).m_value & 0xffff0000) == 0))

// whether a (Trusted)Imm32 fits in a signed immediate value
#define PPC_IMM_OK_S(x) (MOZ_LIKELY(((x).m_value & 0xffff8000) == 0 || \
    ((x).m_value & 0xffff8000) == 0xffff8000))

// whether the offset part of an Address fits in a (signed) immediate value
#define PPC_OFFS_OK(x) (MOZ_LIKELY(((x).offset & 0xffff8000) == 0 || \
    ((x).offset & 0xffff8000) == 0xffff8000))

// same test, but checking a bit ahead (for multiple loads)
#define PPC_OFFS_INCR_OK(x, incr) (MOZ_LIKELY((((x).offset + incr) & 0xffff8000) == 0 || \
    (((x).offset + incr) & 0xffff8000) == 0xffff8000))

#endif /* jit_ppc_Assembler_ppc_h */

/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_ppc_Architecture_ppc_h
#define jit_ppc_Architecture_ppc_h

#include "mozilla/MathAlgorithms.h"

#include <limits.h>
#include <stdint.h>

#include "js/Utility.h"

/* The new TenFourFox 32-bit PowerOpen-compliant JIT. */

namespace js {
namespace jit {

// Not used on PPC.
static const uint32_t ShadowStackSpace = 0;

// These offsets are specific to nunboxing, and capture offsets into the
// components of a js::Value.
// We are big-endian, unlike all those other puny little-endian architectures,
// so we use different constants. (type == tag)
static const int32_t NUNBOX32_TYPE_OFFSET = 0;
static const int32_t NUNBOX32_PAYLOAD_OFFSET = 4;

// Size of each bailout table entry.
// For PowerPC this is a single bl.
static const uint32_t BAILOUT_TABLE_ENTRY_SIZE = sizeof(void *);

// GPRs.
class Registers
{
  public:
    enum RegisterID {
        r0 = 0,
        tempRegister = r0,
        r1,
        sp = r1,
        stackPointerRegister = r1,
        r2,
        r3,
        r4,
        r5,
        r6,
        r7,
        r8,
        r9,
        r10,
        lr,   /* This is a lie so that pushing register sets works. We never use r11. */
        r12,
        addressTempRegister = r12,
        r13,
        r14,
        r15,
        r16,
        r17,
        r18,
        r19,
        r20,
        r21,
        r22,
        r23,
        r24,
        r25,
        r26,
        r27,
        r28,
        r29,
        r30,
        r31,
        invalid_reg
    };
    typedef RegisterID Code;
    typedef RegisterID Encoding;
    
    // Content spilled during bailouts.
    union RegisterContent {
    	uintptr_t r;
    };

    static const char *GetName(Code code) {
        static const char *Names[] = {
             "r0",  "sp",  "toc", "r3",  "r4",  "r5",  "r6",  "r7",
             "r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15",
             "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",
             "r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31"};
        return Names[code];
    }
    static const char *GetName(uint32_t i) {
        MOZ_ASSERT(i < Total);
        return GetName(Code(i));
    }

    static Code FromName(const char *name);

    static const Encoding StackPointer = sp;
    static const Encoding Invalid = invalid_reg;

    static const uint32_t Total = 32;
    static const uint32_t Allocatable = 15;

    static const uint32_t AllMask = 0xffffffff;
    static const uint32_t ArgRegMask =
        (1 << Registers::r3) |
        (1 << Registers::r4) |
        (1 << Registers::r5) |
        (1 << Registers::r6) |
        (1 << Registers::r7) |
        (1 << Registers::r8) |
        (1 << Registers::r9) |
        (1 << Registers::r10);

    static const uint32_t VolatileMask = ArgRegMask;

    // We use this constant to save registers when entering functions. This
    // is why fake-LR is added here too.
    static const uint32_t NonVolatileMask =
    	(1 << Registers::lr)  |
    	(1 << Registers::r2)  |
        (1 << Registers::r13) |
        (1 << Registers::r14) |
        (1 << Registers::r15) |
        (1 << Registers::r16) |
        (1 << Registers::r17) |
        (1 << Registers::r18) |
        (1 << Registers::r19) |
        (1 << Registers::r20) |
        (1 << Registers::r21) |
        (1 << Registers::r22) |
        (1 << Registers::r23) |
        (1 << Registers::r24) |
        (1 << Registers::r25) |
        (1 << Registers::r26) |
        (1 << Registers::r27) |
        (1 << Registers::r28) |
        (1 << Registers::r29) |
        (1 << Registers::r30) |
        (1 << Registers::r31);

    static const uint32_t WrapperMask =
        VolatileMask |          // = arguments
        (1 << Registers::r14) | // = outReg
        (1 << Registers::r15);  // = argBase

    static const uint32_t NonAllocatableMask =
        (1 << Registers::r0)  |
        (1 << Registers::sp)  |
        (1 << Registers::lr)  |
        (1 << Registers::r2)  |
        (1 << Registers::r12) |
        // These are non-volatile work registers held over from PPCBC.
        (1 << Registers::r16) |
        (1 << Registers::r17) |
        (1 << Registers::r18);
        // Despite its use as a rectifier, r19 must be allocatable (see
        // ICCallScriptedCompiler::generateStubCode).

    // Registers that can be allocated without being saved, generally.
    static const uint32_t TempMask = VolatileMask & ~NonAllocatableMask;

    // Registers returned from a JS -> JS call.
    static const uint32_t JSCallMask =
        (1 << Registers::r5) |
        (1 << Registers::r6);

    // Registers returned from a JS -> C call.
    static const uint32_t CallMask =
        (1 << Registers::r3) |
        (1 << Registers::r4);  // used for double-size returns
 
    static const uint32_t AllocatableMask =
    	// Be explicit
        (1 << Registers::r3)  |
        (1 << Registers::r4)  |
        (1 << Registers::r5)  |
        (1 << Registers::r6)  |
        (1 << Registers::r7)  |
        (1 << Registers::r8)  |
        (1 << Registers::r9)  |
        (1 << Registers::r10) |
        (1 << Registers::r13) |
        (1 << Registers::r14) |
        (1 << Registers::r15) |
        (1 << Registers::r19) |
        (1 << Registers::r20) |
        (1 << Registers::r21) |
        (1 << Registers::r22) |
        (1 << Registers::r23) |
        (1 << Registers::r24) |
        (1 << Registers::r25);

    typedef uint32_t SetType;
    static uint32_t SetSize(SetType x) {
        static_assert(sizeof(SetType) == 4, "SetType must be 32 bits");
        return mozilla::CountPopulation32(x);
    }
    static uint32_t FirstBit(SetType x) {
        return mozilla::CountTrailingZeroes32(x);
    }
    static uint32_t LastBit(SetType x) {
        return 31 - mozilla::CountLeadingZeroes32(x);
    }
};

// Smallest integer type that can hold a register bitmask.
typedef uint32_t PackedRegisterMask;


// FPRs.
class FloatRegisters
{
  public:
    enum FPRegisterID {
        f0 = 0,
        f1,
        f2,
        f3,
        f4,
        f5,
        f6,
        f7,
        f8,
        f9,
        f10,
        f11,
        f12,
        f13,
        f14,
        f15,
        f16,
        f17,
        f18,
        f19,
        f20,
        f21,
        f22,
        f23,
        f24,
        f25,
        f26,
        f27,
        f28,
        f29,
        f30,
        f31,
        invalid_freg
    };
    typedef FPRegisterID Code;
    typedef FPRegisterID Encoding;
    
    // Content spilled during bailouts.
    union RegisterContent {
    	double d;
    };

    static const char *GetName(Code code) {
        static const char * const Names[] = { "f0", "f1", "f2", "f3",  "f4", "f5",  "f6", "f7",
                                              "f8", "f9",  "f10", "f11", "f12", "f13",
                                              "f14", "f15", "f16", "f17", "f18", "f19",
                                              "f20", "f21", "f22", "f23", "f24", "f25",
                                              "f26", "f27", "f28", "f29", "f30", "f31"};
        return Names[code];
    }
    static const char *GetName(uint32_t i) {
        MOZ_ASSERT(i < Total);
        return GetName(Code(i % 32));
    }

    static Code FromName(const char *name);

    static const Code Invalid = invalid_freg;

    static const uint32_t Total = 32;
    static const uint32_t TotalDouble = 32;
    static const uint32_t TotalSingle = 32;
    
    // By declaring the allocator can only use the volatile FPRs, this
    // saves us a great deal of complexity in the trampoline because we don't
    // have to save anything. (Don't allocate f0/1/2, though.)
    // XXX: This might make spills to the stack for ABI-compliant calls bigger.
    static const uint32_t Allocatable = 11;

    static const uint32_t TotalPhys = 32;
    static const uint32_t AllDoubleMask = 0xffffffff;
    static const uint32_t AllMask = AllDoubleMask;

    static const uint32_t VolatileMask = 
        (1 << FloatRegisters::f0)  |
        (1 << FloatRegisters::f1)  |
        (1 << FloatRegisters::f2)  |
        (1 << FloatRegisters::f3)  |
        (1 << FloatRegisters::f4)  |
        (1 << FloatRegisters::f5)  |
        (1 << FloatRegisters::f6)  |
        (1 << FloatRegisters::f7)  |
        (1 << FloatRegisters::f8)  |
        (1 << FloatRegisters::f9)  |
        (1 << FloatRegisters::f10) |
        (1 << FloatRegisters::f11) |
        (1 << FloatRegisters::f12) |
        (1 << FloatRegisters::f13);

    static const uint32_t NonVolatileMask = 
        (1 << FloatRegisters::f14) |
        (1 << FloatRegisters::f15) |
        (1 << FloatRegisters::f16) |
        (1 << FloatRegisters::f17) |
        (1 << FloatRegisters::f18) |
        (1 << FloatRegisters::f19) |
        (1 << FloatRegisters::f20) |
        (1 << FloatRegisters::f21) |
        (1 << FloatRegisters::f22) |
        (1 << FloatRegisters::f23) |
        (1 << FloatRegisters::f24) |
        (1 << FloatRegisters::f25) |
        (1 << FloatRegisters::f26) |
        (1 << FloatRegisters::f27) |
        (1 << FloatRegisters::f28) |
        (1 << FloatRegisters::f29) |
        (1 << FloatRegisters::f30) |
        (1 << FloatRegisters::f31);

	static const uint32_t VolatileDoubleMask = VolatileMask;
	static const uint32_t NonVolatileDoubleMask = NonVolatileMask;

    static const uint32_t WrapperMask = VolatileMask;

    // The allocator is not allowed to use f0 (the scratch FPR), nor any of
    // the non-volatile registers, nor f3 for some routines. We also hide
    // f2 from the allocator in case we need it for convertInt32ToDouble.
    // f1 must be allocatable because Ion may expect to optimize with it.
    static const uint32_t NonAllocatableMask =
        (1 << FloatRegisters::f0) |
        (1 << FloatRegisters::f2) |
        (1 << FloatRegisters::f3) |
        NonVolatileMask;
	static const uint32_t NonAllocatableDoubleMask = NonAllocatableMask;

    // Registers that can be allocated without being saved, generally.
    static const uint32_t TempMask = VolatileMask & ~NonAllocatableMask;
	
    static const uint32_t AllocatableMask = 
        // Be explicit
        (1 << FloatRegisters::f1) |
        (1 << FloatRegisters::f4) |
        (1 << FloatRegisters::f5) |
        (1 << FloatRegisters::f6) |
        (1 << FloatRegisters::f7) |
        (1 << FloatRegisters::f8) |
        (1 << FloatRegisters::f9) |
        (1 << FloatRegisters::f10) |
        (1 << FloatRegisters::f11) |
        (1 << FloatRegisters::f12) |
        (1 << FloatRegisters::f13);
    static const uint32_t AllocatableDoubleMask = AllocatableMask;

    typedef uint32_t SetType;
};

template <typename T>
class TypedRegisterSet;

class FloatRegister
{
  public:
    typedef FloatRegisters Codes;
    typedef Codes::Code Code;
    typedef Codes::Encoding Encoding;

    Code code_;

    MOZ_CONSTEXPR FloatRegister(uint32_t code)
      : code_ (Code(code))
    { }
    MOZ_CONSTEXPR FloatRegister()
      : code_(Code(FloatRegisters::invalid_freg))
    { }

	operator int() const { return code_; } // Man, wish we could do this with Register.
	
    bool operator==(const FloatRegister &other) const {
        MOZ_ASSERT(!isInvalid());
        MOZ_ASSERT(!other.isInvalid());
        return code_ == other.code_;
    }
    bool isDouble() const { return true; }
    bool isSingle() const { return true; }
    // SIMD. These assume that, on architectures with SIMD, that
    // the FPRs are the vector registers. They are, of course, full of it.
    bool isInt32x4() const { return false; }
    bool isFloat32x4() const { return false; }
    bool isSimd128() const { return false; }
    bool equiv(const FloatRegister &other) const { return true; }
    size_t size() const { return 8; }
    bool isInvalid() const {
        return code_ == FloatRegisters::invalid_freg;
    }
    FloatRegister asSingle() const {
    	MOZ_ASSERT(!isInvalid());
    	return FloatRegister(code_);
    }
    FloatRegister asDouble() const {
    	MOZ_ASSERT(!isInvalid());
    	return FloatRegister(code_);
    }
    FloatRegister asInt32x4() const { MOZ_CRASH("NYI"); }
    FloatRegister asFloat32x4() const { MOZ_CRASH("NYI"); }

    Code code() const {
        MOZ_ASSERT(!isInvalid());
        return code_;
    }
    Encoding encoding() {
    	MOZ_ASSERT(!isInvalid());
    	return code_;
    }
    uint32_t id() const {
        return code_;
    }
    static FloatRegister FromCode(uint32_t i) {
        MOZ_ASSERT(i < FloatRegisters::Total);
        FloatRegister r = { (FloatRegisters::Code)i };
        return r;
    }

    bool volatile_() const {
        return !!((1 << code()) & FloatRegisters::VolatileMask);
    }
    const char *name() const {
        return FloatRegisters::GetName(code_);
    }
    bool operator != (const FloatRegister &other) const {
        return code_ != other.code_;
    }
    bool aliases(const FloatRegister &other) {
    	return code_ == other.code_;
    }
    uint32_t numAliased() const {
        return 1;
    }
    uint32_t numAlignedAliased() const {
    	return 1;
    }
    // N.B. FloatRegister is an explicit outparam here because msvc-2010
    // miscompiled it on win64 when the value was simply returned. We rip
    // off x86, so we'll do the same, puddin' tame.
    void aliased(uint32_t aliasIdx, FloatRegister *ret) {
        MOZ_ASSERT(aliasIdx == 0);
        *ret = *this;
    }
    void alignedAliased(uint32_t aliasIdx, FloatRegister *ret) {
        MOZ_ASSERT(aliasIdx == 0);
        *ret = *this;
    }
    typedef FloatRegisters::SetType SetType;
    SetType alignedOrDominatedAliasedSet() const {
    	// Always double, always a single register.
    	return SetType(1) << code_;
    }
    static uint32_t SetSize(SetType x) {
        static_assert(sizeof(SetType) == 4, "SetType must be 32 bits");
        return mozilla::CountPopulation32(x);
    }
    static Code FromName(const char *name) {
        return FloatRegisters::FromName(name);
    }
    static TypedRegisterSet<FloatRegister> ReduceSetForPush(const TypedRegisterSet<FloatRegister> &s);
    static uint32_t GetSizeInBytes(const TypedRegisterSet<FloatRegister> &s);
    static uint32_t GetPushSizeInBytes(const TypedRegisterSet<FloatRegister> &s);
    uint32_t getRegisterDumpOffsetInBytes();
    static uint32_t FirstBit(SetType x) {
        return mozilla::CountTrailingZeroes32(x);
    }
    static uint32_t LastBit(SetType x) {
        return 31 - mozilla::CountLeadingZeroes32(x);
    }
};

// SPRs (PPC backend specific).
// These have no peer in lesser chips. That is because PPC has no peer in
// lesser chips. These don't count against the register cap because the
// allocator is unaware of them. In fact, we don't treat these as regular
// registers at all (hey, they're Special Purpose anyway).
#if(0)
class SPRs // Class definition not yet supported.
{
  public:
#endif
    enum SPRegisterID {
      xer = 1,
      lr_spr = 8, /* I'm in the REAL witness protection program. */
      ctr = 9,
      vrsave = 256, /* for future SIMD JS */
      invalid_spreg
    };
#if(0)
    static const char *getSPRName(SPRegisterID code) {
#define XXX "INVALID"
        static const char *N_vrsave = "vrsave";
        static const char *N_bogus = XXX;
        static const char *Names[] = {
            XXX, "xer", XXX, XXX, XXX, XXX, XXX, XXX,
            "lr","ctr"
        };
#undef XXX
        return 
               (code == vrsave) ? N_vrsave :
               (code >  ctr)    ? N_bogus :
               Names[code];
    }
};
#endif

// CRs (PPC backend specific).
// We have eight condition registers, each for how unconditionally wonderful
// PowerPC is, and sometimes for storing condition results.
// Assume CR0 as default.
#if(0)
class CRs // Class definition not yet supported.
{
  public:
#endif
    enum CRegisterID {
      cr0 = 0,
      cr1,
      cr2,
      cr3,
      cr4,
      cr5,
      cr6,
      cr7,
      invalid_creg
    };
#if(0)
    static const char *getCRName(CRegisterID code) {
        static const char *Names[] = {
            "cr0",  "cr1",  "cr2",  "cr3",  "cr4",  "cr5",  "cr6",  "cr7"
        };
        return Names[code];
    }
};
#endif

// All spades are groovy.
inline bool
hasFPU() {
	return true;
}

// All spades are groovy, I mean, FPRs can be treated as floats or doubles.
inline bool
hasUnaliasedDouble() {
    return false;
}

inline bool
hasMultiAlias() {
    return false;
}

inline bool
has_altivec() {
#ifdef TENFOURFOX_VMX
    return true;
#else
    return false;
#endif
}

inline bool
has_sqrt() {
#ifdef TENFOURFOX_G5
    return true;
#else
    return false;
#endif
}

// See the comments above AsmJSMappedSize in AsmJSValidate.h for more info.
// TODO: Note that it requires Codegen to respect the
// offset field of AsmJSHeapAccess.
static const size_t AsmJSCheckedImmediateRange = 0;
static const size_t AsmJSImmediateRange = 0;

} // namespace jit
} // namespace js

#endif /* jit_ppc_Architecture_ppc_h */

/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
 
/* This is our equivalent of SharedICRegisters but I'm too lazy to bother. */

#ifndef jit_ppc_BaselineRegisters_ppc_h
#define jit_ppc_BaselineRegisters_ppc_h

#include "jit/MacroAssembler.h"

namespace js {
namespace jit {

// r13 has a copy of the stack pointer for Baseline (see Trampoline). It
// is non-allocatable and non-volatile.
static MOZ_CONSTEXPR_VAR Register BaselineFrameReg = r13;
static MOZ_CONSTEXPR_VAR Register BaselineStackReg = r1;

// ValueOperands R0, R1, and R2.
// R0 == JSReturnReg, and R2 uses registers not
// preserved across calls.  R1 value should be
// preserved across calls.
static MOZ_CONSTEXPR_VAR ValueOperand R0(r6,  r5);
static MOZ_CONSTEXPR_VAR ValueOperand R1(r15, r14);
static MOZ_CONSTEXPR_VAR ValueOperand R2(r4,  r3);

// BaselineTailCallReg and BaselineStubReg
// These use registers that are not preserved across calls.
static MOZ_CONSTEXPR_VAR Register ICTailCallReg = r8;
// Note that BaselineTailCallReg is actually just a link register alias,
// not truly LR like ARM or MIPS use. This is different from "fake LR"
// because the cross-platform JIT expects to address it like a regular GPR,
// and because we have no way of pushing LR in our JitCode "prologue."
static MOZ_CONSTEXPR_VAR Register ICStubReg = r7;

static MOZ_CONSTEXPR_VAR Register ExtractTemp0 = InvalidReg;
static MOZ_CONSTEXPR_VAR Register ExtractTemp1 = InvalidReg;


// FloatReg0 must be equal to ReturnFloatReg.
static MOZ_CONSTEXPR_VAR FloatRegister FloatReg0 = f1;
static MOZ_CONSTEXPR_VAR FloatRegister FloatReg1 = { FloatRegisters::f2 };

} // namespace jit
} // namespace js

#endif /* jit_ppc_BaselineRegisters_ppc_h */

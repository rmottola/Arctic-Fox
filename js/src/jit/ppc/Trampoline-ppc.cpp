/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=4 sw=4 et tw=99:
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*

   IonPower (C)2015 Contributors to TenFourFox. All rights reserved.
 
   Authors: Cameron Kaiser <classilla@floodgap.com>
   with thanks to Ben Stuhl and David Kilbridge 
   and the authors of the ARM and MIPS ports
 
 */

/* This file is largely specific to PowerOpen ABI. Some minor but non-trivial
   modifications are required for SysV. */

#include "jscompartment.h"

#include "jit/Bailouts.h"
#include "jit/JitFrames.h"
#include "jit/Linker.h"
#include "jit/JitCompartment.h"
#include "jit/JitSpewer.h"
#include "jit/osxppc/Bailouts-ppc.h"
#include "jit/osxppc/SharedICHelpers-ppc.h"
#include "jit/MacroAssembler-inl.h"
#ifdef JS_ION_PERF
# include "jit/PerfSpewer.h"
#endif
#include "jit/VMFunctions.h"

using namespace js;
using namespace js::jit;

// All registers to save and restore. This includes the stack pointer, since we
// use the ability to reference register values on the stack by index.
static const LiveRegisterSet AllRegs =
  LiveRegisterSet(GeneralRegisterSet(Registers::AllMask),
              FloatRegisterSet(FloatRegisters::AllMask));

// This is our *original* stack frame.
struct EnterJITStack
{
    void *savedSP;      // 0(r1)
    void *savedCR;
    void *savedLR;
    void *reserved0;
    void *reserved1;
    void *reserved2;    
    // Don't forget the argument area! (TenFourFox issue 179)
    void *arg0;         // 24(r1)
    void *arg1;
    void *arg2;
    void *arg3;
    void *arg4;
    void *arg5;
    void *arg6;
    void *arg7;         
    // Make the stack quadword aligned.
    void *padding0;     // 56(r1)

    // Non-volatile registers being saved for various purposes.
    void *savedR13;     // AsmJS and BaselineFrameReg
    void *savedR14;     // Baseline Compiler
    void *savedR15;     // Baseline Compiler
    void *savedR16;     // temporary stack for ABI calls
    void *savedR17;     // temporary stack for Trampoline
    void *savedR18;     // temporary LR for ABI calls
    // GPRs. We only save r19 through r25 inclusive to save some stack space.
    void *savedR19;     // 84(r1)
    void *savedR20;
    void *savedR21;
    void *savedR22;
    void *savedR23;
    void *savedR24;
    void *savedR25;
    // 112(r1)

    // We don't need to save any FPRs; we don't let the allocator use
    // any of the non-volatile ones.
};

// Utility code to give the compiler view of the size of the struct.
#if(0)
template<int s> struct Wow;
struct foo {
    int a,b;
};
Wow<sizeof(NativeObject)> wow;
#endif
JS_STATIC_ASSERT(sizeof(EnterJITStack) % 16 == 0);

/*
 * This method generates a trampoline for a C++ function with
 * the following signature using standard ABI calling convention (from
 * IonCompartment.h):

typedef JSBool(*EnterJitCode) (void *code, int argc, Value *argv,
                               StackFrame *fp,
                               CalleeToken calleeToken, JSObject *scopeChain,
                               size_t numStackValues, Value *vp);

 *
 */
JitCode *
JitRuntime::generateEnterJIT(JSContext *cx, EnterJitType type)
{
    // PowerOpen calling convention
    const Register reg_code  = r3;
    const Register reg_argc  = r4;
    const Register reg_argv  = r5;
    const Register reg_frame = r6;
    const Register reg_token = r7;
    const Register reg_scope = r8;
    const Register reg_nsv   = r9;
    const Register reg_vp    = r10; // Whew! Just fits!

    MOZ_ASSERT(OsrFrameReg == reg_frame);

    MacroAssembler masm(cx);
    Register sp = stackPointerRegister;
    Label epilogue;

	// ****
    // 1. Save non-volatile registers. These must be saved by the trampoline
    // here because they might be scanned by the conservative garbage
    // collector. This is essentially a standard PowerPC function prologue
    // except for the stack management, since IonPower is not ABI compliant.
    // ****

    // Keep a copy of sp; we need it to emit bogus stack frames for calls
    // requiring ABI compliance (it will live in r18 in just a moment).
    masm.x_mr(r12, sp);

    // Save LR and CR to the caller's linkage area.
    masm.x_mflr(r0);
    masm.stw(r0, sp, 8);
    masm.mfcr(r0);
    masm.stw(r0, sp, 4);

    // Now create the stack frame, in a fully ABI compliant manner so that
    // VMFunctions we call can walk the stack back.
    masm.stwu(sp, sp, -((uint16_t)sizeof(EnterJITStack)));

    // Dump the GPRs in the new frame. Emit an unrolled loop.
    // We don't need to save any FPRs.
    // XXX: We save more than we need, but that's probably a good thing.
    uint32_t j = 60;
    for (uint32_t i = 13; i < 26; i++) {
        masm.stw(Register::FromCode((Register::Code)i), sp, j);
        j+=4;
    }
    MOZ_ASSERT(j == sizeof(EnterJITStack));

    // Save sp to r18 so that we can restore it for ABI calls.
    masm.x_mr(r18, r12);

    // Registers saved. Prepare our internals.
    if (type == EnterJitBaseline) {
        // Set the baseline to the current stack pointer value so that
        // pushes "make sense."
        masm.x_mr(BaselineFrameReg, sp);
    }
    

	// ****
    // 2. Copy arguments from JS's buffer onto the stack so that JIT code
    // can access them. WARNING: we are no longer ABI compliant now.
    // ****
    
    // If we are constructing, the count also needs to include newTarget.
    MOZ_ASSERT(CalleeToken_FunctionConstructing == 0x01);
    masm.andi_rc(tempRegister, reg_token, CalleeToken_FunctionConstructing);
    masm.add(reg_argc, reg_argc, tempRegister);
    
    {
        Label top, bottom;
        masm.x_slwi(r17, reg_argc, 3); /* times 8 */
        // Do this here so that r17 is already "zero" if argc is zero. See
        // below.

        // If argc is already zero, skip.
        masm.cmplwi(reg_argc, 0);
        masm.x_beq(cr0, 
            10*4,
            Assembler::NotLikelyB, Assembler::DontLinkB); // forward branch not
            // likely to be taken (thus don't set Lk bit)

        // First, move argc into CTR.
        masm.x_mtctr(reg_argc);

        // Use reg_argc as a work register. We can't use r0, because addi
        // will wreck the code. We'll use r17 again in step 3, so we just
        // clobber reg_argc. (If argc was already zero, then r17 was already
        // cleared above.)
        masm.x_mr(reg_argc, r17);

        masm.bind(&top);

        // Now, copy from the argv pointer onto the stack with a tight bdnz
        // loop. The arguments are 64-bit, but the load could be misaligned,
        // so we do integer loads and stores. This will push the arguments
        // on in reverse order.
        masm.x_subi(reg_argc, reg_argc, 4); // We start out at +1, so sub first.
                    // As a nice side effect, this puts the low word on the
                    // stack first so that we don't have endian problems.
        masm.lwzx(r0, reg_argv, reg_argc);
        masm.x_subi(reg_argc, reg_argc, 4);
        masm.stwu(r0, sp, -4); // store to sp(-4) // not aliased on G5
        masm.lwzx(r0, reg_argv, reg_argc);
        masm.stwu(r0, sp, -4);
        masm.x_bdnz(-6*4, Assembler::NotLikelyB,
            Assembler::DontLinkB); // reverse branch, likely
            // to be taken (thus don't set Lk bit)
        
        masm.bind(&bottom);
    }

	// ****
    // 3. Now that the arguments are on the stack, create the Ion stack
    // frame. The OsrFrameReg is already reg_frame; we asserted such.
    // ****

    // Push number of actual arguments.
    // We don't need reg_argv anymore, so unbox to that.
    masm.unboxInt32(Address(reg_vp, 0), reg_argv);
    masm.push(reg_argv);
    // Push the callee token.
    masm.push(reg_token);
    // Create an Ion entry frame with r17, which still has the number of
    // bytes actually pushed. We don't bother with realigning the stack until
    // we actually have to call an ABI-compliant routine. Add two more words
    // for argv and token.
    masm.addi(r17, r17, 8);
    masm.makeFrameDescriptor(r17, JitFrame_Entry); // r17 is clobbered
    masm.push(r17); // frame descriptor with encoded argc

    // Save the value pointer in r17, which is now free and is non-volatile.
    // We will need it in the epilogue.
    masm.x_mr(r17, reg_vp);

    CodeLabel returnLabel;
    CodeLabel oomReturnLabel; // there can be only one, or something

    if (type == EnterJitBaseline) {
        // Handle OSR.
        ispew("--==-- OSR CASE --==--");

        // ... but only if we have to.
        masm.cmpwi(OsrFrameReg, 0);
        BufferOffset notOsr = masm._bc(0, Assembler::Equal);

		// First, write the return address. Rather than clobber LR, we just get
		// it from the label. Use r4 since it's free now.
		masm.ma_li32(r4, returnLabel.patchAt());
        masm.push2(r4, BaselineFrameReg);

        // Reserve a new frame and update the Baseline frame pointer.
        MOZ_ASSERT(BaselineFrame::Size() < 32767);
        masm.x_subi(sp, sp, BaselineFrame::Size());
        masm.x_mr(BaselineFrameReg, sp);

        // Reserve space for locals and stack values. These are doubles,
        // so multiply reg_nsv by 8. Use r12 since we need some immediates
        // and r0 won't work.
        masm.x_slwi(r12, reg_nsv, 3);
        masm.subf(sp, r12, sp);

        // Enter the exit frame. 
        MOZ_ASSERT((BaselineFrame::Size() + BaselineFrame::FramePointerOffset)
            < 32767);
        masm.addi(r12, r12,
            (BaselineFrame::Size() + BaselineFrame::FramePointerOffset));
        masm.makeFrameDescriptor(r12, JitFrame_BaselineJS);
        masm.x_li(r0, 0); // Fake return address
        masm.push2(r12, r0);
        masm.enterFakeExitFrame(ExitFrameLayoutBareToken);

        masm.push2(BaselineFrameReg, reg_code);

        /* Call the following:
           bool InitBaselineFrameForOsr(BaselineFrame *frame,
                StackFrame *interpFrame,
                uint32_t numStackValues) 
        */
        masm.setupUnalignedABICall(r3);
        masm.passABIArg(BaselineFrameReg); // this overwrites r3 (we saved it)
        masm.passABIArg(OsrFrameReg); // this overwrites r4 (don't care)
        masm.passABIArg(reg_nsv); // this overwrites r5 (don't care)
        masm.callWithABI(JS_FUNC_TO_DATA_PTR(void *,
            jit::InitBaselineFrameForOsr));
        
        // We have to keep the return code in r3, but we are confident pretty
        // much all of our other argregs were trashed, so just take r4 to
        // get reg_code back.
        masm.pop2(r4, BaselineFrameReg);

        // If successful, call the code.
        masm.x_mtctr(r4); // prefetch
        masm.cmpwi(r3, 0); // precompute
        masm.addi(sp, sp, ExitFrameLayout::SizeWithFooter());
        masm.addi(BaselineFrameReg, BaselineFrameReg, BaselineFrame::Size());
        BufferOffset error = masm._bc(0, Assembler::Equal);

        // If OSR-ing, then emit instrumentation for setting lastProfilerFrame
        // if profiler instrumentation is enabled.
        {
            Label skipProfilingInstrumentation;
            Register realFramePtr = r5; // it's been clobbered anyway
            Register scratch = r6; // ditto
            
            AbsoluteAddress addressOfEnabled(cx->runtime()->spsProfiler.addressOfEnabled());
            masm.branch32(Assembler::Equal, addressOfEnabled, Imm32(0),
                          &skipProfilingInstrumentation); // SHORT
            masm.addi(realFramePtr, BaselineFrameReg, sizeof(void*));
            masm.profilerEnterFrame(realFramePtr, scratch);
            masm.bind(&skipProfilingInstrumentation);
        }
        masm.bctr();

        // Otherwise fall through to the error path: initializing the frame
        // failed, probably OOM. Throw and terminate.
        masm.bindSS(error);
        masm.x_mr(sp, BaselineFrameReg);
        masm.moveValue(MagicValue(JS_ION_ERROR), JSReturnOperand);
        masm.addi(sp, sp, 2 * sizeof(uintptr_t));
        masm.ma_li32(tempRegister, oomReturnLabel.patchAt());
        masm.jump(tempRegister);

        // Otherwise, this is the much simpler non-OSR path.
        masm.bindSS(notOsr);
        // Load the scope chain into Baseline R1. This is already in an
        // argument register.
        MOZ_ASSERT(R1.scratchReg() != reg_code);
        masm.x_mr(R1.scratchReg(), reg_scope);
    }

	// ****
    // 4. Call the jitcode.
    // ****

#if(0)
	// Test trampoline (substitutes for callJit()).
    masm.x_li32(r5, 0);
    masm.x_li32(r6, JSVAL_TYPE_INT32);
#else
	// This pushes a computed return address, which is expected.
    masm.callJit(reg_code); // masm.ma_callIonHalfPush(reg_code);
#endif

	// OSR returns here.
	if (type == EnterJitBaseline) {
		masm.bind(returnLabel.target());
		masm.addCodeLabel(returnLabel);
		masm.bind(oomReturnLabel.target());
		masm.addCodeLabel(oomReturnLabel);
	}

	// ****
    // 5. Unwind the Ion stack and return to ABI compliance.
    // ****
    
    // The return address was already popped.
    // Pop off arguments by using the frame descriptor on the stack.
    masm.pop(r4); // since we know it's not in use
    masm.x_srwi(r4, r4, FRAMESIZE_SHIFT); // JitFrames.h: FRAMESIZE_SHIFT = 4;
    masm.add(sp, sp, r4);

	// ****
    // 6. Store the returned value from JSReturnOperand in the environment.
    // ****
    
    // We saved this in r17. This is a 64-bit jsval.
    masm.storeValue(JSReturnOperand, Address(r17, 0));

	// ****
    // 7. Standard PowerPC epilogue.
    // ****
    
    masm.bind(&epilogue);
    // Restore GPRs. (We have no FPRs to restore.)
    j -= 4; // otherwise r25 starts at 112!
    for (uint32_t i = 25; i > 12; i--) {
        masm.lwz(Register::FromCode(i), sp, j);
        j-=4;
    }
    MOZ_ASSERT(j == 56);

    // Tear down frame.
    masm.addi(sp, sp, sizeof(EnterJITStack));

    // Fetch original LR and CR from linkage area.
    masm.lwz(r0, sp, 8);
    masm.x_mtlr(r0);
    masm.lwz(r0, sp, 4);
    masm.x_mtcr(r0);

    // Return true.
    masm.x_li32(r3, 1);
    masm.blr();

    Linker linker(masm);
    AutoFlushICache afc("GenerateEnterJIT");
    return linker.newCode<NoGC>(cx, OTHER_CODE);
}

/* Ion only */
JitCode *
JitRuntime::generateInvalidator(JSContext *cx)
{
    MacroAssembler masm(cx);
    ispew("|||| generateInvalidator ||||");
    
    MOZ_ASSERT(sizeof(InvalidationBailoutStack) == (
    	sizeof(uintptr_t) * Registers::Total +
    	sizeof(double) * FloatRegisters::TotalPhys +
    	sizeof(uintptr_t) + // ionScript_
    	sizeof(uintptr_t))); // osiPointReturnAddress_

    // At this point, either of these two situations just happened:
    // 1) Execution has just returned from C code, which left the stack aligned
    // or
    // 2) Execution has just returned from Ion code, which left the stack
    // unaligned (and we need to realign it).
    //
    // We do the minimum amount of work in assembly and shunt the rest
    // off to InvalidationBailout.
	//
	// Normally we would pop the return address, but the invalidation epilogue just branches
	// to us, so there *is* no return address on the stack. Instead, just start writing.
	// Push registers such that we can access them from [base + code]. The script and
	// osiPoint should already be on stack.
	masm.PushRegsInMask(AllRegs);
	
    // Created InvalidationBailoutStack; hand it to InvalidationBailout().
    masm.x_mr(r3, stackPointerRegister);
    
    // Allocate and set pointers to retvals on stack.
    masm.x_subi(stackPointerRegister, stackPointerRegister, 2 * sizeof(uintptr_t));
    masm.addi(r4, stackPointerRegister, sizeof(uintptr_t)); // frameSize outparam
    masm.x_mr(r5, stackPointerRegister); // bailoutInfo outparam
    
    masm.setupUnalignedABICall(r0);
    masm.passABIArg(r3);
    masm.passABIArg(r4);
    masm.passABIArg(r5);
    masm.callWithABI(JS_FUNC_TO_DATA_PTR(void *, InvalidationBailout));

    // Get the outparams. (The return code is in r3, which is where the bailout tail
    // expects it to be.)
    masm.lwz(r5, stackPointerRegister, 0); // bailoutInfo
    masm.lwz(r4, stackPointerRegister, sizeof(uintptr_t)); // frameSize

    // Remove the return address, the IonScript, the register state
    // (InvaliationBailoutStack) and the space that was allocated for the
    // retvals.
    masm.addi(stackPointerRegister, stackPointerRegister,
        sizeof(InvalidationBailoutStack) + 2 * sizeof(uintptr_t));
    // Remove everything else (the frame size tells us how much).
    masm.add(stackPointerRegister, stackPointerRegister, r4);

    // Jump to shared bailout tail. The BailoutInfo pointer is already in r5
    // and the return code is already in r3.
    JitCode *bailoutTail = cx->runtime()->jitRuntime()->getBailoutTail();
    masm.branch(bailoutTail);

    Linker linker(masm);
    AutoFlushICache afc("Invalidator");
    JitCode *code = linker.newCode<NoGC>(cx, OTHER_CODE);
    JitSpew(JitSpew_IonInvalidate, "   invalidation thunk created at %p", (void *) code->raw());
    return code;
}

// If we have a function call where the callee has more formal arguments than
// the caller passes in, the JITs call the arguments-rectifier instead. It
// pushes some |undefined| values and re-pushes the other arguments on top of
// it (arguments are pushed last-to-first).
JitCode *
JitRuntime::generateArgumentsRectifier(JSContext *cx, void **returnAddrOut)
{
    MacroAssembler masm(cx);
    ispew("|||| generateArgumentsRectifier ||||");

    // ArgumentsRectifierReg contains the |nargs| pushed onto the current
    // frame. Including |this|, there are (|nargs| + 1) arguments to copy.
    // This is given to us when we are called by the code generator. ARM's
    // choice of r8 implies this should be a non-volatile register, but it
    // does allow it to be allocatable, so choose r19. ARM also freely
    // uses r0-r8 in this routine, so we're safe using r3-r8, even though
    // r8 is notionally our TailCallReg in Baseline.
    MOZ_ASSERT(ArgumentsRectifierReg == r19); // Assembler-ppc.h
    // Copy number of actual arguments. Keep this; we need it later.
    masm.load32(Address(stackPointerRegister,
        RectifierFrameLayout::offsetOfNumActualArgs()), r3);

    // Get the callee token and make r4 a pointer. It should be a JSFunction.
    masm.load32(Address(stackPointerRegister,
       RectifierFrameLayout::offsetOfCalleeToken()), r4);
    masm.x_li32(r12, CalleeTokenMask); // Can't use andi_rc.
    masm.and_(r12, r4, r12); // Keep r4 as the token.

    // Get nargs into r8 from the JSFunction for later. This is uint16_t.
    MOZ_ASSERT(JSFunction::offsetOfNargs() < 65536);
    masm.lhz(r8, r12, JSFunction::offsetOfNargs());

    // Finally, r5 = r8 - r19 (ARM ma_sub is z = x - y), yielding
    // the number of |undefined|s to push on the stack. Save this
    // for the below.
    masm.subf(r5, r19, r8);
    
    // Put that into CTR for later so we can free up r5.
    masm.x_mtctr(r5);

    // Get a pointer to the topmost argument in r5, which should be
    // sp (i.e., r12) + r19 * 8 + sizeof(RectifierFrameLayout). 
    // Use r6 and r7 as temporaries, since we'll overwrite them below.
    // Precompute whether we're constructing while we're doing that.
    MOZ_ASSERT(CalleeToken_FunctionConstructing == 0x01);
    masm.andi_rc(r0, r4, CalleeToken_FunctionConstructing);
    masm.x_slwi(r6, r19, 3);
    masm.cmplwi(r0, (uint32_t)CalleeToken_FunctionConstructing); // precompute
    masm.add(r7, r6, stackPointerRegister);
    MOZ_ASSERT(sizeof(RectifierFrameLayout) < 32768);
    masm.addi(r5, r7, sizeof(RectifierFrameLayout));
    // Keep r5! We need it for the copy operation!
    
    // Are we constructing? If so, we need to also add sizeof(Value) to nargs.
    {
        BufferOffset notConstructing = masm._bc(0, Assembler::NotEqual);
        
        // Move the value up on the stack.
        masm.addi(stackPointerRegister, stackPointerRegister, -sizeof(Value));
        masm.lwz(r7, r5, NUNBOX32_TYPE_OFFSET + sizeof(Value));
        masm.lwz(r6, r5, NUNBOX32_PAYLOAD_OFFSET + sizeof(Value));
        masm.stw(r7, stackPointerRegister, NUNBOX32_TYPE_OFFSET);
        masm.stw(r6, stackPointerRegister, NUNBOX32_PAYLOAD_OFFSET);
        
        // Increment nargs for the added argument we just pushed.
        masm.addi(r8, r8, 1);
        
        masm.bindSS(notConstructing);
    }

    // r6 = type, r7 = payload.
    // Using an fpreg for this doesn't really pay off for the number of
    // slots we typically end up pushing.
    masm.moveValue(UndefinedValue(), r6, r7);

    // Push as many |undefined| as we require. CTR is already loaded.
    {
        // This should be a single dispatch group, hopefully.
        masm.addi(stackPointerRegister, stackPointerRegister, -8);
        // Push payload first! We're big endian!
        masm.stw(r7, stackPointerRegister, 4);
        masm.stw(r6, stackPointerRegister, 0); // push2(r7,r6)
#if defined(_PPC970_)
        masm.x_nop();
        masm.x_bdnz(-4*4, Assembler::NotLikelyB,
            Assembler::DontLinkB); // reverse branch, likely
            // to be taken (thus don't set Lk bit)
#else
        masm.x_bdnz(-3*4, Assembler::NotLikelyB,
            Assembler::DontLinkB); // reverse branch, likely
            // to be taken (thus don't set Lk bit)
#endif
    }

    // Put the number of arguments pushed already + 1 into CTR (to count
    // |this|).
    // Everyone clobbers their rectifier register here, so we will too.
    masm.addi(r19, r19, 1);
    masm.x_mtctr(r19);

    // Push again (i.e., copy) from the position on the stack. Unfortunately,
    // we are almost certainly copying from a misaligned address and probably
    // to one as well, so we cannot use the FPU even though these are 64-bit.
    // r5 is still pointing to the top argument.
    {
        // ENDIAN!!!!
        masm.lwz(r0, r5, 4); // low word on first
        masm.stwu(r0, stackPointerRegister, -4);
        masm.lwz(r0, r5, 0);
        masm.x_subi(r5, r5, 8); // next double
        masm.stwu(r0, stackPointerRegister, -4);
        masm.x_bdnz(-5*4, Assembler::NotLikelyB,
            Assembler::DontLinkB); // reverse branch, likely
            // to be taken (thus don't set Lk bit)
    }

    // Turn nargs into number of bytes: (nargs + 1) * 8
    // This is still in r8 from before.
    // We may have increased it in the constructing block.
    masm.addi(r8, r8, 1);
    masm.x_slwi(r8, r8, 3);
    // Construct sizeDescriptor.
    masm.makeFrameDescriptor(r8, JitFrame_Rectifier);

    // Construct IonJSFrameLayout.
    masm.push3(r3, r4, r8); // nargs, calleeToken; frame descriptor on top

    // Call the target function.
    // Note that this code assumes the function is JITted, so we use
    // callJit.
    masm.andPtr(Imm32(CalleeTokenMask), r4);
    masm.load32(Address(r4, JSFunction::offsetOfNativeOrScript()), r3);
    masm.loadBaselineOrIonRaw(r3, r3, nullptr);
    uint32_t returnOffset = masm.callJitNoProfiler(r3); 
    
    // This is the current state of the stack upon return:
    // arg1
    //  ...
    // argN
    // num actual args
    // callee token
    // sizeDescriptor     <- sp now
    // return address

    // Unwind the rectifier frame using the descriptor.
    masm.lwz(r3, r1, 0);
    masm.addi(r1, r1, 12); // throw away the token and nargs
    MOZ_ASSERT(FRAMESIZE_SHIFT < 16);
    masm.x_srwi(r3, r3, FRAMESIZE_SHIFT);

    // arg1
    //  ...
    // argN               <- sp now
    // num actual args
    // callee token
    // sizeDescriptor
    // return address

    // Now, discard pushed arguments, and exit.
    masm.add(r1, r1, r3);
    masm.ret(); // NOT blr

    Linker linker(masm);
    AutoFlushICache afc("ArgumentsRectifier");
    JitCode *code = linker.newCode<NoGC>(cx, OTHER_CODE);

    if (returnAddrOut)
        *returnAddrOut = (void *) (code->raw() + returnOffset);

    ispew("^^^^ generateArgumentsRectifier ^^^^");
    return code;
}

// NOTE: Members snapshotOffset_ and padding_ of BailoutStack
// are not stored in PushBailoutFrame().
static const uint32_t bailoutDataSize = sizeof(BailoutStack) - 2 * sizeof(uintptr_t);
static const uint32_t bailoutInfoOutParamSize = 2 * sizeof(uintptr_t);

/* There are two different stack layouts when doing a bailout. They are
 * represented by the class BailoutStack.
 *
 * - The first case is when bailout goes through the bailout table. In this case
 * the table offset is stored in LR (look at JitRuntime::generateBailoutTable())
 * and the thunk code should save it on stack. In this case frameClassId_ cannot
 * be NO_FRAME_SIZE_CLASS_ID. Members snapshotOffset_ and padding_ are not on
 * the stack.
 *
 * - The second is when bailout is done via out of line code (lazy bailout).
 * In this case the frame size is stored in LR (look at
 * CodeGeneratorPPC::generateOutOfLineCode()) and the thunk code should save it
 * on stack. The other differences are snapshotOffset_ and padding_ are
 * pushed to the stack by CodeGeneratorPPC::visitOutOfLineBailout(), and
 * frameClassId_ is forced to be NO_FRAME_SIZE_CLASS_ID
 * (see JitRuntime::generateBailoutHandler).
 *
 */

/* Ion only */
static void
PushBailoutFrame(MacroAssembler &masm, uint32_t frameClass, Register spArg)
{
	// from Bailouts-ppc.h
    JS_STATIC_ASSERT(sizeof(BailoutStack) == (
    	sizeof(uintptr_t) + // padding_
    	sizeof(uintptr_t) + // snapshotOffset_
    	sizeof(uintptr_t) * Registers::Total +
    	sizeof(double) * FloatRegisters::TotalPhys +
		sizeof(uintptr_t) + // frameSize_ / tableOffset_ union
		sizeof(uintptr_t)));// frameClassId_    	
    MOZ_ASSERT(BailoutStack::offsetOfFrameSize() == 4);
    MOZ_ASSERT(BailoutStack::offsetOfFrameClass() == 0);

    // Continue constructing the BailoutStack (snapshotOffset_ and padding_
	// should already be present).
	//
	// First, save all registers, since the invalidator expects to examine them.
	masm.PushRegsInMask(AllRegs);
	
	// Get the framesize, which was stashed in LR.
	// See also JitRuntime::generateBailoutTable() and
	// CodeGeneratorPPC::generateOutOfLineCode().
    masm.x_mflr(tempRegister); // new dispatch group
    masm.x_li32(addressTempRegister, frameClass);

    // Add space for the framesize and frameclass.
    masm.addi(stackPointerRegister, stackPointerRegister, -8);
    masm.stw(tempRegister, stackPointerRegister, BailoutStack::offsetOfFrameSize());
    // Store the frame class.
    masm.stw(addressTempRegister, stackPointerRegister, BailoutStack::offsetOfFrameClass());
    
    // The pointer to this stack is now the first argument for Bailout().
    masm.x_mr(spArg, stackPointerRegister); // BailoutStack
}

/* Ion only */
static void
GenerateBailoutThunk(JSContext *cx, MacroAssembler &masm, uint32_t frameClass)
{
    PushBailoutFrame(masm, frameClass, r3);

    // Call the bailout function, passing a pointer to this (trivial) struct
    // we just constructed on the stack.
    masm.x_li32(tempRegister, 0); // "nullptr"
    masm.x_subi(stackPointerRegister, stackPointerRegister, bailoutInfoOutParamSize);
    masm.stw(tempRegister, stackPointerRegister, 0);
    masm.x_mr(r4, stackPointerRegister);

    masm.setupUnalignedABICall(r0);
    masm.passABIArg(r3);
    masm.passABIArg(r4);
    masm.callWithABI(JS_FUNC_TO_DATA_PTR(void *, Bailout));

    // Return value is on the stack. Leave r3 alone;
    // generateBailoutTail() uses it also.
    masm.lwz(r5, stackPointerRegister, 0);

    // Remove the BailoutInfo frame and the registers, and the topmost
    // Ion frame's stack.
	if (frameClass == NO_FRAME_SIZE_CLASS_ID) { // Frame has a tacky size
		// Load frameSize from the stack.
		masm.lwz(r4, stackPointerRegister,
			bailoutInfoOutParamSize + BailoutStack::offsetOfFrameSize());
		// Remove the complete class and the outparams.
		masm.addi(stackPointerRegister, stackPointerRegister,
			sizeof(BailoutStack) + bailoutInfoOutParamSize);
		// Finally, remove that frame.
		masm.add(stackPointerRegister, r4, stackPointerRegister);
	} else { // Frame has a classy size
		uint32_t frameSize = FrameSizeClass::FromClass(frameClass).frameSize();
		// Using the fixed framesize, remove everything.
		masm.addi(stackPointerRegister, stackPointerRegister,
			bailoutDataSize + bailoutInfoOutParamSize + frameSize);
	}

    // Jump to shared bailout tail. The BailoutInfo pointer is already in r5.
    JitCode *bailoutTail = cx->runtime()->jitRuntime()->getBailoutTail();
    masm.branch(bailoutTail);
}

/* Ion only */
JitCode *
JitRuntime::generateBailoutTable(JSContext *cx, uint32_t frameClass)
{
    MacroAssembler masm(cx);

    Label bailout;
    for (size_t i = 0; i < BAILOUT_TABLE_SIZE; i++) {
    	// This is just a string of bl instructions to the end of the table.
    	// LR becomes, thus, the offset to the bailout entry.
    	int32_t offset = (BAILOUT_TABLE_SIZE - i) * BAILOUT_TABLE_ENTRY_SIZE;
    	masm._b(offset, Assembler::RelativeBranch, Assembler::LinkB);
	}
    masm.bind(&bailout);

	// LR is saved in PushBailoutFrame().
    GenerateBailoutThunk(cx, masm, frameClass);

    Linker linker(masm);
    AutoFlushICache afc("BailoutTable");
    return linker.newCode<NoGC>(cx, OTHER_CODE);
}

/* Ion only */
/* "getGenericBailoutHandler()" */
JitCode *
JitRuntime::generateBailoutHandler(JSContext *cx)
{
    MacroAssembler masm(cx);
    GenerateBailoutThunk(cx, masm, NO_FRAME_SIZE_CLASS_ID);

    Linker linker(masm);
    AutoFlushICache afc("BailoutHandler");
    return linker.newCode<NoGC>(cx, OTHER_CODE);
}

/* The VMWrapper is considered JitCode and MUST be called with callJit, or
   there will be no PC for it to pop and return to. */

JitCode *
JitRuntime::generateVMWrapper(JSContext *cx, const VMFunction &f)
{
	uint32_t stackAdjust = 0;
    MOZ_ASSERT(functionWrappers_);
    MOZ_ASSERT(functionWrappers_->initialized());
    VMWrapperMap::AddPtr p = functionWrappers_->lookupForAdd(&f);
    if (p)
        return p->value();

    ispew("=== generateVMWrapper ===");

    // Generate a separated code for the wrapper.
    MacroAssembler masm(cx);
    GeneralRegisterSet regs = GeneralRegisterSet(Register::Codes::WrapperMask);

    // The Wrapper register set is a superset of the volatile register set.
    // Avoid conflicts with argument registers while discarding the result
    // after the function call.
    JS_STATIC_ASSERT((Register::Codes::VolatileMask &
        ~Register::Codes::WrapperMask) == 0);

    // We can try to make this a direct call with a quick bl iff the
    // target address can be expressed in 26 bits. We can't really do this
    // in an offset because we don't know this wrapper's final location. If
    // a long call, load the address into CTR here because we will need both
	// temp registers later, and the CPU can load it into cache at
	// its leisure (G5 especially).
    if ((uint32_t)f.wrapped & 0xfc000000) { // won't fit in 26 bits
	    masm.x_li32(tempRegister, (uint32_t)f.wrapped);
	    masm.x_mtctr(tempRegister);
    }
	
    // An exit frame descriptor (PC followed by a BaselineJS frame descriptor)
    // should be on top of the stack. Link it up and add the footer:
    //
    // linkExitFrame() (ours) -> put sp in compartment runtime ionTop
    // PushWithPatch(-1) | footer part 1
    // Push(f)           | footer part 2
    // loadJSContext
    Register cxreg = r3; // JSContext is always the first argument.
    masm.enterExitFrame(&f);
    masm.loadJSContext(cxreg);

    // Save the base of the argument set stored on the stack.
    // Use temp registers (which means we have to make sure we only emit
    // elemental instructions that won't clobber them), so we needn't take
    // them either.
    Register argsBase = InvalidReg;
    if (f.explicitArgs) {
        argsBase = r12;
        //regs.take(argsBase);
        masm.addi(argsBase, stackPointerRegister,
                ExitFrameLayout::SizeWithFooter());
    }

    // Reserve space for the outparameter.
    // TODO: Coalesce back-to-back stack reservations (but still use
    // reserveStack to make the coalesced reservation so that framePushed_
    // matches).
    Register outReg = InvalidReg;
    switch (f.outParam) {
      case Type_Value:
        outReg = r0;
        //regs.take(outReg);
        masm.reserveStack(sizeof(Value));
        masm.x_mr(outReg, stackPointerRegister);
        break;

      case Type_Handle:
        outReg = r0;
        //regs.take(outReg);
        // masm.PushEmptyRooted can clobber r12, so save it temporarily.
        if (f.explicitArgs && f.outParamRootType == VMFunction::RootValue)
            masm.x_mr(r4, argsBase); // it's not in use yet
        masm.PushEmptyRooted(f.outParamRootType);
        if (f.explicitArgs && f.outParamRootType == VMFunction::RootValue)
            masm.x_mr(argsBase, r4); // and it's still not
        masm.x_mr(outReg, stackPointerRegister);
        break;

      case Type_Int32:
      case Type_Pointer:
      case Type_Bool:
        outReg = r0;
        //regs.take(outReg);
        masm.reserveStack(sizeof(int32_t));
        masm.x_mr(outReg, stackPointerRegister);
        break;

      case Type_Double:
        outReg = r0;
        //regs.take(outReg);
        masm.reserveStack(sizeof(double));
        masm.x_mr(outReg, stackPointerRegister);
        break;

      default:
        MOZ_ASSERT(f.outParam == Type_Void);
        break;
    }

    Register temp = regs.getAny();
    masm.setupUnalignedABICall(temp);
    masm.passABIArg(cxreg);

    size_t argDisp = 0;
    JitSpew(JitSpew_Codegen, ">>> VMFn: %i args to copy", f.explicitArgs);
    MOZ_ASSERT(f.explicitArgs < 8);

    // Copy any arguments.
    for (uint32_t explicitArg = 0; explicitArg < f.explicitArgs; explicitArg++) {
        MoveOperand from;
        switch (f.argProperties(explicitArg)) {
          case VMFunction::WordByValue:
            ispew("VMFunction: argument by value");
            masm.passABIArg(MoveOperand(argsBase, argDisp), MoveOp::GENERAL);
            argDisp += sizeof(void *);
            break;
          case VMFunction::DoubleByValue:
            // Values should be passed by reference, not by value, so we
            // assert that the argument is a double-precision float.
            ispew("VMFunction: double by value");
            MOZ_ASSERT(f.argPassedInFloatReg(explicitArg));
            masm.passABIArg(MoveOperand(argsBase, argDisp), MoveOp::DOUBLE);
            argDisp += 2 * sizeof(void *);
            break;
          case VMFunction::WordByRef:
            ispew("VMFunction: arg by ref");
            masm.passABIArg(
                MoveOperand(argsBase, argDisp, MoveOperand::EFFECTIVE_ADDRESS),
                    MoveOp::GENERAL);
            argDisp += sizeof(void *);
            break;
          case VMFunction::DoubleByRef:
            ispew("VMFunction: double by ref");
            masm.passABIArg(
                MoveOperand(argsBase, argDisp, MoveOperand::EFFECTIVE_ADDRESS),
                    MoveOp::GENERAL);
            // Even though the register is word-sized, the stack uses a
            // 64-bit quantity.
            argDisp += 2 * sizeof(void *);
            break;
        }
    }

    // Copy the implicit outparam, if any.
    if (outReg != InvalidReg)
        masm.passABIArg(outReg);

	// Manually call the ABI (don't use callWithABI itself; the resolutions may
	// use r12 and it will overwrite it). CTR was already loaded way back when
    // if a long call is required.
    masm.callWithABIPre(&stackAdjust);
    if ((uint32_t)f.wrapped & 0xfc000000) { // won't fit in 26 bits
        masm.bctr(Assembler::LinkB);
    } else {
        masm._b((uint32_t)f.wrapped, Assembler::AbsoluteBranch,
            Assembler::LinkB);
    }
    masm.callWithABIPost(stackAdjust, MoveOp::GENERAL);

    // Test for failure, according to the various types.
    switch (f.failType()) {
      case Type_Object:
      case Type_Bool:
        // Called functions return bools, which are 0/false and non-zero/true.
        masm.branch32(Assembler::Equal, r3, Imm32(0),
            masm.failureLabel());
        break;
      default:
        MOZ_CRASH("unknown failure kind");
        break;
    }

    // Load the outparam and free any allocated stack.
    switch (f.outParam) {
      case Type_Handle:
        masm.popRooted(f.outParamRootType, ReturnReg, JSReturnOperand);
        break;

      case Type_Value:
        masm.loadValue(Address(stackPointerRegister, 0), JSReturnOperand);
        masm.freeStack(sizeof(Value));
        break;

      case Type_Int32:
      case Type_Pointer:
        masm.load32(Address(stackPointerRegister, 0), ReturnReg);
        masm.freeStack(sizeof(int32_t));
        break;

      case Type_Bool:
        // Boolean on PowerPC, natively, is a 32-bit word. Load that,
        // not just the first byte like ARM and x86 do (and in a nauseating
        // little endian fashion at that).
        masm.load32(Address(stackPointerRegister, 0), ReturnReg);
        masm.freeStack(sizeof(int32_t));
        break;

      case Type_Double:
        masm.loadDouble(Address(stackPointerRegister, 0), ReturnFloat32Reg);
        masm.freeStack(sizeof(double));
        break;

      default:
        MOZ_ASSERT(f.outParam == Type_Void);
        break;
    }

    masm.leaveExitFrame();
    // Exit.
    masm.retn(Imm32(sizeof(ExitFrameLayout) +
                    f.explicitStackSlots() * sizeof(void *) +
                    f.extraValuesToPop * sizeof(Value)));
    
    Linker linker(masm);
    AutoFlushICache afc("VMWrapper");
    JitCode *wrapper = linker.newCode<NoGC>(cx, OTHER_CODE);
    if (!wrapper)
        return NULL;

    // linker.newCode may trigger a GC and sweep functionWrappers_ so we have to
    // use relookupOrAdd instead of add.
    if (!functionWrappers_->relookupOrAdd(p, &f, wrapper))
        return NULL;

    return wrapper;
}

JitCode *
JitRuntime::generatePreBarrier(JSContext *cx, MIRType type)
{
    MacroAssembler masm(cx);
    ispew("|||| generatePreBarrier ||||");

    LiveRegisterSet save = LiveRegisterSet(GeneralRegisterSet(Registers::VolatileMask),
                                   FloatRegisterSet(FloatRegisters::VolatileMask));
    masm.PushRegsInMask(save);

    MOZ_ASSERT(PreBarrierReg == r4);
    masm.movePtr(ImmWord((uintptr_t)cx->runtime()), r3);

    masm.setupUnalignedABICall(r5);
    masm.passABIArg(r3);
    masm.passABIArg(r4);
    masm.callWithABI(IonMarkFunction(type));

    masm.PopRegsInMask(save);
    masm.ret();

    ispew("^^^^ generatePreBarrier ^^^^");

    Linker linker(masm);
    AutoFlushICache afc("PreBarrier");
    return linker.newCode<NoGC>(cx, OTHER_CODE);
}

typedef bool (*HandleDebugTrapFn)(
    JSContext *, BaselineFrame *, uint8_t *, bool *);
static const VMFunction HandleDebugTrapInfo =
    FunctionInfo<HandleDebugTrapFn>(HandleDebugTrap);

JitCode *
JitRuntime::generateDebugTrapHandler(JSContext *cx)
{
    MacroAssembler masm;
    ispew("|||| generateDebugTrapHandler ||||");

    Register scratch1 = r6;
    Register scratch2 = r5;
    Register scratch3 = r15;
    
    // Load the return address into r15. Here, and only here, is LR the
    // return address (because we call this from code we control). We'll
    // need it to push in several places since we entered without a return
    // address on the stack.
    masm.x_mflr(scratch3);

    // Load BaselineFrame pointer in scratch1.
    masm.x_mr(scratch1, BaselineFrameReg);
    masm.subPtr(Imm32(BaselineFrame::Size()), scratch1);

    // Enter a stub frame and call the HandleDebugTrap VM function. Ensure
    // the stub frame has a NULL ICStub pointer, since this pointer is marked
    // during GC. Also, push the return address here for HandleDebugTrap.
    masm.movePtr(ImmWord((uintptr_t)nullptr), ICStubReg);
    masm.push(scratch3);
    EmitBaselineEnterStubFrame(masm, scratch2);

    JitCode *code =
        cx->runtime()->jitRuntime()->getVMWrapper(HandleDebugTrapInfo);
    if (!code)
        return nullptr;

	// Push the modified frame and the return address one more time.
	masm.push2(scratch3, scratch1);
    EmitBaselineCallVM(code, masm);
    EmitBaselineLeaveStubFrame(masm);

    // If the stub returns |true|, we have to perform a forced return
    // (return from the JS frame). If the stub returns |false|, just return
    // from the trap stub so that execution continues where it left off.
    Label forcedReturn;
    masm.branchTest32(Assembler::NonZero, ReturnReg, ReturnReg, &forcedReturn);
    // HandleDebugTrap may have changed our return address since we called it,
    // so we pull LR from the stack. If there was no change, no harm, no foul.
    masm.lwz(tempRegister, stackPointerRegister, 0);
    masm.x_mtlr(tempRegister); // XXX mtctr?
    masm.addi(stackPointerRegister, stackPointerRegister, 4);
    masm.blr();

	// The forced return discards LR and returns using Ion ABI.
    masm.bind(&forcedReturn);
    masm.loadValue(Address(BaselineFrameReg,
                           BaselineFrame::reverseOffsetOfReturnValue()),
                   JSReturnOperand);
    masm.x_mr(r1, BaselineFrameReg);
    masm.pop(BaselineFrameReg);
    // Before returning, if profiling is turned on, make sure that lastProfilingFrame
    // is set to the correct caller frame.
    {
        Label skipProfilingInstrumentation;
        AbsoluteAddress addressOfEnabled(cx->runtime()->spsProfiler.addressOfEnabled());
        masm.branch32(Assembler::Equal, addressOfEnabled, Imm32(0), &skipProfilingInstrumentation);
        masm.profilerExitFrame();
        masm.bind(&skipProfilingInstrumentation);
    }
    masm.ret();

    ispew("^^^^ generateDebugTrapHandler ^^^^");

    Linker linker(masm);
    AutoFlushICache afc("DebugTrapHandler");
    return linker.newCode<NoGC>(cx, OTHER_CODE);
}

JitCode *
JitRuntime::generateExceptionTailStub(JSContext *cx, void *handler)
{
    MacroAssembler masm;

    masm.handleFailureWithHandlerTail(handler);

    Linker linker(masm);
    AutoFlushICache afc("ExceptionTailStub");
    JitCode *code = linker.newCode<NoGC>(cx, OTHER_CODE);

#ifdef JS_ION_PERF
    writePerfSpewerJitCodeProfile(code, "ExceptionTailStub");
#endif

    return code;
}

JitCode *
JitRuntime::generateBailoutTailStub(JSContext *cx)
{
    MacroAssembler masm;

    masm.generateBailoutTail(r4, r5);

    Linker linker(masm);
    AutoFlushICache afc("BailoutTailStub");
    JitCode *code = linker.newCode<NoGC>(cx, OTHER_CODE);

#ifdef JS_ION_PERF
    writePerfSpewerJitCodeProfile(code, "BailoutTailStub");
#endif

    return code;
}

JitCode*
JitRuntime::generateProfilerExitFrameTailStub(JSContext* cx)
{
/* XXX: This actually DOES get called, so it should probably be a target for optimization. */
   
	MacroAssembler masm;
	
    Register scratch1 = r7;
    Register scratch2 = r8;
    Register scratch3 = r9;
    Register scratch4 = r10;

    //
    // The code generated below expects that the current stack pointer points
    // to an Ion or Baseline frame, at the state it would be immediately
    // before a ret().  Thus, after this stub's business is done, it executes
    // a ret() and returns directly to the caller script, on behalf of the
    // callee script that jumped to this code.
    //
    // Thus the expected stack is:
    //
    //                                   StackPointer ----+
    //                                                    v
    // ..., ActualArgc, CalleeToken, Descriptor, ReturnAddr
    // MEM-HI                                       MEM-LOW
    //
    //
    // The generated jitcode is responsible for overwriting the
    // jitActivation->lastProfilingFrame field with a pointer to the previous
    // Ion or Baseline jit-frame that was pushed before this one. It is also
    // responsible for overwriting jitActivation->lastProfilingCallSite with
    // the return address into that frame.  The frame could either be an
    // immediate "caller" frame, or it could be a frame in a previous
    // JitActivation (if the current frame was entered from C++, and the C++
    // was entered by some caller jit-frame further down the stack).
    //
    // So this jitcode is responsible for "walking up" the jit stack, finding
    // the previous Ion or Baseline JS frame, and storing its address and the
    // return address into the appropriate fields on the current jitActivation.
    //
    // There are a fixed number of different path types that can lead to the
    // current frame, which is either a baseline or ion frame:
    //
    // <Baseline-Or-Ion>
    // ^
    // |
    // ^--- Ion
    // |
    // ^--- Baseline Stub <---- Baseline
    // |
    // ^--- Argument Rectifier
    // |    ^
    // |    |
    // |    ^--- Ion
    // |    |
    // |    ^--- Baseline Stub <---- Baseline
    // |
    // ^--- Entry Frame (From C++)
    //
    Register actReg = scratch4;
    AbsoluteAddress activationAddr(GetJitContext()->runtime->addressOfProfilingActivation());
    masm.loadPtr(activationAddr, actReg);

    Address lastProfilingFrame(actReg, JitActivation::offsetOfLastProfilingFrame());
    Address lastProfilingCallSite(actReg, JitActivation::offsetOfLastProfilingCallSite());

#ifdef DEBUG
    // Ensure that the frame we are exiting is the current lastProfilingFrame.
    {
        masm.loadPtr(lastProfilingFrame, scratch1);
        Label checkOk;
        masm.branchPtr(Assembler::Equal, scratch1, ImmWord(0), &checkOk);
        masm.branchPtr(Assembler::Equal, StackPointer, scratch1, &checkOk);
        masm.assumeUnreachable(
            "Mismatch between stored lastProfilingFrame and current stack pointer.");
        masm.bind(&checkOk); // SHORT (but not worth it)
    }
#endif

    // Load the frame descriptor and figure out what to do depending on its type.
    masm.loadPtr(Address(StackPointer, JitFrameLayout::offsetOfDescriptor()), scratch1);

    // Going into the conditionals, we will have:
    //      FrameDescriptor.size in scratch1
    //      FrameDescriptor.type in scratch2
    masm.ma_and(scratch2, scratch1, Imm32((1 << FRAMETYPE_BITS) - 1));
    masm.rshiftPtr(Imm32(FRAMESIZE_SHIFT), scratch1);

    // Handling of each case is dependent on FrameDescriptor.type
    BufferOffset handle_IonJS, handle_BaselineStub, handle_Rectifier,
        handle_IonAccessorIC, handle_Entry, end;

    handle_IonJS = masm._bc(0,
        masm.ma_cmp(scratch2, Imm32(JitFrame_IonJS), Assembler::Equal));
    handle_IonAccessorIC = masm._bc(0,
        masm.ma_cmp(scratch2, Imm32(JitFrame_IonAccessorIC), Assembler::Equal));
    handle_Rectifier = masm._bc(0,
        masm.ma_cmp(scratch2, Imm32(JitFrame_BaselineJS), Assembler::Equal));
    handle_BaselineStub = masm._bc(0,
        masm.ma_cmp(scratch2, Imm32(JitFrame_BaselineStub), Assembler::Equal));
    handle_Rectifier = masm._bc(0,
        masm.ma_cmp(scratch2, Imm32(JitFrame_Rectifier), Assembler::Equal));
    handle_Entry = masm._bc(0,
        masm.ma_cmp(scratch2, Imm32(JitFrame_Entry), Assembler::Equal));

    masm.x_trap(); // Oops.

    //
    // JitFrame_IonJS
    //
    // Stack layout:
    //                  ...
    //                  Ion-Descriptor
    //     Prev-FP ---> Ion-ReturnAddr
    //                  ... previous frame data ... |- Descriptor.Size
    //                  ... arguments ...           |
    //                  ActualArgc          |
    //                  CalleeToken         |- JitFrameLayout::Size()
    //                  Descriptor          |
    //        FP -----> ReturnAddr          |
    //
    masm.bindSS(handle_IonJS);
    {
        // |scratch1| contains Descriptor.size

        // returning directly to an IonJS frame.  Store return addr to frame
        // in lastProfilingCallSite.
        masm.loadPtr(Address(StackPointer, JitFrameLayout::offsetOfReturnAddress()), scratch2);
        masm.storePtr(scratch2, lastProfilingCallSite);

        // Store return frame in lastProfilingFrame.
        // scratch2 := StackPointer + Descriptor.size*1 + JitFrameLayout::Size();
        masm.add(scratch2, StackPointer, scratch1);
        masm.addi(scratch2, scratch2, JitFrameLayout::Size());
        masm.storePtr(scratch2, lastProfilingFrame);
        masm.ret();
    }

    //
    // JitFrame_BaselineStub
    //
    // Look past the stub and store the frame pointer to
    // the baselineJS frame prior to it.
    //
    // Stack layout:
    //              ...
    //              BL-Descriptor
    // Prev-FP ---> BL-ReturnAddr
    //      +-----> BL-PrevFramePointer
    //      |       ... BL-FrameData ...
    //      |       BLStub-Descriptor
    //      |       BLStub-ReturnAddr
    //      |       BLStub-StubPointer          |
    //      +------ BLStub-SavedFramePointer    |- Descriptor.Size
    //              ... arguments ...           |
    //              ActualArgc          |
    //              CalleeToken         |- JitFrameLayout::Size()
    //              Descriptor          |
    //    FP -----> ReturnAddr          |
    //
    // We take advantage of the fact that the stub frame saves the frame
    // pointer pointing to the baseline frame, so a bunch of calculation can
    // be avoided.
    //
    masm.bindSS(handle_BaselineStub);
    {
        masm.add(scratch3, StackPointer, scratch1);
        Address stubFrameReturnAddr(scratch3,
                                    JitFrameLayout::Size() +
                                    BaselineStubFrameLayout::offsetOfReturnAddress());
        masm.loadPtr(stubFrameReturnAddr, scratch2);
        masm.storePtr(scratch2, lastProfilingCallSite);

        Address stubFrameSavedFramePtr(scratch3,
                                       JitFrameLayout::Size() - (2 * sizeof(void*)));
        masm.loadPtr(stubFrameSavedFramePtr, scratch2);
        masm.addPtr(Imm32(sizeof(void*)), scratch2); // Skip past BL-PrevFramePtr
        masm.storePtr(scratch2, lastProfilingFrame);
        masm.ret();
    }


    //
    // JitFrame_Rectifier
    //
    // The rectifier frame can be preceded by either an IonJS or a
    // BaselineStub frame.
    //
    // Stack layout if caller of rectifier was Ion:
    //
    //              Ion-Descriptor
    //              Ion-ReturnAddr
    //              ... ion frame data ... |- Rect-Descriptor.Size
    //              < COMMON LAYOUT >
    //
    // Stack layout if caller of rectifier was Baseline:
    //
    //              BL-Descriptor
    // Prev-FP ---> BL-ReturnAddr
    //      +-----> BL-SavedFramePointer
    //      |       ... baseline frame data ...
    //      |       BLStub-Descriptor
    //      |       BLStub-ReturnAddr
    //      |       BLStub-StubPointer          |
    //      +------ BLStub-SavedFramePointer    |- Rect-Descriptor.Size
    //              ... args to rectifier ...   |
    //              < COMMON LAYOUT >
    //
    // Common stack layout:
    //
    //              ActualArgc          |
    //              CalleeToken         |- IonRectitiferFrameLayout::Size()
    //              Rect-Descriptor     |
    //              Rect-ReturnAddr     |
    //              ... rectifier data & args ... |- Descriptor.Size
    //              ActualArgc      |
    //              CalleeToken     |- JitFrameLayout::Size()
    //              Descriptor      |
    //    FP -----> ReturnAddr      |
    //
    masm.bindSS(handle_Rectifier);
    {
        // scratch2 := StackPointer + Descriptor.size*1 + JitFrameLayout::Size();
        masm.add(scratch2, StackPointer, scratch1);
        masm.add32(Imm32(JitFrameLayout::Size()), scratch2);
        masm.loadPtr(Address(scratch2, RectifierFrameLayout::offsetOfDescriptor()), scratch3);
        masm.x_srwi(scratch1, scratch3, FRAMESIZE_SHIFT);
        masm.and32(Imm32((1 << FRAMETYPE_BITS) - 1), scratch3);

        // Now |scratch1| contains Rect-Descriptor.Size
        // and |scratch2| points to Rectifier frame
        // and |scratch3| contains Rect-Descriptor.Type

        // Check for either Ion or BaselineStub frame.
        Label handle_Rectifier_BaselineStub;
        masm.branch32(Assembler::NotEqual, scratch3, Imm32(JitFrame_IonJS),
                      &handle_Rectifier_BaselineStub);

        // Handle Rectifier <- IonJS
        // scratch3 := RectFrame[ReturnAddr]
        masm.loadPtr(Address(scratch2, RectifierFrameLayout::offsetOfReturnAddress()), scratch3);
        masm.storePtr(scratch3, lastProfilingCallSite);

        // scratch3 := RectFrame + Rect-Descriptor.Size + RectifierFrameLayout::Size()
        masm.add(scratch3, scratch2, scratch1);
        masm.add32(Imm32(RectifierFrameLayout::Size()), scratch3);
        masm.storePtr(scratch3, lastProfilingFrame);
        masm.ret();

        // Handle Rectifier <- BaselineStub <- BaselineJS
        masm.bind(&handle_Rectifier_BaselineStub);
#ifdef DEBUG
        {
            Label checkOk;
            masm.branch32(Assembler::Equal, scratch3, Imm32(JitFrame_BaselineStub), &checkOk);
            masm.assumeUnreachable("Unrecognized frame preceding baselineStub.");
            masm.bind(&checkOk);
        }
#endif
        masm.add(scratch3, scratch2, scratch1);
        Address stubFrameReturnAddr(scratch3, RectifierFrameLayout::Size() +
                                              BaselineStubFrameLayout::offsetOfReturnAddress());
        masm.loadPtr(stubFrameReturnAddr, scratch2);
        masm.storePtr(scratch2, lastProfilingCallSite);

        Address stubFrameSavedFramePtr(scratch3,
                                       RectifierFrameLayout::Size() - (2 * sizeof(void*)));
        masm.loadPtr(stubFrameSavedFramePtr, scratch2);
        masm.addPtr(Imm32(sizeof(void*)), scratch2);
        masm.storePtr(scratch2, lastProfilingFrame);
        masm.ret();
    }

    // JitFrame_IonAccessorIC
    //
    // The caller is always an IonJS frame.
    //
    //              Ion-Descriptor
    //              Ion-ReturnAddr
    //              ... ion frame data ... |- AccFrame-Descriptor.Size
    //              StubCode             |
    //              AccFrame-Descriptor  |- IonAccessorICFrameLayout::Size()
    //              AccFrame-ReturnAddr  |
    //              ... accessor frame data & args ... |- Descriptor.Size
    //              ActualArgc      |
    //              CalleeToken     |- JitFrameLayout::Size()
    //              Descriptor      |
    //    FP -----> ReturnAddr      |
    masm.bindSS(handle_IonAccessorIC);
    {
        // scratch2 := StackPointer + Descriptor.size + JitFrameLayout::Size()
        masm.add(scratch2, StackPointer, scratch1);
        masm.addPtr(Imm32(JitFrameLayout::Size()), scratch2);

        // scratch3 := AccFrame-Descriptor.Size
        masm.loadPtr(Address(scratch2, IonAccessorICFrameLayout::offsetOfDescriptor()), scratch3);
#ifdef DEBUG
        // Assert previous frame is an IonJS frame.
        masm.movePtr(scratch3, scratch1);
        masm.and32(Imm32((1 << FRAMETYPE_BITS) - 1), scratch1);
        {
            Label checkOk;
            masm.branch32(Assembler::Equal, scratch1, Imm32(JitFrame_IonJS), &checkOk);
            masm.assumeUnreachable("IonAccessorIC frame must be preceded by IonJS frame");
            masm.bind(&checkOk);
        }
#endif
        masm.rshiftPtr(Imm32(FRAMESIZE_SHIFT), scratch3);

        // lastProfilingCallSite := AccFrame-ReturnAddr
        masm.loadPtr(Address(scratch2, IonAccessorICFrameLayout::offsetOfReturnAddress()), scratch1);
        masm.storePtr(scratch1, lastProfilingCallSite);

        // lastProfilingFrame := AccessorFrame + AccFrame-Descriptor.Size +
        //                       IonAccessorICFrameLayout::Size()
        masm.add(scratch1, scratch2, scratch3);
        masm.addPtr(Imm32(IonAccessorICFrameLayout::Size()), scratch1);
        masm.storePtr(scratch1, lastProfilingFrame);
        masm.ret();
    }

    //
    // JitFrame_Entry
    //
    // If at an entry frame, store null into both fields.
    //
    masm.bindSS(handle_Entry);
    {
        masm.movePtr(ImmPtr(nullptr), scratch1);
        masm.storePtr(scratch1, lastProfilingCallSite);
        masm.storePtr(scratch1, lastProfilingFrame);
        masm.ret();
    }

    Linker linker(masm);
    AutoFlushICache afc("ProfilerExitFrameTailStub");
    JitCode* code = linker.newCode<NoGC>(cx, OTHER_CODE);

#ifdef JS_ION_PERF
    writePerfSpewerJitCodeProfile(code, "ProfilerExitFrameStub");
#endif

    return code;
}

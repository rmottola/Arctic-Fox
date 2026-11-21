/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/osxppc/CodeGenerator-ppc.h"

#include "mozilla/MathAlgorithms.h"

#include "jscntxt.h"
#include "jscompartment.h"
#include "jsnum.h"

#include "jit/CodeGenerator.h"
#include "jit/JitFrames.h"
#include "jit/JitCompartment.h"
#include "jit/MIR.h"
#include "jit/MIRGraph.h"
#include "vm/Shape.h"
#include "vm/TraceLogging.h"

#include "jsscriptinlines.h"

#include "jit/MacroAssembler-inl.h"
#include "jit/shared/CodeGenerator-shared-inl.h"

using namespace js;
using namespace js::jit;

using mozilla::FloorLog2;
using mozilla::NegativeInfinity;
using JS::GenericNaN;

#if DEBUG

/* Useful class to print visual guard blocks. */
class AutoDeBlock
{
    private:
        const char *blockname;

    public:
        AutoDeBlock(const char *name) {
            blockname = name;
            JitSpew(JitSpew_Codegen, ">>>>>>>> CGPPC: %s", blockname);
        }

        ~AutoDeBlock() {
            JitSpew(JitSpew_Codegen, "         CGPPC: %s <<<<<<<<", blockname);
        }
};

#define ADBlock(x)  AutoDeBlock _adbx(x)
            
#else

/* Useful preprocessor macros to completely elide visual guard blocks. */

#define ADBlock(x)  ;

#endif

// shared
CodeGeneratorPPC::CodeGeneratorPPC(MIRGenerator *gen, LIRGraph *graph, MacroAssembler *masm)
  : CodeGeneratorShared(gen, graph, masm)
{
}

bool
CodeGeneratorPPC::generatePrologue()
{
    ADBlock("generatePrologue");
    MOZ_ASSERT(masm.framePushed() == 0);	
    MOZ_ASSERT(!gen->compilingAsmJS());

    // If profiling, save the current frame pointer to a per-thread global field.
    if (isProfilerInstrumentationEnabled())
        masm.profilerEnterFrame(StackPointer, CallTempReg0);

    // Note that this automatically sets MacroAssembler::framePushed().
    masm.reserveStack(frameSize());
    masm.checkStackAlignment();

    emitTracelogIonStart();

    return true;
}

bool
CodeGeneratorPPC::generateEpilogue()
{
    ADBlock("generateEpilogue");
	
    MOZ_ASSERT(!gen->compilingAsmJS());
    masm.bind(&returnLabel_);
    
    emitTracelogIonStop();

    if (!isProfilerInstrumentationEnabled()) {
        // We have a faster option.

        masm.lwz(tempRegister, stackPointerRegister, frameSize());
        masm.x_mtctr(tempRegister);
        // The reason for the two addis here is to keep framePushed() from
        // getting out of whack. We'd burn the extra dispatch slots anyway.
        masm.freeStack(frameSize());
        MOZ_ASSERT(masm.framePushed() == 0);
        masm.addi(stackPointerRegister, stackPointerRegister, 4);
        masm.bctr();
        return true;
    }
    // Fallback to slower.

    masm.freeStack(frameSize());
    MOZ_ASSERT(masm.framePushed() == 0);

    // If profiling, reset the per-thread global lastJitFrame to point to
    // the previous frame.
    if (isProfilerInstrumentationEnabled())
        masm.profilerExitFrame();

    masm.ret();
    return true;
}

void
CodeGeneratorPPC::branchToBlock(FloatRegister lhs, FloatRegister rhs,
                                 MBasicBlock *mir, Assembler::DoubleCondition cond)
{
    ADBlock("branchToBlock-float");

    // Skip past trivial blocks.
    mir = skipTrivialBlocks(mir);

    Label *label = mir->lir()->label();
    if (Label *oolEntry = labelForBackedgeWithImplicitCheck(mir)) {
        // Note: the backedge is initially a jump to the next instruction.
        // It will be patched to the target block's label during link().
        RepatchLabel rejoin;

        CodeOffsetJump backedge;
        BufferOffset skip = masm._bc(0,
            masm.ma_fcmp(lhs, rhs, Assembler::InvertCondition(cond)));
        backedge = masm.backedgeJump(&rejoin);
        masm.bind(&rejoin);
        masm.bindSS(skip);

        if (!patchableBackedges_.append(PatchableBackedgeInfo(backedge, label, oolEntry)))
            MOZ_CRASH();
    } else {
        masm.branchDouble(cond, lhs, rhs, mir->lir()->label());
    }
}

void
OutOfLineBailout::accept(CodeGeneratorPPC *codegen)
{
    codegen->visitOutOfLineBailout(this);
}

void
CodeGeneratorPPC::visitTestIAndBranch(LTestIAndBranch *test)
{
    ADBlock("visitTestIAndBranch");
	
    const LAllocation *opd = test->getOperand(0);
    MBasicBlock *ifTrue = test->ifTrue();
    MBasicBlock *ifFalse = test->ifFalse();

    emitBranch(ToRegister(opd), Imm32(0), Assembler::NonZero, ifTrue, ifFalse);
}

void
CodeGeneratorPPC::visitCompare(LCompare *comp)
{
    ADBlock("visitCompare");
	
    Assembler::Condition cond = JSOpToCondition(comp->mir()->compareType(), comp->jsop());
    const LAllocation *left = comp->getOperand(0);
    const LAllocation *right = comp->getOperand(1);
    const LDefinition *def = comp->getDef(0);

    if (right->isConstant())
        masm.cmp32Set(cond, ToRegister(left), Imm32(ToInt32(right)), ToRegister(def));
    else if (right->isGeneralReg())
        masm.cmp32Set(cond, ToRegister(left), ToRegister(right), ToRegister(def));
    else
        masm.cmp32Set(cond, ToRegister(left), ToAddress(right), ToRegister(def));
}

void
CodeGeneratorPPC::visitCompareAndBranch(LCompareAndBranch *comp)
{
    ADBlock("visitCompareAndBranch");
	
    Assembler::Condition cond = JSOpToCondition(comp->cmpMir()->compareType(), comp->jsop());
    if (comp->right()->isConstant()) {
        emitBranch(ToRegister(comp->left()), Imm32(ToInt32(comp->right())), cond,
                   comp->ifTrue(), comp->ifFalse());
    } else if (comp->right()->isGeneralReg()) {
        Register lhs = ToRegister(comp->left());
        Register rhs = ToRegister(comp->right());
        if (lhs != rhs) {
            emitBranch(lhs, rhs, cond, comp->ifTrue(), comp->ifFalse());
        } else {
            // We have been asked to compare a register to itself, which
            // can occur in idiomatic code like (x !== x) where NaN would be
            // expected to be true. However, if we get here, type inference
            // has determined this is an integer, so this can't happen and
            // can cause all kinds of weirdness. We use the JSOp instead of
            // the computed condition so that we know exactly the operation
            // the compiler wants to accomplish. If this is EQ or STRICTEQ,
            // branch to truth. Everything else branches to falseth.
            //
            // See also visitCompareVAndBranch for the ValueOperand situation.
            JSOp what = comp->jsop();
            if (what == JSOP_EQ || what == JSOP_STRICTEQ) {
                if (!isNextBlock(comp->ifTrue()->lir())) {
                    ispew("!!>> optimized GPR branch EQ lhs == rhs <<!!");
                    // Don't branch directly. Let jumpToBlock handle loop
                    // headers and such, which are considered non-trivial.
                    jumpToBlock(comp->ifTrue());
                } else 
                    ispew("!!>> elided GPR branch EQ lhs == rhs <<!!");
            } else {
                if (!isNextBlock(comp->ifFalse()->lir())) {
                    ispew("!!>> optimized GPR branch NE lhs == rhs <<!!");
                    jumpToBlock(comp->ifFalse());
                } else
                    ispew("!!>> elided GPR branch NE lhs == rhs <<!!");
            }
        }
    } else {
        emitBranch(ToRegister(comp->left()), ToAddress(comp->right()), cond,
                   comp->ifTrue(), comp->ifFalse());
    }
}

bool
CodeGeneratorPPC::generateOutOfLineCode()
{
    ADBlock("generateOutOfLineCode");
    if (!CodeGeneratorShared::generateOutOfLineCode())
        return false;

    if (deoptLabel_.used()) {
        // All non-table-based bailouts will go here.
        masm.bind(&deoptLabel_);

	// The snapshot number is on the stack from ::visitOutOfLineBailout.
        // Load the frame size into LR so the handler can recover the IonScript.
        // We have to use LR because generateBailoutTable is based on linked
        // branch instructions. (Don't push like x86 does.)
        masm.x_li32(tempRegister, frameSize());
        masm.x_mtlr(tempRegister);

        // This will call either GenerateBailoutThunk or GenerateParallelBailoutThunk
        // in the Trampoline.
        JitCode *handler = gen->jitRuntime()->getGenericBailoutHandler();
        masm.branch(handler);
    }
    return !masm.oom();
}

void
CodeGeneratorPPC::bailoutFrom(Label *label, LSnapshot *snapshot)
{
    ADBlock("bailoutFrom");
	
    if (masm.bailed())
        return;

    MOZ_ASSERT_IF(!masm.oom(), label->used());
    MOZ_ASSERT_IF(!masm.oom(), !label->bound());
    encode(snapshot); // infallible

    // Though the assembler doesn't track all frame pushes, at least make sure
    // the known value makes sense. We can't use bailout tables if the stack
    // isn't properly aligned to the static frame size.
    MOZ_ASSERT_IF(frameClass_ != FrameSizeClass::None(),
                  frameClass_.frameSize() == masm.framePushed());

    // We don't use table bailouts because retargeting is easier this way.
    InlineScriptTree *tree = snapshot->mir()->block()->trackedTree();
    OutOfLineBailout *ool = new(alloc()) OutOfLineBailout(snapshot, masm.framePushed());
    addOutOfLineCode(ool, new(alloc()) BytecodeSite(tree, tree->script()->code())); // infallible

    masm.retarget(label, ool->entry());
}

template <typename T1, typename T2>
void
CodeGeneratorPPC::bailoutCmp32(Assembler::Condition c, T1 lhs, T2 rhs, LSnapshot *snapshot) {
#if DEBUG
	uint32_t curr = masm.currentOffset();
#endif
	// This branch is (or should be) always short.

        BufferOffset skip = masm._bc(0,
            masm.ma_cmp(lhs, rhs, Assembler::InvertCondition(c)),
            cr0, Assembler::LikelyB);
        bailout(snapshot);
#if DEBUG
	MOZ_ASSERT((masm.currentOffset() - curr) < 12288); // paranoia
#endif
        masm.bindSS(skip);
}
template void CodeGeneratorPPC::bailoutCmp32<Address, Register> 
(Assembler::Condition c, Address lhs, Register rhs, LSnapshot *snapshot);
template void CodeGeneratorPPC::bailoutCmp32<Address, Imm32> 
(Assembler::Condition c, Address lhs, Imm32 rhs, LSnapshot *snapshot);
template void CodeGeneratorPPC::bailoutCmp32<Register, ImmPtr> 
(Assembler::Condition c, Register lhs, ImmPtr rhs, LSnapshot *snapshot);
template void CodeGeneratorPPC::bailoutCmp32<Register, Register>
(Assembler::Condition c, Register lhs, Register rhs, LSnapshot *snapshot);

void
CodeGeneratorPPC::bailout(LSnapshot *snapshot)
{
    ADBlock("bailout");
	
    Label label;
    masm.jump(&label);
    bailoutFrom(&label, snapshot);
}

void
CodeGeneratorPPC::visitOutOfLineBailout(OutOfLineBailout *ool)
{
    ADBlock("visitOutOfLineBailout");
	
    // Push snapshotOffset and add a word of padding (XXX: do we need this?).
    // This is the snapshotOffset_ and padding_ in BailoutStack (Bailouts-ppc.h).
    masm.subPtr(Imm32(2 * sizeof(void *)), StackPointer);
    masm.storePtr(ImmWord(ool->snapshot()->snapshotOffset()), Address(StackPointer, 0));

    // ::generateOutOfLineCode()
    masm.jump(&deoptLabel_);
}

// Common code for visitMinMaxD/F, using fsel, because PowerPC is awesome and
// it saves us a branch or six.
//
// You'll notice this pattern occur a lot for D/F pairs.
// Since stfs/lfs automatically convert to and from single precision, there is no
// good reason to use the single precision instructions, so we just use a unified
// routine in most cases. Unfortunately, Ion often expects to use a single
// precision result immediately without further conversion or storage -- see MathF.
static void
FPRMinMax(FloatRegister first, FloatRegister second, FloatRegister output, MacroAssembler &masm, bool isMax)
{
    // If first or second is NaN, the result is NaN.
    masm.fcmpu(first, second);
    BufferOffset nan = masm._bc(0, Assembler::DoubleUnordered);
    // If first and second are equal, we need a zero check, because fsel does not
    // distinguish between -0 and 0.
    BufferOffset equal = masm._bc(0, Assembler::Equal);
	
    // Compute the difference between first and second, according to
    // the sense.
    MOZ_ASSERT(first != fpTempRegister);
    MOZ_ASSERT(second != fpTempRegister);
    if (isMax)
        masm.fsub(fpTempRegister, first, second); // temp = first - second
    else
        masm.fsub(fpTempRegister, second, first);
	
    // fsel returns first if fpTempRegister is >= 0, and second if less.
    masm.fsel(output, fpTempRegister, first, second);
    BufferOffset done = masm._b(0);
	
    // NaN. Load NaN.
    masm.bindSS(nan);
    masm.loadConstantDouble(GenericNaN(), output);
    BufferOffset done2 = masm._b(0);
	
    // Equality. First check for zero.
    MOZ_ASSERT(first == output);
    masm.bindSS(equal);
   // Since we know that they both must be ordered, get 0 from an fsub.
    masm.fsub(fpTempRegister, first, first);
    masm.fcmpu(first, fpTempRegister);
    // If first is non-zero, then second must also be, so leave output == first.
    BufferOffset done3 = masm._bc(0, Assembler::DoubleNotEqualOrUnordered);
    // So now both operands are either -0 or 0.
    if (isMax) {
        // -0 + -0 = -0 and -0 + 0 = 0.
        masm.addDouble(second, first);
    } else {
        masm.negateDouble(first);
        masm.subDouble(second, first);
        masm.negateDouble(first);
    }
    
    masm.bindSS(done);
    masm.bindSS(done2);
    masm.bindSS(done3);
}

void
CodeGeneratorPPC::visitMinMaxD(LMinMaxD *ins)
{
    ADBlock("visitMinMaxD");
    FloatRegister first = ToFloatRegister(ins->first());
    FloatRegister second = ToFloatRegister(ins->second());
    FloatRegister output = ToFloatRegister(ins->output());
    
    FPRMinMax(first, second, output, masm, ins->mir()->isMax());
}
void
CodeGeneratorPPC::visitMinMaxF(LMinMaxF *ins)
{
    ADBlock("visitMinMaxF");
    FloatRegister first = ToFloatRegister(ins->first());
    FloatRegister second = ToFloatRegister(ins->second());
    FloatRegister output = ToFloatRegister(ins->output());
    
    FPRMinMax(first, second, output, masm, ins->mir()->isMax());
}

// Common code for visitAbsD/F.
static void
FPRAbs(FloatRegister input, FloatRegister output, MacroAssembler &masm)
{
    masm.fabs(output, input);
}

void
CodeGeneratorPPC::visitAbsD(LAbsD *ins)
{
    ADBlock("visitAbsD");
    FPRAbs(ToFloatRegister(ins->input()), ToFloatRegister(ins->output()), masm);
}
void
CodeGeneratorPPC::visitAbsF(LAbsF *ins)
{
    ADBlock("visitAbsF");
    FPRAbs(ToFloatRegister(ins->input()), ToFloatRegister(ins->output()), masm);
}

// Common code for visitSqrtD/F and visitPowHalfD.
// input can == output.
static void
FPRSqrt(MacroAssembler &masm, FloatRegister src, FloatRegister dest) {
#ifdef _PPC970_
#warning using native POWER4/970 square root
	masm.fsqrt(dest, src);
#else
#warning using software square root
    // This is David Kilbridge's Newton-Raphson iterative square root
    // algorithm based on the reciprocal square root instruction on
    // Apple G3s and G4s. Not all PPC cores have this instruction.
    FloatRegister fpTemp2 = f3;
    FloatRegister fpTemp3 = f2;

    // Clear relevant bits in the FPSCR. mtfsfi wouldn't save much here.
    // NB: gdb disassembler munges these oddly, but they are correct.
    masm.mtfsb0(0); // fx
    masm.mtfsb0(5); // zx
    masm.mtfsb0(7); // vxsnan
    masm.mtfsb0(22); // vxsqrt
    masm.mtfsb0(30); // set round to nearest
    masm.mtfsb0(31); //  "    "    "    "

    // Temporarily make src the "answer."
    masm.fmr(dest, src);
    // Compute the reciprocal and load flags to CR1: z =~ 1/sqrt(src)
    masm.frsqrte_rc(fpTempRegister, src);

    // If no new exception, commence the spanking. Er, the iterations.
    // No, it's too perilous. But it *is* Likely. And then comes the ...
    // bc+ 4, 4, @0
    BufferOffset b0 = masm._bc(0, Assembler::GreaterThanOrEqual, cr1,
        Assembler::LikelyB);

    // Exception. Get ZX bit to see if we wound up dividing by zero.
    masm.mcrfs(cr7, 1);
    // If ZX is true, return zero (jump to the end), because sqrt(0)==0
    // and our reciprocal just tried to divide by zero, so src must be
    // zero.
    // bc 12, 29, @3
    BufferOffset b3 = masm._bc(0, Assembler::GreaterThan, cr7);
    // ZX was not. Return the unordered result from fpTempRegister. src
    // was negative or NaN; the answer is unordered.
    // b @2
    BufferOffset b2 = masm._b(0);

    masm.bindSS(b0); // @0
    // It is not an exception to get the sqrt of +- Infinity, which is
    // itself. Don't iterate on that (check FPRF/FE bit first).
    masm.mcrfs(cr7, 4);
    // If set, return src.
    // bc 12, 30, @3
    BufferOffset bb3 = masm._bc(0, Assembler::Equal, cr7);

    // Iterations now required. First, load constant for N-R iterations.
    masm.x_lis(tempRegister, 0x3F00); // 0.5 = 0x3f000000
    masm.stwu(tempRegister, stackPointerRegister, -4);
    masm.lfs(fpTemp3, stackPointerRegister, 0);
    masm.addi(stackPointerRegister, stackPointerRegister, 4);

    // x = x/2 for the following
    masm.fmul(dest, src, fpTemp3); // 4

    // Do three iterations converging to 1/sqrt(x):
    // compute z^2 => T1
    // 1/2 - T1 * x (which is x/2) => T2
    // z + z * T2 => new z
    // It's time to kick ass with fused multiply add. YEE HAW.
#define NRIT    masm.fmul(fpTemp2, fpTempRegister, fpTempRegister); \
                masm.fnmsub(fpTemp2, fpTemp2, dest, fpTemp3); \
                masm.fmadd(fpTempRegister, fpTempRegister, fpTemp2, \
                    fpTempRegister);

    NRIT
    NRIT
    NRIT
#undef NRIT

    // The final iteration gets the last bit correct.
    masm.fadd(dest, dest, dest); // turn x/2 back into x
    masm.fmul(fpTemp2, fpTempRegister, dest); // y = z * x
    masm.fnmsub(dest, fpTemp2, fpTemp2, dest); // x - y^2
    masm.fmul(dest, dest, fpTemp3); // divided by 2

    // Finally: result = y + ((x - y^2)/2) * z
    masm.fmadd(fpTempRegister, dest, fpTempRegister, fpTemp2);
    // Final result is in fpTempRegister.

    // @2
    masm.bindSS(b2);
    masm.fmr(dest, fpTempRegister);

    // @3
    masm.bindSS(b3);
    masm.bindSS(bb3);
#endif
}

void
CodeGeneratorPPC::visitSqrtD(LSqrtD *ins)
{
    ADBlock("visitSqrtD");
    FPRSqrt(masm, ToFloatRegister(ins->input()), ToFloatRegister(ins->output()));
}
void
CodeGeneratorPPC::visitSqrtF(LSqrtF *ins)
{
    ADBlock("visitSqrtF");
    FloatRegister output = ToFloatRegister(ins->output());
    FPRSqrt(masm, ToFloatRegister(ins->input()), output);
    masm.frsp(output, output); // See MathF.
}

void
CodeGeneratorPPC::visitAddI(LAddI *ins)
{
    ADBlock("visitAddI");
	
    const LAllocation *lhs = ins->getOperand(0);
    const LAllocation *rhs = ins->getOperand(1);
    const LDefinition *dest = ins->getDef(0);

    MOZ_ASSERT(rhs->isConstant() || rhs->isGeneralReg());

    // If there is no snapshot, we don't need to check for overflow
    if (!ins->snapshot()) {
        if (rhs->isConstant())
            masm.ma_addu(ToRegister(dest), ToRegister(lhs), Imm32(ToInt32(rhs)));
        else
            masm.add(ToRegister(dest), ToRegister(lhs), ToRegister(rhs));
        return;
    }

    Label overflow;
    if (rhs->isConstant())
        masm.ma_addTestOverflow(ToRegister(dest), ToRegister(lhs), Imm32(ToInt32(rhs)), &overflow);
    else
        masm.ma_addTestOverflow(ToRegister(dest), ToRegister(lhs), ToRegister(rhs), &overflow);

    bailoutFrom(&overflow, ins->snapshot());
}

void
CodeGeneratorPPC::visitSubI(LSubI *ins)
{
    ADBlock("visitSubI");
	
    const LAllocation *lhs = ins->getOperand(0);
    const LAllocation *rhs = ins->getOperand(1);
    const LDefinition *dest = ins->getDef(0);

    MOZ_ASSERT(rhs->isConstant() || rhs->isGeneralReg());

    // If there is no snapshot, we don't need to check for overflow
    if (!ins->snapshot()) {
        if (rhs->isConstant())
            masm.ma_subu(ToRegister(dest), ToRegister(lhs), Imm32(ToInt32(rhs)));
        else
            masm.subf(ToRegister(dest), ToRegister(rhs), ToRegister(lhs)); // T = B - A
        return;
    }

    Label overflow;
    if (rhs->isConstant())
        masm.ma_subTestOverflow(ToRegister(dest), ToRegister(lhs), Imm32(ToInt32(rhs)), &overflow);
    else
        masm.ma_subTestOverflow(ToRegister(dest), ToRegister(lhs), ToRegister(rhs), &overflow);

    bailoutFrom(&overflow, ins->snapshot());
}

// We have more strength-reduction options here, so this mostly replaces
// x_sr_mulli for Ion code.
void
CodeGeneratorPPC::visitMulI(LMulI *ins)
{
    ADBlock("visitMulI");

    const LAllocation *lhs = ins->lhs();
    const LAllocation *rhs = ins->rhs();
    Register dest = ToRegister(ins->output());
    MMul *mul = ins->mir();

    MOZ_ASSERT_IF(mul->mode() == MMul::Integer, !mul->canBeNegativeZero() && !mul->canOverflow());

    if (rhs->isConstant()) {
        int32_t constant = ToInt32(rhs);
        Register src = ToRegister(lhs);
        Label multConsOverflow;

        // Bailout on -0.0
        if (mul->canBeNegativeZero() && constant <= 0) {
            Assembler::Condition cond = (constant == 0) ? Assembler::LessThan : Assembler::Equal;
            bailoutCmp32(cond, src, Imm32(0), ins->snapshot());
        }

        // Refer to x_sr_mulli for the specific issues around mulli on
        // PowerPC, but in short, depending on the implementation mulli in
        // the worst case can take up to five cycles (G5 is about four).
        // We optimize more trivial cases here than other arches to desperately
        // avoid this. Assume most integer ops are about one cycle.
        //
        // Only bother if we cannot overflow, because repeated addo may
        // leave XER in an indeterminate state, and serialization problems
        // with XER may cause this to be slower. TODO: Summary overflow
        // would help a lot here.
        //
        // Lowering probably elides quite a lot of this, but let's check anyway.
        
        switch (constant) {
          // The negative zero check already occurred, so these can be heavily optimized.
          
          // -5 is probably pushing it.
          case -4:
            if (!mul->canOverflow()) {
                masm.neg(dest, src);
                masm.add(dest, dest, dest);
                masm.add(dest, dest, dest);
                return; // escape overflow check
            } // else fall through
            break;
          case -3:
            if (!mul->canOverflow()) {
                masm.neg(dest, src);
                masm.add(dest, dest, dest);
                // Remember: subf T,A,B is T <= B - A !!
                masm.subf(dest, src, dest);
                return; // escape overflow check
            } // else fall through
            break;
          case -2:
            // Shouldn't screw up XER too bad ...
            masm.neg(dest, src);
            if (mul->canOverflow())
                masm.addo(dest, dest, dest); // fall through to overflow check
            else {
                masm.add(dest, dest, dest);
                return; // escape overflow check
            }
            break;
          case -1:
            if (mul->canOverflow()) {
                bailoutCmp32(Assembler::Equal, src, Imm32(INT32_MIN), ins->snapshot());
            }
            masm.neg(dest, src);
            return; // can't overflow (now)
          case 0:
            masm.x_li(dest, 0);
            return; // can't overflow
          case 1:
            if (src != dest)
                masm.x_mr(dest, src);
            return; // can't overflow
          case 2:
            // Shouldn't screw up XER too bad either ...
            if (mul->canOverflow())
            	masm.addo(dest, src, src); // fall through to overflow check
            else {
            	masm.add(dest, src, src);
            	return; // escape overflow check
            }
            break;
          case 3:
            // This is on SunSpider, btw.
            if (!mul->canOverflow()) {
                masm.add(dest, src, src);
                masm.add(dest, dest, src);
                return; // escape overflow check
            } // else fall through
            break;
          // Other powers of two are below.
          case 5:
            if (!mul->canOverflow()) {
                masm.add(dest, src, src);
                masm.add(dest, dest, dest);
                masm.add(dest, dest, src);
                return; // escape overflow check
            } // else fall through
            break;
          case 6:
            if (!mul->canOverflow()) {
       	        masm.add(dest, src, src);
                masm.add(dest, dest, src);
       	        masm.add(dest, dest, dest);
       	        return; // escape overflow check
            } // else fall through
            break;
          case 10:
            // This one is shaky, but is no worse than mulli, and may
            // be faster if mulli(10) takes more than 4 cycles. Plus, it's
            // common in some benchmarks and math algorithms.
            if (!mul->canOverflow()) {
                masm.add(dest, src, src);
                masm.add(dest, dest, dest);
                masm.add(dest, dest, src);
                masm.add(dest, dest, dest);
                return; // escape overflow check
            } // else fall through
            break;

          default:
            // Try to optimize powers of 2.
            uint32_t shift = FloorLog2(constant);

            if (!mul->canOverflow() && (constant > 0) && (shift < 32)) {
                // If it cannot overflow, we can do lots of optimizations.
                // (Assuming that the shift is < 31, that is.)
                uint32_t rest = constant - (1 << shift);

                // See if the constant has one bit set, meaning it can be
                // encoded as a bitshift.
                if ((1 << shift) == constant) {
                    masm.x_slwi(dest, src, shift);
                    return;
                }

                // If the constant cannot be encoded as (1<<C1), see if it can
                // be encoded as (1<<C1) | (1<<C2), which can be computed
                // using an add and a shift.
                uint32_t shift_rest = FloorLog2(rest);
                if (src != dest && (1u << shift_rest) == rest && ((shift - shift_rest) < 32)
                		&& (shift_rest < 32)) {
                    masm.x_slwi(dest, src, (shift - shift_rest));
                    masm.add(dest, src, dest);
                    if (shift_rest != 0)
                        masm.x_slwi(dest, dest, shift_rest);
                    return;
                }
            }

            if (mul->canOverflow() && (constant > 0) && (src != dest) && (shift < 32)) {
            	// If it can overflow, there's still one more thing we can try.

                if ((1 << shift) == constant) {
                    // dest = lhs * pow(2, shift)
                    masm.x_slwi(dest, src, shift);
                    // At runtime, check (lhs == dest >> shift). If this does
                    // not hold, some bits were lost due to overflow, and the
                    // computation will require an FPR (bailout).
                    masm.srawi(tempRegister, dest, shift);
                    bailoutCmp32(Assembler::NotEqual, src, tempRegister, ins->snapshot());
                    return;
                }
            }
            break;

        } // switch(constant)

        // Multiply and overflow check. All of the non-overflow cases have
        // already exited, and -2 and 2 already did their math.
        if (constant != -2 && constant != 2) {
            // Well, we tried. At least see if we can avoid borking XER.
            if (!mul->canOverflow()) {
            	// We can!
            	if (Imm16::IsInSignedRange(constant)) {
            		masm.mulli(dest, src, constant);
            	} else {
            		masm.x_li32(tempRegister, constant);
            		masm.mullw(dest, src, tempRegister);
            	}
            	return;
            } else {
            	// We can't.
            	masm.x_li32(tempRegister, constant); // no mullwoi
            	masm.mullwo(dest, src, tempRegister);
            }
        }
        
        masm.bc(Assembler::Overflow, &multConsOverflow);
        bailoutFrom(&multConsOverflow, ins->snapshot());
    } else {
    	// Register case.

        if (mul->canOverflow()) {
            Label multRegOverflow;
        	
            masm.mullwo(dest, ToRegister(lhs), ToRegister(rhs));
            masm.bc(Assembler::Overflow, &multRegOverflow);
            bailoutFrom(&multRegOverflow, ins->snapshot());
        } else
            masm.mullw(dest, ToRegister(lhs), ToRegister(rhs));

        // Must check negative zero, since we can't trust our registers.
        if (mul->canBeNegativeZero()) {
            Label lessThanZero;
            masm.and__rc(tempRegister, dest, dest);
            BufferOffset done = masm._bc(0, Assembler::NonZero, cr0,
                Assembler::LikelyB);

            // Zero result. If LHS or RHS were negative, this is signed
            // negative zero, and an FPR is needed (i.e., bailout). Logical |or|
            // them together: if either is negative, the high bit will be set
            // and the result will be negative.
            masm.or__rc(tempRegister, ToRegister(lhs), ToRegister(rhs));
            masm.bc(Assembler::Signed, &lessThanZero);
            bailoutFrom(&lessThanZero, ins->snapshot());

            masm.bindSS(done);
        }
    }
}

void
CodeGeneratorPPC::visitUDivOrMod(LUDivOrMod *ins)
{
    ADBlock("visitUDivOrMod");

    Register lhs = ToRegister(ins->lhs());
    Register rhs = ToRegister(ins->rhs());
    Register output = ToRegister(ins->output());

    if (!(ins->mir()->isDiv())) {
        BufferOffset done;
        if (ins->mir()->toMod()->canBeDivideByZero()) {
            if (ins->mir()->toMod()->isTruncated()) {
                // Infinity|0 == 0
                masm.cmpwi(rhs, 0);
                BufferOffset nonzero = masm._bc(0, Assembler::NotEqual, cr0,
                    Assembler::LikelyB);
                masm.move32(Imm32(0), output);
                done = masm._b(0);
            
                masm.bindSS(nonzero);
            } else {
                MOZ_ASSERT(ins->snapshot());
                bailoutCmp32(Assembler::Equal, rhs, Imm32(0), ins->snapshot());
            }
        }

        // Compute remainder; overflow not needed.
        masm.divwu(tempRegister, lhs, rhs);
        masm.mullw(addressTempRegister, tempRegister, rhs);
        masm.subf(output, addressTempRegister, lhs);

        if (!ins->mir()->toMod()->isTruncated()) {
    	    MOZ_ASSERT(ins->snapshot());
            bailoutCmp32(Assembler::LessThan, output, Imm32(0), ins->snapshot());
        }

        masm.bindSS(done);
        return;
    }

    BufferOffset done;
    if (ins->mir()->toDiv()->canBeDivideByZero()) {
        if (ins->mir()->toDiv()->isTruncated()) {
            masm.cmpwi(rhs, 0);
            BufferOffset nonzero = masm._bc(0, Assembler::NotEqual);
            masm.move32(Imm32(0), output);
            done = masm._b(0);
            
            masm.bindSS(nonzero);
        } else {
            MOZ_ASSERT(ins->snapshot());
            bailoutCmp32(Assembler::Equal, rhs, Imm32(0), ins->snapshot());
        }
    }

    // Overflow bit not needed.
    masm.divwu(output, lhs, rhs);

    // Remainder when unexpected: bailout
    if (!ins->mir()->toDiv()->canTruncateRemainder()) {
    	Register temp = addressTempRegister;
 
        MOZ_ASSERT(ins->snapshot());
        MOZ_ASSERT(output != temp);
        MOZ_ASSERT(lhs != temp);
        MOZ_ASSERT(rhs != temp);
        MOZ_ASSERT(output != tempRegister);
        MOZ_ASSERT(lhs != tempRegister);
        MOZ_ASSERT(rhs != tempRegister);

        Label remainderNonZero;
        
        // We don't need mullwo; we know this won't overflow.
        masm.mullw(tempRegister, output, rhs);
        masm.cmpw(tempRegister, lhs);
        masm.bc(Assembler::NotEqual, &remainderNonZero);
        bailoutFrom(&remainderNonZero, ins->snapshot());
    }

    // Signed values when unexpected: bailout
    if (!ins->mir()->toDiv()->isTruncated()) {
    	MOZ_ASSERT(ins->snapshot());
        bailoutCmp32(Assembler::LessThan, output, Imm32(0), ins->snapshot());
    }

    masm.bindSS(done);
}

void
CodeGeneratorPPC::visitDivI(LDivI *ins)
{
    ADBlock("visitDivI");

    Label overflowException;
    BufferOffset done, done2;
    Register lhs = ToRegister(ins->lhs());
    Register rhs = ToRegister(ins->rhs());
    Register output = ToRegister(ins->output());
    Register temp = ToRegister(ins->getTemp(0));
    LSnapshot *snap = ins->snapshot();
    MDiv *mir = ins->mir();
    
    // divwo will automatically set the Overflow bit if INT_MIN/-1 is
    // performed, or if we divide by zero, so we just bailout based on that.
    // We only have to handle truncation.
    
    // Handle divide by zero.
    if (mir->canBeDivideByZero() && mir->canTruncateInfinities()) {
    	// Truncated division by zero is zero (Infinity|0 == 0)
    	
    	masm.and__rc(tempRegister, rhs, rhs);
        BufferOffset notzero = masm._bc(0, Assembler::NonZero, cr0,
            Assembler::LikelyB);
        masm.x_li(output, 0);
        done = masm._b(0);

        masm.bindSS(notzero);
    }

    // Handle an integer overflow exception from -2147483648 / -1.
    if (mir->canBeNegativeOverflow() && mir->canTruncateOverflow()) {
        // (-INT32_MIN)|0 == INT32_MIN

        masm.x_li32(output, INT32_MIN); // sign extends
        masm.cmpw(lhs, output);
        BufferOffset skip = masm._bc(0, Assembler::NotEqual, cr0,
            Assembler::LikelyB);
        masm.cmpwi(rhs, -1); // sign extends
        done2 = masm._bc(0, Assembler::Equal);

        masm.bindSS(skip);
    }

    // Handle negative 0. (0/-Y)
    // We must do this in the *non*-truncated case, because divwo won't
    // throw in that situation.
    if (!mir->canTruncateNegativeZero() && mir->canBeNegativeZero()) {
        MOZ_ASSERT(snap);
        masm.and__rc(tempRegister, lhs, lhs);
        BufferOffset nonzero = masm._bc(0, Assembler::NonZero, cr0,
            Assembler::LikelyB);
        bailoutCmp32(Assembler::LessThan, rhs, Imm32(0), snap);
            
        masm.bindSS(nonzero);
    }

    // DIVIDE! AND! CONQUER!
    if (snap) {
        // If Overflow is thrown, we know we've handled truncation already, so
        // we must bail out. However, only do this if there is a snapshot,
        // because if there isn't, then Ion already knows we can't (mustn't)
        // fail.
        masm.divwo(output, lhs, rhs);
        masm.bc(Assembler::Overflow, &overflowException);
        bailoutFrom(&overflowException, snap);
    } else {
        // We have nothing to bail out from, so checking overflow is pointless.
        masm.divw(output, lhs, rhs);
    }
		
    // If we cannot truncate the result, bailout if there's a remainder.
    if (!mir->canTruncateRemainder()) {
        MOZ_ASSERT(snap);
        Label remainderNonZero;
        
        // We don't need mullwo; we know this won't overflow.
        masm.mullw(tempRegister, output, rhs);
        masm.cmpw(tempRegister, lhs);
        masm.bc(Assembler::NotEqual, &remainderNonZero);
        bailoutFrom(&remainderNonZero, snap);
    }

    masm.bindSS(done);
    masm.bindSS(done2);
}

void
CodeGeneratorPPC::visitDivPowTwoI(LDivPowTwoI *ins)
{
    ADBlock("visitDivPowTwoI");
    // Careful: some PowerPC implementations will merrily try to shift
    // more than 32 bits.

    Register lhs = ToRegister(ins->numerator());
    Register dest = ToRegister(ins->output());
    Register tmp = ToRegister(ins->getTemp(0));
    int32_t shift = ins->shift();

    if (shift != 0) {
        MDiv *mir = ins->mir();
        if (!mir->isTruncated()) {
            // If the remainder is going to be != 0, bailout since this must
            // be a double.
            masm.x_slwi(tmp, lhs, (32 - shift) & 0x1f);
            bailoutCmp32(Assembler::NonZero, tmp, tmp, ins->snapshot());
        }

        if (!mir->canBeNegativeDividend()) {
            // Numerator is unsigned, so needs no adjusting. Do the shift.
            masm.srawi(dest, lhs, shift & 0x1f);
            return;
        }

        // Adjust the value so that shifting produces a correctly rounded result
        // when the numerator is negative. See 10-1 "Signed Division by a Known
        // Power of 2" in Henry S. Warren, Jr.'s Hacker's Delight.
        if (shift > 1) {
            masm.srawi(tmp, lhs, 31);
            masm.x_srwi(tmp, tmp, (32 - shift) & 0x1f);
            masm.add32(lhs, tmp);
        } else {
            if (shift != 0)
                masm.x_srwi(tmp, lhs, (32 - shift) & 0x1f);
            else
                masm.x_mr(tmp, lhs); // XXX
            masm.add32(lhs, tmp);
        }

        // Do the shift.
        masm.srawi(dest, tmp, shift & 0x1f);
    } else {
        masm.move32(lhs, dest);
    }
}

void
CodeGeneratorPPC::visitModI(LModI *ins)
{
    ADBlock("visitModI");

    Register lhs = ToRegister(ins->lhs());
    Register rhs = ToRegister(ins->rhs());
    Register dest = ToRegister(ins->output());
    Register callTemp = ToRegister(ins->getTemp(0));
    LSnapshot *snap = ins->snapshot();
    MMod *mir = ins->mir();
    BufferOffset prevent, done, done2, done3, done4;

    // This isn't mostly the same as visitDivI, mostly due to some unusual
    // special cases. That's why consolidating them as most of the other arches
    // do doesn't pay off for us.
    masm.move32(lhs, callTemp);

    // Prevent INT_MIN % -1;
    // The integer division will give INT_MIN, but we want -(double)INT_MIN.
    if (mir->canBeNegativeDividend()) {
    	prevent = masm._bc(0,
            masm.ma_cmp(lhs, Imm32(INT_MIN), Assembler::NotEqual), cr0,
            Assembler::LikelyB);
        if (mir->isTruncated()) {
            // (INT_MIN % -1)|0 == 0
            BufferOffset skip = masm._bc(0,
                masm.ma_cmp(rhs, Imm32(-1), Assembler::NotEqual), cr0,
                Assembler::LikelyB);
            masm.move32(Imm32(0), dest);
            done = masm._b(0);
            
            masm.bindSS(skip);
        } else {
            MOZ_ASSERT(snap);
            bailoutCmp32(Assembler::Equal, rhs, Imm32(-1), snap);
        }
        masm.bindSS(prevent);
    }

    // 0/X (with X < 0) is bad because both of these values *should* be
    // doubles, and the result should be -0.0, which cannot be represented in
    // integers. X/0 is bad because it will give garbage (or abort), when it
    // should give either \infty, -\infty or NAN.

    // Prevent 0 / X (with X < 0) and X / 0
    // testing X / Y.  Compare Y with 0.
    // There are three cases: (Y < 0), (Y == 0) and (Y > 0)
    // If (Y < 0), then we compare X with 0, and bail if X == 0
    // If (Y == 0), then we simply want to bail.
    // if (Y > 0), we don't bail.

    if (mir->canBeDivideByZero()) {
        if (mir->isTruncated()) {
            BufferOffset skip = masm._bc(0,
                masm.ma_cmp(rhs, Imm32(0), Assembler::NotEqual), cr0,
                Assembler::LikelyB);
            masm.move32(Imm32(0), dest);
            done2 = masm._b(0);
            
            masm.bindSS(skip);
        } else {
            MOZ_ASSERT(snap);
            bailoutCmp32(Assembler::Equal, rhs, Imm32(0), snap);
        }
    }

    if (mir->canBeNegativeDividend()) {
        masm.cmpwi(rhs, 0);
        BufferOffset notNegative = masm._bc(0, Assembler::GreaterThan);        

        if (mir->isTruncated()) {
            // NaN|0 == 0 and (0 % -X)|0 == 0
            BufferOffset skip = masm._bc(0,
                masm.ma_cmp(lhs, Imm32(0), Assembler::NotEqual), cr0,
                Assembler::LikelyB);
            masm.move32(Imm32(0), dest);
            done3 = masm._b(0);
            
            masm.bindSS(skip);
        } else {
            MOZ_ASSERT(snap);
            bailoutCmp32(Assembler::Equal, lhs, Imm32(0), snap);
        }
        masm.bindSS(notNegative);
    }

    // Compute remainder; overflow not needed.
    masm.divw(tempRegister, lhs, rhs);
    masm.mullw(addressTempRegister, tempRegister, rhs);
    masm.subf(dest, addressTempRegister, lhs);

    // If X%Y == 0 and X < 0, then we *actually* wanted to return -0.0
    if (mir->canBeNegativeDividend()) {
        if (mir->isTruncated()) {
            // -0.0|0 == 0
        } else {
            MOZ_ASSERT(snap);
            // No worries if X >= 0.
            done4 = masm._bc(0,
                masm.ma_cmp(dest, Imm32(0), Assembler::NotEqual), cr0,
                Assembler::LikelyB);
            // Well, shoot.
            // Use callTemp, just in case the output register and lhs were the same.
            bailoutCmp32(Assembler::Signed, callTemp, Imm32(0), snap);
        }
    }
    masm.bindSS(done);
    masm.bindSS(done2);
    masm.bindSS(done3);
    masm.bindSS(done4);
}

void
CodeGeneratorPPC::visitModPowTwoI(LModPowTwoI *ins)
{
    ADBlock("visitModPowTwoI");

    Register in = ToRegister(ins->getOperand(0));
    Register out = ToRegister(ins->getDef(0));
    MMod *mir = ins->mir();

    masm.move32(in, out);
    
    // If zero, nothing to do.
    masm.cmpwi(in, 0); // signed comparison please
    BufferOffset done = masm._bc(0, Assembler::Equal);
    
    // If positive, just use a bitmask.
    BufferOffset negative = masm._bc(0, Assembler::LessThan);
    masm.andi_rc(out, out, ((1 << ins->shift()) - 1));
    BufferOffset done2 = masm._b(0);
    
    // If negative, negate, bitmask, and negate again.
    masm.bindSS(negative);

    masm.neg32(out);
    masm.andi_rc(out, out, ((1 << ins->shift()) - 1));
    masm.neg32(out);

    // Negative-zero check.
    if (mir->canBeNegativeDividend()) {
        if (!mir->isTruncated()) {
            MOZ_ASSERT(mir->fallible());
            bailoutCmp32(Assembler::Equal, out, Imm32(0), ins->snapshot());
        } else {
            // -0|0 == 0, so leave it be.
        }
    }
    masm.bindSS(done);
    masm.bindSS(done2);
}

// visitModMaskI NYI/NYS

void
CodeGeneratorPPC::visitBitNotI(LBitNotI *ins)
{
    ADBlock("visitBitNotI");
	
    const LAllocation *input = ins->getOperand(0);
    const LDefinition *dest = ins->getDef(0);
    MOZ_ASSERT(!input->isConstant());

    masm.nor(ToRegister(dest), ToRegister(input), ToRegister(input)); // OPPCC appendix A p.540
}

void
CodeGeneratorPPC::visitBitOpI(LBitOpI *ins)
{
    ADBlock("visitBitOpI");
	
    const LAllocation *lhs = ins->getOperand(0);
    const LAllocation *rhs = ins->getOperand(1);
    const LDefinition *dest = ins->getDef(0);
    // all of these bitops should be either imm32's, or integer registers.
    switch (ins->bitop()) {
      case JSOP_BITOR:
        if (rhs->isConstant())
            masm.ma_or(ToRegister(dest), ToRegister(lhs), Imm32(ToInt32(rhs)));
        else
            masm.ma_or(ToRegister(dest), ToRegister(lhs), ToRegister(rhs));
        break;
      case JSOP_BITXOR:
        if (rhs->isConstant())
            masm.ma_xor(ToRegister(dest), ToRegister(lhs), Imm32(ToInt32(rhs)));
        else
            masm.ma_xor(ToRegister(dest), ToRegister(lhs), ToRegister(rhs));
        break;
      case JSOP_BITAND:
        if (rhs->isConstant())
            masm.ma_and(ToRegister(dest), ToRegister(lhs), Imm32(ToInt32(rhs)));
        else
            masm.ma_and(ToRegister(dest), ToRegister(lhs), ToRegister(rhs));
        break;
      default:
        MOZ_CRASH("unexpected binary opcode");
    }
}

void
CodeGeneratorPPC::visitShiftI(LShiftI *ins)
{
    ADBlock("visitShiftI");

    Register lhs = ToRegister(ins->lhs());
    const LAllocation *rhs = ins->rhs();
    Register dest = ToRegister(ins->output());

    if (rhs->isConstant()) {
    	// Some PowerPC implementations will merrily try to shift more than 31 bits.
        int32_t shift = ToInt32(rhs) & 0x1F;
        switch (ins->bitop()) {
          case JSOP_LSH:
            if (shift)
                masm.x_slwi(dest, lhs, shift);
            else {
                if (lhs != dest)
                    masm.x_mr(dest, lhs);
	    }
            break;
          case JSOP_RSH:
            if (shift)
                masm.srawi(dest, lhs, shift);
            else {
                if (lhs != dest)
                    masm.x_mr(dest, lhs);
            }
            break;
          case JSOP_URSH:
            if (shift) {
                masm.x_srwi(dest, lhs, shift);
            } else {
                // x >>> 0 can overflow.
                if (lhs != dest)
                    masm.x_mr(dest, lhs);
                if (ins->mir()->toUrsh()->fallible()) {
                    bailoutCmp32(Assembler::LessThan, dest, Imm32(0), ins->snapshot());
                }
            }
            break;
          default:
            MOZ_CRASH("Unexpected shift op");
        }
    } else {
        // The shift amounts should be AND'ed into the 0-31 range.
        // It would be nice to know which, if any, Power Mac CPUs shift >31 bits
        // so we can save an instruction here.
        masm.andi_rc(dest, ToRegister(rhs), 0x1f); // no andi w/o Rc

        switch (ins->bitop()) {
          case JSOP_LSH:
            masm.slw(dest, lhs, dest);
            break;
          case JSOP_RSH:
            masm.sraw(dest, lhs, dest);
            break;
          case JSOP_URSH:
            masm.srw(dest, lhs, dest);
            if (ins->mir()->toUrsh()->fallible()) {
                // x >>> 0 can overflow.
                bailoutCmp32(Assembler::LessThan, dest, Imm32(0), ins->snapshot());
            }
            break;
          default:
            MOZ_CRASH("Unexpected shift op");
        }
    }
}

void
CodeGeneratorPPC::visitUrshD(LUrshD *ins)
{
    ADBlock("visitUrshD");
    Register lhs = ToRegister(ins->lhs());
    Register temp = ToRegister(ins->temp());
    // Oh, master, don't let them overshift me on certain
    // PowerPC implementations that don't know any better!

    const LAllocation *rhs = ins->rhs();
    FloatRegister out = ToFloatRegister(ins->output());

    if (rhs->isConstant()) {
    	int32_t shift = ToInt32(rhs) & 0x1f;
    	if (shift)
            masm.x_srwi(temp, lhs, shift);
        else
            masm.x_mr(temp, lhs); // XXX
    } else {
    	masm.andi_rc(tempRegister, ToRegister(rhs), 0x1f);
        masm.srw(temp, lhs, tempRegister);
    }

    masm.convertUInt32ToDouble(temp, out);
}

void
CodeGeneratorPPC::visitClzI(LClzI *ins)
{
    ADBlock("visitClzI");
    Register input = ToRegister(ins->input());
    Register output = ToRegister(ins->output());

    masm.cntlzw(output, input);
}

// no F form
void
CodeGeneratorPPC::visitPowHalfD(LPowHalfD *ins)
{
    ADBlock("visitPowHalfD");
    FloatRegister input = ToFloatRegister(ins->input());
    FloatRegister output = ToFloatRegister(ins->output());

    // Masm.pow(-Infinity, 0.5) == Infinity.
    masm.loadConstantDouble(NegativeInfinity<double>(), ScratchDoubleReg);
    masm.fcmpu(input, ScratchDoubleReg);
    BufferOffset skip = masm._bc(0, Assembler::DoubleNotEqualOrUnordered, cr0,
        Assembler::LikelyB);
    masm.fneg(output, ScratchDoubleReg);
    BufferOffset done = masm._b(0);

    masm.bindSS(skip);
    // Math.pow(-0, 0.5) == 0 == Math.pow(0, 0.5).
    // Adding 0 converts any -0 to 0.
    masm.zeroDouble(ScratchDoubleReg);
    masm.fadd(output, input, ScratchDoubleReg);
    FPRSqrt(masm, output, output);

    masm.bindSS(done);
}

MoveOperand
CodeGeneratorPPC::toMoveOperand(const LAllocation a) const
{
    if (a.isGeneralReg())
        return MoveOperand(ToRegister(a));
    if (a.isFloatReg()) {
        return MoveOperand(ToFloatRegister(a));
    }
    int32_t offset = ToStackOffset(a);
    MOZ_ASSERT((offset & 3) == 0);

    return MoveOperand(StackPointer, offset);
}

class js::jit::OutOfLineTableSwitch : public OutOfLineCodeBase<CodeGeneratorPPC>
{
    MTableSwitch *mir_;
    CodeLabel jumpLabel_;

    void accept(CodeGeneratorPPC *codegen) {
        codegen->visitOutOfLineTableSwitch(this);
    }

  public:
    OutOfLineTableSwitch(MTableSwitch *mir)
      : mir_(mir)
    {}

    MTableSwitch *mir() const {
        return mir_;
    }

    CodeLabel *jumpLabel() {
        return &jumpLabel_;
    }
};

// 2^this is the length of a table switch case.
#define PPC_OOLTS_STANZA_LENGTH_POWER 4

void
CodeGeneratorPPC::visitOutOfLineTableSwitch(OutOfLineTableSwitch *ool)
{
    ADBlock("visitOutOfLineTableSwitch");
    MTableSwitch *mir = ool->mir();

    masm.align(sizeof(void*));
    masm.bind(ool->jumpLabel()->target());
    masm.addCodeLabel(*ool->jumpLabel());

    for (size_t i = 0; i < mir->numCases(); i++) {
        LBlock *caseblock = skipTrivialBlocks(mir->getCase(i))->lir();
        Label *caseheader = caseblock->label();
        uint32_t caseoffset = caseheader->offset();

        // The entries of the jump table need to be absolute addresses and thus
        // must be patched after codegen is finished.
        CodeLabel cl;
        mozilla::DebugOnly<int32_t> bo = masm.nextOffset().getOffset();

        masm.ma_li32(tempRegister, cl.patchAt()); // Always patchable, always two words.
        masm.x_mtctr(tempRegister);
        masm.bctr();
        MOZ_ASSERT((masm.nextOffset().getOffset() - bo) == (1 << PPC_OOLTS_STANZA_LENGTH_POWER));

        cl.target()->bind(caseoffset);
        masm.addCodeLabel(cl);
    }
}

void
CodeGeneratorPPC::emitTableSwitchDispatch(MTableSwitch *mir, Register index,
                                           Register address)
{
    ADBlock("emitTableSwitchDispatch");
    Label *defaultcase = skipTrivialBlocks(mir->getDefault())->lir()->label();

    // Lower value with low value
    if (mir->low() != 0)
        masm.subPtr(Imm32(mir->low()), index);

    // Jump to default case if input is out of range
    int32_t cases = mir->numCases();
    masm.branchPtr(Assembler::AboveOrEqual, index, ImmWord(cases), defaultcase);

    // To fill in the CodeLabels for the case entries, we need to first
    // generate the case entries (we don't yet know their offsets in the
    // instruction stream).
    OutOfLineTableSwitch *ool = new(alloc()) OutOfLineTableSwitch(mir);
    addOutOfLineCode(ool, mir);

    // Compute the position where a pointer to the right case stands.
    masm.ma_li32(address, ool->jumpLabel()->patchAt());
    masm.lshiftPtr(Imm32(PPC_OOLTS_STANZA_LENGTH_POWER), index);
    masm.addPtr(index, address);
    masm.branch(address); // There doesn't appear to be a better way to schedule this.
}

void
CodeGeneratorPPC::visitMathD(LMathD *math)
{
    ADBlock("visitMathD");
    const LAllocation *src1 = math->getOperand(0);
    const LAllocation *src2 = math->getOperand(1);
    const LDefinition *output = math->getDef(0);
    switch(math->jsop()) {
      // Operand order is the same.
      case JSOP_ADD:
        masm.fadd(ToFloatRegister(output), ToFloatRegister(src1), ToFloatRegister(src2));
        break;
      case JSOP_SUB:
        masm.fsub(ToFloatRegister(output), ToFloatRegister(src1), ToFloatRegister(src2));
        break;
      case JSOP_MUL:
        masm.fmul(ToFloatRegister(output), ToFloatRegister(src1), ToFloatRegister(src2));
        break;
      case JSOP_DIV:
        masm.fdiv(ToFloatRegister(output), ToFloatRegister(src1), ToFloatRegister(src2));
        break;
      default:
        MOZ_CRASH("unexpected opcode");
    }
}

void
CodeGeneratorPPC::visitMathF(LMathF *math)
{
    ADBlock("visitMathF");
    const LAllocation *src1 = math->getOperand(0);
    const LAllocation *src2 = math->getOperand(1);
    const LDefinition *output = math->getDef(0);
    switch(math->jsop()) {
      // Operand order is the same.
      case JSOP_ADD:
        masm.fadds(ToFloatRegister(output), ToFloatRegister(src1), ToFloatRegister(src2));
        break;
      case JSOP_SUB:
        masm.fsubs(ToFloatRegister(output), ToFloatRegister(src1), ToFloatRegister(src2));
        break;
      case JSOP_MUL:
        masm.fmuls(ToFloatRegister(output), ToFloatRegister(src1), ToFloatRegister(src2));
        break;
      case JSOP_DIV:
        masm.fdivs(ToFloatRegister(output), ToFloatRegister(src1), ToFloatRegister(src2));
        break;
      default:
        MOZ_CRASH("unexpected opcode");
    }
}

// Common code for visitFloor/F, visitCeil/F and visitRound/F.
// This essentially combines branchTruncateDouble and convertDoubleToInt32.
// The fudge register is considered volatile and may be clobbered if provided.
static void
FPRounder(MacroAssembler &masm, FloatRegister input, FloatRegister scratch, FloatRegister fudge, Register output,
	Label *bailoutBogusFloat, Label *bailoutOverflow, uint32_t rmode, bool ceilCheck, bool roundCheck)
{
	FloatRegister victim = input;
        BufferOffset done, done2, skipCheck, skipCheck2, skipCheck3;
	
	// Prepare zero checks for the next step.
        // If fudge is provided, it's ordered, so use a sub to generate zero.
        if (fudge != InvalidFloatReg) {
            masm.fsub(scratch, fudge, fudge);
        } else {
	    masm.zeroDouble(scratch);
        }
	masm.fcmpu(input, scratch);
	// We need the result of this comparison one more time at the end if we are round()ing, so
	// put another copy in CR1.
	if (roundCheck)
		masm.fcmpu(cr1, input, scratch);
	
	// Since we have to invariably put the float on the stack at some point, do it
	// here so we can schedule better.
	masm.stfdu(input, stackPointerRegister, -8);
	
	if (ceilCheck) {
		// ceil() has an edge case for -1 < x < 0: it should be -0. We bail out for those values below.
		skipCheck = masm._bc(0, Assembler::DoubleGreaterThan, cr0,
                    Assembler::LikelyB);
		masm.loadConstantDouble(-1.0, scratch);
		masm.fcmpu(input, scratch);
                skipCheck2 = masm._bc(0, Assembler::DoubleLessThanOrEqual, cr0,
                    Assembler::LikelyB);
	} else {
		// Check for equality with zero.
		// If ordered and non-zero, proceed to truncation.
                skipCheck3 = masm._bc(0, Assembler::DoubleNotEqual, cr0,
                    Assembler::LikelyB);
	}
	// We are either zero, NaN or negative zero. Analyse the float's upper word. If it's non-zero, oops.
	// (We also get here if the ceil check tripped, but we'd be bailing out anyway, so.)
	masm.lwz(tempRegister, stackPointerRegister, 0);
	masm.cmpwi(tempRegister, 0);
	masm.addi(stackPointerRegister, stackPointerRegister, 8);
	masm.bc(Assembler::NotEqual, bailoutBogusFloat); // either -0.0 or NaN, or (ceil) non-zero
		
	// It's zero; return zero.
	masm.x_li32(output, 0);
	done = masm._b(0);
	
	// Now we have a valid, non-zero float.
	masm.bindSS(skipCheck);
	masm.bindSS(skipCheck2);
	masm.bindSS(skipCheck3);
	// If a fudge factor was provided, add it now.
	if (fudge != InvalidFloatReg) {
		masm.fadd(fudge, fudge, input);
		victim = fudge;
	}
	// We're only interested in whether the integer overflowed -- we don't care if
	// it was truncated (FI) or which way (FR), so we just want VXCVI (FPSCR field 5).
	masm.mtfsb0(23); // whack VXCVI
	// Set the current rounding mode, if the mode is not 0x01 (because we can just fctiwz).
	// Set it back to 0 when we're done.
	// Don't update the CR; we only want the FPSCR.
	if (rmode != 0x01) {
                if (rmode == 0x03) {
                    masm.mtfsfi(7,3);
                } else {
		    if (rmode & 0x01)
		    	masm.mtfsb1(31);
		    if (rmode & 0x02)
			masm.mtfsb1(30);
                }
		masm.fctiw(scratch, victim);
                if (rmode == 0x03) {
                    masm.mtfsfi(7,0);
                } else {
		    if (rmode & 0x01)
		    	masm.mtfsb0(31);
		    if (rmode & 0x02)
		    	masm.mtfsb0(30);
                }
	} else
		masm.fctiwz(scratch, victim);
	masm.stfd(scratch, stackPointerRegister, 0); // overwrite original float on stack
	masm.mcrfs(cr0, 5); // microcoded on G5; new dispatch group; VXCVI -> cr0::SOBit
	// Pull out the lower 32 bits. This is the result.
	masm.lwz(output, stackPointerRegister, 4);
	masm.addi(stackPointerRegister, stackPointerRegister, 8);
	masm.bc(Assembler::SOBit, bailoutOverflow);
		
	// round() must bail out if the result is zero and the input was negative (copied
	// in CR1 from the beginning). By this time, we can assume the result was ordered.
	if (roundCheck) {
		masm.cmpwi(output, 0); // "free"
                done2 = masm._bc(0,
                    Assembler::DoubleGreaterThan, cr1, Assembler::LikelyB);
		masm.bc(Assembler::Equal, bailoutBogusFloat);
	}
	masm.bindSS(done);
	masm.bindSS(done2);
}

// fctiw can (to our displeasure) round down, not up. For example, in rounding mode 0x00,
// the above "rounds" -3.5 to -4. Arguably that's right, but JS expects -3. The simplest fix
// for that is to add 0.5 and round towards -Inf, which also works in the positive case.
// However, we can't add it until after the -0/NaN checks, so we provide it as a "fudge" register.
void
CodeGeneratorPPC::visitRound(LRound *lir)
{
	ADBlock("visitRound");
	Label bailoutBogusFloat, bailoutOverflow;
	FloatRegister input = ToFloatRegister(lir->input());
	FloatRegister temp = ToFloatRegister(lir->temp());
	
	masm.point5Double(temp);
	FPRounder(masm, input, ScratchDoubleReg, temp, ToRegister(lir->output()),
		&bailoutBogusFloat, &bailoutOverflow, 0x03, false, true);
	bailoutFrom(&bailoutBogusFloat, lir->snapshot());
	bailoutFrom(&bailoutOverflow, lir->snapshot());
}
void
CodeGeneratorPPC::visitRoundF(LRoundF *lir)
{
	ADBlock("visitRoundF");
	Label bailoutBogusFloat, bailoutOverflow;
	FloatRegister input = ToFloatRegister(lir->input());
	FloatRegister temp = ToFloatRegister(lir->temp());
	
	masm.point5Double(temp);
	FPRounder(masm, input, ScratchDoubleReg, temp, ToRegister(lir->output()),
		&bailoutBogusFloat, &bailoutOverflow, 0x03, false, true);
	bailoutFrom(&bailoutBogusFloat, lir->snapshot());
	bailoutFrom(&bailoutOverflow, lir->snapshot());
}

void
CodeGeneratorPPC::visitFloor(LFloor *lir)
{
	ADBlock("visitFloor");
	Label bailoutBogusFloat, bailoutOverflow;
	
	FPRounder(masm, ToFloatRegister(lir->input()), ScratchDoubleReg, InvalidFloatReg,
		ToRegister(lir->output()), &bailoutBogusFloat, &bailoutOverflow, 0x03, false, false);
	bailoutFrom(&bailoutBogusFloat, lir->snapshot());
	bailoutFrom(&bailoutOverflow, lir->snapshot());
}
void
CodeGeneratorPPC::visitFloorF(LFloorF *lir)
{
	ADBlock("visitFloorF");
	Label bailoutBogusFloat, bailoutOverflow;
	
	FPRounder(masm, ToFloatRegister(lir->input()), ScratchFloat32Reg, InvalidFloatReg,
		ToRegister(lir->output()), &bailoutBogusFloat, &bailoutOverflow, 0x03, false, false);
	bailoutFrom(&bailoutBogusFloat, lir->snapshot());
	bailoutFrom(&bailoutOverflow, lir->snapshot());
}

void
CodeGeneratorPPC::visitCeil(LCeil *lir)
{
	ADBlock("visitCeil");
	Label bailoutBogusFloat, bailoutOverflow;
	
	FPRounder(masm, ToFloatRegister(lir->input()), ScratchDoubleReg, InvalidFloatReg,
		ToRegister(lir->output()), &bailoutBogusFloat, &bailoutOverflow, 0x02, true, false);
	bailoutFrom(&bailoutBogusFloat, lir->snapshot());
	bailoutFrom(&bailoutOverflow, lir->snapshot());
}
void
CodeGeneratorPPC::visitCeilF(LCeilF *lir)
{
	ADBlock("visitCeilF");
	Label bailoutBogusFloat, bailoutOverflow;
	
	FPRounder(masm, ToFloatRegister(lir->input()), ScratchFloat32Reg, InvalidFloatReg,
		ToRegister(lir->output()), &bailoutBogusFloat, &bailoutOverflow, 0x02, true, false);
	bailoutFrom(&bailoutBogusFloat, lir->snapshot());
	bailoutFrom(&bailoutOverflow, lir->snapshot());
}

void
CodeGeneratorPPC::visitTruncateDToInt32(LTruncateDToInt32 *ins)
{
    emitTruncateDouble(ToFloatRegister(ins->input()), ToRegister(ins->output()),
                              ins->mir());
}

void
CodeGeneratorPPC::visitTruncateFToInt32(LTruncateFToInt32 *ins)
{
    emitTruncateFloat32(ToFloatRegister(ins->input()), ToRegister(ins->output()),
                               ins->mir());
}

static const uint32_t FrameSizes[] = { 128, 256, 512, 1024 };

FrameSizeClass
FrameSizeClass::FromDepth(uint32_t frameDepth)
{
    for (uint32_t i = 0; i < JS_ARRAY_LENGTH(FrameSizes); i++) {
        if (frameDepth < FrameSizes[i])
            return FrameSizeClass(i);
    }

    return FrameSizeClass::None();
}

FrameSizeClass
FrameSizeClass::ClassLimit()
{
    return FrameSizeClass(JS_ARRAY_LENGTH(FrameSizes));
}

uint32_t
FrameSizeClass::frameSize() const
{
    MOZ_ASSERT(class_ != NO_FRAME_SIZE_CLASS_ID);
    MOZ_ASSERT(class_ < JS_ARRAY_LENGTH(FrameSizes));

    return FrameSizes[class_];
}

ValueOperand
CodeGeneratorPPC::ToValue(LInstruction *ins, size_t pos)
{
    Register typeReg = ToRegister(ins->getOperand(pos + TYPE_INDEX));
    Register payloadReg = ToRegister(ins->getOperand(pos + PAYLOAD_INDEX));
    return ValueOperand(typeReg, payloadReg);
}

ValueOperand
CodeGeneratorPPC::ToOutValue(LInstruction *ins)
{
    Register typeReg = ToRegister(ins->getDef(TYPE_INDEX));
    Register payloadReg = ToRegister(ins->getDef(PAYLOAD_INDEX));
    return ValueOperand(typeReg, payloadReg);
}

ValueOperand
CodeGeneratorPPC::ToTempValue(LInstruction *ins, size_t pos)
{
    Register typeReg = ToRegister(ins->getTemp(pos + TYPE_INDEX));
    Register payloadReg = ToRegister(ins->getTemp(pos + PAYLOAD_INDEX));
    return ValueOperand(typeReg, payloadReg);
}

void
CodeGeneratorPPC::visitValue(LValue *value)
{
    ADBlock("visitValue");
    const ValueOperand out = ToOutValue(value);

    masm.moveValue(value->value(), out);
}

void
CodeGeneratorPPC::visitBox(LBox *box)
{
    ADBlock("visitBox");
    const LDefinition *type = box->getDef(TYPE_INDEX);

    MOZ_ASSERT(!box->getOperand(0)->isConstant());

    // For NUNBOX32, the input operand and the output payload have the same
    // virtual register. All that needs to be written is the type tag for
    // the type definition.
    masm.move32(Imm32(MIRTypeToTag(box->type())), ToRegister(type));
}

void
CodeGeneratorPPC::visitBoxFloatingPoint(LBoxFloatingPoint *box)
{
    ADBlock("visitBoxFloatingPoint");
	
    const LDefinition *payload = box->getDef(PAYLOAD_INDEX);
    const LDefinition *type = box->getDef(TYPE_INDEX);
    const LAllocation *in = box->getOperand(0);

    FloatRegister reg = ToFloatRegister(in);
    if (box->type() == MIRType_Float32) {
        masm.convertFloat32ToDouble(reg, fpTempRegister);
        reg = fpTempRegister;
    }
    masm.ma_mv(reg, ValueOperand(ToRegister(type), ToRegister(payload)));
}

void
CodeGeneratorPPC::visitUnbox(LUnbox *unbox)
{
    ADBlock("visitUnbox");
	
    // Note that for unbox, the type and payload indexes are switched on the
    // inputs.
    MUnbox *mir = unbox->mir();
    Register type = ToRegister(unbox->type());

    if (mir->fallible()) {
        bailoutCmp32(Assembler::NotEqual, type, Imm32(MIRTypeToTag(mir->type())),
                          unbox->snapshot());
    }
}

void
CodeGeneratorPPC::visitDouble(LDouble *ins)
{
    ADBlock("visitDouble");
    const LDefinition *out = ins->getDef(0);

    masm.loadConstantDouble(ins->getDouble(), ToFloatRegister(out));
}

void
CodeGeneratorPPC::visitFloat32(LFloat32 *ins)
{
    ADBlock("visitFloat32");
	
    const LDefinition *out = ins->getDef(0);
    masm.loadConstantFloat32(ins->getFloat(), ToFloatRegister(out));
}

Register
CodeGeneratorPPC::splitTagForTest(const ValueOperand &value)
{
    return value.typeReg();
}

void
CodeGeneratorPPC::visitTestDAndBranch(LTestDAndBranch *test)
{
    ADBlock("visitTestDAndBranch");
    FloatRegister input = ToFloatRegister(test->input());

    MBasicBlock *ifTrue = test->ifTrue();
    MBasicBlock *ifFalse = test->ifFalse();

    masm.zeroDouble(fpTempRegister);
    // If 0, or NaN, the result is false.

    if (isNextBlock(ifFalse->lir())) {
        branchToBlock(input, fpTempRegister, ifTrue,
                      Assembler::DoubleNotEqual);
    } else {
        branchToBlock(input, fpTempRegister, ifFalse,
                      Assembler::DoubleEqualOrUnordered);
        jumpToBlock(ifTrue);
    }
}

void
CodeGeneratorPPC::visitTestFAndBranch(LTestFAndBranch *test)
{
    ADBlock("visitTestFAndBranch");
    // Same as visitTestDAndBranch.
    FloatRegister input = ToFloatRegister(test->input());

    MBasicBlock *ifTrue = test->ifTrue();
    MBasicBlock *ifFalse = test->ifFalse();

    masm.zeroDouble(fpTempRegister);
    // If 0, or NaN, the result is false.

    if (isNextBlock(ifFalse->lir())) {
        branchToBlock(input, fpTempRegister, ifTrue,
                      Assembler::DoubleNotEqual);
    } else {
        branchToBlock(input, fpTempRegister, ifFalse,
                      Assembler::DoubleEqualOrUnordered);
        jumpToBlock(ifTrue);
    }
}

void
CodeGeneratorPPC::visitCompareD(LCompareD *comp)
{
    ADBlock("visitCompareD");

    FloatRegister lhs = ToFloatRegister(comp->left());
    FloatRegister rhs = ToFloatRegister(comp->right());
    Register dest = ToRegister(comp->output());

    Assembler::DoubleCondition cond = JSOpToDoubleCondition(comp->mir()->jsop());
    masm.ma_cmp_set(cond, lhs, rhs, dest);
}

void
CodeGeneratorPPC::visitCompareF(LCompareF *comp)
{
    ADBlock("visitCompareF");
    // Same as visitCompareD.

    FloatRegister lhs = ToFloatRegister(comp->left());
    FloatRegister rhs = ToFloatRegister(comp->right());
    Register dest = ToRegister(comp->output());

    Assembler::DoubleCondition cond = JSOpToDoubleCondition(comp->mir()->jsop());
    masm.ma_cmp_set(cond, lhs, rhs, dest);
}


void
CodeGeneratorPPC::visitCompareDAndBranch(LCompareDAndBranch *comp)
{
    ADBlock("visitCompareDAndBranch");
    FloatRegister lhs = ToFloatRegister(comp->left());
    FloatRegister rhs = ToFloatRegister(comp->right());

    Assembler::DoubleCondition cond = JSOpToDoubleCondition(comp->cmpMir()->jsop());
    MBasicBlock *ifTrue = comp->ifTrue();
    MBasicBlock *ifFalse = comp->ifFalse();

    if (isNextBlock(ifFalse->lir())) {
        branchToBlock(lhs, rhs, ifTrue, cond);
    } else {
        branchToBlock(lhs, rhs, ifFalse,
                      Assembler::InvertCondition(cond));
        jumpToBlock(ifTrue);
    }
}

void
CodeGeneratorPPC::visitCompareFAndBranch(LCompareFAndBranch *comp)
{
    ADBlock("visitCompareFAndBranch");
    FloatRegister lhs = ToFloatRegister(comp->left());
    FloatRegister rhs = ToFloatRegister(comp->right());

    Assembler::DoubleCondition cond = JSOpToDoubleCondition(comp->cmpMir()->jsop());
    MBasicBlock *ifTrue = comp->ifTrue();
    MBasicBlock *ifFalse = comp->ifFalse();

    if (isNextBlock(ifFalse->lir())) {
        branchToBlock(lhs, rhs, ifTrue, cond);
    } else {
        branchToBlock(lhs, rhs, ifFalse,
                      Assembler::InvertCondition(cond));
        jumpToBlock(ifTrue);
    }
}

void
CodeGeneratorPPC::visitCompareB(LCompareB *lir)
{
    ADBlock("visitCompareB");
    MCompare *mir = lir->mir();

    const ValueOperand lhs = ToValue(lir, LCompareB::Lhs);
    const LAllocation *rhs = lir->rhs();
    const Register output = ToRegister(lir->output());

    MOZ_ASSERT(mir->jsop() == JSOP_STRICTEQ || mir->jsop() == JSOP_STRICTNE);
    Assembler::Condition cond = JSOpToCondition(mir->compareType(), mir->jsop());

    BufferOffset done, notBoolean;
    notBoolean = masm._bc(0,
        masm.ma_cmp(lhs.typeReg(), ImmType(JSVAL_TYPE_BOOLEAN),
        Assembler::NotEqual));
    {
        if (rhs->isConstant())
            masm.cmp32Set(cond, lhs.payloadReg(), Imm32(rhs->toConstant()->toBoolean()), output);
        else
            masm.cmp32Set(cond, lhs.payloadReg(), ToRegister(rhs), output);
        done = masm._b(0);
    }

    masm.bindSS(notBoolean);
    {
        masm.move32(Imm32(mir->jsop() == JSOP_STRICTNE), output);
    }

    masm.bindSS(done);
}

void
CodeGeneratorPPC::visitCompareBAndBranch(LCompareBAndBranch *lir)
{
    ADBlock("visitCompareBAndBranch");
    MCompare *mir = lir->cmpMir();
    const ValueOperand lhs = ToValue(lir, LCompareBAndBranch::Lhs);
    const LAllocation *rhs = lir->rhs();

    MOZ_ASSERT(mir->jsop() == JSOP_STRICTEQ || mir->jsop() == JSOP_STRICTNE);

    MBasicBlock *mirNotBoolean = (mir->jsop() == JSOP_STRICTEQ) ? lir->ifFalse() : lir->ifTrue();
    branchToBlock(lhs.typeReg(), ImmType(JSVAL_TYPE_BOOLEAN), mirNotBoolean, Assembler::NotEqual);

    Assembler::Condition cond = JSOpToCondition(mir->compareType(), mir->jsop());
    if (rhs->isConstant())
        emitBranch(lhs.payloadReg(), Imm32(rhs->toConstant()->toBoolean()), cond, lir->ifTrue(),
                   lir->ifFalse());
    else
        emitBranch(lhs.payloadReg(), ToRegister(rhs), cond, lir->ifTrue(), lir->ifFalse());
}

void
CodeGeneratorPPC::visitCompareBitwise(LCompareBitwise *lir)
{
    ADBlock("visitCompareBitwise");

    MCompare *mir = lir->mir();
    Assembler::Condition cond = JSOpToCondition(mir->compareType(), mir->jsop());
    const ValueOperand lhs = ToValue(lir, LCompareBitwise::LhsInput);
    const ValueOperand rhs = ToValue(lir, LCompareBitwise::RhsInput);
    const Register output = ToRegister(lir->output());

    MOZ_ASSERT(IsEqualityOp(mir->jsop()));
    BufferOffset done;

    // If output is not one of the payload registers, we can save a branch.
    // (Unfortunately we can't if it is, because if it turns out it's the same
    // type, it will be clobbered prior to the cmp32Set.)
    masm.cmpw(lhs.typeReg(), rhs.typeReg());
    if (output != lhs.payloadReg() && output != rhs.payloadReg())
    	masm.move32(Imm32(cond == Assembler::NotEqual), output);
    BufferOffset notEqual = masm._bc(0, Assembler::NotEqual);
    {
        masm.cmp32Set(cond, lhs.payloadReg(), rhs.payloadReg(), output);
        if (output == lhs.payloadReg() || output == rhs.payloadReg())
        	done = masm._b(0);
    }
    masm.bindSS(notEqual);
    if (output == lhs.payloadReg() || output == rhs.payloadReg()) {
    	masm.move32(Imm32(cond == Assembler::NotEqual), output);
	masm.bindSS(done);
    }
}

void
CodeGeneratorPPC::visitCompareBitwiseAndBranch(LCompareBitwiseAndBranch *lir)
{
    ADBlock("visitCompareBitwiseAndBranch");
    MCompare *mir = lir->cmpMir();
    Assembler::Condition cond = JSOpToCondition(mir->compareType(), mir->jsop());
    const ValueOperand lhs = ToValue(lir, LCompareBitwiseAndBranch::LhsInput);
    const ValueOperand rhs = ToValue(lir, LCompareBitwiseAndBranch::RhsInput);

    MOZ_ASSERT(mir->jsop() == JSOP_EQ || mir->jsop() == JSOP_STRICTEQ ||
               mir->jsop() == JSOP_NE || mir->jsop() == JSOP_STRICTNE);

    if (lhs == rhs) {
        // We've been asked to compare a ValueOperand with itself; see
        // visitCompareAndBranch for when that situation occurs. Likewise
        // this can also be statically predicted, because a VO is always a GPR
        // pair and so it can never NOT equal itself -- and this also solves
        // a number of icky test failures as well.
        JSOp what = mir->jsop();

        if (what == JSOP_EQ || what == JSOP_STRICTEQ) {
            if (!isNextBlock(lir->ifTrue()->lir())) {
                ispew("!!>> optimized GPR VO branch EQ lhs == rhs <<!!");
                // Don't branch directly. Let jumpToBlock handle loop
                // headers and such, which are considered non-trivial.
                jumpToBlock(lir->ifTrue());
            } else 
                ispew("!!>> elided GPR VO branch EQ lhs == rhs <<!!");
        } else {
            if (!isNextBlock(lir->ifFalse()->lir())) {
                ispew("!!>> optimized GPR VO branch NE lhs == rhs <<!!");
                jumpToBlock(lir->ifFalse());
            } else
                ispew("!!>> elided GPR VO branch NE lhs == rhs <<!!");
        }
        return;
    }

    MBasicBlock *notEqual = (cond == Assembler::Equal) ? lir->ifFalse() : lir->ifTrue();

    branchToBlock(lhs.typeReg(), rhs.typeReg(), notEqual, Assembler::NotEqual);
    emitBranch(lhs.payloadReg(), rhs.payloadReg(), cond, lir->ifTrue(), lir->ifFalse());
}

void
CodeGeneratorPPC::visitBitAndAndBranch(LBitAndAndBranch *lir)
{
    ADBlock("visitBitAndAndBranch");
    if (lir->right()->isConstant())
        masm.ma_and(tempRegister, ToRegister(lir->left()), Imm32(ToInt32(lir->right())));
    else
        masm.ma_and(tempRegister, ToRegister(lir->left()), ToRegister(lir->right()));
    emitBranch(tempRegister, tempRegister, Assembler::NonZero, lir->ifTrue(),
               lir->ifFalse());
}

void
CodeGeneratorPPC::visitAsmJSUInt32ToDouble(LAsmJSUInt32ToDouble *lir)
{
    ADBlock("XXX visitAsmJSUInt32ToDouble");
    masm.convertUInt32ToDouble(ToRegister(lir->input()), ToFloatRegister(lir->output()));
}

void
CodeGeneratorPPC::visitAsmJSUInt32ToFloat32(LAsmJSUInt32ToFloat32 *lir)
{
    ADBlock("XXX visitAsmJSUInt32ToFloat32");
    masm.convertUInt32ToFloat32(ToRegister(lir->input()), ToFloatRegister(lir->output()));
}

void
CodeGeneratorPPC::visitNotI(LNotI *ins)
{
    ADBlock("visitNotI");
    masm.cmp32Set(Assembler::Equal, ToRegister(ins->input()), Imm32(0),
                  ToRegister(ins->output()));
}

// Common code for visitNotD/F.
static void
FPRNot(FloatRegister in, Register dest, MacroAssembler &masm)
{
    // Since this operation is |not|, we want to set a bit if
    // the double is falsey, which means 0.0, -0.0 or NaN.
    masm.zeroDouble(ScratchDoubleReg);
    masm.fcmpu(in, ScratchDoubleReg);
    masm.x_li32(dest, 1); // default value, run while fcmpu is computing
    BufferOffset done = masm._bc(0, Assembler::DoubleEqualOrUnordered);
    masm.x_li32(dest, 0);
    masm.bindSS(done);
}
void
CodeGeneratorPPC::visitNotD(LNotD *ins)
{
    ADBlock("visitNotD");
    FPRNot(ToFloatRegister(ins->input()), ToRegister(ins->output()), masm);
}
void
CodeGeneratorPPC::visitNotF(LNotF *ins)
{
    ADBlock("visitNotF");
    FPRNot(ToFloatRegister(ins->input()), ToRegister(ins->output()), masm);
}

void
CodeGeneratorPPC::visitGuardShape(LGuardShape *guard)
{
    ADBlock("visitGuardShape");
    Register obj = ToRegister(guard->input());
    Register tmp = ToRegister(guard->tempInt());

    masm.loadPtr(Address(obj, JSObject::offsetOfShape()), tmp);
    bailoutCmpPtr(Assembler::NotEqual, tmp, ImmGCPtr(guard->mir()->shape()),
                  guard->snapshot());
}

void
CodeGeneratorPPC::visitGuardObjectGroup(LGuardObjectGroup *guard)
{
    ADBlock("visitGuardObjectGroup");
    Register obj = ToRegister(guard->input());
    Register tmp = ToRegister(guard->tempInt());

    masm.loadPtr(Address(obj, JSObject::offsetOfGroup()), tmp);
    Assembler::Condition cond = guard->mir()->bailOnEquality()
                                ? Assembler::Equal
                                : Assembler::NotEqual;
    bailoutCmpPtr(cond, tmp, ImmGCPtr(guard->mir()->group()), guard->snapshot());
}

void
CodeGeneratorPPC::visitGuardClass(LGuardClass *guard)
{
    ADBlock("visitGuardClass");
    Register obj = ToRegister(guard->input());
    Register tmp = ToRegister(guard->tempInt());

    masm.loadObjClass(obj, tmp);
    bailoutCmpPtr(Assembler::NotEqual, tmp, Imm32((uint32_t)guard->mir()->getClass()),
                  guard->snapshot());
}

void
CodeGeneratorPPC::generateInvalidateEpilogue()
{
    ADBlock("generateInvalidateEpilogue");

    // Ensure that there is enough space in the buffer for the OsiPoint
    // patching to occur. Otherwise, we could overwrite the invalidation
    // epilogue.
    for (size_t i = 0; i < sizeof(void *); i += Assembler::NopSize())
        masm.x_nop();

    masm.bind(&invalidate_);
    
    // Partially construct InvalidationBailoutStack, which will be finished up
    // in generateInvalidator in the trampoline.
    
    // 1. Push LR. This came from PatchWrite_NearCall, which used bctrl, so the
    // next PC after the OsiPoint is in LR. This serves as the osiPointReturnAddress.
    masm.x_mflr(tempRegister);
    masm.push(tempRegister);

    // 2. Push the Ion script onto the stack (when we determine what that
    // pointer is).
    invalidateEpilogueData_ = masm.pushWithPatch(ImmWord(uintptr_t(-1)));
    JitCode *thunk = gen->jitRuntime()->getInvalidationThunk();

    // 3. Call the invalidator, which will unwind the frame and continue.
    masm.branch(thunk);

    // We should never reach this point in JIT code -- the invalidation thunk
    // should pop the invalidated JS frame and return directly to its caller.
    //masm.assumeUnreachable("Should have returned directly to its caller instead of here.");
}

void
CodeGeneratorPPC::visitLoadTypedArrayElementStatic(LLoadTypedArrayElementStatic *ins)
{
    MOZ_CRASH("NYI");
}

void
CodeGeneratorPPC::visitStoreTypedArrayElementStatic(LStoreTypedArrayElementStatic *ins)
{
    MOZ_CRASH("NYI");
}

void
CodeGeneratorPPC::visitAsmJSCall(LAsmJSCall *ins)
{
    emitAsmJSCall(ins);
}

void
CodeGeneratorPPC::visitAsmJSLoadHeap(LAsmJSLoadHeap *ins)
{
	ADBlock("visitAsmJSLoadHeap");
	MOZ_CRASH("visitAsmJSLoadHeap WTF!");
#if(0)
#error must adjust for infallibility
    const MAsmJSLoadHeap *mir = ins->mir();
    const LAllocation *ptr = ins->ptr();
    const LDefinition *out = ins->output();

    bool isSigned;
    int size;
    bool isFloat = false;
    switch (mir->viewType()) {
      case Scalar::Int8:    isSigned = true;  size =  8; break;
      case Scalar::Uint8:   isSigned = false; size =  8; break;
      case Scalar::Int16:   isSigned = true;  size = 16; break;
      case Scalar::Uint16:  isSigned = false; size = 16; break;
      case Scalar::Int32:   isSigned = true;  size = 32; break;
      case Scalar::Uint32:  isSigned = false; size = 32; break;
      case Scalar::Float64: isFloat  = true;  size = 64; break;
      case Scalar::Float32: isFloat  = true;  size = 32; break;
      default: MOZ_CRASH("unexpected array type");
    }

    if (ptr->isConstant()) {
        MOZ_ASSERT(!mir->needsBoundsCheck());
        int32_t ptrImm = ptr->toConstant()->toInt32();
        MOZ_ASSERT(ptrImm >= 0);
        if (isFloat) {
            if (size == 32) {
                masm.loadFloat32(Address(HeapReg, ptrImm), ToFloatRegister(out));
            } else {
                masm.loadDouble(Address(HeapReg, ptrImm), ToFloatRegister(out));
            }
        }  else {
            masm.ma_load(ToRegister(out), Address(HeapReg, ptrImm),
                         static_cast<LoadStoreSize>(size), isSigned ? SignExtend : ZeroExtend);
        }
        return true;
    }

    Register ptrReg = ToRegister(ptr);

    if (!mir->needsBoundsCheck()) {
        if (isFloat) {
            if (size == 32) {
                masm.loadFloat32(BaseIndex(HeapReg, ptrReg, TimesOne), ToFloatRegister(out));
            } else {
                masm.loadDouble(BaseIndex(HeapReg, ptrReg, TimesOne), ToFloatRegister(out));
            }
        } else {
            masm.ma_load(ToRegister(out), BaseIndex(HeapReg, ptrReg, TimesOne),
                         static_cast<LoadStoreSize>(size), isSigned ? SignExtend : ZeroExtend);
        }
        return true;
    }

    BufferOffset bo = masm.ma_BoundsCheck(ScratchRegister);

    Label outOfRange;
    Label done;
    masm.ma_b(ptrReg, ScratchRegister, &outOfRange, Assembler::AboveOrEqual, ShortJump);
    // Offset is ok, let's load value.
    if (isFloat) {
        if (size == 32)
            masm.loadFloat32(BaseIndex(HeapReg, ptrReg, TimesOne), ToFloatRegister(out));
        else
            masm.loadDouble(BaseIndex(HeapReg, ptrReg, TimesOne), ToFloatRegister(out));
    } else {
        masm.ma_load(ToRegister(out), BaseIndex(HeapReg, ptrReg, TimesOne),
                     static_cast<LoadStoreSize>(size), isSigned ? SignExtend : ZeroExtend);
    }
    masm.ma_b(&done, ShortJump);
    masm.bind(&outOfRange);
    // Offset is out of range. Load default values.
    if (isFloat) {
        if (size == 32)
            masm.loadFloat32(Address(GlobalReg, AsmJSNaN32GlobalDataOffset - AsmJSGlobalRegBias),
                             ToFloatRegister(out));
        else
            masm.loadDouble(Address(GlobalReg, AsmJSNaN64GlobalDataOffset - AsmJSGlobalRegBias),
                            ToFloatRegister(out));
    } else {
        masm.move32(Imm32(0), ToRegister(out));
    }
    masm.bind(&done);

    masm.append(AsmJSHeapAccess(bo.getOffset()));
    return true;
#endif
}

void
CodeGeneratorPPC::visitAsmJSStoreHeap(LAsmJSStoreHeap *ins)
{
	ADBlock("visitAsmJSStoreHeap");
	MOZ_CRASH("visitAsmJSStoreHeap WTF!");
#if(0)
#error must adjust for infallibility
    const MAsmJSStoreHeap *mir = ins->mir();
    const LAllocation *value = ins->value();
    const LAllocation *ptr = ins->ptr();

    bool isSigned;
    int size;
    bool isFloat = false;
    switch (mir->viewType()) {
      case Scalar::Int8:    isSigned = true;  size =  8; break;
      case Scalar::Uint8:   isSigned = false; size =  8; break;
      case Scalar::Int16:   isSigned = true;  size = 16; break;
      case Scalar::Uint16:  isSigned = false; size = 16; break;
      case Scalar::Int32:   isSigned = true;  size = 32; break;
      case Scalar::Uint32:  isSigned = false; size = 32; break;
      case Scalar::Float64: isFloat  = true;  size = 64; break;
      case Scalar::Float32: isFloat  = true;  size = 32; break;
      default: MOZ_CRASH("unexpected array type");
    }

    if (ptr->isConstant()) {
        MOZ_ASSERT(!mir->needsBoundsCheck());
        int32_t ptrImm = ptr->toConstant()->toInt32();
        MOZ_ASSERT(ptrImm >= 0);

        if (isFloat) {
            if (size == 32) {
                masm.storeFloat32(ToFloatRegister(value), Address(HeapReg, ptrImm));
            } else {
                masm.storeDouble(ToFloatRegister(value), Address(HeapReg, ptrImm));
            }
        }  else {
            masm.ma_store(ToRegister(value), Address(HeapReg, ptrImm),
                          static_cast<LoadStoreSize>(size), isSigned ? SignExtend : ZeroExtend);
        }
        return true;
    }

    Register ptrReg = ToRegister(ptr);
    Address dstAddr(ptrReg, 0);

    if (!mir->needsBoundsCheck()) {
        if (isFloat) {
            if (size == 32) {
                masm.storeFloat32(ToFloatRegister(value), BaseIndex(HeapReg, ptrReg, TimesOne));
            } else
                masm.storeDouble(ToFloatRegister(value), BaseIndex(HeapReg, ptrReg, TimesOne));
        } else {
            masm.ma_store(ToRegister(value), BaseIndex(HeapReg, ptrReg, TimesOne),
                          static_cast<LoadStoreSize>(size), isSigned ? SignExtend : ZeroExtend);
        }
        return true;
    }

    BufferOffset bo = masm.ma_BoundsCheck(ScratchRegister);

    Label rejoin;
    masm.ma_b(ptrReg, ScratchRegister, &rejoin, Assembler::AboveOrEqual, ShortJump);

    // Offset is ok, let's store value.
    if (isFloat) {
        if (size == 32) {
            masm.storeFloat32(ToFloatRegister(value), BaseIndex(HeapReg, ptrReg, TimesOne));
        } else
            masm.storeDouble(ToFloatRegister(value), BaseIndex(HeapReg, ptrReg, TimesOne));
    } else {
        masm.ma_store(ToRegister(value), BaseIndex(HeapReg, ptrReg, TimesOne),
                      static_cast<LoadStoreSize>(size), isSigned ? SignExtend : ZeroExtend);
    }
    masm.bind(&rejoin);

    masm.append(AsmJSHeapAccess(bo.getOffset()));
    return true;
#endif
}

void
CodeGeneratorPPC::visitAsmJSCompareExchangeHeap(LAsmJSCompareExchangeHeap* ins)
{
	MOZ_CRASH("visitAsmJSCompareExchangeHeap(LAsmJSCompareExchangeHeap* ins) NYI");
}

void
CodeGeneratorPPC::visitAsmJSAtomicBinopHeap(LAsmJSAtomicBinopHeap* ins)
{
	MOZ_CRASH("visitAsmJSAtomicBinopHeap(LAsmJSAtomicBinopHeap* ins) NYI");
}

void
CodeGeneratorPPC::visitAsmJSPassStackArg(LAsmJSPassStackArg *ins)
{
	ADBlock("visitAsmJSPassStackArg");
	MOZ_CRASH("visitAsmJSPassStackArg WTF!");
#if(0)
#error must adjust for infallibility
    const MAsmJSPassStackArg *mir = ins->mir();
    if (ins->arg()->isConstant()) {
        masm.storePtr(ImmWord(ToInt32(ins->arg())), Address(StackPointer, mir->spOffset()));
    } else {
        if (ins->arg()->isGeneralReg()) {
            masm.storePtr(ToRegister(ins->arg()), Address(StackPointer, mir->spOffset()));
        } else {
            masm.storeDouble(ToFloatRegister(ins->arg()).doubleOverlay(0),
                             Address(StackPointer, mir->spOffset()));
        }
    }

    return true;
#endif
}

void
CodeGeneratorPPC::visitEffectiveAddress(LEffectiveAddress *ins)
{
    ADBlock("visitEffectiveAddress");
    const MEffectiveAddress *mir = ins->mir();
    Register base = ToRegister(ins->base());
    Register index = ToRegister(ins->index());
    Register output = ToRegister(ins->output());

    BaseIndex address(base, index, mir->scale(), mir->displacement());
    masm.computeEffectiveAddress(address, output);
}

void
CodeGeneratorPPC::visitAsmJSLoadGlobalVar(LAsmJSLoadGlobalVar *ins)
{
	ADBlock("visitAsmJSLoadGlobalVar");
	MOZ_CRASH("visitAsmJSLoadGlobalVar WTF!");
#if(0)
#error must adjust for infallibility
    const MAsmJSLoadGlobalVar *mir = ins->mir();
    unsigned addr = mir->globalDataOffset() - AsmJSGlobalRegBias;
    if (mir->type() == MIRType_Int32)
        masm.load32(Address(GlobalReg, addr), ToRegister(ins->output()));
    else if (mir->type() == MIRType_Float32)
        masm.loadFloat32(Address(GlobalReg, addr), ToFloatRegister(ins->output()));
    else
        masm.loadDouble(Address(GlobalReg, addr), ToFloatRegister(ins->output()));
    return true;
#endif
}

void
CodeGeneratorPPC::visitAsmJSStoreGlobalVar(LAsmJSStoreGlobalVar *ins)
{
	ADBlock("visitAsmJSStoreGlobalVar");
	MOZ_CRASH("visitAsmJSStoreGlobalVar WTF!");
#if(0)
#error must adjust for infallibility
    const MAsmJSStoreGlobalVar *mir = ins->mir();

    MIRType type = mir->value()->type();
    MOZ_ASSERT(IsNumberType(type));
    unsigned addr = mir->globalDataOffset() - AsmJSGlobalRegBias;
    if (mir->value()->type() == MIRType_Int32)
        masm.store32(ToRegister(ins->value()), Address(GlobalReg, addr));
    else if (mir->value()->type() == MIRType_Float32)
        masm.storeFloat32(ToFloatRegister(ins->value()), Address(GlobalReg, addr));
    else
        masm.storeDouble(ToFloatRegister(ins->value()), Address(GlobalReg, addr));
    return true;
#endif
}

void
CodeGeneratorPPC::visitAsmJSLoadFuncPtr(LAsmJSLoadFuncPtr *ins)
{
	ADBlock("visitAsmJSLoadFuncPtr");
	MOZ_CRASH("visitAsmJSLoadFuncPtr WTF!");
#if(0)
#error must adjust for infallibility
    const MAsmJSLoadFuncPtr *mir = ins->mir();

    Register index = ToRegister(ins->index());
    Register out = ToRegister(ins->output());
    unsigned addr = mir->globalDataOffset() - AsmJSGlobalRegBias;

    BaseIndex source(GlobalReg, index, TimesFour, addr);
    masm.load32(source, out);
    return true;
#endif
}

void
CodeGeneratorPPC::visitAsmJSLoadFFIFunc(LAsmJSLoadFFIFunc *ins)
{
	ADBlock("visitAsmJSLoadFFIFunc");
	MOZ_CRASH("visitAsmJSLoadFFIFunc WTF!");
#if(0)
#error must adjust for infallibility
    const MAsmJSLoadFFIFunc *mir = ins->mir();
    masm.loadPtr(Address(GlobalReg, mir->globalDataOffset() - AsmJSGlobalRegBias),
                 ToRegister(ins->output()));
    return true;
#endif
}

void
CodeGeneratorPPC::visitNegI(LNegI *ins)
{
    ADBlock("visitNegI");
    Register input = ToRegister(ins->input());
    Register output = ToRegister(ins->output());

    masm.neg(output, input);
}

void
CodeGeneratorPPC::visitNegD(LNegD *ins)
{
    ADBlock("visitNegD");
    FloatRegister input = ToFloatRegister(ins->input());
    FloatRegister output = ToFloatRegister(ins->output());

    masm.fneg(output, input);
}

void
CodeGeneratorPPC::visitNegF(LNegF *ins)
{
    ADBlock("visitNegD");
    FloatRegister input = ToFloatRegister(ins->input());
    FloatRegister output = ToFloatRegister(ins->output());

    masm.fneg(output, input);
}

void
CodeGeneratorPPC::setReturnDoubleRegs(LiveRegisterSet* regs)
{
    // This is kind of superfluous on PowerPC, but anyway,
    MOZ_ASSERT(ReturnFloat32Reg == ReturnDoubleReg);
    regs->add(ReturnDoubleReg);
    // No SIMD, because AltiVec registers are not FPRs.
}

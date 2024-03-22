/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_x64_LIR_x64_h
#define jit_x64_LIR_x64_h

namespace js {
namespace jit {

// Given an untyped input, guards on whether it's a specific type and returns
// the unboxed payload.
class LUnboxBase : public LInstructionHelper<1, 1, 0>
{
  public:
    explicit LUnboxBase(const LAllocation& input) {
        setOperand(0, input);
    }

    static const size_t Input = 0;

    MUnbox* mir() const {
        return mir_->toUnbox();
    }
};

class LUnbox : public LUnboxBase {
  public:
    LIR_HEADER(Unbox)

    explicit LUnbox(const LAllocation& input)
      : LUnboxBase(input)
    { }

    const char* extraName() const {
        return StringFromMIRType(mir()->type());
    }
};

class LUnboxFloatingPoint : public LUnboxBase {
    MIRType type_;

  public:
    LIR_HEADER(UnboxFloatingPoint)

    LUnboxFloatingPoint(const LAllocation& input, MIRType type)
      : LUnboxBase(input),
        type_(type)
    { }

    MIRType type() const {
        return type_;
    }
    const char* extraName() const {
        return StringFromMIRType(type_);
    }
};

// Convert a 32-bit unsigned integer to a double.
class LAsmJSUInt32ToDouble : public LInstructionHelper<1, 1, 0>
{
  public:
    LIR_HEADER(AsmJSUInt32ToDouble)

    explicit LAsmJSUInt32ToDouble(const LAllocation& input) {
        setOperand(0, input);
    }
};

// Convert a 32-bit unsigned integer to a float32.
class LAsmJSUInt32ToFloat32 : public LInstructionHelper<1, 1, 0>
{
  public:
    LIR_HEADER(AsmJSUInt32ToFloat32)

    explicit LAsmJSUInt32ToFloat32(const LAllocation& input) {
        setOperand(0, input);
    }
};

class LAsmJSLoadFuncPtr : public LInstructionHelper<1, 1, 1>
{
  public:
    LIR_HEADER(AsmJSLoadFuncPtr);
    LAsmJSLoadFuncPtr(const LAllocation& index, const LDefinition& temp) {
        setOperand(0, index);
        setTemp(0, temp);
    }
    MAsmJSLoadFuncPtr* mir() const {
        return mir_->toAsmJSLoadFuncPtr();
    }
    const LAllocation* index() {
        return getOperand(0);
    }
    const LDefinition* temp() {
        return getTemp(0);
    }
};

class LDivOrModI64 : public LBinaryMath<1>
{
  public:
    LIR_HEADER(DivOrModI64)

    LDivOrModI64(const LAllocation& lhs, const LAllocation& rhs, const LDefinition& temp) {
        setOperand(0, lhs);
        setOperand(1, rhs);
        setTemp(0, temp);
    }

    const LDefinition* remainder() {
        return getTemp(0);
    }

    MBinaryArithInstruction* mir() const {
        MOZ_ASSERT(mir_->isDiv() || mir_->isMod());
        return static_cast<MBinaryArithInstruction*>(mir_);
    }
    bool canBeDivideByZero() const {
        if (mir_->isMod())
            return mir_->toMod()->canBeDivideByZero();
        return mir_->toDiv()->canBeDivideByZero();
    }
    bool canBeNegativeOverflow() const {
        if (mir_->isMod())
            return mir_->toMod()->canBeNegativeDividend();
        return mir_->toDiv()->canBeNegativeOverflow();
    }
};

// This class performs a simple x86 'div', yielding either a quotient or
// remainder depending on whether this instruction is defined to output
// rax (quotient) or rdx (remainder).
class LUDivOrMod64 : public LBinaryMath<1>
{
  public:
    LIR_HEADER(UDivOrMod64);

    LUDivOrMod64(const LAllocation& lhs, const LAllocation& rhs, const LDefinition& temp) {
        setOperand(0, lhs);
        setOperand(1, rhs);
        setTemp(0, temp);
    }

    const LDefinition* remainder() {
        return getTemp(0);
    }

    const char* extraName() const {
        return mir()->isTruncated() ? "Truncated" : nullptr;
    }

    MBinaryArithInstruction* mir() const {
        MOZ_ASSERT(mir_->isDiv() || mir_->isMod());
        return static_cast<MBinaryArithInstruction*>(mir_);
    }

    bool canBeDivideByZero() const {
        if (mir_->isMod())
            return mir_->toMod()->canBeDivideByZero();
        return mir_->toDiv()->canBeDivideByZero();
    }
};

class LTruncateToInt64 : public LInstructionHelper<1, 1, 1>
{
  public:
    LIR_HEADER(TruncateToInt64);

    LTruncateToInt64(const LAllocation& in, const LDefinition& temp) {
        setOperand(0, in);
        setTemp(0, temp);
    }

    MTruncateToInt64* mir() const {
        return mir_->toTruncateToInt64();
    }

    const LDefinition* temp() {
        return getTemp(0);
    }
};

class LInt64ToFloatingPoint : public LInstructionHelper<1, INT64_PIECES, 0>
{
  public:
    LIR_HEADER(Int64ToFloatingPoint);

    explicit LInt64ToFloatingPoint(const LInt64Allocation& in) {
        setInt64Operand(0, in);
    }

    MInt64ToFloatingPoint* mir() const {
        return mir_->toInt64ToFloatingPoint();
    }

    const LDefinition* temp() {
        return getTemp(0);
    }
};

} // namespace jit
} // namespace js

#endif /* jit_x64_LIR_x64_h */

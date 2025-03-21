/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 *
 * Copyright 2015 Mozilla Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "asmjs/WasmTypes.h"

#include "fdlibm.h"

#include "jslibmath.h"
#include "jsmath.h"

#include "asmjs/WasmInstance.h"
#include "asmjs/WasmSerialize.h"
#include "jit/MacroAssembler.h"
#include "js/Conversions.h"
#include "vm/Interpreter.h"

#include "vm/Stack-inl.h"

using namespace js;
using namespace js::jit;
using namespace js::wasm;

void
Val::writePayload(uint8_t* dst)
{
    switch (type_) {
      case ValType::I32:
      case ValType::F32:
        memcpy(dst, &u.i32_, sizeof(u.i32_));
        return;
      case ValType::I64:
      case ValType::F64:
        memcpy(dst, &u.i64_, sizeof(u.i64_));
        return;
      case ValType::I8x16:
      case ValType::I16x8:
      case ValType::I32x4:
      case ValType::F32x4:
      case ValType::B8x16:
      case ValType::B16x8:
      case ValType::B32x4:
        memcpy(dst, &u, jit::Simd128DataSize);
        return;
      case ValType::Limit:
        MOZ_CRASH("Bad value type");
    }
}

#if defined(JS_CODEGEN_ARM)
extern "C" {

extern MOZ_EXPORT int64_t
__aeabi_idivmod(int, int);

extern MOZ_EXPORT int64_t
__aeabi_uidivmod(int, int);

}
#endif

static void
WasmReportOverRecursed()
{
    ReportOverRecursed(JSRuntime::innermostWasmActivation()->cx(), JSMSG_WASM_OVERRECURSED);
}

static bool
WasmHandleExecutionInterrupt()
{
    WasmActivation* activation = JSRuntime::innermostWasmActivation();
    bool success = CheckForInterrupt(activation->cx());

    // Preserve the invariant that having a non-null resumePC means that we are
    // handling an interrupt.  Note that resumePC has already been copied onto
    // the stack by the interrupt stub, so we can clear it before returning
    // to the stub.
    activation->setResumePC(nullptr);

    return success;
}

static void
HandleTrap(int32_t trapIndex)
{
    JSContext* cx = JSRuntime::innermostWasmActivation()->cx();

    MOZ_ASSERT(trapIndex < int32_t(Trap::Limit) && trapIndex >= 0);
    Trap trap = Trap(trapIndex);

    unsigned errorNumber;
    switch (trap) {
      case Trap::Unreachable:
        errorNumber = JSMSG_WASM_UNREACHABLE;
        break;
      case Trap::IntegerOverflow:
        errorNumber = JSMSG_WASM_INTEGER_OVERFLOW;
        break;
      case Trap::InvalidConversionToInteger:
        errorNumber = JSMSG_WASM_INVALID_CONVERSION;
        break;
      case Trap::IntegerDivideByZero:
        errorNumber = JSMSG_WASM_INT_DIVIDE_BY_ZERO;
        break;
      case Trap::BadIndirectCall:
        errorNumber = JSMSG_WASM_BAD_IND_CALL;
        break;
      case Trap::ImpreciseSimdConversion:
        errorNumber = JSMSG_SIMD_FAILED_CONVERSION;
        break;
      case Trap::OutOfBounds:
        errorNumber = JSMSG_BAD_INDEX;
        break;
      case Trap::UnalignedAccess:
        errorNumber = JSMSG_WASM_UNALIGNED_ACCESS;
        break;
      default:
        MOZ_CRASH("unexpected trap");
    }

    JS_ReportErrorNumber(cx, GetErrorMessage, nullptr, errorNumber);
}

static int32_t
CoerceInPlace_ToInt32(MutableHandleValue val)
{
    JSContext* cx = JSRuntime::innermostWasmActivation()->cx();

    int32_t i32;
    if (!ToInt32(cx, val, &i32))
        return false;
    val.set(Int32Value(i32));

    return true;
}

static int32_t
CoerceInPlace_ToNumber(MutableHandleValue val)
{
    JSContext* cx = JSRuntime::innermostWasmActivation()->cx();

    double dbl;
    if (!ToNumber(cx, val, &dbl))
        return false;
    val.set(DoubleValue(dbl));

    return true;
}

template <class F>
static inline void*
FuncCast(F* pf, ABIFunctionType type)
{
    void *pv = JS_FUNC_TO_DATA_PTR(void*, pf);
#ifdef JS_SIMULATOR
    pv = Simulator::RedirectNativeFunction(pv, type);
#endif
    return pv;
}

void*
wasm::AddressOf(SymbolicAddress imm, ExclusiveContext* cx)
{
    switch (imm) {
      case SymbolicAddress::Runtime:
        return cx->runtimeAddressForJit();
      case SymbolicAddress::RuntimeInterruptUint32:
        return cx->runtimeAddressOfInterruptUint32();
      case SymbolicAddress::StackLimit:
        return cx->stackLimitAddressForJitCode(StackForUntrustedScript);
      case SymbolicAddress::ReportOverRecursed:
        return FuncCast(WasmReportOverRecursed, Args_General0);
      case SymbolicAddress::HandleExecutionInterrupt:
        return FuncCast(WasmHandleExecutionInterrupt, Args_General0);
      case SymbolicAddress::HandleTrap:
        return FuncCast(HandleTrap, Args_General1);
      case SymbolicAddress::CallImport_Void:
        return FuncCast(Instance::callImport_void, Args_General3);
      case SymbolicAddress::CallImport_I32:
        return FuncCast(Instance::callImport_i32, Args_General3);
      case SymbolicAddress::CallImport_I64:
        return FuncCast(Instance::callImport_i64, Args_General3);
      case SymbolicAddress::CallImport_F64:
        return FuncCast(Instance::callImport_f64, Args_General3);
      case SymbolicAddress::CoerceInPlace_ToInt32:
        return FuncCast(CoerceInPlace_ToInt32, Args_General1);
      case SymbolicAddress::CoerceInPlace_ToNumber:
        return FuncCast(CoerceInPlace_ToNumber, Args_General1);
      case SymbolicAddress::ToInt32:
        return FuncCast<int32_t (double)>(JS::ToInt32, Args_Int_Double);
#if defined(JS_CODEGEN_ARM)
      case SymbolicAddress::aeabi_idivmod:
        return FuncCast(__aeabi_idivmod, Args_General2);
      case SymbolicAddress::aeabi_uidivmod:
        return FuncCast(__aeabi_uidivmod, Args_General2);
      case SymbolicAddress::AtomicCmpXchg:
        return FuncCast<int32_t (int32_t, int32_t, int32_t, int32_t)>(js::atomics_cmpxchg_asm_callout, Args_General4);
      case SymbolicAddress::AtomicXchg:
        return FuncCast<int32_t (int32_t, int32_t, int32_t)>(js::atomics_xchg_asm_callout, Args_General3);
      case SymbolicAddress::AtomicFetchAdd:
        return FuncCast<int32_t (int32_t, int32_t, int32_t)>(js::atomics_add_asm_callout, Args_General3);
      case SymbolicAddress::AtomicFetchSub:
        return FuncCast<int32_t (int32_t, int32_t, int32_t)>(js::atomics_sub_asm_callout, Args_General3);
      case SymbolicAddress::AtomicFetchAnd:
        return FuncCast<int32_t (int32_t, int32_t, int32_t)>(js::atomics_and_asm_callout, Args_General3);
      case SymbolicAddress::AtomicFetchOr:
        return FuncCast<int32_t (int32_t, int32_t, int32_t)>(js::atomics_or_asm_callout, Args_General3);
      case SymbolicAddress::AtomicFetchXor:
        return FuncCast<int32_t (int32_t, int32_t, int32_t)>(js::atomics_xor_asm_callout, Args_General3);
#endif
      case SymbolicAddress::ModD:
        return FuncCast(NumberMod, Args_Double_DoubleDouble);
      case SymbolicAddress::SinD:
#ifdef _WIN64
        // Workaround a VS 2013 sin issue, see math_sin_uncached.
        return FuncCast<double (double)>(js::math_sin_uncached, Args_Double_Double);
#else
        return FuncCast<double (double)>(sin, Args_Double_Double);
#endif
      case SymbolicAddress::CosD:
        return FuncCast<double (double)>(cos, Args_Double_Double);
      case SymbolicAddress::TanD:
        return FuncCast<double (double)>(tan, Args_Double_Double);
      case SymbolicAddress::ASinD:
        return FuncCast<double (double)>(fdlibm::asin, Args_Double_Double);
      case SymbolicAddress::ACosD:
        return FuncCast<double (double)>(fdlibm::acos, Args_Double_Double);
      case SymbolicAddress::ATanD:
        return FuncCast<double (double)>(fdlibm::atan, Args_Double_Double);
      case SymbolicAddress::CeilD:
        return FuncCast<double (double)>(fdlibm::ceil, Args_Double_Double);
      case SymbolicAddress::CeilF:
        return FuncCast<float (float)>(fdlibm::ceilf, Args_Float32_Float32);
      case SymbolicAddress::FloorD:
        return FuncCast<double (double)>(fdlibm::floor, Args_Double_Double);
      case SymbolicAddress::FloorF:
        return FuncCast<float (float)>(fdlibm::floorf, Args_Float32_Float32);
      case SymbolicAddress::TruncD:
        return FuncCast<double (double)>(fdlibm::trunc, Args_Double_Double);
      case SymbolicAddress::TruncF:
        return FuncCast<float (float)>(fdlibm::truncf, Args_Float32_Float32);
      case SymbolicAddress::NearbyIntD:
        return FuncCast<double (double)>(nearbyint, Args_Double_Double);
      case SymbolicAddress::NearbyIntF:
        return FuncCast<float (float)>(nearbyintf, Args_Float32_Float32);
      case SymbolicAddress::ExpD:
        return FuncCast<double (double)>(fdlibm::exp, Args_Double_Double);
      case SymbolicAddress::LogD:
        return FuncCast<double (double)>(fdlibm::log, Args_Double_Double);
      case SymbolicAddress::PowD:
        return FuncCast(ecmaPow, Args_Double_DoubleDouble);
      case SymbolicAddress::ATan2D:
        return FuncCast(ecmaAtan2, Args_Double_DoubleDouble);
      case SymbolicAddress::Limit:
        break;
    }

    MOZ_CRASH("Bad SymbolicAddress");
}

SignalUsage::SignalUsage(ExclusiveContext* cx)
  :
#ifdef ASMJS_MAY_USE_SIGNAL_HANDLERS_FOR_OOB
    // Signal-handling is only used to eliminate bounds checks when the OS page
    // size is an even divisor of the WebAssembly page size.
    forOOB(cx->canUseSignalHandlers() &&
           gc::SystemPageSize() <= PageSize &&
           PageSize % gc::SystemPageSize() == 0),
#else
    forOOB(false),
#endif
    forInterrupt(cx->canUseSignalHandlers())
{}

bool
SignalUsage::operator==(SignalUsage rhs) const
{
    return forOOB == rhs.forOOB && forInterrupt == rhs.forInterrupt;
}

static inline MOZ_MUST_USE bool
GetCPUID(uint32_t* cpuId)
{
    enum Arch {
        X86 = 0x1,
        X64 = 0x2,
        ARM = 0x3,
        MIPS = 0x4,
        MIPS64 = 0x5,
        ARCH_BITS = 3
    };

#if defined(JS_CODEGEN_X86)
    MOZ_ASSERT(uint32_t(jit::CPUInfo::GetSSEVersion()) <= (UINT32_MAX >> ARCH_BITS));
    *cpuId = X86 | (uint32_t(jit::CPUInfo::GetSSEVersion()) << ARCH_BITS);
    return true;
#elif defined(JS_CODEGEN_X64)
    MOZ_ASSERT(uint32_t(jit::CPUInfo::GetSSEVersion()) <= (UINT32_MAX >> ARCH_BITS));
    *cpuId = X64 | (uint32_t(jit::CPUInfo::GetSSEVersion()) << ARCH_BITS);
    return true;
#elif defined(JS_CODEGEN_ARM)
    MOZ_ASSERT(jit::GetARMFlags() <= (UINT32_MAX >> ARCH_BITS));
    *cpuId = ARM | (jit::GetARMFlags() << ARCH_BITS);
    return true;
#elif defined(JS_CODEGEN_MIPS32)
    MOZ_ASSERT(jit::GetMIPSFlags() <= (UINT32_MAX >> ARCH_BITS));
    *cpuId = MIPS | (jit::GetMIPSFlags() << ARCH_BITS);
    return true;
#elif defined(JS_CODEGEN_MIPS64)
    MOZ_ASSERT(jit::GetMIPSFlags() <= (UINT32_MAX >> ARCH_BITS));
    *cpuId = MIPS64 | (jit::GetMIPSFlags() << ARCH_BITS);
    return true;
#else
    return false;
#endif
}

MOZ_MUST_USE bool
Assumptions::init(SignalUsage usesSignal, JS::BuildIdOp buildIdOp)
{
    this->usesSignal = usesSignal;

    if (!GetCPUID(&cpuId))
        return false;

    if (!buildIdOp || !buildIdOp(&buildId))
        return false;

    return true;
}

bool
Assumptions::operator==(const Assumptions& rhs) const
{
    return usesSignal == rhs.usesSignal &&
           cpuId == rhs.cpuId &&
           buildId.length() == rhs.buildId.length() &&
           PodEqual(buildId.begin(), rhs.buildId.begin(), buildId.length()) &&
           newFormat == rhs.newFormat;
}

size_t
Assumptions::serializedSize() const
{
    return sizeof(usesSignal) +
           sizeof(uint32_t) +
           SerializedPodVectorSize(buildId) +
           sizeof(bool);
}

uint8_t*
Assumptions::serialize(uint8_t* cursor) const
{
    cursor = WriteBytes(cursor, &usesSignal, sizeof(usesSignal));
    cursor = WriteScalar<uint32_t>(cursor, cpuId);
    cursor = SerializePodVector(cursor, buildId);
    cursor = WriteScalar<bool>(cursor, newFormat);
    return cursor;
}

const uint8_t*
Assumptions::deserialize(const uint8_t* cursor)
{
    (cursor = ReadBytes(cursor, &usesSignal, sizeof(usesSignal))) &&
    (cursor = ReadScalar<uint32_t>(cursor, &cpuId)) &&
    (cursor = DeserializePodVector(cursor, &buildId)) &&
    (cursor = ReadScalar<bool>(cursor, &newFormat));
    return cursor;
}

size_t
Assumptions::sizeOfExcludingThis(MallocSizeOf mallocSizeOf) const
{
    return buildId.sizeOfExcludingThis(mallocSizeOf);
}

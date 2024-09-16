/*
 * Copyright 2013 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 *
 * This header provides some of the helpers (like std::enable_if_t) which will
 * become available with C++14 in the type_traits header (in the skstd
 * namespace). This header also provides several Skia specific additions such
 * as SK_WHEN and the sknonstd namespace.
 */

#ifndef SkTLogic_DEFINED
#define SkTLogic_DEFINED

#include <cstddef>
#include <stdint.h>
#include <utility>
#include <memory>
#include <algorithm>
#include <functional>

#ifdef MOZ_SKIA_AVOID_CXX11
#include "mozilla/Function.h"
#include "mozilla/Move.h"
#include "mozilla/TypeTraits.h"
#include "mozilla/UniquePtr.h"

namespace std {
  using mozilla::Forward;
  using mozilla::Move;
  #define forward Forward
  #define move Move

  // If we have 'using mozilla::function', we're going to collide with
  // 'std::function' on platforms that have it. Therefore we use a macro
  // work around.
  template<typename Signature>
  using moz_function = mozilla::function<Signature>;
  #define function moz_function

  typedef decltype(nullptr) moz_nullptr_t;
  #define nullptr_t moz_nullptr_t

  using mozilla::DefaultDelete;
  using mozilla::UniquePtr;
  #define default_delete DefaultDelete
  #define unique_ptr UniquePtr

  using mozilla::IsEnum;
  using mozilla::IsIntegral;
  using mozilla::FalseType;
  using mozilla::TrueType;
  #define is_enum IsEnum
  #define is_integral IsIntegral
  #define false_type FalseType
  #define true_type TrueType

#if SKIA_IMPLEMENTATION
  using mozilla::IntegralConstant;
  using mozilla::IsEmpty;
  using mozilla::IsPod;
  using mozilla::IsUnsigned;
  #define integral_constant IntegralConstant
  #define is_empty IsEmpty
  #define is_pod IsPod
  #define is_unsigned IsUnsigned
#endif
}

namespace skstd {

template <bool B> using bool_constant = mozilla::IntegralConstant<bool, B>;

template <bool B, typename T, typename F> using conditional_t = typename mozilla::Conditional<B, T, F>::Type;
template <bool B, typename T = void> using enable_if_t = typename mozilla::EnableIf<B, T>::Type;

template <typename From, typename To> using is_convertible = mozilla::IsConvertible<From, To>;
template <typename T> using remove_pointer_t = typename mozilla::RemovePointer<T>::Type;

template <typename T> struct underlying_type {
    using type = __underlying_type(T);
};
template <typename T> using underlying_type_t = typename skstd::underlying_type<T>::type;

}

#else /* !MOZ_SKIA_AVOID_CXX11 */

#include <type_traits>

namespace skstd {

template <bool B> using bool_constant = std::integral_constant<bool, B>;

template <bool B, typename T, typename F> using conditional_t = typename std::conditional<B, T, F>::type;
template <bool B, typename T = void> using enable_if_t = typename std::enable_if<B, T>::type;

template <typename T> using remove_const_t = typename std::remove_const<T>::type;
template <typename T> using remove_volatile_t = typename std::remove_volatile<T>::type;
template <typename T> using remove_cv_t = typename std::remove_cv<T>::type;
template <typename T> using remove_pointer_t = typename std::remove_pointer<T>::type;
template <typename T> using remove_reference_t = typename std::remove_reference<T>::type;
template <typename T> using remove_extent_t = typename std::remove_extent<T>::type;

// template<typename R, typename... Args> struct is_function<
//     R [calling-convention] (Args...[, ...]) [const] [volatile] [&|&&]> : std::true_type {};
// The cv and ref-qualified versions are strange types we're currently avoiding, so not supported.
// These aren't supported in msvc either until vs2015u2.
// On all platforms, variadic functions only exist in the c calling convention.
// mcvc 2013 introduced __vectorcall, but it wan't until 2015 that it was added to is_function.
template <typename> struct is_function : std::false_type {};
#if !defined(WIN32)
template <typename R, typename... Args> struct is_function<R(Args...)> : std::true_type {};
#else
template <typename R, typename... Args> struct is_function<R __cdecl (Args...)> : std::true_type {};
#if defined(_M_IX86)
template <typename R, typename... Args> struct is_function<R __stdcall (Args...)> : std::true_type {};
template <typename R, typename... Args> struct is_function<R __fastcall (Args...)> : std::true_type {};
#endif
#if defined(_MSC_VER) && (_M_IX86_FP >= 2 || defined(_M_AMD64) || defined(_M_X64))
template <typename R, typename... Args> struct is_function<R __vectorcall (Args...)> : std::true_type {};
#endif
#endif
template <typename R, typename... Args> struct is_function<R(Args..., ...)> : std::true_type {};

template <typename T> using add_const_t = typename std::add_const<T>::type;
template <typename T> using add_volatile_t = typename std::add_volatile<T>::type;
template <typename T> using add_cv_t = typename std::add_cv<T>::type;
template <typename T> using add_pointer_t = typename std::add_pointer<T>::type;
template <typename T> using add_lvalue_reference_t = typename std::add_lvalue_reference<T>::type;

template <typename... T> using common_type_t = typename std::common_type<T...>::type;

// Chromium currently requires gcc 4.8.2 or a recent clang compiler, but uses libstdc++4.6.4.
// Note that Precise actually uses libstdc++4.6.3.
// Unfortunately, libstdc++ STL before libstdc++4.7 do not define std::underlying_type.
// Newer gcc and clang compilers have __underlying_type which does not depend on runtime support.
// See https://gcc.gnu.org/onlinedocs/libstdc++/manual/abi.html for __GLIBCXX__ values.
// Unfortunately __GLIBCXX__ is a date, but no updates to versions before 4.7 are now anticipated.
#define SK_GLIBCXX_4_7_0 20120322
// Updates to versions before 4.7 but released after 4.7 was released.
#define SK_GLIBCXX_4_5_4 20120702
#define SK_GLIBCXX_4_6_4 20121127
#if defined(__GLIBCXX__) && (__GLIBCXX__ <  SK_GLIBCXX_4_7_0 || \
                             __GLIBCXX__ == SK_GLIBCXX_4_5_4 || \
                             __GLIBCXX__ == SK_GLIBCXX_4_6_4)
template <typename T> struct underlying_type {
    using type = __underlying_type(T);
};
#else
template <typename T> using underlying_type = std::underlying_type<T>;
#endif
template <typename T> using underlying_type_t = typename skstd::underlying_type<T>::type;

template <typename S, typename D,
          bool=std::is_void<S>::value || is_function<D>::value || std::is_array<D>::value>
struct is_convertible_detector {
    static const/*expr*/ bool value = std::is_void<D>::value;
};
template <typename S, typename D> struct is_convertible_detector<S, D, false> {
    using yes_type = uint8_t;
    using no_type = uint16_t;

    template <typename To> static void param_convertable_to(To);

    template <typename From, typename To>
    static decltype(param_convertable_to<To>(std::declval<From>()), yes_type()) convertible(int);

    template <typename, typename> static no_type convertible(...);

    static const/*expr*/ bool value = sizeof(convertible<S, D>(0)) == sizeof(yes_type);
};
// std::is_convertable is known to be broken (not work with incomplete types) in Android clang NDK.
// This is currently what prevents us from using std::unique_ptr.
template<typename S, typename D> struct is_convertible
    : bool_constant<is_convertible_detector<S, D>::value> {};

}  // namespace skstd

// The sknonstd namespace contains things we would like to be proposed and feel std-ish.
namespace sknonstd {

// The name 'copy' here is fraught with peril. In this case it means 'append', not 'overwrite'.
// Alternate proposed names are 'propagate', 'augment', or 'append' (and 'add', but already taken).
// std::experimental::propagate_const already exists for other purposes in TSv2.
// These also follow the <dest, source> pattern used by boost.
template <typename D, typename S> struct copy_const {
    using type = skstd::conditional_t<std::is_const<S>::value, skstd::add_const_t<D>, D>;
};
template <typename D, typename S> using copy_const_t = typename copy_const<D, S>::type;

template <typename D, typename S> struct copy_volatile {
    using type = skstd::conditional_t<std::is_volatile<S>::value, skstd::add_volatile_t<D>, D>;
};
template <typename D, typename S> using copy_volatile_t = typename copy_volatile<D, S>::type;

template <typename D, typename S> struct copy_cv {
    using type = copy_volatile_t<copy_const_t<D, S>, S>;
};
template <typename D, typename S> using copy_cv_t = typename copy_cv<D, S>::type;

// The name 'same' here means 'overwrite'.
// Alternate proposed names are 'replace', 'transfer', or 'qualify_from'.
// same_xxx<D, S> can be written as copy_xxx<remove_xxx_t<D>, S>
template <typename D, typename S> using same_const = copy_const<skstd::remove_const_t<D>, S>;
template <typename D, typename S> using same_const_t = typename same_const<D, S>::type;
template <typename D, typename S> using same_volatile =copy_volatile<skstd::remove_volatile_t<D>,S>;
template <typename D, typename S> using same_volatile_t = typename same_volatile<D, S>::type;
template <typename D, typename S> using same_cv = copy_cv<skstd::remove_cv_t<D>, S>;
template <typename D, typename S> using same_cv_t = typename same_cv<D, S>::type;

}  // namespace sknonstd

#endif /* MOZ_SKIA_AVOID_CXX11 */

// Just a pithier wrapper for enable_if_t.
#define SK_WHEN(condition, T) skstd::enable_if_t<!!(condition), T>

#endif

/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MediaTrackConstraints.h"

#include <limits>

namespace mozilla {

template<class ValueType>
template<class ConstrainRange>
void
NormalizedConstraintSet::Range<ValueType>::SetFrom(const ConstrainRange& aOther)
{
  if (aOther.mIdeal.WasPassed()) {
    mIdeal.emplace(aOther.mIdeal.Value());
  }
  if (aOther.mExact.WasPassed()) {
    mMin = aOther.mExact.Value();
    mMax = aOther.mExact.Value();
  } else {
    if (aOther.mMin.WasPassed()) {
      mMin = aOther.mMin.Value();
    }
    if (aOther.mMax.WasPassed()) {
      mMax = aOther.mMax.Value();
    }
  }
}

NormalizedConstraintSet::LongRange::LongRange(
    const dom::OwningLongOrConstrainLongRange& aOther, bool advanced)
: Range<int32_t>(1 + INT32_MIN, INT32_MAX) // +1 avoids Windows compiler bug
{
  if (aOther.IsLong()) {
    if (advanced) {
      mMin = mMax = aOther.GetAsLong();
    } else {
      mIdeal.emplace(aOther.GetAsLong());
    }
  } else {
    SetFrom(aOther.GetAsConstrainLongRange());
  }
}

NormalizedConstraintSet::DoubleRange::DoubleRange(
    const dom::OwningDoubleOrConstrainDoubleRange& aOther, bool advanced)
: Range<double>(-std::numeric_limits<double>::infinity(),
                std::numeric_limits<double>::infinity())
{
  if (aOther.IsDouble()) {
    if (advanced) {
      mMin = mMax = aOther.GetAsDouble();
    } else {
      mIdeal.emplace(aOther.GetAsDouble());
    }
  } else {
    SetFrom(aOther.GetAsConstrainDoubleRange());
  }
}

NormalizedConstraintSet::BooleanRange::BooleanRange(
    const dom::OwningBooleanOrConstrainBooleanParameters& aOther, bool advanced)
: Range<bool>(false, true)
{
  if (aOther.IsBoolean()) {
    if (advanced) {
      mMin = mMax = aOther.GetAsBoolean();
    } else {
      mIdeal.emplace(aOther.GetAsBoolean());
    }
  } else {
    const ConstrainBooleanParameters& r = aOther.GetAsConstrainBooleanParameters();
    if (r.mIdeal.WasPassed()) {
      mIdeal.emplace(r.mIdeal.Value());
    }
    if (r.mExact.WasPassed()) {
      mMin = r.mExact.Value();
      mMax = r.mExact.Value();
    }
  }
}

NormalizedConstraintSet::StringRange::StringRange(
    const dom::OwningStringOrStringSequenceOrConstrainDOMStringParameters& aOther,
    bool advanced)
{
  if (aOther.IsString()) {
    if (advanced) {
      mExact.insert(aOther.GetAsString());
    } else {
      mIdeal.insert(aOther.GetAsString());
    }
  } else if (aOther.IsStringSequence()) {
    if (advanced) {
      mExact.clear();
      for (auto& str : aOther.GetAsStringSequence()) {
        mExact.insert(str);
      }
    } else {
      mIdeal.clear();
      for (auto& str : aOther.GetAsStringSequence()) {
        mIdeal.insert(str);
      }
    }
  } else {
    SetFrom(aOther.GetAsConstrainDOMStringParameters());
  }
}

void
NormalizedConstraintSet::StringRange::SetFrom(
    const ConstrainDOMStringParameters& aOther)
{
  if (aOther.mIdeal.WasPassed()) {
    mIdeal.clear();
    if (aOther.mIdeal.Value().IsString()) {
      mIdeal.insert(aOther.mIdeal.Value().GetAsString());
    } else {
      for (auto& str : aOther.mIdeal.Value().GetAsStringSequence()) {
        mIdeal.insert(str);
      }
    }
  }
  if (aOther.mExact.WasPassed()) {
    mExact.clear();
    if (aOther.mExact.Value().IsString()) {
      mExact.insert(aOther.mExact.Value().GetAsString());
    } else {
      for (auto& str : aOther.mExact.Value().GetAsStringSequence()) {
        mIdeal.insert(str);
      }
    }
  }
}

auto
NormalizedConstraintSet::StringRange::Clamp(const ValueType& n) const -> ValueType
{
  if (!mExact.size()) {
    return n;
  }
  ValueType result;
  for (auto& entry : n) {
    if (mExact.find(entry) != mExact.end()) {
      result.insert(entry);
    }
  }
  return result;
}

bool
NormalizedConstraintSet::StringRange::Intersects(const StringRange& aOther) const
{
  if (!mExact.size() || !aOther.mExact.size()) {
    return true;
  }
  for (auto& entry : aOther.mExact) {
    if (mExact.find(entry) != mExact.end()) {
      return true;
    }
  }
  return false;
}

void
NormalizedConstraintSet::StringRange::Intersect(const StringRange& aOther)
{
  if (!aOther.mExact.size()) {
    return;
  }
  for (auto& entry : mExact) {
    if (aOther.mExact.find(entry) == aOther.mExact.end()) {
      mExact.erase(entry);
    }
  }
}

NormalizedConstraints::NormalizedConstraints(const dom::MediaTrackConstraints& aOther)
: NormalizedConstraintSet(aOther, false)
{
  if (aOther.mAdvanced.WasPassed()) {
    for (auto& entry : aOther.mAdvanced.Value()) {
      mAdvanced.AppendElement(NormalizedConstraintSet(entry, true));
    }
  }
}

FlattenedConstraints::FlattenedConstraints(const NormalizedConstraints& aOther)
: NormalizedConstraintSet(aOther)
{
  for (auto& set : aOther.mAdvanced) {
    // Must only apply compatible i.e. inherently non-overconstraining sets
    // This rule is pretty much why this code is centralized here.
    if (mWidth.Intersects(set.mWidth) &&
        mHeight.Intersects(set.mHeight) &&
        mFrameRate.Intersects(set.mFrameRate)) {
      mWidth.Intersect(set.mWidth);
      mHeight.Intersect(set.mHeight);
      mFrameRate.Intersect(set.mFrameRate);
    }
    if (mEchoCancellation.Intersects(set.mEchoCancellation)) {
        mEchoCancellation.Intersect(set.mEchoCancellation);
    }
    if (mMozNoiseSuppression.Intersects(set.mMozNoiseSuppression)) {
        mMozNoiseSuppression.Intersect(set.mMozNoiseSuppression);
    }
    if (mMozAutoGainControl.Intersects(set.mMozAutoGainControl)) {
        mMozAutoGainControl.Intersect(set.mMozAutoGainControl);
    }
  }
}

// MediaEngine helper
//
// The full algorithm for all devices. Sources that don't list capabilities
// need to fake it and hardcode some by populating mHardcodedCapabilities above.
//
// Fitness distance returned as integer math * 1000. Infinity = UINT32_MAX

// First, all devices have a minimum distance based on their deviceId.
// If you have no other constraints, use this one. Reused by all device types.

uint32_t
MediaConstraintsHelper::GetMinimumFitnessDistance(
    const dom::MediaTrackConstraintSet &aConstraints,
    bool aAdvanced,
    const nsString& aDeviceId)
{
  uint64_t distance =
    uint64_t(FitnessDistance(aDeviceId, aConstraints.mDeviceId, aAdvanced));

  // This function is modeled on MediaEngineCameraVideoSource::GetFitnessDistance
  // and will make more sense once more audio constraints are added.

  return uint32_t(std::min(distance, uint64_t(UINT32_MAX)));
}

template<class ValueType, class ConstrainRange>
/* static */ uint32_t
MediaConstraintsHelper::FitnessDistance(ValueType aN,
                                        const ConstrainRange& aRange)
{
  if ((aRange.mExact.WasPassed() && aRange.mExact.Value() != aN) ||
      (aRange.mMin.WasPassed() && aRange.mMin.Value() > aN) ||
      (aRange.mMax.WasPassed() && aRange.mMax.Value() < aN)) {
    return UINT32_MAX;
  }
  if (!aRange.mIdeal.WasPassed() || aN == aRange.mIdeal.Value()) {
    return 0;
  }
  return uint32_t(ValueType((std::abs(aN - aRange.mIdeal.Value()) * 1000) /
                            std::max(std::abs(aN), std::abs(aRange.mIdeal.Value()))));
}

// Binding code doesn't templatize well...

/*static*/ uint32_t
MediaConstraintsHelper::FitnessDistance(int32_t aN,
    const OwningLongOrConstrainLongRange& aConstraint, bool aAdvanced)
{
  if (aConstraint.IsLong()) {
    ConstrainLongRange range;
    (aAdvanced ? range.mExact : range.mIdeal).Construct(aConstraint.GetAsLong());
    return FitnessDistance(aN, range);
  } else {
    return FitnessDistance(aN, aConstraint.GetAsConstrainLongRange());
  }
}

/*static*/ uint32_t
MediaConstraintsHelper::FitnessDistance(double aN,
    const OwningDoubleOrConstrainDoubleRange& aConstraint,
    bool aAdvanced)
{
  if (aConstraint.IsDouble()) {
    ConstrainDoubleRange range;
    (aAdvanced ? range.mExact : range.mIdeal).Construct(aConstraint.GetAsDouble());
    return FitnessDistance(aN, range);
  } else {
    return FitnessDistance(aN, aConstraint.GetAsConstrainDoubleRange());
  }
}

// Fitness distance returned as integer math * 1000. Infinity = UINT32_MAX

/* static */ uint32_t
MediaConstraintsHelper::FitnessDistance(nsString aN,
                             const ConstrainDOMStringParameters& aParams)
{
  struct Func
  {
    static bool
    Contains(const OwningStringOrStringSequence& aStrings, nsString aN)
    {
      return aStrings.IsString() ? aStrings.GetAsString() == aN
                                 : aStrings.GetAsStringSequence().Contains(aN);
    }
  };

  if (aParams.mExact.WasPassed() && !Func::Contains(aParams.mExact.Value(), aN)) {
    return UINT32_MAX;
  }
  if (aParams.mIdeal.WasPassed() && !Func::Contains(aParams.mIdeal.Value(), aN)) {
    return 1000;
  }
  return 0;
}

/* static */ uint32_t
MediaConstraintsHelper::FitnessDistance(nsString aN,
    const OwningStringOrStringSequenceOrConstrainDOMStringParameters& aConstraint,
    bool aAdvanced)
{
  if (aConstraint.IsString()) {
    ConstrainDOMStringParameters params;
    if (aAdvanced) {
      params.mExact.Construct();
      params.mExact.Value().SetAsString() = aConstraint.GetAsString();
    } else {
      params.mIdeal.Construct();
      params.mIdeal.Value().SetAsString() = aConstraint.GetAsString();
    }
    return FitnessDistance(aN, params);
  } else if (aConstraint.IsStringSequence()) {
    ConstrainDOMStringParameters params;
    if (aAdvanced) {
      params.mExact.Construct();
      params.mExact.Value().SetAsStringSequence() = aConstraint.GetAsStringSequence();
    } else {
      params.mIdeal.Construct();
      params.mIdeal.Value().SetAsStringSequence() = aConstraint.GetAsStringSequence();
    }
    return FitnessDistance(aN, params);
  } else {
    return FitnessDistance(aN, aConstraint.GetAsConstrainDOMStringParameters());
  }
}

}

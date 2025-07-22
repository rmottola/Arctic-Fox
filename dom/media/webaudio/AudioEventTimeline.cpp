/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AudioEventTimeline.h"

#include "mozilla/ErrorResult.h"

static float LinearInterpolate(double t0, float v0, double t1, float v1, double t)
{
  return v0 + (v1 - v0) * ((t - t0) / (t1 - t0));
}

static float ExponentialInterpolate(double t0, float v0, double t1, float v1, double t)
{
  return v0 * powf(v1 / v0, (t - t0) / (t1 - t0));
}

static float ExponentialApproach(double t0, double v0, float v1, double timeConstant, double t)
{
  return v1 + (v0 - v1) * expf(-(t - t0) / timeConstant);
}

static float ExtractValueFromCurve(double startTime, float* aCurve, uint32_t aCurveLength, double duration, double t)
{
  if (t >= startTime + duration) {
    // After the duration, return the last curve value
    return aCurve[aCurveLength - 1];
  }
  double ratio = std::max((t - startTime) / duration, 0.0);
  if (ratio >= 1.0) {
    return aCurve[aCurveLength - 1];
  }
  return aCurve[uint32_t(aCurveLength * ratio)];
}

namespace mozilla {
namespace dom {

template <class ErrorResult> bool
AudioEventTimeline::ValidateEvent(AudioTimelineEvent& aEvent,
                                  ErrorResult& aRv)
{
  MOZ_ASSERT(NS_IsMainThread());

  auto TimeOf = [](const AudioTimelineEvent& aEvent) -> double {
    return aEvent.template Time<double>();
  };

  // Validate the event itself
  if (!WebAudioUtils::IsTimeValid(TimeOf(aEvent)) ||
      !WebAudioUtils::IsTimeValid(aEvent.mTimeConstant)) {
    aRv.Throw(NS_ERROR_DOM_SYNTAX_ERR);
    return false;
  }

  if (aEvent.mType == AudioTimelineEvent::SetValueCurve) {
    if (!aEvent.mCurve || !aEvent.mCurveLength) {
      aRv.Throw(NS_ERROR_DOM_SYNTAX_ERR);
      return false;
    }
    for (uint32_t i = 0; i < aEvent.mCurveLength; ++i) {
      if (!IsValid(aEvent.mCurve[i])) {
        aRv.Throw(NS_ERROR_DOM_SYNTAX_ERR);
        return false;
      }
    }
  }

  if (aEvent.mType == AudioTimelineEvent::SetTarget &&
      WebAudioUtils::FuzzyEqual(aEvent.mTimeConstant, 0.0)) {
    aRv.Throw(NS_ERROR_DOM_SYNTAX_ERR);
    return false;
  }

  bool timeAndValueValid = IsValid(aEvent.mValue) &&
                           IsValid(aEvent.mDuration);
  if (!timeAndValueValid) {
    aRv.Throw(NS_ERROR_DOM_SYNTAX_ERR);
    return false;
  }

  // Make sure that non-curve events don't fall within the duration of a
  // curve event.
  for (unsigned i = 0; i < mEvents.Length(); ++i) {
    if (mEvents[i].mType == AudioTimelineEvent::SetValueCurve &&
        !(aEvent.mType == AudioTimelineEvent::SetValueCurve &&
          TimeOf(aEvent) == TimeOf(mEvents[i])) &&
        TimeOf(mEvents[i]) <= TimeOf(aEvent) &&
        TimeOf(mEvents[i]) + mEvents[i].mDuration >= TimeOf(aEvent)) {
      aRv.Throw(NS_ERROR_DOM_SYNTAX_ERR);
      return false;
    }
  }

  // Make sure that curve events don't fall in a range which includes other
  // events.
  if (aEvent.mType == AudioTimelineEvent::SetValueCurve) {
    for (unsigned i = 0; i < mEvents.Length(); ++i) {
      // In case we have two curve at the same time
      if (mEvents[i].mType == AudioTimelineEvent::SetValueCurve &&
          TimeOf(mEvents[i]) == TimeOf(aEvent)) {
        continue;
      }
      if (TimeOf(mEvents[i]) > TimeOf(aEvent) &&
          TimeOf(mEvents[i]) < TimeOf(aEvent) + aEvent.mDuration) {
        aRv.Throw(NS_ERROR_DOM_SYNTAX_ERR);
        return false;
      }
    }
  }

  // Make sure that invalid values are not used for exponential curves
  if (aEvent.mType == AudioTimelineEvent::ExponentialRamp) {
    if (aEvent.mValue <= 0.f) {
      aRv.Throw(NS_ERROR_DOM_SYNTAX_ERR);
      return false;
    }
    const AudioTimelineEvent* previousEvent = GetPreviousEvent(TimeOf(aEvent));
    if (previousEvent) {
      if (previousEvent->mValue <= 0.f) {
        aRv.Throw(NS_ERROR_DOM_SYNTAX_ERR);
        return false;
      }
    } else {
      if (mValue <= 0.f) {
        aRv.Throw(NS_ERROR_DOM_SYNTAX_ERR);
        return false;
      }
    }
  }
  return true;
}
template bool
AudioEventTimeline::ValidateEvent(AudioTimelineEvent& aEvent,
                                  ErrorResult& aRv);

// This method computes the AudioParam value at a given time based on the event timeline
template<class TimeType> void
AudioEventTimeline::GetValuesAtTimeHelper(TimeType aTime, float* aBuffer,
                                          const size_t aSize)
{
  MOZ_ASSERT(aBuffer);
  MOZ_ASSERT(aSize);

  auto TimeOf = [](const AudioTimelineEvent& aEvent) -> TimeType {
    return aEvent.template Time<TimeType>();
  };

  size_t eventIndex = 0;
  const AudioTimelineEvent* previous = nullptr;

  // Let's remove old events except the last one: we need it to calculate some curves.
  CleanupEventsOlderThan(aTime);

  for (size_t bufferIndex = 0; bufferIndex < aSize; ++bufferIndex, ++aTime) {

    bool timeMatchesEventIndex = false;
    const AudioTimelineEvent* next;
    for (; ; ++eventIndex) {

      if (eventIndex >= mEvents.Length()) {
        next = nullptr;
        break;
      }

      next = &mEvents[eventIndex];
      if (aTime < TimeOf(*next)) {
        break;
      }

#ifdef DEBUG
      MOZ_ASSERT(next->mType == AudioTimelineEvent::SetValueAtTime ||
                 next->mType == AudioTimelineEvent::SetTarget ||
                 next->mType == AudioTimelineEvent::LinearRamp ||
                 next->mType == AudioTimelineEvent::ExponentialRamp ||
                 next->mType == AudioTimelineEvent::SetValueCurve);
#endif

      if (TimesEqual(aTime, TimeOf(*next))) {
        mLastComputedValue = mComputedValue;
        // Find the last event with the same time
        while (eventIndex < mEvents.Length() - 1 &&
               TimesEqual(aTime, TimeOf(mEvents[eventIndex + 1]))) {
          ++eventIndex;
        }
        timeMatchesEventIndex = true;
        break;
      }

      previous = next;
    }

    if (timeMatchesEventIndex) {
      // The time matches one of the events exactly.
      MOZ_ASSERT(TimesEqual(aTime, TimeOf(mEvents[eventIndex])));

      switch (mEvents[eventIndex].mType) {
        case AudioTimelineEvent::SetTarget:
          // SetTarget nodes can be handled no matter what their next node is
          // (if they have one).
          // Follow the curve, without regard to the next event, starting at
          // the last value of the last event.
          mComputedValue =
            ExponentialApproach(TimeOf(mEvents[eventIndex]),
                                mLastComputedValue, mEvents[eventIndex].mValue,
                                mEvents[eventIndex].mTimeConstant, aTime);
          break;
        case AudioTimelineEvent::SetValueCurve:
          // SetValueCurve events can be handled no matter what their event
          // node is (if they have one)
          mComputedValue =
            ExtractValueFromCurve(TimeOf(mEvents[eventIndex]),
                                  mEvents[eventIndex].mCurve,
                                  mEvents[eventIndex].mCurveLength,
                                  mEvents[eventIndex].mDuration, aTime);
          break;
        default:
          // For other event types
          mComputedValue = mEvents[eventIndex].mValue;
      }
    } else {
      mComputedValue = GetValuesAtTimeHelperInternal(aTime, previous, next);
    }

    aBuffer[bufferIndex] = mComputedValue;
  }
}
template void
AudioEventTimeline::GetValuesAtTimeHelper(double aTime, float* aBuffer,
                                          const size_t aSize);
template void
AudioEventTimeline::GetValuesAtTimeHelper(int64_t aTime, float* aBuffer,
                                          const size_t aSize);

template<class TimeType> float
AudioEventTimeline::GetValuesAtTimeHelperInternal(TimeType aTime,
                                    const AudioTimelineEvent* aPrevious,
                                    const AudioTimelineEvent* aNext)
{
  // If the requested time is before all of the existing events
  if (!aPrevious) {
     return mValue;
  }

  auto TimeOf = [](const AudioTimelineEvent* aEvent) -> TimeType {
    return aEvent->template Time<TimeType>();
  };

  // SetTarget nodes can be handled no matter what their next node is (if
  // they have one)
  if (aPrevious->mType == AudioTimelineEvent::SetTarget) {
    return ExponentialApproach(TimeOf(aPrevious),
                               mLastComputedValue, aPrevious->mValue,
                               aPrevious->mTimeConstant, aTime);
  }

  // SetValueCurve events can be handled no mattar what their next node is
  // (if they have one)
  if (aPrevious->mType == AudioTimelineEvent::SetValueCurve) {
    return ExtractValueFromCurve(TimeOf(aPrevious),
                                 aPrevious->mCurve, aPrevious->mCurveLength,
                                 aPrevious->mDuration, aTime);
  }

  // If the requested time is after all of the existing events
  if (!aNext) {
    switch (aPrevious->mType) {
      case AudioTimelineEvent::SetValueAtTime:
      case AudioTimelineEvent::LinearRamp:
      case AudioTimelineEvent::ExponentialRamp:
        // The value will be constant after the last event
        return aPrevious->mValue;
      case AudioTimelineEvent::SetValueCurve:
        return ExtractValueFromCurve(TimeOf(aPrevious),
                                     aPrevious->mCurve, aPrevious->mCurveLength,
                                     aPrevious->mDuration, aTime);
      case AudioTimelineEvent::SetTarget:
        MOZ_FALLTHROUGH_ASSERT("AudioTimelineEvent::SetTarget");
      case AudioTimelineEvent::SetValue:
      case AudioTimelineEvent::Cancel:
      case AudioTimelineEvent::Stream:
        MOZ_ASSERT(false, "Should have been handled earlier.");
    }
    MOZ_ASSERT(false, "unreached");
  }

  // Finally, handle the case where we have both a previous and a next event

  // First, handle the case where our range ends up in a ramp event
  switch (aNext->mType) {
  case AudioTimelineEvent::LinearRamp:
    return LinearInterpolate(TimeOf(aPrevious),
                             aPrevious->mValue,
                             TimeOf(aNext),
                             aNext->mValue, aTime);

  case AudioTimelineEvent::ExponentialRamp:
    return ExponentialInterpolate(TimeOf(aPrevious),
                                  aPrevious->mValue,
                                  TimeOf(aNext),
                                  aNext->mValue, aTime);

  case AudioTimelineEvent::SetValueAtTime:
  case AudioTimelineEvent::SetTarget:
  case AudioTimelineEvent::SetValueCurve:
    break;
  case AudioTimelineEvent::SetValue:
  case AudioTimelineEvent::Cancel:
  case AudioTimelineEvent::Stream:
    MOZ_ASSERT(false, "Should have been handled earlier.");
  }

  // Now handle all other cases
  switch (aPrevious->mType) {
  case AudioTimelineEvent::SetValueAtTime:
  case AudioTimelineEvent::LinearRamp:
  case AudioTimelineEvent::ExponentialRamp:
    // If the next event type is neither linear or exponential ramp, the
    // value is constant.
    return aPrevious->mValue;
  case AudioTimelineEvent::SetValueCurve:
    return ExtractValueFromCurve(TimeOf(aPrevious),
                                 aPrevious->mCurve, aPrevious->mCurveLength,
                                 aPrevious->mDuration, aTime);
  case AudioTimelineEvent::SetTarget:
    MOZ_FALLTHROUGH_ASSERT("AudioTimelineEvent::SetTarget");
  case AudioTimelineEvent::SetValue:
  case AudioTimelineEvent::Cancel:
  case AudioTimelineEvent::Stream:
    MOZ_ASSERT(false, "Should have been handled earlier.");
  }

  MOZ_ASSERT(false, "unreached");
  return 0.0f;
}
template float
AudioEventTimeline::GetValuesAtTimeHelperInternal(double aTime,
                                    const AudioTimelineEvent* aPrevious,
                                    const AudioTimelineEvent* aNext);
template float
AudioEventTimeline::GetValuesAtTimeHelperInternal(int64_t aTime,
                                    const AudioTimelineEvent* aPrevious,
                                    const AudioTimelineEvent* aNext);

const AudioTimelineEvent*
AudioEventTimeline::GetPreviousEvent(double aTime) const
{
  const AudioTimelineEvent* previous = nullptr;
  const AudioTimelineEvent* next = nullptr;

  auto TimeOf = [](const AudioTimelineEvent& aEvent) -> double {
    return aEvent.template Time<double>();
  };

  bool bailOut = false;
  for (unsigned i = 0; !bailOut && i < mEvents.Length(); ++i) {
    switch (mEvents[i].mType) {
    case AudioTimelineEvent::SetValueAtTime:
    case AudioTimelineEvent::SetTarget:
    case AudioTimelineEvent::LinearRamp:
    case AudioTimelineEvent::ExponentialRamp:
    case AudioTimelineEvent::SetValueCurve:
      if (aTime == TimeOf(mEvents[i])) {
        // Find the last event with the same time
        do {
          ++i;
        } while (i < mEvents.Length() &&
                 aTime == TimeOf(mEvents[i]));
        return &mEvents[i - 1];
      }
      previous = next;
      next = &mEvents[i];
      if (aTime < TimeOf(mEvents[i])) {
        bailOut = true;
      }
      break;
    default:
      MOZ_ASSERT(false, "unreached");
    }
  }
  // Handle the case where the time is past all of the events
  if (!bailOut) {
    previous = next;
  }

  return previous;
}

} // namespace dom
} // namespace mozilla


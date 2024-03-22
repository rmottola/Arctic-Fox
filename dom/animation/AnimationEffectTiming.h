/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_AnimationEffectTiming_h
#define mozilla_dom_AnimationEffectTiming_h

#include "mozilla/dom/AnimationEffectTimingReadOnly.h"
#include "mozilla/Attributes.h" // For MOZ_NON_OWNING_REF

namespace mozilla {
namespace dom {

class AnimationEffectTiming : public AnimationEffectTimingReadOnly
{
public:
  AnimationEffectTiming(const TimingParams& aTiming, KeyframeEffect* aEffect)
    : AnimationEffectTimingReadOnly(aTiming)
    , mEffect(aEffect) { }

  JSObject* WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto) override;

  void Unlink() override { mEffect = nullptr; }

  void SetEndDelay(double aEndDelay);
  void SetIterationStart(double aIterationStart, ErrorResult& aRv);
  void SetDuration(const UnrestrictedDoubleOrString& aDuration,
                   ErrorResult& aRv);

private:
  KeyframeEffect* MOZ_NON_OWNING_REF mEffect;
};

} // namespace dom
} // namespace mozilla

#endif // mozilla_dom_AnimationEffectTiming_h

/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: sw=2 ts=8 et :
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_RemoteContentController_h
#define mozilla_layers_RemoteContentController_h

#include "mozilla/layers/GeckoContentController.h"
#include "mozilla/layers/PAPZParent.h"

namespace mozilla {

namespace dom {
class TabParent;
}

namespace layers {

/**
 * RemoteContentController uses the PAPZ protocol to implement a
 * GeckoContentController for a browser living in a remote process.
 * Most of the member functions can be called on any thread, exceptions are
 * annotated in comments. The PAPZ protocol runs on the main thread (so all the
 * Recv* member functions do too).
 */
class RemoteContentController : public GeckoContentController
                              , public PAPZParent
{
  using GeckoContentController::TapType;
  using GeckoContentController::APZStateChange;

public:
  RemoteContentController();

  virtual ~RemoteContentController();

  virtual void RequestContentRepaint(const FrameMetrics& aFrameMetrics) override;

  virtual void HandleTap(TapType aTapType,
                         const LayoutDevicePoint& aPoint,
                         Modifiers aModifiers,
                         const ScrollableLayerGuid& aGuid,
                         uint64_t aInputBlockId) override;

  virtual void PostDelayedTask(already_AddRefed<Runnable> aTask, int aDelayMs) override;

  virtual bool IsRepaintThread() override;

  virtual void DispatchToRepaintThread(already_AddRefed<Runnable> aTask) override;

  virtual bool GetTouchSensitiveRegion(CSSRect* aOutRegion) override;

  virtual void NotifyAPZStateChange(const ScrollableLayerGuid& aGuid,
                                    APZStateChange aChange,
                                    int aArg) override;

  virtual void UpdateOverscrollVelocity(float aX, float aY, bool aIsRootContent) override;

  virtual void UpdateOverscrollOffset(float aX, float aY, bool aIsRootContent) override;

  virtual void SetScrollingRootContent(bool aIsRootContent) override;

  virtual void NotifyMozMouseScrollEvent(const FrameMetrics::ViewID& aScrollId,
                                         const nsString& aEvent) override;

  virtual void NotifyFlushComplete() override;

  virtual bool RecvUpdateHitRegion(const nsRegion& aRegion) override;

  virtual void ActorDestroy(ActorDestroyReason aWhy) override;

  virtual void Destroy() override;

private:
  MessageLoop* mCompositorThread;
  bool mCanSend;

  // Mutex protecting members below accessed from multiple threads.
  mozilla::Mutex mMutex;
  nsRegion mTouchSensitiveRegion;
};

} // namespace layers

} // namespace mozilla

#endif // mozilla_layers_RemoteContentController_h

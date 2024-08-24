/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=4 ts=8 et tw=80 : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_GeckoContentController_h
#define mozilla_layers_GeckoContentController_h

#include "FrameMetrics.h"               // for FrameMetrics, etc
#include "Units.h"                      // for CSSPoint, CSSRect, etc
#include "mozilla/Assertions.h"         // for MOZ_ASSERT_HELPER2
#include "mozilla/EventForwards.h"      // for Modifiers
#include "nsISupportsImpl.h"
#include "ThreadSafeRefcountingWithMainThreadDestruction.h"

class Task;

namespace mozilla {
namespace layers {

class GeckoContentController
{
public:
  /**
   * At least one class deriving from GeckoContentController needs to do
   * synchronous cleanup on the main thread, so we use
   * NS_INLINE_DECL_THREADSAFE_REFCOUNTING_WITH_MAIN_THREAD_DESTRUCTION.
   */
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING_WITH_MAIN_THREAD_DESTRUCTION(GeckoContentController)

  /**
   * Requests a paint of the given FrameMetrics |aFrameMetrics| from Gecko.
   * Implementations per-platform are responsible for actually handling this.
   * This method will always be called on the Gecko main thread.
   */
  virtual void RequestContentRepaint(const FrameMetrics& aFrameMetrics) = 0;

  /**
   * Requests handling of a double tap. |aPoint| is in CSS pixels, relative to
   * the current scroll offset. This should eventually round-trip back to
   * AsyncPanZoomController::ZoomToRect with the dimensions that we want to zoom
   * to.
   */
  virtual void HandleDoubleTap(const CSSPoint& aPoint,
                               Modifiers aModifiers,
                               const ScrollableLayerGuid& aGuid) = 0;

  /**
   * Requests handling a single tap. |aPoint| is in CSS pixels, relative to the
   * current scroll offset. This should simulate and send to content a mouse
   * button down, then mouse button up at |aPoint|.
   */
  virtual void HandleSingleTap(const CSSPoint& aPoint,
                               Modifiers aModifiers,
                               const ScrollableLayerGuid& aGuid) = 0;

  /**
   * Requests handling a long tap. |aPoint| is in CSS pixels, relative to the
   * current scroll offset.
   */
  virtual void HandleLongTap(const CSSPoint& aPoint,
                             Modifiers aModifiers,
                             const ScrollableLayerGuid& aGuid,
                             uint64_t aInputBlockId) = 0;

  /**
   * Schedules a runnable to run on the controller/UI thread at some time
   * in the future.
   * This method must always be called on the controller thread.
   */
  virtual void PostDelayedTask(Task* aTask, int aDelayMs) = 0;

  /**
   * APZ uses |FrameMetrics::mCompositionBounds| for hit testing. Sometimes,
   * widget code has knowledge of a touch-sensitive region that should
   * additionally constrain hit testing for all frames associated with the
   * controller. This method allows APZ to query the controller for such a
   * region. A return value of true indicates that the controller has such a
   * region, and it is returned in |aOutRegion|.
   * This method needs to be called on the main thread.
   * TODO: once bug 928833 is implemented, this should be removed, as
   * APZ can then get the correct touch-sensitive region for each frame
   * directly from the layer.
   */
  virtual bool GetTouchSensitiveRegion(CSSRect* aOutRegion)
  {
    return false;
  }

  enum APZStateChange {
    /**
     * APZ started modifying the view (including panning, zooming, and fling).
     */
    TransformBegin,
    /**
     * APZ finished modifying the view.
     */
    TransformEnd,
    /**
     * APZ started a touch.
     * |aArg| is 1 if touch can be a pan, 0 otherwise.
     */
    StartTouch,
    /**
     * APZ started a pan.
     */
    StartPanning,
    /**
     * APZ finished processing a touch.
     * |aArg| is 1 if touch was a click, 0 otherwise.
     */
    EndTouch,
    APZStateChangeSentinel
  };
  /**
   * General notices of APZ state changes for consumers.
   * |aGuid| identifies the APZC originating the state change.
   * |aChange| identifies the type of state change
   * |aArg| is used by some state changes to pass extra information (see
   *        the documentation for each state change above)
   */
  virtual void NotifyAPZStateChange(const ScrollableLayerGuid& aGuid,
                                    APZStateChange aChange,
                                    int aArg = 0) {}

  /**
   * Notify content of a MozMouseScrollFailed event.
   */
  virtual void NotifyMozMouseScrollEvent(const FrameMetrics::ViewID& aScrollId, const nsString& aEvent)
  {}

  /**
   * Notify content that the repaint requests have been flushed.
   */
  virtual void NotifyFlushComplete() = 0;

  virtual void UpdateOverscrollVelocity(const float aX, const float aY) {}
  virtual void UpdateOverscrollOffset(const float aX, const float aY) {}
  virtual void SetScrollingRootContent(const bool isRootContent) {}

  GeckoContentController() {}
  virtual void ChildAdopted() {}
  /**
   * Needs to be called on the main thread.
   */
  virtual void Destroy() {}

protected:
  // Protected destructor, to discourage deletion outside of Release():
  virtual ~GeckoContentController() {}
};

} // namespace layers
} // namespace mozilla

#endif // mozilla_layers_GeckoContentController_h

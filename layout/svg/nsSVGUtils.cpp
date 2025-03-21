/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Main header first:
// This is also necessary to ensure our definition of M_SQRT1_2 is picked up
#include "nsSVGUtils.h"
#include <algorithm>

// Keep others in (case-insensitive) order:
#include "gfx2DGlue.h"
#include "gfxContext.h"
#include "gfxMatrix.h"
#include "gfxPlatform.h"
#include "gfxRect.h"
#include "gfxUtils.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/PatternHelpers.h"
#include "mozilla/Preferences.h"
#include "nsCSSClipPathInstance.h"
#include "nsCSSFrameConstructor.h"
#include "nsDisplayList.h"
#include "nsFilterInstance.h"
#include "nsFrameList.h"
#include "nsGkAtoms.h"
#include "nsIContent.h"
#include "nsIDocument.h"
#include "nsIFrame.h"
#include "nsIPresShell.h"
#include "nsISVGChildFrame.h"
#include "nsLayoutUtils.h"
#include "nsPresContext.h"
#include "nsStyleCoord.h"
#include "nsStyleStruct.h"
#include "nsSVGClipPathFrame.h"
#include "nsSVGContainerFrame.h"
#include "nsSVGEffects.h"
#include "nsSVGFilterPaintCallback.h"
#include "nsSVGForeignObjectFrame.h"
#include "gfxSVGGlyphs.h"
#include "nsSVGInnerSVGFrame.h"
#include "nsSVGIntegrationUtils.h"
#include "nsSVGLength2.h"
#include "nsSVGMaskFrame.h"
#include "nsSVGOuterSVGFrame.h"
#include "mozilla/dom/SVGClipPathElement.h"
#include "mozilla/dom/SVGPathElement.h"
#include "nsSVGPathGeometryElement.h"
#include "nsSVGPathGeometryFrame.h"
#include "nsSVGPaintServerFrame.h"
#include "mozilla/dom/SVGSVGElement.h"
#include "nsTextFrame.h"
#include "SVGContentUtils.h"
#include "SVGTextFrame.h"
#include "mozilla/unused.h"

using namespace mozilla;
using namespace mozilla::dom;
using namespace mozilla::gfx;

static bool sSVGPathCachingEnabled;
static bool sSVGDisplayListHitTestingEnabled;
static bool sSVGDisplayListPaintingEnabled;
static bool sSVGNewGetBBoxEnabled;

bool
NS_SVGPathCachingEnabled()
{
  return sSVGPathCachingEnabled;
}

bool
NS_SVGDisplayListHitTestingEnabled()
{
  return sSVGDisplayListHitTestingEnabled;
}

bool
NS_SVGDisplayListPaintingEnabled()
{
  return sSVGDisplayListPaintingEnabled;
}

bool
NS_SVGNewGetBBoxEnabled()
{
  return sSVGNewGetBBoxEnabled;
}


// we only take the address of this:
static mozilla::gfx::UserDataKey sSVGAutoRenderStateKey;

SVGAutoRenderState::SVGAutoRenderState(DrawTarget* aDrawTarget
                                       MOZ_GUARD_OBJECT_NOTIFIER_PARAM_IN_IMPL)
  : mDrawTarget(aDrawTarget)
  , mOriginalRenderState(nullptr)
  , mPaintingToWindow(false)
{
  MOZ_GUARD_OBJECT_NOTIFIER_INIT;
  mOriginalRenderState =
    aDrawTarget->RemoveUserData(&sSVGAutoRenderStateKey);
  // We always remove ourselves from aContext before it dies, so
  // passing nullptr as the destroy function is okay.
  aDrawTarget->AddUserData(&sSVGAutoRenderStateKey, this, nullptr);
}

SVGAutoRenderState::~SVGAutoRenderState()
{
  mDrawTarget->RemoveUserData(&sSVGAutoRenderStateKey);
  if (mOriginalRenderState) {
    mDrawTarget->AddUserData(&sSVGAutoRenderStateKey,
                             mOriginalRenderState, nullptr);
  }
}

void
SVGAutoRenderState::SetPaintingToWindow(bool aPaintingToWindow)
{
  mPaintingToWindow = aPaintingToWindow;
}

/* static */ bool
SVGAutoRenderState::IsPaintingToWindow(DrawTarget* aDrawTarget)
{
  void *state = aDrawTarget->GetUserData(&sSVGAutoRenderStateKey);
  if (state) {
    return static_cast<SVGAutoRenderState*>(state)->mPaintingToWindow;
  }
  return false;
}

void
nsSVGUtils::Init()
{
  Preferences::AddBoolVarCache(&sSVGPathCachingEnabled,
                               "svg.path-caching.enabled");

  Preferences::AddBoolVarCache(&sSVGDisplayListHitTestingEnabled,
                               "svg.display-lists.hit-testing.enabled");

  Preferences::AddBoolVarCache(&sSVGDisplayListPaintingEnabled,
                               "svg.display-lists.painting.enabled");

  Preferences::AddBoolVarCache(&sSVGNewGetBBoxEnabled,
                               "svg.new-getBBox.enabled");
}

nsSVGDisplayContainerFrame*
nsSVGUtils::GetNearestSVGViewport(nsIFrame *aFrame)
{
  NS_ASSERTION(aFrame->IsFrameOfType(nsIFrame::eSVG), "SVG frame expected");
  if (aFrame->GetType() == nsGkAtoms::svgOuterSVGFrame) {
    return nullptr;
  }
  while ((aFrame = aFrame->GetParent())) {
    NS_ASSERTION(aFrame->IsFrameOfType(nsIFrame::eSVG), "SVG frame expected");
    if (aFrame->GetType() == nsGkAtoms::svgInnerSVGFrame ||
        aFrame->GetType() == nsGkAtoms::svgOuterSVGFrame) {
      return do_QueryFrame(aFrame);
    }
  }
  NS_NOTREACHED("This is not reached. It's only needed to compile.");
  return nullptr;
}

nsRect
nsSVGUtils::GetPostFilterVisualOverflowRect(nsIFrame *aFrame,
                                            const nsRect &aPreFilterRect)
{
  MOZ_ASSERT(aFrame->GetStateBits() & NS_FRAME_SVG_LAYOUT,
             "Called on invalid frame type");

  nsSVGFilterProperty *property = nsSVGEffects::GetFilterProperty(aFrame);
  if (!property || !property->ReferencesValidResources()) {
    return aPreFilterRect;
  }

  return nsFilterInstance::GetPostFilterBounds(aFrame, nullptr, &aPreFilterRect);
}

bool
nsSVGUtils::OuterSVGIsCallingReflowSVG(nsIFrame *aFrame)
{
  return GetOuterSVGFrame(aFrame)->IsCallingReflowSVG();
}

bool
nsSVGUtils::AnyOuterSVGIsCallingReflowSVG(nsIFrame* aFrame)
{
  nsSVGOuterSVGFrame* outer = GetOuterSVGFrame(aFrame);
  do {
    if (outer->IsCallingReflowSVG()) {
      return true;
    }
    outer = GetOuterSVGFrame(outer->GetParent());
  } while (outer);
  return false;
}

void
nsSVGUtils::ScheduleReflowSVG(nsIFrame *aFrame)
{
  MOZ_ASSERT(aFrame->IsFrameOfType(nsIFrame::eSVG),
             "Passed bad frame!");

  // If this is triggered, the callers should be fixed to call us before
  // ReflowSVG is called. If we try to mark dirty bits on frames while we're
  // in the process of removing them, things will get messed up.
  NS_ASSERTION(!OuterSVGIsCallingReflowSVG(aFrame),
               "Do not call under nsISVGChildFrame::ReflowSVG!");

  // We don't call nsSVGEffects::InvalidateRenderingObservers here because
  // we should only be called under InvalidateAndScheduleReflowSVG (which
  // calls InvalidateBounds) or nsSVGDisplayContainerFrame::InsertFrames
  // (at which point the frame has no observers).

  if (aFrame->GetStateBits() & NS_FRAME_IS_NONDISPLAY) {
    return;
  }

  if (aFrame->GetStateBits() &
      (NS_FRAME_IS_DIRTY | NS_FRAME_FIRST_REFLOW)) {
    // Nothing to do if we're already dirty, or if the outer-<svg>
    // hasn't yet had its initial reflow.
    return;
  }

  nsSVGOuterSVGFrame *outerSVGFrame = nullptr;

  // We must not add dirty bits to the nsSVGOuterSVGFrame or else
  // PresShell::FrameNeedsReflow won't work when we pass it in below.
  if (aFrame->GetStateBits() & NS_STATE_IS_OUTER_SVG) {
    outerSVGFrame = static_cast<nsSVGOuterSVGFrame*>(aFrame);
  } else {
    aFrame->AddStateBits(NS_FRAME_IS_DIRTY);

    nsIFrame *f = aFrame->GetParent();
    while (f && !(f->GetStateBits() & NS_STATE_IS_OUTER_SVG)) {
      if (f->GetStateBits() &
          (NS_FRAME_IS_DIRTY | NS_FRAME_HAS_DIRTY_CHILDREN)) {
        return;
      }
      f->AddStateBits(NS_FRAME_HAS_DIRTY_CHILDREN);
      f = f->GetParent();
      MOZ_ASSERT(f->IsFrameOfType(nsIFrame::eSVG),
                 "NS_STATE_IS_OUTER_SVG check above not valid!");
    }

    outerSVGFrame = static_cast<nsSVGOuterSVGFrame*>(f);

    MOZ_ASSERT(outerSVGFrame &&
               outerSVGFrame->GetType() == nsGkAtoms::svgOuterSVGFrame,
               "Did not find nsSVGOuterSVGFrame!");
  }

  if (outerSVGFrame->GetStateBits() & NS_FRAME_IN_REFLOW) {
    // We're currently under an nsSVGOuterSVGFrame::Reflow call so there is no
    // need to call PresShell::FrameNeedsReflow, since we have an
    // nsSVGOuterSVGFrame::DidReflow call pending.
    return;
  }

  nsFrameState dirtyBit =
    (outerSVGFrame == aFrame ? NS_FRAME_IS_DIRTY : NS_FRAME_HAS_DIRTY_CHILDREN);

  aFrame->PresContext()->PresShell()->FrameNeedsReflow(
    outerSVGFrame, nsIPresShell::eResize, dirtyBit);
}

bool
nsSVGUtils::NeedsReflowSVG(nsIFrame *aFrame)
{
  MOZ_ASSERT(aFrame->IsFrameOfType(nsIFrame::eSVG),
             "SVG uses bits differently!");

  // The flags we test here may change, hence why we have this separate
  // function.
  return NS_SUBTREE_DIRTY(aFrame);
}

void
nsSVGUtils::NotifyAncestorsOfFilterRegionChange(nsIFrame *aFrame)
{
  MOZ_ASSERT(!(aFrame->GetStateBits() & NS_STATE_IS_OUTER_SVG),
             "Not expecting to be called on the outer SVG Frame");

  aFrame = aFrame->GetParent();

  while (aFrame) {
    if (aFrame->GetStateBits() & NS_STATE_IS_OUTER_SVG)
      return;

    nsSVGFilterProperty *property = nsSVGEffects::GetFilterProperty(aFrame);
    if (property) {
      property->Invalidate();
    }
    aFrame = aFrame->GetParent();
  }
}

Size
nsSVGUtils::GetContextSize(const nsIFrame* aFrame)
{
  Size size;

  MOZ_ASSERT(aFrame->GetContent()->IsSVGElement(), "bad cast");
  const nsSVGElement* element = static_cast<nsSVGElement*>(aFrame->GetContent());

  SVGSVGElement* ctx = element->GetCtx();
  if (ctx) {
    size.width = ctx->GetLength(SVGContentUtils::X);
    size.height = ctx->GetLength(SVGContentUtils::Y);
  }
  return size;
}

float
nsSVGUtils::ObjectSpace(const gfxRect &aRect, const nsSVGLength2 *aLength)
{
  float axis;

  switch (aLength->GetCtxType()) {
  case SVGContentUtils::X:
    axis = aRect.Width();
    break;
  case SVGContentUtils::Y:
    axis = aRect.Height();
    break;
  case SVGContentUtils::XY:
    axis = float(SVGContentUtils::ComputeNormalizedHypotenuse(
                   aRect.Width(), aRect.Height()));
    break;
  default:
    NS_NOTREACHED("unexpected ctx type");
    axis = 0.0f;
    break;
  }
  if (aLength->IsPercentage()) {
    // Multiply first to avoid precision errors:
    return axis * aLength->GetAnimValInSpecifiedUnits() / 100;
  }
  return aLength->GetAnimValue(static_cast<SVGSVGElement*>(nullptr)) * axis;
}

float
nsSVGUtils::UserSpace(nsSVGElement *aSVGElement, const nsSVGLength2 *aLength)
{
  return aLength->GetAnimValue(aSVGElement);
}

float
nsSVGUtils::UserSpace(nsIFrame *aNonSVGContext, const nsSVGLength2 *aLength)
{
  return aLength->GetAnimValue(aNonSVGContext);
}

float
nsSVGUtils::UserSpace(const UserSpaceMetrics& aMetrics, const nsSVGLength2 *aLength)
{
  return aLength->GetAnimValue(aMetrics);
}

nsSVGOuterSVGFrame *
nsSVGUtils::GetOuterSVGFrame(nsIFrame *aFrame)
{
  while (aFrame) {
    if (aFrame->GetStateBits() & NS_STATE_IS_OUTER_SVG) {
      return static_cast<nsSVGOuterSVGFrame*>(aFrame);
    }
    aFrame = aFrame->GetParent();
  }

  return nullptr;
}

nsIFrame*
nsSVGUtils::GetOuterSVGFrameAndCoveredRegion(nsIFrame* aFrame, nsRect* aRect)
{
  nsISVGChildFrame* svg = do_QueryFrame(aFrame);
  if (!svg)
    return nullptr;
  nsSVGOuterSVGFrame* outer = GetOuterSVGFrame(aFrame);
  if (outer == svg) {
    return nullptr;
  }
  nsMargin bp = outer->GetUsedBorderAndPadding();
  *aRect = ((aFrame->GetStateBits() & NS_FRAME_IS_NONDISPLAY) ?
             nsRect(0, 0, 0, 0) : svg->GetCoveredRegion()) +
                 nsPoint(bp.left, bp.top);
  return outer;
}

gfxMatrix
nsSVGUtils::GetCanvasTM(nsIFrame *aFrame)
{
  // XXX yuck, we really need a common interface for GetCanvasTM

  if (!aFrame->IsFrameOfType(nsIFrame::eSVG)) {
    return nsSVGIntegrationUtils::GetCSSPxToDevPxMatrix(aFrame);
  }

  nsIAtom* type = aFrame->GetType();
  if (type == nsGkAtoms::svgForeignObjectFrame) {
    return static_cast<nsSVGForeignObjectFrame*>(aFrame)->GetCanvasTM();
  }
  if (type == nsGkAtoms::svgOuterSVGFrame) {
    return nsSVGIntegrationUtils::GetCSSPxToDevPxMatrix(aFrame);
  }

  nsSVGContainerFrame *containerFrame = do_QueryFrame(aFrame);
  if (containerFrame) {
    return containerFrame->GetCanvasTM();
  }

  return static_cast<nsSVGPathGeometryFrame*>(aFrame)->GetCanvasTM();
}

gfxMatrix
nsSVGUtils::GetUserToCanvasTM(nsIFrame *aFrame)
{
  nsISVGChildFrame* svgFrame = do_QueryFrame(aFrame);
  NS_ASSERTION(svgFrame, "bad frame");

  gfxMatrix tm;
  if (svgFrame) {
    nsSVGElement *content = static_cast<nsSVGElement*>(aFrame->GetContent());
    tm = content->PrependLocalTransformsTo(
                    GetCanvasTM(aFrame->GetParent()),
                    eUserSpaceToParent);
  }
  return tm;
}

void 
nsSVGUtils::NotifyChildrenOfSVGChange(nsIFrame *aFrame, uint32_t aFlags)
{
  for (nsIFrame* kid : aFrame->PrincipalChildList()) {
    nsISVGChildFrame* SVGFrame = do_QueryFrame(kid);
    if (SVGFrame) {
      SVGFrame->NotifySVGChanged(aFlags); 
    } else {
      NS_ASSERTION(kid->IsFrameOfType(nsIFrame::eSVG) || kid->IsSVGText(),
                   "SVG frame expected");
      // recurse into the children of container frames e.g. <clipPath>, <mask>
      // in case they have child frames with transformation matrices
      if (kid->IsFrameOfType(nsIFrame::eSVG)) {
        NotifyChildrenOfSVGChange(kid, aFlags);
      }
    }
  }
}

// ************************************************************

class SVGPaintCallback : public nsSVGFilterPaintCallback
{
public:
  virtual void Paint(gfxContext& aContext, nsIFrame *aTarget,
                     const gfxMatrix& aTransform,
                     const nsIntRect* aDirtyRect) override
  {
    nsISVGChildFrame *svgChildFrame = do_QueryFrame(aTarget);
    NS_ASSERTION(svgChildFrame, "Expected SVG frame here");

    nsIntRect* dirtyRect = nullptr;
    nsIntRect tmpDirtyRect;

    // aDirtyRect is in user-space pixels, we need to convert to
    // outer-SVG-frame-relative device pixels.
    if (aDirtyRect) {
      gfxMatrix userToDeviceSpace = aTransform;
      if (userToDeviceSpace.IsSingular()) {
        return;
      }
      gfxRect dirtyBounds = userToDeviceSpace.TransformBounds(
        gfxRect(aDirtyRect->x, aDirtyRect->y, aDirtyRect->width, aDirtyRect->height));
      dirtyBounds.RoundOut();
      if (gfxUtils::GfxRectToIntRect(dirtyBounds, &tmpDirtyRect)) {
        dirtyRect = &tmpDirtyRect;
      }
    }

    svgChildFrame->PaintSVG(aContext, aTransform, dirtyRect);
  }
};

void
nsSVGUtils::PaintFrameWithEffects(nsIFrame *aFrame,
                                  gfxContext& aContext,
                                  const gfxMatrix& aTransform,
                                  const nsIntRect *aDirtyRect)
{
  NS_ASSERTION(!NS_SVGDisplayListPaintingEnabled() ||
               (aFrame->GetStateBits() & NS_FRAME_IS_NONDISPLAY) ||
               aFrame->PresContext()->IsGlyph(),
               "If display lists are enabled, only painting of non-display "
               "SVG should take this code path");

  nsISVGChildFrame *svgChildFrame = do_QueryFrame(aFrame);
  if (!svgChildFrame)
    return;

  float opacity = aFrame->StyleEffects()->mOpacity;
  if (opacity == 0.0f)
    return;

  const nsIContent* content = aFrame->GetContent();
  if (content->IsSVGElement() &&
      !static_cast<const nsSVGElement*>(content)->HasValidDimensions()) {
    return;
  }

  /* Properties are added lazily and may have been removed by a restyle,
     so make sure all applicable ones are set again. */

  nsSVGEffects::EffectProperties effectProperties =
    nsSVGEffects::GetEffectProperties(aFrame);

  bool isOK = effectProperties.HasNoFilterOrHasValidFilter();

  if (aDirtyRect &&
      !(aFrame->GetStateBits() & NS_FRAME_IS_NONDISPLAY)) {
    // Here we convert aFrame's paint bounds to outer-<svg> device space,
    // compare it to aDirtyRect, and return early if they don't intersect.
    // We don't do this optimization for nondisplay SVG since nondisplay
    // SVG doesn't maintain bounds/overflow rects.
    nsRect overflowRect = aFrame->GetVisualOverflowRectRelativeToSelf();
    if (aFrame->IsFrameOfType(nsIFrame::eSVGGeometry) ||
        aFrame->IsSVGText()) {
      // Unlike containers, leaf frames do not include GetPosition() in
      // GetCanvasTM().
      overflowRect = overflowRect + aFrame->GetPosition();
    }
    int32_t appUnitsPerDevPx = aFrame->PresContext()->AppUnitsPerDevPixel();
    gfxMatrix tm = aTransform;
    if (aFrame->IsFrameOfType(nsIFrame::eSVG | nsIFrame::eSVGContainer)) {
      gfx::Matrix childrenOnlyTM;
      if (static_cast<nsSVGContainerFrame*>(aFrame)->
            HasChildrenOnlyTransform(&childrenOnlyTM)) {
        // Undo the children-only transform:
        if (!childrenOnlyTM.Invert()) {
          return;
        }
        tm = ThebesMatrix(childrenOnlyTM) * tm;
      }
    }
    nsIntRect bounds = TransformFrameRectToOuterSVG(overflowRect,
                         tm, aFrame->PresContext()).
                           ToOutsidePixels(appUnitsPerDevPx);
    if (!aDirtyRect->Intersects(bounds)) {
      return;
    }
  }

  /* SVG defines the following rendering model:
   *
   *  1. Render fill
   *  2. Render stroke
   *  3. Render markers
   *  4. Apply filter
   *  5. Apply clipping, masking, group opacity
   *
   * We follow this, but perform a couple of optimizations:
   *
   * + Use cairo's clipPath when representable natively (single object
   *   clip region).
   *
   * + Merge opacity and masking if both used together.
   */

  if (opacity != 1.0f && CanOptimizeOpacity(aFrame))
    opacity = 1.0f;

  DrawTarget* drawTarget = aContext.GetDrawTarget();
  bool complexEffects = false;

  nsSVGClipPathFrame *clipPathFrame = effectProperties.GetClipPathFrame(&isOK);
  nsSVGMaskFrame *maskFrame = effectProperties.GetFirstMaskFrame(&isOK);

  bool isTrivialClip = clipPathFrame ? clipPathFrame->IsTrivial() : true;

  if (!isOK) {
    // Some resource is invalid. We shouldn't paint anything.
    return;
  }

  // These are used if we require a temporary surface for a custom blend mode.
  RefPtr<gfxContext> target = &aContext;
  IntPoint targetOffset;

  /* Check if we need to do additional operations on this child's
   * rendering, which necessitates rendering into another surface. */
  if (opacity != 1.0f || maskFrame || (clipPathFrame && !isTrivialClip)
      || aFrame->StyleEffects()->mMixBlendMode != NS_STYLE_BLEND_NORMAL) {
    complexEffects = true;

    Matrix maskTransform;
    RefPtr<SourceSurface> maskSurface =
      maskFrame ? maskFrame->GetMaskForMaskedFrame(&aContext,
                                                    aFrame, aTransform, opacity, &maskTransform)
                : nullptr;

    if (maskFrame && !maskSurface) {
      // Entire surface is clipped out.
      return;
    }

    aContext.Save();
    if (!(aFrame->GetStateBits() & NS_FRAME_IS_NONDISPLAY)) {
      // aFrame has a valid visual overflow rect, so clip to it before calling
      // PushGroup() to minimize the size of the surfaces we'll composite:
      gfxContextMatrixAutoSaveRestore matrixAutoSaveRestore(&aContext);
      aContext.Multiply(aTransform);
      nsRect overflowRect = aFrame->GetVisualOverflowRectRelativeToSelf();
      if (aFrame->IsFrameOfType(nsIFrame::eSVGGeometry) ||
          aFrame->IsSVGText()) {
        // Unlike containers, leaf frames do not include GetPosition() in
        // GetCanvasTM().
        overflowRect = overflowRect + aFrame->GetPosition();
      }
      aContext.Clip(NSRectToSnappedRect(overflowRect,
                                        aFrame->PresContext()->AppUnitsPerDevPixel(),
                                        *drawTarget));
    }

    if (aFrame->StyleEffects()->mMixBlendMode != NS_STYLE_BLEND_NORMAL) {
      // Create a temporary context to draw to so we can blend it back with
      // another operator.
      gfxRect clipRect;
      {
        gfxContextMatrixAutoSaveRestore matRestore(&aContext);

        aContext.SetMatrix(gfxMatrix());
        clipRect = aContext.GetClipExtents();
      }

      IntRect drawRect = RoundedOut(ToRect(clipRect));

      RefPtr<DrawTarget> targetDT = aContext.GetDrawTarget()->CreateSimilarDrawTarget(drawRect.Size(), SurfaceFormat::B8G8R8A8);
      target = gfxContext::CreateOrNull(targetDT);
      if (!target) {
        gfxDevCrash(LogReason::InvalidContext) << "SVGPaintWithEffects context problem " << gfx::hexa(targetDT);
        return;
      }
      target->SetMatrix(aContext.CurrentMatrix() * gfxMatrix::Translation(-drawRect.TopLeft()));
      targetOffset = drawRect.TopLeft();
    }

    if (clipPathFrame && !isTrivialClip) {
      Matrix clippedMaskTransform;
      RefPtr<SourceSurface> clipMaskSurface = clipPathFrame->GetClipMask(aContext, aFrame, aTransform,
                                                                         &clippedMaskTransform, maskSurface, maskTransform);

      if (clipMaskSurface) {
        maskSurface = clipMaskSurface;
        maskTransform = clippedMaskTransform;
      }
    }

    if (opacity != 1.0f || maskFrame || (clipPathFrame && !isTrivialClip)) {
      target->PushGroupForBlendBack(gfxContentType::COLOR_ALPHA, opacity, maskSurface, maskTransform);
    }
  }

  /* If this frame has only a trivial clipPath, set up cairo's clipping now so
   * we can just do normal painting and get it clipped appropriately.
   */
  if (clipPathFrame && isTrivialClip) {
    aContext.Save();
    clipPathFrame->ApplyClipPath(aContext, aFrame, aTransform);
  }

  /* Paint the child */
  if (effectProperties.HasValidFilter()) {
    nsRegion* dirtyRegion = nullptr;
    nsRegion tmpDirtyRegion;
    if (aDirtyRect) {
      // aDirtyRect is in outer-<svg> device pixels, but the filter code needs
      // it in frame space.
      gfxMatrix userToDeviceSpace = GetUserToCanvasTM(aFrame);
      if (userToDeviceSpace.IsSingular()) {
        return;
      }
      gfxMatrix deviceToUserSpace = userToDeviceSpace;
      deviceToUserSpace.Invert();
      gfxRect dirtyBounds = deviceToUserSpace.TransformBounds(
                              gfxRect(aDirtyRect->x, aDirtyRect->y,
                                      aDirtyRect->width, aDirtyRect->height));
      tmpDirtyRegion =
        nsLayoutUtils::RoundGfxRectToAppRect(
          dirtyBounds, aFrame->PresContext()->AppUnitsPerCSSPixel()) -
        aFrame->GetPosition();
      dirtyRegion = &tmpDirtyRegion;
    }
    SVGPaintCallback paintCallback;
    nsFilterInstance::PaintFilteredFrame(aFrame, target->GetDrawTarget(),
                                         aTransform, &paintCallback,
                                         dirtyRegion);
  } else {
    svgChildFrame->PaintSVG(*target, aTransform, aDirtyRect);
  }

  if (clipPathFrame && isTrivialClip) {
    aContext.Restore();
  }

  /* No more effects, we're done. */
  if (!complexEffects)
    return;
  
  if (opacity != 1.0f || maskFrame || (clipPathFrame && !isTrivialClip)) {
    target->PopGroupAndBlend();
  }

  if (aFrame->StyleEffects()->mMixBlendMode != NS_STYLE_BLEND_NORMAL) {
    RefPtr<DrawTarget> targetDT = target->GetDrawTarget();
    target = nullptr;
    RefPtr<SourceSurface> targetSurf = targetDT->Snapshot();

    aContext.SetMatrix(gfxMatrix()); // This will be restored right after.
    RefPtr<gfxPattern> pattern = new gfxPattern(targetSurf, Matrix::Translation(targetOffset.x, targetOffset.y));
    aContext.SetPattern(pattern);
    aContext.Paint();
  }

  aContext.Restore();
}

bool
nsSVGUtils::HitTestClip(nsIFrame *aFrame, const gfxPoint &aPoint)
{
  nsSVGEffects::EffectProperties props =
    nsSVGEffects::GetEffectProperties(aFrame);
  if (!props.mClipPath) {
    const nsStyleSVGReset *style = aFrame->StyleSVGReset();
    if (style->HasClipPath()) {
      return nsCSSClipPathInstance::HitTestBasicShapeClip(aFrame, aPoint);
    }
    return true;
  }

  bool isOK = true;
  nsSVGClipPathFrame *clipPathFrame = props.GetClipPathFrame(&isOK);
  if (!isOK) {
    // clipPath is not a valid resource, so nothing gets painted, so
    // hit-testing must fail.
    return false;
  }
  if (!clipPathFrame) {
    // clipPath doesn't exist, ignore it.
    return true;
  }

  return clipPathFrame->PointIsInsideClipPath(aFrame, aPoint);
}

nsIFrame *
nsSVGUtils::HitTestChildren(nsSVGDisplayContainerFrame* aFrame,
                            const gfxPoint& aPoint)
{
  // First we transform aPoint into the coordinate space established by aFrame
  // for its children (e.g. take account of any 'viewBox' attribute):
  gfxPoint point = aPoint;
  if (aFrame->GetContent()->IsSVGElement()) { // must check before cast
    gfxMatrix m = static_cast<const nsSVGElement*>(aFrame->GetContent())->
                    PrependLocalTransformsTo(gfxMatrix(),
                                             eChildToUserSpace);
    if (!m.IsIdentity()) {
      if (!m.Invert()) {
        return nullptr;
      }
      point = m.Transform(point);
    }
  }

  // Traverse the list in reverse order, so that if we get a hit we know that's
  // the topmost frame that intersects the point; then we can just return it.
  nsIFrame* result = nullptr;
  for (nsIFrame* current = aFrame->PrincipalChildList().LastChild();
       current;
       current = current->GetPrevSibling()) {
    nsISVGChildFrame* SVGFrame = do_QueryFrame(current);
    if (SVGFrame) {
      const nsIContent* content = current->GetContent();
      if (content->IsSVGElement() &&
          !static_cast<const nsSVGElement*>(content)->HasValidDimensions()) {
        continue;
      }
      // GetFrameForPoint() expects a point in its frame's SVG user space, so
      // we need to convert to that space:
      gfxPoint p = point;
      if (content->IsSVGElement()) { // must check before cast
        gfxMatrix m = static_cast<const nsSVGElement*>(content)->
                        PrependLocalTransformsTo(gfxMatrix(),
                                                 eUserSpaceToParent);
        if (!m.IsIdentity()) {
          if (!m.Invert()) {
            continue;
          }
          p = m.Transform(p);
        }
      }
      result = SVGFrame->GetFrameForPoint(p);
      if (result)
        break;
    }
  }

  if (result && !HitTestClip(aFrame, aPoint))
    result = nullptr;

  return result;
}

nsRect
nsSVGUtils::GetCoveredRegion(const nsFrameList &aFrames)
{
  nsRect rect;

  for (nsIFrame* kid = aFrames.FirstChild();
       kid;
       kid = kid->GetNextSibling()) {
    nsISVGChildFrame* child = do_QueryFrame(kid);
    if (child) {
      nsRect childRect = child->GetCoveredRegion();
      rect.UnionRect(rect, childRect);
    }
  }

  return rect;
}

nsRect
nsSVGUtils::TransformFrameRectToOuterSVG(const nsRect& aRect,
                                         const gfxMatrix& aMatrix,
                                         nsPresContext* aPresContext)
{
  gfxRect r(aRect.x, aRect.y, aRect.width, aRect.height);
  r.Scale(1.0 / nsPresContext::AppUnitsPerCSSPixel());
  return nsLayoutUtils::RoundGfxRectToAppRect(
    aMatrix.TransformBounds(r), aPresContext->AppUnitsPerDevPixel());
}

IntSize
nsSVGUtils::ConvertToSurfaceSize(const gfxSize& aSize,
                                 bool *aResultOverflows)
{
  IntSize surfaceSize(ClampToInt(ceil(aSize.width)), ClampToInt(ceil(aSize.height)));

  *aResultOverflows = surfaceSize.width != ceil(aSize.width) ||
    surfaceSize.height != ceil(aSize.height);

  if (!Factory::CheckSurfaceSize(surfaceSize)) {
    surfaceSize.width = std::min(NS_SVG_OFFSCREEN_MAX_DIMENSION,
                               surfaceSize.width);
    surfaceSize.height = std::min(NS_SVG_OFFSCREEN_MAX_DIMENSION,
                                surfaceSize.height);
    *aResultOverflows = true;
  }

  return surfaceSize;
}

bool
nsSVGUtils::HitTestRect(const gfx::Matrix &aMatrix,
                        float aRX, float aRY, float aRWidth, float aRHeight,
                        float aX, float aY)
{
  gfx::Rect rect(aRX, aRY, aRWidth, aRHeight);
  if (rect.IsEmpty() || aMatrix.IsSingular()) {
    return false;
  }
  gfx::Matrix toRectSpace = aMatrix;
  toRectSpace.Invert();
  gfx::Point p = toRectSpace * gfx::Point(aX, aY);
  return rect.x <= p.x && p.x <= rect.XMost() &&
         rect.y <= p.y && p.y <= rect.YMost();
}

gfxRect
nsSVGUtils::GetClipRectForFrame(nsIFrame *aFrame,
                                float aX, float aY, float aWidth, float aHeight)
{
  const nsStyleDisplay* disp = aFrame->StyleDisplay();
  const nsStyleEffects* effects = aFrame->StyleEffects();

  if (!(effects->mClipFlags & NS_STYLE_CLIP_RECT)) {
    NS_ASSERTION(effects->mClipFlags == NS_STYLE_CLIP_AUTO,
                 "We don't know about this type of clip.");
    return gfxRect(aX, aY, aWidth, aHeight);
  }

  if (disp->mOverflowX == NS_STYLE_OVERFLOW_HIDDEN ||
      disp->mOverflowY == NS_STYLE_OVERFLOW_HIDDEN) {

    nsIntRect clipPxRect =
      effects->mClip.ToOutsidePixels(aFrame->PresContext()->AppUnitsPerDevPixel());
    gfxRect clipRect =
      gfxRect(clipPxRect.x, clipPxRect.y, clipPxRect.width, clipPxRect.height);

    if (NS_STYLE_CLIP_RIGHT_AUTO & effects->mClipFlags) {
      clipRect.width = aWidth - clipRect.X();
    }
    if (NS_STYLE_CLIP_BOTTOM_AUTO & effects->mClipFlags) {
      clipRect.height = aHeight - clipRect.Y();
    }

    if (disp->mOverflowX != NS_STYLE_OVERFLOW_HIDDEN) {
      clipRect.x = aX;
      clipRect.width = aWidth;
    }
    if (disp->mOverflowY != NS_STYLE_OVERFLOW_HIDDEN) {
      clipRect.y = aY;
      clipRect.height = aHeight;
    }
     
    return clipRect;
  }
  return gfxRect(aX, aY, aWidth, aHeight);
}

void
nsSVGUtils::SetClipRect(gfxContext *aContext,
                        const gfxMatrix &aCTM,
                        const gfxRect &aRect)
{
  if (aCTM.IsSingular())
    return;

  gfxContextMatrixAutoSaveRestore matrixAutoSaveRestore(aContext);
  aContext->Multiply(aCTM);
  aContext->Clip(aRect);
}

gfxRect
nsSVGUtils::GetBBox(nsIFrame *aFrame, uint32_t aFlags)
{
  if (aFrame->GetContent()->IsNodeOfType(nsINode::eTEXT)) {
    aFrame = aFrame->GetParent();
  }
  gfxRect bbox;
  nsISVGChildFrame *svg = do_QueryFrame(aFrame);
  if (svg || aFrame->IsSVGText()) {
    // It is possible to apply a gradient, pattern, clipping path, mask or
    // filter to text. When one of these facilities is applied to text
    // the bounding box is the entire text element in all
    // cases.
    if (aFrame->IsSVGText()) {
      nsIFrame* ancestor = GetFirstNonAAncestorFrame(aFrame);
      if (ancestor && ancestor->IsSVGText()) {
        while (ancestor->GetType() != nsGkAtoms::svgTextFrame) {
          ancestor = ancestor->GetParent();
        }
      }
      svg = do_QueryFrame(ancestor);
    }
    nsIContent* content = aFrame->GetContent();
    if (content->IsSVGElement() &&
        !static_cast<const nsSVGElement*>(content)->HasValidDimensions()) {
      return bbox;
    }

    FrameProperties props = aFrame->Properties();

    if (aFlags == eBBoxIncludeFillGeometry) {
      gfxRect* prop = props.Get(ObjectBoundingBoxProperty());
      if (prop) {
        return *prop;
      }
    }

    gfxMatrix matrix;
    if (aFrame->GetType() == nsGkAtoms::svgForeignObjectFrame ||
        aFrame->GetType() == nsGkAtoms::svgUseFrame) {
      // The spec says getBBox "Returns the tight bounding box in *current user
      // space*". So we should really be doing this for all elements, but that
      // needs investigation to check that we won't break too much content.
      // NOTE: When changing this to apply to other frame types, make sure to
      // also update nsSVGUtils::FrameSpaceInCSSPxToUserSpaceOffset.
      MOZ_ASSERT(content->IsSVGElement(), "bad cast");
      nsSVGElement *element = static_cast<nsSVGElement*>(content);
      matrix = element->PrependLocalTransformsTo(matrix, eChildToUserSpace);
    }
    bbox = svg->GetBBoxContribution(ToMatrix(matrix), aFlags).ToThebesRect();
    // Account for 'clipped'.
    if (aFlags & nsSVGUtils::eBBoxIncludeClipped) {
      gfxRect clipRect(0, 0, 0, 0);
      float x, y, width, height;
      gfxMatrix tm;
      gfxRect fillBBox = 
        svg->GetBBoxContribution(ToMatrix(tm), 
                                 nsSVGUtils::eBBoxIncludeFill).ToThebesRect();
      x = fillBBox.x;
      y = fillBBox.y;
      width = fillBBox.width;
      height = fillBBox.height;
      bool hasClip = aFrame->StyleDisplay()->IsScrollableOverflow();
      if (hasClip) {
        clipRect = 
          nsSVGUtils::GetClipRectForFrame(aFrame, x, y, width, height);
          if (aFrame->GetType() == nsGkAtoms::svgForeignObjectFrame ||
              aFrame->GetType() == nsGkAtoms::svgUseFrame) {
            clipRect = matrix.TransformBounds(clipRect);
          }
      }
      nsSVGEffects::EffectProperties effectProperties =
        nsSVGEffects::GetEffectProperties(aFrame);
      bool isOK = true;
      nsSVGClipPathFrame *clipPathFrame = 
        effectProperties.GetClipPathFrame(&isOK);
      if (clipPathFrame && isOK) {
        SVGClipPathElement *clipContent = 
          static_cast<SVGClipPathElement*>(clipPathFrame->GetContent());
        if (clipContent->IsUnitsObjectBoundingBox()) {
          matrix.Translate(gfxPoint(x, y));
          matrix.Scale(width, height);
        } else if (aFrame->GetType() == nsGkAtoms::svgForeignObjectFrame) {
          matrix.Reset();
        }
        bbox = 
          clipPathFrame->GetBBoxForClipPathFrame(bbox, matrix).ToThebesRect();
        if (hasClip) {
          bbox = bbox.Intersect(clipRect);
        }
      } else {
        if (!isOK) {
          bbox = gfxRect(0, 0, 0, 0);
        } else {
          if (hasClip) {
            bbox = bbox.Intersect(clipRect);
          }
        }
      }
      if (bbox.IsEmpty()) {
        bbox = gfxRect(0, 0, 0, 0);
      }
    }

    if (aFlags == eBBoxIncludeFillGeometry) {
      // Obtaining the bbox for objectBoundingBox calculations is common so we
      // cache the result for future calls, since calculation can be expensive:
      props.Set(ObjectBoundingBoxProperty(), new gfxRect(bbox));
    }

    return bbox;
  }
  return nsSVGIntegrationUtils::GetSVGBBoxForNonSVGFrame(aFrame);
}

gfxPoint
nsSVGUtils::FrameSpaceInCSSPxToUserSpaceOffset(nsIFrame *aFrame)
{
  if (!(aFrame->GetStateBits() & NS_FRAME_SVG_LAYOUT)) {
    // The user space for non-SVG frames is defined as the bounding box of the
    // frame's border-box rects over all continuations.
    return gfxPoint();
  }

  // Leaf frames apply their own offset inside their user space.
  if (aFrame->IsFrameOfType(nsIFrame::eSVGGeometry) ||
      aFrame->IsSVGText()) {
    return nsLayoutUtils::RectToGfxRect(aFrame->GetRect(),
                                         nsPresContext::AppUnitsPerCSSPixel()).TopLeft();
  }

  // For foreignObject frames, nsSVGUtils::GetBBox applies their local
  // transform, so we need to do the same here.
  if (aFrame->GetType() == nsGkAtoms::svgForeignObjectFrame ||
      aFrame->GetType() == nsGkAtoms::svgUseFrame) {
    gfxMatrix transform = static_cast<nsSVGElement*>(aFrame->GetContent())->
        PrependLocalTransformsTo(gfxMatrix(), eChildToUserSpace);
    NS_ASSERTION(!transform.HasNonTranslation(), "we're relying on this being an offset-only transform");
    return transform.GetTranslation();
  }

  return gfxPoint();
}

static gfxRect
GetBoundingBoxRelativeRect(const nsSVGLength2 *aXYWH,
                           const gfxRect& aBBox)
{
  return gfxRect(aBBox.x + nsSVGUtils::ObjectSpace(aBBox, &aXYWH[0]),
                 aBBox.y + nsSVGUtils::ObjectSpace(aBBox, &aXYWH[1]),
                 nsSVGUtils::ObjectSpace(aBBox, &aXYWH[2]),
                 nsSVGUtils::ObjectSpace(aBBox, &aXYWH[3]));
}

gfxRect
nsSVGUtils::GetRelativeRect(uint16_t aUnits, const nsSVGLength2 *aXYWH,
                            const gfxRect& aBBox,
                            const UserSpaceMetrics& aMetrics)
{
  if (aUnits == SVG_UNIT_TYPE_OBJECTBOUNDINGBOX) {
    return GetBoundingBoxRelativeRect(aXYWH, aBBox);
  }
  return gfxRect(UserSpace(aMetrics, &aXYWH[0]),
                 UserSpace(aMetrics, &aXYWH[1]),
                 UserSpace(aMetrics, &aXYWH[2]),
                 UserSpace(aMetrics, &aXYWH[3]));
}

gfxRect
nsSVGUtils::GetRelativeRect(uint16_t aUnits, const nsSVGLength2 *aXYWH,
                            const gfxRect& aBBox, nsIFrame *aFrame)
{
  if (aUnits == SVG_UNIT_TYPE_OBJECTBOUNDINGBOX) {
    return GetBoundingBoxRelativeRect(aXYWH, aBBox);
  }
  nsIContent* content = aFrame->GetContent();
  if (content->IsSVGElement()) {
    nsSVGElement* svgElement = static_cast<nsSVGElement*>(content);
    return GetRelativeRect(aUnits, aXYWH, aBBox, SVGElementMetrics(svgElement));
  }
  return GetRelativeRect(aUnits, aXYWH, aBBox, NonSVGFrameUserSpaceMetrics(aFrame));
}

bool
nsSVGUtils::CanOptimizeOpacity(nsIFrame *aFrame)
{
  if (!(aFrame->GetStateBits() & NS_FRAME_SVG_LAYOUT)) {
    return false;
  }
  nsIAtom *type = aFrame->GetType();
  if (type != nsGkAtoms::svgImageFrame &&
      type != nsGkAtoms::svgPathGeometryFrame) {
    return false;
  }
  if (aFrame->StyleEffects()->HasFilters()) {
    return false;
  }
  // XXX The SVG WG is intending to allow fill, stroke and markers on <image>
  if (type == nsGkAtoms::svgImageFrame) {
    return true;
  }
  const nsStyleSVG *style = aFrame->StyleSVG();
  if (style->HasMarker()) {
    return false;
  }
  if (!style->HasFill() || !HasStroke(aFrame)) {
    return true;
  }
  return false;
}

gfxMatrix
nsSVGUtils::AdjustMatrixForUnits(const gfxMatrix &aMatrix,
                                 nsSVGEnum *aUnits,
                                 nsIFrame *aFrame)
{
  if (aFrame &&
      aUnits->GetAnimValue() == SVG_UNIT_TYPE_OBJECTBOUNDINGBOX) {
    gfxRect bbox = GetBBox(aFrame);
    gfxMatrix tm = aMatrix;
    tm.Translate(gfxPoint(bbox.X(), bbox.Y()));
    tm.Scale(bbox.Width(), bbox.Height());
    return tm;
  }
  return aMatrix;
}

nsIFrame*
nsSVGUtils::GetFirstNonAAncestorFrame(nsIFrame* aStartFrame)
{
  for (nsIFrame *ancestorFrame = aStartFrame; ancestorFrame;
       ancestorFrame = ancestorFrame->GetParent()) {
    if (ancestorFrame->GetType() != nsGkAtoms::svgAFrame) {
      return ancestorFrame;
    }
  }
  return nullptr;
}

bool
nsSVGUtils::GetNonScalingStrokeTransform(nsIFrame *aFrame,
                                         gfxMatrix* aUserToOuterSVG)
{
  if (aFrame->GetContent()->IsNodeOfType(nsINode::eTEXT)) {
    aFrame = aFrame->GetParent();
  }

  if (!aFrame->StyleSVGReset()->HasNonScalingStroke()) {
    return false;
  }

  nsIContent *content = aFrame->GetContent();
  MOZ_ASSERT(content->IsSVGElement(), "bad cast");

  *aUserToOuterSVG = ThebesMatrix(SVGContentUtils::GetCTM(
                       static_cast<nsSVGElement*>(content), true));

  return !aUserToOuterSVG->IsIdentity();
}

// The logic here comes from _cairo_stroke_style_max_distance_from_path
static gfxRect
PathExtentsToMaxStrokeExtents(const gfxRect& aPathExtents,
                              nsIFrame* aFrame,
                              double aStyleExpansionFactor,
                              const gfxMatrix& aMatrix)
{
  double style_expansion =
    aStyleExpansionFactor * nsSVGUtils::GetStrokeWidth(aFrame);

  gfxMatrix matrix = aMatrix;

  gfxMatrix outerSVGToUser;
  if (nsSVGUtils::GetNonScalingStrokeTransform(aFrame, &outerSVGToUser)) {
    outerSVGToUser.Invert();
    matrix.PreMultiply(outerSVGToUser);
  }

  double dx = style_expansion * (fabs(matrix._11) + fabs(matrix._21));
  double dy = style_expansion * (fabs(matrix._22) + fabs(matrix._12));

  gfxRect strokeExtents = aPathExtents;
  strokeExtents.Inflate(dx, dy);
  return strokeExtents;
}

/*static*/ gfxRect
nsSVGUtils::PathExtentsToMaxStrokeExtents(const gfxRect& aPathExtents,
                                          nsTextFrame* aFrame,
                                          const gfxMatrix& aMatrix)
{
  NS_ASSERTION(aFrame->IsSVGText(), "expected an nsTextFrame for SVG text");
  return ::PathExtentsToMaxStrokeExtents(aPathExtents, aFrame, 0.5, aMatrix);
}

/*static*/ gfxRect
nsSVGUtils::PathExtentsToMaxStrokeExtents(const gfxRect& aPathExtents,
                                          nsSVGPathGeometryFrame* aFrame,
                                          const gfxMatrix& aMatrix)
{
  bool strokeMayHaveCorners =
    !SVGContentUtils::ShapeTypeHasNoCorners(aFrame->GetContent());

  // For a shape without corners the stroke can only extend half the stroke
  // width from the path in the x/y-axis directions. For shapes with corners
  // the stroke can extend by sqrt(1/2) (think 45 degree rotated rect, or line
  // with stroke-linecaps="square").
  double styleExpansionFactor = strokeMayHaveCorners ? M_SQRT1_2 : 0.5;

  // The stroke can extend even further for paths that can be affected by
  // stroke-miterlimit.
  bool affectedByMiterlimit =
    aFrame->GetContent()->IsAnyOfSVGElements(nsGkAtoms::path,
                                             nsGkAtoms::polyline,
                                             nsGkAtoms::polygon);

  if (affectedByMiterlimit) {
    const nsStyleSVG* style = aFrame->StyleSVG();
    if (style->mStrokeLinejoin == NS_STYLE_STROKE_LINEJOIN_MITER &&
        styleExpansionFactor < style->mStrokeMiterlimit / 2.0) {
      styleExpansionFactor = style->mStrokeMiterlimit / 2.0;
    }
  }

  return ::PathExtentsToMaxStrokeExtents(aPathExtents,
                                         aFrame,
                                         styleExpansionFactor,
                                         aMatrix);
}

// ----------------------------------------------------------------------

/* static */ nscolor
nsSVGUtils::GetFallbackOrPaintColor(nsStyleContext *aStyleContext,
                                    nsStyleSVGPaint nsStyleSVG::*aFillOrStroke)
{
  const nsStyleSVGPaint &paint = aStyleContext->StyleSVG()->*aFillOrStroke;
  nsStyleContext *styleIfVisited = aStyleContext->GetStyleIfVisited();
  bool isServer = paint.mType == eStyleSVGPaintType_Server ||
                  paint.mType == eStyleSVGPaintType_ContextFill ||
                  paint.mType == eStyleSVGPaintType_ContextStroke;
  nscolor color = isServer ? paint.mFallbackColor : paint.mPaint.mColor;
  if (styleIfVisited) {
    const nsStyleSVGPaint &paintIfVisited =
      styleIfVisited->StyleSVG()->*aFillOrStroke;
    // To prevent Web content from detecting if a user has visited a URL
    // (via URL loading triggered by paint servers or performance
    // differences between paint servers or between a paint server and a
    // color), we do not allow whether links are visited to change which
    // paint server is used or switch between paint servers and simple
    // colors.  A :visited style may only override a simple color with
    // another simple color.
    if (paintIfVisited.mType == eStyleSVGPaintType_Color &&
        paint.mType == eStyleSVGPaintType_Color) {
      nscolor colors[2] = { color, paintIfVisited.mPaint.mColor };
      return nsStyleContext::CombineVisitedColors(
               colors, aStyleContext->RelevantLinkVisited());
    }
  }
  return color;
}

/**
 * Stores in |aTargetPaint| information on how to reconstruct the current
 * fill or stroke pattern. Will also set the paint opacity to transparent if
 * the paint is set to "none".
 * @param aOuterContextPaint pattern information from the outer text context
 * @param aTargetPaint where to store the current pattern information
 * @param aFillOrStroke member pointer to the paint we are setting up
 * @param aProperty the frame property descriptor of the fill or stroke paint
 *   server frame
 */
static void
SetupInheritablePaint(const DrawTarget* aDrawTarget,
                      const gfxMatrix& aContextMatrix,
                      nsIFrame* aFrame,
                      float& aOpacity,
                      gfxTextContextPaint* aOuterContextPaint,
                      SVGTextContextPaint::Paint& aTargetPaint,
                      nsStyleSVGPaint nsStyleSVG::*aFillOrStroke,
                      nsSVGEffects::PaintingPropertyDescriptor aProperty)
{
  const nsStyleSVG *style = aFrame->StyleSVG();
  nsSVGPaintServerFrame *ps =
    nsSVGEffects::GetPaintServer(aFrame, &(style->*aFillOrStroke), aProperty);

  if (ps) {
    RefPtr<gfxPattern> pattern =
      ps->GetPaintServerPattern(aFrame, aDrawTarget, aContextMatrix,
                                aFillOrStroke, aOpacity);
    if (pattern) {
      aTargetPaint.SetPaintServer(aFrame, aContextMatrix, ps);
      return;
    }
  }
  if (aOuterContextPaint) {
    RefPtr<gfxPattern> pattern;
    switch ((style->*aFillOrStroke).mType) {
    case eStyleSVGPaintType_ContextFill:
      pattern = aOuterContextPaint->GetFillPattern(aDrawTarget, aOpacity,
                                                   aContextMatrix);
      break;
    case eStyleSVGPaintType_ContextStroke:
      pattern = aOuterContextPaint->GetStrokePattern(aDrawTarget, aOpacity,
                                                     aContextMatrix);
      break;
    default:
      ;
    }
    if (pattern) {
      aTargetPaint.SetContextPaint(aOuterContextPaint, (style->*aFillOrStroke).mType);
      return;
    }
  }
  nscolor color =
    nsSVGUtils::GetFallbackOrPaintColor(aFrame->StyleContext(), aFillOrStroke);
  aTargetPaint.SetColor(color);
}

/* static */ DrawMode
nsSVGUtils::SetupContextPaint(const DrawTarget* aDrawTarget,
                              const gfxMatrix& aContextMatrix,
                              nsIFrame* aFrame,
                              gfxTextContextPaint* aOuterContextPaint,
                              SVGTextContextPaint* aThisContextPaint)
{
  DrawMode toDraw = DrawMode(0);

  const nsStyleSVG *style = aFrame->StyleSVG();

  // fill:
  if (style->mFill.mType == eStyleSVGPaintType_None) {
    aThisContextPaint->SetFillOpacity(0.0f);
  } else {
    float opacity = nsSVGUtils::GetOpacity(style->FillOpacitySource(),
                                           style->mFillOpacity,
                                           aOuterContextPaint);

    SetupInheritablePaint(aDrawTarget, aContextMatrix, aFrame,
                          opacity, aOuterContextPaint,
                          aThisContextPaint->mFillPaint, &nsStyleSVG::mFill,
                          nsSVGEffects::FillProperty());

    aThisContextPaint->SetFillOpacity(opacity);

    toDraw |= DrawMode::GLYPH_FILL;
  }

  // stroke:
  if (style->mStroke.mType == eStyleSVGPaintType_None) {
    aThisContextPaint->SetStrokeOpacity(0.0f);
  } else {
    float opacity = nsSVGUtils::GetOpacity(style->StrokeOpacitySource(),
                                           style->mStrokeOpacity,
                                           aOuterContextPaint);

    SetupInheritablePaint(aDrawTarget, aContextMatrix, aFrame,
                          opacity, aOuterContextPaint,
                          aThisContextPaint->mStrokePaint, &nsStyleSVG::mStroke,
                          nsSVGEffects::StrokeProperty());

    aThisContextPaint->SetStrokeOpacity(opacity);

    toDraw |= DrawMode::GLYPH_STROKE;
  }

  return toDraw;
}

/* static */ void
nsSVGUtils::MakeFillPatternFor(nsIFrame* aFrame,
                               gfxContext* aContext,
                               GeneralPattern* aOutPattern,
                               gfxTextContextPaint* aContextPaint)
{
  const nsStyleSVG* style = aFrame->StyleSVG();
  if (style->mFill.mType == eStyleSVGPaintType_None) {
    return;
  }

  const float opacity = aFrame->StyleEffects()->mOpacity;

  float fillOpacity = GetOpacity(style->FillOpacitySource(),
                                 style->mFillOpacity,
                                 aContextPaint);
  if (opacity < 1.0f &&
      nsSVGUtils::CanOptimizeOpacity(aFrame)) {
    // Combine the group opacity into the fill opacity (we will have skipped
    // creating an offscreen surface to apply the group opacity).
    fillOpacity *= opacity;
  }

  const DrawTarget* dt = aContext->GetDrawTarget();

  nsSVGPaintServerFrame *ps =
    nsSVGEffects::GetPaintServer(aFrame, &style->mFill,
                                 nsSVGEffects::FillProperty());
  if (ps) {
    RefPtr<gfxPattern> pattern =
      ps->GetPaintServerPattern(aFrame, dt, aContext->CurrentMatrix(),
                                &nsStyleSVG::mFill, fillOpacity);
    if (pattern) {
      pattern->CacheColorStops(dt);
      aOutPattern->Init(*pattern->GetPattern(dt));
      return;
    }
  }

  if (aContextPaint) {
    RefPtr<gfxPattern> pattern;
    switch (style->mFill.mType) {
    case eStyleSVGPaintType_ContextFill:
      pattern = aContextPaint->GetFillPattern(dt, fillOpacity,
                                              aContext->CurrentMatrix());
      break;
    case eStyleSVGPaintType_ContextStroke:
      pattern = aContextPaint->GetStrokePattern(dt, fillOpacity,
                                                aContext->CurrentMatrix());
      break;
    default:
      ;
    }
    if (pattern) {
      aOutPattern->Init(*pattern->GetPattern(dt));
      return;
    }
  }

  // On failure, use the fallback colour in case we have an
  // objectBoundingBox where the width or height of the object is zero.
  // See http://www.w3.org/TR/SVG11/coords.html#ObjectBoundingBox
  Color color(Color::FromABGR(GetFallbackOrPaintColor(aFrame->StyleContext(),
                                                      &nsStyleSVG::mFill)));
  color.a *= fillOpacity;
  aOutPattern->InitColorPattern(ToDeviceColor(color));
}

/* static */ void
nsSVGUtils::MakeStrokePatternFor(nsIFrame* aFrame,
                                 gfxContext* aContext,
                                 GeneralPattern* aOutPattern,
                                 gfxTextContextPaint* aContextPaint)
{
  const nsStyleSVG* style = aFrame->StyleSVG();
  if (style->mStroke.mType == eStyleSVGPaintType_None) {
    return;
  }

  const float opacity = aFrame->StyleEffects()->mOpacity;

  float strokeOpacity = GetOpacity(style->StrokeOpacitySource(),
                                   style->mStrokeOpacity,
                                   aContextPaint);
  if (opacity < 1.0f &&
      nsSVGUtils::CanOptimizeOpacity(aFrame)) {
    // Combine the group opacity into the stroke opacity (we will have skipped
    // creating an offscreen surface to apply the group opacity).
    strokeOpacity *= opacity;
  }

  const DrawTarget* dt = aContext->GetDrawTarget();

  nsSVGPaintServerFrame *ps =
    nsSVGEffects::GetPaintServer(aFrame, &style->mStroke,
                                 nsSVGEffects::StrokeProperty());
  if (ps) {
    RefPtr<gfxPattern> pattern =
      ps->GetPaintServerPattern(aFrame, dt, aContext->CurrentMatrix(),
                                &nsStyleSVG::mStroke, strokeOpacity);
    if (pattern) {
      pattern->CacheColorStops(dt);
      aOutPattern->Init(*pattern->GetPattern(dt));
      return;
    }
  }

  if (aContextPaint) {
    RefPtr<gfxPattern> pattern;
    switch (style->mStroke.mType) {
    case eStyleSVGPaintType_ContextFill:
      pattern = aContextPaint->GetFillPattern(dt, strokeOpacity,
                                              aContext->CurrentMatrix());
      break;
    case eStyleSVGPaintType_ContextStroke:
      pattern = aContextPaint->GetStrokePattern(dt, strokeOpacity,
                                                aContext->CurrentMatrix());
      break;
    default:
      ;
    }
    if (pattern) {
      aOutPattern->Init(*pattern->GetPattern(dt));
      return;
    }
  }

  // On failure, use the fallback colour in case we have an
  // objectBoundingBox where the width or height of the object is zero.
  // See http://www.w3.org/TR/SVG11/coords.html#ObjectBoundingBox
  Color color(Color::FromABGR(GetFallbackOrPaintColor(aFrame->StyleContext(),
                                                      &nsStyleSVG::mStroke)));
  color.a *= strokeOpacity;
  aOutPattern->InitColorPattern(ToDeviceColor(color));
}

/* static */ float
nsSVGUtils::GetOpacity(nsStyleSVGOpacitySource aOpacityType,
                       const float& aOpacity,
                       gfxTextContextPaint *aContextPaint)
{
  float opacity = 1.0f;
  switch (aOpacityType) {
  case eStyleSVGOpacitySource_Normal:
    opacity = aOpacity;
    break;
  case eStyleSVGOpacitySource_ContextFillOpacity:
    if (aContextPaint) {
      opacity = aContextPaint->GetFillOpacity();
    } else {
      NS_WARNING("Content used context-fill-opacity when not in a context element");
    }
    break;
  case eStyleSVGOpacitySource_ContextStrokeOpacity:
    if (aContextPaint) {
      opacity = aContextPaint->GetStrokeOpacity();
    } else {
      NS_WARNING("Content used context-stroke-opacity when not in a context element");
    }
    break;
  default:
    NS_NOTREACHED("Unknown object opacity inheritance type for SVG glyph");
  }
  return opacity;
}

bool
nsSVGUtils::HasStroke(nsIFrame* aFrame, gfxTextContextPaint *aContextPaint)
{
  const nsStyleSVG *style = aFrame->StyleSVG();
  return style->HasStroke() && GetStrokeWidth(aFrame, aContextPaint) > 0;
}

float
nsSVGUtils::GetStrokeWidth(nsIFrame* aFrame, gfxTextContextPaint *aContextPaint)
{
  const nsStyleSVG *style = aFrame->StyleSVG();
  if (aContextPaint && style->StrokeWidthFromObject()) {
    return aContextPaint->GetStrokeWidth();
  }

  nsIContent* content = aFrame->GetContent();
  if (content->IsNodeOfType(nsINode::eTEXT)) {
    content = content->GetParent();
  }

  nsSVGElement *ctx = static_cast<nsSVGElement*>(content);

  return SVGContentUtils::CoordToFloat(ctx, style->mStrokeWidth);
}

static bool
GetStrokeDashData(nsIFrame* aFrame,
                  nsTArray<gfxFloat>& aDashes,
                  gfxFloat* aDashOffset,
                  gfxTextContextPaint *aContextPaint)
{
  const nsStyleSVG* style = aFrame->StyleSVG();
  nsIContent *content = aFrame->GetContent();
  nsSVGElement *ctx = static_cast<nsSVGElement*>
    (content->IsNodeOfType(nsINode::eTEXT) ?
     content->GetParent() : content);

  gfxFloat totalLength = 0.0;
  if (aContextPaint && style->StrokeDasharrayFromObject()) {
    aDashes = aContextPaint->GetStrokeDashArray();

    for (uint32_t i = 0; i < aDashes.Length(); i++) {
      if (aDashes[i] < 0.0) {
        return false;
      }
      totalLength += aDashes[i];
    }

  } else {
    uint32_t count = style->mStrokeDasharray.Length();
    if (!count || !aDashes.SetLength(count, fallible)) {
      return false;
    }

    gfxFloat pathScale = 1.0;

    if (content->IsSVGElement(nsGkAtoms::path)) {
      pathScale = static_cast<SVGPathElement*>(content)->
        GetPathLengthScale(SVGPathElement::eForStroking);
      if (pathScale <= 0) {
        return false;
      }
    }

    const nsTArray<nsStyleCoord>& dasharray = style->mStrokeDasharray;

    for (uint32_t i = 0; i < count; i++) {
      aDashes[i] = SVGContentUtils::CoordToFloat(ctx,
                                                 dasharray[i]) * pathScale;
      if (aDashes[i] < 0.0) {
        return false;
      }
      totalLength += aDashes[i];
    }
  }

  if (aContextPaint && style->StrokeDashoffsetFromObject()) {
    *aDashOffset = aContextPaint->GetStrokeDashOffset();
  } else {
    *aDashOffset = SVGContentUtils::CoordToFloat(ctx,
                                                 style->mStrokeDashoffset);
  }

  return (totalLength > 0.0);
}

void
nsSVGUtils::SetupCairoStrokeGeometry(nsIFrame* aFrame,
                                     gfxContext *aContext,
                                     gfxTextContextPaint *aContextPaint)
{
  float width = GetStrokeWidth(aFrame, aContextPaint);
  if (width <= 0)
    return;
  aContext->SetLineWidth(width);

  // Apply any stroke-specific transform
  gfxMatrix outerSVGToUser;
  if (GetNonScalingStrokeTransform(aFrame, &outerSVGToUser) &&
      outerSVGToUser.Invert()) {
    aContext->Multiply(outerSVGToUser);
  }

  const nsStyleSVG* style = aFrame->StyleSVG();
  
  switch (style->mStrokeLinecap) {
  case NS_STYLE_STROKE_LINECAP_BUTT:
    aContext->SetLineCap(CapStyle::BUTT);
    break;
  case NS_STYLE_STROKE_LINECAP_ROUND:
    aContext->SetLineCap(CapStyle::ROUND);
    break;
  case NS_STYLE_STROKE_LINECAP_SQUARE:
    aContext->SetLineCap(CapStyle::SQUARE);
    break;
  }

  aContext->SetMiterLimit(style->mStrokeMiterlimit);

  switch (style->mStrokeLinejoin) {
  case NS_STYLE_STROKE_LINEJOIN_MITER:
    aContext->SetLineJoin(JoinStyle::MITER_OR_BEVEL);
    break;
  case NS_STYLE_STROKE_LINEJOIN_ROUND:
    aContext->SetLineJoin(JoinStyle::ROUND);
    break;
  case NS_STYLE_STROKE_LINEJOIN_BEVEL:
    aContext->SetLineJoin(JoinStyle::BEVEL);
    break;
  }

  AutoTArray<gfxFloat, 10> dashes;
  gfxFloat dashOffset;
  if (GetStrokeDashData(aFrame, dashes, &dashOffset, aContextPaint)) {
    aContext->SetDash(dashes.Elements(), dashes.Length(), dashOffset);
  }
}

uint16_t
nsSVGUtils::GetGeometryHitTestFlags(nsIFrame* aFrame)
{
  uint16_t flags = 0;

  switch (aFrame->StyleUserInterface()->mPointerEvents) {
  case NS_STYLE_POINTER_EVENTS_NONE:
    break;
  case NS_STYLE_POINTER_EVENTS_AUTO:
  case NS_STYLE_POINTER_EVENTS_VISIBLEPAINTED:
    if (aFrame->StyleVisibility()->IsVisible()) {
      if (aFrame->StyleSVG()->mFill.mType != eStyleSVGPaintType_None)
        flags |= SVG_HIT_TEST_FILL;
      if (aFrame->StyleSVG()->mStroke.mType != eStyleSVGPaintType_None)
        flags |= SVG_HIT_TEST_STROKE;
      if (aFrame->StyleSVG()->mStrokeOpacity > 0)
        flags |= SVG_HIT_TEST_CHECK_MRECT;
    }
    break;
  case NS_STYLE_POINTER_EVENTS_VISIBLEFILL:
    if (aFrame->StyleVisibility()->IsVisible()) {
      flags |= SVG_HIT_TEST_FILL;
    }
    break;
  case NS_STYLE_POINTER_EVENTS_VISIBLESTROKE:
    if (aFrame->StyleVisibility()->IsVisible()) {
      flags |= SVG_HIT_TEST_STROKE;
    }
    break;
  case NS_STYLE_POINTER_EVENTS_VISIBLE:
    if (aFrame->StyleVisibility()->IsVisible()) {
      flags |= SVG_HIT_TEST_FILL | SVG_HIT_TEST_STROKE;
    }
    break;
  case NS_STYLE_POINTER_EVENTS_PAINTED:
    if (aFrame->StyleSVG()->mFill.mType != eStyleSVGPaintType_None)
      flags |= SVG_HIT_TEST_FILL;
    if (aFrame->StyleSVG()->mStroke.mType != eStyleSVGPaintType_None)
      flags |= SVG_HIT_TEST_STROKE;
    if (aFrame->StyleSVG()->mStrokeOpacity)
      flags |= SVG_HIT_TEST_CHECK_MRECT;
    break;
  case NS_STYLE_POINTER_EVENTS_FILL:
    flags |= SVG_HIT_TEST_FILL;
    break;
  case NS_STYLE_POINTER_EVENTS_STROKE:
    flags |= SVG_HIT_TEST_STROKE;
    break;
  case NS_STYLE_POINTER_EVENTS_ALL:
    flags |= SVG_HIT_TEST_FILL | SVG_HIT_TEST_STROKE;
    break;
  default:
    NS_ERROR("not reached");
    break;
  }

  return flags;
}

bool
nsSVGUtils::PaintSVGGlyph(Element* aElement, gfxContext* aContext,
                          gfxTextContextPaint* aContextPaint)
{
  nsIFrame* frame = aElement->GetPrimaryFrame();
  nsISVGChildFrame* svgFrame = do_QueryFrame(frame);
  if (!svgFrame) {
    return false;
  }
  aContext->GetDrawTarget()->AddUserData(&gfxTextContextPaint::sUserDataKey,
                                         aContextPaint, nullptr);
  gfxMatrix m;
  if (frame->GetContent()->IsSVGElement()) {
    // PaintSVG() expects the passed transform to be the transform to its own
    // SVG user space, so we need to account for any 'transform' attribute:
    m = static_cast<nsSVGElement*>(frame->GetContent())->
          PrependLocalTransformsTo(gfxMatrix(), eUserSpaceToParent);
  }
  nsresult rv = svgFrame->PaintSVG(*aContext, m);
  return NS_SUCCEEDED(rv);
}

bool
nsSVGUtils::GetSVGGlyphExtents(Element* aElement,
                               const gfxMatrix& aSVGToAppSpace,
                               gfxRect* aResult)
{
  nsIFrame* frame = aElement->GetPrimaryFrame();
  nsISVGChildFrame* svgFrame = do_QueryFrame(frame);
  if (!svgFrame) {
    return false;
  }

  gfxMatrix transform(aSVGToAppSpace);
  nsIContent* content = frame->GetContent();
  if (content->IsSVGElement()) {
    transform = static_cast<nsSVGElement*>(content)->
                  PrependLocalTransformsTo(aSVGToAppSpace);
  }

  *aResult = svgFrame->GetBBoxContribution(gfx::ToMatrix(transform),
    nsSVGUtils::eBBoxIncludeFill | nsSVGUtils::eBBoxIncludeFillGeometry |
    nsSVGUtils::eBBoxIncludeStroke | nsSVGUtils::eBBoxIncludeStrokeGeometry |
    nsSVGUtils::eBBoxIncludeMarkers).ToThebesRect();
  return true;
}

nsRect
nsSVGUtils::ToCanvasBounds(const gfxRect &aUserspaceRect,
                           const gfxMatrix &aToCanvas,
                           const nsPresContext *presContext)
{
  return nsLayoutUtils::RoundGfxRectToAppRect(
                          aToCanvas.TransformBounds(aUserspaceRect),
                          presContext->AppUnitsPerDevPixel());
}

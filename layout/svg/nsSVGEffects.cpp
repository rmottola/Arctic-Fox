/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Main header first:
#include "nsSVGEffects.h"

// Keep others in (case-insensitive) order:
#include "mozilla/RestyleManagerHandle.h"
#include "mozilla/RestyleManagerHandleInlines.h"
#include "nsCSSFrameConstructor.h"
#include "nsISupportsImpl.h"
#include "nsSVGClipPathFrame.h"
#include "nsSVGPaintServerFrame.h"
#include "nsSVGPathGeometryElement.h"
#include "nsSVGFilterFrame.h"
#include "nsSVGMaskFrame.h"
#include "nsIReflowCallback.h"
#include "nsCycleCollectionParticipant.h"

using namespace mozilla;
using namespace mozilla::dom;

void
nsSVGRenderingObserver::StartListening()
{
  Element* target = GetTarget();
  if (target) {
    target->AddMutationObserver(this);
  }
}

void
nsSVGRenderingObserver::StopListening()
{
  Element* target = GetTarget();

  if (target) {
    target->RemoveMutationObserver(this);
    if (mInObserverList) {
      nsSVGEffects::RemoveRenderingObserver(target, this);
      mInObserverList = false;
    }
  }
  NS_ASSERTION(!mInObserverList, "still in an observer list?");
}

static nsSVGRenderingObserverList *
GetObserverList(Element *aElement)
{
  return static_cast<nsSVGRenderingObserverList*>
    (aElement->GetProperty(nsGkAtoms::renderingobserverlist));
}

Element*
nsSVGRenderingObserver::GetReferencedElement()
{
  Element* target = GetTarget();
#ifdef DEBUG
  if (target) {
    nsSVGRenderingObserverList *observerList = GetObserverList(target);
    bool inObserverList = observerList && observerList->Contains(this);
    NS_ASSERTION(inObserverList == mInObserverList, "failed to track whether we're in our referenced element's observer list!");
  } else {
    NS_ASSERTION(!mInObserverList, "In whose observer list are we, then?");
  }
#endif
  if (target && !mInObserverList) {
    nsSVGEffects::AddRenderingObserver(target, this);
    mInObserverList = true;
  }
  return target;
}

nsIFrame*
nsSVGRenderingObserver::GetReferencedFrame()
{
  Element* referencedElement = GetReferencedElement();
  return referencedElement ? referencedElement->GetPrimaryFrame() : nullptr;
}

nsIFrame*
nsSVGRenderingObserver::GetReferencedFrame(nsIAtom* aFrameType, bool* aOK)
{
  nsIFrame* frame = GetReferencedFrame();
  if (frame) {
    if (frame->GetType() == aFrameType)
      return frame;
    if (aOK) {
      *aOK = false;
    }
  }
  return nullptr;
}

void
nsSVGRenderingObserver::InvalidateViaReferencedElement()
{
  mInObserverList = false;
  DoUpdate();
}

void
nsSVGRenderingObserver::NotifyEvictedFromRenderingObserverList()
{
  mInObserverList = false; // We've been removed from rendering-obs. list.
  StopListening();            // Remove ourselves from mutation-obs. list.
}

void
nsSVGRenderingObserver::AttributeChanged(nsIDocument* aDocument,
                                         dom::Element* aElement,
                                         int32_t aNameSpaceID,
                                         nsIAtom* aAttribute,
                                         int32_t aModType,
                                         const nsAttrValue* aOldValue)
{
  // An attribute belonging to the element that we are observing *or one of its
  // descendants* has changed.
  //
  // In the case of observing a gradient element, say, we want to know if any
  // of its 'stop' element children change, but we don't actually want to do
  // anything for changes to SMIL element children, for example. Maybe it's not
  // worth having logic to optimize for that, but in most cases it could be a
  // small check?
  //
  // XXXjwatt: do we really want to blindly break the link between our
  // observers and ourselves for all attribute changes? For non-ID changes
  // surely that is unnecessary.

  DoUpdate();
}

void
nsSVGRenderingObserver::ContentAppended(nsIDocument *aDocument,
                                        nsIContent *aContainer,
                                        nsIContent *aFirstNewContent,
                                        int32_t /* unused */)
{
  DoUpdate();
}

void
nsSVGRenderingObserver::ContentInserted(nsIDocument *aDocument,
                                        nsIContent *aContainer,
                                        nsIContent *aChild,
                                        int32_t /* unused */)
{
  DoUpdate();
}

void
nsSVGRenderingObserver::ContentRemoved(nsIDocument *aDocument,
                                       nsIContent *aContainer,
                                       nsIContent *aChild,
                                       int32_t aIndexInContainer,
                                       nsIContent *aPreviousSibling)
{
  DoUpdate();
}

/**
 * Note that in the current setup there are two separate observer lists.
 *
 * In nsSVGIDRenderingObserver's ctor, the new object adds itself to the
 * mutation observer list maintained by the referenced element. In this way the
 * nsSVGIDRenderingObserver is notified if there are any attribute or content
 * tree changes to the element or any of its *descendants*.
 *
 * In nsSVGIDRenderingObserver::GetReferencedElement() the
 * nsSVGIDRenderingObserver object also adds itself to an
 * nsSVGRenderingObserverList object belonging to the referenced
 * element.
 *
 * XXX: it would be nice to have a clear and concise executive summary of the
 * benefits/necessity of maintaining a second observer list.
 */

nsSVGIDRenderingObserver::nsSVGIDRenderingObserver(nsIURI* aURI,
                                                   nsIContent* aObservingContent,
                                                   bool aReferenceImage)
  : mElement(this)
{
  // Start watching the target element
  mElement.Reset(aObservingContent, aURI, true, aReferenceImage);
  StartListening();
}

nsSVGIDRenderingObserver::~nsSVGIDRenderingObserver()
{
  StopListening();
}

void
nsSVGIDRenderingObserver::DoUpdate()
{
  if (mElement.get() && mInObserverList) {
    nsSVGEffects::RemoveRenderingObserver(mElement.get(), this);
    mInObserverList = false;
  }
}

void
nsSVGFrameReferenceFromProperty::Detach()
{
  mFrame = nullptr;
  mFramePresShell = nullptr;
}

nsIFrame*
nsSVGFrameReferenceFromProperty::Get()
{
  if (mFramePresShell && mFramePresShell->IsDestroying()) {
    // mFrame is no longer valid.
    Detach();
  }
  return mFrame;
}

NS_IMPL_ISUPPORTS(nsSVGRenderingObserverProperty, nsIMutationObserver)

void
nsSVGRenderingObserverProperty::DoUpdate()
{
  nsSVGIDRenderingObserver::DoUpdate();

  nsIFrame* frame = mFrameReference.Get();
  if (frame && frame->IsFrameOfType(nsIFrame::eSVG)) {
    // Changes should propagate out to things that might be observing
    // the referencing frame or its ancestors.
    nsLayoutUtils::PostRestyleEvent(
      frame->GetContent()->AsElement(), nsRestyleHint(0),
      nsChangeHint_InvalidateRenderingObservers);
  }
}

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsSVGFilterReference)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsSVGFilterReference)

NS_IMPL_CYCLE_COLLECTION(nsSVGFilterReference, mElement)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsSVGFilterReference)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsSVGIDRenderingObserver)
  NS_INTERFACE_MAP_ENTRY(nsIMutationObserver)
  NS_INTERFACE_MAP_ENTRY(nsISVGFilterReference)
NS_INTERFACE_MAP_END

nsSVGFilterFrame *
nsSVGFilterReference::GetFilterFrame()
{
  return static_cast<nsSVGFilterFrame *>
    (GetReferencedFrame(nsGkAtoms::svgFilterFrame, nullptr));
}

void
nsSVGFilterReference::DoUpdate()
{
  nsSVGIDRenderingObserver::DoUpdate();

  if (mFilterChainObserver) {
    mFilterChainObserver->Invalidate();
  }
}

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsSVGFilterChainObserver)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsSVGFilterChainObserver)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsSVGFilterChainObserver)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTION(nsSVGFilterChainObserver, mReferences)

nsSVGFilterChainObserver::nsSVGFilterChainObserver(const nsTArray<nsStyleFilter>& aFilters,
                                                   nsIContent* aFilteredElement)
{
  for (uint32_t i = 0; i < aFilters.Length(); i++) {
    if (aFilters[i].GetType() != NS_STYLE_FILTER_URL)
      continue;

    RefPtr<nsSVGFilterReference> reference =
      new nsSVGFilterReference(aFilters[i].GetURL(), aFilteredElement, this);
    mReferences.AppendElement(reference);
  }
}

nsSVGFilterChainObserver::~nsSVGFilterChainObserver()
{
  for (uint32_t i = 0; i < mReferences.Length(); i++) {
    mReferences[i]->DetachFromChainObserver();
  }
}

bool
nsSVGFilterChainObserver::ReferencesValidResources()
{
  for (uint32_t i = 0; i < mReferences.Length(); i++) {
    if (!mReferences[i]->ReferencesValidResource())
      return false;
  }
  return true;
}

bool
nsSVGFilterChainObserver::IsInObserverLists() const
{
  for (uint32_t i = 0; i < mReferences.Length(); i++) {
    if (!mReferences[i]->IsInObserverList())
      return false;
  }
  return true;
}

void
nsSVGFilterProperty::DoUpdate()
{
  nsIFrame* frame = mFrameReference.Get();
  if (!frame)
    return;

  // Repaint asynchronously in case the filter frame is being torn down
  nsChangeHint changeHint =
    nsChangeHint(nsChangeHint_RepaintFrame);

  if (frame && frame->IsFrameOfType(nsIFrame::eSVG)) {
    // Changes should propagate out to things that might be observing
    // the referencing frame or its ancestors.
    NS_UpdateHint(changeHint, nsChangeHint_InvalidateRenderingObservers);
  }

  // Don't need to request UpdateOverflow if we're being reflowed.
  if (!(frame->GetStateBits() & NS_FRAME_IN_REFLOW)) {
    NS_UpdateHint(changeHint, nsChangeHint_UpdateOverflow);
  }
  frame->PresContext()->RestyleManager()->PostRestyleEvent(
    frame->GetContent()->AsElement(), nsRestyleHint(0), changeHint);
}

void
nsSVGMarkerProperty::DoUpdate()
{
  nsSVGRenderingObserverProperty::DoUpdate();

  nsIFrame* frame = mFrameReference.Get();
  if (!frame)
    return;

  NS_ASSERTION(frame->IsFrameOfType(nsIFrame::eSVG), "SVG frame expected");

  // Repaint asynchronously in case the marker frame is being torn down
  nsChangeHint changeHint =
    nsChangeHint(nsChangeHint_RepaintFrame);

  // Don't need to request ReflowFrame if we're being reflowed.
  if (!(frame->GetStateBits() & NS_FRAME_IN_REFLOW)) {
    NS_UpdateHint(changeHint, nsChangeHint_InvalidateRenderingObservers);
    // XXXjwatt: We need to unify SVG into standard reflow so we can just use
    // nsChangeHint_NeedReflow | nsChangeHint_NeedDirtyReflow here.
    // XXXSDL KILL THIS!!!
    nsSVGUtils::ScheduleReflowSVG(frame);
  }
  frame->PresContext()->RestyleManager()->PostRestyleEvent(
    frame->GetContent()->AsElement(), nsRestyleHint(0), changeHint);
}

bool
nsSVGTextPathProperty::TargetIsValid()
{
  Element* target = GetTarget();
  return target && target->IsSVGElement(nsGkAtoms::path);
}

void
nsSVGTextPathProperty::DoUpdate()
{
  nsSVGRenderingObserverProperty::DoUpdate();

  nsIFrame* frame = mFrameReference.Get();
  if (!frame)
    return;

  NS_ASSERTION(frame->IsFrameOfType(nsIFrame::eSVG) || frame->IsSVGText(),
               "SVG frame expected");

  // Avoid getting into an infinite loop of reflows if the <textPath> is
  // pointing to one of its ancestors.  TargetIsValid returns true iff
  // the target element is a <path> element, and we would not have this
  // nsSVGTextPathProperty if this <textPath> were a descendant of the
  // target <path>.
  //
  // Note that we still have to post the restyle event when we
  // change from being valid to invalid, so that mPositions on the
  // SVGTextFrame gets updated, skipping the <textPath>, ensuring
  // that nothing gets painted for that element.
  bool nowValid = TargetIsValid();
  if (!mValid && !nowValid) {
    // Just return if we were previously invalid, and are still invalid.
    return;
  }
  mValid = nowValid;

  // Repaint asynchronously in case the path frame is being torn down
  nsChangeHint changeHint =
    nsChangeHint(nsChangeHint_RepaintFrame | nsChangeHint_UpdateTextPath);
  frame->PresContext()->RestyleManager()->PostRestyleEvent(
    frame->GetContent()->AsElement(), nsRestyleHint(0), changeHint);
}

static void
InvalidateAllContinuations(nsIFrame* aFrame)
{
  for (nsIFrame* f = aFrame; f;
       f = nsLayoutUtils::GetNextContinuationOrIBSplitSibling(f)) {
    f->InvalidateFrame();
  }
}

void
nsSVGPaintingProperty::DoUpdate()
{
  nsSVGRenderingObserverProperty::DoUpdate();

  nsIFrame* frame = mFrameReference.Get();
  if (!frame)
    return;

  if (frame->GetStateBits() & NS_FRAME_SVG_LAYOUT) {
    nsLayoutUtils::PostRestyleEvent(
      frame->GetContent()->AsElement(), nsRestyleHint(0),
      nsChangeHint_InvalidateRenderingObservers);
    frame->InvalidateFrameSubtree();
  } else {
    InvalidateAllContinuations(frame);
  }
}

static nsSVGRenderingObserver *
CreateMarkerProperty(nsIURI *aURI, nsIFrame *aFrame, bool aReferenceImage)
{ return new nsSVGMarkerProperty(aURI, aFrame, aReferenceImage); }

static nsSVGRenderingObserver *
CreateTextPathProperty(nsIURI *aURI, nsIFrame *aFrame, bool aReferenceImage)
{ return new nsSVGTextPathProperty(aURI, aFrame, aReferenceImage); }

static nsSVGRenderingObserver *
CreatePaintingProperty(nsIURI *aURI, nsIFrame *aFrame, bool aReferenceImage)
{ return new nsSVGPaintingProperty(aURI, aFrame, aReferenceImage); }

static nsSVGRenderingObserver *
GetEffectProperty(nsIURI *aURI, nsIFrame *aFrame,
                  nsSVGEffects::ObserverPropertyDescriptor aProperty,
                  nsSVGRenderingObserver * (* aCreate)(nsIURI *, nsIFrame *, bool))
{
  if (!aURI)
    return nullptr;

  FrameProperties props = aFrame->Properties();
  nsSVGRenderingObserver *prop =
    static_cast<nsSVGRenderingObserver*>(props.Get(aProperty));
  if (prop)
    return prop;
  prop = aCreate(aURI, aFrame, false);
  if (!prop)
    return nullptr;
  NS_ADDREF(prop);
  props.Set(aProperty, static_cast<nsISupports*>(prop));
  return prop;
}

static nsSVGFilterProperty*
GetOrCreateFilterProperty(nsIFrame *aFrame)
{
  const nsStyleSVGReset* style = aFrame->StyleSVGReset();
  if (!style->HasFilters())
    return nullptr;

  FrameProperties props = aFrame->Properties();
  nsSVGFilterProperty *prop = props.Get(nsSVGEffects::FilterProperty());
  if (prop)
    return prop;
  prop = new nsSVGFilterProperty(style->mFilters, aFrame);
  if (!prop)
    return nullptr;
  NS_ADDREF(prop);
  props.Set(nsSVGEffects::FilterProperty(), prop);
  return prop;
}

nsSVGMarkerProperty *
nsSVGEffects::GetMarkerProperty(nsIURI *aURI, nsIFrame *aFrame,
                                ObserverPropertyDescriptor aProp)
{
  MOZ_ASSERT(aFrame->GetType() == nsGkAtoms::svgPathGeometryFrame &&
               static_cast<nsSVGPathGeometryElement*>(aFrame->GetContent())->IsMarkable(),
             "Bad frame");
  return static_cast<nsSVGMarkerProperty*>(
          GetEffectProperty(aURI, aFrame, aProp, CreateMarkerProperty));
}

nsSVGTextPathProperty *
nsSVGEffects::GetTextPathProperty(nsIURI *aURI, nsIFrame *aFrame,
                                  ObserverPropertyDescriptor aProp)
{
  return static_cast<nsSVGTextPathProperty*>(
          GetEffectProperty(aURI, aFrame, aProp, CreateTextPathProperty));
}

nsSVGPaintingProperty *
nsSVGEffects::GetPaintingProperty(nsIURI *aURI, nsIFrame *aFrame,
                                  ObserverPropertyDescriptor aProp)
{
  return static_cast<nsSVGPaintingProperty*>(
          GetEffectProperty(aURI, aFrame, aProp, CreatePaintingProperty));
}

static nsSVGRenderingObserver *
GetEffectPropertyForURI(nsIURI *aURI, nsIFrame *aFrame,
                        nsSVGEffects::URIObserverHashtablePropertyDescriptor aProperty,
                        nsSVGRenderingObserver * (* aCreate)(nsIURI *, nsIFrame *, bool))
{
  if (!aURI)
    return nullptr;

  FrameProperties props = aFrame->Properties();
  nsSVGEffects::URIObserverHashtable *hashtable = props.Get(aProperty);
  if (!hashtable) {
    hashtable = new nsSVGEffects::URIObserverHashtable();
    props.Set(aProperty, hashtable);
  }
  nsSVGRenderingObserver* prop =
    static_cast<nsSVGRenderingObserver*>(hashtable->GetWeak(aURI));
  if (!prop) {
    bool watchImage = aProperty == nsSVGEffects::BackgroundImageProperty();
    prop = aCreate(aURI, aFrame, watchImage);
    hashtable->Put(aURI, prop);
  }
  return prop;
}

nsSVGPaintingProperty *
nsSVGEffects::GetPaintingPropertyForURI(nsIURI *aURI, nsIFrame *aFrame,
                                        URIObserverHashtablePropertyDescriptor aProp)
{
  return static_cast<nsSVGPaintingProperty*>(
          GetEffectPropertyForURI(aURI, aFrame, aProp, CreatePaintingProperty));
}

nsSVGEffects::EffectProperties
nsSVGEffects::GetEffectProperties(nsIFrame *aFrame)
{
  NS_ASSERTION(!aFrame->GetPrevContinuation(), "aFrame should be first continuation");

  EffectProperties result;
  const nsStyleSVGReset *style = aFrame->StyleSVGReset();
  result.mFilter = GetOrCreateFilterProperty(aFrame);
  if (style->mClipPath.GetType() == NS_STYLE_CLIP_PATH_URL) {
    result.mClipPath =
      GetPaintingProperty(style->mClipPath.GetURL(), aFrame, ClipPathProperty());
  } else {
    result.mClipPath = nullptr;
  }

  // FIXME: Bug 1228280.
  // Before fixing bug 1228280, we support only single svg mask as before.
  MOZ_ASSERT(style->mMask.mImageCount > 0);
  nsCOMPtr<nsIURI> uri = style->mMask.mLayers[0].mSourceURI;
  result.mMask = uri ? GetPaintingProperty(uri, aFrame, MaskProperty()) :
                         nullptr;

  return result;
}

nsSVGPaintServerFrame *
nsSVGEffects::GetPaintServer(nsIFrame *aTargetFrame, const nsStyleSVGPaint *aPaint,
                             ObserverPropertyDescriptor aType)
{
  if (aPaint->mType != eStyleSVGPaintType_Server)
    return nullptr;

  // If we're looking at a frame within SVG text, then we need to look up
  // to find the right frame to get the painting property off.  We should at
  // least look up past a text frame, and if the text frame's parent is the
  // anonymous block frame, then we look up to its parent (the SVGTextFrame).
  nsIFrame* frame = aTargetFrame;
  if (frame->GetContent()->IsNodeOfType(nsINode::eTEXT)) {
    frame = frame->GetParent();
    nsIFrame* grandparent = frame->GetParent();
    if (grandparent && grandparent->GetType() == nsGkAtoms::svgTextFrame) {
      frame = grandparent;
    }
  }
  nsSVGPaintingProperty *property =
    nsSVGEffects::GetPaintingProperty(aPaint->mPaint.mPaintServer, frame, aType);
  if (!property)
    return nullptr;
  nsIFrame *result = property->GetReferencedFrame();
  if (!result)
    return nullptr;

  nsIAtom *type = result->GetType();
  if (type != nsGkAtoms::svgLinearGradientFrame &&
      type != nsGkAtoms::svgRadialGradientFrame &&
      type != nsGkAtoms::svgPatternFrame)
    return nullptr;

  return static_cast<nsSVGPaintServerFrame*>(result);
}

nsSVGClipPathFrame *
nsSVGEffects::EffectProperties::GetClipPathFrame(bool *aOK)
{
  if (!mClipPath)
    return nullptr;
  nsSVGClipPathFrame *frame = static_cast<nsSVGClipPathFrame *>
    (mClipPath->GetReferencedFrame(nsGkAtoms::svgClipPathFrame, aOK));
  if (frame && aOK && *aOK) {
    *aOK = frame->IsValid();
  }
  return frame;
}

nsSVGMaskFrame *
nsSVGEffects::EffectProperties::GetMaskFrame(bool *aOK)
{
  if (!mMask)
    return nullptr;
  return static_cast<nsSVGMaskFrame *>
    (mMask->GetReferencedFrame(nsGkAtoms::svgMaskFrame, aOK));
}

void
nsSVGEffects::UpdateEffects(nsIFrame *aFrame)
{
  NS_ASSERTION(aFrame->GetContent()->IsElement(),
               "aFrame's content should be an element");

  FrameProperties props = aFrame->Properties();
  props.Delete(FilterProperty());
  props.Delete(MaskProperty());
  props.Delete(ClipPathProperty());
  props.Delete(MarkerBeginProperty());
  props.Delete(MarkerMiddleProperty());
  props.Delete(MarkerEndProperty());
  props.Delete(FillProperty());
  props.Delete(StrokeProperty());
  props.Delete(BackgroundImageProperty());

  // Ensure that the filter is repainted correctly
  // We can't do that in DoUpdate as the referenced frame may not be valid
  GetOrCreateFilterProperty(aFrame);

  if (aFrame->GetType() == nsGkAtoms::svgPathGeometryFrame &&
      static_cast<nsSVGPathGeometryElement*>(aFrame->GetContent())->IsMarkable()) {
    // Set marker properties here to avoid reference loops
    const nsStyleSVG *style = aFrame->StyleSVG();
    GetEffectProperty(style->mMarkerStart, aFrame, MarkerBeginProperty(),
                      CreateMarkerProperty);
    GetEffectProperty(style->mMarkerMid, aFrame, MarkerMiddleProperty(),
                      CreateMarkerProperty);
    GetEffectProperty(style->mMarkerEnd, aFrame, MarkerEndProperty(),
                      CreateMarkerProperty);
  }
}

nsSVGFilterProperty *
nsSVGEffects::GetFilterProperty(nsIFrame *aFrame)
{
  NS_ASSERTION(!aFrame->GetPrevContinuation(), "aFrame should be first continuation");

  if (!aFrame->StyleSVGReset()->HasFilters())
    return nullptr;

  return static_cast<nsSVGFilterProperty *>
    (aFrame->Properties().Get(FilterProperty()));
}

void
nsSVGRenderingObserverList::InvalidateAll()
{
  if (mObservers.Count() == 0)
    return;

  AutoTArray<nsSVGRenderingObserver*,10> observers;

  for (auto it = mObservers.Iter(); !it.Done(); it.Next()) {
    observers.AppendElement(it.Get()->GetKey());
  }
  mObservers.Clear();

  for (uint32_t i = 0; i < observers.Length(); ++i) {
    observers[i]->InvalidateViaReferencedElement();
  }
}

void
nsSVGRenderingObserverList::InvalidateAllForReflow()
{
  if (mObservers.Count() == 0)
    return;

  AutoTArray<nsSVGRenderingObserver*,10> observers;

  for (auto it = mObservers.Iter(); !it.Done(); it.Next()) {
    nsSVGRenderingObserver* obs = it.Get()->GetKey();
    if (obs->ObservesReflow()) {
      observers.AppendElement(obs);
      it.Remove();
    }
  }

  for (uint32_t i = 0; i < observers.Length(); ++i) {
    observers[i]->InvalidateViaReferencedElement();
  }
}

void
nsSVGRenderingObserverList::RemoveAll()
{
  AutoTArray<nsSVGRenderingObserver*,10> observers;

  for (auto it = mObservers.Iter(); !it.Done(); it.Next()) {
    observers.AppendElement(it.Get()->GetKey());
  }
  mObservers.Clear();

  // Our list is now cleared.  We need to notify the observers we've removed,
  // so they can update their state & remove themselves as mutation-observers.
  for (uint32_t i = 0; i < observers.Length(); ++i) {
    observers[i]->NotifyEvictedFromRenderingObserverList();
  }
}

void
nsSVGEffects::AddRenderingObserver(Element *aElement, nsSVGRenderingObserver *aObserver)
{
  nsSVGRenderingObserverList *observerList = GetObserverList(aElement);
  if (!observerList) {
    observerList = new nsSVGRenderingObserverList();
    if (!observerList)
      return;
    aElement->SetProperty(nsGkAtoms::renderingobserverlist, observerList,
                          nsINode::DeleteProperty<nsSVGRenderingObserverList>);
  }
  aElement->SetHasRenderingObservers(true);
  observerList->Add(aObserver);
}

void
nsSVGEffects::RemoveRenderingObserver(Element *aElement, nsSVGRenderingObserver *aObserver)
{
  nsSVGRenderingObserverList *observerList = GetObserverList(aElement);
  if (observerList) {
    NS_ASSERTION(observerList->Contains(aObserver),
                 "removing observer from an element we're not observing?");
    observerList->Remove(aObserver);
    if (observerList->IsEmpty()) {
      aElement->SetHasRenderingObservers(false);
    }
  }
}

void
nsSVGEffects::RemoveAllRenderingObservers(Element *aElement)
{
  nsSVGRenderingObserverList *observerList = GetObserverList(aElement);
  if (observerList) {
    observerList->RemoveAll();
    aElement->SetHasRenderingObservers(false);
  }
}

void
nsSVGEffects::InvalidateRenderingObservers(nsIFrame *aFrame)
{
  NS_ASSERTION(!aFrame->GetPrevContinuation(), "aFrame must be first continuation");

  nsIContent* content = aFrame->GetContent();
  if (!content || !content->IsElement())
    return;

  // If the rendering has changed, the bounds may well have changed too:
  aFrame->Properties().Delete(nsSVGUtils::ObjectBoundingBoxProperty());

  nsSVGRenderingObserverList *observerList =
    GetObserverList(content->AsElement());
  if (observerList) {
    observerList->InvalidateAll();
    return;
  }

  // Check ancestor SVG containers. The root frame cannot be of type
  // eSVGContainer so we don't have to check f for null here.
  for (nsIFrame *f = aFrame->GetParent();
       f->IsFrameOfType(nsIFrame::eSVGContainer); f = f->GetParent()) {
    if (f->GetContent()->IsElement()) {
      observerList = GetObserverList(f->GetContent()->AsElement());
      if (observerList) {
        observerList->InvalidateAll();
        return;
      }
    }
  }
}

void
nsSVGEffects::InvalidateDirectRenderingObservers(Element *aElement, uint32_t aFlags /* = 0 */)
{
  nsIFrame* frame = aElement->GetPrimaryFrame();
  if (frame) {
    // If the rendering has changed, the bounds may well have changed too:
    frame->Properties().Delete(nsSVGUtils::ObjectBoundingBoxProperty());
  }

  if (aElement->HasRenderingObservers()) {
    nsSVGRenderingObserverList *observerList = GetObserverList(aElement);
    if (observerList) {
      if (aFlags & INVALIDATE_REFLOW) {
        observerList->InvalidateAllForReflow();
      } else {
        observerList->InvalidateAll();
      }
    }
  }
}

void
nsSVGEffects::InvalidateDirectRenderingObservers(nsIFrame *aFrame, uint32_t aFlags /* = 0 */)
{
  nsIContent* content = aFrame->GetContent();
  if (content && content->IsElement()) {
    InvalidateDirectRenderingObservers(content->AsElement(), aFlags);
  }
}

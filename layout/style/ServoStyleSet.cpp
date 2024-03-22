/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/ServoStyleSet.h"

#include "nsCSSAnonBoxes.h"
#include "nsStyleSet.h"

using namespace mozilla;
using namespace mozilla::dom;

ServoStyleSet::ServoStyleSet()
  : mRawSet(Servo_InitStyleSet())
  , mBatching(0)
{
}

void
ServoStyleSet::Init(nsPresContext* aPresContext)
{
}

void
ServoStyleSet::BeginShutdown()
{
}

void
ServoStyleSet::Shutdown()
{
  mRawSet = nullptr;
}

bool
ServoStyleSet::GetAuthorStyleDisabled() const
{
  return false;
}

nsresult
ServoStyleSet::SetAuthorStyleDisabled(bool aStyleDisabled)
{
  MOZ_CRASH("stylo: not implemented");
}

void
ServoStyleSet::BeginUpdate()
{
  ++mBatching;
}

nsresult
ServoStyleSet::EndUpdate()
{
  MOZ_ASSERT(mBatching > 0);
  if (--mBatching > 0) {
    return NS_OK;
  }

  // ... do something ...
  return NS_OK;
}

// resolve a style context
already_AddRefed<nsStyleContext>
ServoStyleSet::ResolveStyleFor(Element* aElement,
                               nsStyleContext* aParentContext)
{
  MOZ_CRASH("stylo: not implemented");
}

already_AddRefed<nsStyleContext>
ServoStyleSet::ResolveStyleFor(Element* aElement,
                               nsStyleContext* aParentContext,
                               TreeMatchContext& aTreeMatchContext)
{
  MOZ_CRASH("stylo: not implemented");
}

already_AddRefed<nsStyleContext>
ServoStyleSet::ResolveStyleForNonElement(nsStyleContext* aParentContext)
{
  MOZ_CRASH("stylo: not implemented");
}

already_AddRefed<nsStyleContext>
ServoStyleSet::ResolvePseudoElementStyle(Element* aParentElement,
                                         CSSPseudoElementType aType,
                                         nsStyleContext* aParentContext,
                                         Element* aPseudoElement)
{
  MOZ_CRASH("stylo: not implemented");
}

// aFlags is an nsStyleSet flags bitfield
already_AddRefed<nsStyleContext>
ServoStyleSet::ResolveAnonymousBoxStyle(nsIAtom* aPseudoTag,
                                        nsStyleContext* aParentContext,
                                        uint32_t aFlags)
{
  MOZ_ASSERT(nsCSSAnonBoxes::IsAnonBox(aPseudoTag));

  // FIXME(heycam): Do something with eSkipParentDisplayBasedStyleFixup,
  // which is the only value of aFlags that can be passed in.
  MOZ_ASSERT(aFlags == 0 ||
             aFlags == nsStyleSet::eSkipParentDisplayBasedStyleFixup);

  MOZ_CRASH("stylo: not implemented");
}

// manage the set of style sheets in the style set
nsresult
ServoStyleSet::AppendStyleSheet(SheetType aType,
                                ServoStyleSheet* aSheet)
{
  MOZ_ASSERT(aSheet);
  MOZ_ASSERT(aSheet->IsApplicable());
  MOZ_ASSERT(nsStyleSet::IsCSSSheetType(aType));

  mSheets[aType].RemoveElement(aSheet);
  mSheets[aType].AppendElement(aSheet);

  return NS_OK;
}

nsresult
ServoStyleSet::PrependStyleSheet(SheetType aType,
                                 ServoStyleSheet* aSheet)
{
  MOZ_ASSERT(aSheet);
  MOZ_ASSERT(aSheet->IsApplicable());
  MOZ_ASSERT(nsStyleSet::IsCSSSheetType(aType));

  mSheets[aType].RemoveElement(aSheet);
  mSheets[aType].InsertElementAt(0, aSheet);

  return NS_OK;
}

nsresult
ServoStyleSet::RemoveStyleSheet(SheetType aType,
                                ServoStyleSheet* aSheet)
{
  MOZ_CRASH("stylo: not implemented");
}

nsresult
ServoStyleSet::ReplaceSheets(SheetType aType,
                             const nsTArray<RefPtr<ServoStyleSheet>>& aNewSheets)
{
  MOZ_CRASH("stylo: not implemented");
}

nsresult
ServoStyleSet::InsertStyleSheetBefore(SheetType aType,
                                      ServoStyleSheet* aNewSheet,
                                      ServoStyleSheet* aReferenceSheet)
{
  MOZ_CRASH("stylo: not implemented");
}

int32_t
ServoStyleSet::SheetCount(SheetType aType) const
{
  MOZ_ASSERT(nsStyleSet::IsCSSSheetType(aType));
  return mSheets[aType].Length();
}

ServoStyleSheet*
ServoStyleSet::StyleSheetAt(SheetType aType,
                            int32_t aIndex) const
{
  MOZ_ASSERT(nsStyleSet::IsCSSSheetType(aType));
  return mSheets[aType][aIndex];
}

nsresult
ServoStyleSet::RemoveDocStyleSheet(ServoStyleSheet* aSheet)
{
  MOZ_CRASH("stylo: not implemented");
}

nsresult
ServoStyleSet::AddDocStyleSheet(ServoStyleSheet* aSheet,
                                nsIDocument* aDocument)
{
  MOZ_CRASH("stylo: not implemented");
}

already_AddRefed<nsStyleContext>
ServoStyleSet::ProbePseudoElementStyle(Element* aParentElement,
                                       CSSPseudoElementType aType,
                                       nsStyleContext* aParentContext)
{
  MOZ_CRASH("stylo: not implemented");
}

already_AddRefed<nsStyleContext>
ServoStyleSet::ProbePseudoElementStyle(Element* aParentElement,
                                       CSSPseudoElementType aType,
                                       nsStyleContext* aParentContext,
                                       TreeMatchContext& aTreeMatchContext,
                                       Element* aPseudoElement)
{
  MOZ_CRASH("stylo: not implemented");
}

nsRestyleHint
ServoStyleSet::HasStateDependentStyle(dom::Element* aElement,
                                      EventStates aStateMask)
{
  MOZ_CRASH("stylo: not implemented");
}

nsRestyleHint
ServoStyleSet::HasStateDependentStyle(dom::Element* aElement,
                                      CSSPseudoElementType aPseudoType,
                                     dom::Element* aPseudoElement,
                                     EventStates aStateMask)
{
  MOZ_CRASH("stylo: not implemented");
}

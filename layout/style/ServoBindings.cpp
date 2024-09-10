/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/ServoBindings.h"

#include "nsCSSRuleProcessor.h"
#include "nsContentUtils.h"
#include "nsIDOMNode.h"
#include "nsIDocument.h"
#include "nsINode.h"
#include "nsNameSpaceManager.h"
#include "nsString.h"
#include "nsStyleStruct.h"
#include "StyleStructContext.h"

#include "mozilla/EventStates.h"
#include "mozilla/dom/Element.h"

uint32_t
Gecko_ChildrenCount(RawGeckoNode* aNode)
{
  return aNode->GetChildCount();
}

bool
Gecko_NodeIsElement(RawGeckoNode* aNode)
{
  return aNode->IsElement();
}

RawGeckoNode*
Gecko_GetParentNode(RawGeckoNode* aNode)
{
  return aNode->GetParentNode();
}

RawGeckoNode*
Gecko_GetFirstChild(RawGeckoNode* aNode)
{
  return aNode->GetFirstChild();
}

RawGeckoNode*
Gecko_GetLastChild(RawGeckoNode* aNode)
{
  return aNode->GetLastChild();
}

RawGeckoNode*
Gecko_GetPrevSibling(RawGeckoNode* aNode)
{
  return aNode->GetPreviousSibling();
}

RawGeckoNode*
Gecko_GetNextSibling(RawGeckoNode* aNode)
{
  return aNode->GetNextSibling();
}

RawGeckoElement*
Gecko_GetParentElement(RawGeckoElement* aElement)
{
  return aElement->GetParentElement();
}

RawGeckoElement*
Gecko_GetFirstChildElement(RawGeckoElement* aElement)
{
  return aElement->GetFirstElementChild();
}

RawGeckoElement* Gecko_GetLastChildElement(RawGeckoElement* aElement)
{
  return aElement->GetLastElementChild();
}

RawGeckoElement*
Gecko_GetPrevSiblingElement(RawGeckoElement* aElement)
{
  return aElement->GetPreviousElementSibling();
}

RawGeckoElement*
Gecko_GetNextSiblingElement(RawGeckoElement* aElement)
{
  return aElement->GetNextElementSibling();
}

RawGeckoElement*
Gecko_GetDocumentElement(RawGeckoDocument* aDoc)
{
  return aDoc->GetDocumentElement();
}

uint8_t
Gecko_ElementState(RawGeckoElement* aElement)
{
  return aElement->StyleState().GetInternalValue() &
         ((1 << (NS_EVENT_STATE_HIGHEST_SERVO_BIT + 1)) - 1);
}

bool
Gecko_IsHTMLElementInHTMLDocument(RawGeckoElement* aElement)
{
  return aElement->IsHTMLElement() && aElement->OwnerDoc()->IsHTMLDocument();
}

bool
Gecko_IsLink(RawGeckoElement* aElement)
{
  return nsCSSRuleProcessor::IsLink(aElement);
}

bool
Gecko_IsTextNode(RawGeckoNode* aNode)
{
  return aNode->NodeInfo()->NodeType() == nsIDOMNode::TEXT_NODE;
}

bool
Gecko_IsVisitedLink(RawGeckoElement* aElement)
{
  return aElement->StyleState().HasState(NS_EVENT_STATE_VISITED);
}

bool
Gecko_IsUnvisitedLink(RawGeckoElement* aElement)
{
  return aElement->StyleState().HasState(NS_EVENT_STATE_UNVISITED);
}

bool
Gecko_IsRootElement(RawGeckoElement* aElement)
{
  return aElement->OwnerDoc()->GetRootElement() == aElement;
}

ServoNodeData*
Gecko_GetNodeData(RawGeckoNode* aNode)
{
  return aNode->GetServoNodeData();
}

void
Gecko_SetNodeData(RawGeckoNode* aNode, ServoNodeData* aData)
{
  aNode->SetServoNodeData(aData);
}

#define STYLE_STRUCT(name, checkdata_cb)                                      \
                                                                              \
void                                                                          \
Gecko_Construct_nsStyle##name(nsStyle##name* ptr)                             \
{                                                                             \
  new (ptr) nsStyle##name(StyleStructContext::ServoContext());                \
}                                                                             \
                                                                              \
void                                                                          \
Gecko_CopyConstruct_nsStyle##name(nsStyle##name* ptr,                         \
                                  const nsStyle##name* other)                 \
{                                                                             \
  new (ptr) nsStyle##name(*other);                                            \
}                                                                             \
                                                                              \
void                                                                          \
Gecko_Destroy_nsStyle##name(nsStyle##name* ptr)                               \
{                                                                             \
  ptr->~nsStyle##name();                                                      \
}

#include "nsStyleStructList.h"

#undef STYLE_STRUCT

#ifndef MOZ_STYLO
void
Servo_DropNodeData(ServoNodeData* data)
{
  MOZ_CRASH("stylo: shouldn't be calling Servo_DropNodeData in a "
            "non-MOZ_STYLO build");
}

RawServoStyleSheet*
Servo_StylesheetFromUTF8Bytes(const uint8_t* bytes, uint32_t length,
                              mozilla::css::SheetParsingMode mode)
{
  MOZ_CRASH("stylo: shouldn't be calling Servo_StylesheetFromUTF8Bytes in a "
            "non-MOZ_STYLO build");
}

void
Servo_AddRefStyleSheet(RawServoStyleSheet* sheet)
{
  MOZ_CRASH("stylo: shouldn't be calling Servo_AddRefStylesheet in a "
            "non-MOZ_STYLO build");
}

void
Servo_ReleaseStyleSheet(RawServoStyleSheet* sheet)
{
  MOZ_CRASH("stylo: shouldn't be calling Servo_ReleaseStylesheet in a "
            "non-MOZ_STYLO build");
}

void
Servo_AppendStyleSheet(RawServoStyleSheet* sheet, RawServoStyleSet* set)
{
  MOZ_CRASH("stylo: shouldn't be calling Servo_AppendStyleSheet in a "
            "non-MOZ_STYLO build");
}

void Servo_PrependStyleSheet(RawServoStyleSheet* sheet, RawServoStyleSet* set)
{
  MOZ_CRASH("stylo: shouldn't be calling Servo_PrependStyleSheet in a "
            "non-MOZ_STYLO build");
}

void Servo_RemoveStyleSheet(RawServoStyleSheet* sheet, RawServoStyleSet* set)
{
  MOZ_CRASH("stylo: shouldn't be calling Servo_RemoveStyleSheet in a "
            "non-MOZ_STYLO build");
}

void
Servo_InsertStyleSheetBefore(RawServoStyleSheet* sheet,
                             RawServoStyleSheet* reference,
                             RawServoStyleSet* set)
{
  MOZ_CRASH("stylo: shouldn't be calling Servo_InsertStyleSheetBefore in a "
            "non-MOZ_STYLO build");
}

bool
Servo_StyleSheetHasRules(RawServoStyleSheet* sheet)
{
  MOZ_CRASH("stylo: shouldn't be calling Servo_StyleSheetHasRules in a "
            "non-MOZ_STYLO build");
}

RawServoStyleSet*
Servo_InitStyleSet()
{
  MOZ_CRASH("stylo: shouldn't be calling Servo_InitStyleSet in a "
            "non-MOZ_STYLO build");
}

void
Servo_DropStyleSet(RawServoStyleSet* set)
{
  MOZ_CRASH("stylo: shouldn't be calling Servo_DropStyleSet in a "
            "non-MOZ_STYLO build");
}

ServoComputedValues*
Servo_GetComputedValues(RawGeckoNode* node)
{
  MOZ_CRASH("stylo: shouldn't be calling Servo_GetComputedValues in a "
            "non-MOZ_STYLO build");
}

ServoComputedValues*
Servo_GetComputedValuesForAnonymousBox(ServoComputedValues* parentStyleOrNull,
                                       nsIAtom* pseudoTag,
                                       RawServoStyleSet* set)
{
  MOZ_CRASH("stylo: shouldn't be calling Servo_GetComputedValuesForAnonymousBox in a "
            "non-MOZ_STYLO build");
}

ServoComputedValues*
Servo_GetComputedValuesForPseudoElement(ServoComputedValues* parent_style,
                                        RawGeckoElement* match_element,
                                        nsIAtom* pseudo_tag,
                                        RawServoStyleSet* set,
                                        bool is_probe)
{
  MOZ_CRASH("stylo: shouldn't be calling Servo_GetComputedValuesForPseudoElement in a "
            "non-MOZ_STYLO build");
}

void
Servo_AddRefComputedValues(ServoComputedValues*)
{
  MOZ_CRASH("stylo: shouldn't be calling Servo_AddRefComputedValues in a "
            "non-MOZ_STYLO build");
}

void
Servo_ReleaseComputedValues(ServoComputedValues*)
{
  MOZ_CRASH("stylo: shouldn't be calling Servo_ReleaseComputedValues in a "
            "non-MOZ_STYLO build");
}

void
Servo_RestyleDocument(RawGeckoDocument* doc, RawServoStyleSet* set)
{
  MOZ_CRASH("stylo: shouldn't be calling Servo_RestyleDocument in a "
            "non-MOZ_STYLO build");
}

#define STYLE_STRUCT(name_, checkdata_cb_)                                     \
const nsStyle##name_*                                                          \
Servo_GetStyle##name_(ServoComputedValues*)                                    \
{                                                                              \
  MOZ_CRASH("stylo: shouldn't be calling Servo_GetStyle" #name_ " in a "       \
            "non-MOZ_STYLO build");                                            \
}
#include "nsStyleStructList.h"
#undef STYLE_STRUCT
#endif

#ifdef MOZ_STYLO
const nsStyleVariables*
Servo_GetStyleVariables(ServoComputedValues* aComputedValues)
{
  // Servo can't provide us with Variables structs yet, so instead of linking
  // to a Servo_GetStyleVariables defined in Servo we define one here that
  // always returns the same, empty struct.
  static nsStyleVariables variables(StyleStructContext::ServoContext());
  return &variables;
}
#endif

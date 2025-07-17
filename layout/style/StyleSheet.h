/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_StyleSheet_h
#define mozilla_StyleSheet_h

#include "mozilla/css/SheetParsingMode.h"
#include "mozilla/dom/CSSStyleSheetBinding.h"
#include "mozilla/StyleBackendType.h"
#include "mozilla/StyleSheetHandle.h"
#include "mozilla/StyleSheetInfo.h"

class nsIDocument;
class nsINode;

namespace mozilla {

class CSSStyleSheet;
class ServoStyleSheet;

/**
 * Superclass for data common to CSSStyleSheet and ServoStyleSheet.
 */
class StyleSheet
{
protected:
  StyleSheet(StyleBackendType aType, css::SheetParsingMode aParsingMode);
  StyleSheet(const StyleSheet& aCopy,
             nsIDocument* aDocumentToUse,
             nsINode* aOwningNodeToUse);

public:
  void SetOwningNode(nsINode* aOwningNode)
  {
    mOwningNode = aOwningNode;
  }

  css::SheetParsingMode ParsingMode() { return mParsingMode; }
  mozilla::dom::CSSStyleSheetParsingMode ParsingModeDOM();

  nsINode* GetOwnerNode() const { return mOwningNode; }

  // The document this style sheet is associated with.  May be null
  nsIDocument* GetDocument() const { return mDocument; }

  /**
   * Whether the sheet is complete.
   */
  bool IsComplete() const;
  void SetComplete();

  // Get a handle to the various stylesheet bits which live on the 'inner' for
  // gecko stylesheets and live on the StyleSheet for Servo stylesheets.
  StyleSheetInfo& SheetInfo();
  const StyleSheetInfo& SheetInfo() const { return const_cast<StyleSheet*>(this)->SheetInfo(); };

  bool IsGecko() const { return !IsServo(); }
  bool IsServo() const
  {
#ifdef MOZ_STYLO
    return mType == StyleBackendType::Servo;
#else
    return false;
#endif
  }

  // Only safe to call if the caller has verified that that |this| is of the
  // correct type.
  inline CSSStyleSheet& AsGecko();
  inline ServoStyleSheet& AsServo();
  inline StyleSheetHandle AsHandle();

protected:
  nsIDocument*          mDocument; // weak ref; parents maintain this for their children
  nsINode*              mOwningNode; // weak ref

  // mParsingMode controls access to nonstandard style constructs that
  // are not safe for use on the public Web but necessary in UA sheets
  // and/or useful in user sheets.
  css::SheetParsingMode mParsingMode;

  StyleBackendType      mType;
  bool                  mDisabled;
};

} // namespace mozilla

#endif // mozilla_StyleSheet_h

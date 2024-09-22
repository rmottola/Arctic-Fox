/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_SCALEDFONTDWRITE_H_
#define MOZILLA_GFX_SCALEDFONTDWRITE_H_

#include <dwrite.h>
#include "ScaledFontBase.h"

struct ID2D1GeometrySink;

namespace mozilla {
namespace gfx {

class ScaledFontDWrite final : public ScaledFontBase
{
public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(ScaledFontDwrite)
  ScaledFontDWrite(IDWriteFontFace *aFont, Float aSize)
    : ScaledFontBase(aSize)
    , mFont(nullptr)
    , mFontFamily(nullptr)
    , mFontFace(aFont)
  {}

  ScaledFontDWrite(IDWriteFont* aFont, IDWriteFontFamily* aFontFamily,
                   IDWriteFontFace *aFontFace, Float aSize)
    : ScaledFontBase(aSize)
    , mFont(aFont)
    , mFontFamily(aFontFamily)
    , mFontFace(aFontFace)
  {}

  virtual FontType GetType() const { return FontType::DWRITE; }

  virtual already_AddRefed<Path> GetPathForGlyphs(const GlyphBuffer &aBuffer, const DrawTarget *aTarget);
  virtual void CopyGlyphsToBuilder(const GlyphBuffer &aBuffer, PathBuilder *aBuilder, BackendType aBackendType, const Matrix *aTransformHint);

  void CopyGlyphsToSink(const GlyphBuffer &aBuffer, ID2D1GeometrySink *aSink);

  virtual bool GetFontFileData(FontFileDataOutput aDataCallback, void *aBaton);

  virtual AntialiasMode GetDefaultAAMode();

#ifdef USE_SKIA
  virtual SkTypeface* GetSkTypeface();
  void GetFontDataFromSystemFonts(IDWriteFactory* aFactory);
#endif

  // The font and font family are only used with Skia
  RefPtr<IDWriteFont> mFont;
  RefPtr<IDWriteFontFamily> mFontFamily;
  RefPtr<IDWriteFontFace> mFontFace;

protected:
#ifdef USE_CAIRO_SCALED_FONT
  cairo_font_face_t* GetCairoFontFace() override;
#endif
};

class GlyphRenderingOptionsDWrite : public GlyphRenderingOptions
{
public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(GlyphRenderingOptionsDWrite)
  GlyphRenderingOptionsDWrite(IDWriteRenderingParams *aParams)
    : mParams(aParams)
  {
  }

  virtual FontType GetType() const { return FontType::DWRITE; }

private:
  friend class DrawTargetD2D;
  friend class DrawTargetD2D1;

  RefPtr<IDWriteRenderingParams> mParams;
};

}
}

#endif /* MOZILLA_GFX_SCALEDFONTDWRITE_H_ */

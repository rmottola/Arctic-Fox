/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */


#ifndef SkPDFFontImpl_DEFINED
#define SkPDFFontImpl_DEFINED

#include "SkPDFFont.h"

class SkPDFType0Font final : public SkPDFFont {
public:
    virtual ~SkPDFType0Font();
    bool multiByteGlyphs() const override { return true; }
    SkPDFFont* getFontSubset(const SkPDFGlyphSet* usage) override;
#ifdef SK_DEBUG
    void emitObject(SkWStream*,
                    const SkPDFObjNumMap&,
                    const SkPDFSubstituteMap&) const override;
#endif

private:
    friend class SkPDFFont;  // to access the constructor
#ifdef SK_DEBUG
    bool fPopulated;
    typedef SkPDFDict INHERITED;
#endif

    SkPDFType0Font(const SkAdvancedTypefaceMetrics* info,
                   SkTypeface* typeface);

    bool populate(const SkPDFGlyphSet* subset);
};

class SkPDFCIDFont final : public SkPDFFont {
public:
    virtual ~SkPDFCIDFont();
    virtual bool multiByteGlyphs() const { return true; }

private:
    friend class SkPDFType0Font;  // to access the constructor

    SkPDFCIDFont(const SkAdvancedTypefaceMetrics* info,
                 SkTypeface* typeface,
                 const SkPDFGlyphSet* subset);

    bool populate(const SkPDFGlyphSet* subset);
    bool addFontDescriptor(int16_t defaultWidth,
                           const SkTDArray<uint32_t>* subset);
};

class SkPDFType1Font final : public SkPDFFont {
public:
    virtual ~SkPDFType1Font();
    virtual bool multiByteGlyphs() const { return false; }

private:
    friend class SkPDFFont;  // to access the constructor

    SkPDFType1Font(const SkAdvancedTypefaceMetrics* info,
                   SkTypeface* typeface,
                   uint16_t glyphID,
                   SkPDFDict* relatedFontDescriptor);

    bool populate(int16_t glyphID);
    bool addFontDescriptor(int16_t defaultWidth);
    void addWidthInfoFromRange(int16_t defaultWidth,
        const SkAdvancedTypefaceMetrics::WidthRange* widthRangeEntry);
};

class SkPDFType3Font final : public SkPDFFont {
public:
    virtual ~SkPDFType3Font();
    virtual bool multiByteGlyphs() const { return false; }

private:
    friend class SkPDFFont;  // to access the constructor

    SkPDFType3Font(const SkAdvancedTypefaceMetrics* info,
                   SkTypeface* typeface,
                   uint16_t glyphID);

    bool populate(uint16_t glyphID);
};

#endif

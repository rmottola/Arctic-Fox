/*
 * Copyright 2011 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkAdvancedTypefaceMetrics.h"
#include "SkEndian.h"
#include "SkFontDescriptor.h"
#include "SkFontMgr.h"
#include "SkMutex.h"
#include "SkOTTable_OS_2.h"
#include "SkOncePtr.h"
#include "SkStream.h"
#include "SkTypeface.h"

SkTypeface::SkTypeface(const SkFontStyle& style, SkFontID fontID, bool isFixedPitch)
    : fUniqueID(fontID), fStyle(style), fIsFixedPitch(isFixedPitch) { }

SkTypeface::~SkTypeface() { }

#ifdef SK_WHITELIST_SERIALIZED_TYPEFACES
extern void WhitelistSerializeTypeface(const SkTypeface*, SkWStream* );
#define SK_TYPEFACE_DELEGATE WhitelistSerializeTypeface
#else
#define SK_TYPEFACE_DELEGATE nullptr
#endif

SkTypeface* (*gCreateTypefaceDelegate)(const char [], SkTypeface::Style ) = nullptr;
void (*gSerializeTypefaceDelegate)(const SkTypeface*, SkWStream* ) = SK_TYPEFACE_DELEGATE;
SkTypeface* (*gDeserializeTypefaceDelegate)(SkStream* ) = nullptr;

///////////////////////////////////////////////////////////////////////////////

class SkEmptyTypeface : public SkTypeface {
public:
    static SkEmptyTypeface* Create() { return new SkEmptyTypeface; }
protected:
    SkEmptyTypeface() : SkTypeface(SkFontStyle(), 0, true) { }

    SkStreamAsset* onOpenStream(int* ttcIndex) const override { return nullptr; }
    SkScalerContext* onCreateScalerContext(const SkDescriptor*) const override {
        return nullptr;
    }
    void onFilterRec(SkScalerContextRec*) const override { }
    virtual SkAdvancedTypefaceMetrics* onGetAdvancedTypefaceMetrics(
                                PerGlyphInfo,
                                const uint32_t*, uint32_t) const override { return nullptr; }
    void onGetFontDescriptor(SkFontDescriptor*, bool*) const override { }
    virtual int onCharsToGlyphs(const void* chars, Encoding encoding,
                                uint16_t glyphs[], int glyphCount) const override {
        if (glyphs && glyphCount > 0) {
            sk_bzero(glyphs, glyphCount * sizeof(glyphs[0]));
        }
        return 0;
    }
    int onCountGlyphs() const override { return 0; };
    int onGetUPEM() const override { return 0; };
    class EmptyLocalizedStrings : public SkTypeface::LocalizedStrings {
    public:
        bool next(SkTypeface::LocalizedString*) override { return false; }
    };
    void onGetFamilyName(SkString* familyName) const override {
        familyName->reset();
    }
    SkTypeface::LocalizedStrings* onCreateFamilyNameIterator() const override {
        return new EmptyLocalizedStrings;
    };
    int onGetTableTags(SkFontTableTag tags[]) const override { return 0; }
    size_t onGetTableData(SkFontTableTag, size_t, size_t, void*) const override {
        return 0;
    }
};

SK_DECLARE_STATIC_MUTEX(gCreateDefaultMutex);
SK_DECLARE_STATIC_ONCE_PTR(SkTypeface, defaults[4]);

SkTypeface* SkTypeface::GetDefaultTypeface(Style style) {
    SkASSERT((int)style < 4);
    return defaults[style].get([=]{
        // It is not safe to call FontConfigTypeface::LegacyCreateTypeface concurrently.
        // To be safe, we serialize here with a mutex so only one call to
        // CreateTypeface is happening at any given time.
        // TODO(bungeman, mtklein): This is sad.  Make our fontconfig code safe?
        SkAutoMutexAcquire lock(&gCreateDefaultMutex);

        SkAutoTUnref<SkFontMgr> fm(SkFontMgr::RefDefault());
        SkTypeface* t = fm->legacyCreateTypeface(nullptr, style);
        return t ? t : SkEmptyTypeface::Create();
    });
}

SkTypeface* SkTypeface::RefDefault(Style style) {
    return SkRef(GetDefaultTypeface(style));
}

uint32_t SkTypeface::UniqueID(const SkTypeface* face) {
    if (nullptr == face) {
        face = GetDefaultTypeface();
    }
    return face->uniqueID();
}

bool SkTypeface::Equal(const SkTypeface* facea, const SkTypeface* faceb) {
    return facea == faceb || SkTypeface::UniqueID(facea) == SkTypeface::UniqueID(faceb);
}

///////////////////////////////////////////////////////////////////////////////

SkTypeface* SkTypeface::CreateFromName(const char name[], Style style) {
    if (gCreateTypefaceDelegate) {
        SkTypeface* result = (*gCreateTypefaceDelegate)(name, style);
        if (result) {
            return result;
        }
    }
    if (nullptr == name) {
        return RefDefault(style);
    }
    SkAutoTUnref<SkFontMgr> fm(SkFontMgr::RefDefault());
    return fm->legacyCreateTypeface(name, style);
}

SkTypeface* SkTypeface::CreateFromTypeface(const SkTypeface* family, Style s) {
    if (!family) {
        return SkTypeface::RefDefault(s);
    }

    if (family->style() == s) {
        family->ref();
        return const_cast<SkTypeface*>(family);
    }

    SkAutoTUnref<SkFontMgr> fm(SkFontMgr::RefDefault());
    bool bold = s & SkTypeface::kBold;
    bool italic = s & SkTypeface::kItalic;
    SkFontStyle newStyle = SkFontStyle(bold ? SkFontStyle::kBold_Weight
                                            : SkFontStyle::kNormal_Weight,
                                       SkFontStyle::kNormal_Width,
                                       italic ? SkFontStyle::kItalic_Slant
                                              : SkFontStyle::kUpright_Slant);
    return fm->matchFaceStyle(family, newStyle);
}

SkTypeface* SkTypeface::CreateFromStream(SkStreamAsset* stream, int index) {
    SkAutoTUnref<SkFontMgr> fm(SkFontMgr::RefDefault());
    return fm->createFromStream(stream, index);
}

SkTypeface* SkTypeface::CreateFromFontData(SkFontData* data) {
    SkAutoTUnref<SkFontMgr> fm(SkFontMgr::RefDefault());
    return fm->createFromFontData(data);
}

SkTypeface* SkTypeface::CreateFromFile(const char path[], int index) {
    SkAutoTUnref<SkFontMgr> fm(SkFontMgr::RefDefault());
    return fm->createFromFile(path, index);
}

///////////////////////////////////////////////////////////////////////////////

void SkTypeface::serialize(SkWStream* wstream) const {
    if (gSerializeTypefaceDelegate) {
        (*gSerializeTypefaceDelegate)(this, wstream);
        return;
    }
    bool isLocal = false;
    SkFontDescriptor desc(this->style());
    this->onGetFontDescriptor(&desc, &isLocal);

    // Embed font data if it's a local font.
    if (isLocal && !desc.hasFontData()) {
        desc.setFontData(this->onCreateFontData());
    }
    desc.serialize(wstream);
}

SkTypeface* SkTypeface::Deserialize(SkStream* stream) {
    if (gDeserializeTypefaceDelegate) {
        return (*gDeserializeTypefaceDelegate)(stream);
    }

    SkFontDescriptor desc;
    if (!SkFontDescriptor::Deserialize(stream, &desc)) {
        return nullptr;
    }

    SkFontData* data = desc.detachFontData();
    if (data) {
        SkTypeface* typeface = SkTypeface::CreateFromFontData(data);
        if (typeface) {
            return typeface;
        }
    }
    return SkTypeface::CreateFromName(desc.getFamilyName(), desc.getStyle());
}

///////////////////////////////////////////////////////////////////////////////

int SkTypeface::countTables() const {
    return this->onGetTableTags(nullptr);
}

int SkTypeface::getTableTags(SkFontTableTag tags[]) const {
    return this->onGetTableTags(tags);
}

size_t SkTypeface::getTableSize(SkFontTableTag tag) const {
    return this->onGetTableData(tag, 0, ~0U, nullptr);
}

size_t SkTypeface::getTableData(SkFontTableTag tag, size_t offset, size_t length,
                                void* data) const {
    return this->onGetTableData(tag, offset, length, data);
}

SkStreamAsset* SkTypeface::openStream(int* ttcIndex) const {
    int ttcIndexStorage;
    if (nullptr == ttcIndex) {
        // So our subclasses don't need to check for null param
        ttcIndex = &ttcIndexStorage;
    }
    return this->onOpenStream(ttcIndex);
}

SkFontData* SkTypeface::createFontData() const {
    return this->onCreateFontData();
}

// This implementation is temporary until this method can be made pure virtual.
SkFontData* SkTypeface::onCreateFontData() const {
    int index;
    SkAutoTDelete<SkStreamAsset> stream(this->onOpenStream(&index));
    return new SkFontData(stream.release(), index, nullptr, 0);
};

int SkTypeface::charsToGlyphs(const void* chars, Encoding encoding,
                              uint16_t glyphs[], int glyphCount) const {
    if (glyphCount <= 0) {
        return 0;
    }
    if (nullptr == chars || (unsigned)encoding > kUTF32_Encoding) {
        if (glyphs) {
            sk_bzero(glyphs, glyphCount * sizeof(glyphs[0]));
        }
        return 0;
    }
    return this->onCharsToGlyphs(chars, encoding, glyphs, glyphCount);
}

int SkTypeface::countGlyphs() const {
    return this->onCountGlyphs();
}

int SkTypeface::getUnitsPerEm() const {
    // should we try to cache this in the base-class?
    return this->onGetUPEM();
}

bool SkTypeface::getKerningPairAdjustments(const uint16_t glyphs[], int count,
                                           int32_t adjustments[]) const {
    SkASSERT(count >= 0);
    // check for the only legal way to pass a nullptr.. everything is 0
    // in which case they just want to know if this face can possibly support
    // kerning (true) or never (false).
    if (nullptr == glyphs || nullptr == adjustments) {
        SkASSERT(nullptr == glyphs);
        SkASSERT(0 == count);
        SkASSERT(nullptr == adjustments);
    }
    return this->onGetKerningPairAdjustments(glyphs, count, adjustments);
}

SkTypeface::LocalizedStrings* SkTypeface::createFamilyNameIterator() const {
    return this->onCreateFamilyNameIterator();
}

void SkTypeface::getFamilyName(SkString* name) const {
    SkASSERT(name);
    this->onGetFamilyName(name);
}

SkAdvancedTypefaceMetrics* SkTypeface::getAdvancedTypefaceMetrics(
                                PerGlyphInfo info,
                                const uint32_t* glyphIDs,
                                uint32_t glyphIDsCount) const {
    SkAdvancedTypefaceMetrics* result =
            this->onGetAdvancedTypefaceMetrics(info, glyphIDs, glyphIDsCount);
    if (result && result->fType == SkAdvancedTypefaceMetrics::kTrueType_Font) {
        struct SkOTTableOS2 os2table;
        if (this->getTableData(SkTEndian_SwapBE32(SkOTTableOS2::TAG), 0,
                               sizeof(os2table), &os2table) > 0) {
            if (os2table.version.v2.fsType.field.Bitmap ||
                (os2table.version.v2.fsType.field.Restricted &&
                 !(os2table.version.v2.fsType.field.PreviewPrint ||
                   os2table.version.v2.fsType.field.Editable))) {
                result->fFlags = SkTBitOr<SkAdvancedTypefaceMetrics::FontFlags>(
                        result->fFlags,
                        SkAdvancedTypefaceMetrics::kNotEmbeddable_FontFlag);
            }
            if (os2table.version.v2.fsType.field.NoSubsetting) {
                result->fFlags = SkTBitOr<SkAdvancedTypefaceMetrics::FontFlags>(
                        result->fFlags,
                        SkAdvancedTypefaceMetrics::kNotSubsettable_FontFlag);
            }
        }
    }
    return result;
}

bool SkTypeface::onGetKerningPairAdjustments(const uint16_t glyphs[], int count,
                                             int32_t adjustments[]) const {
    return false;
}

///////////////////////////////////////////////////////////////////////////////

#include "SkDescriptor.h"
#include "SkPaint.h"

SkRect SkTypeface::getBounds() const {
    return *fLazyBounds.get([&] {
        SkRect* rect = new SkRect;
        if (!this->onComputeBounds(rect)) {
            rect->setEmpty();
        }
        return rect;
    });
}

bool SkTypeface::onComputeBounds(SkRect* bounds) const {
    // we use a big size to ensure lots of significant bits from the scalercontext.
    // then we scale back down to return our final answer (at 1-pt)
    const SkScalar textSize = 2048;
    const SkScalar invTextSize = 1 / textSize;

    SkPaint paint;
    paint.setTypeface(const_cast<SkTypeface*>(this));
    paint.setTextSize(textSize);
    paint.setLinearText(true);

    SkScalerContext::Rec rec;
    SkScalerContext::MakeRec(paint, nullptr, nullptr, &rec);

    SkAutoDescriptor ad(sizeof(rec) + SkDescriptor::ComputeOverhead(1));
    SkDescriptor*    desc = ad.getDesc();
    desc->init();
    desc->addEntry(kRec_SkDescriptorTag, sizeof(rec), &rec);

    SkAutoTDelete<SkScalerContext> ctx(this->createScalerContext(desc, true));
    if (ctx.get()) {
        SkPaint::FontMetrics fm;
        ctx->getFontMetrics(&fm);
        bounds->set(fm.fXMin * invTextSize, fm.fTop * invTextSize,
                    fm.fXMax * invTextSize, fm.fBottom * invTextSize);
        return true;
    }
    return false;
}

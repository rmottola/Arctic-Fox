/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_PLATFORM_MAC_H
#define GFX_PLATFORM_MAC_H

#include <AvailabilityMacros.h>

#include "nsTArrayForwardDeclare.h"
#include "gfxPlatform.h"
#include "mozilla/LookAndFeel.h"

#if defined(MAC_OS_X_VERSION_10_5) && (MAC_OS_X_VERSION_MAX_ALLOWED <= MAC_OS_X_VERSION_10_5)
#include "nsDataHashtable.h"
#include "nsClassHashtable.h"

typedef size_t ByteCount;
#endif

namespace mozilla {
namespace gfx {
class DrawTarget;
class VsyncSource;
} // namespace gfx
} // namespace mozilla

#if defined(MAC_OS_X_VERSION_10_5) && (MAC_OS_X_VERSION_MAX_ALLOWED <= MAC_OS_X_VERSION_10_5)
// 10.4Fx
class FontDirWrapper {
public:
	uint8_t fontDir[1024];
	ByteCount sizer;
	FontDirWrapper(ByteCount sized, uint8_t *dir) {
		if (MOZ_UNLIKELY(sized < 1 || sized > 1023)) return;
		sizer = sized;
		memcpy(fontDir, dir, sizer);
	}
	~FontDirWrapper() { }
};
#endif

class gfxPlatformMac : public gfxPlatform {
public:
    gfxPlatformMac();
    virtual ~gfxPlatformMac();

    static gfxPlatformMac *GetPlatform() {
        return (gfxPlatformMac*) gfxPlatform::GetPlatform();
    }

    virtual already_AddRefed<gfxASurface>
      CreateOffscreenSurface(const IntSize& aSize,
                             gfxImageFormat aFormat) override;

    already_AddRefed<mozilla::gfx::ScaledFont>
      GetScaledFontForFont(mozilla::gfx::DrawTarget* aTarget, gfxFont *aFont) override;

    nsresult GetStandardFamilyName(const nsAString& aFontName, nsAString& aFamilyName) override;

    gfxFontGroup*
    CreateFontGroup(const mozilla::FontFamilyList& aFontFamilyList,
                    const gfxFontStyle *aStyle,
                    gfxTextPerfMetrics* aTextPerf,
                    gfxUserFontSet *aUserFontSet,
                    gfxFloat aDevToCssSize) override;

    virtual gfxFontEntry* LookupLocalFont(const nsAString& aFontName,
                                          uint16_t aWeight,
                                          int16_t aStretch,
                                          uint8_t aStyle) override;

    virtual gfxPlatformFontList* CreatePlatformFontList() override;

    virtual gfxFontEntry* MakePlatformFont(const nsAString& aFontName,
                                           uint16_t aWeight,
                                           int16_t aStretch,
                                           uint8_t aStyle,
                                           const uint8_t* aFontData,
                                           uint32_t aLength) override;

    bool IsFontFormatSupported(nsIURI *aFontURI, uint32_t aFormatFlags) override;

    nsresult GetFontList(nsIAtom *aLangGroup,
                         const nsACString& aGenericFamily,
                         nsTArray<nsString>& aListOfFonts) override;
    nsresult UpdateFontList() override;

    virtual void GetCommonFallbackFonts(uint32_t aCh, uint32_t aNextCh,
                                        int32_t aRunScript,
                                        nsTArray<const char*>& aFontList) override;

    // lookup the system font for a particular system font type and set
    // the name and style characteristics
    static void
    LookupSystemFont(mozilla::LookAndFeel::FontID aSystemFontID,
                     nsAString& aSystemFontName,
                     gfxFontStyle &aFontStyle,
                     float aDevPixPerCSSPixel);

    virtual bool CanRenderContentToDataSurface() const override {
      return true;
    }

    virtual bool SupportsApzWheelInput() const override {
      return true;
    }

    bool RequiresAcceleratedGLContextForCompositorOGL() const override {
      // On OS X in a VM, unaccelerated CompositorOGL shows black flashes, so we
      // require accelerated GL for CompositorOGL but allow unaccelerated GL for
      // BasicCompositor.
      return true;
    }

    virtual bool UseAcceleratedSkiaCanvas() override;

    virtual bool UseProgressivePaint() override;
    virtual already_AddRefed<mozilla::gfx::VsyncSource> CreateHardwareVsyncSource() override;

    // lower threshold on font anti-aliasing
    uint32_t GetAntiAliasingThreshold() { return mFontAntiAliasingThreshold; }
#if defined(MAC_OS_X_VERSION_10_5) && (MAC_OS_X_VERSION_MAX_ALLOWED <= MAC_OS_X_VERSION_10_5)
/* ATS acceleration functions for 10.4 */
ByteCount GetCachedDirSizeForFont(nsString name);
uint8_t *GetCachedDirForFont(nsString name);
void SetCachedDirForFont(nsString name, uint8_t* table, ByteCount sizer);
nsClassHashtable< nsStringHashKey, FontDirWrapper > PlatformFontDirCache;
#endif

protected:
    bool AccelerateLayersByDefault() override;

private:
    virtual void GetPlatformCMSOutputProfile(void* &mem, size_t &size) override;

    // read in the pref value for the lower threshold on font anti-aliasing
    static uint32_t ReadAntiAliasingThreshold();

    uint32_t mFontAntiAliasingThreshold;
};

#endif /* GFX_PLATFORM_MAC_H */

/*
 * Copyright 2013 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkDisplacementMapEffect_DEFINED
#define SkDisplacementMapEffect_DEFINED

#include "SkImageFilter.h"
#include "SkBitmap.h"

class SK_API SkDisplacementMapEffect : public SkImageFilter {
public:
    enum ChannelSelectorType {
        kUnknown_ChannelSelectorType,
        kR_ChannelSelectorType,
        kG_ChannelSelectorType,
        kB_ChannelSelectorType,
        kA_ChannelSelectorType
    };

    ~SkDisplacementMapEffect();

    static SkImageFilter* Create(ChannelSelectorType xChannelSelector,
                                 ChannelSelectorType yChannelSelector,
                                 SkScalar scale, SkImageFilter* displacement,
                                 SkImageFilter* color = NULL,
                                 const CropRect* cropRect = NULL);

    SK_DECLARE_PUBLIC_FLATTENABLE_DESERIALIZATION_PROCS(SkDisplacementMapEffect)

    bool onFilterImageDeprecated(Proxy* proxy,
                                 const SkBitmap& src,
                                 const Context& ctx,
                                 SkBitmap* dst,
                                 SkIPoint* offset) const override;
    SkRect computeFastBounds(const SkRect& src) const override;

    virtual SkIRect onFilterBounds(const SkIRect& src, const SkMatrix&,
                                   MapDirection) const override;
    SkIRect onFilterNodeBounds(const SkIRect&, const SkMatrix&, MapDirection) const override;

#if SK_SUPPORT_GPU
    bool canFilterImageGPU() const override { return true; }
    bool filterImageGPUDeprecated(Proxy* proxy, const SkBitmap& src, const Context& ctx,
                                SkBitmap* result, SkIPoint* offset) const override;
#endif

    SK_TO_STRING_OVERRIDE()

protected:
    SkDisplacementMapEffect(ChannelSelectorType xChannelSelector,
                            ChannelSelectorType yChannelSelector,
                            SkScalar scale, SkImageFilter* inputs[2],
                            const CropRect* cropRect);
    void flatten(SkWriteBuffer&) const override;

private:
    ChannelSelectorType fXChannelSelector;
    ChannelSelectorType fYChannelSelector;
    SkScalar fScale;
    typedef SkImageFilter INHERITED;
    const SkImageFilter* getDisplacementInput() const { return getInput(0); }
    const SkImageFilter* getColorInput() const { return getInput(1); }
};

#endif

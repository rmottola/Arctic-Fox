/*
 * Copyright 2012 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkColorFilterImageFilter.h"

#include "SkCanvas.h"
#include "SkColorFilter.h"
#include "SkDevice.h"
#include "SkReadBuffer.h"
#include "SkWriteBuffer.h"

sk_sp<SkImageFilter> SkColorFilterImageFilter::Make(sk_sp<SkColorFilter> cf,
                                                    sk_sp<SkImageFilter> input,
                                                    const CropRect* cropRect) {
    if (!cf) {
        return nullptr;
    }

    SkColorFilter* inputCF;
    if (input && input->isColorFilterNode(&inputCF)) {
        // This is an optimization, as it collapses the hierarchy by just combining the two
        // colorfilters into a single one, which the new imagefilter will wrap.
        sk_sp<SkColorFilter> newCF(SkColorFilter::MakeComposeFilter(cf,// can't move bc of fallthru
                                                                    sk_sp<SkColorFilter>(inputCF)));
        if (newCF) {
            return sk_sp<SkImageFilter>(new SkColorFilterImageFilter(std::move(newCF),
                                                                     sk_ref_sp(input->getInput(0)),
                                                                     cropRect));
        }
    }

    return sk_sp<SkImageFilter>(new SkColorFilterImageFilter(std::move(cf),
                                                             std::move(input),
                                                             cropRect));
}

SkColorFilterImageFilter::SkColorFilterImageFilter(sk_sp<SkColorFilter> cf,
                                                   sk_sp<SkImageFilter> input,
                                                   const CropRect* cropRect)
    : INHERITED(&input, 1, cropRect)
    , fColorFilter(std::move(cf)) {
}

sk_sp<SkFlattenable> SkColorFilterImageFilter::CreateProc(SkReadBuffer& buffer) {
    SK_IMAGEFILTER_UNFLATTEN_COMMON(common, 1);
    sk_sp<SkColorFilter> cf(buffer.readColorFilter());
    return Make(std::move(cf), common.getInput(0), &common.cropRect());
}

void SkColorFilterImageFilter::flatten(SkWriteBuffer& buffer) const {
    this->INHERITED::flatten(buffer);
    buffer.writeFlattenable(fColorFilter.get());
}

bool SkColorFilterImageFilter::onFilterImageDeprecated(Proxy* proxy, const SkBitmap& source,
                                                       const Context& ctx,
                                                       SkBitmap* result,
                                                       SkIPoint* offset) const {
    SkBitmap src = source;
    SkIPoint srcOffset = SkIPoint::Make(0, 0);
    bool inputResult = this->filterInputDeprecated(0, proxy, source, ctx, &src, &srcOffset);

    SkIRect srcBounds;

    if (fColorFilter->affectsTransparentBlack()) {
        // If the color filter affects transparent black, the bounds are the entire clip.
        srcBounds = ctx.clipBounds();
    } else if (!inputResult) {
        return false;
    } else {
        srcBounds = src.bounds();
        srcBounds.offset(srcOffset);
    }

    SkIRect bounds;
    if (!this->applyCropRect(ctx, srcBounds, &bounds)) {
        return false;
    }

    SkAutoTUnref<SkBaseDevice> device(proxy->createDevice(bounds.width(), bounds.height()));
    if (nullptr == device.get()) {
        return false;
    }
    SkCanvas canvas(device.get());

    SkPaint paint;
    paint.setXfermodeMode(SkXfermode::kSrc_Mode);
    paint.setColorFilter(fColorFilter);

    // TODO: it may not be necessary to clear or drawPaint inside the input bounds
    // (see skbug.com/5075)
    if (fColorFilter->affectsTransparentBlack()) {
        // The subsequent drawBitmap call may not fill the entire canvas. For filters which
        // affect transparent black, ensure that the filter is applied everywhere.
        canvas.drawPaint(paint);
    }

    canvas.drawBitmap(src, SkIntToScalar(srcOffset.fX - bounds.fLeft),
                           SkIntToScalar(srcOffset.fY - bounds.fTop), &paint);
    *result = device.get()->accessBitmap(false);
    offset->fX = bounds.fLeft;
    offset->fY = bounds.fTop;
    return true;
}

bool SkColorFilterImageFilter::onIsColorFilterNode(SkColorFilter** filter) const {
    SkASSERT(1 == this->countInputs());
    if (!this->cropRectIsSet()) {
        if (filter) {
            *filter = SkRef(fColorFilter.get());
        }
        return true;
    }
    return false;
}

bool SkColorFilterImageFilter::affectsTransparentBlack() const {
    return fColorFilter->affectsTransparentBlack();
}

#ifndef SK_IGNORE_TO_STRING
void SkColorFilterImageFilter::toString(SkString* str) const {
    str->appendf("SkColorFilterImageFilter: (");

    str->appendf("input: (");

    if (this->getInput(0)) {
        this->getInput(0)->toString(str);
    }

    str->appendf(") color filter: ");
    fColorFilter->toString(str);

    str->append(")");
}
#endif

/*
 * Copyright 2014 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkBigPicture.h"
#include "SkData.h"
#include "SkDrawable.h"
#include "SkLayerInfo.h"
#include "SkPictureRecorder.h"
#include "SkPictureUtils.h"
#include "SkRecord.h"
#include "SkRecordDraw.h"
#include "SkRecordOpts.h"
#include "SkRecorder.h"
#include "SkTypes.h"

SkPictureRecorder::SkPictureRecorder() {
    fActivelyRecording = false;
    fRecorder.reset(new SkRecorder(nullptr, SkRect::MakeWH(0, 0), &fMiniRecorder));
}

SkPictureRecorder::~SkPictureRecorder() {}

SkCanvas* SkPictureRecorder::beginRecording(const SkRect& cullRect,
                                            SkBBHFactory* bbhFactory /* = nullptr */,
                                            uint32_t recordFlags /* = 0 */) {
    fCullRect = cullRect;
    fFlags = recordFlags;

    if (bbhFactory) {
        fBBH.reset((*bbhFactory)(cullRect));
        SkASSERT(fBBH.get());
    }

    if (!fRecord) {
        fRecord.reset(new SkRecord);
    }
    SkRecorder::DrawPictureMode dpm = (recordFlags & kPlaybackDrawPicture_RecordFlag)
        ? SkRecorder::Playback_DrawPictureMode
        : SkRecorder::Record_DrawPictureMode;
    fRecorder->reset(fRecord.get(), cullRect, dpm, &fMiniRecorder);
    fActivelyRecording = true;
    return this->getRecordingCanvas();
}

SkCanvas* SkPictureRecorder::getRecordingCanvas() {
    return fActivelyRecording ? fRecorder.get() : nullptr;
}

sk_sp<SkPicture> SkPictureRecorder::finishRecordingAsPicture() {
    fActivelyRecording = false;
    fRecorder->restoreToCount(1);  // If we were missing any restores, add them now.

    if (fRecord->count() == 0) {
        return fMiniRecorder.detachAsPicture(fCullRect);
    }

    // TODO: delay as much of this work until just before first playback?
    SkRecordOptimize(fRecord);

    SkAutoTUnref<SkLayerInfo> saveLayerData;

    if (fBBH && (fFlags & kComputeSaveLayerInfo_RecordFlag)) {
        saveLayerData.reset(new SkLayerInfo);
    }

    SkDrawableList* drawableList = fRecorder->getDrawableList();
    SkBigPicture::SnapshotArray* pictList =
        drawableList ? drawableList->newDrawableSnapshot() : nullptr;

    if (fBBH.get()) {
        SkAutoTMalloc<SkRect> bounds(fRecord->count());
        if (saveLayerData) {
            SkRecordComputeLayers(fCullRect, *fRecord, bounds, pictList, saveLayerData);
        } else {
            SkRecordFillBounds(fCullRect, *fRecord, bounds);
        }
        fBBH->insert(bounds, fRecord->count());

        // Now that we've calculated content bounds, we can update fCullRect, often trimming it.
        // TODO: get updated fCullRect from bounds instead of forcing the BBH to return it?
        SkRect bbhBound = fBBH->getRootBound();
        SkASSERT((bbhBound.isEmpty() || fCullRect.contains(bbhBound))
            || (bbhBound.isEmpty() && fCullRect.isEmpty()));
        fCullRect = bbhBound;
    }

    size_t subPictureBytes = fRecorder->approxBytesUsedBySubPictures();
    for (int i = 0; pictList && i < pictList->count(); i++) {
        subPictureBytes += SkPictureUtils::ApproximateBytesUsed(pictList->begin()[i]);
    }
    return sk_make_sp<SkBigPicture>(fCullRect, fRecord.release(), pictList, fBBH.release(),
                            saveLayerData.release(), subPictureBytes);
}

sk_sp<SkPicture> SkPictureRecorder::finishRecordingAsPictureWithCull(const SkRect& cullRect) {
    fCullRect = cullRect;
    return this->finishRecordingAsPicture();
}


void SkPictureRecorder::partialReplay(SkCanvas* canvas) const {
    if (nullptr == canvas) {
        return;
    }

    int drawableCount = 0;
    SkDrawable* const* drawables = nullptr;
    SkDrawableList* drawableList = fRecorder->getDrawableList();
    if (drawableList) {
        drawableCount = drawableList->count();
        drawables = drawableList->begin();
    }
    SkRecordDraw(*fRecord, canvas, nullptr, drawables, drawableCount, nullptr/*bbh*/, nullptr/*callback*/);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

class SkRecordedDrawable : public SkDrawable {
    SkAutoTUnref<SkRecord>          fRecord;
    SkAutoTUnref<SkBBoxHierarchy>   fBBH;
    SkAutoTDelete<SkDrawableList>   fDrawableList;
    const SkRect                    fBounds;
    const bool                      fDoSaveLayerInfo;

public:
    SkRecordedDrawable(SkRecord* record, SkBBoxHierarchy* bbh, SkDrawableList* drawableList,
                       const SkRect& bounds, bool doSaveLayerInfo)
        : fRecord(SkRef(record))
        , fBBH(SkSafeRef(bbh))
        , fDrawableList(drawableList)   // we take ownership
        , fBounds(bounds)
        , fDoSaveLayerInfo(doSaveLayerInfo)
    {}

protected:
    SkRect onGetBounds() override { return fBounds; }

    void onDraw(SkCanvas* canvas) override {
        SkDrawable* const* drawables = nullptr;
        int drawableCount = 0;
        if (fDrawableList) {
            drawables = fDrawableList->begin();
            drawableCount = fDrawableList->count();
        }
        SkRecordDraw(*fRecord, canvas, nullptr, drawables, drawableCount, fBBH, nullptr/*callback*/);
    }

    SkPicture* onNewPictureSnapshot() override {
        SkBigPicture::SnapshotArray* pictList = nullptr;
        if (fDrawableList) {
            // TODO: should we plumb-down the BBHFactory and recordFlags from our host
            //       PictureRecorder?
            pictList = fDrawableList->newDrawableSnapshot();
        }

        SkAutoTUnref<SkLayerInfo> saveLayerData;
        if (fBBH && fDoSaveLayerInfo) {
            // TODO: can we avoid work by not allocating / filling these bounds?
            SkAutoTMalloc<SkRect> scratchBounds(fRecord->count());
            saveLayerData.reset(new SkLayerInfo);

            SkRecordComputeLayers(fBounds, *fRecord, scratchBounds, pictList, saveLayerData);
        }

        size_t subPictureBytes = 0;
        for (int i = 0; pictList && i < pictList->count(); i++) {
            subPictureBytes += SkPictureUtils::ApproximateBytesUsed(pictList->begin()[i]);
        }
        // SkBigPicture will take ownership of a ref on both fRecord and fBBH.
        // We're not willing to give up our ownership, so we must ref them for SkPicture.
        return new SkBigPicture(fBounds, SkRef(fRecord.get()), pictList, SkSafeRef(fBBH.get()),
                                saveLayerData.release(), subPictureBytes);
    }
};

sk_sp<SkDrawable> SkPictureRecorder::finishRecordingAsDrawable() {
    fActivelyRecording = false;
    fRecorder->flushMiniRecorder();
    fRecorder->restoreToCount(1);  // If we were missing any restores, add them now.

    // TODO: delay as much of this work until just before first playback?
    SkRecordOptimize(fRecord);

    if (fBBH.get()) {
        SkAutoTMalloc<SkRect> bounds(fRecord->count());
        SkRecordFillBounds(fCullRect, *fRecord, bounds);
        fBBH->insert(bounds, fRecord->count());
    }

    sk_sp<SkDrawable> drawable =
           sk_make_sp<SkRecordedDrawable>(fRecord, fBBH, fRecorder->detachDrawableList(), fCullRect,
                                   SkToBool(fFlags & kComputeSaveLayerInfo_RecordFlag));

    // release our refs now, so only the drawable will be the owner.
    fRecord.reset(nullptr);
    fBBH.reset(nullptr);

    return drawable;
}

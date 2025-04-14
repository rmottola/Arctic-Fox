/*
 * Copyright 2015 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkBitmapRegionCanvas.h"
#include "SkBitmapRegionCodec.h"
#include "SkBitmapRegionDecoder.h"
#include "SkAndroidCodec.h"
#include "SkCodec.h"
#include "SkCodecPriv.h"

SkBitmapRegionDecoder* SkBitmapRegionDecoder::Create(
        SkData* data, Strategy strategy) {
    return SkBitmapRegionDecoder::Create(new SkMemoryStream(data),
            strategy);
}

SkBitmapRegionDecoder* SkBitmapRegionDecoder::Create(
        SkStreamRewindable* stream, Strategy strategy) {
    SkAutoTDelete<SkStreamRewindable> streamDeleter(stream);
    switch (strategy) {
        case kCanvas_Strategy: {
            SkAutoTDelete<SkCodec> codec(SkCodec::NewFromStream(streamDeleter.release()));
            if (nullptr == codec) {
                SkCodecPrintf("Error: Failed to create decoder.\n");
                return nullptr;
            }

            SkEncodedFormat format = codec->getEncodedFormat();
            switch (format) {
                case SkEncodedFormat::kJPEG_SkEncodedFormat:
                case SkEncodedFormat::kPNG_SkEncodedFormat:
                    break;
                default:
                    // FIXME: Support webp using a special case.  Webp does not support
                    //        scanline decoding.
                    return nullptr;
            }

            // If the image is a jpeg or a png, the scanline ordering should always be
            // kTopDown or kNone.  It is relevant to check because this implementation
            // only supports these two scanline orderings.
            SkASSERT(SkCodec::kTopDown_SkScanlineOrder == codec->getScanlineOrder() ||
                    SkCodec::kNone_SkScanlineOrder == codec->getScanlineOrder());

            return new SkBitmapRegionCanvas(codec.release());
        }
        case kAndroidCodec_Strategy: {
            SkAutoTDelete<SkAndroidCodec> codec =
                    SkAndroidCodec::NewFromStream(streamDeleter.release());
            if (nullptr == codec) {
                SkCodecPrintf("Error: Failed to create codec.\n");
                return NULL;
            }

            SkEncodedFormat format = codec->getEncodedFormat();
            switch (format) {
                case SkEncodedFormat::kJPEG_SkEncodedFormat:
                case SkEncodedFormat::kPNG_SkEncodedFormat:
                case SkEncodedFormat::kWEBP_SkEncodedFormat:
                    break;
                default:
                    return nullptr;
            }

            return new SkBitmapRegionCodec(codec.release());
        }
        default:
            SkASSERT(false);
            return nullptr;
    }
}

/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkPictureData_DEFINED
#define SkPictureData_DEFINED

#include "SkBitmap.h"
#include "SkPicture.h"
#include "SkPictureContentInfo.h"
#include "SkPictureFlat.h"

class SkData;
class SkPictureRecord;
class SkPixelSerializer;
class SkReader32;
class SkStream;
class SkWStream;
class SkBBoxHierarchy;
class SkMatrix;
class SkPaint;
class SkPath;
class SkReadBuffer;
class SkTextBlob;

struct SkPictInfo {
    enum Flags {
        kCrossProcess_Flag      = 1 << 0,
        kScalarIsFloat_Flag     = 1 << 1,
        kPtrIs64Bit_Flag        = 1 << 2,
    };

    char        fMagic[8];
    uint32_t    fVersion;
    SkRect      fCullRect;
    uint32_t    fFlags;
};

#define SK_PICT_READER_TAG     SkSetFourByteTag('r', 'e', 'a', 'd')
#define SK_PICT_FACTORY_TAG    SkSetFourByteTag('f', 'a', 'c', 't')
#define SK_PICT_TYPEFACE_TAG   SkSetFourByteTag('t', 'p', 'f', 'c')
#define SK_PICT_PICTURE_TAG    SkSetFourByteTag('p', 'c', 't', 'r')

// This tag specifies the size of the ReadBuffer, needed for the following tags
#define SK_PICT_BUFFER_SIZE_TAG     SkSetFourByteTag('a', 'r', 'a', 'y')
// these are all inside the ARRAYS tag
#define SK_PICT_BITMAP_BUFFER_TAG   SkSetFourByteTag('b', 't', 'm', 'p')
#define SK_PICT_PAINT_BUFFER_TAG    SkSetFourByteTag('p', 'n', 't', ' ')
#define SK_PICT_PATH_BUFFER_TAG     SkSetFourByteTag('p', 't', 'h', ' ')
#define SK_PICT_TEXTBLOB_BUFFER_TAG SkSetFourByteTag('b', 'l', 'o', 'b')
#define SK_PICT_IMAGE_BUFFER_TAG    SkSetFourByteTag('i', 'm', 'a', 'g')

// Always write this guy last (with no length field afterwards)
#define SK_PICT_EOF_TAG     SkSetFourByteTag('e', 'o', 'f', ' ')

class SkPictureData {
public:
    SkPictureData(const SkPictureRecord& record, const SkPictInfo&, bool deepCopyOps);
    // Does not affect ownership of SkStream.
    static SkPictureData* CreateFromStream(SkStream*,
                                           const SkPictInfo&,
                                           SkPicture::InstallPixelRefProc,
                                           SkTypefacePlayback*);
    static SkPictureData* CreateFromBuffer(SkReadBuffer&, const SkPictInfo&);

    virtual ~SkPictureData();

    void serialize(SkWStream*, SkPixelSerializer*, SkRefCntSet*) const;
    void flatten(SkWriteBuffer&) const;

    bool containsBitmaps() const;

    bool hasText() const { return fContentInfo.hasText(); }

    int opCount() const { return fContentInfo.numOperations(); }

    const sk_sp<SkData>& opData() const { return fOpData; }

protected:
    explicit SkPictureData(const SkPictInfo& info);

    // Does not affect ownership of SkStream.
    bool parseStream(SkStream*, SkPicture::InstallPixelRefProc, SkTypefacePlayback*);
    bool parseBuffer(SkReadBuffer& buffer);

public:
    const SkBitmap& getBitmap(SkReader32* reader) const {
        const int index = reader->readInt();
        return fBitmaps[index];
    }

    const SkImage* getImage(SkReader32* reader) const {
        const int index = reader->readInt();
        return fImageRefs[index];
    }

    const SkPath& getPath(SkReader32* reader) const {
        int index = reader->readInt() - 1;
        return fPaths[index];
    }

    const SkPicture* getPicture(SkReader32* reader) const {
        int index = reader->readInt();
        SkASSERT(index > 0 && index <= fPictureCount);
        return fPictureRefs[index - 1];
    }

    const SkPaint* getPaint(SkReader32* reader) const {
        int index = reader->readInt();
        if (index == 0) {
            return nullptr;
        }
        return &fPaints[index - 1];
    }

    const SkTextBlob* getTextBlob(SkReader32* reader) const {
        int index = reader->readInt();
        SkASSERT(index > 0 && index <= fTextBlobCount);
        return fTextBlobRefs[index - 1];
    }

#if SK_SUPPORT_GPU
    /**
     * sampleCount is the number of samples-per-pixel or zero if non-MSAA.
     * It is defaulted to be zero.
     */
    bool suitableForGpuRasterization(GrContext* context, const char **reason,
                                     int sampleCount = 0) const;

    /**
     * Calls getRecommendedSampleCount with GrPixelConfig and dpi to calculate sampleCount
     * and then calls the above version of suitableForGpuRasterization
     */
    bool suitableForGpuRasterization(GrContext* context, const char **reason,
                                     GrPixelConfig config, SkScalar dpi) const;

    bool suitableForLayerOptimization() const;
#endif

private:
    void init();

    // these help us with reading/writing
    // Does not affect ownership of SkStream.
    bool parseStreamTag(SkStream*, uint32_t tag, uint32_t size,
                        SkPicture::InstallPixelRefProc, SkTypefacePlayback*);
    bool parseBufferTag(SkReadBuffer&, uint32_t tag, uint32_t size);
    void flattenToBuffer(SkWriteBuffer&) const;

    // Only used by getBitmap() if the passed in index is SkBitmapHeap::INVALID_SLOT. This empty
    // bitmap allows playback to draw nothing and move on.
    SkBitmap fBadBitmap;

    SkTArray<SkBitmap> fBitmaps;
    SkTArray<SkPaint>  fPaints;
    SkTArray<SkPath>   fPaths;

    sk_sp<SkData>   fOpData;    // opcodes and parameters

    const SkPicture** fPictureRefs;
    int fPictureCount;
    const SkTextBlob** fTextBlobRefs;
    int fTextBlobCount;
    const SkImage** fImageRefs;
    int fImageCount;

    SkPictureContentInfo fContentInfo;

    SkTypefacePlayback fTFPlayback;
    SkFactoryPlayback* fFactoryPlayback;

    const SkPictInfo fInfo;

    static void WriteFactories(SkWStream* stream, const SkFactorySet& rec);
    static void WriteTypefaces(SkWStream* stream, const SkRefCntSet& rec);

    void initForPlayback() const;
};

#endif

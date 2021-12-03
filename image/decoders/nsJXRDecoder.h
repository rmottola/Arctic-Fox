/* vim:set tw=80 expandtab softtabstop=4 ts=4 sw=4: */

// Copyright © Microsoft Corp.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// • Redistributions of source code must retain the above copyright notice,
//   this list of conditions and the following disclaimer.
// • Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.


#ifndef _nsJXRDecoder_h
#define _nsJXRDecoder_h

#include "nsAutoPtr.h"
#include "Decoder.h"

#include "qcms.h"

struct tagPKImageDecode;
struct tagPKFormatConverter;
struct WMPStream;

namespace mozilla {
namespace image {

class RasterImage;

/**
 * Decoder for JPEG-XR images
 */

#define MAX_SUBBANDS 4

class nsJXRDecoder : public Decoder
{
public:
    ~nsJXRDecoder();

    virtual void InitInternal();
    virtual void WriteInternal(const char *aBuffer, uint32_t aCount);
    virtual void FinishInternal();

private:
    friend class DecoderFactory;

    // Decoders should only be instantiated via DecoderFactory.
    nsJXRDecoder(RasterImage* aImage, bool hasBeenDecoded);

    tagPKImageDecode *m_pDecoder;
    tagPKFormatConverter *m_pConverter;
    WMPStream *m_pStream;

    uint8_t *m_mbRowBuf;
    uint32_t m_mbRowBufStride;

    bool m_decodeAtEnd; // decode at once after the entire image has been downloaded
    bool m_startedDecodingMBRows, m_startedDecodingMBRows_Alpha;
    bool m_mainPlaneFinished;
    bool m_decodingAlpha;

    unsigned int m_scale;

    struct TileRowBandInfo
    {
        size_t lowerLimits[MAX_SUBBANDS];
        size_t upperLimits[MAX_SUBBANDS];
        size_t top, height;
        size_t topMBRow;
        size_t lastPresentSubband; // 1-based. Data for some subbands (most probably, only flexbits) may be completely missing
    };

    size_t m_subbandUpperLimits[MAX_SUBBANDS]; // upper limits for every subband for the entire image in progressive layout

    struct TileRowInfo
    {
        size_t upperLimit; // how many bytes the decoder has to download before decoding this tile row
        size_t nextLowerLimit; // how many bytes can be discarded after decoding this tile row
        size_t top, height;
        size_t numMBRows;
    };

    uint32_t m_totalReceived;
    bool m_decoderInitialized;
    size_t m_currLine;

    // Progressive decoding
    bool m_progressiveDecoding;
    TileRowBandInfo *m_tileRowBandInfos;
    TileRowInfo *m_tileRowInfos;
    size_t m_numAvailableBands; // number of available progressive frequency bands in the image (0 in case of SPATIAL mode)
    size_t m_currentTileRow;

    // Decode progressively only DC and LP bands. The rest will be decoded sequentially.
    size_t m_currentSubBand;
    bool m_startedDecodingSubband;
    bool m_skippedTileRows;

    uint32_t m_alphaBitDepth;
    bool m_planarAlphaIsPremultiplied;

private:

    size_t GetTileRowForMBRow(size_t mbRow);

    bool DecodeAtEnd() const
    {
        return m_decodeAtEnd;
    }

    uint32_t GetTotalNumBytesReceived() const
    {
        return m_totalReceived;
    }

    bool DecoderInitialized() const
    {
        return m_decoderInitialized;
    }

    bool CreateJXRStuff();
    void DestroyJXRStuff();
    void InitializeJXRDecoder();

    void AllocateMBRowBuffer(size_t width, bool decodeAlpha); // Main image plane
    void AllocateMBRowBuffer_Alpha(size_t width); // Planar alpha
    void FreeMBRowBuffers();

    // Main image
    size_t GetCurrentTileRow() const
    {
        return m_currentTileRow;
    }

    size_t GetNumAvailableBands() const
    {
        return m_numAvailableBands;
    }

    size_t GetCurrentSubBand() const
    {
        return m_currentSubBand;
    }

    size_t GetNumTileRows() const;
    size_t GetNumTileCols() const;

    //size_t GetLastPresentTileRowSubband() const
    //{
    //    return m_tileRowBandInfos[GetCurrentTileRow()].lastPresentSubband;
    //}

    size_t GetCurrentSubbandTileRowUpperLimit() const
    {
        return m_tileRowBandInfos[GetCurrentTileRow()].upperLimits[GetCurrentSubBand()];
    }

    size_t GetCurrentSubbandUpperLimit() const
    {
        return m_subbandUpperLimits[GetCurrentSubBand()];
    }

    // Number of subbands that can be decoded with the number of bytes received
    size_t GetNumberOfCoveredSubBands();

    // Progressive decoding only
    bool HasEmptyTileRowSubbands(size_t subband);

    bool FillTileRowBandInfo();
    bool FillTileRowInfo();

    void StartProgressiveDecoding(bool decodeAlpha);
    bool IsProgressiveDecoding() const
    {
        return m_progressiveDecoding;
    }
    void DecodeNextTileRow();

    void DecodeAllMBRows();
    void DecodeAllMBRowsWithAlpha();
    void DecodeAllMBRows_Alpha();

    void StartDecodingMBRows(bool failSafe, bool decodeAlpha);
    bool StartedDecodingMBRows() const
    {
        return m_startedDecodingMBRows;
    }
    bool DecodeNextMBRow(bool invalidate, bool output = true);
    void EndDecodingMBRows();

    bool DecodeNextMBRowWithAlpha();

    // Planar alpha
    uint32_t GetAlphaBitDepth() const
    {
        return m_alphaBitDepth;
    }

    void StartDecodingMBRows_Alpha();
    bool StartedDecodingMBRows_Alpha() const
    {
        return m_startedDecodingMBRows_Alpha;
    }
    bool DecodeNextMBRow_Alpha(bool invalidate);
    void EndDecodingMBRows_Alpha();

    bool HasAlpha() const;
    bool HasPlanarAlpha() const;

    bool FinishedDecodingMainPlane() const
    {
        return m_mainPlaneFinished;
    }

    bool DecodingAlpha() const
    {
        return m_decodingAlpha;
    }

    bool GetSize(size_t &width, size_t &height);
    bool GetThumbnailSize(size_t &width, size_t &height);
    bool Receive(const uint8_t *buf, uint32_t count);

    // Progressive decoding
    bool StartedDecodingSubband() const
    {
        return m_startedDecodingSubband;
    }

    void StartDecodingNextSubband();
    void EndDecodingCurrentSubband();

    void FixWrongImageSizeTag(size_t maxSize);
    void DoTheDecoding();

public:

    void SetScale(unsigned int scale)
    {
        m_scale = scale;
    }

    unsigned int GetScale() const
    {
        return m_scale;
    }

private:
    enum PixelFormat {pfNone, pfGray, pfRGB24, pfBGR24, pfRGB32, pfBGR32, pfRGBA32, pfBGRA32,
        pfCMYK32, pfCMYK64, pfCMYKA40, pfCMYKA80};

    uint32_t GetPixFmtBitsPP(PixelFormat pixFmt);
    PixelFormat m_outPixelFormat;

    qcms_profile *m_inProfile;
    qcms_transform *m_transform;
    uint8_t *m_xfBuf; // color transormation buffer
    size_t m_xfBufRowStride;
    PixelFormat m_xfPixelFormat;

    void ConvertAndTransform(uint8_t *pDecoded, size_t width, size_t numLines);
    void UpdateImage(size_t top, size_t width, size_t height);
    void UpdateImage_AlphaOnly(size_t top, size_t width, size_t height);

    void CreateColorTransform();

    bool SkippedTileRows() const
    {
        return m_skippedTileRows;
    }
};

} // namespace image
} // namespace mozilla


#endif


/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ImageLogging.h"

#include "nsWEBPDecoder.h" // Must appear first.

#include "gfxPlatform.h"

namespace mozilla {
namespace image {

#if defined(PR_LOGGING)
static PRLogModuleInfo *gWEBPLog = PR_NewLogModule("WEBPDecoder");
static PRLogModuleInfo *gWEBPDecoderAccountingLog =
                        PR_NewLogModule("WEBPDecoderAccounting");
#else
#define gWEBPlog
#define gWEBPDecoderAccountingLog
#endif

nsWEBPDecoder::nsWEBPDecoder(RasterImage* aImage)
 : Decoder(aImage)
 , mLexer(Transition::ToUnbuffered(State::FINISHED_WEBP_DATA,
                                   State::WEBP_DATA,
                                   SIZE_MAX))
 , mDecoder(nullptr)
 , mData(nullptr)
 , mPreviousLastLine(0)
{
  MOZ_LOG(gWEBPDecoderAccountingLog, LogLevel::Debug,
         ("nsWEBPDecoder::nsWEBPDecoder: Creating WEBP decoder %p",
          this));
}

nsWEBPDecoder::~nsWEBPDecoder()
{
  MOZ_LOG(gWEBPDecoderAccountingLog, LogLevel::Debug,
         ("nsWEBPDecoder::~nsWEBPDecoder: Destroying WEBP decoder %p",
          this));

  // It is safe to pass nullptr to WebPIDelete().
  WebPIDelete(mDecoder);
}


void
nsWEBPDecoder::InitInternal()
{
#if MOZ_BIG_ENDIAN
  mDecoder = WebPINewRGB(MODE_Argb, nullptr, 0, 0);
#else
  mDecoder = WebPINewRGB(MODE_rgbA, nullptr, 0, 0);
#endif

  if (!mDecoder) {
    PostDecoderError(NS_ERROR_FAILURE);
    return;
  }
}

void
nsWEBPDecoder::FinishInternal()
{
  MOZ_ASSERT(!HasError(), "Shouldn't call FinishInternal after error!");

  // We should never make multiple frames
  MOZ_ASSERT(GetFrameCount() <= 1, "Multiple WebP frames?");

  // Send notifications if appropriate
  if (!IsMetadataDecode() && (GetFrameCount() == 1)) {
    PostFrameStop();
    PostDecodeDone();
  }
}

Maybe<TerminalState>
nsWEBPDecoder::DoDecode(SourceBufferIterator& aIterator)
{
  MOZ_ASSERT(!HasError(), "Shouldn't call WriteInternal after error!");
  MOZ_ASSERT(aIterator.Data());
  MOZ_ASSERT(aIterator.Length() > 0);

  return mLexer.Lex(aIterator.Data(), aIterator.Length(),
                    [=](State aState, const char* aData, size_t aLength) {
    switch (aState) {
      case State::WEBP_DATA:
        return ReadWEBPData(aData, aLength);
      case State::FINISHED_WEBP_DATA:
        return FinishedWEBPData();
    }
    MOZ_CRASH("Unknown State");
  });
}

LexerTransition<nsWEBPDecoder::State>
nsWEBPDecoder::ReadWEBPData(const char* aData, size_t aLength)
{
  const uint8_t* buf = reinterpret_cast<const uint8_t*>(aData);

  VP8StatusCode rv = WebPIAppend(mDecoder, buf, aLength);
  if (rv == VP8_STATUS_OUT_OF_MEMORY) {
    PostDecoderError(NS_ERROR_OUT_OF_MEMORY);
    return Transition::TerminateFailure();
  } else if (rv == VP8_STATUS_INVALID_PARAM ||
             rv == VP8_STATUS_BITSTREAM_ERROR) {
    PostDataError();
    return Transition::TerminateFailure();
  } else if (rv == VP8_STATUS_UNSUPPORTED_FEATURE ||
             rv == VP8_STATUS_USER_ABORT) {
    PostDecoderError(NS_ERROR_FAILURE);
    return Transition::TerminateFailure();
  }

  // Catch any remaining erroneous return value.
  if (rv != VP8_STATUS_OK && rv != VP8_STATUS_SUSPENDED) {
    PostDecoderError(NS_ERROR_FAILURE);
    return Transition::TerminateFailure();
  }

  int lastLineRead = -1;
  int height = 0;
  int width = 0;
  int stride = 0;

  mData = WebPIDecGetRGB(mDecoder, &lastLineRead, &width, &height, &stride);

  if (lastLineRead == -1 || !mData)
    return Transition::TerminateFailure();

  if (width <= 0 || height <= 0) {
    PostDataError();
    return Transition::TerminateFailure();
  }

  if (!HasSize())
    PostSize(width, height);

  // If we're doing a metadata decode, we're done.
  if (IsMetadataDecode()) {
    return Transition::TerminateSuccess();
  }

  // The only valid format for WebP decoding for both alpha and non-alpha
  // images is BGRA, where Opaque images have an A of 255.
  // Assume transparency for all images.
  // XXX: This could be compositor-optimized by doing a one-time check for
  // all-255 alpha pixels, but that might interfere with progressive
  // decoding. Probably not worth it?
  PostHasTransparency();

  if (!mImageData) {
    MOZ_ASSERT(HasSize(), "Didn't fetch metadata?");
    nsresult rv_ = AllocateBasicFrame();
    if (NS_FAILED(rv_)) {
      return Transition::TerminateFailure();
    }
  }
  MOZ_ASSERT(mImageData, "Should have a buffer now");
  MOZ_ASSERT(mDecoder, "Should have a decoder now");

  // Transfer from mData to mImageData
  if (lastLineRead > mPreviousLastLine) {
    for (int line = mPreviousLastLine; line < lastLineRead; line++) {
      for (int pix = 0; pix < width; pix++) {
        uint32_t DataOffset = 4 * (line * width + pix);
#if MOZ_BIG_ENDIAN
        // ARGB -> ARGB // even if Doc says it should be put in BGRA
        mImageData[DataOffset+0] = mData[DataOffset+0];
        mImageData[DataOffset+1] = mData[DataOffset+1];
        mImageData[DataOffset+2] = mData[DataOffset+2];
        mImageData[DataOffset+3] = mData[DataOffset+3];
#else
        // RGBA -> BGRA
        mImageData[DataOffset+0] = mData[DataOffset+2];
        mImageData[DataOffset+1] = mData[DataOffset+1];
        mImageData[DataOffset+2] = mData[DataOffset+0];
        mImageData[DataOffset+3] = mData[DataOffset+3];
#endif
      }
    }

    // Invalidate
    nsIntRect r(0,
                mPreviousLastLine,
                width,
                lastLineRead - mPreviousLastLine + 1);
    PostInvalidation(r);
  }

  mPreviousLastLine = lastLineRead;
  return Transition::TerminateSuccess();
}

LexerTransition<nsWEBPDecoder::State>
nsWEBPDecoder::FinishedWEBPData()
{
  // Since we set up an unbuffered read for SIZE_MAX bytes, if we actually read
  // all that data something is really wrong.
  MOZ_ASSERT_UNREACHABLE("Read the entire address space?");
  return Transition::TerminateFailure();
}

} // namespace imagelib
} // namespace mozilla

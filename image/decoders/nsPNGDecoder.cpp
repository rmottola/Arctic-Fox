/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ImageLogging.h" // Must appear first
#include "nsPNGDecoder.h"

#include <algorithm>
#include <cstdint>

#include "gfxColor.h"
#include "gfxPlatform.h"
#include "imgFrame.h"
#include "nsColor.h"
#include "nsIInputStream.h"
#include "nsMemory.h"
#include "nsRect.h"
#include "nspr.h"
#include "png.h"
#include "RasterImage.h"
#include "SurfacePipeFactory.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/Telemetry.h"

using namespace mozilla::gfx;

namespace mozilla {
namespace image {

static LazyLogModule sPNGLog("PNGDecoder");
static LazyLogModule sPNGDecoderAccountingLog("PNGDecoderAccounting");

// Limit image dimensions. See also pnglibconf.h
#ifndef MOZ_PNG_MAX_WIDTH
#  define MOZ_PNG_MAX_WIDTH 65535
#endif
#ifndef MOZ_PNG_MAX_HEIGHT
#  define MOZ_PNG_MAX_HEIGHT 65535
#endif
// Maximum area supported in pixels (W*H)
#ifndef MOZ_PNG_MAX_PIX
#  define MOZ_PNG_MAX_PIX 268435456 // 256 Mpix = 16Ki x 16Ki
#endif

nsPNGDecoder::AnimFrameInfo::AnimFrameInfo()
 : mDispose(DisposalMethod::KEEP)
 , mBlend(BlendMethod::OVER)
 , mTimeout(0)
{ }

#ifdef PNG_APNG_SUPPORTED

int32_t GetNextFrameDelay(png_structp aPNG, png_infop aInfo)
{
  // Delay, in seconds, is delayNum / delayDen.
  png_uint_16 delayNum = png_get_next_frame_delay_num(aPNG, aInfo);
  png_uint_16 delayDen = png_get_next_frame_delay_den(aPNG, aInfo);

  if (delayNum == 0) {
    return 0; // SetFrameTimeout() will set to a minimum.
  }

  if (delayDen == 0) {
    delayDen = 100; // So says the APNG spec.
  }

  // Need to cast delay_num to float to have a proper division and
  // the result to int to avoid a compiler warning.
  return static_cast<int32_t>(static_cast<double>(delayNum) * 1000 / delayDen);
}

nsPNGDecoder::AnimFrameInfo::AnimFrameInfo(png_structp aPNG, png_infop aInfo)
 : mDispose(DisposalMethod::KEEP)
 , mBlend(BlendMethod::OVER)
 , mTimeout(0)
{
  png_byte dispose_op = png_get_next_frame_dispose_op(aPNG, aInfo);
  png_byte blend_op = png_get_next_frame_blend_op(aPNG, aInfo);

  if (dispose_op == PNG_DISPOSE_OP_PREVIOUS) {
    mDispose = DisposalMethod::RESTORE_PREVIOUS;
  } else if (dispose_op == PNG_DISPOSE_OP_BACKGROUND) {
    mDispose = DisposalMethod::CLEAR;
  } else {
    mDispose = DisposalMethod::KEEP;
  }

  if (blend_op == PNG_BLEND_OP_SOURCE) {
    mBlend = BlendMethod::SOURCE;
  } else {
    mBlend = BlendMethod::OVER;
  }

  mTimeout = GetNextFrameDelay(aPNG, aInfo);
}
#endif

// First 8 bytes of a PNG file
const uint8_t
nsPNGDecoder::pngSignatureBytes[] = { 137, 80, 78, 71, 13, 10, 26, 10 };

nsPNGDecoder::nsPNGDecoder(RasterImage* aImage)
 : Decoder(aImage)
 , mLexer(Transition::ToUnbuffered(State::FINISHED_PNG_DATA,
                                   State::PNG_DATA,
                                   SIZE_MAX))
 , mPNG(nullptr)
 , mInfo(nullptr)
 , mCMSLine(nullptr)
 , interlacebuf(nullptr)
 , mInProfile(nullptr)
 , mTransform(nullptr)
 , format(gfx::SurfaceFormat::UNKNOWN)
 , mCMSMode(0)
 , mChannels(0)
 , mPass(0)
 , mFrameIsHidden(false)
 , mDisablePremultipliedAlpha(false)
 , mNumFrames(0)
{ }

nsPNGDecoder::~nsPNGDecoder()
{
  if (mPNG) {
    png_destroy_read_struct(&mPNG, mInfo ? &mInfo : nullptr, nullptr);
  }
  if (mCMSLine) {
    free(mCMSLine);
  }
  if (interlacebuf) {
    free(interlacebuf);
  }
  if (mInProfile) {
    qcms_profile_release(mInProfile);

    // mTransform belongs to us only if mInProfile is non-null
    if (mTransform) {
      qcms_transform_release(mTransform);
    }
  }
}

nsPNGDecoder::TransparencyType
nsPNGDecoder::GetTransparencyType(SurfaceFormat aFormat,
                                  const IntRect& aFrameRect)
{
  // Check if the image has a transparent color in its palette.
  if (aFormat == SurfaceFormat::B8G8R8A8) {
    return TransparencyType::eAlpha;
  }
  if (!IntRect(IntPoint(), GetSize()).IsEqualEdges(aFrameRect)) {
    MOZ_ASSERT(HasAnimation());
    return TransparencyType::eFrameRect;
  }

  return TransparencyType::eNone;
}

void
nsPNGDecoder::PostHasTransparencyIfNeeded(TransparencyType aTransparencyType)
{
  switch (aTransparencyType) {
    case TransparencyType::eNone:
      return;

    case TransparencyType::eAlpha:
      PostHasTransparency();
      return;

    case TransparencyType::eFrameRect:
      // If the first frame of animated image doesn't draw into the whole image,
      // then record that it is transparent. For subsequent frames, this doesn't
      // affect transparency, because they're composited on top of all previous
      // frames.
      if (mNumFrames == 0) {
        PostHasTransparency();
      }
      return;
  }
}

// CreateFrame() is used for both simple and animated images.
nsresult
nsPNGDecoder::CreateFrame(SurfaceFormat aFormat,
                          const IntRect& aFrameRect,
                          bool aIsInterlaced)
{
  MOZ_ASSERT(HasSize());
  MOZ_ASSERT(!IsMetadataDecode());

  // Check if we have transparency, and send notifications if needed.
  auto transparency = GetTransparencyType(aFormat, aFrameRect);
  PostHasTransparencyIfNeeded(transparency);
  SurfaceFormat format = transparency == TransparencyType::eNone
                       ? SurfaceFormat::B8G8R8X8
                       : SurfaceFormat::B8G8R8A8;

  // Make sure there's no animation or padding if we're downscaling.
  MOZ_ASSERT_IF(mDownscaler, mNumFrames == 0);
  MOZ_ASSERT_IF(mDownscaler, !GetImageMetadata().HasAnimation());
  MOZ_ASSERT_IF(mDownscaler, transparency != TransparencyType::eFrameRect);

  IntSize targetSize = mDownscaler ? mDownscaler->TargetSize()
                                   : GetSize();

  // If this image is interlaced, we can display better quality intermediate
  // results to the user by post processing them with ADAM7InterpolatingFilter.
  SurfacePipeFlags pipeFlags = aIsInterlaced
                             ? SurfacePipeFlags::ADAM7_INTERPOLATE
                             : SurfacePipeFlags();

  if (mNumFrames == 0) {
    // The first frame may be displayed progressively.
    pipeFlags |= SurfacePipeFlags::PROGRESSIVE_DISPLAY;
  }

  Maybe<SurfacePipe> pipe =
    SurfacePipeFactory::CreateSurfacePipe(this, mNumFrames, GetSize(), targetSize,
                                          aFrameRect, format, pipeFlags);

  if (!pipe) {
    mPipe = SurfacePipe();
    return NS_ERROR_FAILURE;
  }

  mPipe = Move(*pipe);

  mFrameRect = aFrameRect;
  mPass = 0;

  MOZ_LOG(sPNGDecoderAccountingLog, LogLevel::Debug,
         ("PNGDecoderAccounting: nsPNGDecoder::CreateFrame -- created "
          "image frame with %dx%d pixels for decoder %p",
          aFrameRect.width, aFrameRect.height, this));

#ifdef PNG_APNG_SUPPORTED
  if (png_get_valid(mPNG, mInfo, PNG_INFO_acTL)) {
    mAnimInfo = AnimFrameInfo(mPNG, mInfo);

    if (mAnimInfo.mDispose == DisposalMethod::CLEAR) {
      // We may have to display the background under this image during
      // animation playback, so we regard it as transparent.
      PostHasTransparency();
    }
  }
#endif

  return NS_OK;
}

// set timeout and frame disposal method for the current frame
void
nsPNGDecoder::EndImageFrame()
{
  if (mFrameIsHidden) {
    return;
  }

  mNumFrames++;

  Opacity opacity = Opacity::SOME_TRANSPARENCY;
  if (format == gfx::SurfaceFormat::B8G8R8X8) {
    opacity = Opacity::FULLY_OPAQUE;
  }

  PostFrameStop(opacity, mAnimInfo.mDispose, mAnimInfo.mTimeout,
                mAnimInfo.mBlend, Some(mFrameRect));
}

void
nsPNGDecoder::InitInternal()
{
  mCMSMode = gfxPlatform::GetCMSMode();
  if (GetSurfaceFlags() & SurfaceFlags::NO_COLORSPACE_CONVERSION) {
    mCMSMode = eCMSMode_Off;
  }
  mDisablePremultipliedAlpha =
    bool(GetSurfaceFlags() & SurfaceFlags::NO_PREMULTIPLY_ALPHA);

#ifdef PNG_HANDLE_AS_UNKNOWN_SUPPORTED
  static png_byte color_chunks[]=
       { 99,  72,  82,  77, '\0',   // cHRM
        105,  67,  67,  80, '\0'};  // iCCP
  static png_byte unused_chunks[]=
       { 98,  75,  71,  68, '\0',   // bKGD
        104,  73,  83,  84, '\0',   // hIST
        105,  84,  88, 116, '\0',   // iTXt
        111,  70,  70, 115, '\0',   // oFFs
        112,  67,  65,  76, '\0',   // pCAL
        115,  67,  65,  76, '\0',   // sCAL
        112,  72,  89, 115, '\0',   // pHYs
        115,  66,  73,  84, '\0',   // sBIT
        115,  80,  76,  84, '\0',   // sPLT
        116,  69,  88, 116, '\0',   // tEXt
        116,  73,  77,  69, '\0',   // tIME
        122,  84,  88, 116, '\0'};  // zTXt
#endif

  // Initialize the container's source image header
  // Always decode to 24 bit pixdepth

  mPNG = png_create_read_struct(PNG_LIBPNG_VER_STRING,
                                nullptr, nsPNGDecoder::error_callback,
                                nsPNGDecoder::warning_callback);
  if (!mPNG) {
    PostDecoderError(NS_ERROR_OUT_OF_MEMORY);
    return;
  }

  mInfo = png_create_info_struct(mPNG);
  if (!mInfo) {
    PostDecoderError(NS_ERROR_OUT_OF_MEMORY);
    png_destroy_read_struct(&mPNG, nullptr, nullptr);
    return;
  }

#ifdef PNG_HANDLE_AS_UNKNOWN_SUPPORTED
  // Ignore unused chunks
  if (mCMSMode == eCMSMode_Off || IsMetadataDecode()) {
    png_set_keep_unknown_chunks(mPNG, 1, color_chunks, 2);
  }

  png_set_keep_unknown_chunks(mPNG, 1, unused_chunks,
                              (int)sizeof(unused_chunks)/5);
#endif

#ifdef PNG_SET_CHUNK_MALLOC_LIMIT_SUPPORTED
  if (mCMSMode != eCMSMode_Off) {
    png_set_chunk_malloc_max(mPNG, 4000000L);
  }
#endif

#ifdef PNG_READ_CHECK_FOR_INVALID_INDEX_SUPPORTED
  // Disallow palette-index checking, for speed; we would ignore the warning
  // anyhow.  This feature was added at libpng version 1.5.10 and is disabled
  // in the embedded libpng but enabled by default in the system libpng.  This
  // call also disables it in the system libpng, for decoding speed.
  // Bug #745202.
  png_set_check_for_invalid_index(mPNG, 0);
#endif

// Set various PNG lib options if supported
#ifdef PNG_SET_OPTION_SUPPORTED
#if defined(PNG_sRGB_PROFILE_CHECKS) && PNG_sRGB_PROFILE_CHECKS >= 0
  // Skip checking of sRGB ICC profiles
  png_set_option(mPNG, PNG_SKIP_sRGB_CHECK_PROFILE, PNG_OPTION_ON);
#endif

#ifdef PNG_MAXIMUM_INFLATE_WINDOW
  // Force a larger zlib inflate window as some images in the wild have
  // incorrectly set metadata (specifically CMF bits) which prevent us from
  // decoding them otherwise.
  png_set_option(mPNG, PNG_MAXIMUM_INFLATE_WINDOW, PNG_OPTION_ON);
#endif
#endif  // PNG_SET_OPTION_SUPPORTED 

  // use this as libpng "progressive pointer" (retrieve in callbacks)
  png_set_progressive_read_fn(mPNG, static_cast<png_voidp>(this),
                              nsPNGDecoder::info_callback,
                              nsPNGDecoder::row_callback,
                              nsPNGDecoder::end_callback);

}

Maybe<TerminalState>
nsPNGDecoder::DoDecode(SourceBufferIterator& aIterator)
{
  MOZ_ASSERT(!HasError(), "Shouldn't call DoDecode after error!");
  MOZ_ASSERT(aIterator.Data());
  MOZ_ASSERT(aIterator.Length() > 0);

  return mLexer.Lex(aIterator.Data(), aIterator.Length(),
                    [=](State aState, const char* aData, size_t aLength) {
    switch (aState) {
      case State::PNG_DATA:
        return ReadPNGData(aData, aLength);
      case State::FINISHED_PNG_DATA:
        return FinishedPNGData();
    }
    MOZ_CRASH("Unknown State");
  });
}

LexerTransition<nsPNGDecoder::State>
nsPNGDecoder::ReadPNGData(const char* aData, size_t aLength)
{
  // libpng uses setjmp/longjmp for error handling.
  if (setjmp(png_jmpbuf(mPNG))) {
    return Transition::TerminateFailure();
  }

  // Pass the data off to libpng.
  png_process_data(mPNG, mInfo,
                   reinterpret_cast<unsigned char*>(const_cast<char*>((aData))),
                   aLength);

  if (HasError()) {
    return Transition::TerminateFailure();
  }

  if (GetDecodeDone()) {
    return Transition::TerminateSuccess();
  }

  // Keep reading data.
  return Transition::ContinueUnbuffered(State::PNG_DATA);
}

LexerTransition<nsPNGDecoder::State>
nsPNGDecoder::FinishedPNGData()
{
  // Since we set up an unbuffered read for SIZE_MAX bytes, if we actually read
  // all that data something is really wrong.
  MOZ_ASSERT_UNREACHABLE("Read the entire address space?");
  return Transition::TerminateFailure();
}

// Sets up gamma pre-correction in libpng before our callback gets called.
// We need to do this if we don't end up with a CMS profile.
static void
PNGDoGammaCorrection(png_structp png_ptr, png_infop info_ptr)
{
  double aGamma;

  if (png_get_gAMA(png_ptr, info_ptr, &aGamma)) {
    if ((aGamma <= 0.0) || (aGamma > 21474.83)) {
      aGamma = 0.45455;
      png_set_gAMA(png_ptr, info_ptr, aGamma);
    }
    png_set_gamma(png_ptr, 2.2, aGamma);
  } else {
    png_set_gamma(png_ptr, 2.2, 0.45455);
  }
}

// Adapted from http://www.littlecms.com/pngchrm.c example code
static qcms_profile*
PNGGetColorProfile(png_structp png_ptr, png_infop info_ptr,
                   int color_type, qcms_data_type* inType, uint32_t* intent)
{
  qcms_profile* profile = nullptr;
  *intent = QCMS_INTENT_PERCEPTUAL; // Our default

  // First try to see if iCCP chunk is present
  if (png_get_valid(png_ptr, info_ptr, PNG_INFO_iCCP)) {
    png_uint_32 profileLen;
    png_bytep profileData;
    png_charp profileName;
    int compression;

    png_get_iCCP(png_ptr, info_ptr, &profileName, &compression,
                 &profileData, &profileLen);

    profile = qcms_profile_from_memory((char*)profileData, profileLen);
    if (profile) {
      uint32_t profileSpace = qcms_profile_get_color_space(profile);

      bool mismatch = false;
      if (color_type & PNG_COLOR_MASK_COLOR) {
        if (profileSpace != icSigRgbData) {
          mismatch = true;
        }
      } else {
        if (profileSpace == icSigRgbData) {
          png_set_gray_to_rgb(png_ptr);
        } else if (profileSpace != icSigGrayData) {
          mismatch = true;
        }
      }

      if (mismatch) {
        qcms_profile_release(profile);
        profile = nullptr;
      } else {
        *intent = qcms_profile_get_rendering_intent(profile);
      }
    }
  }

  // Check sRGB chunk
  if (!profile && png_get_valid(png_ptr, info_ptr, PNG_INFO_sRGB)) {
    profile = qcms_profile_sRGB();

    if (profile) {
      int fileIntent;
      png_set_gray_to_rgb(png_ptr);
      png_get_sRGB(png_ptr, info_ptr, &fileIntent);
      uint32_t map[] = { QCMS_INTENT_PERCEPTUAL,
                         QCMS_INTENT_RELATIVE_COLORIMETRIC,
                         QCMS_INTENT_SATURATION,
                         QCMS_INTENT_ABSOLUTE_COLORIMETRIC };
      *intent = map[fileIntent];
    }
  }

  // Check gAMA/cHRM chunks
  if (!profile &&
       png_get_valid(png_ptr, info_ptr, PNG_INFO_gAMA) &&
       png_get_valid(png_ptr, info_ptr, PNG_INFO_cHRM)) {
    qcms_CIE_xyYTRIPLE primaries;
    qcms_CIE_xyY whitePoint;

    png_get_cHRM(png_ptr, info_ptr,
                 &whitePoint.x, &whitePoint.y,
                 &primaries.red.x,   &primaries.red.y,
                 &primaries.green.x, &primaries.green.y,
                 &primaries.blue.x,  &primaries.blue.y);
    whitePoint.Y =
      primaries.red.Y = primaries.green.Y = primaries.blue.Y = 1.0;

    double gammaOfFile;

    png_get_gAMA(png_ptr, info_ptr, &gammaOfFile);

    profile = qcms_profile_create_rgb_with_gamma(whitePoint, primaries,
                                                 1.0/gammaOfFile);

    if (profile) {
      png_set_gray_to_rgb(png_ptr);
    }
  }

  if (profile) {
    uint32_t profileSpace = qcms_profile_get_color_space(profile);
    if (profileSpace == icSigGrayData) {
      if (color_type & PNG_COLOR_MASK_ALPHA) {
        *inType = QCMS_DATA_GRAYA_8;
      } else {
        *inType = QCMS_DATA_GRAY_8;
      }
    } else {
      if (color_type & PNG_COLOR_MASK_ALPHA ||
          png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) {
        *inType = QCMS_DATA_RGBA_8;
      } else {
        *inType = QCMS_DATA_RGB_8;
      }
    }
  }

  return profile;
}

void
nsPNGDecoder::info_callback(png_structp png_ptr, png_infop info_ptr)
{
  png_uint_32 width, height;
  int bit_depth, color_type, interlace_type, compression_type, filter_type;
  unsigned int channels;

  png_bytep trans = nullptr;
  int num_trans = 0;

  nsPNGDecoder* decoder =
               static_cast<nsPNGDecoder*>(png_get_progressive_ptr(png_ptr));

  // Always decode to 24-bit RGB or 32-bit RGBA
  png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type,
               &interlace_type, &compression_type, &filter_type);

  // Check sizes against cap limits and W*H
  if ((width > MOZ_PNG_MAX_WIDTH) ||
      (height > MOZ_PNG_MAX_HEIGHT) ||
      (width * height > MOZ_PNG_MAX_PIX)) {
    png_longjmp(decoder->mPNG, 1);
  }

  const IntRect frameRect(0, 0, width, height);

  // Post our size to the superclass
  decoder->PostSize(frameRect.width, frameRect.height);
  if (decoder->HasError()) {
    // Setting the size led to an error.
    png_longjmp(decoder->mPNG, 1);
  }

  if (color_type == PNG_COLOR_TYPE_PALETTE) {
    png_set_expand(png_ptr);
  }

  if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
    png_set_expand(png_ptr);
  }

  if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) {
    png_color_16p trans_values;
    png_get_tRNS(png_ptr, info_ptr, &trans, &num_trans, &trans_values);
    // libpng doesn't reject a tRNS chunk with out-of-range samples
    // so we check it here to avoid setting up a useless opacity
    // channel or producing unexpected transparent pixels (bug #428045)
    if (bit_depth < 16) {
      png_uint_16 sample_max = (1 << bit_depth) - 1;
      if ((color_type == PNG_COLOR_TYPE_GRAY &&
           trans_values->gray > sample_max) ||
           (color_type == PNG_COLOR_TYPE_RGB &&
           (trans_values->red > sample_max ||
           trans_values->green > sample_max ||
           trans_values->blue > sample_max))) {
        // clear the tRNS valid flag and release tRNS memory
        png_free_data(png_ptr, info_ptr, PNG_FREE_TRNS, 0);
        num_trans = 0;
      }
    }
    if (num_trans != 0) {
      png_set_expand(png_ptr);
    }
  }

  if (bit_depth == 16) {
    png_set_scale_16(png_ptr);
  }

  qcms_data_type inType = QCMS_DATA_RGBA_8;
  uint32_t intent = -1;
  uint32_t pIntent;
  if (decoder->mCMSMode != eCMSMode_Off) {
    intent = gfxPlatform::GetRenderingIntent();
    decoder->mInProfile = PNGGetColorProfile(png_ptr, info_ptr,
                                             color_type, &inType, &pIntent);
    // If we're not mandating an intent, use the one from the image.
    if (intent == uint32_t(-1)) {
      intent = pIntent;
    }
  }
  if (decoder->mInProfile && gfxPlatform::GetCMSOutputProfile()) {
    qcms_data_type outType;

    if (color_type & PNG_COLOR_MASK_ALPHA || num_trans) {
      outType = QCMS_DATA_RGBA_8;
    } else {
      outType = QCMS_DATA_RGB_8;
    }

    decoder->mTransform = qcms_transform_create(decoder->mInProfile,
                                           inType,
                                           gfxPlatform::GetCMSOutputProfile(),
                                           outType,
                                           (qcms_intent)intent);
  } else {
    png_set_gray_to_rgb(png_ptr);

    // only do gamma correction if CMS isn't entirely disabled
    if (decoder->mCMSMode != eCMSMode_Off) {
      PNGDoGammaCorrection(png_ptr, info_ptr);
    }

    if (decoder->mCMSMode == eCMSMode_All) {
      if (color_type & PNG_COLOR_MASK_ALPHA || num_trans) {
        decoder->mTransform = gfxPlatform::GetCMSRGBATransform();
      } else {
        decoder->mTransform = gfxPlatform::GetCMSRGBTransform();
      }
    }
  }

  // Let libpng expand interlaced images.
  const bool isInterlaced = interlace_type == PNG_INTERLACE_ADAM7;
  if (isInterlaced) {
    png_set_interlace_handling(png_ptr);
  }

  // now all of those things we set above are used to update various struct
  // members and whatnot, after which we can get channels, rowbytes, etc.
  png_read_update_info(png_ptr, info_ptr);
  decoder->mChannels = channels = png_get_channels(png_ptr, info_ptr);

  //---------------------------------------------------------------//
  // copy PNG info into imagelib structs (formerly png_set_dims()) //
  //---------------------------------------------------------------//

  if (channels == 1 || channels == 3) {
    decoder->format = gfx::SurfaceFormat::B8G8R8X8;
  } else if (channels == 2 || channels == 4) {
    decoder->format = gfx::SurfaceFormat::B8G8R8A8;
  } else {
    png_longjmp(decoder->mPNG, 1); // invalid number of channels
  }

#ifdef PNG_APNG_SUPPORTED
  bool isAnimated = png_get_valid(png_ptr, info_ptr, PNG_INFO_acTL);
  if (isAnimated) {
    decoder->PostIsAnimated(GetNextFrameDelay(png_ptr, info_ptr));

    if (decoder->mDownscaler && !decoder->IsFirstFrameDecode()) {
      MOZ_ASSERT_UNREACHABLE("Doing downscale-during-decode "
                             "for an animated image?");
      png_longjmp(decoder->mPNG, 1);  // Abort the decode.
    }
  }
#endif

  if (decoder->IsMetadataDecode()) {
    // If we are animated then the first frame rect is either: 1) the whole image
    // if the IDAT chunk is part of the animation 2) the frame rect of the first
    // fDAT chunk otherwise. If we are not animated then we want to make sure to
    // call PostHasTransparency in the metadata decode if we need to. So it's okay
    // to pass IntRect(0, 0, width, height) here for animated images; they will
    // call with the proper first frame rect in the full decode.
    auto transparency = decoder->GetTransparencyType(decoder->format, frameRect);
    decoder->PostHasTransparencyIfNeeded(transparency);

    // We have the metadata we're looking for, so stop here, before we allocate
    // buffers below.
    png_process_data_pause(png_ptr, /* save = */ false);
    return;
  }

#ifdef PNG_APNG_SUPPORTED
  if (isAnimated) {
    png_set_progressive_frame_fn(png_ptr, nsPNGDecoder::frame_info_callback,
                                 nullptr);
  }

  if (png_get_first_frame_is_hidden(png_ptr, info_ptr)) {
    decoder->mFrameIsHidden = true;
  } else {
#endif
    nsresult rv = decoder->CreateFrame(decoder->format, frameRect, isInterlaced);
    if (NS_FAILED(rv)) {
      png_longjmp(decoder->mPNG, 5); // NS_ERROR_OUT_OF_MEMORY
    }
    MOZ_ASSERT(decoder->mImageData, "Should have a buffer now");
#ifdef PNG_APNG_SUPPORTED
  }
#endif

  if (decoder->mTransform && (channels <= 2 || isInterlaced)) {
    uint32_t bpp[] = { 0, 3, 4, 3, 4 };
    decoder->mCMSLine =
      static_cast<uint8_t*>(malloc(bpp[channels] * frameRect.width));
    if (!decoder->mCMSLine) {
      png_longjmp(decoder->mPNG, 5); // NS_ERROR_OUT_OF_MEMORY
    }
  }

  if (interlace_type == PNG_INTERLACE_ADAM7) {
    if (frameRect.height < INT32_MAX / (frameRect.width * int32_t(channels))) {
      const size_t bufferSize = channels * frameRect.width * frameRect.height;
      decoder->interlacebuf = static_cast<uint8_t*>(malloc(bufferSize));
    }
    if (!decoder->interlacebuf) {
      png_longjmp(decoder->mPNG, 5); // NS_ERROR_OUT_OF_MEMORY
    }
  }
}

void
nsPNGDecoder::PostInvalidationIfNeeded()
{
  Maybe<SurfaceInvalidRect> invalidRect = mPipe.TakeInvalidRect();
  if (!invalidRect) {
    return;
  }

  PostInvalidation(invalidRect->mInputSpaceRect,
                   Some(invalidRect->mOutputSpaceRect));
}

static NextPixel<uint32_t>
PackRGBPixelAndAdvance(uint8_t*& aRawPixelInOut)
{
  const uint32_t pixel =
    gfxPackedPixel(0xFF, aRawPixelInOut[0], aRawPixelInOut[1], aRawPixelInOut[2]);
  aRawPixelInOut += 3;
  return AsVariant(pixel);
}

static NextPixel<uint32_t>
PackRGBAPixelAndAdvance(uint8_t*& aRawPixelInOut)
{
  const uint32_t pixel =
    gfxPackedPixel(aRawPixelInOut[3], aRawPixelInOut[0],
                   aRawPixelInOut[1], aRawPixelInOut[2]);
  aRawPixelInOut += 4;
  return AsVariant(pixel);
}

static NextPixel<uint32_t>
PackUnpremultipliedRGBAPixelAndAdvance(uint8_t*& aRawPixelInOut)
{
  const uint32_t pixel =
    gfxPackedPixelNoPreMultiply(aRawPixelInOut[3], aRawPixelInOut[0],
                                aRawPixelInOut[1], aRawPixelInOut[2]);
  aRawPixelInOut += 4;
  return AsVariant(pixel);
}

void
nsPNGDecoder::row_callback(png_structp png_ptr, png_bytep new_row,
                           png_uint_32 row_num, int pass)
{
  /* libpng comments:
   *
   * This function is called for every row in the image.  If the
   * image is interlacing, and you turned on the interlace handler,
   * this function will be called for every row in every pass.
   * Some of these rows will not be changed from the previous pass.
   * When the row is not changed, the new_row variable will be
   * nullptr. The rows and passes are called in order, so you don't
   * really need the row_num and pass, but I'm supplying them
   * because it may make your life easier.
   *
   * For the non-nullptr rows of interlaced images, you must call
   * png_progressive_combine_row() passing in the row and the
   * old row.  You can call this function for nullptr rows (it will
   * just return) and for non-interlaced images (it just does the
   * memcpy for you) if it will make the code easier.  Thus, you
   * can just do this for all cases:
   *
   *    png_progressive_combine_row(png_ptr, old_row, new_row);
   *
   * where old_row is what was displayed for previous rows.  Note
   * that the first pass (pass == 0 really) will completely cover
   * the old row, so the rows do not have to be initialized.  After
   * the first pass (and only for interlaced images), you will have
   * to pass the current row, and the function will combine the
   * old row and the new row.
   */
  nsPNGDecoder* decoder =
    static_cast<nsPNGDecoder*>(png_get_progressive_ptr(png_ptr));

  if (decoder->mFrameIsHidden) {
    return;  // Skip this frame.
  }

  MOZ_ASSERT_IF(decoder->IsFirstFrameDecode(), decoder->mNumFrames == 0);

  while (pass > decoder->mPass) {
    // Advance to the next pass. We may have to do this multiple times because
    // libpng will skip passes if the image is so small that no pixels have
    // changed on a given pass, but ADAM7InterpolatingFilter needs to be reset
    // once for every pass to perform interpolation properly.
    decoder->mPipe.ResetToFirstRow();
    decoder->mPass++;
  }

  const png_uint_32 height = static_cast<png_uint_32>(decoder->mFrameRect.height);

  if (row_num >= height) {
    // Bail if we receive extra rows. This is especially important because if we
    // didn't, we might overflow the deinterlacing buffer.
    MOZ_ASSERT_UNREACHABLE("libpng producing extra rows?");
    return;
  }

  // Note that |new_row| may be null here, indicating that this is an interlaced
  // image and |row_callback| is being called for a row that hasn't changed.
  MOZ_ASSERT_IF(!new_row, decoder->interlacebuf);
  uint8_t* rowToWrite = new_row;

  if (decoder->interlacebuf) {
    uint32_t width = uint32_t(decoder->mFrameRect.width);

    // We'll output the deinterlaced version of the row.
    rowToWrite = decoder->interlacebuf + (row_num * decoder->mChannels * width);

    // Update the deinterlaced version of this row with the new data.
    png_progressive_combine_row(png_ptr, rowToWrite, new_row);
  }

  decoder->WriteRow(rowToWrite);
}

void
nsPNGDecoder::WriteRow(uint8_t* aRow)
{
  MOZ_ASSERT(aRow);

  uint8_t* rowToWrite = aRow;
  uint32_t width = uint32_t(mFrameRect.width);

  // Apply color management to the row, if necessary, before writing it out.
  if (mTransform) {
    if (mCMSLine) {
      qcms_transform_data(mTransform, rowToWrite, mCMSLine, width);

      // Copy alpha over.
      if (mChannels == 2 || mChannels == 4) {
        for (uint32_t i = 0; i < width; ++i) {
          mCMSLine[4 * i + 3] = rowToWrite[mChannels * i + mChannels - 1];
        }
      }

      rowToWrite = mCMSLine;
    } else {
      qcms_transform_data(mTransform, rowToWrite, rowToWrite, width);
    }
  }

  // Write this row to the SurfacePipe.
  DebugOnly<WriteState> result = WriteState::FAILURE;
  switch (format) {
    case SurfaceFormat::B8G8R8X8:
      result = mPipe.WritePixelsToRow<uint32_t>([&]{
        return PackRGBPixelAndAdvance(rowToWrite);
      });
      break;

    case SurfaceFormat::B8G8R8A8:
      if (mDisablePremultipliedAlpha) {
        result = mPipe.WritePixelsToRow<uint32_t>([&]{
          return PackUnpremultipliedRGBAPixelAndAdvance(rowToWrite);
        });
      } else {
        result = mPipe.WritePixelsToRow<uint32_t>([&]{
          return PackRGBAPixelAndAdvance(rowToWrite);
        });
      }
      break;

    default:
      png_longjmp(mPNG, 1);  // Abort the decode.
  }

  MOZ_ASSERT(result != WriteState::FAILURE);

  PostInvalidationIfNeeded();
}

#ifdef PNG_APNG_SUPPORTED
// got the header of a new frame that's coming
void
nsPNGDecoder::frame_info_callback(png_structp png_ptr, png_uint_32 frame_num)
{
  nsPNGDecoder* decoder =
               static_cast<nsPNGDecoder*>(png_get_progressive_ptr(png_ptr));

  // old frame is done
  decoder->EndImageFrame();

  if (!decoder->mFrameIsHidden && decoder->IsFirstFrameDecode()) {
    // We're about to get a second non-hidden frame, but we only want the first.
    // Stop decoding now. (And avoid allocating the unnecessary buffers below.)
    decoder->PostDecodeDone();
    png_process_data_pause(png_ptr, /* save = */ false);
    return;
  }

  // Only the first frame can be hidden, so unhide unconditionally here.
  decoder->mFrameIsHidden = false;

  const IntRect frameRect(png_get_next_frame_x_offset(png_ptr, decoder->mInfo),
                          png_get_next_frame_y_offset(png_ptr, decoder->mInfo),
                          png_get_next_frame_width(png_ptr, decoder->mInfo),
                          png_get_next_frame_height(png_ptr, decoder->mInfo));

  const bool isInterlaced = bool(decoder->interlacebuf);

  nsresult rv = decoder->CreateFrame(decoder->format, frameRect, isInterlaced);
  if (NS_FAILED(rv)) {
    png_longjmp(decoder->mPNG, 5); // NS_ERROR_OUT_OF_MEMORY
  }
  MOZ_ASSERT(decoder->mImageData, "Should have a buffer now");
}
#endif

void
nsPNGDecoder::end_callback(png_structp png_ptr, png_infop info_ptr)
{
  /* libpng comments:
   *
   * this function is called when the whole image has been read,
   * including any chunks after the image (up to and including
   * the IEND).  You will usually have the same info chunk as you
   * had in the header, although some data may have been added
   * to the comments and time fields.
   *
   * Most people won't do much here, perhaps setting a flag that
   * marks the image as finished.
   */

  nsPNGDecoder* decoder =
               static_cast<nsPNGDecoder*>(png_get_progressive_ptr(png_ptr));

  // We shouldn't get here if we've hit an error
  MOZ_ASSERT(!decoder->HasError(), "Finishing up PNG but hit error!");

  int32_t loop_count = 0;
#ifdef PNG_APNG_SUPPORTED
  if (png_get_valid(png_ptr, info_ptr, PNG_INFO_acTL)) {
    int32_t num_plays = png_get_num_plays(png_ptr, info_ptr);
    loop_count = num_plays - 1;
  }
#endif

  // Send final notifications
  decoder->EndImageFrame();
  decoder->PostDecodeDone(loop_count);
}


void
nsPNGDecoder::error_callback(png_structp png_ptr, png_const_charp error_msg)
{
  MOZ_LOG(sPNGLog, LogLevel::Error, ("libpng error: %s\n", error_msg));
  png_longjmp(png_ptr, 1);
}


void
nsPNGDecoder::warning_callback(png_structp png_ptr, png_const_charp warning_msg)
{
  MOZ_LOG(sPNGLog, LogLevel::Warning, ("libpng warning: %s\n", warning_msg));
}

Telemetry::ID
nsPNGDecoder::SpeedHistogram()
{
  return Telemetry::IMAGE_DECODE_SPEED_PNG;
}

bool
nsPNGDecoder::IsValidICO() const
{
  // Only 32-bit RGBA PNGs are valid ICO resources; see here:
  //   http://blogs.msdn.com/b/oldnewthing/archive/2010/10/22/10079192.aspx

  // If there are errors in the call to png_get_IHDR, the error_callback in
  // nsPNGDecoder.cpp is called.  In this error callback we do a longjmp, so
  // we need to save the jump buffer here. Oterwise we'll end up without a
  // proper callstack.
  if (setjmp(png_jmpbuf(mPNG))) {
    // We got here from a longjmp call indirectly from png_get_IHDR
    return false;
  }

  png_uint_32
      png_width,  // Unused
      png_height; // Unused

  int png_bit_depth,
      png_color_type;

  if (png_get_IHDR(mPNG, mInfo, &png_width, &png_height, &png_bit_depth,
                   &png_color_type, nullptr, nullptr, nullptr)) {

    return ((png_color_type == PNG_COLOR_TYPE_RGB_ALPHA ||
             png_color_type == PNG_COLOR_TYPE_RGB) &&
            png_bit_depth == 8);
  } else {
    return false;
  }
}

} // namespace image
} // namespace mozilla

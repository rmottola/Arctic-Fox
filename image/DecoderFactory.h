/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_image_DecoderFactory_h
#define mozilla_image_DecoderFactory_h

#include "DecoderFlags.h"
#include "mozilla/Attributes.h"
#include "mozilla/Maybe.h"
#include "mozilla/NotNull.h"
#include "mozilla/gfx/2D.h"
#include "nsCOMPtr.h"
#include "SurfaceFlags.h"

namespace mozilla {
namespace image {

class Decoder;
class IDecodingTask;
class nsICODecoder;
class RasterImage;
class SourceBuffer;

/**
 * The type of decoder; this is usually determined from a MIME type using
 * DecoderFactory::GetDecoderType().
 */
enum class DecoderType
{
  PNG,
  GIF,
  JPEG,
  BMP,
  ICO,
  ICON,
  WEBP,
  UNKNOWN
};

class DecoderFactory
{
public:
  /// @return the type of decoder which is appropriate for @aMimeType.
  static DecoderType GetDecoderType(const char* aMimeType);

  /**
   * Creates and initializes a decoder for non-animated images of type @aType.
   * (If the image *is* animated, only the first frame will be decoded.) The
   * decoder will send notifications to @aImage.
   *
   * @param aType Which type of decoder to create - JPEG, PNG, etc.
   * @param aImage The image will own the decoder and which should receive
   *               notifications as decoding progresses.
   * @param aSourceBuffer The SourceBuffer which the decoder will read its data
   *                      from.
   * @param aIntrinsicSize The intrinsic size of the image, normally obtained
   *                       during the metadata decode.
   * @param aTargetSize If not Nothing(), the target size which the image should
   *                    be scaled to during decoding. It's an error to specify
   *                    a target size for a decoder type which doesn't support
   *                    downscale-during-decode.
   * @param aDecoderFlags Flags specifying the behavior of this decoder.
   * @param aSurfaceFlags Flags specifying the type of output this decoder
   *                      should produce.
   * @param aSampleSize The sample size requested using #-moz-samplesize (or 0
   *                    if none).
   */
  static already_AddRefed<IDecodingTask>
  CreateDecoder(DecoderType aType,
                NotNull<RasterImage*> aImage,
                NotNull<SourceBuffer*> aSourceBuffer,
                const gfx::IntSize& aIntrinsicSize,
                const Maybe<gfx::IntSize>& aTargetSize,
                DecoderFlags aDecoderFlags,
                SurfaceFlags aSurfaceFlags,
                int aSampleSize);

  /**
   * Creates and initializes a decoder for animated images of type @aType.
   * The decoder will send notifications to @aImage.
   *
   * @param aType Which type of decoder to create - JPEG, PNG, etc.
   * @param aImage The image will own the decoder and which should receive
   *               notifications as decoding progresses.
   * @param aSourceBuffer The SourceBuffer which the decoder will read its data
   *                      from.
   * @param aIntrinsicSize The intrinsic size of the image, normally obtained
   *                       during the metadata decode.
   * @param aDecoderFlags Flags specifying the behavior of this decoder.
   * @param aSurfaceFlags Flags specifying the type of output this decoder
   *                      should produce.
   */
  static already_AddRefed<IDecodingTask>
  CreateAnimationDecoder(DecoderType aType,
                         NotNull<RasterImage*> aImage,
                         NotNull<SourceBuffer*> aSourceBuffer,
                         const gfx::IntSize& aIntrinsicSize,
                         DecoderFlags aDecoderFlags,
                         SurfaceFlags aSurfaceFlags);

  /**
   * Creates and initializes a metadata decoder of type @aType. This decoder
   * will only decode the image's header, extracting metadata like the size of
   * the image. No actual image data will be decoded and no surfaces will be
   * allocated. The decoder will send notifications to @aImage.
   *
   * @param aType Which type of decoder to create - JPEG, PNG, etc.
   * @param aImage The image will own the decoder and which should receive
   *               notifications as decoding progresses.
   * @param aSourceBuffer The SourceBuffer which the decoder will read its data
   *                      from.
   * @param aSampleSize The sample size requested using #-moz-samplesize (or 0
   *                    if none).
   */
  static already_AddRefed<IDecodingTask>
  CreateMetadataDecoder(DecoderType aType,
                        NotNull<RasterImage*> aImage,
                        NotNull<SourceBuffer*> aSourceBuffer,
                        int aSampleSize);

  /**
   * Creates and initializes a decoder for an ICO resource, which may be either
   * a BMP or PNG image.
   *
   * @param aType Which type of decoder to create. This must be either BMP or
   *              PNG.
   * @param aSourceBuffer The SourceBuffer which the decoder will read its data
   *                      from.
   * @param aICODecoder The ICO decoder which is controlling this resource
   *                    decoder. @aICODecoder's settings will be copied to the
   *                    resource decoder, so the two decoders will have the
   *                    same decoder flags, surface flags, target size, and
   *                    other parameters.
   * @param aDataOffset If @aType is BMP, specifies the offset at which data
   *                    begins in the BMP resource. Must be Some() if and only
   *                    if @aType is BMP.
   */
  static already_AddRefed<Decoder>
  CreateDecoderForICOResource(DecoderType aType,
                              NotNull<SourceBuffer*> aSourceBuffer,
                              NotNull<nsICODecoder*> aICODecoder,
                              const Maybe<uint32_t>& aDataOffset = Nothing());

  /**
   * Creates and initializes an anonymous decoder (one which isn't associated
   * with an Image object). Only the first frame of the image will be decoded.
   *
   * @param aType Which type of decoder to create - JPEG, PNG, etc.
   * @param aSourceBuffer The SourceBuffer which the decoder will read its data
   *                      from.
   * @param aTargetSize If not Nothing(), the target size which the image should
   *                    be scaled to during decoding. It's an error to specify
   *                    a target size for a decoder type which doesn't support
   *                    downscale-during-decode.
   * @param aSurfaceFlags Flags specifying the type of output this decoder
   *                      should produce.
   */
  static already_AddRefed<Decoder>
  CreateAnonymousDecoder(DecoderType aType,
                         NotNull<SourceBuffer*> aSourceBuffer,
                         const Maybe<gfx::IntSize>& aTargetSize,
                         SurfaceFlags aSurfaceFlags);

  /**
   * Creates and initializes an anonymous metadata decoder (one which isn't
   * associated with an Image object). This decoder will only decode the image's
   * header, extracting metadata like the size of the image. No actual image
   * data will be decoded and no surfaces will be allocated.
   *
   * @param aType Which type of decoder to create - JPEG, PNG, etc.
   * @param aSourceBuffer The SourceBuffer which the decoder will read its data
   *                      from.
   */
  static already_AddRefed<Decoder>
  CreateAnonymousMetadataDecoder(DecoderType aType,
                                 NotNull<SourceBuffer*> aSourceBuffer);

private:
  virtual ~DecoderFactory() = 0;

  /**
   * An internal method which allocates a new decoder of the requested @aType.
   */
  static already_AddRefed<Decoder> GetDecoder(DecoderType aType,
                                              RasterImage* aImage,
                                              bool aIsRedecode);
};

} // namespace image
} // namespace mozilla

#endif // mozilla_image_DecoderFactory_h

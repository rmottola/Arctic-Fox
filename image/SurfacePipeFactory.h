/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_image_SurfacePipeFactory_h
#define mozilla_image_SurfacePipeFactory_h

#include "SurfacePipe.h"
#include "SurfaceFilters.h"

namespace mozilla {
namespace image {

namespace detail {

/**
 * FilterPipeline is a helper template for SurfacePipeFactory that determines
 * the full type of the sequence of SurfaceFilters that a sequence of
 * configuration structs corresponds to. To make this work, all configuration
 * structs must include a typedef 'Filter' that identifies the SurfaceFilter
 * they configure.
 */
template <typename... Configs>
struct FilterPipeline;

template <typename Config, typename... Configs>
struct FilterPipeline<Config, Configs...>
{
  typedef typename Config::template Filter<typename FilterPipeline<Configs...>::Type> Type;
};

template <typename Config>
struct FilterPipeline<Config>
{
  typedef typename Config::Filter Type;
};

} // namespace detail

/**
 * Flags for SurfacePipeFactory, used in conjuction with the factory functions
 * in SurfacePipeFactory to enable or disable various SurfacePipe
 * functionality.
 */
enum class SurfacePipeFlags
{
  DEINTERLACE         = 1 << 0,  // If set, deinterlace the image.

  FLIP_VERTICALLY     = 1 << 1,  // If set, flip the image vertically.

  PROGRESSIVE_DISPLAY = 1 << 2   // If set, we expect the image to be displayed
                                 // progressively. This enables features that
                                 // result in a better user experience for
                                 // progressive display but which may be more
                                 // computationally expensive.
};
MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS(SurfacePipeFlags)

class SurfacePipeFactory
{
public:
  /**
   * Creates and initializes a normal (i.e., non-paletted) SurfacePipe.
   *
   * @param aDecoder The decoder whose current frame the SurfacePipe will write
   *                 to.
   * @param aFrameNum Which frame the SurfacePipe will write to. This will be 0
   *                  for non-animated images.
   * @param aInputSize The original size of the image.
   * @param aOutputSize The size the SurfacePipe should output. Must be the same
   *                    as @aInputSize or smaller. If smaller, the image will be
   *                    downscaled during decoding.
   * @param aFrameRect The portion of the image that actually contains data.
   * @param aFormat The surface format of the image; generally B8G8R8A8 or
   *                B8G8R8X8.
   * @param aFlags Flags enabling or disabling various functionality for the
   *               SurfacePipe; see the SurfacePipeFlags documentation for more
   *               information.
   *
   * @return A SurfacePipe if the parameters allowed one to be created
   *         successfully, or Nothing() if the SurfacePipe could not be
   *         initialized.
   */
  static Maybe<SurfacePipe>
  CreateSurfacePipe(Decoder* aDecoder,
                    uint32_t aFrameNum,
                    const nsIntSize& aInputSize,
                    const nsIntSize& aOutputSize,
                    const nsIntRect& aFrameRect,
                    gfx::SurfaceFormat aFormat,
                    SurfacePipeFlags aFlags)
  {
    const bool deinterlace = bool(aFlags & SurfacePipeFlags::DEINTERLACE);
    const bool flipVertically = bool(aFlags & SurfacePipeFlags::FLIP_VERTICALLY);
    const bool progressiveDisplay = bool(aFlags & SurfacePipeFlags::PROGRESSIVE_DISPLAY);
    const bool downscale = aInputSize != aOutputSize;
    const bool removeFrameRect =
      !aFrameRect.IsEqualEdges(nsIntRect(0, 0, aInputSize.width, aInputSize.height));

    // Construct configurations for the SurfaceFilters. Note that the order of
    // these filters is significant. We want to deinterlace raw input rows,
    // before any other transformations, and we want to remove the frame rect
    // (which may involve adding blank rows or columns to the image) before any
    // downscaling, so that the new rows and columns are taken into account.
    DeinterlacingConfig<uint32_t> deinterlacingConfig { progressiveDisplay };
    RemoveFrameRectConfig removeFrameRectConfig { aFrameRect };
    DownscalingConfig downscalingConfig { aInputSize, aFormat };
    SurfaceConfig surfaceConfig { aDecoder, aFrameNum, aOutputSize,
                                  aFormat, flipVertically };

    Maybe<SurfacePipe> pipe;

    if (downscale) {
      if (removeFrameRect) {
        if (deinterlace) {
          pipe = MakePipe(aFrameRect.Size(), deinterlacingConfig,
                          removeFrameRectConfig, downscalingConfig,
                          surfaceConfig);
        } else {  // (deinterlace is false)
          pipe = MakePipe(aFrameRect.Size(), removeFrameRectConfig,
                          downscalingConfig, surfaceConfig);
        }
      } else {  // (removeFrameRect is false)
        if (deinterlace) {
          pipe = MakePipe(aInputSize, deinterlacingConfig,
                          downscalingConfig, surfaceConfig);
        } else {  // (deinterlace is false)
          pipe = MakePipe(aInputSize, downscalingConfig, surfaceConfig);
        }
      }
    } else {  // (downscale is false)
      if (removeFrameRect) {
        if (deinterlace) {
          pipe = MakePipe(aFrameRect.Size(), deinterlacingConfig,
                          removeFrameRectConfig, surfaceConfig);
        } else {  // (deinterlace is false)
          pipe = MakePipe(aFrameRect.Size(), removeFrameRectConfig, surfaceConfig);
        }
      } else {  // (removeFrameRect is false)
        if (deinterlace) {
          pipe = MakePipe(aInputSize, deinterlacingConfig, surfaceConfig);
        } else {  // (deinterlace is false)
          pipe = MakePipe(aInputSize, surfaceConfig);
        }
      }
    }

    return pipe;
  }

  /**
   * Creates and initializes a paletted SurfacePipe.
   *
   * XXX(seth): We'll remove all support for paletted surfaces in bug 1247520,
   * which means we can remove CreatePalettedSurfacePipe() entirely.
   *
   * @param aDecoder The decoder whose current frame the SurfacePipe will write
   *                 to.
   * @param aFrameNum Which frame the SurfacePipe will write to. This will be 0
   *                  for non-animated images.
   * @param aInputSize The original size of the image.
   * @param aFrameRect The portion of the image that actually contains data.
   * @param aFormat The surface format of the image; generally B8G8R8A8 or
   *                B8G8R8X8.
   * @param aPaletteDepth The palette depth of the image.
   * @param aFlags Flags enabling or disabling various functionality for the
   *               SurfacePipe; see the SurfacePipeFlags documentation for more
   *               information.
   *
   * @return A SurfacePipe if the parameters allowed one to be created
   *         successfully, or Nothing() if the SurfacePipe could not be
   *         initialized.
   */
  static Maybe<SurfacePipe>
  CreatePalettedSurfacePipe(Decoder* aDecoder,
                            uint32_t aFrameNum,
                            const nsIntSize& aInputSize,
                            const nsIntRect& aFrameRect,
                            gfx::SurfaceFormat aFormat,
                            uint8_t aPaletteDepth,
                            SurfacePipeFlags aFlags)
  {
    const bool deinterlace = bool(aFlags & SurfacePipeFlags::DEINTERLACE);
    const bool flipVertically = bool(aFlags & SurfacePipeFlags::FLIP_VERTICALLY);
    const bool progressiveDisplay = bool(aFlags & SurfacePipeFlags::PROGRESSIVE_DISPLAY);

    // Construct configurations for the SurfaceFilters.
    DeinterlacingConfig<uint8_t> deinterlacingConfig { progressiveDisplay };
    PalettedSurfaceConfig palettedSurfaceConfig { aDecoder, aFrameNum, aInputSize,
                                                  aFrameRect, aFormat, aPaletteDepth,
                                                  flipVertically };

    Maybe<SurfacePipe> pipe;

    if (deinterlace) {
      pipe = MakePipe(aFrameRect.Size(), deinterlacingConfig,
                      palettedSurfaceConfig);
    } else {
      pipe = MakePipe(aFrameRect.Size(), palettedSurfaceConfig);
    }

    return pipe;
  }

private:
  template <typename... Configs>
  static Maybe<SurfacePipe>
  MakePipe(const nsIntSize& aInputSize, Configs... aConfigs)
  {
    auto pipe = MakeUnique<typename detail::FilterPipeline<Configs...>::Type>();
    nsresult rv = pipe->Configure(aConfigs...);
    if (NS_FAILED(rv)) {
      return Nothing();
    }

    return Some(SurfacePipe { Move(pipe) } );
  }

  virtual ~SurfacePipeFactory() = 0;
};

} // namespace image
} // namespace mozilla

#endif // mozilla_image_SurfacePipeFactory_h

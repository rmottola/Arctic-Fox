/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkLinearBitmapPipeline.h"

#include "SkPM4f.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include "SkColor.h"
#include "SkSize.h"

#ifdef MOZ_SKIA
#include "mozilla/Tuple.h"

namespace std {
  using mozilla::Tie;
  using mozilla::Tuple;
  #define tie Tie
  #define tuple Tuple
}
#else
#include <tuple>
#endif

#include "SkLinearBitmapPipeline_core.h"
#include "SkLinearBitmapPipeline_matrix.h"
#include "SkLinearBitmapPipeline_tile.h"
#include "SkLinearBitmapPipeline_sample.h"

class SkLinearBitmapPipeline::PointProcessorInterface {
public:
    virtual ~PointProcessorInterface() { }
    // Take the first n (where 0 < n && n < 4) items from xs and ys and sample those points. For
    // nearest neighbor, that means just taking the floor xs and ys. For bilerp, this means
    // to expand the bilerp filter around the point and sample using that filter.
    virtual void VECTORCALL pointListFew(int n, Sk4s xs, Sk4s ys) = 0;
    // Same as pointListFew, but n = 4.
    virtual void VECTORCALL pointList4(Sk4s xs, Sk4s ys) = 0;
    // A span is a compact form of sample points that are obtained by mapping points from
    // destination space to source space. This is used for horizontal lines only, and is mainly
    // used to take advantage of memory coherence for horizontal spans.
    virtual void pointSpan(Span span) = 0;
};

class SkLinearBitmapPipeline::SampleProcessorInterface
    : public SkLinearBitmapPipeline::PointProcessorInterface {
public:
    // Used for nearest neighbor when scale factor is 1.0. The span can just be repeated with no
    // edge pixel alignment problems. This is for handling a very common case.
    virtual void repeatSpan(Span span, int32_t repeatCount) = 0;

    // The x's and y's are setup in the following order:
    // +--------+--------+
    // |        |        |
    // |  px00  |  px10  |
    // |    0   |    1   |
    // +--------+--------+
    // |        |        |
    // |  px01  |  px11  |
    // |    2   |    3   |
    // +--------+--------+
    // These pixels coordinates are arranged in the following order in xs and ys:
    // px00  px10  px01  px11
    virtual void VECTORCALL bilerpEdge(Sk4s xs, Sk4s ys) = 0;

    // A span represents sample points that have been mapped from destination space to source
    // space. Each sample point is then expanded to the four bilerp points by add +/- 0.5. The
    // resulting Y values my be off the tile. When y +/- 0.5 are more than 1 apart because of
    // tiling, the second Y is used to denote the retiled Y value.
    virtual void bilerpSpan(Span span, SkScalar y) = 0;
};

class SkLinearBitmapPipeline::PixelPlacerInterface {
public:
    virtual ~PixelPlacerInterface() { }
    // Count is normally not needed, but in these early stages of development it is useful to
    // check bounds.
    // TODO(herb): 4/6/2016 - remove count when code is stable.
    virtual void setDestination(void* dst, int count) = 0;
    virtual void VECTORCALL placePixel(Sk4f pixel0) = 0;
    virtual void VECTORCALL place4Pixels(Sk4f p0, Sk4f p1, Sk4f p2, Sk4f p3) = 0;
};

namespace  {

////////////////////////////////////////////////////////////////////////////////////////////////////
// Matrix Stage
// PointProcessor uses a strategy to help complete the work of the different stages. The strategy
// must implement the following methods:
// * processPoints(xs, ys) - must mutate the xs and ys for the stage.
// * maybeProcessSpan(span, next) - This represents a horizontal series of pixels
//   to work over.
//   span - encapsulation of span.
//   next - a pointer to the next stage.
//   maybeProcessSpan - returns false if it can not process the span and needs to fallback to
//                      point lists for processing.
template<typename Strategy, typename Next>
class MatrixStage final : public SkLinearBitmapPipeline::PointProcessorInterface {
public:
    template <typename... Args>
    MatrixStage(Next* next, Args&&... args)
        : fNext{next}
        , fStrategy{std::forward<Args>(args)...}{ }

    void VECTORCALL pointListFew(int n, Sk4s xs, Sk4s ys) override {
        fStrategy.processPoints(&xs, &ys);
        fNext->pointListFew(n, xs, ys);
    }

    void VECTORCALL pointList4(Sk4s xs, Sk4s ys) override {
        fStrategy.processPoints(&xs, &ys);
        fNext->pointList4(xs, ys);
    }

    // The span you pass must not be empty.
    void pointSpan(Span span) override {
        SkASSERT(!span.isEmpty());
        if (!fStrategy.maybeProcessSpan(span, fNext)) {
            span_fallback(span, this);
        }
    }

private:
    Next* const fNext;
    Strategy fStrategy;
};

template <typename Next = SkLinearBitmapPipeline::PointProcessorInterface>
using TranslateMatrix = MatrixStage<TranslateMatrixStrategy, Next>;

template <typename Next = SkLinearBitmapPipeline::PointProcessorInterface>
using ScaleMatrix = MatrixStage<ScaleMatrixStrategy, Next>;

template <typename Next = SkLinearBitmapPipeline::PointProcessorInterface>
using AffineMatrix = MatrixStage<AffineMatrixStrategy, Next>;

template <typename Next = SkLinearBitmapPipeline::PointProcessorInterface>
using PerspectiveMatrix = MatrixStage<PerspectiveMatrixStrategy, Next>;


static SkLinearBitmapPipeline::PointProcessorInterface* choose_matrix(
    SkLinearBitmapPipeline::PointProcessorInterface* next,
    const SkMatrix& inverse,
    SkLinearBitmapPipeline::MatrixStage* matrixProc) {
    if (inverse.hasPerspective()) {
        matrixProc->Initialize<PerspectiveMatrix<>>(
            next,
            SkVector{inverse.getTranslateX(), inverse.getTranslateY()},
            SkVector{inverse.getScaleX(), inverse.getScaleY()},
            SkVector{inverse.getSkewX(), inverse.getSkewY()},
            SkVector{inverse.getPerspX(), inverse.getPerspY()},
            inverse.get(SkMatrix::kMPersp2));
    } else if (inverse.getSkewX() != 0.0f || inverse.getSkewY() != 0.0f) {
        matrixProc->Initialize<AffineMatrix<>>(
            next,
            SkVector{inverse.getTranslateX(), inverse.getTranslateY()},
            SkVector{inverse.getScaleX(), inverse.getScaleY()},
            SkVector{inverse.getSkewX(), inverse.getSkewY()});
    } else if (inverse.getScaleX() != 1.0f || inverse.getScaleY() != 1.0f) {
        matrixProc->Initialize<ScaleMatrix<>>(
            next,
            SkVector{inverse.getTranslateX(), inverse.getTranslateY()},
            SkVector{inverse.getScaleX(), inverse.getScaleY()});
    } else if (inverse.getTranslateX() != 0.0f || inverse.getTranslateY() != 0.0f) {
        matrixProc->Initialize<TranslateMatrix<>>(
            next,
            SkVector{inverse.getTranslateX(), inverse.getTranslateY()});
    } else {
        return next;
    }
    return matrixProc->get();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Tile Stage

template<typename XStrategy, typename YStrategy, typename Next>
class NearestTileStage final : public SkLinearBitmapPipeline::PointProcessorInterface {
public:
    template <typename... Args>
    NearestTileStage(Next* next, SkISize dimensions)
        : fNext{next}
        , fXStrategy{dimensions.width()}
        , fYStrategy{dimensions.height()}{ }

    void VECTORCALL pointListFew(int n, Sk4s xs, Sk4s ys) override {
        fXStrategy.tileXPoints(&xs);
        fYStrategy.tileYPoints(&ys);
        fNext->pointListFew(n, xs, ys);
    }

    void VECTORCALL pointList4(Sk4s xs, Sk4s ys) override {
        fXStrategy.tileXPoints(&xs);
        fYStrategy.tileYPoints(&ys);
        fNext->pointList4(xs, ys);
    }

    // The span you pass must not be empty.
    void pointSpan(Span span) override {
        SkASSERT(!span.isEmpty());
        SkPoint start; SkScalar length; int count;
        std::tie(start, length, count) = span;
        SkScalar x = X(start);
        SkScalar y = fYStrategy.tileY(Y(start));
        Span yAdjustedSpan{{x, y}, length, count};
        if (!fXStrategy.maybeProcessSpan(yAdjustedSpan, fNext)) {
            span_fallback(span, this);
        }
    }

private:
    Next* const fNext;
    XStrategy fXStrategy;
    YStrategy fYStrategy;
};

template<typename XStrategy, typename YStrategy, typename Next>
class BilerpTileStage final : public SkLinearBitmapPipeline::PointProcessorInterface {
public:
    template <typename... Args>
    BilerpTileStage(Next* next, SkISize dimensions)
        : fXMax(dimensions.width())
        , fYMax(dimensions.height())
        , fNext{next}
        , fXStrategy{dimensions.width()}
        , fYStrategy{dimensions.height()}{ }

    void VECTORCALL pointListFew(int n, Sk4s xs, Sk4s ys) override {
        fXStrategy.tileXPoints(&xs);
        fYStrategy.tileYPoints(&ys);
        // TODO: check to see if xs and ys are in range then just call pointListFew on next.
        if (n >= 1) this->bilerpPoint(xs[0], ys[0]);
        if (n >= 2) this->bilerpPoint(xs[1], ys[1]);
        if (n >= 3) this->bilerpPoint(xs[2], ys[2]);
    }

    void VECTORCALL pointList4(Sk4s xs, Sk4s ys) override {
        fXStrategy.tileXPoints(&xs);
        fYStrategy.tileYPoints(&ys);
        // TODO: check to see if xs and ys are in range then just call pointList4 on next.
        this->bilerpPoint(xs[0], ys[0]);
        this->bilerpPoint(xs[1], ys[1]);
        this->bilerpPoint(xs[2], ys[2]);
        this->bilerpPoint(xs[3], ys[3]);
    }

    struct Wrapper {
        void pointSpan(Span span) {
            processor->breakIntoEdges(span);
        }

        void repeatSpan(Span span, int32_t repeatCount) {
            while (repeatCount --> 0) {
                processor->pointSpan(span);
            }
        }

        BilerpTileStage* processor;
    };

    // The span you pass must not be empty.
    void pointSpan(Span span) override {
        SkASSERT(!span.isEmpty());

        Wrapper wrapper = {this};
        if (!fXStrategy.maybeProcessSpan(span, &wrapper)) {
            span_fallback(span, this);
        }
    }

private:
    void bilerpPoint(SkScalar x, SkScalar y) {
        Sk4f txs = Sk4f{x} + Sk4f{-0.5f, 0.5f, -0.5f, 0.5f};
        Sk4f tys = Sk4f{y} + Sk4f{-0.5f, -0.5f, 0.5f, 0.5f};
        fXStrategy.tileXPoints(&txs);
        fYStrategy.tileYPoints(&tys);
        fNext->bilerpEdge(txs, tys);
    }

    void handleEdges(Span span, SkScalar dx) {
        SkPoint start; SkScalar length; int count;
        std::tie(start, length, count) = span;
        SkScalar x = X(start);
        SkScalar y = Y(start);
        SkScalar tiledY = fYStrategy.tileY(y);
        while (count > 0) {
            this->bilerpPoint(x, tiledY);
            x += dx;
            count -= 1;
        }
    }

    void yProcessSpan(Span span) {
        SkScalar tiledY = fYStrategy.tileY(span.startY());
        if (0.5f <= tiledY && tiledY < fYMax - 0.5f ) {
            Span tiledSpan{{span.startX(), tiledY}, span.length(), span.count()};
            fNext->pointSpan(tiledSpan);
        } else {
            // Convert to the Y0 bilerp sample set by shifting by -0.5f. Then tile that new y
            // value and shift it back resulting in the working Y0. Do the same thing with Y1 but
            // in the opposite direction.
            SkScalar y0 = fYStrategy.tileY(span.startY() - 0.5f) + 0.5f;
            SkScalar y1 = fYStrategy.tileY(span.startY() + 0.5f) - 0.5f;
            Span newSpan{{span.startX(), y0}, span.length(), span.count()};
            fNext->bilerpSpan(newSpan, y1);
        }
    }
    void breakIntoEdges(Span span) {
        if (span.length() == 0) {
            yProcessSpan(span);
        } else {
            SkScalar dx = span.length() / (span.count() - 1);
            if (span.length() > 0) {
                Span leftBorder = span.breakAt(0.5f, dx);
                if (!leftBorder.isEmpty()) {
                    this->handleEdges(leftBorder, dx);
                }
                Span center = span.breakAt(fXMax - 0.5f, dx);
                if (!center.isEmpty()) {
                    this->yProcessSpan(center);
                }

                if (!span.isEmpty()) {
                    this->handleEdges(span, dx);
                }
            } else {
                Span center = span.breakAt(fXMax + 0.5f, dx);
                if (!span.isEmpty()) {
                    this->handleEdges(span, dx);
                }
                Span leftEdge = center.breakAt(0.5f, dx);
                if (!center.isEmpty()) {
                    this->yProcessSpan(center);
                }
                if (!leftEdge.isEmpty()) {
                    this->handleEdges(leftEdge, dx);
                }

            }
        }
    }

    SkScalar fXMax;
    SkScalar fYMax;
    Next* const fNext;
    XStrategy fXStrategy;
    YStrategy fYStrategy;
};

template <typename XStrategy, typename YStrategy, typename Next>
void make_tile_stage(
    SkFilterQuality filterQuality, SkISize dimensions,
    Next* next, SkLinearBitmapPipeline::TileStage* tileStage) {
    if (filterQuality == kNone_SkFilterQuality) {
        tileStage->Initialize<NearestTileStage<XStrategy, YStrategy, Next>>(next, dimensions);
    } else {
        tileStage->Initialize<BilerpTileStage<XStrategy, YStrategy, Next>>(next, dimensions);
    }
}
template <typename XStrategy>
void choose_tiler_ymode(
    SkShader::TileMode yMode, SkFilterQuality filterQuality, SkISize dimensions,
    SkLinearBitmapPipeline::SampleProcessorInterface* next,
    SkLinearBitmapPipeline::TileStage* tileStage) {
    switch (yMode) {
        case SkShader::kClamp_TileMode:
            make_tile_stage<XStrategy, YClampStrategy>(filterQuality, dimensions, next, tileStage);
            break;
        case SkShader::kRepeat_TileMode:
            make_tile_stage<XStrategy, YRepeatStrategy>(filterQuality, dimensions, next, tileStage);
            break;
        case SkShader::kMirror_TileMode:
            make_tile_stage<XStrategy, YMirrorStrategy>(filterQuality, dimensions, next, tileStage);
            break;
    }
};

static SkLinearBitmapPipeline::PointProcessorInterface* choose_tiler(
    SkLinearBitmapPipeline::SampleProcessorInterface* next,
    SkISize dimensions,
    SkShader::TileMode xMode,
    SkShader::TileMode yMode,
    SkFilterQuality filterQuality,
    SkScalar dx,
    SkLinearBitmapPipeline::TileStage* tileStage)
{
    switch (xMode) {
        case SkShader::kClamp_TileMode:
            choose_tiler_ymode<XClampStrategy>(yMode, filterQuality, dimensions, next, tileStage);
            break;
        case SkShader::kRepeat_TileMode:
            if (dx == 1.0f && filterQuality == kNone_SkFilterQuality) {
                choose_tiler_ymode<XRepeatUnitScaleStrategy>(
                    yMode, kNone_SkFilterQuality, dimensions, next, tileStage);
            } else {
                choose_tiler_ymode<XRepeatStrategy>(
                    yMode, filterQuality, dimensions, next, tileStage);
            }
            break;
        case SkShader::kMirror_TileMode:
            choose_tiler_ymode<XMirrorStrategy>(yMode, filterQuality, dimensions, next, tileStage);
            break;
    }

    return tileStage->get();
}


////////////////////////////////////////////////////////////////////////////////////////////////////
// Source Sampling Stage
template <typename SourceStrategy, typename Next>
class NearestNeighborSampler final : public SkLinearBitmapPipeline::SampleProcessorInterface {
public:
    template <typename... Args>
    NearestNeighborSampler(Next* next, Args&&... args)
    : fSampler{next, std::forward<Args>(args)...} { }

    void VECTORCALL pointListFew(int n, Sk4s xs, Sk4s ys) override {
        fSampler.nearestListFew(n, xs, ys);
    }

    void VECTORCALL pointList4(Sk4s xs, Sk4s ys) override {
        fSampler.nearestList4(xs, ys);
    }

    void pointSpan(Span span) override {
        fSampler.nearestSpan(span);
    }

    void repeatSpan(Span span, int32_t repeatCount) override {
        while (repeatCount > 0) {
            fSampler.nearestSpan(span);
            repeatCount--;
        }
    }

    void VECTORCALL bilerpEdge(Sk4s xs, Sk4s ys) override {
        SkFAIL("Using nearest neighbor sampler, but calling a bilerpEdge.");
    }

    void bilerpSpan(Span span, SkScalar y) override {
        SkFAIL("Using nearest neighbor sampler, but calling a bilerpSpan.");
    }

private:
    GeneralSampler<SourceStrategy, Next> fSampler;
};

template <typename SourceStrategy, typename Next>
class BilerpSampler final : public SkLinearBitmapPipeline::SampleProcessorInterface {
public:
    template <typename... Args>
    BilerpSampler(Next* next, Args&&... args)
        : fSampler{next, std::forward<Args>(args)...} { }

    void VECTORCALL pointListFew(int n, Sk4s xs, Sk4s ys) override {
        fSampler.bilerpListFew(n, xs, ys);
    }

    void VECTORCALL pointList4(Sk4s xs, Sk4s ys) override {
        fSampler.bilerpList4(xs, ys);
    }

    void pointSpan(Span span) override {
        fSampler.bilerpSpan(span);
    }

    void repeatSpan(Span span, int32_t repeatCount) override {
        while (repeatCount > 0) {
            fSampler.bilerpSpan(span);
            repeatCount--;
        }
    }

    void VECTORCALL bilerpEdge(Sk4s xs, Sk4s ys) override {
        fSampler.bilerpEdge(xs, ys);
    }

    void bilerpSpan(Span span, SkScalar y) override {
        fSampler.bilerpSpanWithY(span, y);
    }

private:
    GeneralSampler<SourceStrategy, Next> fSampler;
};

using Placer = SkLinearBitmapPipeline::PixelPlacerInterface;

template<template <typename, typename> class Sampler>
static SkLinearBitmapPipeline::SampleProcessorInterface* choose_pixel_sampler_base(
    Placer* next,
    const SkPixmap& srcPixmap,
    SkLinearBitmapPipeline::SampleStage* sampleStage) {
    const SkImageInfo& imageInfo = srcPixmap.info();
    switch (imageInfo.colorType()) {
        case kRGBA_8888_SkColorType:
            if (imageInfo.profileType() == kSRGB_SkColorProfileType) {
                sampleStage->Initialize<Sampler<Pixel8888SRGB, Placer>>(next, srcPixmap);
            } else {
                sampleStage->Initialize<Sampler<Pixel8888LRGB, Placer>>(next, srcPixmap);
            }
            break;
        case kBGRA_8888_SkColorType:
            if (imageInfo.profileType() == kSRGB_SkColorProfileType) {
                sampleStage->Initialize<Sampler<Pixel8888SBGR, Placer>>(next, srcPixmap);
            } else {
                sampleStage->Initialize<Sampler<Pixel8888LBGR, Placer>>(next, srcPixmap);
            }
            break;
        case kIndex_8_SkColorType:
            if (imageInfo.profileType() == kSRGB_SkColorProfileType) {
                sampleStage->Initialize<Sampler<PixelIndex8SRGB, Placer>>(next, srcPixmap);
            } else {
                sampleStage->Initialize<Sampler<PixelIndex8LRGB, Placer>>(next, srcPixmap);
            }
            break;
        default:
            SkFAIL("Not implemented. Unsupported src");
            break;
    }
    return sampleStage->get();
}

SkLinearBitmapPipeline::SampleProcessorInterface* choose_pixel_sampler(
    Placer* next,
    SkFilterQuality filterQuality,
    const SkPixmap& srcPixmap,
    SkLinearBitmapPipeline::SampleStage* sampleStage) {
    if (filterQuality == kNone_SkFilterQuality) {
        return choose_pixel_sampler_base<NearestNeighborSampler>(next, srcPixmap, sampleStage);
    } else {
        return choose_pixel_sampler_base<BilerpSampler>(next, srcPixmap, sampleStage);
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Pixel Placement Stage
template <SkAlphaType alphaType>
class PlaceFPPixel final : public SkLinearBitmapPipeline::PixelPlacerInterface {
public:
    PlaceFPPixel(float postAlpha) : fPostAlpha{postAlpha} { }
    void VECTORCALL placePixel(Sk4f pixel) override {
        SkASSERT(fDst + 1 <= fEnd );
        PlacePixel(fDst, pixel, 0);
        fDst += 1;
    }

    void VECTORCALL place4Pixels(Sk4f p0, Sk4f p1, Sk4f p2, Sk4f p3) override {
        SkASSERT(fDst + 4 <= fEnd);
        SkPM4f* dst = fDst;
        PlacePixel(dst, p0, 0);
        PlacePixel(dst, p1, 1);
        PlacePixel(dst, p2, 2);
        PlacePixel(dst, p3, 3);
        fDst += 4;
    }

    void setDestination(void* dst, int count) override {
        fDst = static_cast<SkPM4f*>(dst);
        fEnd = fDst + count;
    }

private:
    void VECTORCALL PlacePixel(SkPM4f* dst, Sk4f pixel, int index) {
        Sk4f newPixel = pixel;
        if (alphaType == kUnpremul_SkAlphaType) {
            newPixel = Premultiply(pixel);
        }
        newPixel = newPixel * fPostAlpha;
        newPixel.store(dst + index);
    }
    static Sk4f VECTORCALL Premultiply(Sk4f pixel) {
        float alpha = pixel[3];
        return pixel * Sk4f{alpha, alpha, alpha, 1.0f};
    }

    SkPM4f* fDst;
    SkPM4f* fEnd;
    Sk4f fPostAlpha;
};

static SkLinearBitmapPipeline::PixelPlacerInterface* choose_pixel_placer(
    SkAlphaType alphaType,
    float postAlpha,
    SkLinearBitmapPipeline::PixelStage* placerStage) {
    if (alphaType == kUnpremul_SkAlphaType) {
        placerStage->Initialize<PlaceFPPixel<kUnpremul_SkAlphaType>>(postAlpha);
    } else {
        // kOpaque_SkAlphaType is treated the same as kPremul_SkAlphaType
        placerStage->Initialize<PlaceFPPixel<kPremul_SkAlphaType>>(postAlpha);
    }
    return placerStage->get();
}
}  // namespace

////////////////////////////////////////////////////////////////////////////////////////////////////
SkLinearBitmapPipeline::~SkLinearBitmapPipeline() {}

SkLinearBitmapPipeline::SkLinearBitmapPipeline(
    const SkMatrix& inverse,
    SkFilterQuality filterQuality,
    SkShader::TileMode xTile, SkShader::TileMode yTile,
    float postAlpha,
    const SkPixmap& srcPixmap)
{
    SkISize dimensions = srcPixmap.info().dimensions();
    const SkImageInfo& srcImageInfo = srcPixmap.info();

    SkMatrix adjustedInverse = inverse;
    if (filterQuality == kNone_SkFilterQuality) {
        if (inverse.getScaleX() >= 0.0f) {
            adjustedInverse.setTranslateX(
                nextafterf(inverse.getTranslateX(), std::floor(inverse.getTranslateX())));
        }
        if (inverse.getScaleY() >= 0.0f) {
            adjustedInverse.setTranslateY(
                nextafterf(inverse.getTranslateY(), std::floor(inverse.getTranslateY())));
        }
    }

    SkScalar dx = adjustedInverse.getScaleX();

    // If it is an index 8 color type, the sampler converts to unpremul for better fidelity.
    SkAlphaType alphaType = srcImageInfo.alphaType();
    if (srcPixmap.colorType() == kIndex_8_SkColorType) {
        alphaType = kUnpremul_SkAlphaType;
    }

    // As the stages are built, the chooser function may skip a stage. For example, with the
    // identity matrix, the matrix stage is skipped, and the tilerStage is the first stage.
    auto placementStage = choose_pixel_placer(alphaType, postAlpha, &fPixelStage);
    auto samplerStage   = choose_pixel_sampler(placementStage,
                                               filterQuality, srcPixmap, &fSampleStage);
    auto tilerStage     = choose_tiler(samplerStage,
                                       dimensions, xTile, yTile, filterQuality, dx, &fTiler);
    fFirstStage         = choose_matrix(tilerStage, adjustedInverse, &fMatrixStage);
}

void SkLinearBitmapPipeline::shadeSpan4f(int x, int y, SkPM4f* dst, int count) {
    SkASSERT(count > 0);
    fPixelStage->setDestination(dst, count);
    // The count and length arguments start out in a precise relation in order to keep the
    // math correct through the different stages. Count is the number of pixel to produce.
    // Since the code samples at pixel centers, length is the distance from the center of the
    // first pixel to the center of the last pixel. This implies that length is count-1.
    fFirstStage->pointSpan(Span{{x + 0.5f, y + 0.5f}, count - 1.0f, count});
}

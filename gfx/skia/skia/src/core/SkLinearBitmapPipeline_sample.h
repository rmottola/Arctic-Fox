/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkLinearBitmapPipeline_sampler_DEFINED
#define SkLinearBitmapPipeline_sampler_DEFINED

#include "SkFixed.h"
#include "SkLinearBitmapPipeline_core.h"

namespace {
// Explaination of the math:
//              1 - x      x
//           +--------+--------+
//           |        |        |
//  1 - y    |  px00  |  px10  |
//           |        |        |
//           +--------+--------+
//           |        |        |
//    y      |  px01  |  px11  |
//           |        |        |
//           +--------+--------+
//
//
// Given a pixelxy each is multiplied by a different factor derived from the fractional part of x
// and y:
// * px00 -> (1 - x)(1 - y) = 1 - x - y + xy
// * px10 -> x(1 - y) = x - xy
// * px01 -> (1 - x)y = y - xy
// * px11 -> xy
// So x * y is calculated first and then used to calculate all the other factors.
static Sk4s VECTORCALL bilerp4(Sk4s xs, Sk4s ys, Sk4f px00, Sk4f px10,
                               Sk4f px01, Sk4f px11) {
    // Calculate fractional xs and ys.
    Sk4s fxs = xs - xs.floor();
    Sk4s fys = ys - ys.floor();
    Sk4s fxys{fxs * fys};
    Sk4f sum = px11 * fxys;
    sum = sum + px01 * (fys - fxys);
    sum = sum + px10 * (fxs - fxys);
    sum = sum + px00 * (Sk4f{1.0f} - fxs - fys + fxys);
    return sum;
}

// The GeneralSampler class
template<typename SourceStrategy, typename Next>
class GeneralSampler {
public:
    template<typename... Args>
    GeneralSampler(SkLinearBitmapPipeline::PixelPlacerInterface* next, Args&& ... args)
        : fNext{next}, fStrategy{std::forward<Args>(args)...} { }

    void VECTORCALL nearestListFew(int n, Sk4s xs, Sk4s ys) {
        SkASSERT(0 < n && n < 4);
        Sk4f px0, px1, px2;
        fStrategy.getFewPixels(n, xs, ys, &px0, &px1, &px2);
        if (n >= 1) fNext->placePixel(px0);
        if (n >= 2) fNext->placePixel(px1);
        if (n >= 3) fNext->placePixel(px2);
    }

    void VECTORCALL nearestList4(Sk4s xs, Sk4s ys) {
        Sk4f px0, px1, px2, px3;
        fStrategy.get4Pixels(xs, ys, &px0, &px1, &px2, &px3);
        fNext->place4Pixels(px0, px1, px2, px3);
    }

    void nearestSpan(Span span) {
        SkASSERT(!span.isEmpty());
        SkPoint start;
        SkScalar length;
        int count;
        std::tie(start, length, count) = span;
        SkScalar absLength = SkScalarAbs(length);
        if (absLength < (count - 1)) {
            this->nearestSpanSlowRate(span);
        } else if (absLength == (count - 1)) {
            this->nearestSpanUnitRate(span);
        } else {
            this->nearestSpanFastRate(span);
        }
    }

    Sk4f bilerNonEdgePixel(SkScalar x, SkScalar y) {
        Sk4f px00, px10, px01, px11;
        Sk4f xs = Sk4f{x};
        Sk4f ys = Sk4f{y};
        Sk4f sampleXs = xs + Sk4f{-0.5f, 0.5f, -0.5f, 0.5f};
        Sk4f sampleYs = ys + Sk4f{-0.5f, -0.5f, 0.5f, 0.5f};
        fStrategy.get4Pixels(sampleXs, sampleYs, &px00, &px10, &px01, &px11);
        return bilerp4(xs, ys, px00, px10, px01, px11);
    }

    void VECTORCALL bilerpListFew(int n, Sk4s xs, Sk4s ys) {
        SkASSERT(0 < n && n < 4);
        auto bilerpPixel = [&](int index) {
            return this->bilerNonEdgePixel(xs[index], ys[index]);
        };

        if (n >= 1) fNext->placePixel(bilerpPixel(0));
        if (n >= 2) fNext->placePixel(bilerpPixel(1));
        if (n >= 3) fNext->placePixel(bilerpPixel(2));
    }

    void VECTORCALL bilerpList4(Sk4s xs, Sk4s ys) {
        auto bilerpPixel = [&](int index) {
            return this->bilerNonEdgePixel(xs[index], ys[index]);
        };
        fNext->place4Pixels(bilerpPixel(0), bilerpPixel(1), bilerpPixel(2), bilerpPixel(3));
    }

    void VECTORCALL bilerpEdge(Sk4s sampleXs, Sk4s sampleYs) {
        Sk4f px00, px10, px01, px11;
        Sk4f xs = Sk4f{sampleXs[0]};
        Sk4f ys = Sk4f{sampleYs[0]};
        fStrategy.get4Pixels(sampleXs, sampleYs, &px00, &px10, &px01, &px11);
        Sk4f pixel = bilerp4(xs, ys, px00, px10, px01, px11);
        fNext->placePixel(pixel);
    }

    void bilerpSpan(Span span) {
        this->bilerpSpanWithY(span, span.startY());
    }

    void bilerpSpanWithY(Span span, SkScalar y) {
        SkASSERT(!span.isEmpty());
        SkPoint start;
        SkScalar length;
        int count;
        std::tie(start, length, count) = span;
        SkScalar absLength = SkScalarAbs(length);
        if (absLength == 0.0f) {
            this->bilerpSpanZeroRate(span, y);
        } else if (absLength < (count - 1)) {
            this->bilerpSpanSlowRate(span, y);
        } else if (absLength == (count - 1)) {
            if (std::fmod(span.startX() - 0.5f, 1.0f) == 0.0f) {
                if (std::fmod(span.startY() - 0.5f, 1.0f) == 0.0f) {
                    this->nearestSpanUnitRate(span);
                } else {
                    this->bilerpSpanUnitRateAlignedX(span, y);
                }
            } else {
                this->bilerpSpanUnitRate(span, y);
            }
        } else {
            this->bilerpSpanFastRate(span, y);
        }
    }

private:
    // When moving through source space more slowly than dst space (zoomed in),
    // we'll be sampling from the same source pixel more than once.
    void nearestSpanSlowRate(Span span) {
        SkPoint start;
        SkScalar length;
        int count;
        std::tie(start, length, count) = span;
        SkScalar x = X(start);
        SkFixed fx = SkScalarToFixed(x);
        SkScalar dx = length / (count - 1);
        SkFixed fdx = SkScalarToFixed(dx);

        const void* row = fStrategy.row((int)std::floor(Y(start)));
        Next* next = fNext;

        int ix = SkFixedFloorToInt(fx);
        int prevIX = ix;
        Sk4f fpixel = fStrategy.getPixelAt(row, ix);

        // When dx is less than one, each pixel is used more than once. Using the fixed point fx
        // allows the code to quickly check that the same pixel is being used. The code uses this
        // same pixel check to do the sRGB and normalization only once.
        auto getNextPixel = [&]() {
            if (ix != prevIX) {
                fpixel = fStrategy.getPixelAt(row, ix);
                prevIX = ix;
            }
            fx += fdx;
            ix = SkFixedFloorToInt(fx);
            return fpixel;
        };

        while (count >= 4) {
            Sk4f px0 = getNextPixel();
            Sk4f px1 = getNextPixel();
            Sk4f px2 = getNextPixel();
            Sk4f px3 = getNextPixel();
            next->place4Pixels(px0, px1, px2, px3);
            count -= 4;
        }
        while (count > 0) {
            next->placePixel(getNextPixel());
            count -= 1;
        }
    }

    // We're moving through source space at a rate of 1 source pixel per 1 dst pixel.
    // We'll never re-use pixels, but we can at least load contiguous pixels.
    void nearestSpanUnitRate(Span span) {
        SkPoint start;
        SkScalar length;
        int count;
        std::tie(start, length, count) = span;
        int ix = SkScalarFloorToInt(X(start));
        const void* row = fStrategy.row((int)std::floor(Y(start)));
        Next* next = fNext;
        if (length > 0) {
            while (count >= 4) {
                Sk4f px0, px1, px2, px3;
                fStrategy.get4Pixels(row, ix, &px0, &px1, &px2, &px3);
                next->place4Pixels(px0, px1, px2, px3);
                ix += 4;
                count -= 4;
            }

            while (count > 0) {
                next->placePixel(fStrategy.getPixelAt(row, ix));
                ix += 1;
                count -= 1;
            }
        } else {
            while (count >= 4) {
                Sk4f px0, px1, px2, px3;
                fStrategy.get4Pixels(row, ix - 3, &px3, &px2, &px1, &px0);
                next->place4Pixels(px0, px1, px2, px3);
                ix -= 4;
                count -= 4;
            }

            while (count > 0) {
                next->placePixel(fStrategy.getPixelAt(row, ix));
                ix -= 1;
                count -= 1;
            }
        }
    }

    // We're moving through source space faster than dst (zoomed out),
    // so we'll never reuse a source pixel or be able to do contiguous loads.
    void nearestSpanFastRate(Span span) {
        struct NearestWrapper {
            void VECTORCALL pointListFew(int n, Sk4s xs, Sk4s ys) {
                fSampler.nearestListFew(n, xs, ys);
            }

            void VECTORCALL pointList4(Sk4s xs, Sk4s ys) {
                fSampler.nearestList4(xs, ys);
            }

            GeneralSampler& fSampler;
        };
        NearestWrapper wrapper{*this};
        span_fallback(span, &wrapper);
    }

    void bilerpSpanZeroRate(Span span, SkScalar y1) {
        SkScalar y0 = span.startY() - 0.5f;
        y1 += 0.5f;
        int iy0 = SkScalarFloorToInt(y0);
        SkScalar filterY1 = y0 - iy0;
        SkScalar filterY0 = 1.0f - filterY1;
        int iy1 = SkScalarFloorToInt(y1);
        int ix = SkScalarFloorToInt(span.startX());
        Sk4f pixelY0 = fStrategy.getPixelAt(fStrategy.row(iy0), ix);
        Sk4f pixelY1 = fStrategy.getPixelAt(fStrategy.row(iy1), ix);
        Sk4f filterPixel = pixelY0 * filterY0 + pixelY1 * filterY1;
        int count = span.count();
        while (count >= 4) {
            fNext->place4Pixels(filterPixel, filterPixel, filterPixel, filterPixel);
            count -= 4;
        }
        while (count > 0) {
            fNext->placePixel(filterPixel);
            count -= 1;
        }
    }

    // When moving through source space more slowly than dst space (zoomed in),
    // we'll be sampling from the same source pixel more than once.
    void bilerpSpanSlowRate(Span span, SkScalar ry1) {
        SkPoint start;
        SkScalar length;
        int count;
        std::tie(start, length, count) = span;
        SkFixed fx = SkScalarToFixed(X(start)
                                         -0.5f);

        SkFixed fdx = SkScalarToFixed(length / (count - 1));
        //start = start + SkPoint{-0.5f, -0.5f};

        Sk4f xAdjust;
        if (fdx >= 0) {
            xAdjust = Sk4f{-1.0f};
        } else {
            xAdjust = Sk4f{1.0f};
        }
        int ix = SkFixedFloorToInt(fx);
        int ioldx = ix;
        Sk4f x{SkFixedToScalar(fx) - ix};
        Sk4f dx{SkFixedToScalar(fdx)};
        SkScalar ry0 = Y(start) - 0.5f;
        ry1 += 0.5f;
        SkScalar yFloor = std::floor(ry0);
        Sk4f y1 = Sk4f{ry0 - yFloor};
        Sk4f y0 = Sk4f{1.0f} - y1;
        const void* const row0 = fStrategy.row(SkScalarFloorToInt(ry0));
        const void* const row1 = fStrategy.row(SkScalarFloorToInt(ry1));
        Sk4f fpixel00 = y0 * fStrategy.getPixelAt(row0, ix);
        Sk4f fpixel01 = y1 * fStrategy.getPixelAt(row1, ix);
        Sk4f fpixel10 = y0 * fStrategy.getPixelAt(row0, ix + 1);
        Sk4f fpixel11 = y1 * fStrategy.getPixelAt(row1, ix + 1);
        auto getNextPixel = [&]() {
            if (ix != ioldx) {
                fpixel00 = fpixel10;
                fpixel01 = fpixel11;
                fpixel10 = y0 * fStrategy.getPixelAt(row0, ix + 1);
                fpixel11 = y1 * fStrategy.getPixelAt(row1, ix + 1);
                ioldx = ix;
                x = x + xAdjust;
            }

            Sk4f x0, x1;
            x0 = Sk4f{1.0f} - x;
            x1 = x;
            Sk4f fpixel = x0 * (fpixel00 + fpixel01) + x1 * (fpixel10 + fpixel11);
            fx += fdx;
            ix = SkFixedFloorToInt(fx);
            x = x + dx;
            return fpixel;
        };

        while (count >= 4) {
            Sk4f fpixel0 = getNextPixel();
            Sk4f fpixel1 = getNextPixel();
            Sk4f fpixel2 = getNextPixel();
            Sk4f fpixel3 = getNextPixel();

            fNext->place4Pixels(fpixel0, fpixel1, fpixel2, fpixel3);
            count -= 4;
        }

        while (count > 0) {
            fNext->placePixel(getNextPixel());

            count -= 1;
        }
    }

    // We're moving through source space at a rate of 1 source pixel per 1 dst pixel.
    // We'll never re-use pixels, but we can at least load contiguous pixels.
    void bilerpSpanUnitRate(Span span, SkScalar y1) {
        y1 += 0.5f;
        SkScalar y0 = span.startY() - 0.5f;
        int iy0 = SkScalarFloorToInt(y0);
        SkScalar filterY1 = y0 - iy0;
        SkScalar filterY0 = 1.0f - filterY1;
        int iy1 = SkScalarFloorToInt(y1);
        const void* rowY0 = fStrategy.row(iy0);
        const void* rowY1 = fStrategy.row(iy1);
        SkScalar x0 = span.startX() - 0.5f;
        int ix0 = SkScalarFloorToInt(x0);
        SkScalar filterX1 = x0 - ix0;
        SkScalar filterX0 = 1.0f - filterX1;

        auto getPixelY0 = [&]() {
            Sk4f px = fStrategy.getPixelAt(rowY0, ix0);
            return px * filterY0;
        };

        auto getPixelY1 = [&]() {
            Sk4f px = fStrategy.getPixelAt(rowY1, ix0);
            return px * filterY1;
        };

        auto get4PixelsY0 = [&](int ix, Sk4f* px0, Sk4f* px1, Sk4f* px2, Sk4f* px3) {
            fStrategy.get4Pixels(rowY0, ix, px0, px1, px2, px3);
            *px0 = *px0 * filterY0;
            *px1 = *px1 * filterY0;
            *px2 = *px2 * filterY0;
            *px3 = *px3 * filterY0;
        };

        auto get4PixelsY1 = [&](int ix, Sk4f* px0, Sk4f* px1, Sk4f* px2, Sk4f* px3) {
            fStrategy.get4Pixels(rowY1, ix, px0, px1, px2, px3);
            *px0 = *px0 * filterY1;
            *px1 = *px1 * filterY1;
            *px2 = *px2 * filterY1;
            *px3 = *px3 * filterY1;
        };

        auto lerp = [&](Sk4f& pixelX0, Sk4f& pixelX1) {
            return pixelX0 * filterX0 + pixelX1 * filterX1;
        };

        // Mid making 4 unit rate.
        Sk4f pxB = getPixelY0() + getPixelY1();
        if (span.length() > 0) {
            int count = span.count();
            while (count >= 4) {
                Sk4f px00, px10, px20, px30;
                get4PixelsY0(ix0, &px00, &px10, &px20, &px30);
                Sk4f px01, px11, px21, px31;
                get4PixelsY1(ix0, &px01, &px11, &px21, &px31);
                Sk4f pxS0 = px00 + px01;
                Sk4f px0 = lerp(pxB, pxS0);
                Sk4f pxS1 = px10 + px11;
                Sk4f px1 = lerp(pxS0, pxS1);
                Sk4f pxS2 = px20 + px21;
                Sk4f px2 = lerp(pxS1, pxS2);
                Sk4f pxS3 = px30 + px31;
                Sk4f px3 = lerp(pxS2, pxS3);
                pxB = pxS3;
                fNext->place4Pixels(
                    px0,
                    px1,
                    px2,
                    px3);
                ix0 += 4;
                count -= 4;
            }
            while (count > 0) {
                Sk4f pixelY0 = fStrategy.getPixelAt(rowY0, ix0);
                Sk4f pixelY1 = fStrategy.getPixelAt(rowY1, ix0);

                fNext->placePixel(lerp(pixelY0, pixelY1));
                ix0 += 1;
                count -= 1;
            }
        } else {
            int count = span.count();
            while (count >= 4) {
                Sk4f px00, px10, px20, px30;
                get4PixelsY0(ix0 - 3, &px00, &px10, &px20, &px30);
                Sk4f px01, px11, px21, px31;
                get4PixelsY1(ix0 - 3, &px01, &px11, &px21, &px31);
                Sk4f pxS3 = px30 + px31;
                Sk4f px0 = lerp(pxS3, pxB);
                Sk4f pxS2 = px20 + px21;
                Sk4f px1 = lerp(pxS2, pxS3);
                Sk4f pxS1 = px10 + px11;
                Sk4f px2 = lerp(pxS1, pxS2);
                Sk4f pxS0 = px00 + px01;
                Sk4f px3 = lerp(pxS0, pxS1);
                pxB = pxS0;
                fNext->place4Pixels(
                    px0,
                    px1,
                    px2,
                    px3);
                ix0 -= 4;
                count -= 4;
            }
            while (count > 0) {
                Sk4f pixelY0 = fStrategy.getPixelAt(rowY0, ix0);
                Sk4f pixelY1 = fStrategy.getPixelAt(rowY1, ix0);

                fNext->placePixel(lerp(pixelY0, pixelY1));
                ix0 -= 1;
                count -= 1;
            }
        }
    }

    void bilerpSpanUnitRateAlignedX(Span span, SkScalar y1) {
        SkScalar y0 = span.startY() - 0.5f;
        y1 += 0.5f;
        int iy0 = SkScalarFloorToInt(y0);
        SkScalar filterY1 = y0 - iy0;
        SkScalar filterY0 = 1.0f - filterY1;
        int iy1 = SkScalarFloorToInt(y1);
        int ix = SkScalarFloorToInt(span.startX());
        const void* rowY0 = fStrategy.row(iy0);
        const void* rowY1 = fStrategy.row(iy1);
        auto lerp = [&](Sk4f* pixelY0, Sk4f* pixelY1) {
            return *pixelY0 * filterY0 + *pixelY1 * filterY1;
        };

        if (span.length() > 0) {
            int count = span.count();
            while (count >= 4) {
                Sk4f px00, px10, px20, px30;
                fStrategy.get4Pixels(rowY0, ix, &px00, &px10, &px20, &px30);
                Sk4f px01, px11, px21, px31;
                fStrategy.get4Pixels(rowY1, ix, &px01, &px11, &px21, &px31);
                fNext->place4Pixels(
                    lerp(&px00, &px01), lerp(&px10, &px11), lerp(&px20, &px21), lerp(&px30, &px31));
                ix += 4;
                count -= 4;
            }
            while (count > 0) {
                Sk4f pixelY0 = fStrategy.getPixelAt(rowY0, ix);
                Sk4f pixelY1 = fStrategy.getPixelAt(rowY1, ix);

                fNext->placePixel(lerp(&pixelY0, &pixelY1));
                ix += 1;
                count -= 1;
            }
        } else {
            int count = span.count();
            while (count >= 4) {
                Sk4f px00, px10, px20, px30;
                fStrategy.get4Pixels(rowY0, ix - 3, &px30, &px20, &px10, &px00);
                Sk4f px01, px11, px21, px31;
                fStrategy.get4Pixels(rowY1, ix - 3, &px31, &px21, &px11, &px01);
                fNext->place4Pixels(
                    lerp(&px00, &px01), lerp(&px10, &px11), lerp(&px20, &px21), lerp(&px30, &px31));
                ix -= 4;
                count -= 4;
            }
            while (count > 0) {
                Sk4f pixelY0 = fStrategy.getPixelAt(rowY0, ix);
                Sk4f pixelY1 = fStrategy.getPixelAt(rowY1, ix);

                fNext->placePixel(lerp(&pixelY0, &pixelY1));
                ix -= 1;
                count -= 1;
            }
        }
    }

    // We're moving through source space faster than dst (zoomed out),
    // so we'll never reuse a source pixel or be able to do contiguous loads.
    void bilerpSpanFastRate(Span span, SkScalar y1) {
        SkPoint start;
        SkScalar length;
        int count;
        std::tie(start, length, count) = span;
        SkScalar x = X(start);
        SkScalar y = Y(start);
        if (false && y == y1) {
            struct BilerpWrapper {
                void VECTORCALL pointListFew(int n, Sk4s xs, Sk4s ys) {
                    fSampler.bilerpListFew(n, xs, ys);
                }

                void VECTORCALL pointList4(Sk4s xs, Sk4s ys) {
                    fSampler.bilerpList4(xs, ys);
                }

                GeneralSampler& fSampler;
            };
            BilerpWrapper wrapper{*this};
            span_fallback(span, &wrapper);
        } else {
            SkScalar dx = length / (count - 1);
            Sk4f ys = {y - 0.5f, y - 0.5f, y1 + 0.5f, y1 + 0.5f};
            while (count > 0) {
                Sk4f xs = Sk4f{-0.5f, 0.5f, -0.5f, 0.5f} + Sk4f{x};
                this->bilerpEdge(xs, ys);
                x += dx;
                count -= 1;
            }
        }
    }

    Next* const fNext;
    SourceStrategy fStrategy;
};

class sRGBFast {
public:
    static Sk4s VECTORCALL sRGBToLinear(Sk4s pixel) {
        Sk4s l = pixel * pixel;
        return Sk4s{l[0], l[1], l[2], pixel[3]};
    }
};

enum class ColorOrder {
    kRGBA = false,
    kBGRA = true,
};
template <SkColorProfileType colorProfile, ColorOrder colorOrder>
class Pixel8888 {
public:
    Pixel8888(int width, const uint32_t* src) : fSrc{src}, fWidth{width}{ }
    Pixel8888(const SkPixmap& srcPixmap)
        : fSrc{srcPixmap.addr32()}
        , fWidth{static_cast<int>(srcPixmap.rowBytes() / 4)} { }

    void VECTORCALL getFewPixels(int n, Sk4s xs, Sk4s ys, Sk4f* px0, Sk4f* px1, Sk4f* px2) {
        Sk4i XIs = SkNx_cast<int, SkScalar>(xs);
        Sk4i YIs = SkNx_cast<int, SkScalar>(ys);
        Sk4i bufferLoc = YIs * fWidth + XIs;
        switch (n) {
            case 3:
                *px2 = this->getPixelAt(fSrc, bufferLoc[2]);
            case 2:
                *px1 = this->getPixelAt(fSrc, bufferLoc[1]);
            case 1:
                *px0 = this->getPixelAt(fSrc, bufferLoc[0]);
            default:
                break;
        }
    }

    void VECTORCALL get4Pixels(Sk4s xs, Sk4s ys, Sk4f* px0, Sk4f* px1, Sk4f* px2, Sk4f* px3) {
        Sk4i XIs = SkNx_cast<int, SkScalar>(xs);
        Sk4i YIs = SkNx_cast<int, SkScalar>(ys);
        Sk4i bufferLoc = YIs * fWidth + XIs;
        *px0 = this->getPixelAt(fSrc, bufferLoc[0]);
        *px1 = this->getPixelAt(fSrc, bufferLoc[1]);
        *px2 = this->getPixelAt(fSrc, bufferLoc[2]);
        *px3 = this->getPixelAt(fSrc, bufferLoc[3]);
    }

    void get4Pixels(const void* vsrc, int index, Sk4f* px0, Sk4f* px1, Sk4f* px2, Sk4f* px3) {
        const uint32_t* src = static_cast<const uint32_t*>(vsrc);
        *px0 = this->getPixelAt(src, index + 0);
        *px1 = this->getPixelAt(src, index + 1);
        *px2 = this->getPixelAt(src, index + 2);
        *px3 = this->getPixelAt(src, index + 3);
    }

    Sk4f getPixelAt(const void* vsrc, int index) {
        const uint32_t* src = static_cast<const uint32_t*>(vsrc);
        Sk4b bytePixel = Sk4b::Load((uint8_t *)(&src[index]));
        Sk4f pixel = SkNx_cast<float, uint8_t>(bytePixel);
        if (colorOrder == ColorOrder::kBGRA) {
            pixel = SkNx_shuffle<2, 1, 0, 3>(pixel);
        }
        pixel = pixel * Sk4f{1.0f/255.0f};
        if (colorProfile == kSRGB_SkColorProfileType) {
            pixel = sRGBFast::sRGBToLinear(pixel);
        }
        return pixel;
    }

    const void* row(int y) { return fSrc + y * fWidth[0]; }

private:
    const uint32_t* const fSrc;
    const Sk4i            fWidth;
};
using Pixel8888SRGB = Pixel8888<kSRGB_SkColorProfileType, ColorOrder::kRGBA>;
using Pixel8888LRGB = Pixel8888<kLinear_SkColorProfileType, ColorOrder::kRGBA>;
using Pixel8888SBGR = Pixel8888<kSRGB_SkColorProfileType, ColorOrder::kBGRA>;
using Pixel8888LBGR = Pixel8888<kLinear_SkColorProfileType, ColorOrder::kBGRA>;

template <SkColorProfileType colorProfile>
class PixelIndex8 {
public:
    PixelIndex8(const SkPixmap& srcPixmap)
        : fSrc{srcPixmap.addr8()}, fWidth{static_cast<int>(srcPixmap.rowBytes())} {
        SkASSERT(srcPixmap.colorType() == kIndex_8_SkColorType);
        SkColorTable* skColorTable = srcPixmap.ctable();
        SkASSERT(skColorTable != nullptr);

        fColorTable = (Sk4f*)SkAlign16((intptr_t)fColorTableStorage.get());
        for (int i = 0; i < skColorTable->count(); i++) {
            fColorTable[i] = this->convertPixel((*skColorTable)[i]);
        }
    }

    void VECTORCALL getFewPixels(int n, Sk4s xs, Sk4s ys, Sk4f* px0, Sk4f* px1, Sk4f* px2) {
        Sk4i XIs = SkNx_cast<int, SkScalar>(xs);
        Sk4i YIs = SkNx_cast<int, SkScalar>(ys);
        Sk4i bufferLoc = YIs * fWidth + XIs;
        switch (n) {
            case 3:
                *px2 = this->getPixelAt(fSrc, bufferLoc[2]);
            case 2:
                *px1 = this->getPixelAt(fSrc, bufferLoc[1]);
            case 1:
                *px0 = this->getPixelAt(fSrc, bufferLoc[0]);
            default:
                break;
        }
    }

    void VECTORCALL get4Pixels(Sk4s xs, Sk4s ys, Sk4f* px0, Sk4f* px1, Sk4f* px2, Sk4f* px3) {
        Sk4i XIs = SkNx_cast<int, SkScalar>(xs);
        Sk4i YIs = SkNx_cast<int, SkScalar>(ys);
        Sk4i bufferLoc = YIs * fWidth + XIs;
        *px0 = this->getPixelAt(fSrc, bufferLoc[0]);
        *px1 = this->getPixelAt(fSrc, bufferLoc[1]);
        *px2 = this->getPixelAt(fSrc, bufferLoc[2]);
        *px3 = this->getPixelAt(fSrc, bufferLoc[3]);
    }

    void get4Pixels(const void* vsrc, int index, Sk4f* px0, Sk4f* px1, Sk4f* px2, Sk4f* px3) {
        *px0 = this->getPixelAt(vsrc, index + 0);
        *px1 = this->getPixelAt(vsrc, index + 1);
        *px2 = this->getPixelAt(vsrc, index + 2);
        *px3 = this->getPixelAt(vsrc, index + 3);
    }

    Sk4f getPixelAt(const void* vsrc, int index) {
        const uint8_t* src = static_cast<const uint8_t*>(vsrc);
        return getPixel(src + index);
    }

    Sk4f getPixel(const uint8_t* src) {
        Sk4f pixel = fColorTable[*src];
        return pixel;
    }

    const void* row(int y) { return fSrc + y * fWidth[0]; }

private:
    static const size_t kColorTableSize = sizeof(Sk4f[256]) + 12;
    Sk4f convertPixel(SkPMColor pmColor) {
        Sk4b bPixel = Sk4b::Load(&pmColor);
        Sk4f pixel = SkNx_cast<float, uint8_t>(bPixel);
        float alpha = pixel[3];
        if (alpha != 0.0f) {
            float invAlpha = 1.0f / pixel[3];
            Sk4f normalize = {invAlpha, invAlpha, invAlpha, 1.0f / 255.0f};
            pixel = pixel * normalize;
            if (colorProfile == kSRGB_SkColorProfileType) {
                pixel = sRGBFast::sRGBToLinear(pixel);
            }
            return pixel;
        } else {
            return Sk4f{0.0f};
        }
    }
    const uint8_t* const fSrc;
    const Sk4i           fWidth;
    SkAutoMalloc         fColorTableStorage{kColorTableSize};
    Sk4f*                fColorTable;
};

using PixelIndex8SRGB = PixelIndex8<kSRGB_SkColorProfileType>;
using PixelIndex8LRGB = PixelIndex8<kLinear_SkColorProfileType>;

}  // namespace

#endif  // SkLinearBitmapPipeline_sampler_DEFINED

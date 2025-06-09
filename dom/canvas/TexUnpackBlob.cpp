/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TexUnpackBlob.h"

#include "GLBlitHelper.h"
#include "GLContext.h"
#include "GLDefs.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/HTMLCanvasElement.h"
#include "mozilla/RefPtr.h"
#include "nsLayoutUtils.h"
#include "WebGLContext.h"
#include "WebGLTexelConversions.h"
#include "WebGLTexture.h"

namespace mozilla {
namespace webgl {

static bool
UnpackFormatHasColorAndAlpha(GLenum unpackFormat)
{
    switch (unpackFormat) {
    case LOCAL_GL_LUMINANCE_ALPHA:
    case LOCAL_GL_RGBA:
    case LOCAL_GL_SRGB_ALPHA:
        return true;

    default:
        return false;
    }
}

static WebGLTexelFormat
FormatForPackingInfo(const PackingInfo& pi)
{
    switch (pi.type) {
    case LOCAL_GL_UNSIGNED_BYTE:
        switch (pi.format) {
        case LOCAL_GL_RED:
        case LOCAL_GL_LUMINANCE:
        case LOCAL_GL_RED_INTEGER:
            return WebGLTexelFormat::R8;

        case LOCAL_GL_ALPHA:
            return WebGLTexelFormat::A8;

        case LOCAL_GL_LUMINANCE_ALPHA:
            return WebGLTexelFormat::RA8;

        case LOCAL_GL_RGB:
        case LOCAL_GL_RGB_INTEGER:
            return WebGLTexelFormat::RGB8;

        case LOCAL_GL_RGBA:
        case LOCAL_GL_RGBA_INTEGER:
            return WebGLTexelFormat::RGBA8;

        case LOCAL_GL_RG:
        case LOCAL_GL_RG_INTEGER:
            return WebGLTexelFormat::RG8;

        default:
            break;
        }
        break;

    case LOCAL_GL_UNSIGNED_SHORT_5_6_5:
        if (pi.format == LOCAL_GL_RGB)
            return WebGLTexelFormat::RGB565;
        break;

    case LOCAL_GL_UNSIGNED_SHORT_5_5_5_1:
        if (pi.format == LOCAL_GL_RGBA)
            return WebGLTexelFormat::RGBA5551;
        break;

    case LOCAL_GL_UNSIGNED_SHORT_4_4_4_4:
        if (pi.format == LOCAL_GL_RGBA)
            return WebGLTexelFormat::RGBA4444;
        break;

    case LOCAL_GL_HALF_FLOAT:
    case LOCAL_GL_HALF_FLOAT_OES:
        switch (pi.format) {
        case LOCAL_GL_RED:
        case LOCAL_GL_LUMINANCE:
            return WebGLTexelFormat::R16F;

        case LOCAL_GL_ALPHA:           return WebGLTexelFormat::A16F;
        case LOCAL_GL_LUMINANCE_ALPHA: return WebGLTexelFormat::RA16F;
        case LOCAL_GL_RG:              return WebGLTexelFormat::RG16F;
        case LOCAL_GL_RGB:             return WebGLTexelFormat::RGB16F;
        case LOCAL_GL_RGBA:            return WebGLTexelFormat::RGBA16F;

        default:
            break;
        }
        break;

    case LOCAL_GL_FLOAT:
        switch (pi.format) {
        case LOCAL_GL_RED:
        case LOCAL_GL_LUMINANCE:
            return WebGLTexelFormat::R32F;

        case LOCAL_GL_ALPHA:           return WebGLTexelFormat::A32F;
        case LOCAL_GL_LUMINANCE_ALPHA: return WebGLTexelFormat::RA32F;
        case LOCAL_GL_RG:              return WebGLTexelFormat::RG32F;
        case LOCAL_GL_RGB:             return WebGLTexelFormat::RGB32F;
        case LOCAL_GL_RGBA:            return WebGLTexelFormat::RGBA32F;

        default:
            break;
        }
        break;

    case LOCAL_GL_UNSIGNED_INT_10F_11F_11F_REV:
        if (pi.format == LOCAL_GL_RGB)
            return WebGLTexelFormat::RGB11F11F10F;
        break;

    default:
        break;
    }

    return WebGLTexelFormat::FormatNotSupportingAnyConversion;
}

////////////////////

static uint32_t
FallbackOnZero(uint32_t val, uint32_t fallback)
{
    return (val ? val : fallback);
}

TexUnpackBlob::TexUnpackBlob(const WebGLContext* webgl, TexImageTarget target,
                             uint32_t rowLength, uint32_t width, uint32_t height,
                             uint32_t depth, bool isSrcPremult)
    : mAlignment(webgl->mPixelStore_UnpackAlignment)
    , mRowLength(rowLength)
    , mImageHeight(FallbackOnZero(webgl->mPixelStore_UnpackImageHeight, height))

    , mSkipPixels(webgl->mPixelStore_UnpackSkipPixels)
    , mSkipRows(webgl->mPixelStore_UnpackSkipRows)
    , mSkipImages(IsTarget3D(target) ? webgl->mPixelStore_UnpackSkipImages : 0)

    , mWidth(width)
    , mHeight(height)
    , mDepth(depth)

    , mIsSrcPremult(isSrcPremult)
{
    MOZ_ASSERT_IF(!IsTarget3D(target), mDepth == 1);
}

bool
TexUnpackBlob::ConvertIfNeeded(WebGLContext* webgl, const char* funcName,
                               const void* srcBytes, uint32_t srcStride, uint8_t srcBPP,
                               WebGLTexelFormat srcFormat,
                               const webgl::DriverUnpackInfo* dstDUI,
                               const void** const out_bytes,
                               UniqueBuffer* const out_anchoredBuffer) const
{
    *out_bytes = srcBytes;

    if (!HasData() || !mWidth || !mHeight || !mDepth)
        return true;

    //////

    const auto totalSkipRows = mSkipRows + CheckedUint32(mSkipImages) * mImageHeight;
    const auto offset = mSkipPixels * CheckedUint32(srcBPP) + totalSkipRows * srcStride;
    if (!offset.isValid()) {
        webgl->ErrorOutOfMemory("%s: Invalid offset calculation during conversion.",
                                funcName);
        return false;
    }
    const uint32_t skipBytes = offset.value();

    auto const srcBegin = (const uint8_t*)srcBytes + skipBytes;

    //////

    const auto srcOrigin = (webgl->mPixelStore_FlipY ? gl::OriginPos::TopLeft
                                                     : gl::OriginPos::BottomLeft);
    const auto dstOrigin = gl::OriginPos::BottomLeft;
    const bool isDstPremult = webgl->mPixelStore_PremultiplyAlpha;

    const auto pi = dstDUI->ToPacking();

    const auto dstBPP = webgl::BytesPerPixel(pi);
    const auto dstWidthBytes = CheckedUint32(dstBPP) * mWidth;
    const auto dstRowLengthBytes = CheckedUint32(dstBPP) * mRowLength;

    const auto dstAlignment = mAlignment;
    const auto dstStride = RoundUpToMultipleOf(dstRowLengthBytes, dstAlignment);

    //////

    const auto dstTotalRows = CheckedUint32(mDepth - 1) * mImageHeight + mHeight;
    const auto dstUsedSizeExceptLastRow = (dstTotalRows - 1) * dstStride;

    const auto dstSize = skipBytes + dstUsedSizeExceptLastRow + dstWidthBytes;
    if (!dstSize.isValid()) {
        webgl->ErrorOutOfMemory("%s: Invalid dstSize calculation during conversion.",
                                funcName);
        return false;
    }

    //////

    const auto dstFormat = FormatForPackingInfo(pi);

    bool premultMatches = (mIsSrcPremult == isDstPremult);
    if (!UnpackFormatHasColorAndAlpha(dstDUI->unpackFormat)) {
        premultMatches = true;
    }

    const bool needsPixelConversion = (srcFormat != dstFormat || !premultMatches);
    const bool originsMatch = (srcOrigin == dstOrigin);

    MOZ_ASSERT_IF(!needsPixelConversion, srcBPP == dstBPP);

    if (!needsPixelConversion &&
        originsMatch &&
        srcStride == dstStride.value())
    {
        // No conversion needed!
        return true;
    }

    //////
    // We need some sort of conversion, so create the dest buffer.

    *out_anchoredBuffer = calloc(1, dstSize.value());
    *out_bytes = out_anchoredBuffer->get();
    if (!*out_bytes) {
        webgl->ErrorOutOfMemory("%s: Unable to allocate buffer during conversion.",
                                funcName);
        return false;
    }
    const auto dstBegin = (uint8_t*)(*out_bytes) + skipBytes;

    //////
    // Row conversion

    if (!needsPixelConversion) {
        webgl->GenerateWarning("%s: Incurred CPU row conversion, which is slow.",
                               funcName);

        const uint8_t* srcRow = srcBegin;
        uint8_t* dstRow = dstBegin;
        const auto widthBytes = dstWidthBytes.value();
        ptrdiff_t dstCopyStride = dstStride.value();

        if (!originsMatch) {
            dstRow += dstUsedSizeExceptLastRow.value();
            dstCopyStride = -dstCopyStride;
        }

        for (uint32_t i = 0; i < dstTotalRows.value(); i++) {
            memcpy(dstRow, srcRow, widthBytes);
            srcRow += srcStride;
            dstRow += dstCopyStride;
        }
        return true;
    }

    ////////////
    // Pixel conversion.

    MOZ_ASSERT(srcFormat != WebGLTexelFormat::FormatNotSupportingAnyConversion);
    MOZ_ASSERT(dstFormat != WebGLTexelFormat::FormatNotSupportingAnyConversion);

    webgl->GenerateWarning("%s: Incurred CPU pixel conversion, which is very slow.",
                           funcName);

    //////

    // And go!:
    bool wasTrivial;
    if (!ConvertImage(mWidth, dstTotalRows.value(),
                      srcBegin, srcStride, srcOrigin, srcFormat, mIsSrcPremult,
                      dstBegin, dstStride.value(), dstOrigin, dstFormat, isDstPremult,
                      &wasTrivial))
    {
        webgl->ErrorImplementationBug("%s: ConvertImage failed.", funcName);
        return false;
    }

    if (!wasTrivial) {
        webgl->GenerateWarning("%s: Chosen format/type incurred an expensive reformat:"
                               " 0x%04x/0x%04x",
                               funcName, dstDUI->unpackFormat, dstDUI->unpackType);
    }

    return true;
}

static GLenum
DoTexOrSubImage(bool isSubImage, gl::GLContext* gl, TexImageTarget target, GLint level,
                const DriverUnpackInfo* dui, GLint xOffset, GLint yOffset, GLint zOffset,
                GLsizei width, GLsizei height, GLsizei depth, const void* data)
{
    if (isSubImage) {
        return DoTexSubImage(gl, target, level, xOffset, yOffset, zOffset, width, height,
                             depth, dui->ToPacking(), data);
    } else {
        return DoTexImage(gl, target, level, dui, width, height, depth, data);
    }
}

//////////////////////////////////////////////////////////////////////////////////////////
// TexUnpackBytes

TexUnpackBytes::TexUnpackBytes(const WebGLContext* webgl, TexImageTarget target,
                               uint32_t width, uint32_t height, uint32_t depth,
                               const void* bytes)
    : TexUnpackBlob(webgl, target,
                    FallbackOnZero(webgl->mPixelStore_UnpackRowLength, width), width,
                    height, depth, false)
    , mBytes(bytes)
{ }

bool
TexUnpackBytes::TexOrSubImage(bool isSubImage, bool needsRespec, const char* funcName,
                              WebGLTexture* tex, TexImageTarget target, GLint level,
                              const webgl::DriverUnpackInfo* dui, GLint xOffset,
                              GLint yOffset, GLint zOffset, GLenum* const out_error) const
{
    WebGLContext* webgl = tex->mContext;

    const auto pi = dui->ToPacking();

    const auto bytesPerPixel = webgl::BytesPerPixel(pi);
    const auto bytesPerRow = CheckedUint32(mRowLength) * bytesPerPixel;
    const auto rowStride = RoundUpToMultipleOf(bytesPerRow, mAlignment);
    if (!rowStride.isValid()) {
        MOZ_CRASH("Should be checked earlier.");
    }

    const auto format = FormatForPackingInfo(pi);

    const void* uploadBytes;
    UniqueBuffer tempBuffer;
    if (!ConvertIfNeeded(webgl, funcName, mBytes, rowStride.value(), bytesPerPixel,
                         format, dui, &uploadBytes, &tempBuffer))
    {
        return false;
    }

    *out_error = DoTexOrSubImage(isSubImage, webgl->gl, target, level, dui, xOffset,
                                 yOffset, zOffset, mWidth, mHeight, mDepth, uploadBytes);
    return true;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
// TexUnpackImage

TexUnpackImage::TexUnpackImage(const WebGLContext* webgl, TexImageTarget target,
                               uint32_t width, uint32_t height, uint32_t depth,
                               layers::Image* image, bool isAlphaPremult)
    : TexUnpackBlob(webgl, target, image->GetSize().width, width, height, depth,
                    isAlphaPremult)
    , mImage(image)
{ }

bool
TexUnpackImage::TexOrSubImage(bool isSubImage, bool needsRespec, const char* funcName,
                              WebGLTexture* tex, TexImageTarget target, GLint level,
                              const webgl::DriverUnpackInfo* dui, GLint xOffset,
                              GLint yOffset, GLint zOffset, GLenum* const out_error) const
{
    MOZ_ASSERT_IF(needsRespec, !isSubImage);

    WebGLContext* webgl = tex->mContext;

    gl::GLContext* gl = webgl->GL();
    gl->MakeCurrent();

    if (needsRespec) {
        *out_error = DoTexOrSubImage(isSubImage, gl, target.get(), level, dui, xOffset,
                                     yOffset, zOffset, mWidth, mHeight, mDepth,
                                     nullptr);
        if (*out_error)
            return false;
    }

    do {
        if (mDepth != 1)
            break;

        const bool isDstPremult = webgl->mPixelStore_PremultiplyAlpha;
        if (mIsSrcPremult != isDstPremult)
            break;

        if (dui->unpackFormat != LOCAL_GL_RGB && dui->unpackFormat != LOCAL_GL_RGBA)
            break;

        if (dui->unpackType != LOCAL_GL_UNSIGNED_BYTE)
            break;

        gl::ScopedFramebuffer scopedFB(gl);
        gl::ScopedBindFramebuffer bindFB(gl, scopedFB.FB());

        {
            gl::GLContext::LocalErrorScope errorScope(*gl);

            gl->fFramebufferTexture2D(LOCAL_GL_FRAMEBUFFER, LOCAL_GL_COLOR_ATTACHMENT0,
                                      target.get(), tex->mGLName, level);

            if (errorScope.GetError())
                break;
        }

        const GLenum status = gl->fCheckFramebufferStatus(LOCAL_GL_FRAMEBUFFER);
        if (status != LOCAL_GL_FRAMEBUFFER_COMPLETE)
            break;

        const gfx::IntSize destSize(mWidth, mHeight);
        const auto dstOrigin = (webgl->mPixelStore_FlipY ? gl::OriginPos::TopLeft
                                                         : gl::OriginPos::BottomLeft);
        if (!gl->BlitHelper()->BlitImageToFramebuffer(mImage, destSize, scopedFB.FB(),
                                                      dstOrigin))
        {
            break;
        }

        // Blitting was successful, so we're done!
        *out_error = 0;
        return true;
    } while (false);

    webgl->GenerateWarning("%s: Failed to hit GPU-copy fast-path. Falling back to CPU"
                           " upload.",
                           funcName);

    const RefPtr<SourceSurface> surf = mImage->GetAsSourceSurface();

    RefPtr<DataSourceSurface> dataSurf;
    if (surf) {
        // WARNING: OSX can lose our MakeCurrent here.
        dataSurf = surf->GetDataSurface();
    }
    if (!dataSurf) {
        webgl->ErrorOutOfMemory("%s: GetAsSourceSurface or GetDataSurface failed after"
                                " blit failed for TexUnpackImage.",
                                funcName);
        return false;
    }

    const TexUnpackSurface surfBlob(webgl, target, mWidth, mHeight, mDepth, dataSurf,
                                    mIsSrcPremult);

    return surfBlob.TexOrSubImage(isSubImage, needsRespec, funcName, tex, target, level,
                                  dui, xOffset, yOffset, zOffset, out_error);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
// TexUnpackSurface

TexUnpackSurface::TexUnpackSurface(const WebGLContext* webgl, TexImageTarget target,
                                   uint32_t width, uint32_t height, uint32_t depth,
                                   gfx::DataSourceSurface* surf, bool isAlphaPremult)
    : TexUnpackBlob(webgl, target, surf->GetSize().width, width, height, depth,
                    isAlphaPremult)
    , mSurf(surf)
{ }

//////////

static bool
GetFormatForSurf(gfx::SourceSurface* surf, WebGLTexelFormat* const out_texelFormat,
                 uint8_t* const out_bpp)
{
    const auto surfFormat = surf->GetFormat();
    switch (surfFormat) {
    case gfx::SurfaceFormat::B8G8R8A8:
        *out_texelFormat = WebGLTexelFormat::BGRA8;
        *out_bpp = 4;
        return true;

    case gfx::SurfaceFormat::B8G8R8X8:
        *out_texelFormat = WebGLTexelFormat::BGRX8;
        *out_bpp = 4;
        return true;

    case gfx::SurfaceFormat::R8G8B8A8:
        *out_texelFormat = WebGLTexelFormat::RGBA8;
        *out_bpp = 4;
        return true;

    case gfx::SurfaceFormat::R8G8B8X8:
        *out_texelFormat = WebGLTexelFormat::RGBX8;
        *out_bpp = 4;
        return true;

    case gfx::SurfaceFormat::R5G6B5_UINT16:
        *out_texelFormat = WebGLTexelFormat::RGB565;
        *out_bpp = 2;
        return true;

    case gfx::SurfaceFormat::A8:
        *out_texelFormat = WebGLTexelFormat::A8;
        *out_bpp = 1;
        return true;

    case gfx::SurfaceFormat::YUV:
        // Ugh...
        NS_ERROR("We don't handle uploads from YUV sources yet.");
        // When we want to, check out gfx/ycbcr/YCbCrUtils.h. (specifically
        // GetYCbCrToRGBDestFormatAndSize and ConvertYCbCrToRGB)
        return false;

    default:
        return false;
    }
}

//////////

bool
TexUnpackSurface::TexOrSubImage(bool isSubImage, bool needsRespec, const char* funcName,
                                WebGLTexture* tex, TexImageTarget target, GLint level,
                                const webgl::DriverUnpackInfo* dstDUI, GLint xOffset,
                                GLint yOffset, GLint zOffset,
                                GLenum* const out_error) const
{
    WebGLContext* webgl = tex->mContext;

    WebGLTexelFormat srcFormat;
    uint8_t srcBPP;
    if (!GetFormatForSurf(mSurf, &srcFormat, &srcBPP)) {
        webgl->ErrorImplementationBug("%s: GetFormatForSurf failed for"
                                      " WebGLTexelFormat::%u.",
                                      funcName, uint32_t(mSurf->GetFormat()));
        return false;
    }

    gfx::DataSourceSurface::ScopedMap map(mSurf, gfx::DataSourceSurface::MapType::READ);
    if (!map.IsMapped())
        return false;

    const auto srcBytes = map.GetData();
    const auto srcStride = map.GetStride();

    // CPU conversion. (++numCopies)

    webgl->GenerateWarning("%s: Incurred CPU-side conversion, which is very slow.",
                           funcName);

    const void* uploadBytes;
    UniqueBuffer tempBuffer;
    if (!ConvertIfNeeded(webgl, funcName, srcBytes, srcStride, srcBPP, srcFormat,
                         dstDUI, &uploadBytes, &tempBuffer))
    {
        return false;
    }

    //////

    gl::GLContext* const gl = webgl->gl;
    MOZ_ALWAYS_TRUE( gl->MakeCurrent() );

    const auto curEffectiveRowLength = FallbackOnZero(webgl->mPixelStore_UnpackRowLength,
                                                      mWidth);

    const bool changeRowLength = (mRowLength != curEffectiveRowLength);
    if (changeRowLength) {
        MOZ_ASSERT(webgl->IsWebGL2());
        gl->fPixelStorei(LOCAL_GL_UNPACK_ROW_LENGTH, mRowLength);
    }

    *out_error = DoTexOrSubImage(isSubImage, gl, target.get(), level, dstDUI, xOffset,
                                 yOffset, zOffset, mWidth, mHeight, mDepth, uploadBytes);

    if (changeRowLength) {
        gl->fPixelStorei(LOCAL_GL_UNPACK_ROW_LENGTH, webgl->mPixelStore_UnpackRowLength);
    }

    return true;
}

} // namespace webgl
} // namespace mozilla

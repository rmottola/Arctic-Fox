//
// Copyright (c) 2013-2014 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

// validationES3.cpp: Validation functions for OpenGL ES 3.0 entry point parameters

#include "libANGLE/validationES3.h"

#include "libANGLE/validationES.h"
#include "libANGLE/Context.h"
#include "libANGLE/Texture.h"
#include "libANGLE/Framebuffer.h"
#include "libANGLE/Renderbuffer.h"
#include "libANGLE/formatutils.h"
#include "libANGLE/FramebufferAttachment.h"

#include "common/mathutil.h"
#include "common/utilities.h"

using namespace angle;

namespace gl
{

struct ES3FormatCombination
{
    GLenum internalFormat;
    GLenum format;
    GLenum type;
};

bool operator<(const ES3FormatCombination& a, const ES3FormatCombination& b)
{
    return memcmp(&a, &b, sizeof(ES3FormatCombination)) < 0;
}

typedef std::set<ES3FormatCombination> ES3FormatCombinationSet;

static inline void InsertES3FormatCombo(ES3FormatCombinationSet *set, GLenum internalFormat, GLenum format, GLenum type)
{
    ES3FormatCombination info;
    info.internalFormat = internalFormat;
    info.format = format;
    info.type = type;
    set->insert(info);
}

ES3FormatCombinationSet BuildES3FormatSet()
{
    ES3FormatCombinationSet set;

    // Format combinations from ES 3.0.1 spec, table 3.2

    //                        | Internal format      | Format            | Type                            |
    //                        |                      |                   |                                 |
    InsertES3FormatCombo(&set, GL_RGBA8,              GL_RGBA,            GL_UNSIGNED_BYTE                 );
    InsertES3FormatCombo(&set, GL_RGB5_A1,            GL_RGBA,            GL_UNSIGNED_BYTE                 );
    InsertES3FormatCombo(&set, GL_RGBA4,              GL_RGBA,            GL_UNSIGNED_BYTE                 );
    InsertES3FormatCombo(&set, GL_SRGB8_ALPHA8,       GL_RGBA,            GL_UNSIGNED_BYTE                 );
    InsertES3FormatCombo(&set, GL_RGBA8_SNORM,        GL_RGBA,            GL_BYTE                          );
    InsertES3FormatCombo(&set, GL_RGBA4,              GL_RGBA,            GL_UNSIGNED_SHORT_4_4_4_4        );
    InsertES3FormatCombo(&set, GL_RGB10_A2,           GL_RGBA,            GL_UNSIGNED_INT_2_10_10_10_REV   );
    InsertES3FormatCombo(&set, GL_RGB5_A1,            GL_RGBA,            GL_UNSIGNED_INT_2_10_10_10_REV   );
    InsertES3FormatCombo(&set, GL_RGB5_A1,            GL_RGBA,            GL_UNSIGNED_SHORT_5_5_5_1        );
    InsertES3FormatCombo(&set, GL_RGBA16F,            GL_RGBA,            GL_HALF_FLOAT                    );
    InsertES3FormatCombo(&set, GL_RGBA16F,            GL_RGBA,            GL_HALF_FLOAT_OES                );
    InsertES3FormatCombo(&set, GL_RGBA32F,            GL_RGBA,            GL_FLOAT                         );
    InsertES3FormatCombo(&set, GL_RGBA16F,            GL_RGBA,            GL_FLOAT                         );
    InsertES3FormatCombo(&set, GL_RGBA8UI,            GL_RGBA_INTEGER,    GL_UNSIGNED_BYTE                 );
    InsertES3FormatCombo(&set, GL_RGBA8I,             GL_RGBA_INTEGER,    GL_BYTE                          );
    InsertES3FormatCombo(&set, GL_RGBA16UI,           GL_RGBA_INTEGER,    GL_UNSIGNED_SHORT                );
    InsertES3FormatCombo(&set, GL_RGBA16I,            GL_RGBA_INTEGER,    GL_SHORT                         );
    InsertES3FormatCombo(&set, GL_RGBA32UI,           GL_RGBA_INTEGER,    GL_UNSIGNED_INT                  );
    InsertES3FormatCombo(&set, GL_RGBA32I,            GL_RGBA_INTEGER,    GL_INT                           );
    InsertES3FormatCombo(&set, GL_RGB10_A2UI,         GL_RGBA_INTEGER,    GL_UNSIGNED_INT_2_10_10_10_REV   );
    InsertES3FormatCombo(&set, GL_RGB8,               GL_RGB,             GL_UNSIGNED_BYTE                 );
    InsertES3FormatCombo(&set, GL_RGB565,             GL_RGB,             GL_UNSIGNED_BYTE                 );
    InsertES3FormatCombo(&set, GL_SRGB8,              GL_RGB,             GL_UNSIGNED_BYTE                 );
    InsertES3FormatCombo(&set, GL_RGB8_SNORM,         GL_RGB,             GL_BYTE                          );
    InsertES3FormatCombo(&set, GL_RGB565,             GL_RGB,             GL_UNSIGNED_SHORT_5_6_5          );
    InsertES3FormatCombo(&set, GL_R11F_G11F_B10F,     GL_RGB,             GL_UNSIGNED_INT_10F_11F_11F_REV  );
    InsertES3FormatCombo(&set, GL_RGB9_E5,            GL_RGB,             GL_UNSIGNED_INT_5_9_9_9_REV      );
    InsertES3FormatCombo(&set, GL_RGB16F,             GL_RGB,             GL_HALF_FLOAT                    );
    InsertES3FormatCombo(&set, GL_RGB16F,             GL_RGB,             GL_HALF_FLOAT_OES                );
    InsertES3FormatCombo(&set, GL_R11F_G11F_B10F,     GL_RGB,             GL_HALF_FLOAT                    );
    InsertES3FormatCombo(&set, GL_R11F_G11F_B10F,     GL_RGB,             GL_HALF_FLOAT_OES                );
    InsertES3FormatCombo(&set, GL_RGB9_E5,            GL_RGB,             GL_HALF_FLOAT                    );
    InsertES3FormatCombo(&set, GL_RGB9_E5,            GL_RGB,             GL_HALF_FLOAT_OES                );
    InsertES3FormatCombo(&set, GL_RGB32F,             GL_RGB,             GL_FLOAT                         );
    InsertES3FormatCombo(&set, GL_RGB16F,             GL_RGB,             GL_FLOAT                         );
    InsertES3FormatCombo(&set, GL_R11F_G11F_B10F,     GL_RGB,             GL_FLOAT                         );
    InsertES3FormatCombo(&set, GL_RGB9_E5,            GL_RGB,             GL_FLOAT                         );
    InsertES3FormatCombo(&set, GL_RGB8UI,             GL_RGB_INTEGER,     GL_UNSIGNED_BYTE                 );
    InsertES3FormatCombo(&set, GL_RGB8I,              GL_RGB_INTEGER,     GL_BYTE                          );
    InsertES3FormatCombo(&set, GL_RGB16UI,            GL_RGB_INTEGER,     GL_UNSIGNED_SHORT                );
    InsertES3FormatCombo(&set, GL_RGB16I,             GL_RGB_INTEGER,     GL_SHORT                         );
    InsertES3FormatCombo(&set, GL_RGB32UI,            GL_RGB_INTEGER,     GL_UNSIGNED_INT                  );
    InsertES3FormatCombo(&set, GL_RGB32I,             GL_RGB_INTEGER,     GL_INT                           );
    InsertES3FormatCombo(&set, GL_RG8,                GL_RG,              GL_UNSIGNED_BYTE                 );
    InsertES3FormatCombo(&set, GL_RG8_SNORM,          GL_RG,              GL_BYTE                          );
    InsertES3FormatCombo(&set, GL_RG16F,              GL_RG,              GL_HALF_FLOAT                    );
    InsertES3FormatCombo(&set, GL_RG16F,              GL_RG,              GL_HALF_FLOAT_OES                );
    InsertES3FormatCombo(&set, GL_RG32F,              GL_RG,              GL_FLOAT                         );
    InsertES3FormatCombo(&set, GL_RG16F,              GL_RG,              GL_FLOAT                         );
    InsertES3FormatCombo(&set, GL_RG8UI,              GL_RG_INTEGER,      GL_UNSIGNED_BYTE                 );
    InsertES3FormatCombo(&set, GL_RG8I,               GL_RG_INTEGER,      GL_BYTE                          );
    InsertES3FormatCombo(&set, GL_RG16UI,             GL_RG_INTEGER,      GL_UNSIGNED_SHORT                );
    InsertES3FormatCombo(&set, GL_RG16I,              GL_RG_INTEGER,      GL_SHORT                         );
    InsertES3FormatCombo(&set, GL_RG32UI,             GL_RG_INTEGER,      GL_UNSIGNED_INT                  );
    InsertES3FormatCombo(&set, GL_RG32I,              GL_RG_INTEGER,      GL_INT                           );
    InsertES3FormatCombo(&set, GL_R8,                 GL_RED,             GL_UNSIGNED_BYTE                 );
    InsertES3FormatCombo(&set, GL_R8_SNORM,           GL_RED,             GL_BYTE                          );
    InsertES3FormatCombo(&set, GL_R16F,               GL_RED,             GL_HALF_FLOAT                    );
    InsertES3FormatCombo(&set, GL_R16F,               GL_RED,             GL_HALF_FLOAT_OES                );
    InsertES3FormatCombo(&set, GL_R32F,               GL_RED,             GL_FLOAT                         );
    InsertES3FormatCombo(&set, GL_R16F,               GL_RED,             GL_FLOAT                         );
    InsertES3FormatCombo(&set, GL_R8UI,               GL_RED_INTEGER,     GL_UNSIGNED_BYTE                 );
    InsertES3FormatCombo(&set, GL_R8I,                GL_RED_INTEGER,     GL_BYTE                          );
    InsertES3FormatCombo(&set, GL_R16UI,              GL_RED_INTEGER,     GL_UNSIGNED_SHORT                );
    InsertES3FormatCombo(&set, GL_R16I,               GL_RED_INTEGER,     GL_SHORT                         );
    InsertES3FormatCombo(&set, GL_R32UI,              GL_RED_INTEGER,     GL_UNSIGNED_INT                  );
    InsertES3FormatCombo(&set, GL_R32I,               GL_RED_INTEGER,     GL_INT                           );

    // Unsized formats
    InsertES3FormatCombo(&set, GL_RGBA,               GL_RGBA,            GL_UNSIGNED_BYTE                 );
    InsertES3FormatCombo(&set, GL_RGBA,               GL_RGBA,            GL_UNSIGNED_SHORT_4_4_4_4        );
    InsertES3FormatCombo(&set, GL_RGBA,               GL_RGBA,            GL_UNSIGNED_SHORT_5_5_5_1        );
    InsertES3FormatCombo(&set, GL_RGB,                GL_RGB,             GL_UNSIGNED_BYTE                 );
    InsertES3FormatCombo(&set, GL_RGB,                GL_RGB,             GL_UNSIGNED_SHORT_5_6_5          );
    InsertES3FormatCombo(&set, GL_LUMINANCE_ALPHA,    GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE                 );
    InsertES3FormatCombo(&set, GL_LUMINANCE,          GL_LUMINANCE,       GL_UNSIGNED_BYTE                 );
    InsertES3FormatCombo(&set, GL_ALPHA,              GL_ALPHA,           GL_UNSIGNED_BYTE                 );
    InsertES3FormatCombo(&set, GL_SRGB_ALPHA_EXT,     GL_SRGB_ALPHA_EXT,  GL_UNSIGNED_BYTE                 );
    InsertES3FormatCombo(&set, GL_SRGB_EXT,           GL_SRGB_EXT,        GL_UNSIGNED_BYTE                 );
    InsertES3FormatCombo(&set, GL_RG,                 GL_RG,              GL_UNSIGNED_BYTE                 );
    InsertES3FormatCombo(&set, GL_RG,                 GL_RG,              GL_FLOAT                         );
    InsertES3FormatCombo(&set, GL_RG,                 GL_RG,              GL_HALF_FLOAT                    );
    InsertES3FormatCombo(&set, GL_RG,                 GL_RG,              GL_HALF_FLOAT_OES                );
    InsertES3FormatCombo(&set, GL_RED,                GL_RED,             GL_UNSIGNED_BYTE                 );
    InsertES3FormatCombo(&set, GL_RED,                GL_RED,             GL_FLOAT                         );
    InsertES3FormatCombo(&set, GL_RED,                GL_RED,             GL_HALF_FLOAT                    );
    InsertES3FormatCombo(&set, GL_RED,                GL_RED,             GL_HALF_FLOAT_OES                );
    InsertES3FormatCombo(&set, GL_DEPTH_STENCIL,      GL_DEPTH_STENCIL,   GL_UNSIGNED_INT_24_8             );

    // Depth stencil formats
    InsertES3FormatCombo(&set, GL_DEPTH_COMPONENT16,  GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT                );
    InsertES3FormatCombo(&set, GL_DEPTH_COMPONENT24,  GL_DEPTH_COMPONENT, GL_UNSIGNED_INT                  );
    InsertES3FormatCombo(&set, GL_DEPTH_COMPONENT16,  GL_DEPTH_COMPONENT, GL_UNSIGNED_INT                  );
    InsertES3FormatCombo(&set, GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT                         );
    InsertES3FormatCombo(&set, GL_DEPTH24_STENCIL8,   GL_DEPTH_STENCIL,   GL_UNSIGNED_INT_24_8             );
    InsertES3FormatCombo(&set, GL_DEPTH32F_STENCIL8,  GL_DEPTH_STENCIL,   GL_FLOAT_32_UNSIGNED_INT_24_8_REV);

    // From GL_EXT_sRGB
    InsertES3FormatCombo(&set, GL_SRGB8_ALPHA8_EXT,   GL_SRGB_ALPHA_EXT, GL_UNSIGNED_BYTE                  );
    InsertES3FormatCombo(&set, GL_SRGB8,              GL_SRGB_EXT,       GL_UNSIGNED_BYTE                  );

    // From GL_OES_texture_float
    InsertES3FormatCombo(&set, GL_RGBA,               GL_RGBA,            GL_FLOAT                         );
    InsertES3FormatCombo(&set, GL_RGB,                GL_RGB,             GL_FLOAT                         );
    InsertES3FormatCombo(&set, GL_LUMINANCE_ALPHA,    GL_LUMINANCE_ALPHA, GL_FLOAT                         );
    InsertES3FormatCombo(&set, GL_LUMINANCE,          GL_LUMINANCE,       GL_FLOAT                         );
    InsertES3FormatCombo(&set, GL_ALPHA,              GL_ALPHA,           GL_FLOAT                         );

    // From GL_OES_texture_half_float
    InsertES3FormatCombo(&set, GL_LUMINANCE_ALPHA,    GL_LUMINANCE_ALPHA, GL_HALF_FLOAT                    );
    InsertES3FormatCombo(&set, GL_LUMINANCE_ALPHA,    GL_LUMINANCE_ALPHA, GL_HALF_FLOAT_OES                );
    InsertES3FormatCombo(&set, GL_LUMINANCE,          GL_LUMINANCE,       GL_HALF_FLOAT                    );
    InsertES3FormatCombo(&set, GL_LUMINANCE,          GL_LUMINANCE,       GL_HALF_FLOAT_OES                );
    InsertES3FormatCombo(&set, GL_ALPHA,              GL_ALPHA,           GL_HALF_FLOAT                    );
    InsertES3FormatCombo(&set, GL_ALPHA,              GL_ALPHA,           GL_HALF_FLOAT_OES                );

    // From GL_EXT_texture_format_BGRA8888
    InsertES3FormatCombo(&set, GL_BGRA_EXT,           GL_BGRA_EXT,        GL_UNSIGNED_BYTE                 );

    // From GL_EXT_texture_storage
    //                    | Internal format          | Format            | Type                            |
    //                    |                          |                   |                                 |
    InsertES3FormatCombo(&set, GL_ALPHA8_EXT,             GL_ALPHA,           GL_UNSIGNED_BYTE                 );
    InsertES3FormatCombo(&set, GL_LUMINANCE8_EXT,         GL_LUMINANCE,       GL_UNSIGNED_BYTE                 );
    InsertES3FormatCombo(&set, GL_LUMINANCE8_ALPHA8_EXT,  GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE                 );
    InsertES3FormatCombo(&set, GL_ALPHA32F_EXT,           GL_ALPHA,           GL_FLOAT                         );
    InsertES3FormatCombo(&set, GL_LUMINANCE32F_EXT,       GL_LUMINANCE,       GL_FLOAT                         );
    InsertES3FormatCombo(&set, GL_LUMINANCE_ALPHA32F_EXT, GL_LUMINANCE_ALPHA, GL_FLOAT                         );
    InsertES3FormatCombo(&set, GL_ALPHA16F_EXT,           GL_ALPHA,           GL_HALF_FLOAT                    );
    InsertES3FormatCombo(&set, GL_ALPHA16F_EXT,           GL_ALPHA,           GL_HALF_FLOAT_OES                );
    InsertES3FormatCombo(&set, GL_LUMINANCE16F_EXT,       GL_LUMINANCE,       GL_HALF_FLOAT                    );
    InsertES3FormatCombo(&set, GL_LUMINANCE16F_EXT,       GL_LUMINANCE,       GL_HALF_FLOAT_OES                );
    InsertES3FormatCombo(&set, GL_LUMINANCE_ALPHA16F_EXT, GL_LUMINANCE_ALPHA, GL_HALF_FLOAT                    );
    InsertES3FormatCombo(&set, GL_LUMINANCE_ALPHA16F_EXT, GL_LUMINANCE_ALPHA, GL_HALF_FLOAT_OES                );

    // From GL_EXT_texture_storage and GL_EXT_texture_format_BGRA8888
    InsertES3FormatCombo(&set, GL_BGRA8_EXT,              GL_BGRA_EXT,        GL_UNSIGNED_BYTE                 );
    InsertES3FormatCombo(&set, GL_BGRA4_ANGLEX,           GL_BGRA_EXT,        GL_UNSIGNED_SHORT_4_4_4_4_REV_EXT);
    InsertES3FormatCombo(&set, GL_BGRA4_ANGLEX,           GL_BGRA_EXT,        GL_UNSIGNED_BYTE                 );
    InsertES3FormatCombo(&set, GL_BGR5_A1_ANGLEX,         GL_BGRA_EXT,        GL_UNSIGNED_SHORT_1_5_5_5_REV_EXT);
    InsertES3FormatCombo(&set, GL_BGR5_A1_ANGLEX,         GL_BGRA_EXT,        GL_UNSIGNED_BYTE                 );

    // From GL_ANGLE_depth_texture
    InsertES3FormatCombo(&set, GL_DEPTH_COMPONENT32_OES,  GL_DEPTH_COMPONENT, GL_UNSIGNED_INT_24_8_OES         );

    // From GL_EXT_texture_norm16
    InsertES3FormatCombo(&set, GL_R16_EXT, GL_RED, GL_UNSIGNED_SHORT);
    InsertES3FormatCombo(&set, GL_RG16_EXT, GL_RG, GL_UNSIGNED_SHORT);
    InsertES3FormatCombo(&set, GL_RGB16_EXT, GL_RGB, GL_UNSIGNED_SHORT);
    InsertES3FormatCombo(&set, GL_RGBA16_EXT, GL_RGBA, GL_UNSIGNED_SHORT);
    InsertES3FormatCombo(&set, GL_R16_SNORM_EXT, GL_RED, GL_SHORT);
    InsertES3FormatCombo(&set, GL_RG16_SNORM_EXT, GL_RG, GL_SHORT);
    InsertES3FormatCombo(&set, GL_RGB16_SNORM_EXT, GL_RGB, GL_SHORT);
    InsertES3FormatCombo(&set, GL_RGBA16_SNORM_EXT, GL_RGBA, GL_SHORT);

    return set;
}

static bool ValidateTexImageFormatCombination(gl::Context *context, GLenum internalFormat, GLenum format, GLenum type)
{
    // For historical reasons, glTexImage2D and glTexImage3D pass in their internal format as a
    // GLint instead of a GLenum. Therefor an invalid internal format gives a GL_INVALID_VALUE
    // error instead of a GL_INVALID_ENUM error. As this validation function is only called in
    // the validation codepaths for glTexImage2D/3D, we record a GL_INVALID_VALUE error.
    const gl::InternalFormat &formatInfo = gl::GetInternalFormatInfo(internalFormat);
    if (!formatInfo.textureSupport(context->getClientVersion(), context->getExtensions()))
    {
        context->handleError(Error(GL_INVALID_VALUE));
        return false;
    }

    // The type and format are valid if any supported internal format has that type and format
    bool formatSupported = false;
    bool typeSupported = false;

    static const ES3FormatCombinationSet es3FormatSet = BuildES3FormatSet();
    for (ES3FormatCombinationSet::const_iterator i = es3FormatSet.begin(); i != es3FormatSet.end(); i++)
    {
        if (i->format == format || i->type == type)
        {
            const gl::InternalFormat &info = gl::GetInternalFormatInfo(i->internalFormat);
            bool supported = info.textureSupport(context->getClientVersion(), context->getExtensions());
            if (supported && i->type == type)
            {
                typeSupported = true;
            }
            if (supported && i->format == format)
            {
                formatSupported = true;
            }

            // Early-out if both type and format are supported now
            if (typeSupported && formatSupported)
            {
                break;
            }
        }
    }

    if (!typeSupported || !formatSupported)
    {
        context->handleError(Error(GL_INVALID_ENUM));
        return false;
    }

    // Check if this is a valid format combination to load texture data
    ES3FormatCombination searchFormat;
    searchFormat.internalFormat = internalFormat;
    searchFormat.format = format;
    searchFormat.type = type;

    if (es3FormatSet.find(searchFormat) == es3FormatSet.end())
    {
        context->handleError(Error(GL_INVALID_OPERATION));
        return false;
    }

    return true;
}

bool ValidateES3TexImageParametersBase(Context *context,
                                       GLenum target,
                                       GLint level,
                                       GLenum internalformat,
                                       bool isCompressed,
                                       bool isSubImage,
                                       GLint xoffset,
                                       GLint yoffset,
                                       GLint zoffset,
                                       GLsizei width,
                                       GLsizei height,
                                       GLsizei depth,
                                       GLint border,
                                       GLenum format,
                                       GLenum type,
                                       const GLvoid *pixels)
{
    // Validate image size
    if (!ValidImageSizeParameters(context, target, level, width, height, depth, isSubImage))
    {
        context->handleError(Error(GL_INVALID_VALUE));
        return false;
    }

    // Verify zero border
    if (border != 0)
    {
        context->handleError(Error(GL_INVALID_VALUE));
        return false;
    }

    if (xoffset < 0 || yoffset < 0 || zoffset < 0 ||
        std::numeric_limits<GLsizei>::max() - xoffset < width ||
        std::numeric_limits<GLsizei>::max() - yoffset < height ||
        std::numeric_limits<GLsizei>::max() - zoffset < depth)
    {
        context->handleError(Error(GL_INVALID_VALUE));
        return false;
    }

    const gl::Caps &caps = context->getCaps();

    switch (target)
    {
      case GL_TEXTURE_2D:
        if (static_cast<GLuint>(width) > (caps.max2DTextureSize >> level) ||
            static_cast<GLuint>(height) > (caps.max2DTextureSize >> level))
        {
            context->handleError(Error(GL_INVALID_VALUE));
            return false;
        }
        break;

      case GL_TEXTURE_CUBE_MAP_POSITIVE_X:
      case GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
      case GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
      case GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
      case GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
      case GL_TEXTURE_CUBE_MAP_NEGATIVE_Z:
        if (!isSubImage && width != height)
        {
            context->handleError(Error(GL_INVALID_VALUE));
            return false;
        }

        if (static_cast<GLuint>(width) > (caps.maxCubeMapTextureSize >> level))
        {
            context->handleError(Error(GL_INVALID_VALUE));
            return false;
        }
        break;

      case GL_TEXTURE_3D:
        if (static_cast<GLuint>(width) > (caps.max3DTextureSize >> level) ||
            static_cast<GLuint>(height) > (caps.max3DTextureSize >> level) ||
            static_cast<GLuint>(depth) > (caps.max3DTextureSize >> level))
        {
            context->handleError(Error(GL_INVALID_VALUE));
            return false;
        }
        break;

      case GL_TEXTURE_2D_ARRAY:
        if (static_cast<GLuint>(width) > (caps.max2DTextureSize >> level) ||
            static_cast<GLuint>(height) > (caps.max2DTextureSize >> level) ||
            static_cast<GLuint>(depth) > caps.maxArrayTextureLayers)
        {
            context->handleError(Error(GL_INVALID_VALUE));
            return false;
        }
        break;

      default:
          context->handleError(Error(GL_INVALID_ENUM));
        return false;
    }

    gl::Texture *texture = context->getTargetTexture(IsCubeMapTextureTarget(target) ? GL_TEXTURE_CUBE_MAP : target);
    if (!texture)
    {
        context->handleError(Error(GL_INVALID_OPERATION));
        return false;
    }

    if (texture->getImmutableFormat() && !isSubImage)
    {
        context->handleError(Error(GL_INVALID_OPERATION));
        return false;
    }

    // Validate texture formats
    GLenum actualInternalFormat = isSubImage ? texture->getInternalFormat(target, level) : internalformat;
    const gl::InternalFormat &actualFormatInfo = gl::GetInternalFormatInfo(actualInternalFormat);
    if (isCompressed)
    {
        if (!actualFormatInfo.compressed)
        {
            context->handleError(Error(
                GL_INVALID_ENUM, "internalformat is not a supported compressed internal format."));
            return false;
        }

        if (!ValidCompressedImageSize(context, actualInternalFormat, width, height))
        {
            context->handleError(Error(GL_INVALID_OPERATION));
            return false;
        }

        if (!actualFormatInfo.textureSupport(context->getClientVersion(), context->getExtensions()))
        {
            context->handleError(Error(GL_INVALID_ENUM));
            return false;
        }

        if (target == GL_TEXTURE_3D)
        {
            context->handleError(Error(GL_INVALID_OPERATION));
            return false;
        }
    }
    else
    {
        if (!ValidateTexImageFormatCombination(context, actualInternalFormat, format, type))
        {
            return false;
        }

        if (target == GL_TEXTURE_3D && (format == GL_DEPTH_COMPONENT || format == GL_DEPTH_STENCIL))
        {
            context->handleError(Error(GL_INVALID_OPERATION));
            return false;
        }
    }

    // Validate sub image parameters
    if (isSubImage)
    {
        if (isCompressed != actualFormatInfo.compressed)
        {
            context->handleError(Error(GL_INVALID_OPERATION));
            return false;
        }

        if (width == 0 || height == 0 || depth == 0)
        {
            return false;
        }

        if (xoffset < 0 || yoffset < 0 || zoffset < 0)
        {
            context->handleError(Error(GL_INVALID_VALUE));
            return false;
        }

        if (std::numeric_limits<GLsizei>::max() - xoffset < width ||
            std::numeric_limits<GLsizei>::max() - yoffset < height ||
            std::numeric_limits<GLsizei>::max() - zoffset < depth)
        {
            context->handleError(Error(GL_INVALID_VALUE));
            return false;
        }

        if (static_cast<size_t>(xoffset + width) > texture->getWidth(target, level) ||
            static_cast<size_t>(yoffset + height) > texture->getHeight(target, level) ||
            static_cast<size_t>(zoffset + depth) > texture->getDepth(target, level))
        {
            context->handleError(Error(GL_INVALID_VALUE));
            return false;
        }
    }

    // Check for pixel unpack buffer related API errors
    gl::Buffer *pixelUnpackBuffer = context->getGLState().getTargetBuffer(GL_PIXEL_UNPACK_BUFFER);
    if (pixelUnpackBuffer != NULL)
    {
        // ...the data would be unpacked from the buffer object such that the memory reads required
        // would exceed the data store size.
        GLenum sizedFormat = GetSizedInternalFormat(actualInternalFormat, type);
        const gl::InternalFormat &formatInfo = gl::GetInternalFormatInfo(sizedFormat);
        const gl::Extents size(width, height, depth);
        const auto &unpack = context->getGLState().getUnpackState();

        auto copyBytesOrErr = formatInfo.computeUnpackSize(type, size, unpack);
        if (copyBytesOrErr.isError())
        {
            context->handleError(copyBytesOrErr.getError());
            return false;
        }
        CheckedNumeric<size_t> checkedCopyBytes(copyBytesOrErr.getResult());
        CheckedNumeric<size_t> checkedOffset(reinterpret_cast<size_t>(pixels));
        checkedCopyBytes += checkedOffset;

        auto rowPitchOrErr =
            formatInfo.computeRowPitch(type, width, unpack.alignment, unpack.rowLength);
        if (rowPitchOrErr.isError())
        {
            context->handleError(rowPitchOrErr.getError());
            return false;
        }
        auto depthPitchOrErr = formatInfo.computeDepthPitch(type, width, height, unpack.alignment,
                                                            unpack.rowLength, unpack.imageHeight);
        if (depthPitchOrErr.isError())
        {
            context->handleError(depthPitchOrErr.getError());
            return false;
        }

        bool targetIs3D     = target == GL_TEXTURE_3D || target == GL_TEXTURE_2D_ARRAY;
        auto skipBytesOrErr = formatInfo.computeSkipBytes(
            rowPitchOrErr.getResult(), depthPitchOrErr.getResult(), unpack.skipImages,
            unpack.skipRows, unpack.skipPixels, targetIs3D);
        if (skipBytesOrErr.isError())
        {
            context->handleError(skipBytesOrErr.getError());
            return false;
        }
        checkedCopyBytes += skipBytesOrErr.getResult();

        if (!checkedCopyBytes.IsValid() ||
            (checkedCopyBytes.ValueOrDie() > static_cast<size_t>(pixelUnpackBuffer->getSize())))
        {
            // Overflow past the end of the buffer
            context->handleError(Error(GL_INVALID_OPERATION));
            return false;
        }

        // ...data is not evenly divisible into the number of bytes needed to store in memory a datum
        // indicated by type.
        if (!isCompressed)
        {
            size_t dataBytesPerPixel = static_cast<size_t>(gl::GetTypeInfo(type).bytes);

            if ((checkedOffset.ValueOrDie() % dataBytesPerPixel) != 0)
            {
                context->handleError(Error(GL_INVALID_OPERATION));
                return false;
            }
        }

        // ...the buffer object's data store is currently mapped.
        if (pixelUnpackBuffer->isMapped())
        {
            context->handleError(Error(GL_INVALID_OPERATION));
            return false;
        }
    }

    return true;
}

bool ValidateES3TexImage2DParameters(Context *context,
                                     GLenum target,
                                     GLint level,
                                     GLenum internalformat,
                                     bool isCompressed,
                                     bool isSubImage,
                                     GLint xoffset,
                                     GLint yoffset,
                                     GLint zoffset,
                                     GLsizei width,
                                     GLsizei height,
                                     GLsizei depth,
                                     GLint border,
                                     GLenum format,
                                     GLenum type,
                                     const GLvoid *pixels)
{
    if (!ValidTexture2DDestinationTarget(context, target))
    {
        context->handleError(Error(GL_INVALID_ENUM));
        return false;
    }

    return ValidateES3TexImageParametersBase(context, target, level, internalformat, isCompressed,
                                             isSubImage, xoffset, yoffset, zoffset, width, height,
                                             depth, border, format, type, pixels);
}

bool ValidateES3TexImage3DParameters(Context *context,
                                     GLenum target,
                                     GLint level,
                                     GLenum internalformat,
                                     bool isCompressed,
                                     bool isSubImage,
                                     GLint xoffset,
                                     GLint yoffset,
                                     GLint zoffset,
                                     GLsizei width,
                                     GLsizei height,
                                     GLsizei depth,
                                     GLint border,
                                     GLenum format,
                                     GLenum type,
                                     const GLvoid *pixels)
{
    if (!ValidTexture3DDestinationTarget(context, target))
    {
        context->handleError(Error(GL_INVALID_ENUM));
        return false;
    }

    return ValidateES3TexImageParametersBase(context, target, level, internalformat, isCompressed,
                                             isSubImage, xoffset, yoffset, zoffset, width, height,
                                             depth, border, format, type, pixels);
}

struct EffectiveInternalFormatInfo
{
    GLenum mEffectiveFormat;
    GLenum mDestFormat;
    GLuint mMinRedBits;
    GLuint mMaxRedBits;
    GLuint mMinGreenBits;
    GLuint mMaxGreenBits;
    GLuint mMinBlueBits;
    GLuint mMaxBlueBits;
    GLuint mMinAlphaBits;
    GLuint mMaxAlphaBits;

    EffectiveInternalFormatInfo(GLenum effectiveFormat, GLenum destFormat, GLuint minRedBits, GLuint maxRedBits,
                                GLuint minGreenBits, GLuint maxGreenBits, GLuint minBlueBits, GLuint maxBlueBits,
                                GLuint minAlphaBits, GLuint maxAlphaBits)
        : mEffectiveFormat(effectiveFormat), mDestFormat(destFormat), mMinRedBits(minRedBits),
          mMaxRedBits(maxRedBits), mMinGreenBits(minGreenBits), mMaxGreenBits(maxGreenBits),
          mMinBlueBits(minBlueBits), mMaxBlueBits(maxBlueBits), mMinAlphaBits(minAlphaBits),
          mMaxAlphaBits(maxAlphaBits) {};
};

typedef std::vector<EffectiveInternalFormatInfo> EffectiveInternalFormatList;

static EffectiveInternalFormatList BuildSizedEffectiveInternalFormatList()
{
    EffectiveInternalFormatList list;

    // OpenGL ES 3.0.3 Specification, Table 3.17, pg 141: Effective internal format coresponding to destination internal format and
    //                                                    linear source buffer component sizes.
    //                                                                            | Source channel min/max sizes |
    //                                         Effective Internal Format |  N/A   |  R   |  G   |  B   |  A      |
    list.push_back(EffectiveInternalFormatInfo(GL_ALPHA8_EXT,              GL_NONE, 0,  0, 0,  0, 0,  0, 1, 8));
    list.push_back(EffectiveInternalFormatInfo(GL_R8,                      GL_NONE, 1,  8, 0,  0, 0,  0, 0, 0));
    list.push_back(EffectiveInternalFormatInfo(GL_RG8,                     GL_NONE, 1,  8, 1,  8, 0,  0, 0, 0));
    list.push_back(EffectiveInternalFormatInfo(GL_RGB565,                  GL_NONE, 1,  5, 1,  6, 1,  5, 0, 0));
    list.push_back(EffectiveInternalFormatInfo(GL_RGB8,                    GL_NONE, 6,  8, 7,  8, 6,  8, 0, 0));
    list.push_back(EffectiveInternalFormatInfo(GL_RGBA4,                   GL_NONE, 1,  4, 1,  4, 1,  4, 1, 4));
    list.push_back(EffectiveInternalFormatInfo(GL_RGB5_A1,                 GL_NONE, 5,  5, 5,  5, 5,  5, 1, 1));
    list.push_back(EffectiveInternalFormatInfo(GL_RGBA8,                   GL_NONE, 5,  8, 5,  8, 5,  8, 2, 8));
    list.push_back(EffectiveInternalFormatInfo(GL_RGB10_A2,                GL_NONE, 9, 10, 9, 10, 9, 10, 2, 2));

    return list;
}

static EffectiveInternalFormatList BuildUnsizedEffectiveInternalFormatList()
{
    EffectiveInternalFormatList list;

    // OpenGL ES 3.0.3 Specification, Table 3.17, pg 141: Effective internal format coresponding to destination internal format and
    //                                                    linear source buffer component sizes.
    //                                                                                        |          Source channel min/max sizes            |
    //                                         Effective Internal Format |    Dest Format     |     R     |      G     |      B     |      A     |
    list.push_back(EffectiveInternalFormatInfo(GL_ALPHA8_EXT,              GL_ALPHA,           0, UINT_MAX, 0, UINT_MAX, 0, UINT_MAX, 1,        8));
    list.push_back(EffectiveInternalFormatInfo(GL_LUMINANCE8_EXT,          GL_LUMINANCE,       1,        8, 0, UINT_MAX, 0, UINT_MAX, 0, UINT_MAX));
    list.push_back(EffectiveInternalFormatInfo(GL_LUMINANCE8_ALPHA8_EXT,   GL_LUMINANCE_ALPHA, 1,        8, 0, UINT_MAX, 0, UINT_MAX, 1,        8));
    list.push_back(EffectiveInternalFormatInfo(GL_RGB565,                  GL_RGB,             1,        5, 1,        6, 1,        5, 0, UINT_MAX));
    list.push_back(EffectiveInternalFormatInfo(GL_RGB8,                    GL_RGB,             6,        8, 7,        8, 6,        8, 0, UINT_MAX));
    list.push_back(EffectiveInternalFormatInfo(GL_RGBA4,                   GL_RGBA,            1,        4, 1,        4, 1,        4, 1,        4));
    list.push_back(EffectiveInternalFormatInfo(GL_RGB5_A1,                 GL_RGBA,            5,        5, 5,        5, 5,        5, 1,        1));
    list.push_back(EffectiveInternalFormatInfo(GL_RGBA8,                   GL_RGBA,            5,        8, 5,        8, 5,        8, 5,        8));

    return list;
}

static bool GetEffectiveInternalFormat(const InternalFormat &srcFormat, const InternalFormat &destFormat,
                                       GLenum *outEffectiveFormat)
{
    const EffectiveInternalFormatList *list = NULL;
    GLenum targetFormat = GL_NONE;

    if (destFormat.pixelBytes > 0)
    {
        static const EffectiveInternalFormatList sizedList = BuildSizedEffectiveInternalFormatList();
        list = &sizedList;
    }
    else
    {
        static const EffectiveInternalFormatList unsizedList = BuildUnsizedEffectiveInternalFormatList();
        list = &unsizedList;
        targetFormat = destFormat.format;
    }

    for (size_t curFormat = 0; curFormat < list->size(); ++curFormat)
    {
        const EffectiveInternalFormatInfo& formatInfo = list->at(curFormat);
        if ((formatInfo.mDestFormat == targetFormat) &&
            (formatInfo.mMinRedBits   <= srcFormat.redBits   && formatInfo.mMaxRedBits   >= srcFormat.redBits)   &&
            (formatInfo.mMinGreenBits <= srcFormat.greenBits && formatInfo.mMaxGreenBits >= srcFormat.greenBits) &&
            (formatInfo.mMinBlueBits  <= srcFormat.blueBits  && formatInfo.mMaxBlueBits  >= srcFormat.blueBits)  &&
            (formatInfo.mMinAlphaBits <= srcFormat.alphaBits && formatInfo.mMaxAlphaBits >= srcFormat.alphaBits))
        {
            *outEffectiveFormat = formatInfo.mEffectiveFormat;
            return true;
        }
    }

    return false;
}

struct CopyConversion
{
    GLenum mTextureFormat;
    GLenum mFramebufferFormat;

    CopyConversion(GLenum textureFormat, GLenum framebufferFormat)
        : mTextureFormat(textureFormat), mFramebufferFormat(framebufferFormat) { }

    bool operator<(const CopyConversion& other) const
    {
        return memcmp(this, &other, sizeof(CopyConversion)) < 0;
    }
};

typedef std::set<CopyConversion> CopyConversionSet;

static CopyConversionSet BuildValidES3CopyTexImageCombinations()
{
    CopyConversionSet set;

    // From ES 3.0.1 spec, table 3.15
    set.insert(CopyConversion(GL_ALPHA, GL_RGBA));
    set.insert(CopyConversion(GL_LUMINANCE, GL_RED));
    set.insert(CopyConversion(GL_LUMINANCE, GL_RG));
    set.insert(CopyConversion(GL_LUMINANCE, GL_RGB));
    set.insert(CopyConversion(GL_LUMINANCE, GL_RGBA));
    set.insert(CopyConversion(GL_LUMINANCE_ALPHA, GL_RGBA));
    set.insert(CopyConversion(GL_RED, GL_RED));
    set.insert(CopyConversion(GL_RED, GL_RG));
    set.insert(CopyConversion(GL_RED, GL_RGB));
    set.insert(CopyConversion(GL_RED, GL_RGBA));
    set.insert(CopyConversion(GL_RG, GL_RG));
    set.insert(CopyConversion(GL_RG, GL_RGB));
    set.insert(CopyConversion(GL_RG, GL_RGBA));
    set.insert(CopyConversion(GL_RGB, GL_RGB));
    set.insert(CopyConversion(GL_RGB, GL_RGBA));
    set.insert(CopyConversion(GL_RGBA, GL_RGBA));

    // Necessary for ANGLE back-buffers
    set.insert(CopyConversion(GL_ALPHA, GL_BGRA_EXT));
    set.insert(CopyConversion(GL_LUMINANCE, GL_BGRA_EXT));
    set.insert(CopyConversion(GL_LUMINANCE_ALPHA, GL_BGRA_EXT));
    set.insert(CopyConversion(GL_RED, GL_BGRA_EXT));
    set.insert(CopyConversion(GL_RG, GL_BGRA_EXT));
    set.insert(CopyConversion(GL_RGB, GL_BGRA_EXT));
    set.insert(CopyConversion(GL_RGBA, GL_BGRA_EXT));

    set.insert(CopyConversion(GL_RED_INTEGER, GL_RED_INTEGER));
    set.insert(CopyConversion(GL_RED_INTEGER, GL_RG_INTEGER));
    set.insert(CopyConversion(GL_RED_INTEGER, GL_RGB_INTEGER));
    set.insert(CopyConversion(GL_RED_INTEGER, GL_RGBA_INTEGER));
    set.insert(CopyConversion(GL_RG_INTEGER, GL_RG_INTEGER));
    set.insert(CopyConversion(GL_RG_INTEGER, GL_RGB_INTEGER));
    set.insert(CopyConversion(GL_RG_INTEGER, GL_RGBA_INTEGER));
    set.insert(CopyConversion(GL_RGB_INTEGER, GL_RGB_INTEGER));
    set.insert(CopyConversion(GL_RGB_INTEGER, GL_RGBA_INTEGER));
    set.insert(CopyConversion(GL_RGBA_INTEGER, GL_RGBA_INTEGER));

    return set;
}

static bool EqualOrFirstZero(GLuint first, GLuint second)
{
    return first == 0 || first == second;
}

static bool IsValidES3CopyTexImageCombination(GLenum textureInternalFormat, GLenum frameBufferInternalFormat, GLuint readBufferHandle)
{
    const InternalFormat &textureInternalFormatInfo = GetInternalFormatInfo(textureInternalFormat);
    const InternalFormat &framebufferInternalFormatInfo = GetInternalFormatInfo(frameBufferInternalFormat);

    static const CopyConversionSet conversionSet = BuildValidES3CopyTexImageCombinations();
    if (conversionSet.find(CopyConversion(textureInternalFormatInfo.format, framebufferInternalFormatInfo.format)) != conversionSet.end())
    {
        // Section 3.8.5 of the GLES 3.0.3 spec states that source and destination formats
        // must both be signed, unsigned, or fixed point and both source and destinations
        // must be either both SRGB or both not SRGB. EXT_color_buffer_float adds allowed
        // conversion between fixed and floating point.

        if ((textureInternalFormatInfo.colorEncoding == GL_SRGB) != (framebufferInternalFormatInfo.colorEncoding == GL_SRGB))
        {
            return false;
        }

        if (((textureInternalFormatInfo.componentType == GL_INT)          != (framebufferInternalFormatInfo.componentType == GL_INT         )) ||
            ((textureInternalFormatInfo.componentType == GL_UNSIGNED_INT) != (framebufferInternalFormatInfo.componentType == GL_UNSIGNED_INT)))
        {
            return false;
        }

        if ((textureInternalFormatInfo.componentType == GL_UNSIGNED_NORMALIZED ||
             textureInternalFormatInfo.componentType == GL_SIGNED_NORMALIZED ||
             textureInternalFormatInfo.componentType == GL_FLOAT) &&
            !(framebufferInternalFormatInfo.componentType == GL_UNSIGNED_NORMALIZED ||
              framebufferInternalFormatInfo.componentType == GL_SIGNED_NORMALIZED ||
              framebufferInternalFormatInfo.componentType == GL_FLOAT))
        {
            return false;
        }

        // GLES specification 3.0.3, sec 3.8.5, pg 139-140:
        // The effective internal format of the source buffer is determined with the following rules applied in order:
        //    * If the source buffer is a texture or renderbuffer that was created with a sized internal format then the
        //      effective internal format is the source buffer's sized internal format.
        //    * If the source buffer is a texture that was created with an unsized base internal format, then the
        //      effective internal format is the source image array's effective internal format, as specified by table
        //      3.12, which is determined from the <format> and <type> that were used when the source image array was
        //      specified by TexImage*.
        //    * Otherwise the effective internal format is determined by the row in table 3.17 or 3.18 where
        //      Destination Internal Format matches internalformat and where the [source channel sizes] are consistent
        //      with the values of the source buffer's [channel sizes]. Table 3.17 is used if the
        //      FRAMEBUFFER_ATTACHMENT_ENCODING is LINEAR and table 3.18 is used if the FRAMEBUFFER_ATTACHMENT_ENCODING
        //      is SRGB.
        const InternalFormat *sourceEffectiveFormat = NULL;
        if (readBufferHandle != 0)
        {
            // Not the default framebuffer, therefore the read buffer must be a user-created texture or renderbuffer
            if (framebufferInternalFormatInfo.pixelBytes > 0)
            {
                sourceEffectiveFormat = &framebufferInternalFormatInfo;
            }
            else
            {
                // Renderbuffers cannot be created with an unsized internal format, so this must be an unsized-format
                // texture. We can use the same table we use when creating textures to get its effective sized format.
                GLenum sizedInternalFormat = GetSizedInternalFormat(framebufferInternalFormatInfo.format, framebufferInternalFormatInfo.type);
                sourceEffectiveFormat = &GetInternalFormatInfo(sizedInternalFormat);
            }
        }
        else
        {
            // The effective internal format must be derived from the source framebuffer's channel sizes.
            // This is done in GetEffectiveInternalFormat for linear buffers (table 3.17)
            if (framebufferInternalFormatInfo.colorEncoding == GL_LINEAR)
            {
                GLenum effectiveFormat;
                if (GetEffectiveInternalFormat(framebufferInternalFormatInfo, textureInternalFormatInfo, &effectiveFormat))
                {
                    sourceEffectiveFormat = &GetInternalFormatInfo(effectiveFormat);
                }
                else
                {
                    return false;
                }
            }
            else if (framebufferInternalFormatInfo.colorEncoding == GL_SRGB)
            {
                // SRGB buffers can only be copied to sized format destinations according to table 3.18
                if ((textureInternalFormatInfo.pixelBytes > 0) &&
                    (framebufferInternalFormatInfo.redBits   >= 1 && framebufferInternalFormatInfo.redBits   <= 8) &&
                    (framebufferInternalFormatInfo.greenBits >= 1 && framebufferInternalFormatInfo.greenBits <= 8) &&
                    (framebufferInternalFormatInfo.blueBits  >= 1 && framebufferInternalFormatInfo.blueBits  <= 8) &&
                    (framebufferInternalFormatInfo.alphaBits >= 1 && framebufferInternalFormatInfo.alphaBits <= 8))
                {
                    sourceEffectiveFormat = &GetInternalFormatInfo(GL_SRGB8_ALPHA8);
                }
                else
                {
                    return false;
                }
            }
            else
            {
                UNREACHABLE();
                return false;
            }
        }

        if (textureInternalFormatInfo.pixelBytes > 0)
        {
            // Section 3.8.5 of the GLES 3.0.3 spec, pg 139, requires that, if the destination
            // format is sized, component sizes of the source and destination formats must exactly
            // match if the destination format exists.
            if (!EqualOrFirstZero(textureInternalFormatInfo.redBits,
                                  sourceEffectiveFormat->redBits) ||
                !EqualOrFirstZero(textureInternalFormatInfo.greenBits,
                                  sourceEffectiveFormat->greenBits) ||
                !EqualOrFirstZero(textureInternalFormatInfo.blueBits,
                                  sourceEffectiveFormat->blueBits) ||
                !EqualOrFirstZero(textureInternalFormatInfo.alphaBits,
                                  sourceEffectiveFormat->alphaBits))
            {
                return false;
            }
        }


        return true; // A conversion function exists, and no rule in the specification has precluded conversion
                     // between these formats.
    }

    return false;
}

bool ValidateES3CopyTexImageParametersBase(ValidationContext *context,
                                           GLenum target,
                                           GLint level,
                                           GLenum internalformat,
                                           bool isSubImage,
                                           GLint xoffset,
                                           GLint yoffset,
                                           GLint zoffset,
                                           GLint x,
                                           GLint y,
                                           GLsizei width,
                                           GLsizei height,
                                           GLint border)
{
    GLenum textureInternalFormat;
    if (!ValidateCopyTexImageParametersBase(context, target, level, internalformat, isSubImage,
                                            xoffset, yoffset, zoffset, x, y, width, height,
                                            border, &textureInternalFormat))
    {
        return false;
    }

    const auto &state            = context->getGLState();
    gl::Framebuffer *framebuffer = state.getReadFramebuffer();
    GLuint readFramebufferID     = framebuffer->id();

    if (framebuffer->checkStatus(context->getContextState()) != GL_FRAMEBUFFER_COMPLETE)
    {
        context->handleError(Error(GL_INVALID_FRAMEBUFFER_OPERATION));
        return false;
    }

    if (readFramebufferID != 0 && framebuffer->getSamples(context->getContextState()) != 0)
    {
        context->handleError(Error(GL_INVALID_OPERATION));
        return false;
    }

    const gl::FramebufferAttachment *source = framebuffer->getReadColorbuffer();
    GLenum colorbufferInternalFormat = source->getInternalFormat();

    if (isSubImage)
    {
        if (!IsValidES3CopyTexImageCombination(textureInternalFormat, colorbufferInternalFormat,
                                               readFramebufferID))
        {
            context->handleError(Error(GL_INVALID_OPERATION));
            return false;
        }
    }
    else
    {
        if (!gl::IsValidES3CopyTexImageCombination(internalformat, colorbufferInternalFormat,
                                                   readFramebufferID))
        {
            context->handleError(Error(GL_INVALID_OPERATION));
            return false;
        }
    }

    // If width or height is zero, it is a no-op.  Return false without setting an error.
    return (width > 0 && height > 0);
}

bool ValidateES3CopyTexImage2DParameters(ValidationContext *context,
                                         GLenum target,
                                         GLint level,
                                         GLenum internalformat,
                                         bool isSubImage,
                                         GLint xoffset,
                                         GLint yoffset,
                                         GLint zoffset,
                                         GLint x,
                                         GLint y,
                                         GLsizei width,
                                         GLsizei height,
                                         GLint border)
{
    if (!ValidTexture2DDestinationTarget(context, target))
    {
        context->handleError(Error(GL_INVALID_ENUM));
        return false;
    }

    return ValidateES3CopyTexImageParametersBase(context, target, level, internalformat, isSubImage,
                                                 xoffset, yoffset, zoffset, x, y, width, height,
                                                 border);
}

bool ValidateES3CopyTexImage3DParameters(ValidationContext *context,
                                         GLenum target,
                                         GLint level,
                                         GLenum internalformat,
                                         bool isSubImage,
                                         GLint xoffset,
                                         GLint yoffset,
                                         GLint zoffset,
                                         GLint x,
                                         GLint y,
                                         GLsizei width,
                                         GLsizei height,
                                         GLint border)
{
    if (!ValidTexture3DDestinationTarget(context, target))
    {
        context->handleError(Error(GL_INVALID_ENUM));
        return false;
    }

    return ValidateES3CopyTexImageParametersBase(context, target, level, internalformat, isSubImage,
                                                 xoffset, yoffset, zoffset, x, y, width, height,
                                                 border);
}

bool ValidateES3TexStorageParametersBase(Context *context,
                                         GLenum target,
                                         GLsizei levels,
                                         GLenum internalformat,
                                         GLsizei width,
                                         GLsizei height,
                                         GLsizei depth)
{
    if (width < 1 || height < 1 || depth < 1 || levels < 1)
    {
        context->handleError(Error(GL_INVALID_VALUE));
        return false;
    }

    GLsizei maxDim = std::max(width, height);
    if (target != GL_TEXTURE_2D_ARRAY)
    {
        maxDim = std::max(maxDim, depth);
    }

    if (levels > gl::log2(maxDim) + 1)
    {
        context->handleError(Error(GL_INVALID_OPERATION));
        return false;
    }

    const gl::Caps &caps = context->getCaps();

    switch (target)
    {
      case GL_TEXTURE_2D:
        {
            if (static_cast<GLuint>(width) > caps.max2DTextureSize ||
                static_cast<GLuint>(height) > caps.max2DTextureSize)
            {
                context->handleError(Error(GL_INVALID_VALUE));
                return false;
            }
        }
        break;

      case GL_TEXTURE_CUBE_MAP:
        {
            if (width != height)
            {
                context->handleError(Error(GL_INVALID_VALUE));
                return false;
            }

            if (static_cast<GLuint>(width) > caps.maxCubeMapTextureSize)
            {
                context->handleError(Error(GL_INVALID_VALUE));
                return false;
            }
        }
        break;

      case GL_TEXTURE_3D:
        {
            if (static_cast<GLuint>(width) > caps.max3DTextureSize ||
                static_cast<GLuint>(height) > caps.max3DTextureSize ||
                static_cast<GLuint>(depth) > caps.max3DTextureSize)
            {
                context->handleError(Error(GL_INVALID_VALUE));
                return false;
            }
        }
        break;

      case GL_TEXTURE_2D_ARRAY:
        {
            if (static_cast<GLuint>(width) > caps.max2DTextureSize ||
                static_cast<GLuint>(height) > caps.max2DTextureSize ||
                static_cast<GLuint>(depth) > caps.maxArrayTextureLayers)
            {
                context->handleError(Error(GL_INVALID_VALUE));
                return false;
            }
        }
        break;

      default:
          UNREACHABLE();
        return false;
    }

    gl::Texture *texture = context->getTargetTexture(target);
    if (!texture || texture->id() == 0)
    {
        context->handleError(Error(GL_INVALID_OPERATION));
        return false;
    }

    if (texture->getImmutableFormat())
    {
        context->handleError(Error(GL_INVALID_OPERATION));
        return false;
    }

    const gl::InternalFormat &formatInfo = gl::GetInternalFormatInfo(internalformat);
    if (!formatInfo.textureSupport(context->getClientVersion(), context->getExtensions()))
    {
        context->handleError(Error(GL_INVALID_ENUM));
        return false;
    }

    if (formatInfo.pixelBytes == 0)
    {
        context->handleError(Error(GL_INVALID_ENUM));
        return false;
    }

    return true;
}

bool ValidateES3TexStorage2DParameters(Context *context,
                                       GLenum target,
                                       GLsizei levels,
                                       GLenum internalformat,
                                       GLsizei width,
                                       GLsizei height,
                                       GLsizei depth)
{
    if (!ValidTexture2DTarget(context, target))
    {
        context->handleError(Error(GL_INVALID_ENUM));
        return false;
    }

    return ValidateES3TexStorageParametersBase(context, target, levels, internalformat, width,
                                               height, depth);
}

bool ValidateES3TexStorage3DParameters(Context *context,
                                       GLenum target,
                                       GLsizei levels,
                                       GLenum internalformat,
                                       GLsizei width,
                                       GLsizei height,
                                       GLsizei depth)
{
    if (!ValidTexture3DTarget(context, target))
    {
        context->handleError(Error(GL_INVALID_ENUM));
        return false;
    }

    return ValidateES3TexStorageParametersBase(context, target, levels, internalformat, width,
                                               height, depth);
}

bool ValidateBeginQuery(gl::Context *context, GLenum target, GLuint id)
{
    if (context->getClientVersion() < 3)
    {
        context->handleError(Error(GL_INVALID_OPERATION, "GLES version < 3.0"));
        return false;
    }

    return ValidateBeginQueryBase(context, target, id);
}

bool ValidateEndQuery(gl::Context *context, GLenum target)
{
    if (context->getClientVersion() < 3)
    {
        context->handleError(Error(GL_INVALID_OPERATION, "GLES version < 3.0"));
        return false;
    }

    return ValidateEndQueryBase(context, target);
}

bool ValidateGetQueryiv(Context *context, GLenum target, GLenum pname, GLint *params)
{
    if (context->getClientVersion() < 3)
    {
        context->handleError(Error(GL_INVALID_OPERATION, "GLES version < 3.0"));
        return false;
    }

    return ValidateGetQueryivBase(context, target, pname);
}

bool ValidateGetQueryObjectuiv(Context *context, GLuint id, GLenum pname, GLuint *params)
{
    if (context->getClientVersion() < 3)
    {
        context->handleError(Error(GL_INVALID_OPERATION, "GLES version < 3.0"));
        return false;
    }

    return ValidateGetQueryObjectValueBase(context, id, pname);
}

bool ValidateFramebufferTextureLayer(Context *context, GLenum target, GLenum attachment,
                                     GLuint texture, GLint level, GLint layer)
{
    if (context->getClientVersion() < 3)
    {
        context->handleError(Error(GL_INVALID_OPERATION));
        return false;
    }

    if (layer < 0)
    {
        context->handleError(Error(GL_INVALID_VALUE));
        return false;
    }

    if (!ValidateFramebufferTextureBase(context, target, attachment, texture, level))
    {
        return false;
    }

    const gl::Caps &caps = context->getCaps();
    if (texture != 0)
    {
        gl::Texture *tex = context->getTexture(texture);
        ASSERT(tex);

        switch (tex->getTarget())
        {
          case GL_TEXTURE_2D_ARRAY:
            {
                if (level > gl::log2(caps.max2DTextureSize))
                {
                    context->handleError(Error(GL_INVALID_VALUE));
                    return false;
                }

                if (static_cast<GLuint>(layer) >= caps.maxArrayTextureLayers)
                {
                    context->handleError(Error(GL_INVALID_VALUE));
                    return false;
                }
            }
            break;

          case GL_TEXTURE_3D:
            {
                if (level > gl::log2(caps.max3DTextureSize))
                {
                    context->handleError(Error(GL_INVALID_VALUE));
                    return false;
                }

                if (static_cast<GLuint>(layer) >= caps.max3DTextureSize)
                {
                    context->handleError(Error(GL_INVALID_VALUE));
                    return false;
                }
            }
            break;

          default:
              context->handleError(Error(GL_INVALID_OPERATION));
            return false;
        }

        const gl::InternalFormat &internalFormatInfo = gl::GetInternalFormatInfo(tex->getInternalFormat(tex->getTarget(), level));
        if (internalFormatInfo.compressed)
        {
            context->handleError(Error(GL_INVALID_OPERATION));
            return false;
        }
    }

    return true;
}

bool ValidES3ReadFormatType(ValidationContext *context,
                            GLenum internalFormat,
                            GLenum format,
                            GLenum type)
{
    const gl::InternalFormat &internalFormatInfo = gl::GetInternalFormatInfo(internalFormat);

    switch (format)
    {
      case GL_RGBA:
        switch (type)
        {
          case GL_UNSIGNED_BYTE:
            break;
          case GL_UNSIGNED_SHORT:
              if (internalFormatInfo.componentType != GL_UNSIGNED_NORMALIZED &&
                  internalFormatInfo.type != GL_UNSIGNED_SHORT)
              {
                  return false;
              }
              break;
          case GL_UNSIGNED_INT_2_10_10_10_REV:
            if (internalFormat != GL_RGB10_A2)
            {
                return false;
            }
            break;
          case GL_FLOAT:
            if (internalFormatInfo.componentType != GL_FLOAT)
            {
                return false;
            }
            break;
          default:
            return false;
        }
        break;
      case GL_RGBA_INTEGER:
        switch (type)
        {
          case GL_INT:
            if (internalFormatInfo.componentType != GL_INT)
            {
                return false;
            }
            break;
          case GL_UNSIGNED_INT:
            if (internalFormatInfo.componentType != GL_UNSIGNED_INT)
            {
                return false;
            }
            break;
          default:
            return false;
        }
        break;
      case GL_BGRA_EXT:
        switch (type)
        {
          case GL_UNSIGNED_BYTE:
          case GL_UNSIGNED_SHORT_4_4_4_4_REV_EXT:
          case GL_UNSIGNED_SHORT_1_5_5_5_REV_EXT:
            break;
          default:
            return false;
        }
        break;
      case GL_RG_EXT:
      case GL_RED_EXT:
        if (!context->getExtensions().textureRG)
        {
            return false;
        }
        switch (type)
        {
        case GL_UNSIGNED_BYTE:
            break;
        case GL_UNSIGNED_SHORT:
            if (internalFormatInfo.componentType != GL_UNSIGNED_NORMALIZED &&
                internalFormatInfo.type != GL_UNSIGNED_SHORT)
            {
                return false;
            }
            break;
        default:
            return false;
        }
        break;
      default:
        return false;
    }
    return true;
}

bool ValidateES3RenderbufferStorageParameters(gl::Context *context, GLenum target, GLsizei samples,
                                              GLenum internalformat, GLsizei width, GLsizei height)
{
    if (!ValidateRenderbufferStorageParametersBase(context, target, samples, internalformat, width, height))
    {
        return false;
    }

    //The ES3 spec(section 4.4.2) states that the internal format must be sized and not an integer format if samples is greater than zero.
    const gl::InternalFormat &formatInfo = gl::GetInternalFormatInfo(internalformat);
    if ((formatInfo.componentType == GL_UNSIGNED_INT || formatInfo.componentType == GL_INT) && samples > 0)
    {
        context->handleError(Error(GL_INVALID_OPERATION));
        return false;
    }

    // The behavior is different than the ANGLE version, which would generate a GL_OUT_OF_MEMORY.
    const TextureCaps &formatCaps = context->getTextureCaps().get(internalformat);
    if (static_cast<GLuint>(samples) > formatCaps.getMaxSamples())
    {
        context->handleError(
            Error(GL_INVALID_OPERATION,
                  "Samples must not be greater than maximum supported value for the format."));
        return false;
    }

    return true;
}

bool ValidateInvalidateFramebuffer(Context *context, GLenum target, GLsizei numAttachments,
                                   const GLenum *attachments)
{
    if (context->getClientVersion() < 3)
    {
        context->handleError(
            Error(GL_INVALID_OPERATION, "Operation only supported on ES 3.0 and above"));
        return false;
    }

    bool defaultFramebuffer = false;

    switch (target)
    {
      case GL_DRAW_FRAMEBUFFER:
      case GL_FRAMEBUFFER:
          defaultFramebuffer = context->getGLState().getDrawFramebuffer()->id() == 0;
          break;
      case GL_READ_FRAMEBUFFER:
          defaultFramebuffer = context->getGLState().getReadFramebuffer()->id() == 0;
          break;
      default:
          context->handleError(Error(GL_INVALID_ENUM, "Invalid framebuffer target"));
        return false;
    }

    return ValidateDiscardFramebufferBase(context, target, numAttachments, attachments, defaultFramebuffer);
}

bool ValidateClearBuffer(ValidationContext *context)
{
    if (context->getClientVersion() < 3)
    {
        context->handleError(Error(GL_INVALID_OPERATION));
        return false;
    }

    if (context->getGLState().getDrawFramebuffer()->checkStatus(context->getContextState()) !=
        GL_FRAMEBUFFER_COMPLETE)
    {
        context->handleError(Error(GL_INVALID_FRAMEBUFFER_OPERATION));
        return false;
    }

    return true;
}

bool ValidateDrawRangeElements(Context *context,
                               GLenum mode,
                               GLuint start,
                               GLuint end,
                               GLsizei count,
                               GLenum type,
                               const GLvoid *indices,
                               IndexRange *indexRange)
{
    if (context->getClientVersion() < 3)
    {
        context->handleError(Error(GL_INVALID_OPERATION, "Context does not support GLES3."));
        return false;
    }

    if (end < start)
    {
        context->handleError(Error(GL_INVALID_VALUE, "end < start"));
        return false;
    }

    if (!ValidateDrawElements(context, mode, count, type, indices, 0, indexRange))
    {
        return false;
    }

    if (indexRange->end > end || indexRange->start < start)
    {
        // GL spec says that behavior in this case is undefined - generating an error is fine.
        context->handleError(
            Error(GL_INVALID_OPERATION, "Indices are out of the start, end range."));
        return false;
    }
    return true;
}

bool ValidateGetUniformuiv(Context *context, GLuint program, GLint location, GLuint* params)
{
    if (context->getClientVersion() < 3)
    {
        context->handleError(Error(GL_INVALID_OPERATION));
        return false;
    }

    return ValidateGetUniformBase(context, program, location);
}

bool ValidateReadBuffer(Context *context, GLenum src)
{
    if (context->getClientVersion() < 3)
    {
        context->handleError(Error(GL_INVALID_OPERATION));
        return false;
    }

    const Framebuffer *readFBO = context->getGLState().getReadFramebuffer();

    if (readFBO == nullptr)
    {
        context->handleError(gl::Error(GL_INVALID_OPERATION, "No active read framebuffer."));
        return false;
    }

    if (src == GL_NONE)
    {
        return true;
    }

    if (src != GL_BACK && (src < GL_COLOR_ATTACHMENT0 || src > GL_COLOR_ATTACHMENT31))
    {
        context->handleError(gl::Error(GL_INVALID_ENUM, "Unknown enum for 'src' in ReadBuffer"));
        return false;
    }

    if (readFBO->id() == 0)
    {
        if (src != GL_BACK)
        {
            const char *errorMsg = "'src' must be GL_NONE or GL_BACK when reading from the default framebuffer.";
            context->handleError(gl::Error(GL_INVALID_OPERATION, errorMsg));
            return false;
        }
    }
    else
    {
        GLuint drawBuffer = static_cast<GLuint>(src - GL_COLOR_ATTACHMENT0);

        if (drawBuffer >= context->getCaps().maxDrawBuffers)
        {
            const char *errorMsg = "'src' is greater than MAX_DRAW_BUFFERS.";
            context->handleError(gl::Error(GL_INVALID_OPERATION, errorMsg));
            return false;
        }
    }

    return true;
}

bool ValidateCompressedTexImage3D(Context *context,
                                  GLenum target,
                                  GLint level,
                                  GLenum internalformat,
                                  GLsizei width,
                                  GLsizei height,
                                  GLsizei depth,
                                  GLint border,
                                  GLsizei imageSize,
                                  const GLvoid *data)
{
    if (context->getClientVersion() < 3)
    {
        context->handleError(Error(GL_INVALID_OPERATION));
        return false;
    }

    if (!ValidTextureTarget(context, target))
    {
        context->handleError(Error(GL_INVALID_ENUM));
        return false;
    }

    // Validate image size
    if (!ValidImageSizeParameters(context, target, level, width, height, depth, false))
    {
        context->handleError(Error(GL_INVALID_VALUE));
        return false;
    }

    const InternalFormat &formatInfo = GetInternalFormatInfo(internalformat);
    if (!formatInfo.compressed)
    {
        context->handleError(Error(GL_INVALID_ENUM, "Not a valid compressed texture format"));
        return false;
    }

    auto blockSizeOrErr =
        formatInfo.computeCompressedImageSize(GL_UNSIGNED_BYTE, gl::Extents(width, height, depth));
    if (blockSizeOrErr.isError())
    {
        context->handleError(Error(GL_INVALID_VALUE));
        return false;
    }
    if (imageSize < 0 || static_cast<GLuint>(imageSize) != blockSizeOrErr.getResult())
    {
        context->handleError(Error(GL_INVALID_VALUE));
        return false;
    }

    // 3D texture target validation
    if (target != GL_TEXTURE_3D && target != GL_TEXTURE_2D_ARRAY)
    {
        context->handleError(
            Error(GL_INVALID_ENUM, "Must specify a valid 3D texture destination target"));
        return false;
    }

    // validateES3TexImageFormat sets the error code if there is an error
    if (!ValidateES3TexImage3DParameters(context, target, level, internalformat, true, false, 0, 0,
                                         0, width, height, depth, border, GL_NONE, GL_NONE, data))
    {
        return false;
    }

    return true;
}

bool ValidateBindVertexArray(Context *context, GLuint array)
{
    if (context->getClientVersion() < 3)
    {
        context->handleError(Error(GL_INVALID_OPERATION));
        return false;
    }

    return ValidateBindVertexArrayBase(context, array);
}

bool ValidateIsVertexArray(Context *context)
{
    if (context->getClientVersion() < 3)
    {
        context->handleError(Error(GL_INVALID_OPERATION));
        return false;
    }

    return true;
}

bool ValidateProgramBinary(Context *context,
                           GLuint program,
                           GLenum binaryFormat,
                           const void *binary,
                           GLint length)
{
    if (context->getClientVersion() < 3)
    {
        context->handleError(Error(GL_INVALID_OPERATION));
        return false;
    }

    return ValidateProgramBinaryBase(context, program, binaryFormat, binary, length);
}

bool ValidateGetProgramBinary(Context *context,
                              GLuint program,
                              GLsizei bufSize,
                              GLsizei *length,
                              GLenum *binaryFormat,
                              void *binary)
{
    if (context->getClientVersion() < 3)
    {
        context->handleError(Error(GL_INVALID_OPERATION));
        return false;
    }

    return ValidateGetProgramBinaryBase(context, program, bufSize, length, binaryFormat, binary);
}

bool ValidateProgramParameteri(Context *context, GLuint program, GLenum pname, GLint value)
{
    if (context->getClientVersion() < 3)
    {
        context->handleError(Error(GL_INVALID_OPERATION, "Context does not support GLES3."));
        return false;
    }

    if (GetValidProgram(context, program) == nullptr)
    {
        return false;
    }

    switch (pname)
    {
        case GL_PROGRAM_BINARY_RETRIEVABLE_HINT:
            if (value != GL_FALSE && value != GL_TRUE)
            {
                context->handleError(Error(
                    GL_INVALID_VALUE, "Invalid value, expected GL_FALSE or GL_TRUE: %i", value));
                return false;
            }
            break;

        default:
            context->handleError(Error(GL_INVALID_ENUM, "Invalid pname: 0x%X", pname));
            return false;
    }

    return true;
}

bool ValidateBlitFramebuffer(Context *context,
                             GLint srcX0,
                             GLint srcY0,
                             GLint srcX1,
                             GLint srcY1,
                             GLint dstX0,
                             GLint dstY0,
                             GLint dstX1,
                             GLint dstY1,
                             GLbitfield mask,
                             GLenum filter)
{
    if (context->getClientVersion() < 3)
    {
        context->handleError(Error(GL_INVALID_OPERATION));
        return false;
    }

    return ValidateBlitFramebufferParameters(context, srcX0, srcY0, srcX1, srcY1, dstX0, dstY0,
                                             dstX1, dstY1, mask, filter);
}

bool ValidateClearBufferiv(ValidationContext *context,
                           GLenum buffer,
                           GLint drawbuffer,
                           const GLint *value)
{
    switch (buffer)
    {
        case GL_COLOR:
            if (drawbuffer < 0 ||
                static_cast<GLuint>(drawbuffer) >= context->getCaps().maxDrawBuffers)
            {
                context->handleError(Error(GL_INVALID_VALUE));
                return false;
            }
            break;

        case GL_STENCIL:
            if (drawbuffer != 0)
            {
                context->handleError(Error(GL_INVALID_VALUE));
                return false;
            }
            break;

        default:
            context->handleError(Error(GL_INVALID_ENUM));
            return false;
    }

    return ValidateClearBuffer(context);
}

bool ValidateClearBufferuiv(ValidationContext *context,
                            GLenum buffer,
                            GLint drawbuffer,
                            const GLuint *value)
{
    switch (buffer)
    {
        case GL_COLOR:
            if (drawbuffer < 0 ||
                static_cast<GLuint>(drawbuffer) >= context->getCaps().maxDrawBuffers)
            {
                context->handleError(Error(GL_INVALID_VALUE));
                return false;
            }
            break;

        default:
            context->handleError(Error(GL_INVALID_ENUM));
            return false;
    }

    return ValidateClearBuffer(context);
}

bool ValidateClearBufferfv(ValidationContext *context,
                           GLenum buffer,
                           GLint drawbuffer,
                           const GLfloat *value)
{
    switch (buffer)
    {
        case GL_COLOR:
            if (drawbuffer < 0 ||
                static_cast<GLuint>(drawbuffer) >= context->getCaps().maxDrawBuffers)
            {
                context->handleError(Error(GL_INVALID_VALUE));
                return false;
            }
            break;

        case GL_DEPTH:
            if (drawbuffer != 0)
            {
                context->handleError(Error(GL_INVALID_VALUE));
                return false;
            }
            break;

        default:
            context->handleError(Error(GL_INVALID_ENUM));
            return false;
    }

    return ValidateClearBuffer(context);
}

bool ValidateClearBufferfi(ValidationContext *context,
                           GLenum buffer,
                           GLint drawbuffer,
                           GLfloat depth,
                           GLint stencil)
{
    switch (buffer)
    {
        case GL_DEPTH_STENCIL:
            if (drawbuffer != 0)
            {
                context->handleError(Error(GL_INVALID_VALUE));
                return false;
            }
            break;

        default:
            context->handleError(Error(GL_INVALID_ENUM));
            return false;
    }

    return ValidateClearBuffer(context);
}

bool ValidateDrawBuffers(ValidationContext *context, GLsizei n, const GLenum *bufs)
{
    if (context->getClientVersion() < 3)
    {
        context->handleError(Error(GL_INVALID_OPERATION, "Context does not support GLES3."));
        return false;
    }

    return ValidateDrawBuffersBase(context, n, bufs);
}

bool ValidateCopyTexSubImage3D(Context *context,
                               GLenum target,
                               GLint level,
                               GLint xoffset,
                               GLint yoffset,
                               GLint zoffset,
                               GLint x,
                               GLint y,
                               GLsizei width,
                               GLsizei height)
{
    if (context->getClientVersion() < 3)
    {
        context->handleError(Error(GL_INVALID_OPERATION));
        return false;
    }

    return ValidateES3CopyTexImage3DParameters(context, target, level, GL_NONE, true, xoffset,
                                               yoffset, zoffset, x, y, width, height, 0);
}

bool ValidateTexImage3D(Context *context,
                        GLenum target,
                        GLint level,
                        GLint internalformat,
                        GLsizei width,
                        GLsizei height,
                        GLsizei depth,
                        GLint border,
                        GLenum format,
                        GLenum type,
                        const GLvoid *pixels)
{
    if (context->getClientVersion() < 3)
    {
        context->handleError(Error(GL_INVALID_OPERATION));
        return false;
    }

    return ValidateES3TexImage3DParameters(context, target, level, internalformat, false, false, 0,
                                           0, 0, width, height, depth, border, format, type,
                                           pixels);
}

bool ValidateTexSubImage3D(Context *context,
                           GLenum target,
                           GLint level,
                           GLint xoffset,
                           GLint yoffset,
                           GLint zoffset,
                           GLsizei width,
                           GLsizei height,
                           GLsizei depth,
                           GLenum format,
                           GLenum type,
                           const GLvoid *pixels)
{
    if (context->getClientVersion() < 3)
    {
        context->handleError(Error(GL_INVALID_OPERATION));
        return false;
    }

    return ValidateES3TexImage3DParameters(context, target, level, GL_NONE, false, true, xoffset,
                                           yoffset, zoffset, width, height, depth, 0, format, type,
                                           pixels);
}

bool ValidateCompressedTexSubImage3D(Context *context,
                                     GLenum target,
                                     GLint level,
                                     GLint xoffset,
                                     GLint yoffset,
                                     GLint zoffset,
                                     GLsizei width,
                                     GLsizei height,
                                     GLsizei depth,
                                     GLenum format,
                                     GLsizei imageSize,
                                     const GLvoid *data)
{
    if (context->getClientVersion() < 3)
    {
        context->handleError(Error(GL_INVALID_OPERATION));
        return false;
    }

    const InternalFormat &formatInfo = GetInternalFormatInfo(format);
    auto blockSizeOrErr =
        formatInfo.computeCompressedImageSize(GL_UNSIGNED_BYTE, gl::Extents(width, height, depth));
    if (blockSizeOrErr.isError())
    {
        context->handleError(blockSizeOrErr.getError());
        return false;
    }
    if (imageSize < 0 || static_cast<GLuint>(imageSize) != blockSizeOrErr.getResult())
    {
        context->handleError(Error(GL_INVALID_VALUE));
        return false;
    }

    if (!data)
    {
        context->handleError(Error(GL_INVALID_VALUE));
        return false;
    }

    return ValidateES3TexImage3DParameters(context, target, level, GL_NONE, true, true, 0, 0, 0,
                                           width, height, depth, 0, GL_NONE, GL_NONE, data);
}

bool ValidateGenQueries(Context *context, GLint n, GLuint *)
{
    return ValidateGenOrDeleteES3(context, n);
}

bool ValidateDeleteQueries(Context *context, GLint n, const GLuint *)
{
    return ValidateGenOrDeleteES3(context, n);
}

bool ValidateGenSamplers(Context *context, GLint count, GLuint *)
{
    return ValidateGenOrDeleteCountES3(context, count);
}

bool ValidateDeleteSamplers(Context *context, GLint count, const GLuint *)
{
    return ValidateGenOrDeleteCountES3(context, count);
}

bool ValidateGenTransformFeedbacks(Context *context, GLint n, GLuint *)
{
    return ValidateGenOrDeleteES3(context, n);
}

bool ValidateDeleteTransformFeedbacks(Context *context, GLint n, const GLuint *ids)
{
    if (!ValidateGenOrDeleteES3(context, n))
    {
        return false;
    }
    for (GLint i = 0; i < n; ++i)
    {
        auto *transformFeedback = context->getTransformFeedback(ids[i]);
        if (transformFeedback != nullptr && transformFeedback->isActive())
        {
            // ES 3.0.4 section 2.15.1 page 86
            context->handleError(
                Error(GL_INVALID_OPERATION, "Attempt to delete active transform feedback."));
            return false;
        }
    }
    return true;
}

bool ValidateGenVertexArrays(Context *context, GLint n, GLuint *)
{
    return ValidateGenOrDeleteES3(context, n);
}

bool ValidateDeleteVertexArrays(Context *context, GLint n, const GLuint *)
{
    return ValidateGenOrDeleteES3(context, n);
}

bool ValidateGenOrDeleteES3(Context *context, GLint n)
{
    if (context->getClientVersion() < 3)
    {
        context->handleError(Error(GL_INVALID_OPERATION, "Context does not support GLES3."));
        return false;
    }
    return ValidateGenOrDelete(context, n);
}

bool ValidateGenOrDeleteCountES3(Context *context, GLint count)
{
    if (context->getClientVersion() < 3)
    {
        context->handleError(Error(GL_INVALID_OPERATION, "Context does not support GLES3."));
        return false;
    }
    if (count < 0)
    {
        context->handleError(Error(GL_INVALID_VALUE, "count < 0"));
        return false;
    }
    return true;
}

bool ValidateBeginTransformFeedback(Context *context, GLenum primitiveMode)
{
    if (context->getClientVersion() < 3)
    {
        context->handleError(Error(GL_INVALID_OPERATION, "Context does not support GLES3."));
        return false;
    }
    switch (primitiveMode)
    {
        case GL_TRIANGLES:
        case GL_LINES:
        case GL_POINTS:
            break;

        default:
            context->handleError(Error(GL_INVALID_ENUM, "Invalid primitive mode."));
            return false;
    }

    TransformFeedback *transformFeedback = context->getGLState().getCurrentTransformFeedback();
    ASSERT(transformFeedback != nullptr);

    if (transformFeedback->isActive())
    {
        context->handleError(Error(GL_INVALID_OPERATION, "Transform feedback is already active."));
        return false;
    }
    return true;
}

bool ValidateSamplerParameteri(Context *context, GLuint sampler, GLenum pname, GLint param)
{
    if (context->getClientVersion() < 3)
    {
        context->handleError(Error(GL_INVALID_OPERATION, "Context does not support GLES3."));
        return false;
    }

    if (!context->isSampler(sampler))
    {
        context->handleError(Error(GL_INVALID_OPERATION));
        return false;
    }

    if (!ValidateSamplerObjectParameter(context, pname))
    {
        return false;
    }

    if (!ValidateTexParamParameters(context, GL_TEXTURE_2D, pname, param))
    {
        return false;
    }
    return true;
}

bool ValidateSamplerParameterf(Context *context, GLuint sampler, GLenum pname, GLfloat param)
{
    // The only float parameters are MIN_LOD and MAX_LOD. For these any value is permissible, so
    // ValidateSamplerParameteri can be used for validation here.
    return ValidateSamplerParameteri(context, sampler, pname, static_cast<GLint>(param));
}

bool ValidateGetBufferPointerv(Context *context, GLenum target, GLenum pname, GLvoid **params)
{
    if (context->getClientVersion() < 3)
    {
        context->handleError(Error(GL_INVALID_OPERATION, "Context does not support GLES3."));
        return false;
    }

    return ValidateGetBufferPointervBase(context, target, pname, params);
}

bool ValidateUnmapBuffer(Context *context, GLenum target)
{
    if (context->getClientVersion() < 3)
    {
        context->handleError(Error(GL_INVALID_OPERATION));
        return false;
    }

    return ValidateUnmapBufferBase(context, target);
}

bool ValidateMapBufferRange(Context *context,
                            GLenum target,
                            GLintptr offset,
                            GLsizeiptr length,
                            GLbitfield access)
{
    if (context->getClientVersion() < 3)
    {
        context->handleError(Error(GL_INVALID_OPERATION, "Context does not support GLES3."));
        return false;
    }

    return ValidateMapBufferRangeBase(context, target, offset, length, access);
}

bool ValidateFlushMappedBufferRange(Context *context,
                                    GLenum target,
                                    GLintptr offset,
                                    GLsizeiptr length)
{
    if (context->getClientVersion() < 3)
    {
        context->handleError(Error(GL_INVALID_OPERATION, "Context does not support GLES3."));
        return false;
    }

    return ValidateFlushMappedBufferRangeBase(context, target, offset, length);
}

}  // namespace gl

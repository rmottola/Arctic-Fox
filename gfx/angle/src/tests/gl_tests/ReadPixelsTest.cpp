//
// Copyright 2015 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// ReadPixelsTest:
//   Tests calls related to glReadPixels.
//

#include "test_utils/ANGLETest.h"

#include <array>

#include "random_utils.h"

using namespace angle;

namespace
{

class ReadPixelsTest : public ANGLETest
{
  protected:
    ReadPixelsTest()
    {
        setWindowWidth(32);
        setWindowHeight(32);
        setConfigRedBits(8);
        setConfigGreenBits(8);
        setConfigBlueBits(8);
        setConfigAlphaBits(8);
    }
};

// Test out of bounds reads.
TEST_P(ReadPixelsTest, OutOfBounds)
{
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    EXPECT_GL_NO_ERROR();

    GLsizei pixelsWidth = 32;
    GLsizei pixelsHeight = 32;
    GLint offset = 16;
    std::vector<GLubyte> pixels((pixelsWidth + offset) * (pixelsHeight + offset) * 4);

    glReadPixels(-offset, -offset, pixelsWidth + offset, pixelsHeight + offset, GL_RGBA, GL_UNSIGNED_BYTE, &pixels[0]);
    EXPECT_GL_NO_ERROR();

    for (int y = pixelsHeight / 2; y < pixelsHeight; y++)
    {
        for (int x = pixelsWidth / 2; x < pixelsWidth; x++)
        {
            const GLubyte* pixel = &pixels[0] + ((y * (pixelsWidth + offset) + x) * 4);
            unsigned int r = static_cast<unsigned int>(pixel[0]);
            unsigned int g = static_cast<unsigned int>(pixel[1]);
            unsigned int b = static_cast<unsigned int>(pixel[2]);
            unsigned int a = static_cast<unsigned int>(pixel[3]);

            // Expect that all pixels which fell within the framebuffer are red
            EXPECT_EQ(255u, r);
            EXPECT_EQ(0u,   g);
            EXPECT_EQ(0u,   b);
            EXPECT_EQ(255u, a);
        }
    }
}

class ReadPixelsPBOTest : public ReadPixelsTest
{
  protected:
    ReadPixelsPBOTest() : mPBO(0), mTexture(0), mFBO(0) {}

    void SetUp() override
    {
        ANGLETest::SetUp();

        glGenBuffers(1, &mPBO);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, mPBO);
        glBufferData(GL_PIXEL_PACK_BUFFER, 4 * getWindowWidth() * getWindowHeight(), nullptr,
                     GL_STATIC_DRAW);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

        glGenTextures(1, &mTexture);
        glBindTexture(GL_TEXTURE_2D, mTexture);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 4, 1);

        glGenFramebuffers(1, &mFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, mFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mTexture, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        ASSERT_GL_NO_ERROR();
    }

    void TearDown() override
    {
        ANGLETest::TearDown();

        glDeleteBuffers(1, &mPBO);
        glDeleteTextures(1, &mTexture);
        glDeleteFramebuffers(1, &mFBO);
    }

    GLuint mPBO;
    GLuint mTexture;
    GLuint mFBO;
};

// Test that binding a PBO to ARRAY_BUFFER works as expected.
TEST_P(ReadPixelsPBOTest, ArrayBufferTarget)
{
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    EXPECT_GL_NO_ERROR();

    glBindBuffer(GL_PIXEL_PACK_BUFFER, mPBO);
    glReadPixels(0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, 0);

    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glBindBuffer(GL_ARRAY_BUFFER, mPBO);

    GLvoid *mappedPtr = glMapBufferRange(GL_ARRAY_BUFFER, 0, 32, GL_MAP_READ_BIT);
    unsigned char *dataPtr = static_cast<unsigned char *>(mappedPtr);
    EXPECT_GL_NO_ERROR();

    EXPECT_EQ(255, dataPtr[0]);
    EXPECT_EQ(0,   dataPtr[1]);
    EXPECT_EQ(0,   dataPtr[2]);
    EXPECT_EQ(255, dataPtr[3]);

    glUnmapBuffer(GL_ARRAY_BUFFER);
    EXPECT_GL_NO_ERROR();
}

// Test that using a PBO does not overwrite existing data.
TEST_P(ReadPixelsPBOTest, ExistingDataPreserved)
{
    // TODO(geofflang): Figure out why this fails on AMD OpenGL (http://anglebug.com/1291)
    if (isAMD() && getPlatformRenderer() == EGL_PLATFORM_ANGLE_TYPE_OPENGL_ANGLE)
    {
        std::cout << "Test disabled on AMD OpenGL." << std::endl;
        return;
    }

    // Clear backbuffer to red
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    EXPECT_GL_NO_ERROR();

    // Read 16x16 region from red backbuffer to PBO
    glBindBuffer(GL_PIXEL_PACK_BUFFER, mPBO);
    glReadPixels(0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, 0);

    // Clear backbuffer to green
    glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    EXPECT_GL_NO_ERROR();

    // Read 16x16 region from green backbuffer to PBO at offset 16
    glReadPixels(0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, reinterpret_cast<GLvoid*>(16));
    GLvoid * mappedPtr = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, 32, GL_MAP_READ_BIT);
    unsigned char *dataPtr = static_cast<unsigned char *>(mappedPtr);
    EXPECT_GL_NO_ERROR();

    // Test pixel 0 is red (existing data)
    EXPECT_EQ(255, dataPtr[0]);
    EXPECT_EQ(0, dataPtr[1]);
    EXPECT_EQ(0, dataPtr[2]);
    EXPECT_EQ(255, dataPtr[3]);

    // Test pixel 16 is green (new data)
    EXPECT_EQ(0, dataPtr[16 * 4 + 0]);
    EXPECT_EQ(255, dataPtr[16 * 4 + 1]);
    EXPECT_EQ(0, dataPtr[16 * 4 + 2]);
    EXPECT_EQ(255, dataPtr[16 * 4 + 3]);

    glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    EXPECT_GL_NO_ERROR();
}

// Test that calling SubData preserves PBO data.
TEST_P(ReadPixelsPBOTest, SubDataPreservesContents)
{
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    EXPECT_GL_NO_ERROR();

    glBindBuffer(GL_PIXEL_PACK_BUFFER, mPBO);
    glReadPixels(0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, 0);

    unsigned char data[4] = { 1, 2, 3, 4 };

    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glBindBuffer(GL_ARRAY_BUFFER, mPBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, 4, data);

    GLvoid *mappedPtr = glMapBufferRange(GL_ARRAY_BUFFER, 0, 32, GL_MAP_READ_BIT);
    unsigned char *dataPtr = static_cast<unsigned char *>(mappedPtr);
    EXPECT_GL_NO_ERROR();

    EXPECT_EQ(1, dataPtr[0]);
    EXPECT_EQ(2, dataPtr[1]);
    EXPECT_EQ(3, dataPtr[2]);
    EXPECT_EQ(4, dataPtr[3]);

    glUnmapBuffer(GL_ARRAY_BUFFER);
    EXPECT_GL_NO_ERROR();
}

// Same as the prior test, but with an offset.
TEST_P(ReadPixelsPBOTest, SubDataOffsetPreservesContents)
{
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    EXPECT_GL_NO_ERROR();

    glBindBuffer(GL_PIXEL_PACK_BUFFER, mPBO);
    glReadPixels(0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, 0);

    unsigned char data[4] = { 1, 2, 3, 4 };

    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glBindBuffer(GL_ARRAY_BUFFER, mPBO);
    glBufferSubData(GL_ARRAY_BUFFER, 16, 4, data);

    GLvoid *mappedPtr = glMapBufferRange(GL_ARRAY_BUFFER, 0, 32, GL_MAP_READ_BIT);
    unsigned char *dataPtr = static_cast<unsigned char *>(mappedPtr);
    EXPECT_GL_NO_ERROR();

    EXPECT_EQ(255, dataPtr[0]);
    EXPECT_EQ(0, dataPtr[1]);
    EXPECT_EQ(0, dataPtr[2]);
    EXPECT_EQ(255, dataPtr[3]);

    EXPECT_EQ(1, dataPtr[16]);
    EXPECT_EQ(2, dataPtr[17]);
    EXPECT_EQ(3, dataPtr[18]);
    EXPECT_EQ(4, dataPtr[19]);

    glUnmapBuffer(GL_ARRAY_BUFFER);
    EXPECT_GL_NO_ERROR();
}

class ReadPixelsPBODrawTest : public ReadPixelsPBOTest
{
  protected:
    ReadPixelsPBODrawTest() : mProgram(0), mPositionVBO(0) {}

    void SetUp() override
    {
        ReadPixelsPBOTest::SetUp();

        const char *vertexShaderSrc =
            "attribute vec4 aTest; attribute vec2 aPosition; varying vec4 vTest;\n"
            "void main()\n"
            "{\n"
            "    vTest        = aTest;\n"
            "    gl_Position  = vec4(aPosition, 0.0, 1.0);\n"
            "    gl_PointSize = 1.0;\n"
            "}";

        const char *fragmentShaderSrc =
            "precision mediump float; varying vec4 vTest;\n"
            "void main()\n"
            "{\n"
            "    gl_FragColor = vTest;\n"
            "}";

        mProgram = CompileProgram(vertexShaderSrc, fragmentShaderSrc);
        ASSERT_NE(0u, mProgram);

        glGenBuffers(1, &mPositionVBO);
        glBindBuffer(GL_ARRAY_BUFFER, mPositionVBO);
        glBufferData(GL_ARRAY_BUFFER, 128, NULL, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    void TearDown() override
    {
        glDeleteProgram(mProgram);
        glDeleteBuffers(1, &mPositionVBO);
        ReadPixelsPBOTest::TearDown();
    }

    GLuint mProgram;
    GLuint mPositionVBO;
};

// Test that we can draw with PBO data.
TEST_P(ReadPixelsPBODrawTest, DrawWithPBO)
{
    unsigned char data[4] = { 1, 2, 3, 4 };

    glBindTexture(GL_TEXTURE_2D, mTexture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, data);
    EXPECT_GL_NO_ERROR();

    glBindFramebuffer(GL_READ_FRAMEBUFFER, mFBO);
    EXPECT_GL_NO_ERROR();

    glBindBuffer(GL_PIXEL_PACK_BUFFER, mPBO);
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    EXPECT_GL_NO_ERROR();

    float positionData[] = { 0.5f, 0.5f };

    glUseProgram(mProgram);
    glViewport(0, 0, 1, 1);
    glBindBuffer(GL_ARRAY_BUFFER, mPositionVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, 1 * 2 * 4, positionData);
    EXPECT_GL_NO_ERROR();

    GLint positionLocation = glGetAttribLocation(mProgram, "aPosition");
    EXPECT_NE(-1, positionLocation);

    GLint testLocation = glGetAttribLocation(mProgram, "aTest");
    EXPECT_NE(-1, testLocation);

    glVertexAttribPointer(positionLocation, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(positionLocation);
    EXPECT_GL_NO_ERROR();

    glBindBuffer(GL_ARRAY_BUFFER, mPBO);
    glVertexAttribPointer(testLocation, 4, GL_UNSIGNED_BYTE, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(testLocation);
    EXPECT_GL_NO_ERROR();

    glDrawArrays(GL_POINTS, 0, 1);
    EXPECT_GL_NO_ERROR();

    memset(data, 0, 4);
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, data);
    EXPECT_GL_NO_ERROR();

    EXPECT_EQ(1, data[0]);
    EXPECT_EQ(2, data[1]);
    EXPECT_EQ(3, data[2]);
    EXPECT_EQ(4, data[3]);
}

class ReadPixelsMultisampleTest : public ReadPixelsTest
{
  protected:
    ReadPixelsMultisampleTest() : mFBO(0), mRBO(0), mPBO(0) {}

    void SetUp() override
    {
        ANGLETest::SetUp();

        glGenFramebuffers(1, &mFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, mFBO);

        glGenRenderbuffers(1, &mRBO);
        glBindRenderbuffer(GL_RENDERBUFFER, mRBO);

        glGenBuffers(1, &mPBO);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, mPBO);
        glBufferData(GL_PIXEL_PACK_BUFFER, 4 * getWindowWidth() * getWindowHeight(), nullptr,
                     GL_STATIC_DRAW);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

        ASSERT_GL_NO_ERROR();
    }

    void TearDown() override
    {
        ANGLETest::TearDown();

        glDeleteFramebuffers(1, &mFBO);
        glDeleteRenderbuffers(1, &mRBO);
        glDeleteBuffers(1, &mPBO);
    }

    GLuint mFBO;
    GLuint mRBO;
    GLuint mPBO;
};

// Test ReadPixels from a multisampled framebuffer.
TEST_P(ReadPixelsMultisampleTest, BasicClear)
{
    if (getClientVersion() < 3 && !extensionEnabled("GL_ANGLE_framebuffer_multisample"))
    {
        std::cout << "Test skipped because ES3 or GL_ANGLE_framebuffer_multisample is not available." << std::endl;
        return;
    }

    if (extensionEnabled("GL_ANGLE_framebuffer_multisample"))
    {
        glRenderbufferStorageMultisampleANGLE(GL_RENDERBUFFER, 2, GL_RGBA8, 4, 4);
    }
    else
    {
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, 2, GL_RGBA8, 4, 4);
    }

    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, mRBO);
    ASSERT_GL_NO_ERROR();

    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glBindBuffer(GL_PIXEL_PACK_BUFFER, mPBO);
    EXPECT_GL_NO_ERROR();

    glReadPixels(0, 0, 1, 1, GL_RGBA8, GL_UNSIGNED_BYTE, NULL);
    EXPECT_GL_ERROR(GL_INVALID_OPERATION);
}

class ReadPixelsTextureTest : public ANGLETest
{
  public:
    ReadPixelsTextureTest() : mFBO(0), mTexture(0)
    {
        setWindowWidth(32);
        setWindowHeight(32);
        setConfigRedBits(8);
        setConfigGreenBits(8);
        setConfigBlueBits(8);
        setConfigAlphaBits(8);
    }

    void SetUp() override
    {
        ANGLETest::SetUp();

        glGenTextures(1, &mTexture);
        glGenFramebuffers(1, &mFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, mFBO);
    }

    void TearDown() override
    {
        glDeleteFramebuffers(1, &mFBO);
        glDeleteTextures(1, &mTexture);

        ANGLETest::TearDown();
    }

    void initTexture(GLenum textureTarget,
                     GLint levels,
                     GLint attachmentLevel,
                     GLint attachmentLayer)
    {
        glBindTexture(textureTarget, mTexture);
        glTexStorage3D(textureTarget, levels, GL_RGBA8, 4, 4, 4);
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, mTexture, attachmentLevel,
                                  attachmentLayer);
        initializeTextureData(textureTarget, levels);
    }

    void testRead(GLenum textureTarget, GLint levels, GLint attachmentLevel, GLint attachmentLayer)
    {
        initTexture(textureTarget, levels, attachmentLevel, attachmentLayer);
        verifyColor(attachmentLevel, attachmentLayer);
    }

    void initPBO()
    {
        glGenBuffers(1, &mBuffer);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, mBuffer);
        glBufferData(GL_PIXEL_PACK_BUFFER, sizeof(angle::GLColor), nullptr, GL_STREAM_COPY);
        ASSERT_GL_NO_ERROR();
    }

    void testPBORead(GLenum textureTarget,
                     GLint levels,
                     GLint attachmentLevel,
                     GLint attachmentLayer)
    {
        initPBO();
        initTexture(textureTarget, levels, attachmentLevel, attachmentLayer);
        verifyPBO(attachmentLevel, attachmentLayer);
    }

    // Give each {level,layer} pair a (probably) unique color via random.
    GLuint getColorValue(GLint level, GLint layer)
    {
        mRNG.reseed(level + layer * 32);
        return mRNG.randomUInt();
    }

    void verifyColor(GLint level, GLint layer)
    {
        angle::GLColor colorValue(getColorValue(level, layer));
        EXPECT_PIXEL_COLOR_EQ(0, 0, colorValue);
    }

    void verifyPBO(GLint level, GLint layer)
    {
        glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        angle::GLColor expectedColor(getColorValue(level, layer));
        void *mapPointer =
            glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, sizeof(angle::GLColor), GL_MAP_READ_BIT);
        ASSERT_NE(nullptr, mapPointer);
        angle::GLColor actualColor;
        memcpy(&actualColor, mapPointer, sizeof(angle::GLColor));
        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        ASSERT_GL_NO_ERROR();
        EXPECT_EQ(expectedColor, actualColor);
    }

    void initializeTextureData(GLenum textureTarget, GLint levels)
    {
        for (GLint level = 0; level < levels; ++level)
        {
            GLint mipSize = 4 >> level;
            GLint layers  = (textureTarget == GL_TEXTURE_3D ? mipSize : 4);

            size_t layerSize = mipSize * mipSize;
            std::vector<GLuint> textureData(layers * layerSize);

            for (GLint layer = 0; layer < layers; ++layer)
            {
                GLuint colorValue = getColorValue(level, layer);
                size_t offset = (layer * layerSize);
                std::fill(textureData.begin() + offset, textureData.begin() + offset + layerSize,
                          colorValue);
            }

            glTexSubImage3D(textureTarget, level, 0, 0, 0, mipSize, mipSize, layers, GL_RGBA,
                            GL_UNSIGNED_BYTE, textureData.data());
        }
    }

    angle::RNG mRNG;
    GLuint mFBO;
    GLuint mTexture;
    GLuint mBuffer;
};

// Test 3D attachment readback.
TEST_P(ReadPixelsTextureTest, BasicAttachment3D)
{
    testRead(GL_TEXTURE_3D, 1, 0, 0);
}

// Test 3D attachment readback, non-zero mip.
TEST_P(ReadPixelsTextureTest, MipAttachment3D)
{
    testRead(GL_TEXTURE_3D, 2, 1, 0);
}

// Test 3D attachment readback, non-zero layer.
TEST_P(ReadPixelsTextureTest, LayerAttachment3D)
{
    testRead(GL_TEXTURE_3D, 1, 0, 1);
}

// Test 3D attachment readback, non-zero mip and layer.
TEST_P(ReadPixelsTextureTest, MipLayerAttachment3D)
{
    testRead(GL_TEXTURE_3D, 2, 1, 1);
}

// Test 2D array attachment readback.
TEST_P(ReadPixelsTextureTest, BasicAttachment2DArray)
{
    testRead(GL_TEXTURE_2D_ARRAY, 1, 0, 0);
}

// Test 3D attachment readback, non-zero mip.
TEST_P(ReadPixelsTextureTest, MipAttachment2DArray)
{
    testRead(GL_TEXTURE_2D_ARRAY, 2, 1, 0);
}

// Test 3D attachment readback, non-zero layer.
TEST_P(ReadPixelsTextureTest, LayerAttachment2DArray)
{
    testRead(GL_TEXTURE_2D_ARRAY, 1, 0, 1);
}

// Test 3D attachment readback, non-zero mip and layer.
TEST_P(ReadPixelsTextureTest, MipLayerAttachment2DArray)
{
    testRead(GL_TEXTURE_2D_ARRAY, 2, 1, 1);
}

// Test 3D attachment PBO readback.
TEST_P(ReadPixelsTextureTest, BasicAttachment3DPBO)
{
    testPBORead(GL_TEXTURE_3D, 1, 0, 0);
}

// Test 3D attachment readback, non-zero mip.
TEST_P(ReadPixelsTextureTest, MipAttachment3DPBO)
{
    testPBORead(GL_TEXTURE_3D, 2, 1, 0);
}

// Test 3D attachment readback, non-zero layer.
TEST_P(ReadPixelsTextureTest, LayerAttachment3DPBO)
{
    testPBORead(GL_TEXTURE_3D, 1, 0, 1);
}

// Test 3D attachment readback, non-zero mip and layer.
TEST_P(ReadPixelsTextureTest, MipLayerAttachment3DPBO)
{
    testPBORead(GL_TEXTURE_3D, 2, 1, 1);
}

// Test 2D array attachment readback.
TEST_P(ReadPixelsTextureTest, BasicAttachment2DArrayPBO)
{
    testPBORead(GL_TEXTURE_2D_ARRAY, 1, 0, 0);
}

// Test 3D attachment readback, non-zero mip.
TEST_P(ReadPixelsTextureTest, MipAttachment2DArrayPBO)
{
    testPBORead(GL_TEXTURE_2D_ARRAY, 2, 1, 0);
}

// Test 3D attachment readback, non-zero layer.
TEST_P(ReadPixelsTextureTest, LayerAttachment2DArrayPBO)
{
    testPBORead(GL_TEXTURE_2D_ARRAY, 1, 0, 1);
}

// Test 3D attachment readback, non-zero mip and layer.
TEST_P(ReadPixelsTextureTest, MipLayerAttachment2DArrayPBO)
{
    testPBORead(GL_TEXTURE_2D_ARRAY, 2, 1, 1);
}

}  // anonymous namespace

// Use this to select which configurations (e.g. which renderer, which GLES major version) these tests should be run against.
ANGLE_INSTANTIATE_TEST(ReadPixelsTest, ES2_D3D11(), ES2_OPENGL(), ES2_OPENGLES());
ANGLE_INSTANTIATE_TEST(ReadPixelsPBOTest, ES3_D3D11(), ES3_OPENGL(), ES3_OPENGLES());
ANGLE_INSTANTIATE_TEST(ReadPixelsPBODrawTest, ES3_D3D11(), ES3_OPENGL(), ES3_OPENGLES());
ANGLE_INSTANTIATE_TEST(ReadPixelsMultisampleTest, ES3_D3D11(), ES3_OPENGL(), ES3_OPENGLES());
ANGLE_INSTANTIATE_TEST(ReadPixelsTextureTest, ES3_D3D11(), ES3_OPENGL(), ES3_OPENGLES());

//
// Copyright 2015 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

#include "test_utils/ANGLETest.h"

#include "libANGLE/Context.h"
#include "libANGLE/Program.h"

using namespace angle;

class GLSLTest : public ANGLETest
{
  protected:
    GLSLTest()
    {
        setWindowWidth(128);
        setWindowHeight(128);
        setConfigRedBits(8);
        setConfigGreenBits(8);
        setConfigBlueBits(8);
        setConfigAlphaBits(8);
    }

    virtual void SetUp()
    {
        ANGLETest::SetUp();

        mSimpleVSSource = SHADER_SOURCE
        (
            attribute vec4 inputAttribute;
            void main()
            {
                gl_Position = inputAttribute;
            }
        );
    }

    std::string GenerateVaryingType(GLint vectorSize)
    {
        char varyingType[10];

        if (vectorSize == 1)
        {
            sprintf(varyingType, "float");
        }
        else
        {
            sprintf(varyingType, "vec%d", vectorSize);
        }

        return std::string(varyingType);
    }

    std::string GenerateVectorVaryingDeclaration(GLint vectorSize, GLint arraySize, GLint id)
    {
        char buff[100];

        if (arraySize == 1)
        {
            sprintf(buff, "varying %s v%d;\n", GenerateVaryingType(vectorSize).c_str(), id);
        }
        else
        {
            sprintf(buff, "varying %s v%d[%d];\n", GenerateVaryingType(vectorSize).c_str(), id, arraySize);
        }

        return std::string(buff);
    }

    std::string GenerateVectorVaryingSettingCode(GLint vectorSize, GLint arraySize, GLint id)
    {
        std::string returnString;
        char buff[100];

        if (arraySize == 1)
        {
            sprintf(buff, "\t v%d = %s(1.0);\n", id, GenerateVaryingType(vectorSize).c_str());
            returnString += buff;
        }
        else
        {
            for (int i = 0; i < arraySize; i++)
            {
                sprintf(buff, "\t v%d[%d] = %s(1.0);\n", id, i, GenerateVaryingType(vectorSize).c_str());
                returnString += buff;
            }
        }

        return returnString;
    }

    std::string GenerateVectorVaryingUseCode(GLint arraySize, GLint id)
    {
        if (arraySize == 1)
        {
            char buff[100];
            sprintf(buff, "v%d + ", id);
            return std::string(buff);
        }
        else
        {
            std::string returnString;
            for (int i = 0; i < arraySize; i++)
            {
                char buff[100];
                sprintf(buff, "v%d[%d] + ", id, i);
                returnString += buff;
            }
            return returnString;
        }
    }

    void GenerateGLSLWithVaryings(GLint floatCount, GLint floatArrayCount, GLint vec2Count, GLint vec2ArrayCount, GLint vec3Count, GLint vec3ArrayCount,
                                  GLint vec4Count, GLint vec4ArrayCount, bool useFragCoord, bool usePointCoord, bool usePointSize,
                                  std::string* fragmentShader, std::string* vertexShader)
    {
        // Generate a string declaring the varyings, to share between the fragment shader and the vertex shader.
        std::string varyingDeclaration;

        unsigned int varyingCount = 0;

        for (GLint i = 0; i < floatCount; i++)
        {
            varyingDeclaration += GenerateVectorVaryingDeclaration(1, 1, varyingCount);
            varyingCount += 1;
        }

        for (GLint i = 0; i < floatArrayCount; i++)
        {
            varyingDeclaration += GenerateVectorVaryingDeclaration(1, 2, varyingCount);
            varyingCount += 1;
        }

        for (GLint i = 0; i < vec2Count; i++)
        {
            varyingDeclaration += GenerateVectorVaryingDeclaration(2, 1, varyingCount);
            varyingCount += 1;
        }

        for (GLint i = 0; i < vec2ArrayCount; i++)
        {
            varyingDeclaration += GenerateVectorVaryingDeclaration(2, 2, varyingCount);
            varyingCount += 1;
        }

        for (GLint i = 0; i < vec3Count; i++)
        {
            varyingDeclaration += GenerateVectorVaryingDeclaration(3, 1, varyingCount);
            varyingCount += 1;
        }

        for (GLint i = 0; i < vec3ArrayCount; i++)
        {
            varyingDeclaration += GenerateVectorVaryingDeclaration(3, 2, varyingCount);
            varyingCount += 1;
        }

        for (GLint i = 0; i < vec4Count; i++)
        {
            varyingDeclaration += GenerateVectorVaryingDeclaration(4, 1, varyingCount);
            varyingCount += 1;
        }

        for (GLint i = 0; i < vec4ArrayCount; i++)
        {
            varyingDeclaration += GenerateVectorVaryingDeclaration(4, 2, varyingCount);
            varyingCount += 1;
        }

        // Generate the vertex shader
        vertexShader->clear();
        vertexShader->append(varyingDeclaration);
        vertexShader->append("\nvoid main()\n{\n");

        unsigned int currentVSVarying = 0;

        for (GLint i = 0; i < floatCount; i++)
        {
            vertexShader->append(GenerateVectorVaryingSettingCode(1, 1, currentVSVarying));
            currentVSVarying += 1;
        }

        for (GLint i = 0; i < floatArrayCount; i++)
        {
            vertexShader->append(GenerateVectorVaryingSettingCode(1, 2, currentVSVarying));
            currentVSVarying += 1;
        }

        for (GLint i = 0; i < vec2Count; i++)
        {
            vertexShader->append(GenerateVectorVaryingSettingCode(2, 1, currentVSVarying));
            currentVSVarying += 1;
        }

        for (GLint i = 0; i < vec2ArrayCount; i++)
        {
            vertexShader->append(GenerateVectorVaryingSettingCode(2, 2, currentVSVarying));
            currentVSVarying += 1;
        }

        for (GLint i = 0; i < vec3Count; i++)
        {
            vertexShader->append(GenerateVectorVaryingSettingCode(3, 1, currentVSVarying));
            currentVSVarying += 1;
        }

        for (GLint i = 0; i < vec3ArrayCount; i++)
        {
            vertexShader->append(GenerateVectorVaryingSettingCode(3, 2, currentVSVarying));
            currentVSVarying += 1;
        }

        for (GLint i = 0; i < vec4Count; i++)
        {
            vertexShader->append(GenerateVectorVaryingSettingCode(4, 1, currentVSVarying));
            currentVSVarying += 1;
        }

        for (GLint i = 0; i < vec4ArrayCount; i++)
        {
            vertexShader->append(GenerateVectorVaryingSettingCode(4, 2, currentVSVarying));
            currentVSVarying += 1;
        }

        if (usePointSize)
        {
            vertexShader->append("gl_PointSize = 1.0;\n");
        }

        vertexShader->append("}\n");

        // Generate the fragment shader
        fragmentShader->clear();
        fragmentShader->append("precision highp float;\n");
        fragmentShader->append(varyingDeclaration);
        fragmentShader->append("\nvoid main() \n{ \n\tvec4 retColor = vec4(0,0,0,0);\n");

        unsigned int currentFSVarying = 0;

        // Make use of the float varyings
        fragmentShader->append("\tretColor += vec4(");

        for (GLint i = 0; i < floatCount; i++)
        {
            fragmentShader->append(GenerateVectorVaryingUseCode(1, currentFSVarying));
            currentFSVarying += 1;
        }

        for (GLint i = 0; i < floatArrayCount; i++)
        {
            fragmentShader->append(GenerateVectorVaryingUseCode(2, currentFSVarying));
            currentFSVarying += 1;
        }

        fragmentShader->append("0.0, 0.0, 0.0, 0.0);\n");

        // Make use of the vec2 varyings
        fragmentShader->append("\tretColor += vec4(");

        for (GLint i = 0; i < vec2Count; i++)
        {
            fragmentShader->append(GenerateVectorVaryingUseCode(1, currentFSVarying));
            currentFSVarying += 1;
        }

        for (GLint i = 0; i < vec2ArrayCount; i++)
        {
            fragmentShader->append(GenerateVectorVaryingUseCode(2, currentFSVarying));
            currentFSVarying += 1;
        }

        fragmentShader->append("vec2(0.0, 0.0), 0.0, 0.0);\n");

        // Make use of the vec3 varyings
        fragmentShader->append("\tretColor += vec4(");

        for (GLint i = 0; i < vec3Count; i++)
        {
            fragmentShader->append(GenerateVectorVaryingUseCode(1, currentFSVarying));
            currentFSVarying += 1;
        }

        for (GLint i = 0; i < vec3ArrayCount; i++)
        {
            fragmentShader->append(GenerateVectorVaryingUseCode(2, currentFSVarying));
            currentFSVarying += 1;
        }

        fragmentShader->append("vec3(0.0, 0.0, 0.0), 0.0);\n");

        // Make use of the vec4 varyings
        fragmentShader->append("\tretColor += ");

        for (GLint i = 0; i < vec4Count; i++)
        {
            fragmentShader->append(GenerateVectorVaryingUseCode(1, currentFSVarying));
            currentFSVarying += 1;
        }

        for (GLint i = 0; i < vec4ArrayCount; i++)
        {
            fragmentShader->append(GenerateVectorVaryingUseCode(2, currentFSVarying));
            currentFSVarying += 1;
        }

        fragmentShader->append("vec4(0.0, 0.0, 0.0, 0.0);\n");

        // Set gl_FragColor, and use special variables if requested
        fragmentShader->append("\tgl_FragColor = retColor");
        
        if (useFragCoord)
        {
            fragmentShader->append(" + gl_FragCoord");
        }

        if (usePointCoord)
        {
            fragmentShader->append(" + vec4(gl_PointCoord, 0.0, 0.0)");
        }

        fragmentShader->append(";\n}");
    }

    void VaryingTestBase(GLint floatCount, GLint floatArrayCount, GLint vec2Count, GLint vec2ArrayCount, GLint vec3Count, GLint vec3ArrayCount,
                         GLint vec4Count, GLint vec4ArrayCount, bool useFragCoord, bool usePointCoord, bool usePointSize, bool expectSuccess)
    {
        std::string fragmentShaderSource;
        std::string vertexShaderSource;

        GenerateGLSLWithVaryings(floatCount, floatArrayCount, vec2Count, vec2ArrayCount, vec3Count, vec3ArrayCount,
                                 vec4Count, vec4ArrayCount, useFragCoord, usePointCoord, usePointSize,
                                 &fragmentShaderSource, &vertexShaderSource);

        GLuint program = CompileProgram(vertexShaderSource, fragmentShaderSource);

        if (expectSuccess)
        {
            EXPECT_NE(0u, program);
        }
        else
        {
            EXPECT_EQ(0u, program);
        }
    }

    void CompileGLSLWithUniformsAndSamplers(GLint vertexUniformCount,
                                            GLint fragmentUniformCount,
                                            GLint vertexSamplersCount,
                                            GLint fragmentSamplersCount,
                                            bool expectSuccess)
    {
        std::stringstream vertexShader;
        std::stringstream fragmentShader;

        // Generate the vertex shader
        vertexShader << "precision mediump float;\n";

        for (int i = 0; i < vertexUniformCount; i++)
        {
            vertexShader << "uniform vec4 v" << i << ";\n";
        }

        for (int i = 0; i < vertexSamplersCount; i++)
        {
            vertexShader << "uniform sampler2D s" << i << ";\n";
        }

        vertexShader << "void main()\n{\n";

        for (int i = 0; i < vertexUniformCount; i++)
        {
            vertexShader << "    gl_Position +=  v" << i << ";\n";
        }

        for (int i = 0; i < vertexSamplersCount; i++)
        {
            vertexShader << "    gl_Position +=  texture2D(s" << i << ", vec2(0.0, 0.0));\n";
        }

        if (vertexUniformCount == 0 && vertexSamplersCount == 0)
        {
            vertexShader << "   gl_Position = vec4(0.0);\n";
        }

        vertexShader << "}\n";

        // Generate the fragment shader
        fragmentShader << "precision mediump float;\n";

        for (int i = 0; i < fragmentUniformCount; i++)
        {
            fragmentShader << "uniform vec4 v" << i << ";\n";
        }

        for (int i = 0; i < fragmentSamplersCount; i++)
        {
            fragmentShader << "uniform sampler2D s" << i << ";\n";
        }

        fragmentShader << "void main()\n{\n";

        for (int i = 0; i < fragmentUniformCount; i++)
        {
            fragmentShader << "    gl_FragColor +=  v" << i << ";\n";
        }

        for (int i = 0; i < fragmentSamplersCount; i++)
        {
            fragmentShader << "    gl_FragColor +=  texture2D(s" << i << ", vec2(0.0, 0.0));\n";
        }

        if (fragmentUniformCount == 0 && fragmentSamplersCount == 0)
        {
            fragmentShader << "    gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);\n";
        }

        fragmentShader << "}\n";

        GLuint program = CompileProgram(vertexShader.str(), fragmentShader.str());

        if (expectSuccess)
        {
            EXPECT_NE(0u, program);
        }
        else
        {
            EXPECT_EQ(0u, program);
        }
    }

    std::string mSimpleVSSource;
};

class GLSLTest_ES3 : public GLSLTest
{
};

TEST_P(GLSLTest, NamelessScopedStructs)
{
    const std::string fragmentShaderSource = SHADER_SOURCE
    (
        precision mediump float;

        void main()
        {
            struct
            {
                float q;
            } b;

            gl_FragColor = vec4(1, 0, 0, 1);
            gl_FragColor.a += b.q;
        }
    );

    GLuint program = CompileProgram(mSimpleVSSource, fragmentShaderSource);
    EXPECT_NE(0u, program);
}

TEST_P(GLSLTest, ScopedStructsOrderBug)
{
#if defined(__APPLE__)
    // TODO(geofflang): Find out why this doesn't compile on Apple OpenGL drivers
    // (http://anglebug.com/1292)
    if (getPlatformRenderer() == EGL_PLATFORM_ANGLE_TYPE_OPENGL_ANGLE)
    {
        std::cout << "Test disabled on Apple OpenGL." << std::endl;
        return;
    }
#endif

    // TODO(geofflang): Find out why this doesn't compile on AMD OpenGL drivers
    // (http://anglebug.com/1291)
    if (isAMD() && getPlatformRenderer() == EGL_PLATFORM_ANGLE_TYPE_OPENGL_ANGLE)
    {
        std::cout << "Test disabled on AMD OpenGL." << std::endl;
        return;
    }

    const std::string fragmentShaderSource = SHADER_SOURCE
    (
        precision mediump float;

        struct T
        {
            float f;
        };

        void main()
        {
            T a;

            struct T
            {
                float q;
            };

            T b;

            gl_FragColor = vec4(1, 0, 0, 1);
            gl_FragColor.a += a.f;
            gl_FragColor.a += b.q;
        }
    );

    GLuint program = CompileProgram(mSimpleVSSource, fragmentShaderSource);
    EXPECT_NE(0u, program);
}

TEST_P(GLSLTest, ScopedStructsBug)
{
    const std::string fragmentShaderSource = SHADER_SOURCE
    (
        precision mediump float;

        struct T_0
        {
            float f;
        };

        void main()
        {
            gl_FragColor = vec4(1, 0, 0, 1);

            struct T
            {
                vec2 v;
            };

            T_0 a;
            T b;

            gl_FragColor.a += a.f;
            gl_FragColor.a += b.v.x;
        }
    );

    GLuint program = CompileProgram(mSimpleVSSource, fragmentShaderSource);
    EXPECT_NE(0u, program);
}

TEST_P(GLSLTest, DxPositionBug)
{
    const std::string &vertexShaderSource = SHADER_SOURCE
    (
        attribute vec4 inputAttribute;
        varying float dx_Position;
        void main()
        {
            gl_Position = vec4(inputAttribute);
            dx_Position = 0.0;
        }
    );

    const std::string &fragmentShaderSource = SHADER_SOURCE
    (
        precision mediump float;

        varying float dx_Position;

        void main()
        {
            gl_FragColor = vec4(dx_Position, 0, 0, 1);
        }
    );

    GLuint program = CompileProgram(vertexShaderSource, fragmentShaderSource);
    EXPECT_NE(0u, program);
}

TEST_P(GLSLTest, ElseIfRewriting)
{
    const std::string &vertexShaderSource =
        "attribute vec4 a_position;\n"
        "varying float v;\n"
        "void main() {\n"
        "  gl_Position = a_position;\n"
        "  v = 1.0;\n"
        "  if (a_position.x <= 0.5) {\n"
        "    v = 0.0;\n"
        "  } else if (a_position.x >= 0.5) {\n"
        "    v = 2.0;\n"
        "  }\n"
        "}\n";

    const std::string &fragmentShaderSource =
        "precision highp float;\n"
        "varying float v;\n"
        "void main() {\n"
        "  vec4 color = vec4(1.0, 0.0, 0.0, 1.0);\n"
        "  if (v >= 1.0) color = vec4(0.0, 1.0, 0.0, 1.0);\n"
        "  if (v >= 2.0) color = vec4(0.0, 0.0, 1.0, 1.0);\n"
        "  gl_FragColor = color;\n"
        "}\n";

    GLuint program = CompileProgram(vertexShaderSource, fragmentShaderSource);
    ASSERT_NE(0u, program);

    drawQuad(program, "a_position", 0.5f);

    EXPECT_PIXEL_EQ(0, 0, 255, 0, 0, 255);
    EXPECT_PIXEL_EQ(getWindowWidth()-1, 0, 0, 255, 0, 255);
}

TEST_P(GLSLTest, TwoElseIfRewriting)
{
    const std::string &vertexShaderSource =
        "attribute vec4 a_position;\n"
        "varying float v;\n"
        "void main() {\n"
        "  gl_Position = a_position;\n"
        "  if (a_position.x == 0.0) {\n"
        "    v = 1.0;\n"
        "  } else if (a_position.x > 0.5) {\n"
        "    v = 0.0;\n"
        "  } else if (a_position.x > 0.75) {\n"
        "    v = 0.5;\n"
        "  }\n"
        "}\n";

    const std::string &fragmentShaderSource =
        "precision highp float;\n"
        "varying float v;\n"
        "void main() {\n"
        "  gl_FragColor = vec4(v, 0.0, 0.0, 1.0);\n"
        "}\n";

    GLuint program = CompileProgram(vertexShaderSource, fragmentShaderSource);
    EXPECT_NE(0u, program);
}

TEST_P(GLSLTest, InvariantVaryingOut)
{
    // TODO(geofflang): Some OpenGL drivers have compile errors when varyings do not have matching
    // invariant attributes (http://anglebug.com/1293)
    if (getPlatformRenderer() == EGL_PLATFORM_ANGLE_TYPE_OPENGL_ANGLE)
    {
        std::cout << "Test disabled on OpenGL." << std::endl;
        return;
    }

    const std::string fragmentShaderSource = SHADER_SOURCE
    (
        precision mediump float;
        varying float v_varying;
        void main() { gl_FragColor = vec4(v_varying, 0, 0, 1.0); }
    );

    const std::string vertexShaderSource = SHADER_SOURCE
    (
        attribute vec4 a_position;
        invariant varying float v_varying;
        void main() { v_varying = a_position.x; gl_Position = a_position; }
    );

    GLuint program = CompileProgram(vertexShaderSource, fragmentShaderSource);
    EXPECT_NE(0u, program);
}

TEST_P(GLSLTest, FrontFacingAndVarying)
{
    EGLPlatformParameters platform = GetParam().eglParameters;

    const std::string vertexShaderSource = SHADER_SOURCE
    (
        attribute vec4 a_position;
        varying float v_varying;
        void main()
        {
            v_varying = a_position.x;
            gl_Position = a_position;
        }
    );

    const std::string fragmentShaderSource = SHADER_SOURCE
    (
        precision mediump float;
        varying float v_varying;
        void main()
        {
            vec4 c;

            if (gl_FrontFacing)
            {
                c = vec4(v_varying, 0, 0, 1.0);
            }
            else
            {
                c = vec4(0, v_varying, 0, 1.0);
            }
            gl_FragColor = c;
        }
    );

    GLuint program = CompileProgram(vertexShaderSource, fragmentShaderSource);

    // Compilation should fail on D3D11 feature level 9_3, since gl_FrontFacing isn't supported.
    if (platform.renderer == EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE)
    {
        if (platform.majorVersion == 9 && platform.minorVersion == 3)
        {
            EXPECT_EQ(0u, program);
            return;
        }
    }

    // Otherwise, compilation should succeed
    EXPECT_NE(0u, program);
}

TEST_P(GLSLTest, InvariantVaryingIn)
{
    // TODO(geofflang): Some OpenGL drivers have compile errors when varyings do not have matching
    // invariant attributes (http://anglebug.com/1293)
    if (getPlatformRenderer() == EGL_PLATFORM_ANGLE_TYPE_OPENGL_ANGLE)
    {
        std::cout << "Test disabled on OpenGL." << std::endl;
        return;
    }

    const std::string fragmentShaderSource = SHADER_SOURCE
    (
        precision mediump float;
        invariant varying float v_varying;
        void main() { gl_FragColor = vec4(v_varying, 0, 0, 1.0); }
    );

    const std::string vertexShaderSource = SHADER_SOURCE
    (
        attribute vec4 a_position;
        varying float v_varying;
        void main() { v_varying = a_position.x; gl_Position = a_position; }
    );

    GLuint program = CompileProgram(vertexShaderSource, fragmentShaderSource);
    EXPECT_NE(0u, program);
}

TEST_P(GLSLTest, InvariantVaryingBoth)
{
    const std::string fragmentShaderSource = SHADER_SOURCE
    (
        precision mediump float;
        invariant varying float v_varying;
        void main() { gl_FragColor = vec4(v_varying, 0, 0, 1.0); }
    );

    const std::string vertexShaderSource = SHADER_SOURCE
    (
        attribute vec4 a_position;
        invariant varying float v_varying;
        void main() { v_varying = a_position.x; gl_Position = a_position; }
    );

    GLuint program = CompileProgram(vertexShaderSource, fragmentShaderSource);
    EXPECT_NE(0u, program);
}

TEST_P(GLSLTest, InvariantGLPosition)
{
    const std::string fragmentShaderSource = SHADER_SOURCE
    (
        precision mediump float;
        varying float v_varying;
        void main() { gl_FragColor = vec4(v_varying, 0, 0, 1.0); }
    );

    const std::string vertexShaderSource = SHADER_SOURCE
    (
        attribute vec4 a_position;
        invariant gl_Position;
        varying float v_varying;
        void main() { v_varying = a_position.x; gl_Position = a_position; }
    );

    GLuint program = CompileProgram(vertexShaderSource, fragmentShaderSource);
    EXPECT_NE(0u, program);
}

TEST_P(GLSLTest, InvariantAll)
{
    // TODO(geofflang): Some OpenGL drivers have compile errors when varyings do not have matching
    // invariant attributes (http://anglebug.com/1293)
    if (getPlatformRenderer() == EGL_PLATFORM_ANGLE_TYPE_OPENGL_ANGLE)
    {
        std::cout << "Test disabled on OpenGL." << std::endl;
        return;
    }

    const std::string fragmentShaderSource = SHADER_SOURCE
    (
        precision mediump float;
        varying float v_varying;
        void main() { gl_FragColor = vec4(v_varying, 0, 0, 1.0); }
    );

    const std::string vertexShaderSource =
        "#pragma STDGL invariant(all)\n"
        "attribute vec4 a_position;\n"
        "varying float v_varying;\n"
        "void main() { v_varying = a_position.x; gl_Position = a_position; }\n";

    GLuint program = CompileProgram(vertexShaderSource, fragmentShaderSource);
    EXPECT_NE(0u, program);
}

TEST_P(GLSLTest, MaxVaryingVec4)
{
#if defined(__APPLE__)
    // TODO(geofflang): Find out why this doesn't compile on Apple AND OpenGL drivers
    // (http://anglebug.com/1291)
    if (isAMD() && getPlatformRenderer() == EGL_PLATFORM_ANGLE_TYPE_OPENGL_ANGLE)
    {
        std::cout << "Test disabled on Apple AMD OpenGL." << std::endl;
        return;
    }
#endif

    GLint maxVaryings = 0;
    glGetIntegerv(GL_MAX_VARYING_VECTORS, &maxVaryings);

    VaryingTestBase(0, 0, 0, 0, 0, 0, maxVaryings, 0, false, false, false, true);
}

TEST_P(GLSLTest, MaxMinusTwoVaryingVec4PlusTwoSpecialVariables)
{
    GLint maxVaryings = 0;
    glGetIntegerv(GL_MAX_VARYING_VECTORS, &maxVaryings);

    // Generate shader code that uses gl_FragCoord and gl_PointCoord, two special fragment shader variables.
    VaryingTestBase(0, 0, 0, 0, 0, 0, maxVaryings - 2, 0, true, true, false, true);
}

TEST_P(GLSLTest, MaxMinusTwoVaryingVec4PlusThreeSpecialVariables)
{
    // TODO(geofflang): Figure out why this fails on OpenGL AMD (http://anglebug.com/1291)
    if (isAMD() && getPlatformRenderer() == EGL_PLATFORM_ANGLE_TYPE_OPENGL_ANGLE)
    {
        std::cout << "Test disabled on OpenGL." << std::endl;
        return;
    }

    GLint maxVaryings = 0;
    glGetIntegerv(GL_MAX_VARYING_VECTORS, &maxVaryings);

    // Generate shader code that uses gl_FragCoord, gl_PointCoord and gl_PointSize.
    VaryingTestBase(0, 0, 0, 0, 0, 0, maxVaryings - 2, 0, true, true, true, true);
}

// Disabled because drivers are allowed to successfully compile shaders that have more than the
// maximum number of varyings. (http://anglebug.com/1296)
TEST_P(GLSLTest, DISABLED_MaxVaryingVec4PlusFragCoord)
{
    GLint maxVaryings = 0;
    glGetIntegerv(GL_MAX_VARYING_VECTORS, &maxVaryings);

    // Generate shader code that uses gl_FragCoord, a special fragment shader variables.
    // This test should fail, since we are really using (maxVaryings + 1) varyings.
    VaryingTestBase(0, 0, 0, 0, 0, 0, maxVaryings, 0, true, false, false, false);
}

// Disabled because drivers are allowed to successfully compile shaders that have more than the
// maximum number of varyings. (http://anglebug.com/1296)
TEST_P(GLSLTest, DISABLED_MaxVaryingVec4PlusPointCoord)
{
    GLint maxVaryings = 0;
    glGetIntegerv(GL_MAX_VARYING_VECTORS, &maxVaryings);

    // Generate shader code that uses gl_FragCoord, a special fragment shader variables.
    // This test should fail, since we are really using (maxVaryings + 1) varyings.
    VaryingTestBase(0, 0, 0, 0, 0, 0, maxVaryings, 0, false, true, false, false);
}

TEST_P(GLSLTest, MaxVaryingVec3)
{
    GLint maxVaryings = 0;
    glGetIntegerv(GL_MAX_VARYING_VECTORS, &maxVaryings);

    VaryingTestBase(0, 0, 0, 0, maxVaryings, 0, 0, 0, false, false, false, true);
}

TEST_P(GLSLTest, MaxVaryingVec3Array)
{
    GLint maxVaryings = 0;
    glGetIntegerv(GL_MAX_VARYING_VECTORS, &maxVaryings);

    VaryingTestBase(0, 0, 0, 0, 0, maxVaryings / 2, 0, 0, false, false, false, true);
}

// Disabled because of a failure in D3D9
TEST_P(GLSLTest, MaxVaryingVec3AndOneFloat)
{
    if (isD3D9())
    {
        std::cout << "Test disabled on D3D9." << std::endl;
        return;
    }

    GLint maxVaryings = 0;
    glGetIntegerv(GL_MAX_VARYING_VECTORS, &maxVaryings);

    VaryingTestBase(1, 0, 0, 0, maxVaryings, 0, 0, 0, false, false, false, true);
}

// Disabled because of a failure in D3D9
TEST_P(GLSLTest, MaxVaryingVec3ArrayAndOneFloatArray)
{
    if (isD3D9())
    {
        std::cout << "Test disabled on D3D9." << std::endl;
        return;
    }

    GLint maxVaryings = 0;
    glGetIntegerv(GL_MAX_VARYING_VECTORS, &maxVaryings);

    VaryingTestBase(0, 1, 0, 0, 0, maxVaryings / 2, 0, 0, false, false, false, true);
}

// Disabled because of a failure in D3D9
TEST_P(GLSLTest, TwiceMaxVaryingVec2)
{
    if (isD3D9())
    {
        std::cout << "Test disabled on D3D9." << std::endl;
        return;
    }

    if (getPlatformRenderer() == EGL_PLATFORM_ANGLE_TYPE_OPENGLES_ANGLE)
    {
        // TODO(geofflang): Figure out why this fails on NVIDIA's GLES driver
        std::cout << "Test disabled on OpenGL ES." << std::endl;
        return;
    }

#if defined(__APPLE__)
    // TODO(geofflang): Find out why this doesn't compile on Apple AND OpenGL drivers
    // (http://anglebug.com/1291)
    if (isAMD() && getPlatformRenderer() == EGL_PLATFORM_ANGLE_TYPE_OPENGL_ANGLE)
    {
        std::cout << "Test disabled on Apple AMD OpenGL." << std::endl;
        return;
    }
#endif

    GLint maxVaryings = 0;
    glGetIntegerv(GL_MAX_VARYING_VECTORS, &maxVaryings);

    VaryingTestBase(0, 0, 2 * maxVaryings, 0, 0, 0, 0, 0, false, false, false, true);
}

// Disabled because of a failure in D3D9
TEST_P(GLSLTest, MaxVaryingVec2Arrays)
{
    if (isD3DSM3())
    {
        std::cout << "Test disabled on SM3." << std::endl;
        return;
    }

    if (getPlatformRenderer() == EGL_PLATFORM_ANGLE_TYPE_OPENGLES_ANGLE)
    {
        // TODO(geofflang): Figure out why this fails on NVIDIA's GLES driver
        std::cout << "Test disabled on OpenGL ES." << std::endl;
        return;
    }

#if defined(__APPLE__)
    // TODO(geofflang): Find out why this doesn't compile on Apple AND OpenGL drivers
    // (http://anglebug.com/1291)
    if (isAMD() && getPlatformRenderer() == EGL_PLATFORM_ANGLE_TYPE_OPENGL_ANGLE)
    {
        std::cout << "Test disabled on Apple AMD OpenGL." << std::endl;
        return;
    }
#endif

    GLint maxVaryings = 0;
    glGetIntegerv(GL_MAX_VARYING_VECTORS, &maxVaryings);

    VaryingTestBase(0, 0, 0, maxVaryings, 0, 0, 0, 0, false, false, false, true);
}

// Disabled because drivers are allowed to successfully compile shaders that have more than the
// maximum number of varyings. (http://anglebug.com/1296)
TEST_P(GLSLTest, DISABLED_MaxPlusOneVaryingVec3)
{
    GLint maxVaryings = 0;
    glGetIntegerv(GL_MAX_VARYING_VECTORS, &maxVaryings);

    VaryingTestBase(0, 0, 0, 0, maxVaryings + 1, 0, 0, 0, false, false, false, false);
}

// Disabled because drivers are allowed to successfully compile shaders that have more than the
// maximum number of varyings. (http://anglebug.com/1296)
TEST_P(GLSLTest, DISABLED_MaxPlusOneVaryingVec3Array)
{
    GLint maxVaryings = 0;
    glGetIntegerv(GL_MAX_VARYING_VECTORS, &maxVaryings);

    VaryingTestBase(0, 0, 0, 0, 0, maxVaryings / 2 + 1, 0, 0, false, false, false, false);
}

// Disabled because drivers are allowed to successfully compile shaders that have more than the
// maximum number of varyings. (http://anglebug.com/1296)
TEST_P(GLSLTest, DISABLED_MaxVaryingVec3AndOneVec2)
{
    GLint maxVaryings = 0;
    glGetIntegerv(GL_MAX_VARYING_VECTORS, &maxVaryings);

    VaryingTestBase(0, 0, 1, 0, maxVaryings, 0, 0, 0, false, false, false, false);
}

// Disabled because drivers are allowed to successfully compile shaders that have more than the
// maximum number of varyings. (http://anglebug.com/1296)
TEST_P(GLSLTest, DISABLED_MaxPlusOneVaryingVec2)
{
    GLint maxVaryings = 0;
    glGetIntegerv(GL_MAX_VARYING_VECTORS, &maxVaryings);

    VaryingTestBase(0, 0, 2 * maxVaryings + 1, 0, 0, 0, 0, 0, false, false, false, false);
}

// Disabled because drivers are allowed to successfully compile shaders that have more than the
// maximum number of varyings. (http://anglebug.com/1296)
TEST_P(GLSLTest, DISABLED_MaxVaryingVec3ArrayAndMaxPlusOneFloatArray)
{
    GLint maxVaryings = 0;
    glGetIntegerv(GL_MAX_VARYING_VECTORS, &maxVaryings);

    VaryingTestBase(0, maxVaryings / 2 + 1, 0, 0, 0, 0, 0, maxVaryings / 2, false, false, false, false);
}

// Verify shader source with a fixed length that is less than the null-terminated length will compile.
TEST_P(GLSLTest, FixedShaderLength)
{
    GLuint shader = glCreateShader(GL_FRAGMENT_SHADER);

    const std::string appendGarbage = "abcasdfasdfasdfasdfasdf";
    const std::string source = "void main() { gl_FragColor = vec4(0, 0, 0, 0); }" + appendGarbage;
    const char *sourceArray[1] = { source.c_str() };
    GLint lengths[1] = { static_cast<GLint>(source.length() - appendGarbage.length()) };
    glShaderSource(shader, static_cast<GLsizei>(ArraySize(sourceArray)), sourceArray, lengths);
    glCompileShader(shader);

    GLint compileResult;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compileResult);
    EXPECT_NE(compileResult, 0);
}

// Verify that a negative shader source length is treated as a null-terminated length.
TEST_P(GLSLTest, NegativeShaderLength)
{
    GLuint shader = glCreateShader(GL_FRAGMENT_SHADER);

    const char *sourceArray[1] = { "void main() { gl_FragColor = vec4(0, 0, 0, 0); }" };
    GLint lengths[1] = { -10 };
    glShaderSource(shader, static_cast<GLsizei>(ArraySize(sourceArray)), sourceArray, lengths);
    glCompileShader(shader);

    GLint compileResult;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compileResult);
    EXPECT_NE(compileResult, 0);
}

// Verify that a length array with mixed positive and negative values compiles.
TEST_P(GLSLTest, MixedShaderLengths)
{
    GLuint shader = glCreateShader(GL_FRAGMENT_SHADER);

    const char *sourceArray[] =
    {
        "void main()",
        "{",
        "    gl_FragColor = vec4(0, 0, 0, 0);",
        "}",
    };
    GLint lengths[] =
    {
        -10,
        1,
        static_cast<GLint>(strlen(sourceArray[2])),
        -1,
    };
    ASSERT_EQ(ArraySize(sourceArray), ArraySize(lengths));

    glShaderSource(shader, static_cast<GLsizei>(ArraySize(sourceArray)), sourceArray, lengths);
    glCompileShader(shader);

    GLint compileResult;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compileResult);
    EXPECT_NE(compileResult, 0);
}

// Verify that zero-length shader source does not affect shader compilation.
TEST_P(GLSLTest, ZeroShaderLength)
{
    GLuint shader = glCreateShader(GL_FRAGMENT_SHADER);

    const char *sourceArray[] =
    {
        "adfasdf",
        "34534",
        "void main() { gl_FragColor = vec4(0, 0, 0, 0); }",
        "",
        "asdfasdfsdsdf",
    };
    GLint lengths[] =
    {
        0,
        0,
        -1,
        0,
        0,
    };
    ASSERT_EQ(ArraySize(sourceArray), ArraySize(lengths));

    glShaderSource(shader, static_cast<GLsizei>(ArraySize(sourceArray)), sourceArray, lengths);
    glCompileShader(shader);

    GLint compileResult;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compileResult);
    EXPECT_NE(compileResult, 0);
}

// Tests that bad index expressions don't crash ANGLE's translator.
// https://code.google.com/p/angleproject/issues/detail?id=857
TEST_P(GLSLTest, BadIndexBug)
{
    const std::string &fragmentShaderSourceVec =
        "precision mediump float;\n"
        "uniform vec4 uniformVec;\n"
        "void main()\n"
        "{\n"
        "    gl_FragColor = vec4(uniformVec[int()]);\n"
        "}";

    GLuint shader = CompileShader(GL_FRAGMENT_SHADER, fragmentShaderSourceVec);
    EXPECT_EQ(0u, shader);

    if (shader != 0)
    {
        glDeleteShader(shader);
    }

    const std::string &fragmentShaderSourceMat =
        "precision mediump float;\n"
        "uniform mat4 uniformMat;\n"
        "void main()\n"
        "{\n"
        "    gl_FragColor = vec4(uniformMat[int()]);\n"
        "}";

    shader = CompileShader(GL_FRAGMENT_SHADER, fragmentShaderSourceMat);
    EXPECT_EQ(0u, shader);

    if (shader != 0)
    {
        glDeleteShader(shader);
    }

    const std::string &fragmentShaderSourceArray =
        "precision mediump float;\n"
        "uniform vec4 uniformArray;\n"
        "void main()\n"
        "{\n"
        "    gl_FragColor = vec4(uniformArray[int()]);\n"
        "}";

    shader = CompileShader(GL_FRAGMENT_SHADER, fragmentShaderSourceArray);
    EXPECT_EQ(0u, shader);

    if (shader != 0)
    {
        glDeleteShader(shader);
    }
}

// Test that structs defined in uniforms are translated correctly.
TEST_P(GLSLTest, StructSpecifiersUniforms)
{
    const std::string fragmentShaderSource = SHADER_SOURCE
    (
        precision mediump float;

        uniform struct S { float field;} s;

        void main()
        {
            gl_FragColor = vec4(1, 0, 0, 1);
            gl_FragColor.a += s.field;
        }
    );

    GLuint program = CompileProgram(mSimpleVSSource, fragmentShaderSource);
    EXPECT_NE(0u, program);
}

// Test that gl_DepthRange is not stored as a uniform location. Since uniforms
// beginning with "gl_" are filtered out by our validation logic, we must
// bypass the validation to test the behaviour of the implementation.
// (note this test is still Impl-independent)
TEST_P(GLSLTest, DepthRangeUniforms)
{
    const std::string fragmentShaderSource = SHADER_SOURCE
    (
        precision mediump float;

        void main()
        {
            gl_FragColor = vec4(gl_DepthRange.near, gl_DepthRange.far, gl_DepthRange.diff, 1);
        }
    );

    GLuint program = CompileProgram(mSimpleVSSource, fragmentShaderSource);
    EXPECT_NE(0u, program);

    // dive into the ANGLE internals, so we can bypass validation.
    gl::Context *context = reinterpret_cast<gl::Context *>(getEGLWindow()->getContext());
    gl::Program *glProgram = context->getProgram(program);
    GLint nearIndex = glProgram->getUniformLocation("gl_DepthRange.near");
    EXPECT_EQ(-1, nearIndex);

    // Test drawing does not throw an exception.
    drawQuad(program, "inputAttribute", 0.5f);

    EXPECT_GL_NO_ERROR();

    glDeleteProgram(program);
}

// Covers the WebGL test 'glsl/bugs/pow-of-small-constant-in-user-defined-function'
// See https://code.google.com/p/angleproject/issues/detail?id=851
// TODO(jmadill): ANGLE constant folding can fix this
TEST_P(GLSLTest, DISABLED_PowOfSmallConstant)
{
    const std::string &fragmentShaderSource = SHADER_SOURCE
    (
        precision highp float;

        float fun(float arg)
        {
            // These values are still easily within the highp range.
            // The minimum range in terms of 10's exponent is around -19 to 19, and IEEE-754 single precision range is higher than that.
            return pow(arg, 2.0);
        }

        void main()
        {
            // Note that the bug did not reproduce if an uniform was passed to the function instead of a constant,
            // or if the expression was moved outside the user-defined function.
            const float a = 1.0e-6;
            float b = 1.0e12 * fun(a);
            if (abs(b - 1.0) < 0.01)
            {
                gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0); // green
            }
            else
            {
                gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); // red
            }
        }
    );

    GLuint program = CompileProgram(mSimpleVSSource, fragmentShaderSource);
    EXPECT_NE(0u, program);

    drawQuad(program, "inputAttribute", 0.5f);
    EXPECT_PIXEL_EQ(0, 0, 0, 255, 0, 255);
    EXPECT_GL_NO_ERROR();
}

// Test that fragment shaders which contain non-constant loop indexers and compiled for FL9_3 and
// below
// fail with a specific error message.
// Additionally test that the same fragment shader compiles successfully with feature levels greater
// than FL9_3.
TEST_P(GLSLTest, LoopIndexingValidation)
{
    const std::string fragmentShaderSource = SHADER_SOURCE
    (
        precision mediump float;

        uniform float loopMax;

        void main()
        {
            gl_FragColor = vec4(1, 0, 0, 1);
            for (float l = 0.0; l < loopMax; l++)
            {
                if (loopMax > 3.0)
                {
                    gl_FragColor.a += 0.1;
                }
            }
        }
    );

    GLuint shader = glCreateShader(GL_FRAGMENT_SHADER);

    const char *sourceArray[1] = {fragmentShaderSource.c_str()};
    glShaderSource(shader, 1, sourceArray, nullptr);
    glCompileShader(shader);

    GLint compileResult;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compileResult);

    // If the test is configured to run limited to Feature Level 9_3, then it is
    // assumed that shader compilation will fail with an expected error message containing
    // "Loop index cannot be compared with non-constant expression"
    if ((GetParam() == ES2_D3D11_FL9_3() || GetParam() == ES2_D3D9()))
    {
        if (compileResult != 0)
        {
            FAIL() << "Shader compilation succeeded, expected failure";
        }
        else
        {
            GLint infoLogLength;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLogLength);

            std::string infoLog;
            infoLog.resize(infoLogLength);
            glGetShaderInfoLog(shader, static_cast<GLsizei>(infoLog.size()), NULL, &infoLog[0]);

            if (infoLog.find("Loop index cannot be compared with non-constant expression") ==
                std::string::npos)
            {
                FAIL() << "Shader compilation failed with unexpected error message";
            }
        }
    }
    else
    {
        EXPECT_NE(0, compileResult);
    }

    if (shader != 0)
    {
        glDeleteShader(shader);
    }
}

// Tests that the maximum uniforms count returned from querying GL_MAX_VERTEX_UNIFORM_VECTORS
// can actually be used.
TEST_P(GLSLTest, VerifyMaxVertexUniformVectors)
{
    int maxUniforms = 10000;
    glGetIntegerv(GL_MAX_VERTEX_UNIFORM_VECTORS, &maxUniforms);
    EXPECT_GL_NO_ERROR();
    std::cout << "Validating GL_MAX_VERTEX_UNIFORM_VECTORS = " << maxUniforms << std::endl;

    CompileGLSLWithUniformsAndSamplers(maxUniforms, 0, 0, 0, true);
}

// Tests that the maximum uniforms count returned from querying GL_MAX_VERTEX_UNIFORM_VECTORS
// can actually be used along with the maximum number of texture samplers.
TEST_P(GLSLTest, VerifyMaxVertexUniformVectorsWithSamplers)
{
    if (GetParam().eglParameters.renderer == EGL_PLATFORM_ANGLE_TYPE_OPENGL_ANGLE ||
        GetParam().eglParameters.renderer == EGL_PLATFORM_ANGLE_TYPE_OPENGLES_ANGLE)
    {
        std::cout << "Test disabled on OpenGL." << std::endl;
        return;
    }

    int maxUniforms = 10000;
    glGetIntegerv(GL_MAX_VERTEX_UNIFORM_VECTORS, &maxUniforms);
    EXPECT_GL_NO_ERROR();
    std::cout << "Validating GL_MAX_VERTEX_UNIFORM_VECTORS = " << maxUniforms << std::endl;

    int maxTextureImageUnits = 0;
    glGetIntegerv(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, &maxTextureImageUnits);

    CompileGLSLWithUniformsAndSamplers(maxUniforms, 0, maxTextureImageUnits, 0, true);
}

// Tests that the maximum uniforms count + 1 from querying GL_MAX_VERTEX_UNIFORM_VECTORS
// fails shader compilation.
TEST_P(GLSLTest, VerifyMaxVertexUniformVectorsExceeded)
{
    int maxUniforms = 10000;
    glGetIntegerv(GL_MAX_VERTEX_UNIFORM_VECTORS, &maxUniforms);
    EXPECT_GL_NO_ERROR();
    std::cout << "Validating GL_MAX_VERTEX_UNIFORM_VECTORS + 1 = " << maxUniforms + 1 << std::endl;

    CompileGLSLWithUniformsAndSamplers(maxUniforms + 1, 0, 0, 0, false);
}

// Tests that the maximum uniforms count returned from querying GL_MAX_FRAGMENT_UNIFORM_VECTORS
// can actually be used.
TEST_P(GLSLTest, VerifyMaxFragmentUniformVectors)
{
    int maxUniforms = 10000;
    glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_VECTORS, &maxUniforms);
    EXPECT_GL_NO_ERROR();
    std::cout << "Validating GL_MAX_FRAGMENT_UNIFORM_VECTORS = " << maxUniforms << std::endl;

    CompileGLSLWithUniformsAndSamplers(0, maxUniforms, 0, 0, true);
}

// Tests that the maximum uniforms count returned from querying GL_MAX_FRAGMENT_UNIFORM_VECTORS
// can actually be used along with the maximum number of texture samplers.
TEST_P(GLSLTest, VerifyMaxFragmentUniformVectorsWithSamplers)
{
    if (GetParam().eglParameters.renderer == EGL_PLATFORM_ANGLE_TYPE_OPENGL_ANGLE ||
        GetParam().eglParameters.renderer == EGL_PLATFORM_ANGLE_TYPE_OPENGLES_ANGLE)
    {
        std::cout << "Test disabled on OpenGL." << std::endl;
        return;
    }

    int maxUniforms = 10000;
    glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_VECTORS, &maxUniforms);
    EXPECT_GL_NO_ERROR();

    int maxTextureImageUnits = 0;
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &maxTextureImageUnits);

    CompileGLSLWithUniformsAndSamplers(0, maxUniforms, 0, maxTextureImageUnits, true);
}

// Tests that the maximum uniforms count + 1 from querying GL_MAX_FRAGMENT_UNIFORM_VECTORS
// fails shader compilation.
TEST_P(GLSLTest, VerifyMaxFragmentUniformVectorsExceeded)
{
    int maxUniforms = 10000;
    glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_VECTORS, &maxUniforms);
    EXPECT_GL_NO_ERROR();
    std::cout << "Validating GL_MAX_FRAGMENT_UNIFORM_VECTORS + 1 = " << maxUniforms + 1
              << std::endl;

    CompileGLSLWithUniformsAndSamplers(0, maxUniforms + 1, 0, 0, false);
}

// Use this to select which configurations (e.g. which renderer, which GLES major version) these tests should be run against.
ANGLE_INSTANTIATE_TEST(GLSLTest,
                       ES2_D3D9(),
                       ES2_D3D11(),
                       ES2_D3D11_FL9_3(),
                       ES2_OPENGL(),
                       ES3_OPENGL(),
                       ES2_OPENGLES(),
                       ES3_OPENGLES());

// Use this to select which configurations (e.g. which renderer, which GLES major version) these tests should be run against.
ANGLE_INSTANTIATE_TEST(GLSLTest_ES3, ES3_D3D11(), ES3_OPENGL(), ES3_OPENGLES());

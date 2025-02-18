//
// Copyright (c) 2015 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// MalformedShader_test.cpp:
//   Tests that malformed shaders fail compilation.
//

#include "angle_gl.h"
#include "gtest/gtest.h"
#include "GLSLANG/ShaderLang.h"
#include "compiler/translator/TranslatorESSL.h"

class MalformedShaderTest : public testing::Test
{
  public:
    MalformedShaderTest() : mExtraCompileOptions(0) {}

  protected:
    virtual void SetUp()
    {
        ShBuiltInResources resources;
        ShInitBuiltInResources(&resources);

        mTranslator = new TranslatorESSL(GL_FRAGMENT_SHADER, SH_GLES3_SPEC);
        ASSERT_TRUE(mTranslator->Init(resources));
    }

    virtual void TearDown()
    {
        delete mTranslator;
    }

    // Return true when compilation succeeds
    bool compile(const std::string& shaderString)
    {
        const char *shaderStrings[] = { shaderString.c_str() };
        bool compilationSuccess =
            mTranslator->compile(shaderStrings, 1, SH_INTERMEDIATE_TREE | mExtraCompileOptions);
        TInfoSink &infoSink = mTranslator->getInfoSink();
        mInfoLog = infoSink.info.c_str();
        return compilationSuccess;
    }

    bool hasWarning() const
    {
        return mInfoLog.find("WARNING: ") != std::string::npos;
    }

  protected:
    std::string mInfoLog;
    TranslatorESSL *mTranslator;
    int mExtraCompileOptions;
};

class MalformedVertexShaderTest : public MalformedShaderTest
{
  public:
    MalformedVertexShaderTest() {}

  protected:
    void SetUp() override
    {
        ShBuiltInResources resources;
        ShInitBuiltInResources(&resources);

        mTranslator = new TranslatorESSL(GL_VERTEX_SHADER, SH_GLES3_SPEC);
        ASSERT_TRUE(mTranslator->Init(resources));
    }
};

class MalformedWebGL2ShaderTest : public MalformedShaderTest
{
  public:
    MalformedWebGL2ShaderTest() {}

  protected:
    void SetUp() override
    {
        ShBuiltInResources resources;
        ShInitBuiltInResources(&resources);

        mTranslator = new TranslatorESSL(GL_FRAGMENT_SHADER, SH_WEBGL2_SPEC);
        ASSERT_TRUE(mTranslator->Init(resources));
    }
};

class MalformedWebGL1ShaderTest : public MalformedShaderTest
{
  public:
    MalformedWebGL1ShaderTest() {}

  protected:
    void SetUp() override
    {
        ShBuiltInResources resources;
        ShInitBuiltInResources(&resources);

        mTranslator = new TranslatorESSL(GL_FRAGMENT_SHADER, SH_WEBGL_SPEC);
        ASSERT_TRUE(mTranslator->Init(resources));
    }
};

class UnrollForLoopsTest : public MalformedShaderTest
{
  public:
    UnrollForLoopsTest() { mExtraCompileOptions = SH_UNROLL_FOR_LOOP_WITH_INTEGER_INDEX; }
};

// This is a test for a bug that used to exist in ANGLE:
// Calling a function with all parameters missing should not succeed.
TEST_F(MalformedShaderTest, FunctionParameterMismatch)
{
    const std::string &shaderString =
        "precision mediump float;\n"
        "float fun(float a) {\n"
        "   return a * 2.0;\n"
        "}\n"
        "void main() {\n"
        "   float ff = fun();\n"
        "   gl_FragColor = vec4(ff);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Functions can't be redeclared as variables in the same scope (ESSL 1.00 section 4.2.7)
TEST_F(MalformedShaderTest, RedeclaringFunctionAsVariable)
{
    const std::string &shaderString =
        "precision mediump float;\n"
        "float fun(float a) {\n"
        "   return a * 2.0;\n"
        "}\n"
        "float fun;\n"
        "void main() {\n"
        "   gl_FragColor = vec4(0.0);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Functions can't be redeclared as structs in the same scope (ESSL 1.00 section 4.2.7)
TEST_F(MalformedShaderTest, RedeclaringFunctionAsStruct)
{
    const std::string &shaderString =
        "precision mediump float;\n"
        "float fun(float a) {\n"
        "   return a * 2.0;\n"
        "}\n"
        "struct fun { float a; };\n"
        "void main() {\n"
        "   gl_FragColor = vec4(0.0);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Functions can't be redeclared with different qualifiers (ESSL 1.00 section 6.1.0)
TEST_F(MalformedShaderTest, RedeclaringFunctionWithDifferentQualifiers)
{
    const std::string &shaderString =
        "precision mediump float;\n"
        "float fun(out float a);\n"
        "float fun(float a) {\n"
        "   return a * 2.0;\n"
        "}\n"
        "void main() {\n"
        "   gl_FragColor = vec4(0.0);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Assignment and equality are undefined for structures containing arrays (ESSL 1.00 section 5.7)
TEST_F(MalformedShaderTest, CompareStructsContainingArrays)
{
    const std::string &shaderString =
        "precision mediump float;\n"
        "struct s { float a[3]; };\n"
        "void main() {\n"
        "   s a;\n"
        "   s b;\n"
        "   bool c = (a == b);\n"
        "   gl_FragColor = vec4(c ? 1.0 : 0.0);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Assignment and equality are undefined for structures containing arrays (ESSL 1.00 section 5.7)
TEST_F(MalformedShaderTest, AssignStructsContainingArrays)
{
    const std::string &shaderString =
        "precision mediump float;\n"
        "struct s { float a[3]; };\n"
        "void main() {\n"
        "   s a;\n"
        "   s b;\n"
        "   b.a[0] = 0.0;\n"
        "   a = b;\n"
        "   gl_FragColor = vec4(a.a[0]);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Assignment and equality are undefined for structures containing samplers (ESSL 1.00 sections 5.7 and 5.9)
TEST_F(MalformedShaderTest, CompareStructsContainingSamplers)
{
    const std::string &shaderString =
        "precision mediump float;\n"
        "struct s { sampler2D foo; };\n"
        "uniform s a;\n"
        "uniform s b;\n"
        "void main() {\n"
        "   bool c = (a == b);\n"
        "   gl_FragColor = vec4(c ? 1.0 : 0.0);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Samplers are not allowed as l-values (ESSL 3.00 section 4.1.7), our interpretation is that this
// extends to structs containing samplers. ESSL 1.00 spec is clearer about this.
TEST_F(MalformedShaderTest, AssignStructsContainingSamplers)
{
    const std::string &shaderString =
        "#version 300 es\n"
        "precision mediump float;\n"
        "struct s { sampler2D foo; };\n"
        "uniform s a;\n"
        "out vec4 my_FragColor;\n"
        "void main() {\n"
        "   s b;\n"
        "   b = a;\n"
        "   my_FragColor = vec4(1.0);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// This is a regression test for a particular bug that was in ANGLE.
// It also verifies that ESSL3 functionality doesn't leak to ESSL1.
TEST_F(MalformedShaderTest, ArrayWithNoSizeInInitializerList)
{
    const std::string &shaderString =
        "precision mediump float;\n"
        "void main() {\n"
        "   float a[2], b[];\n"
        "   gl_FragColor = vec4(1.0);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Const variables need an initializer.
TEST_F(MalformedShaderTest, ConstVarNotInitialized)
{
    const std::string &shaderString =
        "#version 300 es\n"
        "precision mediump float;\n"
        "out vec4 my_FragColor;\n"
        "void main() {\n"
        "   const float a;\n"
        "   my_FragColor = vec4(1.0);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Const variables need an initializer. In ESSL1 const structs containing
// arrays are not allowed at all since it's impossible to initialize them.
// Even though this test is for ESSL3 the only thing that's critical for
// ESSL1 is the non-initialization check that's used for both language versions.
// Whether ESSL1 compilation generates the most helpful error messages is a
// secondary concern.
TEST_F(MalformedShaderTest, ConstStructNotInitialized)
{
    const std::string &shaderString =
        "#version 300 es\n"
        "precision mediump float;\n"
        "struct S {\n"
        "   float a[3];\n"
        "};\n"
        "out vec4 my_FragColor;\n"
        "void main() {\n"
        "   const S b;\n"
        "   my_FragColor = vec4(1.0);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Const variables need an initializer. In ESSL1 const arrays are not allowed
// at all since it's impossible to initialize them.
// Even though this test is for ESSL3 the only thing that's critical for
// ESSL1 is the non-initialization check that's used for both language versions.
// Whether ESSL1 compilation generates the most helpful error messages is a
// secondary concern.
TEST_F(MalformedShaderTest, ConstArrayNotInitialized)
{
    const std::string &shaderString =
        "#version 300 es\n"
        "precision mediump float;\n"
        "out vec4 my_FragColor;\n"
        "void main() {\n"
        "   const float a[3];\n"
        "   my_FragColor = vec4(1.0);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Block layout qualifiers can't be used on non-block uniforms (ESSL 3.00 section 4.3.8.3)
TEST_F(MalformedShaderTest, BlockLayoutQualifierOnRegularUniform)
{
    const std::string &shaderString =
        "#version 300 es\n"
        "precision mediump float;\n"
        "layout(packed) uniform mat2 x;\n"
        "out vec4 my_FragColor;\n"
        "void main() {\n"
        "   my_FragColor = vec4(1.0);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Block layout qualifiers can't be used on non-block uniforms (ESSL 3.00 section 4.3.8.3)
TEST_F(MalformedShaderTest, BlockLayoutQualifierOnUniformWithEmptyDecl)
{
    // Yes, the comma in the declaration below is not a typo.
    // Empty declarations are allowed in GLSL.
    const std::string &shaderString =
        "#version 300 es\n"
        "precision mediump float;\n"
        "layout(packed) uniform mat2, x;\n"
        "out vec4 my_FragColor;\n"
        "void main() {\n"
        "   my_FragColor = vec4(1.0);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Arrays of arrays are not allowed (ESSL 3.00 section 4.1.9)
TEST_F(MalformedShaderTest, ArraysOfArrays1)
{
    const std::string &shaderString =
        "#version 300 es\n"
        "precision mediump float;\n"
        "out vec4 my_FragColor;\n"
        "void main() {\n"
        "   float[5] a[3];\n"
        "   my_FragColor = vec4(1.0);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Arrays of arrays are not allowed (ESSL 3.00 section 4.1.9)
TEST_F(MalformedShaderTest, ArraysOfArrays2)
{
    const std::string &shaderString =
        "#version 300 es\n"
        "precision mediump float;\n"
        "out vec4 my_FragColor;\n"
        "void main() {\n"
        "   float[2] a, b[3];\n"
        "   my_FragColor = vec4(1.0);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Implicitly sized arrays need to be initialized (ESSL 3.00 section 4.1.9)
TEST_F(MalformedShaderTest, UninitializedImplicitArraySize)
{
    const std::string &shaderString =
        "#version 300 es\n"
        "precision mediump float;\n"
        "out vec4 my_FragColor;\n"
        "void main() {\n"
        "   float[] a;\n"
        "   my_FragColor = vec4(1.0);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// An operator can only form a constant expression if all the operands are constant expressions
// - even operands of ternary operator that are never evaluated. (ESSL 3.00 section 4.3.3)
TEST_F(MalformedShaderTest, TernaryOperatorNotConstantExpression)
{
    const std::string &shaderString =
        "#version 300 es\n"
        "precision mediump float;\n"
        "out vec4 my_FragColor;\n"
        "uniform bool u;\n"
        "void main() {\n"
        "   const bool a = true ? true : u;\n"
        "   my_FragColor = vec4(1.0);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Ternary operator can't operate on arrays (ESSL 3.00 section 5.7)
TEST_F(MalformedShaderTest, TernaryOperatorOnArrays)
{
    const std::string &shaderString =
        "#version 300 es\n"
        "precision mediump float;\n"
        "out vec4 my_FragColor;\n"
        "void main() {\n"
        "   float[1] a = float[1](0.0);\n"
        "   float[1] b = float[1](1.0);\n"
        "   float[1] c = true ? a : b;\n"
        "   my_FragColor = vec4(1.0);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Ternary operator can't operate on structs (ESSL 3.00 section 5.7)
TEST_F(MalformedShaderTest, TernaryOperatorOnStructs)
{
    const std::string &shaderString =
        "#version 300 es\n"
        "precision mediump float;\n"
        "out vec4 my_FragColor;\n"
        "struct S { float foo; };\n"
        "void main() {\n"
        "   S a = S(0.0);\n"
        "   S b = S(1.0);\n"
        "   S c = true ? a : b;\n"
        "   my_FragColor = vec4(1.0);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Array length() returns a constant signed integral expression (ESSL 3.00 section 4.1.9)
// Assigning it to unsigned should result in an error.
TEST_F(MalformedShaderTest, AssignArrayLengthToUnsigned)
{
    const std::string &shaderString =
        "#version 300 es\n"
        "precision mediump float;\n"
        "out vec4 my_FragColor;\n"
        "void main() {\n"
        "   int[1] arr;\n"
        "   uint l = arr.length();\n"
        "   my_FragColor = vec4(float(l));\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Global variable initializers need to be constant expressions (ESSL 1.00 section 4.3)
// Initializing with a varying should be an error.
TEST_F(MalformedShaderTest, AssignVaryingToGlobal)
{
    const std::string &shaderString =
        "precision mediump float;\n"
        "varying float a;\n"
        "float b = a * 2.0;\n"
        "void main() {\n"
        "   gl_FragColor = vec4(b);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Global variable initializers need to be constant expressions (ESSL 3.00 section 4.3)
// Initializing with an uniform should be an error.
TEST_F(MalformedShaderTest, AssignUniformToGlobalESSL3)
{
    const std::string &shaderString =
        "#version 300 es\n"
        "precision mediump float;\n"
        "uniform float a;\n"
        "float b = a * 2.0;\n"
        "out vec4 my_FragColor;\n"
        "void main() {\n"
        "   my_FragColor = vec4(b);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Global variable initializers need to be constant expressions (ESSL 1.00 section 4.3)
// Initializing with an uniform should generate a warning
// (we don't generate an error on ESSL 1.00 because of legacy compatibility)
TEST_F(MalformedShaderTest, AssignUniformToGlobalESSL1)
{
    const std::string &shaderString =
        "precision mediump float;\n"
        "uniform float a;\n"
        "float b = a * 2.0;\n"
        "void main() {\n"
        "   gl_FragColor = vec4(b);\n"
        "}\n";
    if (compile(shaderString))
    {
        if (!hasWarning())
        {
            FAIL() << "Shader compilation succeeded without warnings, expecting warning " << mInfoLog;
        }
    }
    else
    {
        FAIL() << "Shader compilation failed, expecting success with warning " << mInfoLog;
    }
}

// Global variable initializers need to be constant expressions (ESSL 1.00 section 4.3)
// Initializing with an user-defined function call should be an error.
TEST_F(MalformedShaderTest, AssignFunctionCallToGlobal)
{
    const std::string &shaderString =
        "precision mediump float;\n"
        "float foo() { return 1.0; }\n"
        "float b = foo();\n"
        "void main() {\n"
        "   gl_FragColor = vec4(b);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Global variable initializers need to be constant expressions (ESSL 1.00 section 4.3)
// Initializing with an assignment to another global should be an error.
TEST_F(MalformedShaderTest, AssignAssignmentToGlobal)
{
    const std::string &shaderString =
        "precision mediump float;\n"
        "float c = 1.0;\n"
        "float b = (c = 0.0);\n"
        "void main() {\n"
        "   gl_FragColor = vec4(b);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Global variable initializers need to be constant expressions (ESSL 1.00 section 4.3)
// Initializing with incrementing another global should be an error.
TEST_F(MalformedShaderTest, AssignIncrementToGlobal)
{
    const std::string &shaderString =
        "precision mediump float;\n"
        "float c = 1.0;\n"
        "float b = (c++);\n"
        "void main() {\n"
        "   gl_FragColor = vec4(b);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Global variable initializers need to be constant expressions (ESSL 1.00 section 4.3)
// Initializing with a texture lookup function call should be an error.
TEST_F(MalformedShaderTest, AssignTexture2DToGlobal)
{
    const std::string &shaderString =
        "precision mediump float;\n"
        "uniform mediump sampler2D s;\n"
        "float b = texture2D(s, vec2(0.5, 0.5)).x;\n"
        "void main() {\n"
        "   gl_FragColor = vec4(b);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Global variable initializers need to be constant expressions (ESSL 3.00 section 4.3)
// Initializing with a non-constant global should be an error.
TEST_F(MalformedShaderTest, AssignNonConstGlobalToGlobal)
{
    const std::string &shaderString =
        "#version 300 es\n"
        "precision mediump float;\n"
        "float a = 1.0;\n"
        "float b = a * 2.0;\n"
        "out vec4 my_FragColor;\n"
        "void main() {\n"
        "   my_FragColor = vec4(b);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Global variable initializers need to be constant expressions (ESSL 3.00 section 4.3)
// Initializing with a constant global should be fine.
TEST_F(MalformedShaderTest, AssignConstGlobalToGlobal)
{
    const std::string &shaderString =
        "#version 300 es\n"
        "precision mediump float;\n"
        "const float a = 1.0;\n"
        "float b = a * 2.0;\n"
        "out vec4 my_FragColor;\n"
        "void main() {\n"
        "   my_FragColor = vec4(b);\n"
        "}\n";
    if (!compile(shaderString))
    {
        FAIL() << "Shader compilation failed, expecting success " << mInfoLog;
    }
}

// Statically assigning to both gl_FragData and gl_FragColor is forbidden (ESSL 1.00 section 7.2)
TEST_F(MalformedShaderTest, WriteBothFragDataAndFragColor)
{
    const std::string &shaderString =
        "precision mediump float;\n"
        "void foo() {\n"
        "   gl_FragData[0].a++;\n"
        "}\n"
        "void main() {\n"
        "   gl_FragColor.x += 0.0;\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Version directive must be on the first line (ESSL 3.00 section 3.3)
TEST_F(MalformedShaderTest, VersionOnSecondLine)
{
    const std::string &shaderString =
        "\n"
        "#version 300 es\n"
        "precision mediump float;\n"
        "out vec4 my_FragColor;\n"
        "void main() {\n"
        "   my_FragColor = vec4(0.0);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Layout qualifier can only appear in global scope (ESSL 3.00 section 4.3.8)
TEST_F(MalformedShaderTest, LayoutQualifierInCondition)
{
    const std::string &shaderString =
        "#version 300 es\n"
        "precision mediump float;\n"
        "uniform vec4 u;\n"
        "out vec4 my_FragColor;\n"
        "void main() {\n"
        "    int i = 0;\n"
        "    for (int j = 0; layout(location = 0) bool b = false; ++j) {\n"
        "        ++i;\n"
        "    }\n"
        "    my_FragColor = u;\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Layout qualifier can only appear where specified (ESSL 3.00 section 4.3.8)
TEST_F(MalformedShaderTest, LayoutQualifierInFunctionReturnType)
{
    const std::string &shaderString =
        "#version 300 es\n"
        "precision mediump float;\n"
        "uniform vec4 u;\n"
        "out vec4 my_FragColor;\n"
        "layout(location = 0) vec4 foo() {\n"
        "    return u;\n"
        "}\n"
        "void main() {\n"
        "    my_FragColor = foo();\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// If there is more than one output, the location must be specified for all outputs.
// (ESSL 3.00.04 section 4.3.8.2)
TEST_F(MalformedShaderTest, TwoOutputsNoLayoutQualifiers)
{
    const std::string &shaderString =
        "#version 300 es\n"
        "precision mediump float;\n"
        "uniform vec4 u;\n"
        "out vec4 my_FragColor;\n"
        "out vec4 my_SecondaryFragColor;\n"
        "void main() {\n"
        "    my_FragColor = vec4(1.0);\n"
        "    my_SecondaryFragColor = vec4(0.5);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// (ESSL 3.00.04 section 4.3.8.2)
TEST_F(MalformedShaderTest, TwoOutputsFirstLayoutQualifier)
{
    const std::string &shaderString =
        "#version 300 es\n"
        "precision mediump float;\n"
        "uniform vec4 u;\n"
        "layout(location = 0) out vec4 my_FragColor;\n"
        "out vec4 my_SecondaryFragColor;\n"
        "void main() {\n"
        "    my_FragColor = vec4(1.0);\n"
        "    my_SecondaryFragColor = vec4(0.5);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// (ESSL 3.00.04 section 4.3.8.2)
TEST_F(MalformedShaderTest, TwoOutputsSecondLayoutQualifier)
{
    const std::string &shaderString =
        "#version 300 es\n"
        "precision mediump float;\n"
        "uniform vec4 u;\n"
        "out vec4 my_FragColor;\n"
        "layout(location = 0) out vec4 my_SecondaryFragColor;\n"
        "void main() {\n"
        "    my_FragColor = vec4(1.0);\n"
        "    my_SecondaryFragColor = vec4(0.5);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Uniforms can be arrays (ESSL 3.00 section 4.3.5)
TEST_F(MalformedShaderTest, UniformArray)
{
    const std::string &shaderString =
        "#version 300 es\n"
        "precision mediump float;\n"
        "uniform vec4[2] u;\n"
        "out vec4 my_FragColor;\n"
        "void main() {\n"
        "    my_FragColor = u[0];\n"
        "}\n";
    if (!compile(shaderString))
    {
        FAIL() << "Shader compilation failed, expecting success " << mInfoLog;
    }
}

// Fragment shader input variables cannot be arrays of structs (ESSL 3.00 section 4.3.4)
TEST_F(MalformedShaderTest, FragmentInputArrayOfStructs)
{
    const std::string &shaderString =
        "#version 300 es\n"
        "precision mediump float;\n"
        "struct S {\n"
        "    vec4 foo;\n"
        "};\n"
        "in S i[2];\n"
        "out vec4 my_FragColor;\n"
        "void main() {\n"
        "    my_FragColor = i[0].foo;\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Vertex shader inputs can't be arrays (ESSL 3.00 section 4.3.4)
// This test is testing the case where the array brackets are after the variable name, so
// the arrayness isn't known when the type and qualifiers are initially parsed.
TEST_F(MalformedVertexShaderTest, VertexShaderInputArray)
{
    const std::string &shaderString =
        "#version 300 es\n"
        "precision mediump float;\n"
        "in vec4 i[2];\n"
        "void main() {\n"
        "    gl_Position = i[0];\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Vertex shader inputs can't be arrays (ESSL 3.00 section 4.3.4)
// This test is testing the case where the array brackets are after the type.
TEST_F(MalformedVertexShaderTest, VertexShaderInputArrayType)
{
    const std::string &shaderString =
        "#version 300 es\n"
        "precision mediump float;\n"
        "in vec4[2] i;\n"
        "void main() {\n"
        "    gl_Position = i[0];\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Fragment shader inputs can't contain booleans (ESSL 3.00 section 4.3.4)
TEST_F(MalformedShaderTest, FragmentShaderInputStructWithBool)
{
    const std::string &shaderString =
        "#version 300 es\n"
        "precision mediump float;\n"
        "struct S {\n"
        "    bool foo;\n"
        "};\n"
        "in S s;\n"
        "out vec4 my_FragColor;\n"
        "void main() {\n"
        "    my_FragColor = vec4(0.0);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Fragment shader inputs without a flat qualifier can't contain integers (ESSL 3.00 section 4.3.4)
TEST_F(MalformedShaderTest, FragmentShaderInputStructWithInt)
{
    const std::string &shaderString =
        "#version 300 es\n"
        "precision mediump float;\n"
        "struct S {\n"
        "    int foo;\n"
        "};\n"
        "in S s;\n"
        "out vec4 my_FragColor;\n"
        "void main() {\n"
        "    my_FragColor = vec4(0.0);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Selecting a field of a vector that's the result of dynamic indexing a constant array should work.
TEST_F(MalformedShaderTest, ShaderSelectingFieldOfVectorIndexedFromArray)
{
    const std::string &shaderString =
        "#version 300 es\n"
        "precision mediump float;\n"
        "out vec4 my_FragColor;\n"
        "uniform int i;\n"
        "void main() {\n"
        "    float f = vec2[1](vec2(0.0, 0.1))[i].x;\n"
        "    my_FragColor = vec4(f);\n"
        "}\n";
    if (!compile(shaderString))
    {
        FAIL() << "Shader compilation failed, expecting success " << mInfoLog;
    }
}

// Passing an array into a function and then passing a value from that array into another function
// should work. This is a regression test for a bug where the mangled name of a TType was not
// properly updated when determining the type resulting from array indexing.
TEST_F(MalformedShaderTest, ArrayValueFromFunctionParameterAsParameter)
{
    const std::string &shaderString =
        "precision mediump float;\n"
        "uniform float u;\n"
        "float foo(float f) {\n"
        "   return f * 2.0;\n"
        "}\n"
        "float bar(float[2] f) {\n"
        "    return foo(f[0]);\n"
        "}\n"
        "void main()\n"
        "{\n"
        "    float arr[2];\n"
        "    arr[0] = u;\n"
        "    gl_FragColor = vec4(bar(arr));\n"
        "}\n";
    if (!compile(shaderString))
    {
        FAIL() << "Shader compilation failed, expecting success " << mInfoLog;
    }
}

// Test that out-of-range integer literal generates an error in ESSL 3.00.
TEST_F(MalformedShaderTest, OutOfRangeIntegerLiteral)
{
    const std::string &shaderString =
        "#version 300 es\n"
        "precision mediump float;\n"
        "precision highp int;\n"
        "out vec4 my_FragColor;\n"
        "void main() {\n"
        "    my_FragColor = vec4(0x100000000);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Test that vector field selection from a value taken from an array constructor is accepted as a
// constant expression.
TEST_F(MalformedShaderTest, FieldSelectionFromVectorArrayConstructorIsConst)
{
    const std::string &shaderString =
        "#version 300 es\n"
        "precision mediump float;\n"
        "out vec4 my_FragColor;\n"
        "void main()\n"
        "{\n"
        "    const float f = vec2[1](vec2(0.0, 1.0))[0].x;\n"
        "    my_FragColor = vec4(f);\n"
        "}\n";
    if (!compile(shaderString))
    {
        FAIL() << "Shader compilation failed, expecting success " << mInfoLog;
    }
}

// Test that structure field selection from a value taken from an array constructor is accepted as a
// constant expression.
TEST_F(MalformedShaderTest, FieldSelectionFromStructArrayConstructorIsConst)
{
    const std::string &shaderString =
        "#version 300 es\n"
        "precision mediump float;\n"
        "out vec4 my_FragColor;\n"
        "struct S { float member; };\n"
        "void main()\n"
        "{\n"
        "    const float f = S[1](S(0.0))[0].member;\n"
        "    my_FragColor = vec4(f);\n"
        "}\n";
    if (!compile(shaderString))
    {
        FAIL() << "Shader compilation failed, expecting success " << mInfoLog;
    }
}

// Test that a reference to a const array is accepted as a constant expression.
TEST_F(MalformedShaderTest, ArraySymbolIsConst)
{
    const std::string &shaderString =
        "#version 300 es\n"
        "precision mediump float;\n"
        "out vec4 my_FragColor;\n"
        "void main()\n"
        "{\n"
        "    const float[2] arr = float[2](0.0, 1.0);\n"
        "    const float f = arr[0];\n"
        "    my_FragColor = vec4(f);\n"
        "}\n";
    if (!compile(shaderString))
    {
        FAIL() << "Shader compilation failed, expecting success " << mInfoLog;
    }
}

// Test that using an array constructor in a parameter to a built-in function is accepted as a
// constant expression.
TEST_F(MalformedShaderTest, BuiltInFunctionAppliedToArrayConstructorIsConst)
{
    const std::string &shaderString =
        "#version 300 es\n"
        "precision mediump float;\n"
        "out vec4 my_FragColor;\n"
        "void main()\n"
        "{\n"
        "    const float f = sin(float[2](0.0, 1.0)[0]);\n"
        "    my_FragColor = vec4(f);\n"
        "}\n";
    if (!compile(shaderString))
    {
        FAIL() << "Shader compilation failed, expecting success " << mInfoLog;
    }
}

// Test that using an array constructor in a parameter to a built-in function is accepted as a
// constant expression.
TEST_F(MalformedShaderTest, BuiltInFunctionWithMultipleParametersAppliedToArrayConstructorIsConst)
{
    const std::string &shaderString =
        "#version 300 es\n"
        "precision mediump float;\n"
        "out vec4 my_FragColor;\n"
        "void main()\n"
        "{\n"
        "    const float f = pow(1.0, float[2](0.0, 1.0)[0]);\n"
        "    my_FragColor = vec4(f);\n"
        "}\n";
    if (!compile(shaderString))
    {
        FAIL() << "Shader compilation failed, expecting success " << mInfoLog;
    }
}

// Test that using an array constructor in a parameter to a constructor is accepted as a constant
// expression.
TEST_F(MalformedShaderTest, ConstructorWithMultipleParametersAppliedToArrayConstructorIsConst)
{
    const std::string &shaderString =
        "#version 300 es\n"
        "precision mediump float;\n"
        "out vec4 my_FragColor;\n"
        "void main()\n"
        "{\n"
        "    const vec2 f = vec2(1.0, float[2](0.0, 1.0)[0]);\n"
        "    my_FragColor = vec4(f.x);\n"
        "}\n";
    if (!compile(shaderString))
    {
        FAIL() << "Shader compilation failed, expecting success " << mInfoLog;
    }
}

// Test that using an array constructor in an operand of the ternary selection operator is accepted
// as a constant expression.
TEST_F(MalformedShaderTest, TernaryOperatorAppliedToArrayConstructorIsConst)
{
    const std::string &shaderString =
        "#version 300 es\n"
        "precision mediump float;\n"
        "out vec4 my_FragColor;\n"
        "void main()\n"
        "{\n"
        "    const float f = true ? float[2](0.0, 1.0)[0] : 1.0;\n"
        "    my_FragColor = vec4(f);\n"
        "}\n";
    if (!compile(shaderString))
    {
        FAIL() << "Shader compilation failed, expecting success " << mInfoLog;
    }
}

// Test that a ternary operator with one unevaluated non-constant operand is not a constant
// expression.
TEST_F(MalformedShaderTest, TernaryOperatorNonConstantOperand)
{
    const std::string &shaderString =
        "precision mediump float;\n"
        "uniform float u;\n"
        "void main()\n"
        "{\n"
        "    const float f = true ? 1.0 : u;\n"
        "    gl_FragColor = vec4(f);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Test that a sampler can't be used in constructor argument list
TEST_F(MalformedShaderTest, SamplerInConstructorArguments)
{
    const std::string &shaderString =
        "precision mediump float;\n"
        "uniform sampler2D s;\n"
        "void main()\n"
        "{\n"
        "    vec2 v = vec2(0.0, s);\n"
        "    gl_FragColor = vec4(v, 0.0, 0.0);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Test that void can't be used in constructor argument list
TEST_F(MalformedShaderTest, VoidInConstructorArguments)
{
    const std::string &shaderString =
        "precision mediump float;\n"
        "void foo() {}\n"
        "void main()\n"
        "{\n"
        "    vec2 v = vec2(0.0, foo());\n"
        "    gl_FragColor = vec4(v, 0.0, 0.0);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Test that a shader passing a struct into a constructor of array of structs with 1 element works.
TEST_F(MalformedShaderTest, SingleStructArrayConstructor)
{
    const std::string &shaderString =
        "#version 300 es\n"
        "precision mediump float;\n"
        "out vec4 my_FragColor;\n"
        "uniform float u;\n"
        "struct S { float member; };\n"
        "void main()\n"
        "{\n"
        "    S[1] sarr = S[1](S(u));\n"
        "    my_FragColor = vec4(sarr[0].member);\n"
        "}\n";
    if (!compile(shaderString))
    {
        FAIL() << "Shader compilation failed, expecting success " << mInfoLog;
    }
}

// Test that a shader with empty constructor parameter list is not accepted.
TEST_F(MalformedShaderTest, EmptyArrayConstructor)
{
    const std::string &shaderString =
        "#version 300 es\n"
        "precision mediump float;\n"
        "out vec4 my_FragColor;\n"
        "uniform float u;\n"
        "const float[] f = f[]();\n"
        "void main()\n"
        "{\n"
        "    my_FragColor = vec4(0.0);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Test that indexing fragment outputs with a non-constant expression is forbidden, even if ANGLE
// is able to constant fold the index expression. ESSL 3.00 section 4.3.6.
TEST_F(MalformedShaderTest, DynamicallyIndexedFragmentOutput)
{
    const std::string &shaderString =
        "#version 300 es"
        "precision mediump float;\n"
        "uniform int a;\n"
        "out vec4[2] my_FragData;\n"
        "void main()\n"
        "{\n"
        "    my_FragData[true ? 0 : a] = vec4(0.0);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Test that indexing an interface block array with a non-constant expression is forbidden, even if
// ANGLE is able to constant fold the index expression. ESSL 3.00 section 4.3.7.
TEST_F(MalformedShaderTest, DynamicallyIndexedInterfaceBlock)
{
    const std::string &shaderString =
        "#version 300 es"
        "precision mediump float;\n"
        "uniform int a;\n"
        "uniform B\n"
        "{\n"
        "    vec4 f;\n"
        "}\n"
        "blocks[2];\n"
        "out vec4 my_FragColor;\n"
        "void main()\n"
        "{\n"
        "    my_FragColor = blocks[true ? 0 : a].f;\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Test that a shader that uses a struct definition in place of a struct constructor does not
// compile. See GLSL ES 1.00 section 5.4.3.
TEST_F(MalformedShaderTest, StructConstructorWithStructDefinition)
{
    const std::string &shaderString =
        "precision mediump float;\n"
        "void main()\n"
        "{\n"
        "    struct s { float f; } (0.0);\n"
        "    gl_FragColor = vec4(0.0);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Test that indexing gl_FragData with a non-constant expression is forbidden in WebGL 2.0, even
// when ANGLE is able to constant fold the index.
// WebGL 2.0 spec section 'GLSL ES 1.00 Fragment Shader Output'
TEST_F(MalformedWebGL2ShaderTest, IndexFragDataWithNonConstant)
{
    const std::string &shaderString =
        "precision mediump float;\n"
        "void main()\n"
        "{\n"
        "    for (int i = 0; i < 2; ++i) {\n"
        "        gl_FragData[true ? 0 : i] = vec4(0.0);\n"
        "    }\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Test that a non-constant texture offset is not accepted for textureOffset.
// ESSL 3.00 section 8.8
TEST_F(MalformedShaderTest, TextureOffsetNonConst)
{
    const std::string &shaderString =
        "#version 300 es\n"
        "precision mediump float;\n"
        "out vec4 my_FragColor;\n"
        "uniform vec3 u_texCoord;\n"
        "uniform mediump sampler3D u_sampler;\n"
        "uniform int x;\n"
        "void main()\n"
        "{\n"
        "   my_FragColor = textureOffset(u_sampler, u_texCoord, ivec3(x, 3, -8));\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Test that a non-constant texture offset is not accepted for textureProjOffset with bias.
// ESSL 3.00 section 8.8
TEST_F(MalformedShaderTest, TextureProjOffsetNonConst)
{
    const std::string &shaderString =
        "#version 300 es\n"
        "precision mediump float;\n"
        "out vec4 my_FragColor;\n"
        "uniform vec4 u_texCoord;\n"
        "uniform mediump sampler3D u_sampler;\n"
        "uniform int x;\n"
        "void main()\n"
        "{\n"
        "   my_FragColor = textureProjOffset(u_sampler, u_texCoord, ivec3(x, 3, -8), 0.0);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Test that an out-of-range texture offset is not accepted.
// GLES 3.0.4 section 3.8.10 specifies that out-of-range offset has undefined behavior.
TEST_F(MalformedShaderTest, TextureLodOffsetOutOfRange)
{
    const std::string &shaderString =
        "#version 300 es\n"
        "precision mediump float;\n"
        "out vec4 my_FragColor;\n"
        "uniform vec3 u_texCoord;\n"
        "uniform mediump sampler3D u_sampler;\n"
        "void main()\n"
        "{\n"
        "   my_FragColor = textureLodOffset(u_sampler, u_texCoord, 0.0, ivec3(0, 0, 8));\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Test that default precision qualifier for uint is not accepted.
// ESSL 3.00.4 section 4.5.4: Only allowed for float, int and sampler types.
TEST_F(MalformedShaderTest, DefaultPrecisionUint)
{
    const std::string &shaderString =
        "#version 300 es\n"
        "precision mediump float;\n"
        "precision mediump uint;\n"
        "out vec4 my_FragColor;\n"
        "void main()\n"
        "{\n"
        "   my_FragColor = vec4(0.0);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Test that sampler3D needs to be precision qualified.
// ESSL 3.00.4 section 4.5.4: New ESSL 3.00 sampler types don't have predefined precision.
TEST_F(MalformedShaderTest, NoPrecisionSampler3D)
{
    const std::string &shaderString =
        "#version 300 es\n"
        "precision mediump float;\n"
        "uniform sampler3D s;\n"
        "out vec4 my_FragColor;\n"
        "void main()\n"
        "{\n"
        "   my_FragColor = vec4(0.0);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Test that using a non-constant expression in a for loop initializer is forbidden in WebGL 1.0,
// even when ANGLE is able to constant fold the initializer.
// ESSL 1.00 Appendix A.
TEST_F(MalformedWebGL1ShaderTest, NonConstantLoopIndex)
{
    const std::string &shaderString =
        "precision mediump float;\n"
        "uniform int u;\n"
        "void main()\n"
        "{\n"
        "    for (int i = (true ? 1 : u); i < 5; ++i) {\n"
        "        gl_FragColor = vec4(0.0);\n"
        "    }\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Regression test for an old crash bug in ANGLE.
// ForLoopUnroll used to crash when it encountered a while loop.
TEST_F(UnrollForLoopsTest, WhileLoop)
{
    const std::string &shaderString =
        "precision mediump float;\n"
        "void main()\n"
        "{\n"
        "    while (true) {\n"
        "        gl_FragColor = vec4(0.0);\n"
        "        break;\n"
        "    }\n"
        "}\n";
    if (!compile(shaderString))
    {
        FAIL() << "Shader compilation failed, expecting success " << mInfoLog;
    }
}

// Regression test for an old crash bug in ANGLE.
// ForLoopUnroll used to crash when it encountered a loop that didn't fit the ESSL 1.00
// Appendix A limitations.
TEST_F(UnrollForLoopsTest, UnlimitedForLoop)
{
    const std::string &shaderString =
        "precision mediump float;\n"
        "void main()\n"
        "{\n"
        "    for (;true;) {\n"
        "        gl_FragColor = vec4(0.0);\n"
        "        break;\n"
        "    }\n"
        "}\n";
    if (!compile(shaderString))
    {
        FAIL() << "Shader compilation failed, expecting success " << mInfoLog;
    }
}

// Check that indices that are not integers are rejected.
// The check should be done even if ESSL 1.00 Appendix A limitations are not applied.
TEST_F(MalformedShaderTest, NonIntegerIndex)
{
    const std::string &shaderString =
        "precision mediump float;\n"
        "void main()\n"
        "{\n"
        "    float f[3];\n"
        "    const float i = 2.0;\n"
        "    gl_FragColor = vec4(f[i]);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// ESSL1 shaders with a duplicate function prototype should be rejected.
// ESSL 1.00.17 section 4.2.7.
TEST_F(MalformedShaderTest, DuplicatePrototypeESSL1)
{
    const std::string &shaderString =
        "precision mediump float;\n"
        "void foo();\n"
        "void foo();\n"
        "void foo() {}\n"
        "void main()\n"
        "{\n"
        "    gl_FragColor = vec4(0.0);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// ESSL3 shaders with a duplicate function prototype should be allowed.
// ESSL 3.00.4 section 4.2.3.
TEST_F(MalformedShaderTest, DuplicatePrototypeESSL3)
{
    const std::string &shaderString =
        "#version 300 es\n"
        "precision mediump float;\n"
        "out vec4 my_FragColor;\n"
        "void foo();\n"
        "void foo();\n"
        "void foo() {}\n"
        "void main()\n"
        "{\n"
        "    my_FragColor = vec4(0.0);\n"
        "}\n";
    if (!compile(shaderString))
    {
        FAIL() << "Shader compilation failed, expecting success " << mInfoLog;
    }
}

// Shaders with a local function prototype should be rejected.
// ESSL 3.00.4 section 4.2.4.
TEST_F(MalformedShaderTest, LocalFunctionPrototype)
{
    const std::string &shaderString =
        "#version 300 es\n"
        "precision mediump float;\n"
        "out vec4 my_FragColor;\n"
        "void main()\n"
        "{\n"
        "    void foo();\n"
        "    my_FragColor = vec4(0.0);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// ESSL 3.00 fragment shaders can not use #pragma STDGL invariant(all).
// ESSL 3.00.4 section 4.6.1. Does not apply to other versions of ESSL.
TEST_F(MalformedShaderTest, ESSL300FragmentInvariantAll)
{
    const std::string &shaderString =
        "#version 300 es\n"
        "#pragma STDGL invariant(all)\n"
        "precision mediump float;\n"
        "out vec4 my_FragColor;\n"
        "void main()\n"
        "{\n"
        "    my_FragColor = vec4(0.0);\n"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

// Built-in functions can be overloaded in ESSL 1.00.
TEST_F(MalformedShaderTest, ESSL100BuiltInFunctionOverload)
{
    const std::string &shaderString =
        "precision mediump float;\n"
        "int sin(int x)\n"
        "{\n"
        "    return int(sin(float(x)));\n"
        "}\n"
        "void main()\n"
        "{\n"
        "   gl_FragColor = vec4(sin(1));"
        "}\n";
    if (!compile(shaderString))
    {
        FAIL() << "Shader compilation failed, expecting success " << mInfoLog;
    }
}

// Built-in functions can not be overloaded in ESSL 3.00.
TEST_F(MalformedShaderTest, ESSL300BuiltInFunctionOverload)
{
    const std::string &shaderString =
        "#version 300 es\n"
        "precision mediump float;\n"
        "out vec4 my_FragColor;\n"
        "int sin(int x)\n"
        "{\n"
        "    return int(sin(float(x)));\n"
        "}\n"
        "void main()\n"
        "{\n"
        "   my_FragColor = vec4(sin(1));"
        "}\n";
    if (compile(shaderString))
    {
        FAIL() << "Shader compilation succeeded, expecting failure " << mInfoLog;
    }
}

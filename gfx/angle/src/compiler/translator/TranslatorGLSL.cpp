//
// Copyright (c) 2002-2013 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

#include "compiler/translator/TranslatorGLSL.h"

#include "angle_gl.h"
#include "compiler/translator/BuiltInFunctionEmulatorGLSL.h"
#include "compiler/translator/EmulatePrecision.h"
#include "compiler/translator/ExtensionGLSL.h"
#include "compiler/translator/OutputGLSL.h"
#include "compiler/translator/VersionGLSL.h"

TranslatorGLSL::TranslatorGLSL(sh::GLenum type,
                               ShShaderSpec spec,
                               ShShaderOutput output)
    : TCompiler(type, spec, output) {
}

void TranslatorGLSL::initBuiltInFunctionEmulator(BuiltInFunctionEmulator *emu, int compileOptions)
{
    if (compileOptions & SH_EMULATE_BUILT_IN_FUNCTIONS)
    {
        InitBuiltInFunctionEmulatorForGLSLWorkarounds(emu, getShaderType());
    }

    int targetGLSLVersion = ShaderOutputTypeToGLSLVersion(getOutputType());
    InitBuiltInFunctionEmulatorForGLSLMissingFunctions(emu, getShaderType(), targetGLSLVersion);
}

void TranslatorGLSL::translate(TIntermNode *root, int compileOptions)
{
    TInfoSinkBase& sink = getInfoSink().obj;

    // Write GLSL version.
    writeVersion(root);

    writePragma();

    // Write extension behaviour as needed
    writeExtensionBehavior(root);

    bool precisionEmulation = getResources().WEBGL_debug_shader_precision && getPragma().debugShaderPrecision;

    if (precisionEmulation)
    {
        EmulatePrecision emulatePrecision(getSymbolTable(), getShaderVersion());
        root->traverse(&emulatePrecision);
        emulatePrecision.updateTree();
        emulatePrecision.writeEmulationHelpers(sink, getOutputType());
    }

    // Write emulated built-in functions if needed.
    if (!getBuiltInFunctionEmulator().IsOutputEmpty())
    {
        sink << "// BEGIN: Generated code for built-in function emulation\n\n";
        sink << "#define webgl_emu_precision\n\n";
        getBuiltInFunctionEmulator().OutputEmulatedFunctions(sink);
        sink << "// END: Generated code for built-in function emulation\n\n";
    }

    // Write array bounds clamping emulation if needed.
    getArrayBoundsClamper().OutputClampingFunctionDefinition(sink);

    // Declare gl_FragColor and glFragData as webgl_FragColor and webgl_FragData
    // if it's core profile shaders and they are used.
    if (getShaderType() == GL_FRAGMENT_SHADER)
    {
        const bool mayHaveESSL1SecondaryOutputs =
            IsExtensionEnabled(getExtensionBehavior(), "GL_EXT_blend_func_extended") &&
            getShaderVersion() == 100;
        const bool declareGLFragmentOutputs = IsGLSL130OrNewer(getOutputType());

        bool hasGLFragColor          = false;
        bool hasGLFragData           = false;
        bool hasGLSecondaryFragColor = false;
        bool hasGLSecondaryFragData  = false;

        for (const auto &outputVar : outputVariables)
        {
            if (declareGLFragmentOutputs)
            {
                if (outputVar.name == "gl_FragColor")
                {
                    ASSERT(!hasGLFragColor);
                    hasGLFragColor = true;
                    continue;
                }
                else if (outputVar.name == "gl_FragData")
                {
                    ASSERT(!hasGLFragData);
                    hasGLFragData = true;
                    continue;
                }
            }
            if (mayHaveESSL1SecondaryOutputs)
            {
                if (outputVar.name == "gl_SecondaryFragColorEXT")
                {
                    ASSERT(!hasGLSecondaryFragColor);
                    hasGLSecondaryFragColor = true;
                    continue;
                }
                else if (outputVar.name == "gl_SecondaryFragDataEXT")
                {
                    ASSERT(!hasGLSecondaryFragData);
                    hasGLSecondaryFragData = true;
                    continue;
                }
            }
        }
        ASSERT(!((hasGLFragColor || hasGLSecondaryFragColor) &&
                 (hasGLFragData || hasGLSecondaryFragData)));
        if (hasGLFragColor)
        {
            sink << "out vec4 webgl_FragColor;\n";
        }
        if (hasGLFragData)
        {
            sink << "out vec4 webgl_FragData[gl_MaxDrawBuffers];\n";
        }
        if (hasGLSecondaryFragColor)
        {
            sink << "out vec4 angle_SecondaryFragColor;\n";
        }
        if (hasGLSecondaryFragData)
        {
            sink << "out vec4 angle_SecondaryFragData[" << getResources().MaxDualSourceDrawBuffers
                 << "];\n";
        }
    }

    // Write translated shader.
    TOutputGLSL outputGLSL(sink,
                           getArrayIndexClampingStrategy(),
                           getHashFunction(),
                           getNameMap(),
                           getSymbolTable(),
                           getShaderVersion(),
                           getOutputType());
    root->traverse(&outputGLSL);
}

void TranslatorGLSL::writeVersion(TIntermNode *root)
{
    TVersionGLSL versionGLSL(getShaderType(), getPragma(), getOutputType());
    root->traverse(&versionGLSL);
    int version = versionGLSL.getVersion();
    // We need to write version directive only if it is greater than 110.
    // If there is no version directive in the shader, 110 is implied.
    if (version > 110)
    {
        TInfoSinkBase& sink = getInfoSink().obj;
        sink << "#version " << version << "\n";
    }
}

void TranslatorGLSL::writeExtensionBehavior(TIntermNode *root)
{
    TInfoSinkBase& sink = getInfoSink().obj;
    const TExtensionBehavior& extBehavior = getExtensionBehavior();
    for (const auto &iter : extBehavior)
    {
        if (iter.second == EBhUndefined)
        {
            continue;
        }

        if (getOutputType() == SH_GLSL_COMPATIBILITY_OUTPUT)
        {
            // For GLSL output, we don't need to emit most extensions explicitly,
            // but some we need to translate in GL compatibility profile.
            if (iter.first == "GL_EXT_shader_texture_lod")
            {
                sink << "#extension GL_ARB_shader_texture_lod : " << getBehaviorString(iter.second)
                     << "\n";
            }

            if (iter.first == "GL_EXT_draw_buffers")
            {
                sink << "#extension GL_ARB_draw_buffers : " << getBehaviorString(iter.second)
                     << "\n";
            }
        }
    }

    // GLSL ES 3 explicit location qualifiers need to use an extension before GLSL 330
    if (getShaderVersion() >= 300 && getOutputType() < SH_GLSL_330_CORE_OUTPUT)
    {
        sink << "#extension GL_ARB_explicit_attrib_location : require\n";
    }

    TExtensionGLSL extensionGLSL(getOutputType());
    root->traverse(&extensionGLSL);

    for (const auto &ext : extensionGLSL.getEnabledExtensions())
    {
        sink << "#extension " << ext << " : enable\n";
    }
    for (const auto &ext : extensionGLSL.getRequiredExtensions())
    {
        sink << "#extension " << ext << " : require\n";
    }
}

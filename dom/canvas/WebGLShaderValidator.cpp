/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebGLShaderValidator.h"

#include "angle/ShaderLang.h"
#include "gfxPrefs.h"
#include "GLContext.h"
#include "mozilla/Preferences.h"
#include "MurmurHash3.h"
#include "nsPrintfCString.h"
#include <regex>
#include <string>
#include <vector>
#include "WebGLContext.h"

namespace mozilla {
namespace webgl {

uint64_t
IdentifierHashFunc(const char* name, size_t len)
{
    // NB: we use the x86 function everywhere, even though it's suboptimal perf
    // on x64.  They return different results; not sure if that's a requirement.
    uint64_t hash[2];
    MurmurHash3_x86_128(name, len, 0, hash);
    return hash[0];
}

static ShCompileOptions
ChooseValidatorCompileOptions(const ShBuiltInResources& resources,
                              const mozilla::gl::GLContext* gl)
{
    ShCompileOptions options = SH_VARIABLES |
                               SH_ENFORCE_PACKING_RESTRICTIONS |
                               SH_OBJECT_CODE |
                               SH_INIT_GL_POSITION;

    // Sampler arrays indexed with non-constant expressions are forbidden in
    // GLSL 1.30 and later.
    // ESSL 3 requires constant-integral-expressions for this as well.
    // Just do it universally.
    options |= SH_UNROLL_FOR_LOOP_WITH_SAMPLER_ARRAY_INDEX;

#ifndef XP_MACOSX
    // We want to do this everywhere, but to do this on Mac, we need
    // to do it only on Mac OSX > 10.6 as this causes the shader
    // compiler in 10.6 to crash
    options |= SH_CLAMP_INDIRECT_ARRAY_BOUNDS;
#endif

#ifdef XP_MACOSX
    if (gl->WorkAroundDriverBugs()) {
        // Work around https://bugs.webkit.org/show_bug.cgi?id=124684,
        // https://chromium.googlesource.com/angle/angle/+/5e70cf9d0b1bb
        options |= SH_UNFOLD_SHORT_CIRCUIT;

        // Work around that Mac drivers handle struct scopes incorrectly.
        options |= SH_REGENERATE_STRUCT_NAMES;
        options |= SH_INIT_OUTPUT_VARIABLES;
    }
#endif

    if (gfxPrefs::WebGLAllANGLEOptions()) {
        options = -1;

        options ^= SH_INTERMEDIATE_TREE;
        options ^= SH_LINE_DIRECTIVES;
        options ^= SH_SOURCE_PATH;

        options ^= SH_LIMIT_EXPRESSION_COMPLEXITY;
        options ^= SH_LIMIT_CALL_STACK_DEPTH;

        options ^= SH_EXPAND_SELECT_HLSL_INTEGER_POW_EXPRESSIONS;
        options ^= SH_HLSL_GET_DIMENSIONS_IGNORES_BASE_LEVEL;

        options ^= SH_DONT_REMOVE_INVARIANT_FOR_FRAGMENT_INPUT;
        options ^= SH_REMOVE_INVARIANT_AND_CENTROID_FOR_ESSL3;
    }

    if (resources.MaxExpressionComplexity > 0) {
        options |= SH_LIMIT_EXPRESSION_COMPLEXITY;
    }
    if (resources.MaxCallStackDepth > 0) {
        options |= SH_LIMIT_CALL_STACK_DEPTH;
    }

    return options;
}

////////////////////////////////////////

static ShShaderOutput
ShaderOutput(gl::GLContext* gl)
{
    if (gl->IsGLES()) {
        return SH_ESSL_OUTPUT;
    } else {
        uint32_t version = gl->ShadingLanguageVersion();
        switch (version) {
        case 100: return SH_GLSL_COMPATIBILITY_OUTPUT;
        case 120: return SH_GLSL_COMPATIBILITY_OUTPUT;
        case 130: return SH_GLSL_130_OUTPUT;
        case 140: return SH_GLSL_140_OUTPUT;
        case 150: return SH_GLSL_150_CORE_OUTPUT;
        case 330: return SH_GLSL_330_CORE_OUTPUT;
        case 400: return SH_GLSL_400_CORE_OUTPUT;
        case 410: return SH_GLSL_410_CORE_OUTPUT;
        case 420: return SH_GLSL_420_CORE_OUTPUT;
        case 430: return SH_GLSL_430_CORE_OUTPUT;
        case 440: return SH_GLSL_440_CORE_OUTPUT;
        case 450: return SH_GLSL_450_CORE_OUTPUT;
        default:
            MOZ_CRASH("GFX: Unexpected GLSL version.");
        }
    }

    return SH_GLSL_COMPATIBILITY_OUTPUT;
}

/*static*/ void
ShaderValidator::ChooseResources(const WebGLContext* webgl, ShBuiltInResources* res)
{
    memset(res, 0, sizeof(*res));
    sh::InitBuiltInResources(res);

    res->HashFunction = webgl::IdentifierHashFunc;

    res->MaxVertexAttribs             = webgl->mGLMaxVertexAttribs;
    res->MaxVertexUniformVectors      = webgl->mGLMaxVertexUniformVectors;
    res->MaxVaryingVectors            = webgl->mGLMaxVaryingVectors;
    res->MaxVertexTextureImageUnits   = webgl->mGLMaxVertexTextureImageUnits;
    res->MaxCombinedTextureImageUnits = webgl->mGLMaxTextureUnits;
    res->MaxTextureImageUnits         = webgl->mGLMaxTextureImageUnits;
    res->MaxFragmentUniformVectors    = webgl->mGLMaxFragmentUniformVectors;
    res->MaxDrawBuffers               = webgl->mImplMaxDrawBuffers;

    // Tell ANGLE to allow highp in frag shaders. (unless disabled)
    // If underlying GLES doesn't have highp in frag shaders, it should complain anyways.
    res->FragmentPrecisionHigh = 1;

    if (webgl->mDisableFragHighP) {
        res->FragmentPrecisionHigh = 0;
    }

    res->EXT_frag_depth           = webgl->IsExtensionEnabled(WebGLExtensionID::EXT_frag_depth);
    res->OES_standard_derivatives = webgl->IsExtensionEnabled(WebGLExtensionID::OES_standard_derivatives);
    res->EXT_draw_buffers         = webgl->IsExtensionEnabled(WebGLExtensionID::WEBGL_draw_buffers);
    res->EXT_shader_texture_lod   = webgl->IsExtensionEnabled(WebGLExtensionID::EXT_shader_texture_lod);

    if (webgl->gl->WorkAroundDriverBugs()) {
#ifdef XP_MACOSX
        if (webgl->gl->Vendor() == gl::GLVendor::NVIDIA) {
            // Work around bug 890432
            res->MaxExpressionComplexity = 1000;
        }
#endif
    }
}

ShaderValidator::ShaderValidator(const WebGLContext* webgl)
{
    const auto spec = (webgl->IsWebGL2() ? SH_WEBGL2_SPEC : SH_WEBGL_SPEC);
    const auto outputLang = ShaderOutput(webgl->gl);

    ShBuiltInResources resources;
    ChooseResources(webgl, &resources);

    mCompileOptions = ChooseValidatorCompileOptions(resources, webgl->gl);
    mVertCompiler = sh::ConstructCompiler(LOCAL_GL_VERTEX_SHADER  , spec, outputLang, &resources);
    mFragCompiler = sh::ConstructCompiler(LOCAL_GL_FRAGMENT_SHADER, spec, outputLang, &resources);
    MOZ_RELEASE_ASSERT(mVertCompiler);
    MOZ_RELEASE_ASSERT(mFragCompiler);

#ifdef DEBUG
    mWebGL = webgl;
    mResources = resources;
#endif
}

ShaderValidator::~ShaderValidator()
{
    ShDestruct(mVertCompiler);
    ShDestruct(mFragCompiler);
}

UniquePtr<const ShaderInfo>
ShaderValidator::Compile(GLenum shaderType, const char* source,
                         nsCString* const out_infoLog) const
{
#ifdef DEBUG
    ShBuiltInResources resources;
    ChooseResources(mWebGL, &resources);
    MOZ_ASSERT(memcmp(&mResources, &resources, sizeof(resources)) == 0);
#endif

    const ShHandle* pCompiler;
    switch (shaderType) {
    case LOCAL_GL_VERTEX_SHADER:
        pCompiler = &mVertCompiler;
        break;

    case LOCAL_GL_FRAGMENT_SHADER:
        pCompiler = &mFragCompiler;
        break;

    default:
        MOZ_CRASH();
    }
    const auto& compiler = *pCompiler;

    UniquePtr<ShaderInfo> info;
    if (sh::Compile(compiler, &source, 1, mCompileOptions)) {
        info.reset(new ShaderInfo);

        info->shaderVersion = sh::GetShaderVersion(compiler);
        info->translatedSource = sh::GetObjectCode(compiler);

        info->uniforms = *sh::GetUniforms(compiler);
        info->varyings = *sh::GetVaryings(compiler);
        info->attribs  = *sh::GetAttributes(compiler);
        info->outputs  = *sh::GetOutputVariables(compiler);
        info->blocks   = *sh::GetInterfaceBlocks(compiler);

        for (const auto& itr : *sh::GetNameHashingMap(compiler)) {
            info->mapName.insert({itr.first, itr.second});
            info->unmapName.insert({itr.second, itr.first});
        }
    }

    *out_infoLog = sh::GetInfoLog(compiler).c_str();

    sh::ClearResults(compiler);
    return Move(info);
}

////////////////////////////////////////////////////////////////////////////////

template<size_t N>
static inline bool
StartsWith(const std::string& haystack, const char (&needle)[N])
{
    return haystack.compare(0, N - 1, needle) == 0;
}

bool
ShaderInfo::CanLinkToVert(const ShaderInfo& vert, const WebGLContext* webgl,
                          nsCString* const out_log) const
{
    if (shaderVersion != vert.shaderVersion) {
        *out_log = nsPrintfCString("Fragment shader version %u does not match vertex"
                                   " shader version %u.",
                                   shaderVersion, vert.shaderVersion);
        return false;
    }

    for (const auto& fragVar : uniforms) {
        for (const auto& vertVar : vert.uniforms) {
            if (vertVar.name != fragVar.name)
                continue;

            if (!fragVar.isSameUniformAtLinkTime(vertVar)) {
                *out_log = nsPrintfCString("Uniform `%s` is not linkable between"
                                           " attached shaders.",
                                           fragVar.name.c_str());
                return false;
            }
            break;
        }
    }

    for (const auto& fragVar : blocks) {
        for (const auto& vertVar : vert.blocks) {
            if (vertVar.name != fragVar.name)
                continue;

            if (!fragVar.isSameInterfaceBlockAtLinkTime(vertVar)) {
                *out_log = nsPrintfCString("Interface block `%s` is not linkable between"
                                           " attached shaders.",
                                           fragVar.name.c_str());
                return false;
            }
            break;
        }
    }

    {
        std::vector<sh::ShaderVariable> staticUseVaryingList;

        for (const auto& fragVar : varyings) {
            if (fragVar.isBuiltIn()) {
                if (fragVar.staticUse) {
                    staticUseVaryingList.push_back(fragVar);
                }
                continue;
            }

            bool definedInVertShader = false;
            bool staticVertUse = false;

            for (const auto& vertVar : vert.varyings) {
                if (vertVar.name != fragVar.name)
                    continue;

                if (!fragVar.isSameVaryingAtLinkTime(vertVar, shaderVersion)) {
                    *out_log = nsPrintfCString("Varying `%s`is not linkable between"
                                               " attached shaders.",
                                               fragVar.name.c_str());
                    return false;
                }

                definedInVertShader = true;
                staticVertUse = vertVar.staticUse;
                break;
            }

            if (!definedInVertShader && fragVar.staticUse) {
                *out_log = nsPrintfCString("Varying `%s` has static-use in the frag"
                                           " shader, but is undeclared in the vert"
                                           " shader.",
                                           fragVar.name.c_str());
                return false;
            }

            if (staticVertUse && fragVar.staticUse) {
                staticUseVaryingList.push_back(fragVar);
            }
        }

        if (!ShCheckVariablesWithinPackingLimits(webgl->mGLMaxVaryingVectors,
                                                 staticUseVaryingList))
        {
            *out_log = "Statically used varyings do not fit within packing limits. (see"
                       " GLSL ES Specification 1.0.17, p111)";
            return false;
        }
    }

    if (shaderVersion == 100) {
        // Enforce ESSL1 invariant linking rules.
        bool isInvariant_Position = false;
        bool isInvariant_PointSize = false;
        bool isInvariant_FragCoord = false;
        bool isInvariant_PointCoord = false;

        for (const auto& vertVar : vert.varyings) {
            if (vertVar.name == "gl_Position") {
                isInvariant_Position = vertVar.isInvariant;
            } else if (vertVar.name == "gl_PointSize") {
                isInvariant_PointSize = vertVar.isInvariant;
            }
        }

        for (const auto& fragVar : varyings) {
            if (fragVar.name == "gl_FragCoord") {
                isInvariant_FragCoord = fragVar.isInvariant;
            } else if (fragVar.name == "gl_PointCoord") {
                isInvariant_PointCoord = fragVar.isInvariant;
            }
        }

        ////

        const auto fnCanBuiltInsLink = [](bool vertIsInvariant, bool fragIsInvariant) {
            if (vertIsInvariant)
                return true;

            return !fragIsInvariant;
        };

        if (!fnCanBuiltInsLink(isInvariant_Position, isInvariant_FragCoord)) {
            *out_log = "gl_Position must be invariant if gl_FragCoord is. (see GLSL ES"
                       " Specification 1.0.17, p39)";
            return false;
        }

        if (!fnCanBuiltInsLink(isInvariant_PointSize, isInvariant_PointCoord)) {
            *out_log = "gl_PointSize must be invariant if gl_PointCoord is. (see GLSL ES"
                       " Specification 1.0.17, p39)";
            return false;
        }
    }

    const auto& gl = webgl->gl;

    if (gl->WorkAroundDriverBugs() &&
        webgl->mIsMesa)
    {
        // Bug 777028: Mesa can't handle more than 16 samplers per program, counting each
        // array entry.
        const auto fnCalcSamplers = [&](const ShaderInfo& info) {
            size_t accum = 0;
            for (const auto& cur : info.uniforms) {
                switch (cur.type) {
                case LOCAL_GL_SAMPLER_2D:
                case LOCAL_GL_SAMPLER_CUBE:
                    accum += cur.arraySize;
                    break;
                }
            }
            return accum;
        };
        const auto numSamplers_upperBound = fnCalcSamplers(vert) + fnCalcSamplers(*this);
        if (numSamplers_upperBound > 16) {
            *out_log = "Programs with more than 16 samplers are disallowed on Mesa"
                       " drivers to avoid crashing.";
            return false;
        }

        // Bug 1203135: Mesa crashes internally if we exceed the reported maximum
        // attribute count.
        if (vert.attribs.size() > webgl->mGLMaxVertexAttribs) {
            *out_log = "Number of attributes exceeds Mesa's reported max attribute"
                       " count.";
            return false;
        }
    }

    return true;
}

//const std::string var("foo.bar[3].qux[10]")
//const std::sregex_token_iterator itr(var.begin(), var.end(), kRegex_GLSLVar, {-1,0});
//const std::vector<std::string> parts(itr, std::sregex_token_iterator());
//  => ||foo|.|bar|[3].|qux|[10]|

/*static*/ std::string
ShaderInfo::MapNameWith(const std::string& srcName,
                        const decltype(ShaderInfo::mapName)& map)
{
    static const std::regex kRegex_GLSLVar("[a-zA-Z_][a-zA-Z_0-9]*");
    const std::vector<std::string> srcParts(std::sregex_token_iterator(srcName.begin(),
                                                                       srcName.end(),
                                                                       kRegex_GLSLVar,
                                                                       {-1,0}),
                                            std::sregex_token_iterator());
    std::vector<const std::string*> dstParts;
    dstParts.reserve(srcParts.size());
    size_t dstNameSize = 0;
    for (const auto& src : srcParts) {
        const auto itr = map.find(src);
        const std::string* dst;
        if (itr == map.end()) {
            dst = &src;
        } else {
            dst = &(itr->second);
        }
        dstParts.push_back(dst);
        dstNameSize += dst->size();
    }

    std::string dstName;
    dstName.reserve(dstNameSize);
    for (const auto& dst : dstParts) {
        dstName += *dst;
    }
    return dstName;
}

////

template<typename T>
size_t MemSize(const T& x);

size_t
IndirectMemSize(int64_t)
{
    return 0;
}

size_t
IndirectMemSize(uint64_t)
{
    return 0;
}

size_t
IndirectMemSize(const std::string& x)
{
    return x.size();
}

template<typename K, typename V>
size_t
IndirectMemSize(const std::map<K, V>& x)
{
    size_t ret = 0;
    for (const auto& pair : x) {
        ret += MemSize(pair.first);
        ret += MemSize(pair.second);
    }
    return ret;
}

template<typename T>
size_t
IndirectMemSize(const std::vector<T>& x)
{
    size_t ret = 0;
    for (const auto& cur : x) {
        ret += MemSize(cur);
    }
    return ret;
}

size_t
IndirectMemSize(const sh::ShaderVariable& x)
{
    return IndirectMemSize(x.name) +
           IndirectMemSize(x.mappedName) +
           IndirectMemSize(x.fields) +
           IndirectMemSize(x.structName);
}

size_t
IndirectMemSize(const sh::InterfaceBlock& x)
{
    return IndirectMemSize(x.name) +
           IndirectMemSize(x.mappedName) +
           IndirectMemSize(x.instanceName) +
           IndirectMemSize(x.fields);
}

template<typename T>
size_t MemSize(const T& x)
{
    return sizeof(x) + IndirectMemSize(x);
}

size_t
ShaderInfo::MemSize() const
{
    return sizeof(*this) +
           IndirectMemSize(translatedSource) +
           IndirectMemSize(uniforms) +
           IndirectMemSize(varyings) +
           IndirectMemSize(attribs) +
           IndirectMemSize(outputs) +
           IndirectMemSize(blocks) +
           IndirectMemSize(mapName) +
           IndirectMemSize(unmapName);
}

} // namespace webgl
} // namespace mozilla

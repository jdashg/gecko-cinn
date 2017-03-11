/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef WEBGL_SHADER_VALIDATOR_H_
#define WEBGL_SHADER_VALIDATOR_H_

#include "angle/ShaderLang.h"
#include "mozilla/UniquePtr.h"
#include "GLDefs.h"
#include "nsString.h"
#include <map>
#include <string>

namespace mozilla {
class WebGLContext;

namespace webgl {
struct ShaderInfo;

class ShaderValidator final
{
    ShCompileOptions mCompileOptions;
    ShHandle mVertCompiler;
    ShHandle mFragCompiler;

#ifdef DEBUG
    const WebGLContext* mWebGL;
    ShBuiltInResources mResources;
#endif

    static void ChooseResources(const WebGLContext* webgl, ShBuiltInResources* res);

public:
    explicit ShaderValidator(const WebGLContext* webgl);
    virtual ~ShaderValidator();

public:
    UniquePtr<const ShaderInfo> Compile(GLenum shaderType, const char* source,
                                        nsCString* const out_infoLog) const;
};

struct ShaderInfo final
{
    std::string translatedSource;
    uint16_t shaderVersion;
    std::vector<sh::Uniform> uniforms;
    std::vector<sh::Varying> varyings;
    std::vector<sh::Attribute> attribs;
    std::vector<sh::OutputVariable> outputs;
    std::vector<sh::InterfaceBlock> blocks;

    std::map<std::string, const std::string> mapName;
    std::map<std::string, const std::string> unmapName;

    bool CanLinkToVert(const ShaderInfo& vert, const WebGLContext* webgl,
                       nsCString* const out_log) const;

    static std::string MapNameWith(const std::string& srcName,
                                   const decltype(mapName)& map);

    std::string MapName(const std::string& userName) const {
        return MapNameWith(userName, mapName);
    }
    //std::string UnmapName(const std::string& mappedName) const {
    //    return MapNameWith(mappedName, unmapName);
    //}

    size_t MemSize() const;
};

} // namespace webgl
} // namespace mozilla

#endif // WEBGL_SHADER_VALIDATOR_H_

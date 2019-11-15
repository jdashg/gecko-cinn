/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebGL2Context.h"
#include "WebGLSampler.h"
#include "GLContext.h"

namespace mozilla {

already_AddRefed<WebGLSampler> WebGL2Context::CreateSampler() {
  const FuncScope funcScope(*this, "createSampler");
  if (IsContextLost()) return nullptr;

  RefPtr<WebGLSampler> globj = new WebGLSampler(this);
  return globj.forget();
}

void WebGL2Context::DeleteSampler(WebGLSampler* sampler) {
  const FuncScope funcScope(*this, "deleteSampler");
  if (!ValidateDeleteObject(sampler)) return;

  for (auto& slot : mBoundSamplers) {
    if (slot == sampler) {
      slot = nullptr;
    }
  }

  sampler->RequestDelete();
}

void WebGL2Context::BindSampler(GLuint unit, WebGLSampler* sampler) {
  const FuncScope funcScope(*this, "bindSampler");
  if (IsContextLost()) return;

  if (sampler && !ValidateObject("sampler", *sampler)) return;

  if (unit >= mBoundSamplers.Length())
    return ErrorInvalidValue("unit must be < %u", mBoundSamplers.Length());

  ////

  gl->fBindSampler(unit, sampler ? sampler->mGLName : 0);

  mBoundSamplers[unit] = sampler;
}

void WebGL2Context::SamplerParameteri(WebGLSampler& sampler, GLenum pname,
                                      GLint param) {
  const FuncScope funcScope(*this, "samplerParameteri");
  if (IsContextLost()) return;

  if (!ValidateObject("sampler", sampler)) return;

  sampler.SamplerParameter(pname, FloatOrInt(param));
}

void WebGL2Context::SamplerParameterf(WebGLSampler& sampler, GLenum pname,
                                      GLfloat param) {
  const FuncScope funcScope(*this, "samplerParameterf");
  if (IsContextLost()) return;

  if (!ValidateObject("sampler", sampler)) return;

  sampler.SamplerParameter(pname, FloatOrInt(param));
}

Maybe<double> WebGL2Context::GetSamplerParameter(
    const WebGLSampler& sampler, GLenum pname) const {
  const FuncScope funcScope(*this, "getSamplerParameter");
  if (IsContextLost()) return {};

  if (!ValidateObject("sampler", sampler)) return {};

  ////

  switch (pname) {
    case LOCAL_GL_TEXTURE_MIN_FILTER:
    case LOCAL_GL_TEXTURE_MAG_FILTER:
    case LOCAL_GL_TEXTURE_WRAP_S:
    case LOCAL_GL_TEXTURE_WRAP_T:
    case LOCAL_GL_TEXTURE_WRAP_R:
    case LOCAL_GL_TEXTURE_COMPARE_MODE:
    case LOCAL_GL_TEXTURE_COMPARE_FUNC: {
      GLint param = 0;
      gl->fGetSamplerParameteriv(sampler.mGLName, pname, &param);
      return Some(param);
    }
    case LOCAL_GL_TEXTURE_MIN_LOD:
    case LOCAL_GL_TEXTURE_MAX_LOD: {
      GLfloat param = 0;
      gl->fGetSamplerParameterfv(sampler.mGLName, pname, &param);
      return Some(param);
    }

    default:
      ErrorInvalidEnumInfo("pname", pname);
      return {};
  }
}

}  // namespace mozilla

/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebGLContext.h"
#include "WebGLContextUtils.h"
#include "WebGLBuffer.h"
#include "WebGLVertexAttribData.h"
#include "WebGLShader.h"
#include "WebGLProgram.h"
#include "WebGLFramebuffer.h"
#include "WebGLRenderbuffer.h"
#include "WebGLTexture.h"
#include "WebGLExtensions.h"
#include "WebGLVertexArray.h"

#include "nsString.h"
#include "nsDebug.h"
#include "nsReadableUtils.h"

#include "gfxContext.h"
#include "gfxPlatform.h"
#include "GLContext.h"

#include "nsContentUtils.h"
#include "nsError.h"
#include "nsLayoutUtils.h"

#include "CanvasUtils.h"
#include "gfxUtils.h"

#include "jsfriendapi.h"

#include "WebGLTexelConversions.h"
#include "WebGLValidateStrings.h"
#include <algorithm>

// needed to check if current OS is lower than 10.7
#if defined(MOZ_WIDGET_COCOA)
#  include "nsCocoaFeatures.h"
#endif

#include "mozilla/DebugOnly.h"
#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/ImageData.h"
#include "mozilla/dom/ToJSValue.h"
#include "mozilla/EndianUtils.h"

namespace mozilla {

static bool IsValidTexTarget(WebGLContext* webgl, uint8_t funcDims,
                             GLenum rawTexTarget, TexTarget* const out) {
  uint8_t targetDims;

  switch (rawTexTarget) {
    case LOCAL_GL_TEXTURE_2D:
    case LOCAL_GL_TEXTURE_CUBE_MAP:
      targetDims = 2;
      break;

    case LOCAL_GL_TEXTURE_3D:
    case LOCAL_GL_TEXTURE_2D_ARRAY:
      if (!webgl->IsWebGL2()) return false;

      targetDims = 3;
      break;

    default:
      return false;
  }

  // Some funcs (like GenerateMipmap) doesn't know the dimension, so don't check
  // it.
  if (funcDims && targetDims != funcDims) return false;

  *out = rawTexTarget;
  return true;
}

bool ValidateTexTarget(WebGLContext* webgl, uint8_t funcDims,
                       GLenum rawTexTarget, TexTarget* const out_texTarget,
                       WebGLTexture** const out_tex) {
  if (webgl->IsContextLost()) return false;

  TexTarget texTarget;
  if (!IsValidTexTarget(webgl, funcDims, rawTexTarget, &texTarget)) {
    webgl->ErrorInvalidEnumInfo("texTarget", rawTexTarget);
    return false;
  }

  WebGLTexture* tex = webgl->ActiveBoundTextureForTarget(texTarget);
  if (!tex) {
    webgl->ErrorInvalidOperation("No texture is bound to this target.");
    return false;
  }

  *out_texTarget = texTarget;
  *out_tex = tex;
  return true;
}

/*virtual*/
bool WebGLContext::IsTexParamValid(GLenum pname) const {
  switch (pname) {
    case LOCAL_GL_TEXTURE_MIN_FILTER:
    case LOCAL_GL_TEXTURE_MAG_FILTER:
    case LOCAL_GL_TEXTURE_WRAP_S:
    case LOCAL_GL_TEXTURE_WRAP_T:
      return true;

    case LOCAL_GL_TEXTURE_MAX_ANISOTROPY_EXT:
      return IsExtensionEnabled(
          WebGLExtensionID::EXT_texture_filter_anisotropic);

    default:
      return false;
  }
}

//////////////////////////////////////////////////////////////////////////////////////////
// GL calls

void WebGLContext::BindTexture(GLenum rawTarget, WebGLTexture* newTex) {
  const FuncScope funcScope(*this, "bindTexture");
  if (IsContextLost()) return;

  if (newTex && !ValidateObject("tex", *newTex)) return;

  // Need to check rawTarget first before comparing against newTex->Target() as
  // newTex->Target() returns a TexTarget, which will assert on invalid value.
  WebGLRefPtr<WebGLTexture>* currentTexPtr = nullptr;
  switch (rawTarget) {
    case LOCAL_GL_TEXTURE_2D:
      currentTexPtr = &mBound2DTextures[mActiveTexture];
      break;

    case LOCAL_GL_TEXTURE_CUBE_MAP:
      currentTexPtr = &mBoundCubeMapTextures[mActiveTexture];
      break;

    case LOCAL_GL_TEXTURE_3D:
      if (IsWebGL2()) currentTexPtr = &mBound3DTextures[mActiveTexture];
      break;

    case LOCAL_GL_TEXTURE_2D_ARRAY:
      if (IsWebGL2()) currentTexPtr = &mBound2DArrayTextures[mActiveTexture];
      break;
  }

  if (!currentTexPtr) {
    ErrorInvalidEnumInfo("target", rawTarget);
    return;
  }

  const TexTarget texTarget(rawTarget);
  if (newTex) {
    if (!newTex->BindTexture(texTarget)) return;
  } else {
    gl->fBindTexture(texTarget.get(), 0);
  }

  *currentTexPtr = newTex;
}

void WebGLContext::GenerateMipmap(GLenum rawTexTarget) {
  const FuncScope funcScope(*this, "generateMipmap");
  const uint8_t funcDims = 0;

  TexTarget texTarget;
  WebGLTexture* tex;
  if (!ValidateTexTarget(this, funcDims, rawTexTarget, &texTarget, &tex))
    return;

  tex->GenerateMipmap();
}

Maybe<double> WebGLContext::GetTexParameter(const WebGLTexture& tex,
                                                GLenum pname) const {
  const FuncScope funcScope(*this, "getTexParameter");

  if (!IsTexParamValid(pname)) {
    ErrorInvalidEnumInfo("pname", pname);
    return Nothing();
  }

  return tex.GetTexParameter(pname);
}

void WebGLContext::TexParameter_base(GLenum rawTexTarget, GLenum pname,
                                     const FloatOrInt& param) {
  const FuncScope funcScope(*this, "texParameter");
  const uint8_t funcDims = 0;

  TexTarget texTarget;
  WebGLTexture* tex;
  if (!ValidateTexTarget(this, funcDims, rawTexTarget, &texTarget, &tex))
    return;

  tex->TexParameter(texTarget, pname, param);
}

//////////////////////////////////////////////////////////////////////////////////////////
// Uploads

static bool IsTexTarget3D(const GLenum texTarget) {
  switch (texTarget) {
    case LOCAL_GL_TEXTURE_2D_ARRAY:
    case LOCAL_GL_TEXTURE_3D:
      return true;

    default:
      return false;
  }
}


void WebGLContext::TexStorage(GLenum texTarget,
                               uint32_t levels, GLenum internalFormat,
                               uvec3 size) const {
  if (!IsTexTarget3D(texTarget)) {
    size.z = 1;
  }
  const auto tex = GetActiveTex(texTarget);
  tex->TexStorage(texTarget, levels, internalFormat, size);
}

void WebGLContext::TexImage(GLenum imageTarget, uint32_t level,
                            GLenum respecFormat, uvec3 offset, uvec3 size,
                            const webgl::PackingInfo& pi,
                            const TexImageSource& src,
                            const dom::HTMLCanvasElement& canvas) const {
  if (respecFormat) {
    offset = {0,0,0};
  }
  const auto texTarget = ImageToTexTarget(imageTarget);
  if (!IsTexTarget3D(texTarget)) {
    size.z = 1;
  }
  const auto tex = GetActiveTex(texTarget);
  tex->TexImage(imageTarget, level, respecFormat, offset, size, pi, src, canvas);
}

void WebGLContext::CompressedTexImage(bool sub, GLenum imageTarget,
                                      uint32_t level, GLenum format,
                                      uvec3 offset, uvec3 size,
                                      const Range<const uint8_t>& src,
                                      const uint32_t pboImageSize,
                                      const Maybe<uint64_t> pboOffset) const {
  if (!sub) {
    offset = {0,0,0};
  }
  const auto texTarget = ImageToTexTarget(imageTarget);
  if (!IsTexTarget3D(texTarget)) {
    size.z = 1;
  }
  const auto tex = GetActiveTex(texTarget);
  tex->CompressedTexImage(sub, imageTarget, level, format, offset, size,
        src, pboImageSize, pboOffset);
}

void WebGLContext::CopyTexImage(GLenum imageTarget, uint32_t level,
                                  GLenum respecFormat, uvec3 dstOffset,
                                  const ivec2& srcOffset,
                                  const uvec2& size) const {
  if (respecFormat) {
    dstOffset = {0,0,0};
  }
  const auto tex = GetActiveTex(ImageToTexTarget(imageTarget));
  tex->CopyTexImage(imageTarget, level, respecFormat, dstOffset, srcOffset, size);
}

}  // namespace mozilla

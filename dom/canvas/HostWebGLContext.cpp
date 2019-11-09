/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "HostWebGLContext.h"

#include "CompositableHost.h"
#include "mozilla/layers/LayerTransactionChild.h"
#include "mozilla/layers/TextureClientSharedSurface.h"

#include "TexUnpackBlob.h"
#include "WebGL2Context.h"
#include "WebGLBuffer.h"
#include "WebGLContext.h"
#include "WebGLCrossProcessCommandQueue.h"
#include "WebGLFramebuffer.h"
#include "WebGLParent.h"
#include "WebGLProgram.h"
#include "WebGLRenderbuffer.h"
#include "WebGLSampler.h"
#include "WebGLShader.h"
#include "WebGLSync.h"
#include "WebGLTexture.h"
#include "WebGLTransformFeedback.h"
#include "WebGLVertexArray.h"
#include "WebGLQuery.h"

namespace mozilla {

LazyLogModule gWebGLBridgeLog("webglbridge");

#define DEFINE_OBJECT_ID_MAP_FUNCS(_WebGLType)                                 \
  WebGLId<WebGL##_WebGLType> HostWebGLContext::Insert(                         \
      RefPtr<WebGL##_WebGLType>&& aObj, const WebGLId<WebGL##_WebGLType>& aId) \
      const {                                                                  \
    return m##_WebGLType##Map.Insert(std::move(aObj), aId);                    \
  }                                                                            \
  WebGL##_WebGLType* HostWebGLContext::Find(                                   \
      const WebGLId<WebGL##_WebGLType>& aId) const {                           \
    return m##_WebGLType##Map.Find(aId);                                       \
  }                                                                            \
  void HostWebGLContext::Remove(const WebGLId<WebGL##_WebGLType>& aId) const { \
    return m##_WebGLType##Map.Remove(aId);                                     \
  }

DEFINE_OBJECT_ID_MAP_FUNCS(Framebuffer);
DEFINE_OBJECT_ID_MAP_FUNCS(Program);
DEFINE_OBJECT_ID_MAP_FUNCS(Query);
DEFINE_OBJECT_ID_MAP_FUNCS(Renderbuffer);
DEFINE_OBJECT_ID_MAP_FUNCS(Sampler);
DEFINE_OBJECT_ID_MAP_FUNCS(Shader);
DEFINE_OBJECT_ID_MAP_FUNCS(Sync);
DEFINE_OBJECT_ID_MAP_FUNCS(TransformFeedback);
DEFINE_OBJECT_ID_MAP_FUNCS(VertexArray);
DEFINE_OBJECT_ID_MAP_FUNCS(Buffer);
DEFINE_OBJECT_ID_MAP_FUNCS(Texture);

/*static*/
UniquePtr<HostWebGLContext> HostWebGLContext::Create(
    OwnerData&& ownerData, const webgl::InitContextDesc& desc,
    webgl::InitContextResult* const out) {
  auto host = WrapUnique(new HostWebGLContext(std::move(ownerData)));
  auto webgl = WebGLContext::Create(*host, desc, out);
  if (!webgl) return nullptr;
  return host;
}

HostWebGLContext::HostWebGLContext(OwnerData&& ownerData)
    : mOwnerData(std::move(ownerData)) {
  if (mOwnerData.outOfProcess) {
    mOwnerData.outOfProcess->mCommandSink->mHostContext = this;
  }
}

HostWebGLContext::~HostWebGLContext() = default;

CommandResult HostWebGLContext::RunCommandsForDuration(TimeDuration aDuration) {
  return mOwnerData.outOfProcess->mCommandSink->ProcessUpToDuration(aDuration);
}

void HostWebGLContext::SetCompositableHost(
    RefPtr<CompositableHost>& aCompositableHost) {
  mContext->SetCompositableHost(aCompositableHost);
}

// -

void HostWebGLContext::OnContextLoss(const webgl::ContextLossReason reason) {
  if (mOwnerData.inProcess) {
    (*mOwnerData.inProcess)->OnContextLoss(reason);
  } else {
    (void)mOwnerData.outOfProcess->mParent.SendOnContextLoss(reason);
  }
}

void HostWebGLContext::Present() { mContext->Present(); }

// -

void HostWebGLContext::CreateFramebuffer(const WebGLId<WebGLFramebuffer>& aId) {
  Insert(mContext->CreateFramebuffer(), aId);
}

void HostWebGLContext::CreateProgram(const WebGLId<WebGLProgram>& aId) {
  Insert(mContext->CreateProgram(), aId);
}

void HostWebGLContext::CreateRenderbuffer(
    const WebGLId<WebGLRenderbuffer>& aId) {
  Insert(mContext->CreateRenderbuffer(), aId);
}

void HostWebGLContext::CreateShader(GLenum aType,
                                    const WebGLId<WebGLShader>& aId) {
  Insert(mContext->CreateShader(aType), aId);
}

WebGLId<WebGLBuffer> HostWebGLContext::CreateBuffer() {
  return Insert(RefPtr<WebGLBuffer>(mContext->CreateBuffer()));
}

WebGLId<WebGLTexture> HostWebGLContext::CreateTexture() {
  return Insert(RefPtr<WebGLTexture>(mContext->CreateTexture()));
}

void HostWebGLContext::CreateSampler(const WebGLId<WebGLSampler>& aId) {
  Insert(GetWebGL2Context()->CreateSampler(), aId);
}

WebGLId<WebGLSync> HostWebGLContext::FenceSync(const WebGLId<WebGLSync>& aId,
                                               GLenum condition,
                                               GLbitfield flags) {
  return Insert(GetWebGL2Context()->FenceSync(condition, flags), aId);
}

void HostWebGLContext::CreateTransformFeedback(
    const WebGLId<WebGLTransformFeedback>& aId) {
  Insert(GetWebGL2Context()->CreateTransformFeedback(), aId);
}

void HostWebGLContext::CreateVertexArray(const WebGLId<WebGLVertexArray>& aId) {
  Insert(mContext->CreateVertexArray(), aId);
}

void HostWebGLContext::CreateQuery(const WebGLId<WebGLQuery>& aId) const {
  Insert(const_cast<WebGL2Context*>(GetWebGL2Context())->CreateQuery(), aId);
}

// ------------------------- Composition -------------------------
Maybe<ICRData> HostWebGLContext::InitializeCanvasRenderer(
    layers::LayersBackend backend) {
  return mContext->InitializeCanvasRenderer(backend);
}

void HostWebGLContext::Resize(const uvec2& size) {
  return mContext->Resize(size);
}

uvec2 HostWebGLContext::DrawingBufferSize() {
  return mContext->DrawingBufferSize();
}

void HostWebGLContext::OnMemoryPressure() {
  return mContext->OnMemoryPressure();
}

void HostWebGLContext::DidRefresh() { mContext->DidRefresh(); }

// ------------------------- GL State -------------------------

void HostWebGLContext::CopyTexImage2D(GLenum target, GLint level,
                                      GLenum internalFormat, GLint x, GLint y,
                                      uint32_t width, uint32_t height,
                                      uint32_t depth) {
  mContext->CopyTexImage2D(target, level, internalFormat, x, y, width, height,
                           depth);
}

void HostWebGLContext::TexStorage(uint8_t funcDims, GLenum target,
                                  GLsizei levels, GLenum internalFormat,
                                  GLsizei width, GLsizei height, GLsizei depth,
                                  FuncScopeId aFuncId) {
  const WebGLContext::FuncScope scope(*mContext, GetFuncScopeName(aFuncId));
  GetWebGL2Context()->TexStorage(funcDims, target, levels, internalFormat,
                                 width, height, depth);
}

template <typename TexUnpackType>
struct ToTexUnpackTypeMatcher {
  template <typename T, typename mozilla::EnableIf<
                            mozilla::IsConvertible<T*, TexUnpackType*>::value,
                            int>::Type = 0>
  UniquePtr<TexUnpackType> operator()(UniquePtr<T>& x) {
    return std::move(x);
  }
  template <typename T, typename mozilla::EnableIf<
                            !mozilla::IsConvertible<T*, TexUnpackType*>::value,
                            char>::Type = 0>
  UniquePtr<TexUnpackType> operator()(UniquePtr<T>& x) {
    MOZ_ASSERT_UNREACHABLE(
        "Attempted to read TexUnpackBlob as something it was not");
    return nullptr;
  }
  UniquePtr<TexUnpackType> operator()(WebGLTexPboOffset& aPbo) {
    UniquePtr<webgl::TexUnpackBytes> bytes = mContext->ToTexUnpackBytes(aPbo);
    return operator()(bytes);
  }
  WebGLContext* mContext;
};

template <typename TexUnpackType>
UniquePtr<TexUnpackType> AsTexUnpackType(WebGLContext* aContext,
                                         MaybeWebGLTexUnpackVariant&& src) {
  if (!src) {
    return nullptr;
  }
  if ((!src.ref().is<WebGLTexPboOffset>()) &&
      (!aContext->ValidateNullPixelUnpackBuffer())) {
    return nullptr;
  }

  return src.ref().match(ToTexUnpackTypeMatcher<TexUnpackType>{aContext});
}

void HostWebGLContext::TexImage(uint8_t funcDims, GLenum target, GLint level,
                                GLenum internalFormat, GLsizei width,
                                GLsizei height, GLsizei depth, GLint border,
                                GLenum unpackFormat, GLenum unpackType,
                                MaybeWebGLTexUnpackVariant&& src,
                                FuncScopeId aFuncId) {
  const WebGLContext::FuncScope scope(*mContext, GetFuncScopeName(aFuncId));
  mContext->TexImage(
      funcDims, target, level, internalFormat, width, height, depth, border,
      unpackFormat, unpackType,
      AsTexUnpackType<webgl::TexUnpackBlob>(mContext, std::move(src)));
}

void HostWebGLContext::TexSubImage(uint8_t funcDims, GLenum target, GLint level,
                                   GLint xOffset, GLint yOffset, GLint zOffset,
                                   GLsizei width, GLsizei height, GLsizei depth,
                                   GLenum unpackFormat, GLenum unpackType,
                                   MaybeWebGLTexUnpackVariant&& src,
                                   FuncScopeId aFuncId) {
  const WebGLContext::FuncScope scope(*mContext, GetFuncScopeName(aFuncId));
  mContext->TexSubImage(
      funcDims, target, level, xOffset, yOffset, zOffset, width, height, depth,
      unpackFormat, unpackType,
      AsTexUnpackType<webgl::TexUnpackBlob>(mContext, std::move(src)));
}

void HostWebGLContext::CompressedTexImage(
    uint8_t funcDims, GLenum target, GLint level, GLenum internalFormat,
    GLsizei width, GLsizei height, GLsizei depth, GLint border,
    MaybeWebGLTexUnpackVariant&& src, const Maybe<GLsizei>& expectedImageSize,
    FuncScopeId aFuncId) {
  const WebGLContext::FuncScope scope(*mContext, GetFuncScopeName(aFuncId));
  mContext->CompressedTexImage(
      funcDims, target, level, internalFormat, width, height, depth, border,
      AsTexUnpackType<webgl::TexUnpackBytes>(mContext, std::move(src)),
      expectedImageSize);
}

void HostWebGLContext::CompressedTexSubImage(
    uint8_t funcDims, GLenum target, GLint level, GLint xOffset, GLint yOffset,
    GLint zOffset, GLsizei width, GLsizei height, GLsizei depth,
    GLenum unpackFormat, MaybeWebGLTexUnpackVariant&& src,
    const Maybe<GLsizei>& expectedImageSize, FuncScopeId aFuncId) {
  const WebGLContext::FuncScope scope(*mContext, GetFuncScopeName(aFuncId));
  mContext->CompressedTexSubImage(
      funcDims, target, level, xOffset, yOffset, zOffset, width, height, depth,
      unpackFormat,
      AsTexUnpackType<webgl::TexUnpackBytes>(mContext, std::move(src)),
      expectedImageSize);
}

void HostWebGLContext::CopyTexSubImage(uint8_t funcDims, GLenum target,
                                       GLint level, GLint xOffset,
                                       GLint yOffset, GLint zOffset, GLint x,
                                       GLint y, uint32_t width, uint32_t height,
                                       uint32_t depth, FuncScopeId aFuncId) {
  const WebGLContext::FuncScope scope(*mContext, GetFuncScopeName(aFuncId));
  mContext->CopyTexSubImage(funcDims, target, level, xOffset, yOffset, zOffset,
                            x, y, width, height, depth);
}

// ------------------------------ WebGL Debug
// ------------------------------------

void HostWebGLContext::GenerateError(const GLenum error,
                                     const std::string& text) const {
  mContext->GenerateError(error, text.c_str());
}

void HostWebGLContext::JsWarning(const std::string& text) const {
  if (mOwnerData.inProcess) {
    (*mOwnerData.inProcess)->JsWarning(text);
    return;
  }
  (void)mOwnerData.outOfProcess->mParent.SendJsWarning(text);
}

}  // namespace mozilla

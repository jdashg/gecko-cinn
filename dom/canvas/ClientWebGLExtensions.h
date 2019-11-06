/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef CLIENTWEBGLEXTENSIONS_H_
#define CLIENTWEBGLEXTENSIONS_H_

#include "WebGLExtensions.h"
#include "ClientWebGLContext.h"

namespace mozilla {

/**
 * The ClientWebGLExtension... classes back the JS Extension classes.  They
 * direct their calls to the ClientWebGLContext, adding a boolean first
 * parameter, set to true, to indicate that an extension was the origin of
 * the call.
 */
class ClientWebGLExtensionBase : public nsWrapperCache {
  friend ClientWebGLContext;

 protected:
  WeakPtr<ClientWebGLContext> mContext;

 public:
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(ClientWebGLExtensionBase)
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_NATIVE_CLASS(ClientWebGLExtensionBase)

  ClientWebGLExtensionBase(const RefPtr<ClientWebGLContext>& aClient)
      : mContext(aClient.get()) {}

  ClientWebGLContext* GetParentObject() const { return mContext; }

 protected:
  virtual ~ClientWebGLExtensionBase() = default;
};

// To be used for implementations of ClientWebGLExtensionBase
#define DECLARE_WEBGL_EXTENSION_GOOP(_Extension)                           \
 public:                                                                   \
  virtual JSObject* WrapObject(JSContext* cx,                              \
                               JS::Handle<JSObject*> givenProto) override; \
  Client##_Extension(const RefPtr<ClientWebGLContext>&);

// To be used for implementations of ClientWebGLExtensionBase
#define DEFINE_WEBGL_EXTENSION_GOOP(_WebGLBindingType, _Extension)             \
  JSObject* Client##_Extension::WrapObject(JSContext* cx,                      \
                                           JS::Handle<JSObject*> givenProto) { \
    return dom::_WebGLBindingType##_Binding::Wrap(cx, this, givenProto);       \
  }                                                                            \
  Client##_Extension::Client##_Extension(                                      \
      const RefPtr<ClientWebGLContext>& aClient)                               \
      : ClientWebGLExtensionBase(aClient) {}

// Many extensions have no methods.  This is a shorthand for declaring client
// versions of such classes.
#define DECLARE_SIMPLE_WEBGL_EXTENSION(_Extension)                           \
  class Client##_Extension : public ClientWebGLExtensionBase {               \
   public:                                                                   \
    virtual JSObject* WrapObject(JSContext* cx,                              \
                                 JS::Handle<JSObject*> givenProto) override; \
    Client##_Extension(const RefPtr<ClientWebGLContext>&);                   \
  };

////

class ClientWebGLExtensionCompressedTextureASTC
    : public ClientWebGLExtensionBase {
  DECLARE_WEBGL_EXTENSION_GOOP(WebGLExtensionCompressedTextureASTC)

  void GetSupportedProfiles(dom::Nullable<nsTArray<nsString> >& retval) const {
    if (!mContext) return;
    mContext->GetSupportedProfilesASTC(retval);
  }
};

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionFloatBlend)

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionCompressedTextureBPTC)

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionCompressedTextureES3)

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionCompressedTextureETC1)

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionCompressedTexturePVRTC)

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionCompressedTextureRGTC)

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionFBORenderMipmap)

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionCompressedTextureS3TC)

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionCompressedTextureS3TC_SRGB)

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionDebugRendererInfo)

class ClientWebGLExtensionDebugShaders : public ClientWebGLExtensionBase {
  DECLARE_WEBGL_EXTENSION_GOOP(WebGLExtensionDebugShaders)

  void GetTranslatedShaderSource(const WebGLShaderJS& shader,
                                 nsAString& retval) const {
    if (!mContext) return;
    mContext->GetTranslatedShaderSource(shader, retval);
  }
};

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionDepthTexture)

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionElementIndexUint)

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionEXTColorBufferFloat)

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionFragDepth)

class ClientWebGLExtensionLoseContext : public ClientWebGLExtensionBase {
  DECLARE_WEBGL_EXTENSION_GOOP(WebGLExtensionLoseContext)

  void LoseContext() {
    if (!mContext) return;
    mContext->LoseContext(webgl::ContextLossReason::Manual);
  }
  void RestoreContext() {
    if (!mContext) return;
    mContext->RestoreContext();
  }
};

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionSRGB)

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionStandardDerivatives)

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionShaderTextureLod)

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionTextureFilterAnisotropic)

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionTextureFloat)

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionTextureFloatLinear)

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionTextureHalfFloat)

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionTextureHalfFloatLinear)

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionColorBufferFloat)

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionColorBufferHalfFloat)

class ClientWebGLExtensionDrawBuffers : public ClientWebGLExtensionBase {
  DECLARE_WEBGL_EXTENSION_GOOP(WebGLExtensionDrawBuffers)

  void DrawBuffersWEBGL(const dom::Sequence<GLenum>& buffers) {
    if (!mContext) return;
    mContext->DrawBuffers(buffers);
  }
};

class ClientWebGLExtensionVertexArray : public ClientWebGLExtensionBase {
  DECLARE_WEBGL_EXTENSION_GOOP(WebGLExtensionVertexArray)

  RefPtr<WebGLVertexArrayJS> CreateVertexArrayOES() {
    if (!mContext) return nullptr;
    return mContext->CreateVertexArray();
  }
  void DeleteVertexArrayOES(WebGLVertexArrayJS* array) {
    if (!mContext) return;
    mContext->DeleteVertexArray(array);
  }
  bool IsVertexArrayOES(const WebGLVertexArrayJS* array) {
    if (!mContext) return false;
    return mContext->IsVertexArray(array);
  }
  void BindVertexArrayOES(WebGLVertexArrayJS* array) {
    if (!mContext) return;
    mContext->BindVertexArray(array);
  }
};

class ClientWebGLExtensionInstancedArrays : public ClientWebGLExtensionBase {
  DECLARE_WEBGL_EXTENSION_GOOP(WebGLExtensionInstancedArrays)

  void DrawArraysInstancedANGLE(GLenum mode, GLint first, GLsizei count,
                                GLsizei primcount) {
    if (!mContext) return;
    mContext->DrawArraysInstanced(mode, first, count, primcount);
  }
  void DrawElementsInstancedANGLE(GLenum mode, GLsizei count, GLenum type,
                                  WebGLintptr offset, GLsizei primcount) {
    if (!mContext) return;
    mContext->DrawElementsInstanced(mode, count, type, offset, primcount,
                                    FuncScopeId::drawElementsInstanced);
  }
  void VertexAttribDivisorANGLE(GLuint index, GLuint divisor) {
    if (!mContext) return;
    mContext->VertexAttribDivisor(index, divisor);
  }
};

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionBlendMinMax)

class ClientWebGLExtensionDisjointTimerQuery : public ClientWebGLExtensionBase {
  DECLARE_WEBGL_EXTENSION_GOOP(WebGLExtensionDisjointTimerQuery)

  RefPtr<WebGLQueryJS> CreateQueryEXT() const {
    if (!mContext) return nullptr;
    return mContext->CreateQuery();
  }
  void DeleteQueryEXT(WebGLQueryJS* query) const {
    if (!mContext) return;
    mContext->DeleteQuery(query);
  }
  bool IsQueryEXT(const WebGLQueryJS* query) const {
    if (!mContext) return false;
    return mContext->IsQuery(query);
  }
  void BeginQueryEXT(GLenum target, WebGLQueryJS& query) const {
    if (!mContext) return;
    mContext->BeginQuery(target, query);
  }
  void EndQueryEXT(GLenum target) const {
    if (!mContext) return;
    mContext->EndQuery(target);
  }
  void QueryCounterEXT(WebGLQueryJS& query, GLenum target) const {
    if (!mContext) return;
    mContext->QueryCounter(query, target);
  }
  void GetQueryEXT(JSContext* cx, GLenum target, GLenum pname,
                   JS::MutableHandleValue retval) const {
    if (!mContext) return;
    mContext->GetQuery(cx, target, pname, retval);
  }
  void GetQueryObjectEXT(JSContext* cx, const WebGLQueryJS& query,
                         GLenum pname, JS::MutableHandleValue retval) const {
    if (!mContext) return;
    mContext->GetQueryParameter(cx, query, pname, retval);
  }
};

class ClientWebGLExtensionExplicitPresent : public ClientWebGLExtensionBase {
  DECLARE_WEBGL_EXTENSION_GOOP(WebGLExtensionExplicitPresent)

  void Present() const {
    if (!mContext) return;
    mContext->Present();
  }
};

class ClientWebGLExtensionMOZDebug : public ClientWebGLExtensionBase {
  DECLARE_WEBGL_EXTENSION_GOOP(WebGLExtensionMOZDebug)

  void GetParameter(JSContext* cx, GLenum pname,
                    JS::MutableHandle<JS::Value> retval,
                    ErrorResult& er) const {
    if (!mContext) return;
    mContext->MOZDebugGetParameter(cx, pname, retval, er);
  }
};

class ClientWebGLExtensionMultiview : public ClientWebGLExtensionBase {
  DECLARE_WEBGL_EXTENSION_GOOP(WebGLExtensionMultiview)

  void FramebufferTextureMultiviewOVR(const GLenum target,
                                      const GLenum attachment,
                                      const WebGLTextureJS* const texture,
                                      const GLint level,
                                      const GLint baseViewIndex,
                                      const GLsizei numViews) const {
    if (!mContext) return;
    mContext->FramebufferTextureMultiview(target, attachment, texture, level,
                                          baseViewIndex, numViews);
  }
};

}  // namespace mozilla

#endif  // CLIENTWEBGLEXTENSIONS_H_

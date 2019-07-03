/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef CLIENTWEBGLEXTENSIONS_H_
#define CLIENTWEBGLEXTENSIONS_H_

#include "WebGLExtensions.h"
#include "ClientWebGLContext.h"

namespace mozilla {

class ClientWebGLExtensionBase : public nsWrapperCache {
  friend ClientWebGLContext;

protected:
  WeakPtr<ClientWebGLContext> mContext;

 public:
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(ClientWebGLExtensionBase)
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_NATIVE_CLASS(ClientWebGLExtensionBase)

 protected:
  explicit ClientWebGLExtensionBase(ClientWebGLContext& context)
      : mContext(&context) {}
  virtual ~ClientWebGLExtensionBase() = default;

 public:
  auto GetParentObject() const { return mContext.get(); }
};

// -

// To be used for implementations of ClientWebGLExtensionBase
#define DEFINE_WEBGL_EXTENSION_GOOP(_WebGLBindingType, _Extension)             \
  JSObject* Client##_Extension::WrapObject(JSContext* cx,                      \
                                           JS::Handle<JSObject*> givenProto) { \
    return dom::_WebGLBindingType##_Binding::Wrap(cx, this, givenProto);       \
  }                                                                            \
  Client##_Extension::Client##_Extension(                                      \
      ClientWebGLContext& aClient)                               \
      : ClientWebGLExtensionBase(aClient) {}

// Many extensions have no methods.  This is a shorthand for declaring client
// versions of such classes.
#define DECLARE_SIMPLE_WEBGL_EXTENSION(_Extension)                           \
  class Client##_Extension : public ClientWebGLExtensionBase {               \
   public:                                                                   \
    virtual JSObject* WrapObject(JSContext* cx,                              \
                                 JS::Handle<JSObject*> givenProto) override; \
    Client##_Extension(ClientWebGLContext&);                   \
  };

////

class ClientWebGLExtensionCompressedTextureASTC
    : public ClientWebGLExtensionBase {
public:
  virtual JSObject* WrapObject(JSContext* cx,
                               JS::Handle<JSObject*> givenProto) override;
  ClientWebGLExtensionCompressedTextureASTC(ClientWebGLContext&);

  void GetSupportedProfiles(dom::Nullable<nsTArray<nsString>>& retval) const {
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
public:
  virtual JSObject* WrapObject(JSContext* cx,
                               JS::Handle<JSObject*> givenProto) override;
  ClientWebGLExtensionDebugShaders(ClientWebGLContext&);

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
public:
  virtual JSObject* WrapObject(JSContext* cx,
                               JS::Handle<JSObject*> givenProto) override;
  ClientWebGLExtensionLoseContext(ClientWebGLContext&);

  void LoseContext() {
    if (!mContext) return;
    mContext->EmulateLoseContext();
  }
  void RestoreContext() {
    if (!mContext) return;
    mContext->RestoreContext(webgl::LossStatus::LostManually);
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
public:
  virtual JSObject* WrapObject(JSContext* cx,
                               JS::Handle<JSObject*> givenProto) override;
  ClientWebGLExtensionDrawBuffers(ClientWebGLContext&);

  void DrawBuffersWEBGL(const dom::Sequence<GLenum>& buffers) {
    if (!mContext) return;
    mContext->DrawBuffers(buffers);
  }
};

class ClientWebGLExtensionVertexArray : public ClientWebGLExtensionBase {
public:
  virtual JSObject* WrapObject(JSContext* cx,
                               JS::Handle<JSObject*> givenProto) override;
  ClientWebGLExtensionVertexArray(ClientWebGLContext&);

  already_AddRefed<WebGLVertexArrayJS> CreateVertexArrayOES() {
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
public:
  virtual JSObject* WrapObject(JSContext* cx,
                               JS::Handle<JSObject*> givenProto) override;
  ClientWebGLExtensionInstancedArrays(ClientWebGLContext&);

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
public:
  virtual JSObject* WrapObject(JSContext* cx,
                               JS::Handle<JSObject*> givenProto) override;
  ClientWebGLExtensionDisjointTimerQuery(ClientWebGLContext&);

  already_AddRefed<WebGLQueryJS> CreateQueryEXT() const {
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
                   JS::MutableHandle<JS::Value> retval) const {
    if (!mContext) return;
    mContext->GetQuery(cx, target, pname, retval);
  }
  void GetQueryObjectEXT(JSContext* cx, WebGLQueryJS& query,
                         GLenum pname, JS::MutableHandle<JS::Value> retval) const {
    if (!mContext) return;
    mContext->GetQueryParameter(cx, query, pname, retval);
  }
};

class ClientWebGLExtensionExplicitPresent : public ClientWebGLExtensionBase {
public:
  virtual JSObject* WrapObject(JSContext* cx,
                               JS::Handle<JSObject*> givenProto) override;
  ClientWebGLExtensionExplicitPresent(ClientWebGLContext&);

  void Present() const {
    if (!mContext) return;
    mContext->Present();
  }
};

class ClientWebGLExtensionMOZDebug : public ClientWebGLExtensionBase {
public:
  virtual JSObject* WrapObject(JSContext* cx,
                               JS::Handle<JSObject*> givenProto) override;
  ClientWebGLExtensionMOZDebug(ClientWebGLContext&);

  void GetParameter(JSContext* cx, GLenum pname,
                    JS::MutableHandle<JS::Value> retval,
                    ErrorResult& er) const {
    if (!mContext) return;
    mContext->MOZDebugGetParameter(cx, pname, retval, er);
  }
};

class ClientWebGLExtensionMultiview : public ClientWebGLExtensionBase {
public:
  virtual JSObject* WrapObject(JSContext* cx,
                               JS::Handle<JSObject*> givenProto) override;
  ClientWebGLExtensionMultiview(ClientWebGLContext&);

  void FramebufferTextureMultiviewOVR(const GLenum target,
                                      const GLenum attachment,
                                      WebGLTextureJS* const texture,
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

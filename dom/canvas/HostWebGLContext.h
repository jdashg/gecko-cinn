/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef HOSTWEBGLCONTEXT_H_
#define HOSTWEBGLCONTEXT_H_

#include "mozilla/dom/BindingUtils.h"
#include "mozilla/HashTable.h"
#include "mozilla/GfxMessageUtils.h"
#include "mozilla/HashTable.h"
#include "nsString.h"
#include "WebGLContext.h"
#include "WebGL2Context.h"
#include "mozilla/dom/WebGLTypes.h"
#include "WebGLActiveInfo.h"

#ifndef WEBGL_BRIDGE_LOG_
#  define WEBGL_BRIDGE_LOG_(lvl, ...) \
    MOZ_LOG(mozilla::gWebGLBridgeLog, lvl, (__VA_ARGS__))
#  define WEBGL_BRIDGE_LOGD(...) WEBGL_BRIDGE_LOG_(LogLevel::Debug, __VA_ARGS__)
#  define WEBGL_BRIDGE_LOGE(...) WEBGL_BRIDGE_LOG_(LogLevel::Error, __VA_ARGS__)
#endif  // WEBGL_BRIDGE_LOG_

namespace mozilla {

class HostWebGLCommandSink;

extern LazyLogModule gWebGLBridgeLog;

namespace dom {
class WebGLParent;
}
namespace layers {
class CompositableHost;
}

template<typename C, typename K, typename V>
inline auto Find(const C<K,V>& container, const K& key, const V notFound = V()) -> V {
  const auto itr = container.find(key);
  if (itr == container.end()) return notFound;
  return itr->second;
}

/**
 * Host endpoint of a WebGLContext.  HostWebGLContext owns a WebGLContext
 * that it uses to execute commands sent from its ClientWebGLContext.
 *
 * A HostWebGLContext continuously issues a Task to the Compositor thread that
 * causes it to drain its queue of commands.  It also maintains a map of WebGL
 * objects (e.g. ObjectIdMap<WebGLShader>) that it uses associate them with
 * their cross-process IDs.
 *
 * This class is not an implementation of the
 * nsICanvasRenderingContextInternal DOM class.  That is the
 * ClientWebGLContext.
 */
class HostWebGLContext final : public SupportsWeakPtr<HostWebGLContext> {
  friend class WebGLContext;

  using ObjectId = webgl::ObjectId;

 public:
  MOZ_DECLARE_WEAKREFERENCE_TYPENAME(HostWebGLContext)

  struct RemotingData final {
    dom::WebGLParent& mParent;
    UniquePtr<HostWebGLCommandSink> mCommandSink;
  };
  struct OwnerData final {
    Maybe<ClientWebGLContext*> inProcess;
    Maybe<RemotingData> outOfProcess;
  };

  static UniquePtr<HostWebGLContext> Create(OwnerData&&,
                                            const webgl::InitContextDesc&,
                                            webgl::InitContextResult* out);

 private:
  explicit HostWebGLContext(OwnerData&&);

 public:
  virtual ~HostWebGLContext();

  WebGLContext* GetWebGLContext() const { return mContext; }

 public:
  const OwnerData mOwnerData;

 private:
  RefPtr<WebGLContext> mContext;

  #define _(X) std::unordered_map<ObjectId, RefPtr<WebGL##X>> m##X##Map;

  _(Buffer)
  _(Framebuffer)
  _(Program)
  _(Query)
  _(Renderbuffer)
  _(Sampler)
  _(Shader)
  _(Sync)
  _(Texture)
  _(TransformFeedback)
  _(VertexArray)

  #undef _

  class AutoResolveT final {
    friend class HostWebGLContext;

    const HostWebGLContext& mParent;
    const ObjectId mId;

    explicit AutoResolveT(const HostWebGLContext& parent, const ObjectId id) : mParent(parent), mId(id) {}

    MOZ_IMPLICIT operator WebGLBuffer*() const {
      return Find(mParent.mBufferMap, mId).get();
    }
    MOZ_IMPLICIT operator WebGLFramebuffer*() const {
      return Find(mParent.mFramebufferMap, mId).get();
    }
    MOZ_IMPLICIT operator WebGLProgram*() const {
      return Find(mParent.mProgramMap, mId).get();
    }
    MOZ_IMPLICIT operator WebGLQuery*() const {
      return Find(mParent.mQueryMap, mId).get();
    }
    MOZ_IMPLICIT operator WebGLRenderbuffer*() const {
      return Find(mParent.mRenderbufferMap, mId).get();
    }
    MOZ_IMPLICIT operator WebGLSampler*() const {
      return Find(mParent.mSamplerMap, mId).get();
    }
    MOZ_IMPLICIT operator WebGLShader*() const {
      return Find(mParent.mShaderMap, mId).get();
    }
    MOZ_IMPLICIT operator WebGLSync*() const {
      return Find(mParent.mSyncMap, mId).get();
    }
    MOZ_IMPLICIT operator WebGLTexture*() const {
      return Find(mParent.mTextureMap, mId).get();
    }
    MOZ_IMPLICIT operator WebGLTransformFeedback*() const {
      return Find(mParent.mTransformFeedbackMap, mId).get();
    }
    MOZ_IMPLICIT operator WebGLVertexArray*() const {
      return Find(mParent.mVertexArrayMap, mId).get();
    }
  };

  AutoResolveT ById(const ObjectId id) const {
    return {*this, id};
  }

  // -------------------------------------------------------------------------
  // RPC Framework
  // -------------------------------------------------------------------------

 public:
  CommandResult RunCommandsForDuration(TimeDuration aDuration);

  // -------------------------------------------------------------------------
  // Host-side methods.  Calls in the client are forwarded to the host.
  // -------------------------------------------------------------------------

 public:
  // ------------------------- Composition -------------------------
  void Present();

  Maybe<ICRData> InitializeCanvasRenderer(layers::LayersBackend backend);

  void Resize(const uvec2& size);

  uvec2 DrawingBufferSize();

  void SetCompositableHost(RefPtr<layers::CompositableHost>& aCompositableHost);

  void OnMemoryPressure();
  void OnContextLoss(webgl::ContextLossReason);

  void DidRefresh();

  void RequestExtension(const WebGLExtensionID ext) {
    mContext->RequestExtension(ext);
  }

  // -
  // Creation and destruction

  void CreateBuffer(ObjectId);
  void CreateFramebuffer(ObjectId);
  void CreateProgram(ObjectId);
  void CreateQuery(ObjectId);
  void CreateRenderbuffer(ObjectId);
  void CreateSampler(ObjectId);
  void CreateShader(GLenum aType, ObjectId);
  void FenceSync(ObjectId, GLenum condition, GLbitfield flags);
  void CreateTexture(ObjectId);
  void CreateTransformFeedback(ObjectId);
  void CreateVertexArray(ObjectId);

  void DeleteBuffer(ObjectId);
  void DeleteFramebuffer(ObjectId);
  void DeleteProgram(ObjectId);
  void DeleteQuery(ObjectId);
  void DeleteRenderbuffer(ObjectId);
  void DeleteSampler(ObjectId);
  void DeleteShader(ObjectId);
  void DeleteSync(ObjectId);
  void DeleteTexture(ObjectId);
  void DeleteTransformFeedback(ObjectId);
  void DeleteVertexArray(ObjectId);

  // ------------------------- GL State -------------------------
  bool IsContextLost() const;

  void Disable(GLenum cap) const {
    mContext->Disable(cap);
  }

  void Enable(GLenum cap) const {
    mContext->Enable(cap);
  }

  bool IsEnabled(GLenum cap) const {
    return mContext->IsEnabled(cap);
  }

  Maybe<double> GetParameter(GLenum pname, bool debug) const {
    return mContext->GetParameter(pname, debug);
  }

  Maybe<std::string> GetString(GLenum pname, bool debug) const {
    return mContext->GetString(pname, debug);
  }

  void AttachShader(ObjectId prog, ObjectId shader) const {
    mContext->AttachShader(ById(prog), ById(shader));
  }

  void BindAttribLocation(ObjectId id, GLuint location,
                          const std::string& name) const {
    mContext->BindAttribLocation(ById(id), location, name);
  }

  void BindFramebuffer(GLenum target, ObjectId id) const {
    mContext->BindFramebuffer(target, ById(id));
  }

  void BlendColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) const {
    mContext->BlendColor(r, g, b, a);
  }

  void BlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha) const {
    mContext->BlendEquationSeparate(modeRGB, modeAlpha);
  }

  void BlendFuncSeparate(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha,
                         GLenum dstAlpha) const {
    mContext->BlendFuncSeparate(srcRGB, dstRGB, srcAlpha, dstAlpha);
  }

  GLenum CheckFramebufferStatus(GLenum target) const {
    return mContext->CheckFramebufferStatus(target);
  }

  void Clear(GLbitfield mask) const {
    mContext->Clear(mask);
  }

  void ClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) const {
    mContext->ClearColor(r, g, b, a);
  }

  void ClearDepth(GLclampf v) const {
    mContext->ClearDepth(v);
  }

  void ClearStencil(GLint v) const {
    mContext->ClearStencil(v);
  }

  void ColorMask(WebGLboolean r, WebGLboolean g, WebGLboolean b,
                 WebGLboolean a) const {
    mContext->ColorMask(r, g, b, a);
  }

  void CompileShader(const ObjectId id) const {
    mContext->CompileShader(ById(id));
  }

  void CullFace(GLenum face) const {
    mContext->CullFace(face);
  }

  void DepthFunc(GLenum func) const {
    mContext->DepthFunc(func);
  }

  void DepthMask(WebGLboolean b) const {
    mContext->DepthMask(b);
  }

  void DepthRange(GLclampf zNear, GLclampf zFar) const {
    mContext->DepthRange(zNear, zFar);
  }

  void DetachShader(const ObjectId prog,
                    const ObjectId shader) const {
    mContext->DetachShader(ById(prog), ById(shader));
  }

  void Flush() const {
    mContext->Flush();
  }

  void Finish() const {
    mContext->Finish();
  }

  void FramebufferAttach(const GLenum target, const GLenum attachEnum,
                         const GLenum texTarget, const ObjectId id,
                         const GLint mipLevel, const GLint zLayerBase,
                         const GLsizei numViewLayers) const {
    mContext->FramebufferAttach(target, attachEnum, texTarget, ById(id), ById(id),
                                 mipLevel, zLayerBase, numViewLayers);
  }

  void FrontFace(GLenum mode) const {
    mContext->FrontFace(mode);
  }

  Maybe<double> GetBufferParameter(GLenum target, GLenum pname) const {
    return mContext->GetBufferParameter(target, pname);
  }

  GLenum GetError() const {
    return mContext->GetError();
  }

  GLint GetFragDataLocation(ObjectId id, const std::string& name) const {
    return mContext->GetFragDataLocation(ById(id), name);
  }

  Maybe<double> GetFramebufferAttachmentParameter(ObjectId id,
                                                      GLenum attachment,
                                                      GLenum pname) const {
    return mContext->GetFramebufferAttachmentParameter(ById(id), attachment,
      pname);
  }

  webgl::LinkResult GetLinkResult(ObjectId id) const {
    return mContext->GetLinkResult(ById(id));
  }

  Maybe<double> GetRenderbufferParameter(ObjectId id, GLenum pname) const {
    return mContext->GetRenderbufferParameter(ById(id), pname);
  }

  Maybe<webgl::ShaderPrecisionFormat> GetShaderPrecisionFormat(GLenum shaderType,
                                             GLenum precisionType) const {
    return mContext->GetShaderPrecisionFormat(shadertype, prescisionType);
  }

  webgl::GetUniformData GetUniform(ObjectId prog, uint32_t loc) const {
    return mContext->GetUniform(ById(prog), loc);
  }

  void Hint(GLenum target, GLenum mode) const {
    mContext->Hint(target, mode);
  }

  void LineWidth(GLfloat width) const {
    mContext->LineWidth(width);
  }

  void LinkProgram(const ObjectId id) const {
    mContext->LinkProgram(ById(id));
  }

  void PixelStorei(GLenum pname, GLint param) const {
    mContext->PixelStorei(pname, param);
  }

  void PolygonOffset(GLfloat factor, GLfloat units) const {
    mContext->PolygonOffset(factor, units);
  }

  void SampleCoverage(GLclampf value, bool invert) const {
    mContext->SampleCoverage(value, invert);
  }

  void Scissor(GLint x, GLint y, GLsizei width, GLsizei height) const {
    mContext->Scissor(x, y, width, height);
  }

  void ShaderSource(const ObjectId id,
                    const std::string& source) const {
    mContext->ShaderSource(ById(id), source);
  }

  void StencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask) const {
    mContext->StencilFuncSeparate(face, func, ref, mask);
  }
  void StencilMaskSeparate(GLenum face, GLuint mask) const {
    mContext->StencilMaskSeparate(face, mask);
  }
  void StencilOpSeparate(GLenum face, GLenum sfail, GLenum dpfail,
                         GLenum dppass) const {
    mContext->StencilOpSeparate(face, sfail, dpfail, dppass);
  }

  void Viewport(GLint x, GLint y, GLsizei width, GLsizei height) const {
    mContext->Viewport(x, y, width, height);
  }

  // ------------------------- Buffer Objects -------------------------
  void BindBuffer(GLenum target, const ObjectId id) const {
    mContext->BindBuffer(target, ById(id));
  }

  void BindBufferRange(GLenum target, GLuint index,
                       const ObjectId id, uint64_t offset,
                       uint64_t size) const {
    mContext->BindBufferRange(target, index, ById(id), offset, size);
  }

  void CopyBufferSubData(GLenum readTarget, GLenum writeTarget,
                         GLintptr readOffset, GLintptr writeOffset,
                         GLsizeiptr size) const {
    mContext->CopyBufferSubData(readTarget, writeTarget, readOffset, writeOffset, size);
  }

  Maybe<UniquePtr<RawBuffer<>>> GetBufferSubData(GLenum target,
                                                 GLintptr srcByteOffset,
                                                 size_t byteLen);

  void BufferData(GLenum target, const RawBuffer<>& data, GLenum usage);

  void BufferSubData(GLenum target, WebGLsizeiptr dstByteOffset,
                     const RawBuffer<>& srcData);

  // -------------------------- Framebuffer Objects --------------------------
  void BlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                       GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                       GLbitfield mask, GLenum filter) const {
    mContext->BlitFramebuffer(srcX0, srcY0, srcX1, srcY1,
                       dstX0, dstY0, dstX1, dstY1,
                       mask, filter);
  }

  void InvalidateFramebuffer(GLenum target,
                             const nsTArray<GLenum>& attachments) const {
    mContext->InvalidateFramebuffer(target, attachments);
  }

  void InvalidateSubFramebuffer(GLenum target,
                                const nsTArray<GLenum>& attachments, GLint x,
                                GLint y, GLsizei width, GLsizei height) const {
    mContext->InvalidateSubFramebuffer(target, attachments, x, y, width, height);
  }

  void ReadBuffer(GLenum mode) const {
    mContext->ReadBuffer(mode);
  }

  // ----------------------- Renderbuffer objects -----------------------
  Maybe<nsTArray<int32_t>> GetInternalformatParameter(GLenum target,
                                                      GLenum internalformat,
                                                      GLenum pname);

  void RenderbufferStorageMultisample(ObjectId id, GLsizei samples,
                                      GLenum internalFormat, GLsizei width,
                                      GLsizei height) const {
    mContext->RenderbufferStorageMultisample(ById(id), samples, internalFormat,
     width, height);
  }

  // --------------------------- Texture objects ---------------------------
  void ActiveTexture(GLenum texUnit) const {
    mContext->ActiveTexture(texUnit);
  }

  void BindTexture(GLenum texTarget, const ObjectId id) const {
    BindTexture(texTarget, ById(id));
  }

  void GenerateMipmap(GLenum texTarget) const {
    mContext->GenerateMipmap(texTarget);
  }

  void CopyTexImage2D(GLenum target, GLint level, GLenum internalFormat,
                      GLint x, GLint y, uint32_t width, uint32_t height) const {
    mContext->CopyTexImage2D(target, level, internalFormat, x, y, width, height);
  }

  void TexStorage(uint8_t funcDims, GLenum target, GLsizei levels,
                  GLenum internalFormat, GLsizei width, GLsizei height,
                  GLsizei depth) const {
    mContext->TexStorage(funcDims, target, levels, internalFormat, width, height, depth);
  }

  void TexImage(uint8_t funcDims, GLenum target, GLint level,
                GLenum internalFormat, GLsizei width, GLsizei height,
                GLsizei depth, GLint border, GLenum unpackFormat,
                GLenum unpackType, MaybeWebGLTexUnpackVariant&& src) const {
    mContext->TexImage(funcDims, target, level, internalFormat, width, height, depth, border,
    unpackFormat, unpackType, src);
  }

  void TexSubImage(uint8_t funcDims, GLenum target, GLint level, GLint xOffset,
                   GLint yOffset, GLint zOffset, GLsizei width, GLsizei height,
                   GLsizei depth, GLenum unpackFormat, GLenum unpackType,
                   MaybeWebGLTexUnpackVariant&& src) const {
    mContext->TexSubImage(funcDims, target, level, xOffset, yOffset, zOffset,
    width, height, depth, unpackFormat, unpackType, src);
  }

  void CompressedTexImage(uint8_t funcDims, GLenum target, GLint level,
                          GLenum internalFormat, GLsizei width, GLsizei height,
                          GLsizei depth, GLint border,
                          MaybeWebGLTexUnpackVariant&& src,
                          const Maybe<GLsizei>& expectedImageSize) const {
    mContext->CompressedTexImage(funcDims, target, level, internalFormat,
     width, height, depth, border,
     src, expectedImageSize);
  }

  void CompressedTexSubImage(uint8_t funcDims, GLenum target, GLint level,
                             GLint xOffset, GLint yOffset, GLint zOffset,
                             GLsizei width, GLsizei height, GLsizei depth,
                             GLenum unpackFormat,
                             MaybeWebGLTexUnpackVariant&& src,
                             const Maybe<GLsizei>& expectedImageSize) const {
    mContext->CompressedTexSubImage(funcDims, target, level,
      xOffset, yOffset, zOffset, width, hieght, depth, unpackFormat, src,
      expectedImageSize);
  }

  void CopyTexSubImage(uint8_t funcDims, GLenum target, GLint level,
                       GLint xOffset, GLint yOffset, GLint zOffset, GLint x,
                       GLint y, uint32_t width, uint32_t height) const {
    mContext->CopyTexSubImage(funcDims, target, level, xOffset, yOffset, zOffset,
    x, y, width, height);
  }

  Maybe<double> GetTexParameter(ObjectId id, GLenum pname) const {
    return GetTexParameter(ById(id), pname);
  }

  void TexParameter_base(GLenum texTarget, GLenum pname,
                         const FloatOrInt& param) const {
    mContext->TexParameter_base(texTarget, pname, param);
  }

  // ------------------- Programs and shaders --------------------------------
  void UseProgram(ObjectId id) const {
    mContext->UseProgram(ById(id));
  }

  void ValidateProgram(ObjectId id) const {
    mContext->ValdiateProgram(ById(id));
  }

  // ------------------------ Uniforms and attributes ------------------------

  void UniformNTv(ObjectId id, const uint8_t n,
              const webgl::UniformBaseType t, const RawBuffer<>& bytes) const {
    mContext->UniformNTv(ById(id), n, t, bytes);
  }

  void UniformMatrixAxBfv(uint8_t A, uint8_t B, const ObjectId id,
                          bool transpose, const RawBuffer<const float>& data) const {
    mContext->UniformMatrixAxBfv(A, B, ById(id), transpose, data);
  }

  void VertexAttrib4T(GLuint index, const webgl::GenericVertexAttribData& data) const {
    mContext->VertexAttrib4T(index, data);
  }

  void VertexAttribDivisor(GLuint index, GLuint divisor) const {
    mContext->VertexAttribDivisor(index, divisor);
  }

  uint64_t GetIndexedParameter(GLenum target, GLuint index) const {
    return mContext->GetIndexedParameter(target, index);
  }

  void UniformBlockBinding(const ObjectId id,
                           GLuint uniformBlockIndex,
                           GLuint uniformBlockBinding) const {
    mContext->UniformBlockBinding(ById(id), uniformBlockIndex, uniformBlockBinding);
  }

  void EnableVertexAttribArray(GLuint index) const {
    mContext->EnableVertexAttribArray(index);
  }

  void DisableVertexAttribArray(GLuint index) const {
    mContext->DisableVertexAttribArray(index);
  }

  Maybe<double> GetVertexAttrib(GLuint index, GLenum pname) const {
    return mContext->GetVertexAttrib(index, pname);
  }

  void VertexAttribAnyPointer(bool isFuncInt, GLuint index, GLint size,
                              GLenum type, bool normalized, GLsizei stride,
                              WebGLintptr byteOffset, FuncScopeId aFuncId);

  // --------------------------- Buffer Operations --------------------------
  void ClearBufferTv(GLenum buffer, GLint drawBuffer, webgl::AttribBaseType t,
                     const RawBuffer<>& data) const {
    mContext->ClearBufferTv(buffer, drawBuffer, t, data);
  }

  void ClearBufferfi(GLenum buffer, GLint drawBuffer, GLfloat depth,
                     GLint stencil) const {
    mContext->ClearBufferfi(buffer, drawBuffer, depth, stencil);
  }

  // ------------------------------ Readback -------------------------------
  void ReadPixelsPbo(GLint x, GLint y, GLsizei width, GLsizei height,
                     GLenum format, GLenum type, WebGLsizeiptr offset) const {
    mContext->ReadPixelsPbo(x, y, width, height, format, type, offset);
  }

  Maybe<UniquePtr<RawBuffer<>>> ReadPixels(GLint x, GLint y, GLsizei width,
                                            GLsizei height, GLenum format,
                                            GLenum type, size_t byteLen);

  // ----------------------------- Sampler -----------------------------------

  void BindSampler(GLuint unit, ObjectId id) const {
    mContext->BindSampler(unit, ById(id));
  }

  void SamplerParameteri(ObjectId id, GLenum pname,
                         GLint param) const {
    mContext->SamplerParameteri(ById(id), pname, param);
  }

  void SamplerParameterf(ObjectId id, GLenum pname,
                         GLfloat param) const {
    mContext->SamplerParameterf(ById(id), pname, param);
  }

  Maybe<double> GetSamplerParameter(ObjectId id,
                                        GLenum pname) const {
    return mContext->GetSamplerParameter(ById(id), pname);
  }

  // ------------------------------- GL Sync ---------------------------------

  GLenum ClientWaitSync(ObjectId id, GLbitfield flags,
                        GLuint64 timeout) const {
    return mContext->ClientWaitSync(ById(Id), flags, timeout);
  }

  void WaitSync(ObjectId id, GLbitfield flags,
                GLint64 timeout) const {
    mContext->WaitSync(ById(id), flags, timeout);
  }

  // -------------------------- Transform Feedback ---------------------------
  void BindTransformFeedback(ObjectId id) const {
    mContext->BindTransformFeedback(ById(id));
  }

  void BeginTransformFeedback(GLenum primitiveMode) const {
    mContext->BeginTransformFeedback(primitiveMode);
  }

  void EndTransformFeedback() const {
    mContext->EndTransformFeedback();
  }

  void PauseTransformFeedback() const {
    mContext->PauseTransformFeedback();
  }

  void ResumeTransformFeedback() const {
    mContext->ResumeTransformFeedback();
  }

  void TransformFeedbackVaryings(ObjectId id,
                                 const std::vector<std::string>& varyings,
                                 GLenum bufferMode) const {
    mContext->TransformFeedbackVaryings(ById(id), varyings, bufferMode);
  }

  // ------------------------------ WebGL Debug
  // ------------------------------------
  void GenerateError(GLenum error, const std::string&) const;
  void JsWarning(const std::string&) const;

  // -------------------------------------------------------------------------
  // Host-side extension methods.  Calls in the client are forwarded to the
  // host. Some extension methods are also available in WebGL2 Contexts.  For
  // them, the final parameter is a boolean indicating if the call originated
  // from an extension.
  // -------------------------------------------------------------------------

  // Misc. Extensions
  void DrawBuffers(const std::vector<GLenum>& buffers) const {
    mContext->DrawBuffers(buffers);
  }

  void LoseContext(webgl::ContextLossReason);

  // VertexArrayObjectEXT
  void BindVertexArray(ObjectId id) const {
    mContext->BindVertexArray(ById(id));
  }

  // InstancedElementsEXT
  void DrawArraysInstanced(GLenum mode, GLint first, GLsizei vertCount,
                           GLsizei primCount) const {
    mContext->DrawArraysInstanced(mode, first, vertCount, primCount);
  }

  void DrawElementsInstanced(GLenum mode, GLsizei vertCount, GLenum type,
                             WebGLintptr offset, GLsizei primCount) const {
    mContext->DrawElementsInstanced(mode, vertCount, type, offset, primCount);
  }

  // GLQueryEXT
  void BeginQuery(GLenum target, ObjectId id) const {
    mContext->BeginQuery(target, ById(id));
  }

  void EndQuery(GLenum target) const {
    mContext->EndQuery(target);
  }

  void QueryCounter(ObjectId id, GLenum target) const {
    mContext->QueryCounter(ById(Id), target);
  }

  Maybe<double> GetQueryParameter(ObjectId id,
                                      GLenum pname) const {
    return mContext->GetQueryParameter(ById(id), pname);
  }

  // -------------------------------------------------------------------------
  // Client-side methods.  Calls in the Host are forwarded to the client.
  // -------------------------------------------------------------------------
 public:
  void PostContextCreationError(const nsCString& aMsg);

  void OnLostContext();

  void OnRestoredContext();

  // Etc
 public:
  already_AddRefed<layers::SharedSurfaceTextureClient> GetVRFrame();

 protected:
  const WebGL2Context* GetWebGL2Context() const {
    MOZ_RELEASE_ASSERT(mContext->IsWebGL2(), "Requires WebGL2 context");
    return static_cast<WebGL2Context*>(mContext.get());
  }

  WebGL2Context* GetWebGL2Context() {
    const auto* constThis = this;
    return const_cast<WebGL2Context*>(constThis->GetWebGL2Context());
  }

  mozilla::ipc::Shmem PopShmem() { return mShmemStack.PopLastElement(); }

  nsTArray<mozilla::ipc::Shmem> mShmemStack;
  ClientWebGLContext* mClientContext;
};

}  // namespace mozilla

#endif  // HOSTWEBGLCONTEXT_H_

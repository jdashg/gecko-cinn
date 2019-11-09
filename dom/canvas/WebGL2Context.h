/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef WEBGL2CONTEXT_H_
#define WEBGL2CONTEXT_H_

#include "WebGLContext.h"

namespace mozilla {

class ErrorResult;
class HostWebGLContext;
class WebGLSampler;
class WebGLSync;
class WebGLTransformFeedback;
class WebGLVertexArrayObject;
namespace dom {
class OwningUnsignedLongOrUint32ArrayOrBoolean;
class OwningWebGLBufferOrLongLong;
}  // namespace dom

class WebGL2Context final : public WebGLContext {
  friend class WebGLContext;

 public:
  WebGL2Context(HostWebGLContext& host, const webgl::InitContextDesc& desc)
      : WebGLContext(host, desc) {}

  virtual bool IsWebGL2() const override { return true; }

  // -------------------------------------------------------------------------
  // Buffer objects - WebGL2ContextBuffers.cpp

  void CopyBufferSubData(GLenum readTarget, GLenum writeTarget,
                         WebGLintptr readOffset, WebGLintptr writeOffset,
                         WebGLsizeiptr size);

 private:
  template <typename BufferT>
  void GetBufferSubDataT(GLenum target, WebGLintptr offset,
                         const BufferT& data);

 public:
  UniqueBuffer GetBufferSubData(GLenum target,
                                                 WebGLintptr srcByteOffset,
                                                 size_t byteLen);

  // -------------------------------------------------------------------------
  // Framebuffer objects - WebGL2ContextFramebuffers.cpp

  void BlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                       GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                       GLbitfield mask, GLenum filter);

  void InvalidateFramebuffer(GLenum target,
                             const nsTArray<GLenum>& attachments);
  void InvalidateSubFramebuffer(GLenum target,
                                const nsTArray<GLenum>& attachments, GLint x,
                                GLint y, GLsizei width, GLsizei height);
  void ReadBuffer(GLenum mode);

  // -------------------------------------------------------------------------
  // Renderbuffer objects - WebGL2ContextRenderbuffers.cpp

  Maybe<nsTArray<int32_t>> GetInternalformatParameter(GLenum target,
                                                      GLenum internalformat,
                                                      GLenum pname);

  // -------------------------------------------------------------------------
  // Texture objects - WebGL2ContextTextures.cpp

  void TexStorage(uint8_t funcDims, GLenum target, GLsizei levels,
                  GLenum internalFormat, GLsizei width, GLsizei height,
                  GLsizei depth);

  GLint GetFragDataLocation(const WebGLProgram& prog, const nsAString& name);

  // GL 3.0 & ES 3.0
  void VertexAttribI4i(GLuint index, GLint x, GLint y, GLint z, GLint w);
  void VertexAttribI4ui(GLuint index, GLuint x, GLuint y, GLuint z, GLuint w);

  // -------------------------------------------------------------------------
  // Writing to the drawing buffer

  /* Implemented in WebGLContext
  void VertexAttribDivisor(GLuint index, GLuint divisor);
  void DrawArraysInstanced(GLenum mode, GLint first, GLsizei count,
                           GLsizei instanceCount);
  void DrawElementsInstanced(GLenum mode, GLsizei count, GLenum type,
                             WebGLintptr offset, GLsizei instanceCount);
  */

  // ------------------------------------------------------------------------
  // Multiple Render Targets - WebGL2ContextMRTs.cpp
  /* Implemented in WebGLContext
  void DrawBuffers(const nsTArray<GLenum>& buffers);
  */

 private:
  bool ValidateClearBuffer(GLenum buffer, GLint drawBuffer,
                           webgl::AttribBaseType funcType);

 public:
  void ClearBufferfi(GLenum buffer, GLint drawBuffer, GLfloat depth,
                     GLint stencil);
  void ClearBufferTv(GLenum buffer, GLint drawBuffer, const webgl::TypedQuad& data);

  // -------------------------------------------------------------------------
  // Sampler Objects - WebGL2ContextSamplers.cpp

  already_AddRefed<WebGLSampler> CreateSampler();
  void DeleteSampler(WebGLSampler* sampler);
  void BindSampler(GLuint unit, WebGLSampler* sampler);
  void SamplerParameteri(WebGLSampler& sampler, GLenum pname, GLint param);
  void SamplerParameterf(WebGLSampler& sampler, GLenum pname, GLfloat param);
  Maybe<double> GetSamplerParameter(const WebGLSampler& sampler,
                                        GLenum pname) const;

  // -------------------------------------------------------------------------
  // Sync objects - WebGL2ContextSync.cpp

  const GLuint64 kMaxClientWaitSyncTimeoutNS =
      1000 * 1000 * 1000;  // 1000ms in ns.

  already_AddRefed<WebGLSync> FenceSync(GLenum condition, GLbitfield flags);
  void DeleteSync(WebGLSync* sync);
  GLenum ClientWaitSync(const WebGLSync& sync, GLbitfield flags,
                        GLuint64 timeout);

  // -------------------------------------------------------------------------
  // Transform Feedback - WebGL2ContextTransformFeedback.cpp

  already_AddRefed<WebGLTransformFeedback> CreateTransformFeedback();
  void DeleteTransformFeedback(WebGLTransformFeedback* tf);
  void BindTransformFeedback(GLenum target, WebGLTransformFeedback* tf);
  void BeginTransformFeedback(GLenum primitiveMode);
  void EndTransformFeedback();
  void PauseTransformFeedback();
  void ResumeTransformFeedback();
  void TransformFeedbackVaryings(WebGLProgram& program,
                                 const nsTArray<nsString>& varyings,
                                 GLenum bufferMode);

  // -------------------------------------------------------------------------
  // Uniform Buffer Objects and Transform Feedback Buffers -
  // WebGL2ContextUniforms.cpp
  // TODO(djg): Implemented in WebGLContext
  /*
      void BindBufferBase(GLenum target, GLuint index, WebGLBuffer* buffer);
      void BindBufferRange(GLenum target, GLuint index, WebGLBuffer* buffer,
                           WebGLintptr offset, WebGLsizeiptr size);
  */
  Maybe<double> GetParameter(GLenum pname) override;

  // Make the inline version from the superclass visible here.
  using WebGLContext::GetParameter;

  void UniformBlockBinding(WebGLProgram& program, GLuint uniformBlockIndex,
                           GLuint uniformBlockBinding);

 private:
  virtual UniquePtr<webgl::FormatUsageAuthority> CreateFormatUsage(
      gl::GLContext* gl) const override;

  virtual bool IsTexParamValid(GLenum pname) const override;

  void UpdateBoundQuery(GLenum target, WebGLQuery* query);

  // CreateVertexArrayImpl is assumed to be infallible.
  virtual WebGLVertexArray* CreateVertexArrayImpl() override;
};

}  // namespace mozilla

#endif

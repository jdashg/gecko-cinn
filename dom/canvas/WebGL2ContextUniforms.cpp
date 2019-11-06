/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebGL2Context.h"

#include "GLContext.h"
#include "mozilla/dom/WebGL2RenderingContextBinding.h"
#include "mozilla/RefPtr.h"
#include "WebGLBuffer.h"
#include "WebGLContext.h"
#include "WebGLProgram.h"
#include "WebGLUniformLocation.h"
#include "WebGLVertexArray.h"
#include "WebGLVertexAttribData.h"

namespace mozilla {

// -------------------------------------------------------------------------
// Uniforms

void WebGLContext::Uniform1ui(WebGLUniformLocation* loc, GLuint v0) {
  const FuncScope funcScope(*this, "uniform1ui");
  if (!ValidateUniformSetter(loc, 1, webgl::AttribBaseType::UInt)) return;

  gl->fUniform1ui(loc->mLoc, v0);
}

void WebGLContext::Uniform2ui(WebGLUniformLocation* loc, GLuint v0, GLuint v1) {
  const FuncScope funcScope(*this, "uniform2ui");
  if (!ValidateUniformSetter(loc, 2, webgl::AttribBaseType::UInt)) return;

  gl->fUniform2ui(loc->mLoc, v0, v1);
}

void WebGLContext::Uniform3ui(WebGLUniformLocation* loc, GLuint v0, GLuint v1,
                              GLuint v2) {
  const FuncScope funcScope(*this, "uniform3ui");
  if (!ValidateUniformSetter(loc, 3, webgl::AttribBaseType::UInt)) return;

  gl->fUniform3ui(loc->mLoc, v0, v1, v2);
}

void WebGLContext::Uniform4ui(WebGLUniformLocation* loc, GLuint v0, GLuint v1,
                              GLuint v2, GLuint v3) {
  const FuncScope funcScope(*this, "uniform4ui");
  if (!ValidateUniformSetter(loc, 4, webgl::AttribBaseType::UInt)) return;

  gl->fUniform4ui(loc->mLoc, v0, v1, v2, v3);
}

// -------------------------------------------------------------------------
// Uniform Buffer Objects and Transform Feedback Buffers

void WebGL2Context::UniformBlockBinding(WebGLProgram& program,
                                        GLuint uniformBlockIndex,
                                        GLuint uniformBlockBinding) {
  const FuncScope funcScope(*this, "uniformBlockBinding");
  if (IsContextLost()) return;

  if (!ValidateObject("program", program)) return;

  program.UniformBlockBinding(uniformBlockIndex, uniformBlockBinding);
}

}  // namespace mozilla

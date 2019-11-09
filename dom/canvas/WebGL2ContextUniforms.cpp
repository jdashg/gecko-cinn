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
#include "WebGLVertexArray.h"
#include "WebGLVertexAttribData.h"

namespace mozilla {

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

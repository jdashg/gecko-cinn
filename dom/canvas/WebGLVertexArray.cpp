/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebGLVertexArray.h"

#include "GLContext.h"
#include "mozilla/dom/WebGLRenderingContextBinding.h"
#include "WebGLBuffer.h"
#include "WebGLContext.h"
#include "WebGLVertexArrayGL.h"
#include "WebGLVertexArrayFake.h"

namespace mozilla {

static GLuint CreateVao(ContextGL* const context) {
  const auto& gl = context->gl;
  if (!gl->IsSupported(gl::GLFeature::vertex_array_object))
    return 0;

  GLuint ret = 0;
  gl->fGenVertexArrays(1, &ret);
  return ret;
}

VertexArrayGL::VertexArrayGL(ContextGL* const context)
    : AVertexArray(context), mGLName(CreateVao(context)),
      mAttribs(context->mVertexAttribCount) {
}

VertexArrayGL::~VertexArrayGL() {
  if (mGLName) {
    const auto& gl = mContext->gl;
    if (gl) {
      gl->fDeleteVertexArrays(1, &mGLName);
    }
  }
}

void
VertexArrayGL::Bind(VertexArrayGL* const prev) const {
  const auto& gl = mContext->gl;
  if (mGLName) {
    gl->fBindVertexArray(mGLName);
    return;
  }

  if (!prev) {
    for (const auto& cur : mAttribs) {
      MOZ_ASSERT(!cur.mBuffer);
    }
    return;
  }

  const auto& count = mAttribs.size();
  for (size_t i = 0; i < count; ++i) {
    if (mAttribs[i].mBuffer || prev->mAttribs[i].mBuffer) {
      VertexAttribPointer(mAttribs[i]);
    }
  }
  gl->fBindBuffer(LOCAL_GL_ELEMENT_ARRAY_BUFFER, mIndexBuffer ? mIndexBuffer->mGLName : 0);
}

}  // namespace mozilla

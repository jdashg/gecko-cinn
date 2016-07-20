/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebGLExtensions.h"

#include "GLContext.h"
#include "mozilla/dom/WebGLRenderingContextBinding.h"
#include "WebGLContext.h"

namespace mozilla {

void
WebGLExtensionInstancedArrays::DrawArraysInstancedANGLE(GLenum mode,
                                                        GLint first,
                                                        GLsizei count,
                                                        GLsizei primcount)
{
    if (!mContext)
        return;

    mContext->DrawArraysInstanced(mode, first, count, primcount);
}

void
WebGLExtensionInstancedArrays::DrawElementsInstancedANGLE(GLenum mode,
                                                          GLsizei count,
                                                          GLenum type,
                                                          WebGLintptr offset,
                                                          GLsizei primcount)
{
    if (!mContext)
        return;

    mContext->DrawElementsInstanced(mode, count, type, offset, primcount);
}

void
WebGLExtensionInstancedArrays::VertexAttribDivisorANGLE(GLuint index,
                                                        GLuint divisor)
{
    if (!mContext)
        return;

    mContext->VertexAttribDivisor(index, divisor);
}

/*static*/ bool
WebGLExtensionInstancedArrays::IsSupported(const WebGLContext* webgl)
{
    if (webgl->IsWebGL2())
        return false;

    gl::GLContext* gl = webgl->GL();
    return gl->IsSupported(gl::GLFeature::draw_instanced) &&
           gl->IsSupported(gl::GLFeature::instanced_arrays);
}

IMPL_WEBGL_EXTENSION_GOOP(WebGLExtensionInstancedArrays, ANGLE_instanced_arrays)

} // namespace mozilla

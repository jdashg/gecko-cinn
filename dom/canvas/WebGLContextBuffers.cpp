/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebGLContext.h"

#include "GLContext.h"
#include "WebGLBuffer.h"
#include "WebGLTransformFeedback.h"
#include "WebGLVertexArray.h"

namespace mozilla {

WebGLRefPtr<WebGLBuffer>* WebGLContext::ValidateBufferSlot(GLenum target) {
  WebGLRefPtr<WebGLBuffer>* slot = nullptr;

  switch (target) {
    case LOCAL_GL_ARRAY_BUFFER:
      slot = &mBoundArrayBuffer;
      break;

    case LOCAL_GL_ELEMENT_ARRAY_BUFFER:
      slot = &(mBoundVertexArray->mElementArrayBuffer);
      break;
  }

  if (IsWebGL2()) {
    switch (target) {
      case LOCAL_GL_COPY_READ_BUFFER:
        slot = &mBoundCopyReadBuffer;
        break;

      case LOCAL_GL_COPY_WRITE_BUFFER:
        slot = &mBoundCopyWriteBuffer;
        break;

      case LOCAL_GL_PIXEL_PACK_BUFFER:
        slot = &mBoundPixelPackBuffer;
        break;

      case LOCAL_GL_PIXEL_UNPACK_BUFFER:
        slot = &mBoundPixelUnpackBuffer;
        break;

      case LOCAL_GL_TRANSFORM_FEEDBACK_BUFFER:
        slot = &mBoundTransformFeedbackBuffer;
        break;

      case LOCAL_GL_UNIFORM_BUFFER:
        slot = &mBoundUniformBuffer;
        break;
    }
  }

  if (!slot) {
    ErrorInvalidEnumInfo("target", target);
    return nullptr;
  }

  return slot;
}

WebGLBuffer* WebGLContext::ValidateBufferSelection(GLenum target) {
  const auto& slot = ValidateBufferSlot(target);
  if (!slot) return nullptr;
  const auto& buffer = *slot;

  if (!buffer) {
    ErrorInvalidOperation("Buffer for `target` is null.");
    return nullptr;
  }

  if (target == LOCAL_GL_TRANSFORM_FEEDBACK_BUFFER) {
    if (mBoundTransformFeedback->IsActiveAndNotPaused()) {
      ErrorInvalidOperation(
          "Cannot select TRANSFORM_FEEDBACK_BUFFER when"
          " transform feedback is active and unpaused.");
      return nullptr;
    }
    if (buffer->IsBoundForNonTF()) {
      ErrorInvalidOperation(
          "Specified WebGLBuffer is currently bound for"
          " non-transform-feedback.");
      return nullptr;
    }
  } else {
    if (buffer->IsBoundForTF()) {
      ErrorInvalidOperation(
          "Specified WebGLBuffer is currently bound for"
          " transform feedback.");
      return nullptr;
    }
  }

  return buffer.get();
}

IndexedBufferBinding* WebGLContext::ValidateIndexedBufferSlot(GLenum target,
                                                              GLuint index) {
  decltype(mIndexedUniformBufferBindings)* bindings;
  const char* maxIndexEnum;
  switch (target) {
    case LOCAL_GL_TRANSFORM_FEEDBACK_BUFFER:
      bindings = &(mBoundTransformFeedback->mIndexedBindings);
      maxIndexEnum = "MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS";
      break;

    case LOCAL_GL_UNIFORM_BUFFER:
      bindings = &mIndexedUniformBufferBindings;
      maxIndexEnum = "MAX_UNIFORM_BUFFER_BINDINGS";
      break;

    default:
      ErrorInvalidEnumInfo("target", target);
      return nullptr;
  }

  if (index >= bindings->size()) {
    ErrorInvalidValue("`index` >= %s.", maxIndexEnum);
    return nullptr;
  }

  return &(*bindings)[index];
}

////////////////////////////////////////

void WebGLContext::BindBuffer(GLenum target, WebGLBuffer* buffer) {
  const FuncScope funcScope(*this, "bindBuffer");
  if (IsContextLost()) return;

  if (buffer && !ValidateObject("buffer", *buffer)) return;

  const auto& slot = ValidateBufferSlot(target);
  if (!slot) return;

  if (buffer && !buffer->ValidateCanBindToTarget(target)) return;

  gl->fBindBuffer(target, buffer ? buffer->mGLName : 0);

  WebGLBuffer::SetSlot(target, buffer, slot);
  if (buffer) {
    buffer->SetContentAfterBind(target);
  }

  switch (target) {
    case LOCAL_GL_PIXEL_PACK_BUFFER:
    case LOCAL_GL_PIXEL_UNPACK_BUFFER:
      gl->fBindBuffer(target, 0);
      break;
  }
}

////////////////////////////////////////

bool WebGLContext::ValidateIndexedBufferBinding(
    GLenum target, GLuint index,
    WebGLRefPtr<WebGLBuffer>** const out_genericBinding,
    IndexedBufferBinding** const out_indexedBinding) {
  *out_genericBinding = ValidateBufferSlot(target);
  if (!*out_genericBinding) return false;

  *out_indexedBinding = ValidateIndexedBufferSlot(target, index);
  if (!*out_indexedBinding) return false;

  if (target == LOCAL_GL_TRANSFORM_FEEDBACK_BUFFER &&
      mBoundTransformFeedback->mIsActive) {
    ErrorInvalidOperation(
        "Cannot update indexed buffer bindings on active"
        " transform feedback objects.");
    return false;
  }

  return true;
}

void WebGLContext::BindBufferBase(GLenum target, GLuint index,
                                  WebGLBuffer* buffer) {
  const FuncScope funcScope(*this, "bindBufferBase");
  if (IsContextLost()) return;

  if (buffer && !ValidateObject("buffer", *buffer)) return;

  WebGLRefPtr<WebGLBuffer>* genericBinding;
  IndexedBufferBinding* indexedBinding;
  if (!ValidateIndexedBufferBinding(target, index, &genericBinding,
                                    &indexedBinding)) {
    return;
  }

  if (buffer && !buffer->ValidateCanBindToTarget(target)) return;

  ////

  gl->fBindBufferBase(target, index, buffer ? buffer->mGLName : 0);

  ////

  WebGLBuffer::SetSlot(target, buffer, genericBinding);
  WebGLBuffer::SetSlot(target, buffer, &indexedBinding->mBufferBinding);
  indexedBinding->mRangeStart = 0;
  indexedBinding->mRangeSize = 0;

  if (buffer) {
    buffer->SetContentAfterBind(target);
  }
}

void WebGLContext::BindBufferRange(GLenum target, GLuint index,
                                   WebGLBuffer* buffer, WebGLintptr offset,
                                   WebGLsizeiptr size) {
  const FuncScope funcScope(*this, "bindBufferRange");
  if (IsContextLost()) return;

  if (buffer && !ValidateObject("buffer", *buffer)) return;

  if (!ValidateNonNegative("offset", offset) ||
      !ValidateNonNegative("size", size)) {
    return;
  }

  WebGLRefPtr<WebGLBuffer>* genericBinding;
  IndexedBufferBinding* indexedBinding;
  if (!ValidateIndexedBufferBinding(target, index, &genericBinding,
                                    &indexedBinding)) {
    return;
  }

  if (buffer && !buffer->ValidateCanBindToTarget(target)) return;

  if (buffer && !size) {
    ErrorInvalidValue("Size must be non-zero for non-null buffer.");
    return;
  }

  ////

  switch (target) {
    case LOCAL_GL_TRANSFORM_FEEDBACK_BUFFER:
      if (offset % 4 != 0 || size % 4 != 0) {
        ErrorInvalidValue("For %s, `offset` and `size` must be multiples of 4.",
                          "TRANSFORM_FEEDBACK_BUFFER");
        return;
      }
      break;

    case LOCAL_GL_UNIFORM_BUFFER: {
      GLuint offsetAlignment = 0;
      gl->GetUIntegerv(LOCAL_GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT,
                       &offsetAlignment);
      if (offset % offsetAlignment != 0) {
        ErrorInvalidValue("For %s, `offset` must be a multiple of %s.",
                          "UNIFORM_BUFFER", "UNIFORM_BUFFER_OFFSET_ALIGNMENT");
        return;
      }
    } break;
  }

    ////

#ifdef XP_MACOSX
  if (buffer && buffer->Content() == WebGLBuffer::Kind::Undefined &&
      gl->WorkAroundDriverBugs()) {
    // BindBufferRange will fail if the buffer's contents is undefined.
    // Bind so driver initializes the buffer.
    gl->fBindBuffer(target, buffer->mGLName);
  }
#endif

  gl->fBindBufferRange(target, index, buffer ? buffer->mGLName : 0, offset,
                       size);

  ////

  WebGLBuffer::SetSlot(target, buffer, genericBinding);
  WebGLBuffer::SetSlot(target, buffer, &indexedBinding->mBufferBinding);
  indexedBinding->mRangeStart = offset;
  indexedBinding->mRangeSize = size;

  if (buffer) {
    buffer->SetContentAfterBind(target);
  }
}

////////////////////////////////////////

RefPtr<ABuffer> ContextGL::CreateBuffer(const bool isIndexBuffer) {
  const FuncScope funcScope(*this, "createBuffer");
  GLuint buf = 0;
  gl->fGenBuffers(1, &buf);
  return new BufferGL(this, isIndexBuffer, buf);
}

}  // namespace mozilla

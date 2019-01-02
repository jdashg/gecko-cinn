/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebGL2Context.h"

#include "GLContext.h"
#include "WebGLBuffer.h"
#include "WebGLTransformFeedback.h"

namespace mozilla {
namespace webgl {

void BufferGL::CopyBufferSubData(const uint64_t destOffset, const ABuffer& asrc,
                                 const uint64_t srcOffset, const uint64_t size) {
  const FuncScope funcScope(*mContext, "copyBufferSubData");
  if (IsContextLost()) return;
  const auto& src = *asrc->AsGL();

  const auto fnValidateOffsetSize = [&](const char* const info, const uint64_t offset,
                                        const BufferGL& buffer) {
    const auto neededBytes = CheckedInt<uint64_t>(offset) + size;
    if (!neededBytes.isValid() || neededBytes.value() > buffer.ByteLength()) {
      mContext->ErrorInvalidValue("Invalid %s range.", info);
      return false;
    }
    return true;
  };

  if (!fnValidateOffsetSize("src", readOffset, src) ||
      !fnValidateOffsetSize("dest", destOffset, *this)) {
    return;
  }

  if (&src == this) {
    const bool separate =
        (srcOffset + size <= destOffset || destOffset + size <= srcOffset);
    if (!separate) {
      mContext->ErrorInvalidValue(
          "Ranges [readOffset, readOffset + size) and"
          " [writeOffset, writeOffset + size) overlap.");
      return;
    }
  }

  if (src.mIsIndexBuffer != mIsIndexBuffer) {
    mContext->ErrorInvalidOperation("Can't copy between index and non-index buffers.");
    return;
  }

  const ScopedLazyBind readBind(gl, readTarget, readBuffer);
  const ScopedLazyBind writeBind(gl, writeTarget, writeBuffer);
  gl->fCopyBufferSubData(readTarget, writeTarget, readOffset, writeOffset,
                         size);

  writeBuffer->ResetLastUpdateFenceId();
}

void BufferGL::GetBufferSubData(const uint64_t srcOffset, uint8_t* const dest,
                                const uint64_t size) const
{
  const FuncScope funcScope(*this, "getBufferSubData");
  if (IsContextLost()) return;
  if (!ValidateRange(srcOffset, size)) return;

  // -

  const auto& gl = mContext->gl;

  const auto target = ImplicitTarget();
  gl->fBindBuffer(target, mGLName);

  if (size) {
    const auto mappedBytes = gl->fMapBufferRange(
        target, srcOffset, size, LOCAL_GL_MAP_READ_BIT);
    memcpy(dest, mappedBytes, size);
    gl->fUnmapBuffer(target);
  }

  gl->fBindBuffer(target, 0);
}

}  // namespace webgl
}  // namespace mozilla

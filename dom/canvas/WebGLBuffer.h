/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef WEBGL_BUFFER_H_
#define WEBGL_BUFFER_H_

#include <map>

#include "CacheInvalidator.h"
#include "GLDefs.h"
#include "mozilla/LinkedList.h"
#include "nsWrapperCache.h"
#include "WebGLObjectModel.h"
#include "WebGLTypes.h"

namespace mozilla {
namespace webgl {

class BufferGL final : public ABuffer {
  //enemy class ContextJS;
 public:
  const GLuint mGLName;

  // -

  BufferGL(ContextGL* webgl, bool isIndexBuffer);
  auto AsGL() override { return this; }

  size_t SizeOfIncludingThis(mozilla::MallocSizeOf mallocSizeOf) const;

  auto ByteLength() const { return mByteLength; }

  Maybe<uint32_t> GetIndexedFetchMaxVert(GLenum type, uint64_t byteOffset,
                                         uint32_t indexCount) const;
  bool ValidateRange(uint64_t byteOffset, uint64_t byteLen) const;

private:
  GLenum ImplicitTarget() const;
public:
  void BufferData(GLenum usage, uint64_t srcDataLen, const uint8_t* srcData) override;
  void BufferSubData(uint64_t dstByteOffset, uint64_t srcDataLen, const uint8_t* srcData) override;
  void CopyBufferSubData(uint64_t destOffset, const ABuffer& asrc, uint64_t srcOffset,
                         uint64_t size) override;
  void GetBufferSubData(uint64_t srcOffset, uint8_t* dest, uint64_t size) const override;

  ////

 private:
  ~BufferGL();

  void InvalidateCacheRange(uint64_t byteOffset, uint64_t byteLength) const;

  uint64_t mByteLength = 0;
  mutable uint64_t mLastUpdateFenceId = 0;

  struct IndexRange final {
    GLenum type;
    uint64_t byteOffset;
    uint32_t indexCount;

    bool operator<(const IndexRange& x) const {
      if (type != x.type) return type < x.type;

      if (byteOffset != x.byteOffset) return byteOffset < x.byteOffset;

      return indexCount < x.indexCount;
    }
  };

  UniqueBuffer mIndexCache;
  mutable std::map<IndexRange, Maybe<uint32_t>> mIndexRanges;

 public:
  CacheInvalidator mFetchInvalidator;

  void ResetLastUpdateFenceId() const;
};

}  // namespace mozilla

#endif  // WEBGL_BUFFER_H_

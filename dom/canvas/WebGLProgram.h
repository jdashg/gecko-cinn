/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef WEBGL_PROGRAM_H_
#define WEBGL_PROGRAM_H_

#include <map>
#include <set>
#include <vector>

#include "mozilla/LinkedList.h"
#include "mozilla/RefPtr.h"
#include "mozilla/WeakPtr.h"
#include "nsWrapperCache.h"

#include "CacheInvalidator.h"
#include "WebGLContext.h"
#include "WebGLObjectModel.h"

namespace mozilla {
class ErrorResult;
class WebGLContext;
class WebGLProgram;
class WebGLShader;

namespace dom {
template <typename>
struct Nullable;
class OwningUnsignedLongOrUint32ArrayOrBoolean;
template <typename>
class Sequence;
}  // namespace dom

namespace webgl {

enum class TextureBaseType : uint8_t;

struct UniformBlockInfo final {
  const ActiveUniformBlockInfo& info;
  const IndexedBufferBinding* binding;
};

struct FragOutputInfo final {
  const uint8_t loc;
  const std::string userName;
  const std::string mappedName;
  const TextureBaseType baseType;
};

struct CachedDrawFetchLimits final {
  uint64_t maxVerts = 0;
  uint64_t maxInstances = 0;
  std::vector<BufferAndIndex> usedBuffers;
};

// -

struct SamplerUniformInfo final {
  const decltype(WebGLContext::mBound2DTextures)& texListForType;
  const webgl::TextureBaseType texBaseType;
  const bool isShadowSampler;
  std::vector<uint32_t> texUnits;
};

struct LocationInfo final {
  const ActiveUniformInfo& info;
  const uint32_t indexIntoUniform;
  SamplerUniformInfo* const samplerInfo;
};

// -

struct LinkedProgramInfo final : public RefCounted<LinkedProgramInfo>,
                                 public SupportsWeakPtr<LinkedProgramInfo>,
                                 public CacheInvalidator {
  friend class mozilla::WebGLProgram;

  MOZ_DECLARE_REFCOUNTED_TYPENAME(LinkedProgramInfo)
  MOZ_DECLARE_WEAKREFERENCE_TYPENAME(LinkedProgramInfo)

  //////

  WebGLProgram* const prog;
  const GLenum transformFeedbackBufferMode;

  std::vector<UniformBlockInfo> uniformBlocks;
  std::unordered_map<uint8_t, const FragOutputInfo> fragOutputs;
  uint8_t zLayerCount = 1;

  mutable std::vector<size_t> componentsPerTFVert;

  bool attrib0Active;

  // -

  std::map<std::string, std::string> nameMap;
  webgl::LinkActiveInfo active;


  std::vector<std::unique_ptr<SamplerUniformInfo>> samplerUniforms;
  std::unordered_map<uint32_t, LocationInfo> locationMap;

  //////

  mutable CacheWeakMap<const WebGLVertexArray*, CachedDrawFetchLimits>
      mDrawFetchCache;

  const CachedDrawFetchLimits* GetDrawFetchLimits() const;

  //////

  explicit LinkedProgramInfo(WebGLProgram* prog);
  ~LinkedProgramInfo();
};

}  // namespace webgl

class WebGLProgram final : public WebGLRefCountedObject<WebGLProgram>,
                           public LinkedListElement<WebGLProgram> {
  friend class WebGLTransformFeedback;
  friend struct webgl::LinkedProgramInfo;

 public:
  NS_INLINE_DECL_REFCOUNTING(WebGLProgram)

  explicit WebGLProgram(WebGLContext* webgl);

  void Delete();

  // GL funcs
  void AttachShader(WebGLShader& shader);
  void BindAttribLocation(GLuint index, const std::string& name);
  void DetachShader(const WebGLShader& shader);
  void UniformBlockBinding(GLuint uniformBlockIndex,
                           GLuint uniformBlockBinding) const;

  void LinkProgram();
  bool UseProgram() const;
  void ValidateProgram() const;

  ////////////////

  void TransformFeedbackVaryings(const std::vector<std::string>& varyings,
                                 GLenum bufferMode);

  bool IsLinked() const { return mMostRecentLinkInfo; }

  const webgl::LinkedProgramInfo* LinkInfo() const {
    return mMostRecentLinkInfo.get();
  }

  const auto& VertShader() const { return mVertShader; }
  const auto& FragShader() const { return mFragShader; }

 private:
  ~WebGLProgram();

  void LinkAndUpdate();
  bool ValidateForLink();
  bool ValidateAfterTentativeLink(std::string* const out_linkLog) const;

 public:
  const GLuint mGLName;

 private:
  WebGLRefPtr<WebGLShader> mVertShader;
  WebGLRefPtr<WebGLShader> mFragShader;
  size_t mNumActiveTFOs;

  std::map<std::string, GLuint> mNextLink_BoundAttribLocs;

  std::vector<std::string> mNextLink_TransformFeedbackVaryings;
  GLenum mNextLink_TransformFeedbackBufferMode;

  std::string mLinkLog;
  RefPtr<const webgl::LinkedProgramInfo> mMostRecentLinkInfo;
};

}  // namespace mozilla

#endif  // WEBGL_PROGRAM_H_

/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef WEBGLCONTEXT_H_
#define WEBGLCONTEXT_H_

#include <stdarg.h>

#include "GLContextTypes.h"
#include "GLDefs.h"
#include "mozilla/Attributes.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/dom/HTMLCanvasElement.h"
#include "mozilla/dom/Nullable.h"
#include "mozilla/dom/TypedArray.h"
#include "mozilla/EnumeratedArray.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/LinkedList.h"
#include "mozilla/UniquePtr.h"
#include "nsCycleCollectionNoteChild.h"
#include "nsICanvasRenderingContextInternal.h"
#include "nsLayoutUtils.h"
#include "nsTArray.h"
#include "nsWrapperCache.h"
#include "SurfaceTypes.h"
#include "ScopedGLHelpers.h"
#include "TexUnpackBlob.h"

// Local
#include "CacheInvalidator.h"
#include "WebGLContextLossHandler.h"
#include "WebGLObjectModel.h"
#include "WebGLStrongTypes.h"

// Generated
#include "nsIDOMEventListener.h"
#include "nsICanvasRenderingContextInternal.h"
#include "nsIObserver.h"
#include "mozilla/dom/HTMLCanvasElement.h"
#include "nsWrapperCache.h"
#include "nsLayoutUtils.h"
#include "mozilla/dom/WebGLRenderingContextBinding.h"
#include "mozilla/dom/WebGL2RenderingContextBinding.h"

class nsIDocShell;

// WebGL-only GLenums
// clang-format off
#define LOCAL_GL_BROWSER_DEFAULT_WEBGL              0x9244
#define LOCAL_GL_CONTEXT_LOST_WEBGL                 0x9242
#define LOCAL_GL_MAX_CLIENT_WAIT_TIMEOUT_WEBGL      0x9247
#define LOCAL_GL_UNPACK_COLORSPACE_CONVERSION_WEBGL 0x9243
#define LOCAL_GL_UNPACK_FLIP_Y_WEBGL                0x9240
#define LOCAL_GL_UNPACK_PREMULTIPLY_ALPHA_WEBGL     0x9241
// clang-format on

namespace mozilla {
class ScopedCopyTexImageSource;
class ScopedDrawCallWrapper;
class ScopedResolveTexturesForDraw;
class ScopedUnpackReset;
class WebGLActiveInfo;
class WebGLBuffer;
class WebGLExtensionBase;
class WebGLFramebuffer;
class WebGLProgram;
class WebGLQuery;
class WebGLRenderbuffer;
class WebGLSampler;
class WebGLShader;
class WebGLShaderPrecisionFormat;
class WebGLSync;
class WebGLTexture;
class WebGLTransformFeedback;
class WebGLUniformLocation;
class WebGLVertexArray;

namespace dom {
class Element;
class ImageData;
class OwningHTMLCanvasElementOrOffscreenCanvas;
struct WebGLContextAttributes;
}  // namespace dom

namespace gfx {
class SourceSurface;
class VRLayerChild;
}  // namespace gfx

namespace gl {
class GLScreenBuffer;
class MozFramebuffer;
}  // namespace gl

namespace webgl {
class AvailabilityRunnable;
struct CachedDrawFetchLimits;
struct FormatInfo;
class FormatUsageAuthority;
struct FormatUsageInfo;
struct ImageInfo;
struct LinkedProgramInfo;
struct SamplingState;
class ScopedPrepForResourceClear;
class ShaderValidator;
class TexUnpackBlob;
struct UniformInfo;
struct UniformBlockInfo;
}  // namespace webgl

WebGLTexelFormat GetWebGLTexelFormat(TexInternalFormat format);

void AssertUintParamCorrect(gl::GLContext* gl, GLenum pname, GLuint shadow);

struct WebGLContextOptions {
  bool alpha = true;
  bool depth = true;
  bool stencil = false;
  bool premultipliedAlpha = true;
  bool antialias = true;
  bool preserveDrawingBuffer = false;
  bool failIfMajorPerformanceCaveat = false;
  dom::WebGLPowerPreference powerPreference =
      dom::WebGLPowerPreference::Default;

  WebGLContextOptions();
  bool operator==(const WebGLContextOptions&) const;
};

// From WebGLContextUtils
TexTarget TexImageTargetToTexTarget(TexImageTarget texImageTarget);

struct WebGLIntOrFloat {
  const enum { Int, Float, Uint } mType;

  union {
    GLint i;
    GLfloat f;
    GLuint u;
  } mValue;

  explicit WebGLIntOrFloat(GLint i) : mType(Int) { mValue.i = i; }
  explicit WebGLIntOrFloat(GLfloat f) : mType(Float) { mValue.f = f; }

  GLint AsInt() const {
    return (mType == Int) ? mValue.i : NS_lroundf(mValue.f);
  }
  GLfloat AsFloat() const {
    return (mType == Float) ? mValue.f : GLfloat(mValue.i);
  }
};

struct IndexedBufferBinding {
  WebGLRefPtr<WebGLBuffer> mBufferBinding;
  uint64_t mRangeStart;
  uint64_t mRangeSize;

  IndexedBufferBinding();

  uint64_t ByteCount() const;
};

////

struct FloatOrInt final  // For TexParameter[fi] and friends.
{
  const bool isFloat;
  const GLfloat f;
  const GLint i;

  explicit FloatOrInt(GLint x) : isFloat(false), f(x), i(x) {}

  explicit FloatOrInt(GLfloat x) : isFloat(true), f(x), i(roundf(x)) {}

  FloatOrInt& operator=(const FloatOrInt& x) {
    memcpy(this, &x, sizeof(x));
    return *this;
  }
};

////////////////////////////////////

struct TexImageSource {
  const dom::ArrayBufferView* mView;
  GLuint mViewElemOffset;
  GLuint mViewElemLengthOverride;

  const WebGLsizeiptr* mPboOffset;

  const dom::ImageBitmap* mImageBitmap;
  const dom::ImageData* mImageData;

  const dom::Element* mDomElem;
  ErrorResult* mOut_error;

 protected:
  TexImageSource() { memset(this, 0, sizeof(*this)); }
};

////

struct TexImageSourceAdapter final : public TexImageSource {
  TexImageSourceAdapter(const dom::Nullable<dom::ArrayBufferView>* maybeView,
                        ErrorResult*) {
    if (!maybeView->IsNull()) {
      mView = &(maybeView->Value());
    }
  }

  TexImageSourceAdapter(const dom::ArrayBufferView* view, ErrorResult*) {
    mView = view;
  }

  TexImageSourceAdapter(const dom::ArrayBufferView* view, GLuint viewElemOffset,
                        GLuint viewElemLengthOverride = 0) {
    mView = view;
    mViewElemOffset = viewElemOffset;
    mViewElemLengthOverride = viewElemLengthOverride;
  }

  TexImageSourceAdapter(const WebGLsizeiptr* pboOffset, GLuint ignored1,
                        GLuint ignored2 = 0) {
    mPboOffset = pboOffset;
  }

  TexImageSourceAdapter(const WebGLsizeiptr* pboOffset, ErrorResult* ignored) {
    mPboOffset = pboOffset;
  }

  TexImageSourceAdapter(const dom::ImageBitmap* imageBitmap,
                        ErrorResult* out_error) {
    mImageBitmap = imageBitmap;
    mOut_error = out_error;
  }

  TexImageSourceAdapter(const dom::ImageData* imageData, ErrorResult*) {
    mImageData = imageData;
  }

  TexImageSourceAdapter(const dom::Element* domElem,
                        ErrorResult* const out_error) {
    mDomElem = domElem;
    mOut_error = out_error;
  }
};

// --

namespace webgl {
class AvailabilityRunnable final : public Runnable {
 public:
  const RefPtr<WebGLContext> mWebGL;  // Prevent CC
  std::vector<RefPtr<WebGLQuery>> mQueries;
  std::vector<RefPtr<WebGLSync>> mSyncs;

  explicit AvailabilityRunnable(WebGLContext* webgl);
  ~AvailabilityRunnable();

  NS_IMETHOD Run() override;
};
}  // namespace webgl

////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////

namespace webgl {

/**
 * All-new:
 * ContextJS/BufferJS/etc
 *
 * s/WebGLContext/webgl::ContextGL/
 * s/WebGLBuffer/webgl::BufferGL/
 *
 * class BufferGL : public ABuffer
 * class BufferDispatch : public ABuffer
 *
 * class ContextGL : public AContext {
 *   rp<ABuffer> CreateBuffer() override;
 * }
 *
 * ContextJS:
 *   rp<ContextGL> inner
 *   rp<BufferJS> array_buffer
 *
 * BufferJS:
 *   rp<ABuffer> inner_ref; // nulled on deletion.
 *   WeakPtr<ABuffer> inner
 *
 */
class ObjectJS : public VRefCounted
{
private:
  const ContextJS* mContext;
  bool mIsDeleted = false;
  GLenum mTarget = 0;
  bool mDtorShouldDelete_Called = false;

  // -

public:
  const auto& Context() const { mContext; }
  const auto& Target() const { mTarget; }

  void MarkDeletedOrLost() {
    if (!mContext)
      return;

    mIsDeleted = true;
    ReleaseChildren();
  }

protected:
  bool DtorShouldDelete() {
    mDtorShouldDelete_Called = true;
    return mContext;
  }

private:
  explicit Object(const ContextJS* const context)
    : mContext(context)
  { }

  virtual ~Object() {
    MOZ_RELEASE_ASSERT(mDtorShouldDelete_Called);
    MOZ_RELEASE_ASSERT(!mContext);
  }

  virtual void ReleaseChildren() {}

  void OnContextLost() {
    MarkDeletedOrLost();
  }
};

class ABuffer : public VRefCounted, public VSupportsWeakPtr
{
  virtual BufferGL* AsGL() const {
    MOZ_CRASH("Not for gl.");
  }
  virtual BufferDispatch* AsDispatch() {
    MOZ_CRASH("Not for dispatch.");
  }

  auto AsGL() const {
    return const_cast<const BufferGL*>(const_cast<ABuffer*>(this)->AsGL());
  }
  auto AsDispatch() const {
    return const_cast<const BufferDispatch*>(const_cast<ABuffer*>(this)->AsDispatch());
  }
};

class BufferTfBindCounts final {
  size_t mTfBindCount = 0;
  size_t mNonTfBindCount = 0;

public:
  bool IsBoundForTf() const { return bool(mTfBindCount); }
  bool IsBoundForNonTf() const { return bool(mNonTfBindCount); }

  enum class For {
    NonTf,
    Tf,
  };
  enum class On {
    Bind,
    Unbind,
  };

  void Add(const On on, const For forType) {
      auto* slot = &mNonTfBindCount;
      if (forType == For::Tf) {
          slot = &mTfBindCount;
      }
      *slot += (on == On::Bind) ? +1 : -1;
  }

  template<typename T>
  static void UpdateSlot(const GLenum target, RefPtr<T>* const slot,
                         T* const next)
  {
    const auto forType = (target == LOCAL_GL_TRANSFORM_FEEDBACK) ? For::Tf
                                                                 : For::NonTf;
    if (*slot) {
      (*slot)->OnBindChange(On::Unbind, forType);
    }
    *slot = next;
    if (*slot) {
      (*slot)->OnBindChange(On::Bind, forType);
    }
  }
}

class BufferJS
    : public ObjectJS
    , public nsWrapperCache
{
  friend class ContextJS;

  RefPtr<ABuffer> mInnerRef;
  WeakPtr<ABuffer> mInner;
  size_t mNonTfUseCount = 0;
  size_t mTfUseCount = 0;

public:
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(BufferJS)
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_NATIVE_CLASS(BufferJS)
  JSObject* WrapObject(JSContext*, JS::Handle<JSObject*>) override;

  explicit BufferJS(ContextJS* const context)
    : ObjectJS(context)
  { }

  ~BufferJS() {
    if (DtorShouldDelete()) {
      mContext->DeleteBuffer(this);
    }
  }

  enum class BindingFor {
    NonTf,
    Tf,
  };

  void OnBindChange(const BindingFor bindingFor, const int8_t diff) {
    auto slot = &mNonTfUseCount;
    if (bindingFor == BindingFor::Tf) {
      slot = &mTfUseCount;
    }
    *slot += diff;
  }

  static void UpdateSlot(const BindingFor boundFor, RefPtr<BufferJS>* const slot,
                         BufferJS* const next)
  {
    if (*slot) {
      (*slot)->OnBindChange(boundFor, -1);
    }
    *slot = next;
    if (*slot) {
      (*slot)->OnBindChange(boundFor, +1);
    }
  }
};

class TransformFeedbackJS
    : public ObjectJS
    , public nsWrapperCache
{
  friend class ContextJS;

  std::vector<RefPtr<BufferJS>> mAttribs;

  enum class Status {
    Inactive,
    Active,
    Paused,
  };
  Status mStatus = Status::Inactive;

public:
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(TransformFeedbackJS)
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_NATIVE_CLASS(TransformFeedbackJS)
  JSObject* WrapObject(JSContext*, JS::Handle<JSObject*>) override;

  explicit TransformFeedbackJS(ContextJS* const context)
    : ObjectJS(context)
  { }

  ~TransformFeedbackJS() {
    if (DtorShouldDelete()) {
      mContext->DeleteTransformFeedback(this);
    }
  }

public:
  void OnBindChange(const int8_t diff) const {
    const auto fn = [&](BufferJS* const x) {
      if (!x) return;
      x->OnBindChange(BufferJS::BindingFor::Tf, diff);
    };
    for (const auto& slot : mAttribs) {
      fn(slot.get());
    }
  }

  void ReleaseChildren() override {
    MOZ_ASSERT(mContext->mBoundTfo != this);
    for (auto& slot : mAttribs) {
      slot = nullptr;
    }
  }
};

class VertexArrayJS
    : public ObjectJS
    , public nsWrapperCache
{
  friend class ContextJS;

  RefPtr<BufferJS> mIndexBuffer;
  std::vector<RefPtr<BufferJS>> mAttribs;

public:
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(VertexArrayJS)
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_NATIVE_CLASS(VertexArrayJS)
  JSObject* WrapObject(JSContext*, JS::Handle<JSObject*>) override;

  explicit VertexArrayJS(ContextJS* const context)
    : ObjectJS(context)
  { }

  ~VertexArrayJS() {
    if (DtorShouldDelete()) {
      mContext->DeleteVertexArray(this);
    }
  }

public:
  void OnBindChange(const int8_t diff) const {
    const auto fn = [&](BufferJS* const x) {
      if (!x) return;
      x->OnBindChange(BufferJS::BindingFor::NonTf, diff);
    };
    fn(mIndexBuffer.get());
    for (const auto& slot : mAttribs) {
      fn(slot.get());
    }
  }

  void ReleaseChildren() override {
    MOZ_ASSERT(mContext->mBoundVao != this);
    mIndexBuffer = nullptr;
    for (auto& slot : mAttribs) {
      slot = nullptr;
    }
  }
};

////////////////////////////////////////////////////////////////////////////////

template<typename T>
struct auto_AddRefed final
{
    RefPtr<T> ptr;

    template<typename U>
    explicit auto_AddRefed(std::nullptr_t)
    { }

    template<typename U>
    explicit auto_AddRefed(U* const rhs)
        : ptr(rhs)
    { }

    template<typename U>
    explicit auto_AddRefed(const RefPtr<U>& rhs)
        : ptr(rhs)
    { }

    explicit operator already_AddRefed<T>() const {
        auto ret = ptr;
        return ret.forget();
    }

    explicit operator RefPtr<T>() const {
        return ptr;
    }
};

////////////////////////////////////////////////////////////////////////////////


class ContextJS : public nsICanvasRenderingContextInternal,
                     public nsWrapperCache {
  friend class Context2JS;
  friend class WebGLContextUserData;
  friend class WebGLExtensionCompressedTextureASTC;
  friend class WebGLExtensionCompressedTextureBPTC;
  friend class WebGLExtensionCompressedTextureES3;
  friend class WebGLExtensionCompressedTextureETC1;
  friend class WebGLExtensionCompressedTexturePVRTC;
  friend class WebGLExtensionCompressedTextureRGTC;
  friend class WebGLExtensionCompressedTextureS3TC;
  friend class WebGLExtensionCompressedTextureS3TC_SRGB;
  friend class WebGLExtensionDepthTexture;
  friend class WebGLExtensionDisjointTimerQuery;
  friend class WebGLExtensionDrawBuffers;
  friend class WebGLExtensionLoseContext;
  friend class WebGLExtensionMOZDebug;
  friend class WebGLExtensionVertexArray;
  friend class webgl::AvailabilityRunnable;
  friend struct webgl::LinkedProgramInfo;
  friend struct webgl::UniformBlockInfo;

 private:
  RefPtr<AContext> mInner;

 public:
  ContextJS();

 protected:
  virtual ~ContextJS();

 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS_AMBIGUOUS(
      ContextJS, nsICanvasRenderingContextInternal)
  virtual JSObject* WrapObject(JSContext*, JS::Handle<JSObject*>) override;

  // ----------------------------------

private:
  std::unordered_map<GLenum, RefPtr<BufferJS>> mBoundBufferByTarget;
  std::vector<RefPtr<BufferJS>> mBoundUbos;

  // -

  RefPtr<BufferJS>* GetBufferSlot(const GLenum target) {
    if (target == LOCAL_GL_ELEMENT_ARRAY_BUFFER) return &mBoundVao->mIndexBuffer;

    const auto itr = mBoundBufferByTarget.find(target);
    if (itr != mBoundBufferByTarget.end()) return &(itr->second);

    ErrorInvalidEnum("Bad target.");
    return nullptr;
  }

  BufferJS* BufferByTarget(const GLenum target) const {
    const auto slot = GetBufferSlot(target);
    if (!slot) return;

    const auto buffer = *slot;
    if (!buffer) {
      ErrorInvalidOperation("No buffer bound.");
      return nullptr;
    }
    return buffer;
  }

  // -

  bool TryBufferTargetAssignment(GLenum* const bufferTarget, const GLenum newTarget) {
    const bool ok = [&]() {
      if (!*bufferTarget) return true;
      const bool isCurIndex = *bufferTarget == LOCAL_GL_ELEMENT_ARRAY_BUFFER;
      const bool isNewIndex = newTarget == LOCAL_GL_ELEMENT_ARRAY_BUFFER;
      return isNewIndex == isCurIndex;
    }();
    if (!ok) {
      ErrorInvalidOperation("Buffer can no longer be bound as that target.");
      return false;
    }
    *bufferTarget = newTarget;
    return true;
  }

public:
  void BindBuffer(const GLenum target, BufferJS* const buffer) {
    const auto context = GetContext();
    if (!context) return nullptr;
    if (buffer && !IsBuffer(buffer)) return;

    const auto slot = GetBufferSlot(target);
    if (!slot) return;

    if (buffer) {
      if (!TryBufferTargetAssignment(context, &buffer->mTarget, target)) return;
    }

    *slot = buffer;
  }

  // -

  private:
    void BindBufferRangeImpl(GLenum target, GLuint index, BufferJS* buffer,
                             uint64_t offset, uint64_t size) {
      const auto context = GetContext();
      if (!context) return nullptr;
      if (buffer && !IsBuffer(buffer)) return;

      switch (target) {
        case LOCAL_GL_TRANSFORM_FEEDBACK_BUFFER:
        case LOCAL_GL_UNIFORM_BUFFER:
          break;
        default:
          ErrorInvalidEnum("Bad target.");
          return;
      }
      const auto slot = GetBufferSlot(target);
      MOZ_ASSERT(slot);

      auto bindingFor = BufferJS::BindingFor::NonTf;
      if (target == LOCAL_GL_TRANSFORM_FEEDBACK_BUFFER) {
        bindingFor = BufferJS::BindingFor::Tf;
        if (mBoundTfo->mIsActive) {
          ErrorInvalidOperation("Active Transform Feedback objects are immutable.");
          return;
        }
      }
      ABuffer* abuffer = nullptr;
      if (buffer) {
        if (!TryBufferTargetAssignment(&buffer->mTarget, target)) return;
        abuffer = buffer->mInner;
      }

      context->BindBufferRange(target, index, abuffer, offset, size);
    }

public:
    void BindBufferBase(GLenum target, GLuint index, BufferJS* buffer) {
      BindBufferRangeImpl(target, index, buffer, 0, UINT64_MAX);
    }

    void BindBufferRange(GLenum target, GLuint index, BufferJS* buffer,
                         WebGLintptr offset, WebGLsizeiptr size) {
      if (!ValidateNonNegative("offset", offset) ||
          !ValidateNonNegative("size", size)) {
        return;
      }
      BindBufferRangeImpl(target, index, buffer, offset, size);
    }

  // -

private:
  void BufferDataImpl(GLenum target, uint64_t dataLen, const uint8_t* data, GLenum usage) {
    const auto buffer = BufferByTarget(target);
    if (!buffer) return;
    buffer->mInner->BufferData(usage, dataLen, data);
  }

public:
  void BufferData(GLenum target, WebGLsizeiptr size, GLenum usage) {
    if (size < 0) {
      ErrorInvalidValue("size");
      return;
    }
    BufferDataImpl(target, uint64_t(size), nullptr, usage);
  }

  void BufferData(GLenum target, const dom::Nullable<dom::ArrayBuffer>& maybeSrc,
                  GLenum usage) {
    const auto src = GetNonNull(maybeSrc);
    if (!src) return;

    uint64_t srcByteLen;
    uint8_t* srcBytes;
    GetLengthAndData(*src, &srcByteLen, &srcBytes);
    BufferDataImpl(target, srcByteLen, srcBytes, usage);
  }

  void BufferData(GLenum target, const dom::ArrayBufferView& src, GLenum usage,
                  GLuint srcElemOffset = 0, GLuint srcElemCountOverride = 0) {
    uint64_t srcByteLen;
    uint8_t* srcBytes;
    if (!ValidateArrayBufferView(src, srcElemOffset, srcElemCountOverride, &srcBytes,
                                 &srcByteLen)) {
      return;
    }
    BufferDataImpl(target, srcByteLen, srcBytes, usage);
  }

  // -

private:
  void BufferSubDataImpl(GLenum target, WebGLsizeiptr dstByteOffset,
                         uint64_t srcDataLen, const uint8_t* srcData) {
    if (!ValidateNonNegative("dstByteOffset", dstByteOffset)) return;

    const auto buffer = BufferByTarget(target);
    if (!buffer) return;
    buffer->mInner->BufferSubData(uint64_t(dstByteOffset), srcDataLen, srcData);
  }

public:
  void BufferSubData(GLenum target, WebGLsizeiptr dstByteOffset,
                     const dom::ArrayBufferView& src, GLuint srcElemOffset = 0,
                     GLuint srcElemCountOverride = 0) {
    uint64_t srcByteLen;
    uint8_t* srcBytes;
    if (!ValidateArrayBufferView(src, srcElemOffset, srcElemCountOverride, &srcBytes,
                                 &srcByteLen)) {
      return;
    }
    BufferSubDataImpl(target, dstByteOffset, srcByteLen, srcBytes);
  }

  void BufferSubData(GLenum target, WebGLsizeiptr dstByteOffset,
                     const dom::ArrayBuffer& src) {
    uint64_t srcByteLen;
    uint8_t* srcBytes;
    GetLengthAndData(src, &srcByteLen, &srcBytes);
    BufferSubDataImpl(target, dstByteOffset, srcByteLen, srcBytes);
  }

  // -

  void CopyBufferSubData(const GLenum srcTarget, const GLenum destTarget,
                         const GLintptr srcOffset, const GLintptr destOffset,
                         const GLsizeiptr size) {
    if (!ValidateNonNegative("srcOffset", srcOffset) ||
        !ValidateNonNegative("destOffset", destOffset) ||
        !ValidateNonNegative("size", size)) {
      return;
    }

    const auto src = BufferByTarget(srcTarget);
    if (!src) return;
    const auto dest = BufferByTarget(destTarget);
    if (!dest) return;

    dest->mInner->CopyBufferSubData(uint64_t(destOffset), *src->mInner,
                                    uint64_t(srcOffset), uint64_t(size));
  }

  // -

  void GetBufferSubData(GLenum target, GLintptr srcByteOffset,
                        const dom::ArrayBufferView& destData,
                        GLuint destElemOffset, GLuint destElemCountOverride)
  {
    if (!ValidateNonNegative("srcByteOffset", srcByteOffset)) return;

    const auto buffer = BufferByTarget(target);
    if (!buffer) return;

    uint64_t destByteLen;
    uint8_t* destBytes;
    if (!ValidateArrayBufferView(dstData, dstElemOffset, dstElemCountOverride, &destBytes,
                                 &destByteLen)) {
      return;
    }

    // -

    if (mContext->mCompletedFenceId < mLastUpdateFenceId) {
      mContext->GenerateWarning(
          "Reading from a buffer without checking for previous"
          " command completion likely causes pipeline stalls."
          " Please use FenceSync.");
    }

    // -

    buffer->mInner->GetBufferSubData(srcByteOffset, destBytes, destByteLen);
  }

  // ----------------------------------

  RefPtr<VertexArrayJS> mDefaultVao;
  RefPtr<VertexArrayJS> mBoundVao;

  void BindVertexArray(VertexArrayJS* obj) {
    const auto context = GetContext();
    if (!context) return;

    if (!obj) {
      obj = mDefaultVao.get();
    }

    if (!ValidateObject(*obj)) {
      ErrorInvalidOperation("bindVertexArray: Invalid object.");
      return;
    }

    mBoundVao->OnBindChange(-1);
    mBoundVao = obj;
    mBoundVao->mTarget = LOCAL_GL_VERTEX_ARRAY;
    mBoundVao->OnBindChange(+1);

    context->BindVertexArray(*mBoundVao->mInner);
  }

  // ----------------------------------

  RefPtr<TransformFeedbackJS> mDefaultTfo;
  RefPtr<TransformFeedbackJS> mBoundTfo;

  void BindTransformFeedback(const GLenum target, TransformFeedbackJS* obj) {
    const auto context = GetContext();
    if (!context) return;

    if (target != LOCAL_GL_TRANSFORM_FEEDBACK) {
      ErrorInvalidEnum("Bad target.");
      return;
    }

    if (!obj) {
      obj = mDefaultTfo.get();
    }

    if (!ValidateObject(*obj)) {
      ErrorInvalidOperation("Invalid object.");
      return;
    }

    if (mBoundTfo->mStatus == TransformFeedbackJS::Status::Active) {
      ErrorInvalidOperation("Current bound object is active and unpaused.");
      return;
    }

    mBoundTfo->OnBindChange(-1);
    mBoundTfo = obj;
    mBoundTfo->mTarget = LOCAL_GL_TRANSFORM_FEEDBACK;
    mBoundTfo->OnBindChange(+1);

    context->BindTransformFeedback(obj->mInner);
  }

  void BeginTransformFeedback(const GLenum primMode) {
    const auto context = GetContext();
    if (!context) return;

    switch (primMode) {
      case LOCAL_GL_POINTS:
      case LOCAL_GL_LINES:
      case LOCAL_GL_TRIANGLES:
        break;
      default:
        ErrorInvalidEnum("Bad primMode.");
        return;
    }

    if (mBoundTfo->mStatus != TransformFeedbackJS::Status::Inactive) {
      ErrorInvalidOperation("Already active.");
      return;
    }

    context->BeginTransformFeedback(mBoundTfo->mInner);
  }

  void EndTransformFeedback();
  void PauseTransformFeedback();
  void ResumeTransformFeedback();

  // ----------------------------------

  NS_IMETHOD Reset() override {
    /* (InitializeWithSurface) */
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  virtual UniquePtr<uint8_t[]> GetImageBuffer(int32_t* out_format) override;
  NS_IMETHOD GetInputStream(const char* mimeType,
                            const char16_t* encoderOptions,
                            nsIInputStream** out_stream) override;

  virtual already_AddRefed<mozilla::gfx::SourceSurface> GetSurfaceSnapshot(
      gfxAlphaType* out_alphaType) override;

  virtual void SetOpaqueValueFromOpaqueAttr(bool) override{};
  bool GetIsOpaque() override { return !mOptions.alpha; }
  NS_IMETHOD SetContextOptions(JSContext* cx, JS::Handle<JS::Value> options,
                               ErrorResult& aRvForDictionaryInit) override;

  NS_IMETHOD SetIsIPC(bool) override { return NS_ERROR_NOT_IMPLEMENTED; }

  /**
   * An abstract base class to be implemented by callers wanting to be notified
   * that a refresh has occurred. Callers must ensure an observer is removed
   * before it is destroyed.
   */
  virtual void DidRefresh() override;

  NS_IMETHOD Redraw(const gfxRect&) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  // -

  void SynthesizeGLError(GLenum err) const;
  void SynthesizeGLError(GLenum err, const char* fmt, ...) const
      MOZ_FORMAT_PRINTF(3, 4);

  void ErrorInvalidEnum(const char* fmt = 0, ...) const MOZ_FORMAT_PRINTF(2, 3);
  void ErrorInvalidOperation(const char* fmt = 0, ...) const
      MOZ_FORMAT_PRINTF(2, 3);
  void ErrorInvalidValue(const char* fmt = 0, ...) const
      MOZ_FORMAT_PRINTF(2, 3);
  void ErrorInvalidFramebufferOperation(const char* fmt = 0, ...) const
      MOZ_FORMAT_PRINTF(2, 3);
  void ErrorInvalidEnumInfo(const char* info, GLenum enumValue) const;
  void ErrorOutOfMemory(const char* fmt = 0, ...) const MOZ_FORMAT_PRINTF(2, 3);
  void ErrorImplementationBug(const char* fmt = 0, ...) const
      MOZ_FORMAT_PRINTF(2, 3);

  void ErrorInvalidEnumArg(const char* argName, GLenum val) const;

  static const char* ErrorName(GLenum error);

  /**
   * Return displayable name for GLenum.
   * This version is like gl::GLenumToStr but with out the GL_ prefix to
   * keep consistency with how errors are reported from WebGL.
   * Returns hex formatted version of glenum if glenum is unknown.
   */
  static void EnumName(GLenum val, nsCString* out_name);

  void DummyReadFramebufferOperation();

  WebGLTexture* ActiveBoundTextureForTarget(const TexTarget texTarget) const {
    switch (texTarget.get()) {
      case LOCAL_GL_TEXTURE_2D:
        return mBound2DTextures[mActiveTexture];
      case LOCAL_GL_TEXTURE_CUBE_MAP:
        return mBoundCubeMapTextures[mActiveTexture];
      case LOCAL_GL_TEXTURE_3D:
        return mBound3DTextures[mActiveTexture];
      case LOCAL_GL_TEXTURE_2D_ARRAY:
        return mBound2DArrayTextures[mActiveTexture];
      default:
        MOZ_CRASH("GFX: bad target");
    }
  }

  /* Use this function when you have the texture image target, for example:
   * GL_TEXTURE_2D, GL_TEXTURE_CUBE_MAP_[POSITIVE|NEGATIVE]_[X|Y|Z], and
   * not the actual texture binding target: GL_TEXTURE_2D or
   * GL_TEXTURE_CUBE_MAP.
   */
  WebGLTexture* ActiveBoundTextureForTexImageTarget(
      const TexImageTarget texImgTarget) const {
    const TexTarget texTarget = TexImageTargetToTexTarget(texImgTarget);
    return ActiveBoundTextureForTarget(texTarget);
  }

  already_AddRefed<Layer> GetCanvasLayer(nsDisplayListBuilder* builder,
                                         Layer* oldLayer,
                                         LayerManager* manager) override;

  bool UpdateWebRenderCanvasData(nsDisplayListBuilder* aBuilder,
                                 WebRenderCanvasData* aCanvasData) override;

  bool InitializeCanvasRenderer(nsDisplayListBuilder* aBuilder,
                                CanvasRenderer* aRenderer) override;

  // Note that 'clean' here refers to its invalidation state, not the
  // contents of the buffer.
  void MarkContextClean() override { mInvalidated = false; }

  void MarkContextCleanForFrameCapture() override {
    mCapturedFrameInvalidated = false;
  }

  bool IsContextCleanForFrameCapture() override {
    return !mCapturedFrameInvalidated;
  }

  gl::GLContext* GL() const { return gl; }

  bool IsPremultAlpha() const { return mOptions.premultipliedAlpha; }

  bool IsPreservingDrawingBuffer() const {
    return mOptions.preserveDrawingBuffer;
  }

  bool PresentScreenBuffer(gl::GLScreenBuffer* const screen = nullptr);

  // Prepare the context for capture before compositing
  void BeginComposition(gl::GLScreenBuffer* const screen = nullptr);
  // Clean up the context after captured for compositing
  void EndComposition();

  // a number that increments every time we have an event that causes
  // all context resources to be lost.
  uint32_t Generation() const { return mGeneration.value(); }

  void RunContextLossTimer();
  void UpdateContextLossStatus();
  void EnqueueUpdateContextLossStatus();

  bool TryToRestoreContext();

  void AssertCachedBindings() const;
  void AssertCachedGlobalState() const;

  dom::HTMLCanvasElement* GetCanvas() const { return mCanvasElement; }
  nsIDocument* GetOwnerDoc() const;

  // WebIDL WebGLRenderingContext API
  void Commit();
  void GetCanvas(
      dom::Nullable<dom::OwningHTMLCanvasElementOrOffscreenCanvas>& retval);

 private:
  gfx::IntSize DrawingBufferSize();

 public:
  GLsizei DrawingBufferWidth() {
    const FuncScope funcScope(*this, "drawingBufferWidth");
    return DrawingBufferSize().width;
  }
  GLsizei DrawingBufferHeight() {
    const FuncScope funcScope(*this, "drawingBufferHeight");
    return DrawingBufferSize().height;
  }

  layers::LayersBackend GetCompositorBackendType() const;

  void GetContextAttributes(dom::Nullable<dom::WebGLContextAttributes>& retval);

  // This is the entrypoint. Don't test against it directly.
  bool IsContextLost() const;

  void GetSupportedExtensions(dom::Nullable<nsTArray<nsString>>& retval,
                              dom::CallerType callerType);
  void GetExtension(JSContext* cx, const nsAString& name,
                    JS::MutableHandle<JSObject*> retval,
                    dom::CallerType callerType, ErrorResult& rv);

  // -

private:
  bool IsObj(const ObjectJS* const obj) const {
    return obj && obj->Context() == this;
  }

public:
  auto_AddRefed<BufferJS> CreateBuffer() {
    if (!GetContext()) return nullptr;
    return new BufferJS(this);
  }

  auto_AddRefed<FramebufferJS> CreateFramebuffer() {
    if (!GetContext()) return nullptr;
    return new FramebufferJS(this);
  }

  auto_AddRefed<ProgramJS> CreateProgram() {
    const auto context = GetContext();
    if (!context) return nullptr;

    const auto aobj = context->CreateProgram();
    return new ProgramJS(this, aobj);
  }

  auto_AddRefed<QueryJS> CreateQuery() {
    const auto inner = GetInner();
    if (!inner) return nullptr;

    const auto aobj = inner->CreateQuery();
    return new QueryJS(this, aobj);
  }

  auto_AddRefed<RenderbufferJS> CreateRenderbuffer() {
    if (!GetContext()) return nullptr;
    return new RenderbufferJS(this);
  }

  auto_AddRefed<ShaderJS> CreateShader(const GLenum type) {
    const auto context = GetContext();
    if (!context) return nullptr;

    const auto aobj = context->CreateProgram(type);
    return new ProgramJS(this, aobj);
  }

  auto_AddRefed<SamplerJS> CreateSampler() {
    if (!GetContext()) return nullptr;
    return new SamplerJS(this);
  }

  auto_AddRefed<TextureJS> CreateTexture() {
    if (!GetContext()) return nullptr;
    return new TextureJS(this);
  }

  auto_AddRefed<TransformFeedbackJS> CreateTransformFeedback() {
    if (!GetContext()) return nullptr;
    return new TransformFeedbackJS(this);
  }

  auto_AddRefed<VertexArrayJS> CreateVertexArray() {
    if (!GetContext()) return nullptr;
    return new VertexArrayJS(this);
  }

  // -

private:
  void DeleteObj(ObjectJS* const obj) {
    if (!IsObj(obj)) return;
    obj->MarkDeletedOrLost();
  }
public:
  void DeleteBuffer(BufferJS* const obj) { DeleteObj(obj); }
  void DeleteFramebuffer(FramebufferJS* const obj) { DeleteObj(obj); }
  void DeleteProgram(ProgramJS* const obj) { DeleteObj(obj); }
  void DeleteQuery(QueryJS* const obj) { DeleteObj(obj); }
  void DeleteRenderbuffer(RenderbufferJS* const obj) { DeleteObj(obj); }
  void DeleteSampler(SamplerJS* const obj) { DeleteObj(obj); }
  void DeleteShader(ShaderJS* const obj) { DeleteObj(obj); }
  void DeleteSync(SyncJS* const obj) { DeleteObj(obj); }
  void DeleteTexture(TextureJS* const obj) { DeleteObj(obj); }
  void DeleteTransformFeedback(TransformFeedbackJS* const obj) { DeleteObj(obj); }
  void DeleteVertexArray(VertexArrayJS* const obj) { DeleteObj(obj); }

  // -

  bool IsBuffer(const BufferJS* const obj) const {
    return IsObj(obj) && obj->Target();
  }
  bool IsFramebuffer(const FramebufferJS* const obj) const {
   return IsObj(obj) && obj->Target();
  }
  bool IsProgram(const ProgramJS* const obj) const {
   return IsObj(obj);
  }
  bool IsQuery(const QueryJS* const obj) const {
   return IsObj(obj);
  }
  bool IsRenderbuffer(const RenderbufferJS* const obj) const {
   return IsObj(obj) && obj->Target();
  }
  bool IsSsampler(const SamplerJS* const obj) const {
   return IsObj(obj) && obj->Target();
  }
  bool IsShader(const ShaderJS* const obj) const {
   return IsObj(obj);
  }
  bool IsSync(const SyncJS* const obj) const {
   return IsObj(obj);
  }
  bool IsTexture(const TextureJS* obj) const {
   return IsObj(obj) && obj->Target();
  }
  bool IsTransformFeedback(const TransformFeedbackJS* obj) const {
   return IsObj(obj) && obj->Target();
  }
  bool IsVertexArray(const VertexArrayJS* const obj) const {
   return IsObj(obj) && obj->Target();
  }

  // -------------------------------
  // Narrow the API:

  void BlendEquation(GLenum mode) {
    BlendEquationSeparate(mode, mode);
  }
  void BlendFunc(GLenum sfactor, GLenum dfactor) {
    BlendFuncSeparate(sfactor, dfactor, sfactor, dfactor);
  }
  void StencilFunc(GLenum func, GLint ref, GLuint mask) {
    StencilFuncSeparate(LOCAL_GL_FRONT_AND_BACK, func, ref, mask);
  }
  void StencilMask(GLuint mask) {
    StencilMaskSeparate(LOCAL_GL_FRONT_AND_BACK, mask);
  }
  void StencilOp(GLenum sfail, GLenum dpfail, GLenum dppass) {
    StencilOpSeparate(LOCAL_GL_FRONT_AND_BACK, sfail, dpfail, dppass);
  }

  // ----------------------------
  // Forward directly to mInner:

  void BlendColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) {
    mInner->BlendColor(r, g, b, a);
  }
  void BlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha) {
    mInner->BlendEquationSeparate(modeRGB, modeAlpha);
  }
  void BlendFuncSeparate(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha,
                         GLenum dstAlpha) {
    mInner->BlendFuncSeparate(srcRGB, dstRGB, srcAlpha, dstAlpha);
  }
  void Clear(GLbitfield mask) { mInner->Clear(mDrawFramebuffer.get(), mask); }
  void ClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) {
    mInner->ClearColor(r, g, b, a);
  }
  void ClearDepth(GLclampf v) { mInner->ClearDepth(v); }
  void ClearStencil(GLint v) { mInner->ClearStencil(v); }
  void ColorMask(WebGLboolean r, WebGLboolean g, WebGLboolean b,
                 WebGLboolean a) { mInner->ColorMask(r, g, b, a); }
  void CullFace(GLenum face) { mInner->CullFace(face); }
  void DepthFunc(GLenum func) { mInner->DepthFunc(func); }
  void DepthMask(WebGLboolean b) { mInner->DepthMask(b); }
  void DepthRange(GLclampf zNear, GLclampf zFar) { mInner->DepthRange(zNear, zFar); }
  void DisableVertexAttribArray(GLuint index) {
    mInner->SetEnabledVertexAttribArray(index, false);
  }
  void EnableVertexAttribArray(GLuint index) {
    mInner->SetEnabledVertexAttribArray(index, true);
  }
  void Flush() { mInner->Flush(); }
  void Finish() { mInner->Finish(); }
  void FrontFace(GLenum mode) { mInner->FrontFace(mode); }
  void Hint(GLenum target, GLenum mode) { mInner->Hint(target, mode); }
  void LineWidth(GLfloat width) { mInner->LineWidth(width); }
  void LoseContext() {
    mInner->LoseContext();
  }
  void PolygonOffset(GLfloat factor, GLfloat units) { mInner->PolygonOffset(factor, units); }
  void RestoreContext() {
    mInner->RestoreContext();
  }
  void SampleCoverage(GLclampf value, WebGLboolean invert) { mInner->SampleCoverage(value, invert); }
  void Scissor(GLint x, GLint y, GLsizei width, GLsizei height) {
    mInner->Scissor(x, y, width, height);
  }
  void StencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask) {
    mInner->StencilFuncSeparate(face, func, ref, mask);
  }
  void StencilMaskSeparate(GLenum face, GLuint mask) {
    mInner->StencilMaskSeparate(face, mask);
  }
  void StencilOpSeparate(GLenum face, GLenum sfail, GLenum dpfail,
                         GLenum dppass) {
    mInner->StencilOpSeparate(face, sfail, dpfail, dppass);
  }
  void VertexAttrib4f(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w) {
    const float data[4] = {x, y, z, w};
    mInner->VertexAttrib4v(index, webgl::AttribBaseType::Float, (const uint8_t*)data);
  }
  void VertexAttribI4i(GLuint index, GLint x, GLint y, GLint z, GLint w) {
    const int32_t data[4] = {x, y, z, w};
    mInner->VertexAttrib4v(index, webgl::AttribBaseType::Int, (const uint8_t*)data);
  }
  void VertexAttribI4ui(GLuint index, GLuint x, GLuint y, GLuint z, GLuint w) {
    const uint32_t data[4] = {x, y, z, w};
    mInner->VertexAttrib4v(index, webgl::AttribBaseType::Uint, (const uint8_t*)data);
  }
  void Viewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    mInner->Viewport(x, y, width, height);
  }

  // ---------------------------------
  // Nontrivial entrypoints:

  void BindAttribLocation(WebGLProgram& prog, GLuint location,
                          const nsAString& name);

  void BindFramebuffer(const GLenum target, FramebufferJS* const obj) {
    const auto context = GetContext();
    if (!context) return;

    if (obj && !IsFramebuffer(obj)) {
      ErrorInvalidOperation("bindFramebuffer: Invalid object.");
      return;
    }
    switch (target) {
    case LOCAL_GL_FRAMEBUFFER:
      mBoundDrawFbo = obj;
      mBoundReadFbo = obj;
      return;
    case LOCAL_GL_DRAW_FRAMEBUFFER:
      if (!IsWebGL2()) break;
      mBoundDrawFbo = obj;
      return;
    case LOCAL_GL_READ_FRAMEBUFFER:
      if (!IsWebGL2()) break;
      mBoundReadFbo = obj;
      return;
    }
    ErrorInvalidEnum("bindFramebuffer: Bad target.");
    return;
  }

  void BindRenderbuffer(const GLenum target, RenderbufferJS* const obj) {
    const auto context = GetContext();
    if (!context) return;

    if (target != LOCAL_GL_RENDERBUFFER) {
      ErrorInvalidEnum("bindRenderbuffer: Bad `target`.");
      return;
    }
    if (obj && !IsRenderbuffer(obj)) {
      ErrorInvalidOperation("bindRenderbuffer: Invalid object.");
      return;
    }
    mBoundRenderbuffer = obj;
  }

  Maybe<FramebufferJS*> GetFbByTarget(const char* const funcName, const GLenum target) const {
    switch (target) {
    case LOCAL_GL_FRAMEBUFFER:
      return Some(mBoundDrawFbo.get());
    case LOCAL_GL_DRAW_FRAMEBUFFER:
      if (!IsWebGL2()) break;
      return Some(mBoundDrawFbo.get());
    case LOCAL_GL_READ_FRAMEBUFFER:
      if (!IsWebGL2()) break;
      return Some(mBoundReadFbo.get());
    }
    ErrorInvalidEnum("%s: Bad target.", funcName);
    return {};
  }

  GLenum CheckFramebufferStatus(GLenum target) {
    const auto context = GetContext();
    if (!context) return 0;
    const auto maybeFb = GetFbByTarget("checkFramebufferStatus", target);
    if (!maybeFb) return 0;
    const auto& fb = maybeFb.value();
    if (!fb) return LOCAL_GL_FRAMEBUFFER_COMPLETE;

    return context->CheckFramebufferStatus(*fb);
  }

  void CompileShader(ShaderJS& shader) {
    const auto context = GetContext();
    if (!context) return;

    context->CompileShader(shader->mInner, shader->mSourceText);
  }


  void AttachShader(ProgramJS& prog, ShaderJS& shader) {
    if (!GetContext()) return;
    if (!IsProgram(&prog) || !IsShader(&shader)) return;
    auto& slot = prog.mShaders[shader.mType];
    if (slot) {
      ErrorInvalidOperation("attachShader: Shader of that type already attached.");
      return;
    }
    slot = shader;
  }

  void DetachShader(ProgramJS& prog, const ShaderJS& shader) {
    if (!GetContext()) return;
    if (!IsProgram(&prog) || !IsShader(&shader)) return;
  }

  void GetAttachedShaders(const ProgramJS& prog,
                          dom::Nullable<nsTArray<RefPtr<ShaderJS>>>& retval);





  void DrawBuffers(const dom::Sequence<GLenum>& buffers);

  void FramebufferRenderbuffer(GLenum target, GLenum attachment,
                               GLenum rbTarget, WebGLRenderbuffer* rb);
  void FramebufferTexture2D(GLenum target, GLenum attachment,
                            GLenum texImageTarget, WebGLTexture* tex,
                            GLint level);

  already_AddRefed<WebGLActiveInfo> GetActiveAttrib(const WebGLProgram& prog,
                                                    GLuint index);
  already_AddRefed<WebGLActiveInfo> GetActiveUniform(const WebGLProgram& prog,
                                                     GLuint index);


  GLint GetAttribLocation(const WebGLProgram& prog, const nsAString& name);
  JS::Value GetBufferParameter(GLenum target, GLenum pname);

  void GetBufferParameter(JSContext*, GLenum target, GLenum pname,
                          JS::MutableHandle<JS::Value> retval) {
    retval.set(GetBufferParameter(target, pname));
  }

  GLenum GetError();
  virtual JS::Value GetFramebufferAttachmentParameter(JSContext* cx,
                                                      GLenum target,
                                                      GLenum attachment,
                                                      GLenum pname,
                                                      ErrorResult& rv);

  void GetFramebufferAttachmentParameter(JSContext* cx, GLenum target,
                                         GLenum attachment, GLenum pname,
                                         JS::MutableHandle<JS::Value> retval,
                                         ErrorResult& rv) {
    retval.set(
        GetFramebufferAttachmentParameter(cx, target, attachment, pname, rv));
  }

  JS::Value GetProgramParameter(const WebGLProgram& prog, GLenum pname);

  void GetProgramParameter(JSContext*, const WebGLProgram& prog, GLenum pname,
                           JS::MutableHandle<JS::Value> retval) {
    retval.set(GetProgramParameter(prog, pname));
  }

  void GetProgramInfoLog(const WebGLProgram& prog, nsAString& retval);
  JS::Value GetRenderbufferParameter(GLenum target, GLenum pname);

  void GetRenderbufferParameter(JSContext*, GLenum target, GLenum pname,
                                JS::MutableHandle<JS::Value> retval) {
    retval.set(GetRenderbufferParameter(target, pname));
  }

  JS::Value GetShaderParameter(const WebGLShader& shader, GLenum pname);

  void GetShaderParameter(JSContext*, const WebGLShader& shader, GLenum pname,
                          JS::MutableHandle<JS::Value> retval) {
    retval.set(GetShaderParameter(shader, pname));
  }

  already_AddRefed<WebGLShaderPrecisionFormat> GetShaderPrecisionFormat(
      GLenum shadertype, GLenum precisiontype);

  void GetShaderInfoLog(const WebGLShader& shader, nsACString& retval);
  void GetShaderInfoLog(const WebGLShader& shader, nsAString& retval);
  void GetShaderSource(const WebGLShader& shader, nsAString& retval);

  JS::Value GetUniform(JSContext* cx, const WebGLProgram& prog,
                       const WebGLUniformLocation& loc);

  void GetUniform(JSContext* cx, const WebGLProgram& prog,
                  const WebGLUniformLocation& loc,
                  JS::MutableHandle<JS::Value> retval) {
    retval.set(GetUniform(cx, prog, loc));
  }

  already_AddRefed<WebGLUniformLocation> GetUniformLocation(
      const WebGLProgram& prog, const nsAString& name);

  void LinkProgram(WebGLProgram& prog);
  void PixelStorei(GLenum pname, GLint param);

  already_AddRefed<layers::SharedSurfaceTextureClient> GetVRFrame();
  void EnsureVRReady();

  ////

  webgl::PackingInfo ValidImplementationColorReadPI(
      const webgl::FormatUsageInfo* usage) const;

 protected:
  bool ReadPixels_SharedPrecheck(dom::CallerType aCallerType,
                                 ErrorResult& out_error);
  void ReadPixelsImpl(GLint x, GLint y, GLsizei width, GLsizei height,
                      GLenum format, GLenum type, void* data, uint32_t dataLen);
  bool DoReadPixelsAndConvert(const webgl::FormatInfo* srcFormat, GLint x,
                              GLint y, GLsizei width, GLsizei height,
                              GLenum format, GLenum destType, void* dest,
                              uint32_t dataLen, uint32_t rowStride);

 public:
  void ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                  GLenum format, GLenum type,
                  const dom::Nullable<dom::ArrayBufferView>& maybeView,
                  dom::CallerType aCallerType, ErrorResult& rv) {
    const FuncScope funcScope(*this, "readPixels");
    if (!ValidateNonNull("pixels", maybeView)) return;
    ReadPixels(x, y, width, height, format, type, maybeView.Value(), 0,
               aCallerType, rv);
  }

  void ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                  GLenum format, GLenum type, WebGLsizeiptr offset,
                  dom::CallerType, ErrorResult& out_error);

  void ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                  GLenum format, GLenum type,
                  const dom::ArrayBufferView& dstData, GLuint dstOffset,
                  dom::CallerType, ErrorResult& out_error);

  ////

  void RenderbufferStorage(GLenum target, GLenum internalFormat, GLsizei width,
                           GLsizei height) {
    const FuncScope funcScope(*this, "renderbufferStorage");
    RenderbufferStorage_base(target, 0, internalFormat, width, height);
  }

 protected:
  void RenderbufferStorage_base(GLenum target, GLsizei samples,
                                GLenum internalformat, GLsizei width,
                                GLsizei height);

 public:
  void ShaderSource(WebGLShader& shader, const nsAString& source);


  //////

  void Uniform1f(WebGLUniformLocation* loc, GLfloat x);
  void Uniform2f(WebGLUniformLocation* loc, GLfloat x, GLfloat y);
  void Uniform3f(WebGLUniformLocation* loc, GLfloat x, GLfloat y, GLfloat z);
  void Uniform4f(WebGLUniformLocation* loc, GLfloat x, GLfloat y, GLfloat z,
                 GLfloat w);

  void Uniform1i(WebGLUniformLocation* loc, GLint x);
  void Uniform2i(WebGLUniformLocation* loc, GLint x, GLint y);
  void Uniform3i(WebGLUniformLocation* loc, GLint x, GLint y, GLint z);
  void Uniform4i(WebGLUniformLocation* loc, GLint x, GLint y, GLint z, GLint w);

  void Uniform1ui(WebGLUniformLocation* loc, GLuint v0);
  void Uniform2ui(WebGLUniformLocation* loc, GLuint v0, GLuint v1);
  void Uniform3ui(WebGLUniformLocation* loc, GLuint v0, GLuint v1, GLuint v2);
  void Uniform4ui(WebGLUniformLocation* loc, GLuint v0, GLuint v1, GLuint v2,
                  GLuint v3);

  //////////////////////////

  typedef dom::Float32ArrayOrUnrestrictedFloatSequence Float32ListU;
  typedef dom::Int32ArrayOrLongSequence Int32ListU;
  typedef dom::Uint32ArrayOrUnsignedLongSequence Uint32ListU;

 protected:
  template <typename elemT, typename viewT>
  struct Arr {
    const size_t elemCount;
    const elemT* const elemBytes;

   private:
    static size_t ComputeAndReturnLength(const viewT& view) {
      view.ComputeLengthAndData();
      return view.LengthAllowShared();
    }

   public:
    explicit Arr(const viewT& view)
        : elemCount(ComputeAndReturnLength(view)),
          elemBytes(view.DataAllowShared()) {}

    explicit Arr(const dom::Sequence<elemT>& seq)
        : elemCount(seq.Length()), elemBytes(seq.Elements()) {}

    Arr(size_t _elemCount, const elemT* _elemBytes)
        : elemCount(_elemCount), elemBytes(_elemBytes) {}

    ////

    static Arr From(const Float32ListU& list) {
      if (list.IsFloat32Array()) return Arr(list.GetAsFloat32Array());

      return Arr(list.GetAsUnrestrictedFloatSequence());
    }

    static Arr From(const Int32ListU& list) {
      if (list.IsInt32Array()) return Arr(list.GetAsInt32Array());

      return Arr(list.GetAsLongSequence());
    }

    static Arr From(const Uint32ListU& list) {
      if (list.IsUint32Array()) return Arr(list.GetAsUint32Array());

      return Arr(list.GetAsUnsignedLongSequence());
    }
  };

  typedef Arr<GLfloat, dom::Float32Array> Float32Arr;
  typedef Arr<GLint, dom::Int32Array> Int32Arr;
  typedef Arr<GLuint, dom::Uint32Array> Uint32Arr;

  ////////////////

  void UniformNfv(const char* funcName, uint8_t N, WebGLUniformLocation* loc,
                  const Float32Arr& arr, GLuint elemOffset,
                  GLuint elemCountOverride);
  void UniformNiv(const char* funcName, uint8_t N, WebGLUniformLocation* loc,
                  const Int32Arr& arr, GLuint elemOffset,
                  GLuint elemCountOverride);
  void UniformNuiv(const char* funcName, uint8_t N, WebGLUniformLocation* loc,
                   const Uint32Arr& arr, GLuint elemOffset,
                   GLuint elemCountOverride);

  void UniformMatrixAxBfv(const char* funcName, uint8_t A, uint8_t B,
                          WebGLUniformLocation* loc, bool transpose,
                          const Float32Arr& arr, GLuint elemOffset,
                          GLuint elemCountOverride);

  ////////////////

 public:
#define FOO(N)                                                                \
  void Uniform##N##fv(WebGLUniformLocation* loc, const Float32ListU& list,    \
                      GLuint elemOffset = 0, GLuint elemCountOverride = 0) {  \
    UniformNfv("uniform" #N "fv", N, loc, Float32Arr::From(list), elemOffset, \
               elemCountOverride);                                            \
  }

  FOO(1)
  FOO(2)
  FOO(3)
  FOO(4)

#undef FOO

  //////

#define FOO(N)                                                               \
  void Uniform##N##iv(WebGLUniformLocation* loc, const Int32ListU& list,     \
                      GLuint elemOffset = 0, GLuint elemCountOverride = 0) { \
    UniformNiv("uniform" #N "iv", N, loc, Int32Arr::From(list), elemOffset,  \
               elemCountOverride);                                           \
  }

  FOO(1)
  FOO(2)
  FOO(3)
  FOO(4)

#undef FOO

  //////

#define FOO(N)                                                                 \
  void Uniform##N##uiv(WebGLUniformLocation* loc, const Uint32ListU& list,     \
                       GLuint elemOffset = 0, GLuint elemCountOverride = 0) {  \
    UniformNuiv("uniform" #N "uiv", N, loc, Uint32Arr::From(list), elemOffset, \
                elemCountOverride);                                            \
  }

  FOO(1)
  FOO(2)
  FOO(3)
  FOO(4)

#undef FOO

  //////

#define FOO(X, A, B)                                                           \
  void UniformMatrix##X##fv(WebGLUniformLocation* loc, bool transpose,         \
                            const Float32ListU& list, GLuint elemOffset = 0,   \
                            GLuint elemCountOverride = 0) {                    \
    UniformMatrixAxBfv("uniformMatrix" #X "fv", A, B, loc, transpose,          \
                       Float32Arr::From(list), elemOffset, elemCountOverride); \
  }

  FOO(2, 2, 2)
  FOO(2x3, 2, 3)
  FOO(2x4, 2, 4)

  FOO(3x2, 3, 2)
  FOO(3, 3, 3)
  FOO(3x4, 3, 4)

  FOO(4x2, 4, 2)
  FOO(4x3, 4, 3)
  FOO(4, 4, 4)

#undef FOO

  ////////////////////////////////////

  void UseProgram(WebGLProgram* prog);

  bool ValidateAttribArraySetter(uint32_t count, uint32_t arrayLength);
  bool ValidateUniformLocation(const WebGLUniformLocation* loc);
  bool ValidateUniformSetter(const WebGLUniformLocation* loc,
                             uint8_t setterElemSize,
                             webgl::AttribBaseType setterType);
  bool ValidateUniformArraySetter(const WebGLUniformLocation* loc,
                                  uint8_t setterElemSize,
                                  webgl::AttribBaseType setterType,
                                  uint32_t setterArraySize,
                                  uint32_t* out_numElementsToUpload);
  bool ValidateUniformMatrixArraySetter(const WebGLUniformLocation* loc,
                                        uint8_t setterCols, uint8_t setterRows,
                                        webgl::AttribBaseType setterType,
                                        uint32_t setterArraySize,
                                        bool setterTranspose,
                                        uint32_t* out_numElementsToUpload);
  void ValidateProgram(const WebGLProgram& prog);
  bool ValidateUniformLocation(const char* info, WebGLUniformLocation* loc);
  bool ValidateSamplerUniformSetter(const char* info, WebGLUniformLocation* loc,
                                    GLint value);
  // -----------------------------------------------------------------------------
  // WEBGL_lose_context
 public:

  // -----------------------------------------------------------------------------
  // Buffer Objects (WebGLContextBuffers.cpp)
  // -----------------------------------------------------------------------------
  // Queries (WebGL2ContextQueries.cpp)
 protected:
  WebGLRefPtr<WebGLQuery> mQuerySlot_SamplesPassed;
  WebGLRefPtr<WebGLQuery> mQuerySlot_TFPrimsWritten;
  WebGLRefPtr<WebGLQuery> mQuerySlot_TimeElapsed;

  WebGLRefPtr<WebGLQuery>* ValidateQuerySlotByTarget(GLenum target);

 public:
  already_AddRefed<WebGLQuery> CreateQuery();
  void DeleteQuery(WebGLQuery* query);
  bool IsQuery(const WebGLQuery* query);
  void BeginQuery(GLenum target, WebGLQuery& query);
  void EndQuery(GLenum target);
  void GetQuery(JSContext* cx, GLenum target, GLenum pname,
                JS::MutableHandleValue retval);
  void GetQueryParameter(JSContext* cx, const WebGLQuery& query, GLenum pname,
                         JS::MutableHandleValue retval);

  // -----------------------------------------------------------------------------
  // State and State Requests (WebGLContextState.cpp)
 private:
  void SetEnabled(const char* funcName, GLenum cap, bool enabled);

 public:
  void Disable(GLenum cap) { SetEnabled("disabled", cap, false); }
  void Enable(GLenum cap) { SetEnabled("enabled", cap, true); }
  bool GetStencilBits(GLint* const out_stencilBits) const;
  virtual JS::Value GetParameter(JSContext* cx, GLenum pname, ErrorResult& rv);

  void GetParameter(JSContext* cx, GLenum pname,
                    JS::MutableHandle<JS::Value> retval, ErrorResult& rv) {
    retval.set(GetParameter(cx, pname, rv));
  }

  bool IsEnabled(GLenum cap);

 private:
  // State tracking slots
  realGLboolean mDitherEnabled;
  realGLboolean mRasterizerDiscardEnabled;
  realGLboolean mScissorTestEnabled;
  realGLboolean mDepthTestEnabled = 0;
  realGLboolean mStencilTestEnabled;
  GLenum mGenerateMipmapHint = 0;

  bool ValidateCapabilityEnum(GLenum cap);
  realGLboolean* GetStateTrackingSlot(GLenum cap);

  // Allocation debugging variables
  mutable uint64_t mDataAllocGLCallCount;

  void OnDataAllocCall() const { mDataAllocGLCallCount++; }

  uint64_t GetNumGLDataAllocCalls() const { return mDataAllocGLCallCount; }

  void OnEndOfFrame() const;

  // -----------------------------------------------------------------------------
  // Texture funcions (WebGLContextTextures.cpp)
 public:
  void ActiveTexture(GLenum texUnit);
  void BindTexture(GLenum texTarget, WebGLTexture* tex);
  already_AddRefed<WebGLTexture> CreateTexture();
  void DeleteTexture(WebGLTexture* tex);
  void GenerateMipmap(GLenum texTarget);

  void GetTexParameter(JSContext*, GLenum texTarget, GLenum pname,
                       JS::MutableHandle<JS::Value> retval) {
    retval.set(GetTexParameter(texTarget, pname));
  }

  void TexParameterf(GLenum texTarget, GLenum pname, GLfloat param) {
    TexParameter_base(texTarget, pname, FloatOrInt(param));
  }

  void TexParameteri(GLenum texTarget, GLenum pname, GLint param) {
    TexParameter_base(texTarget, pname, FloatOrInt(param));
  }

 protected:
  JS::Value GetTexParameter(GLenum texTarget, GLenum pname);
  void TexParameter_base(GLenum texTarget, GLenum pname,
                         const FloatOrInt& param);

  virtual bool IsTexParamValid(GLenum pname) const;

  ////////////////////////////////////

 public:
  void CompressedTexImage2D(GLenum target, GLint level, GLenum internalFormat,
                            GLsizei width, GLsizei height, GLint border,
                            GLsizei imageSize, WebGLsizeiptr offset) {
    const FuncScope funcScope(*this, "compressedTexImage2D");
    const uint8_t funcDims = 2;
    const GLsizei depth = 1;
    const TexImageSourceAdapter src(&offset, 0, 0);
    CompressedTexImage(funcDims, target, level, internalFormat, width, height,
                       depth, border, src, Some(imageSize));
  }

  template <typename T>
  void CompressedTexImage2D(GLenum target, GLint level, GLenum internalFormat,
                            GLsizei width, GLsizei height, GLint border,
                            const T& anySrc, GLuint viewElemOffset = 0,
                            GLuint viewElemLengthOverride = 0) {
    const FuncScope funcScope(*this, "compressedTexImage2D");
    const uint8_t funcDims = 2;
    const GLsizei depth = 1;
    const TexImageSourceAdapter src(&anySrc, viewElemOffset,
                                    viewElemLengthOverride);
    CompressedTexImage(funcDims, target, level, internalFormat, width, height,
                       depth, border, src, Nothing());
  }

  void CompressedTexSubImage2D(GLenum target, GLint level, GLint xOffset,
                               GLint yOffset, GLsizei width, GLsizei height,
                               GLenum unpackFormat, GLsizei imageSize,
                               WebGLsizeiptr offset) {
    const FuncScope funcScope(*this, "compressedTexSubImage2D");
    const uint8_t funcDims = 2;
    const GLint zOffset = 0;
    const GLsizei depth = 1;
    const TexImageSourceAdapter src(&offset, 0, 0);
    CompressedTexSubImage(funcDims, target, level, xOffset, yOffset, zOffset,
                          width, height, depth, unpackFormat, src,
                          Some(imageSize));
  }

  template <typename T>
  void CompressedTexSubImage2D(GLenum target, GLint level, GLint xOffset,
                               GLint yOffset, GLsizei width, GLsizei height,
                               GLenum unpackFormat, const T& anySrc,
                               GLuint viewElemOffset = 0,
                               GLuint viewElemLengthOverride = 0) {
    const FuncScope funcScope(*this, "compressedTexSubImage2D");
    const uint8_t funcDims = 2;
    const GLint zOffset = 0;
    const GLsizei depth = 1;
    const TexImageSourceAdapter src(&anySrc, viewElemOffset,
                                    viewElemLengthOverride);
    CompressedTexSubImage(funcDims, target, level, xOffset, yOffset, zOffset,
                          width, height, depth, unpackFormat, src, Nothing());
  }

 protected:
  void CompressedTexImage(uint8_t funcDims, GLenum target, GLint level,
                          GLenum internalFormat, GLsizei width, GLsizei height,
                          GLsizei depth, GLint border,
                          const TexImageSource& src,
                          const Maybe<GLsizei>& expectedImageSize);
  void CompressedTexSubImage(uint8_t funcDims, GLenum target, GLint level,
                             GLint xOffset, GLint yOffset, GLint zOffset,
                             GLsizei width, GLsizei height, GLsizei depth,
                             GLenum unpackFormat, const TexImageSource& src,
                             const Maybe<GLsizei>& expectedImageSize);

  ////////////////////////////////////

 public:
  void CopyTexImage2D(GLenum target, GLint level, GLenum internalFormat,
                      GLint x, GLint y, GLsizei width, GLsizei height,
                      GLint border);

  void CopyTexSubImage2D(GLenum target, GLint level, GLint xOffset,
                         GLint yOffset, GLint x, GLint y, GLsizei width,
                         GLsizei height) {
    const FuncScope funcScope(*this, "copyTexSubImage2D");
    const uint8_t funcDims = 2;
    const GLint zOffset = 0;
    CopyTexSubImage(funcDims, target, level, xOffset, yOffset, zOffset, x, y,
                    width, height);
  }

 protected:
  void CopyTexSubImage(uint8_t funcDims, GLenum target, GLint level,
                       GLint xOffset, GLint yOffset, GLint zOffset, GLint x,
                       GLint y, GLsizei width, GLsizei height);

  ////////////////////////////////////
  // TexImage

  // Implicit width/height uploads

 public:
  template <typename T>
  void TexImage2D(GLenum target, GLint level, GLenum internalFormat,
                  GLenum unpackFormat, GLenum unpackType, const T& src,
                  ErrorResult& out_error) {
    GLsizei width = 0;
    GLsizei height = 0;
    GLint border = 0;
    TexImage2D(target, level, internalFormat, width, height, border,
               unpackFormat, unpackType, src, out_error);
  }

  template <typename T>
  void TexSubImage2D(GLenum target, GLint level, GLint xOffset, GLint yOffset,
                     GLenum unpackFormat, GLenum unpackType, const T& src,
                     ErrorResult& out_error) {
    GLsizei width = 0;
    GLsizei height = 0;
    TexSubImage2D(target, level, xOffset, yOffset, width, height, unpackFormat,
                  unpackType, src, out_error);
  }

  ////

  template <typename T>
  void TexImage2D(GLenum target, GLint level, GLenum internalFormat,
                  GLsizei width, GLsizei height, GLint border,
                  GLenum unpackFormat, GLenum unpackType, const T& anySrc,
                  ErrorResult& out_error) {
    const TexImageSourceAdapter src(&anySrc, &out_error);
    TexImage2D(target, level, internalFormat, width, height, border,
               unpackFormat, unpackType, src);
  }

  void TexImage2D(GLenum target, GLint level, GLenum internalFormat,
                  GLsizei width, GLsizei height, GLint border,
                  GLenum unpackFormat, GLenum unpackType,
                  const dom::ArrayBufferView& view, GLuint viewElemOffset,
                  ErrorResult&) {
    const TexImageSourceAdapter src(&view, viewElemOffset);
    TexImage2D(target, level, internalFormat, width, height, border,
               unpackFormat, unpackType, src);
  }

 protected:
  void TexImage2D(GLenum target, GLint level, GLenum internalFormat,
                  GLsizei width, GLsizei height, GLint border,
                  GLenum unpackFormat, GLenum unpackType,
                  const TexImageSource& src) {
    const FuncScope funcScope(*this, "texImage2D");
    const uint8_t funcDims = 2;
    const GLsizei depth = 1;
    TexImage(funcDims, target, level, internalFormat, width, height, depth,
             border, unpackFormat, unpackType, src);
  }

  void TexImage(uint8_t funcDims, GLenum target, GLint level,
                GLenum internalFormat, GLsizei width, GLsizei height,
                GLsizei depth, GLint border, GLenum unpackFormat,
                GLenum unpackType, const TexImageSource& src);

  ////

 public:
  template <typename T>
  void TexSubImage2D(GLenum target, GLint level, GLint xOffset, GLint yOffset,
                     GLsizei width, GLsizei height, GLenum unpackFormat,
                     GLenum unpackType, const T& anySrc,
                     ErrorResult& out_error) {
    const TexImageSourceAdapter src(&anySrc, &out_error);
    TexSubImage2D(target, level, xOffset, yOffset, width, height, unpackFormat,
                  unpackType, src);
  }

  void TexSubImage2D(GLenum target, GLint level, GLint xOffset, GLint yOffset,
                     GLsizei width, GLsizei height, GLenum unpackFormat,
                     GLenum unpackType, const dom::ArrayBufferView& view,
                     GLuint viewElemOffset, ErrorResult&) {
    const TexImageSourceAdapter src(&view, viewElemOffset);
    TexSubImage2D(target, level, xOffset, yOffset, width, height, unpackFormat,
                  unpackType, src);
  }

 protected:
  void TexSubImage2D(GLenum target, GLint level, GLint xOffset, GLint yOffset,
                     GLsizei width, GLsizei height, GLenum unpackFormat,
                     GLenum unpackType, const TexImageSource& src) {
    const FuncScope funcScope(*this, "texSubImage2D");
    const uint8_t funcDims = 2;
    const GLint zOffset = 0;
    const GLsizei depth = 1;
    TexSubImage(funcDims, target, level, xOffset, yOffset, zOffset, width,
                height, depth, unpackFormat, unpackType, src);
  }

  void TexSubImage(uint8_t funcDims, GLenum target, GLint level, GLint xOffset,
                   GLint yOffset, GLint zOffset, GLsizei width, GLsizei height,
                   GLsizei depth, GLenum unpackFormat, GLenum unpackType,
                   const TexImageSource& src);

  ////////////////////////////////////
  // WebGLTextureUpload.cpp
 public:
  UniquePtr<webgl::TexUnpackBlob> From(TexImageTarget target, GLsizei rawWidth,
                                       GLsizei rawHeight, GLsizei rawDepth,
                                       GLint border, const TexImageSource& src,
                                       dom::Uint8ClampedArray* const scopedArr);

 protected:
  bool ValidateTexImageSpecification(uint8_t funcDims, GLenum texImageTarget,
                                     GLint level, GLsizei width, GLsizei height,
                                     GLsizei depth, GLint border,
                                     TexImageTarget* const out_target,
                                     WebGLTexture** const out_texture,
                                     webgl::ImageInfo** const out_imageInfo);
  bool ValidateTexImageSelection(uint8_t funcDims, GLenum texImageTarget,
                                 GLint level, GLint xOffset, GLint yOffset,
                                 GLint zOffset, GLsizei width, GLsizei height,
                                 GLsizei depth,
                                 TexImageTarget* const out_target,
                                 WebGLTexture** const out_texture,
                                 webgl::ImageInfo** const out_imageInfo);
  bool ValidateUnpackInfo(bool usePBOs, GLenum format, GLenum type,
                          webgl::PackingInfo* const out);

  UniquePtr<webgl::TexUnpackBlob> FromDomElem(TexImageTarget target,
                                              uint32_t width, uint32_t height,
                                              uint32_t depth,
                                              const dom::Element& elem,
                                              ErrorResult* const out_error);

  UniquePtr<webgl::TexUnpackBytes> FromCompressed(
      TexImageTarget target, GLsizei rawWidth, GLsizei rawHeight,
      GLsizei rawDepth, GLint border, const TexImageSource& src,
      const Maybe<GLsizei>& expectedImageSize);

  // -----------------------------------------------------------------------------
  // Vertices Feature (WebGLContextVertices.cpp)
  GLenum mPrimRestartTypeBytes = 0;

 public:
  void DrawArrays(GLenum mode, GLint first, GLsizei count) {
    const FuncScope funcScope(*this, "drawArrays");
    DrawArraysInstanced(mode, first, count, 1);
  }

  void DrawElements(GLenum mode, GLsizei count, GLenum type,
                    WebGLintptr byteOffset) {
    const FuncScope funcScope(*this, "drawElements");
    DrawElementsInstanced(mode, count, type, byteOffset, 1);
  }

  void DrawArraysInstanced(GLenum mode, GLint first, GLsizei vertexCount,
                           GLsizei instanceCount);
  void DrawElementsInstanced(GLenum mode, GLsizei vertexCount, GLenum type,
                             WebGLintptr byteOffset, GLsizei instanceCount);


  JS::Value GetVertexAttrib(JSContext* cx, GLuint index, GLenum pname,
                            ErrorResult& rv);

  void GetVertexAttrib(JSContext* cx, GLuint index, GLenum pname,
                       JS::MutableHandle<JS::Value> retval, ErrorResult& rv) {
    retval.set(GetVertexAttrib(cx, index, pname, rv));
  }

  WebGLsizeiptr GetVertexAttribOffset(GLuint index, GLenum pname);

  ////

  void VertexAttrib1f(GLuint index, GLfloat x) {
    const FuncScope funcScope(*this, "vertexAttrib1f");
    VertexAttrib4f(index, x, 0, 0, 1);
  }
  void VertexAttrib2f(GLuint index, GLfloat x, GLfloat y) {
    const FuncScope funcScope(*this, "vertexAttrib2f");
    VertexAttrib4f(index, x, y, 0, 1);
  }
  void VertexAttrib3f(GLuint index, GLfloat x, GLfloat y, GLfloat z) {
    const FuncScope funcScope(*this, "vertexAttrib3f");
    VertexAttrib4f(index, x, y, z, 1);
  }

  ////

  void VertexAttrib1fv(GLuint index, const Float32ListU& list) {
    const FuncScope funcScope(*this, "vertexAttrib1fv");
    const auto& arr = Float32Arr::From(list);
    if (!ValidateAttribArraySetter(1, arr.elemCount)) return;

    VertexAttrib4f(index, arr.elemBytes[0], 0, 0, 1);
  }

  void VertexAttrib2fv(GLuint index, const Float32ListU& list) {
    const FuncScope funcScope(*this, "vertexAttrib2fv");
    const auto& arr = Float32Arr::From(list);
    if (!ValidateAttribArraySetter(2, arr.elemCount)) return;

    VertexAttrib4f(index, arr.elemBytes[0], arr.elemBytes[1], 0, 1);
  }

  void VertexAttrib3fv(GLuint index, const Float32ListU& list) {
    const FuncScope funcScope(*this, "vertexAttrib3fv");
    const auto& arr = Float32Arr::From(list);
    if (!ValidateAttribArraySetter(3, arr.elemCount)) return;

    VertexAttrib4f(index, arr.elemBytes[0], arr.elemBytes[1], arr.elemBytes[2],
                   1);
  }

  void VertexAttrib4fv(GLuint index, const Float32ListU& list) {
    const FuncScope funcScope(*this, "vertexAttrib4fv");
    const auto& arr = Float32Arr::From(list);
    if (!ValidateAttribArraySetter(4, arr.elemCount)) return;

    VertexAttrib4f(index, arr.elemBytes[0], arr.elemBytes[1], arr.elemBytes[2],
                   arr.elemBytes[3]);
  }

  ////

 protected:
  void VertexAttribAnyPointer(bool isFuncInt, GLuint index, GLint size,
                              GLenum type, bool normalized, GLsizei stride,
                              WebGLintptr byteOffset);

 public:
  void VertexAttribPointer(GLuint index, GLint size, GLenum type,
                           WebGLboolean normalized, GLsizei stride,
                           WebGLintptr byteOffset) {
    const FuncScope funcScope(*this, "vertexAttribPointer");
    const bool isFuncInt = false;
    VertexAttribAnyPointer(isFuncInt, index, size, type, normalized, stride,
                           byteOffset);
  }

  void VertexAttribDivisor(GLuint index, GLuint divisor);

 private:
  WebGLBuffer* DrawElements_check(GLsizei indexCount, GLenum type,
                                  WebGLintptr byteOffset,
                                  GLsizei instanceCount);
  void Draw_cleanup();

  void VertexAttrib1fv_base(GLuint index, uint32_t arrayLength,
                            const GLfloat* ptr);
  void VertexAttrib2fv_base(GLuint index, uint32_t arrayLength,
                            const GLfloat* ptr);
  void VertexAttrib3fv_base(GLuint index, uint32_t arrayLength,
                            const GLfloat* ptr);
  void VertexAttrib4fv_base(GLuint index, uint32_t arrayLength,
                            const GLfloat* ptr);

  bool BindArrayAttribToLocation0(WebGLProgram* prog);

  // -----------------------------------------------------------------------------
  // PROTECTED
 protected:
  WebGLVertexAttrib0Status WhatDoesVertexAttrib0Need() const;
  bool DoFakeVertexAttrib0(uint64_t vertexCount);
  void UndoFakeVertexAttrib0();

  CheckedUint32 mGeneration;

  WebGLContextOptions mOptions;

  bool mInvalidated;
  bool mCapturedFrameInvalidated;
  bool mResetLayer;
  bool mOptionsFrozen;
  bool mDisableExtensions;
  bool mIsMesa;
  bool mLoseContextOnMemoryPressure;
  bool mCanLoseContextInForeground;
  bool mShouldPresent;
  bool mDisableFragHighP;
  bool mVRReady;

  template <typename WebGLObjectType>
  void DeleteWebGLObjectsArray(nsTArray<WebGLObjectType>& array);

  GLuint mActiveTexture = 0;
  GLenum mDefaultFB_DrawBuffer0 = 0;
  GLenum mDefaultFB_ReadBuffer = 0;

  mutable GLenum mWebGLError;

  bool mBypassShaderValidation;

  webgl::ShaderValidator* CreateShaderValidator(GLenum shaderType) const;

  // some GL constants
  uint32_t mGLMaxTextureUnits = 0;

  uint32_t mGLMaxVertexAttribs = 0;
  uint32_t mGLMaxFragmentUniformVectors = 0;
  uint32_t mGLMaxVertexUniformVectors = 0;
  uint32_t mGLMaxVaryingVectors = 0;

  uint32_t mGLMaxTransformFeedbackSeparateAttribs = 0;
  uint32_t mGLMaxUniformBufferBindings = 0;

  uint32_t mGLMaxVertexTextureImageUnits = 0;
  uint32_t mGLMaxFragmentTextureImageUnits = 0;
  uint32_t mGLMaxCombinedTextureImageUnits = 0;

  uint32_t mGLMaxColorAttachments = 0;
  uint32_t mGLMaxDrawBuffers = 0;

  uint32_t mGLMaxViewportDims[2];

 public:
  GLenum LastColorAttachmentEnum() const {
    return LOCAL_GL_COLOR_ATTACHMENT0 + mGLMaxColorAttachments - 1;
  }

  const decltype(mOptions)& Options() const { return mOptions; }

 protected:
  // Texture sizes are often not actually the GL values. Let's be explicit that
  // these are implementation limits.
  uint32_t mGLMaxTextureSize = 0;
  uint32_t mGLMaxCubeMapTextureSize = 0;
  uint32_t mGLMax3DTextureSize = 0;
  uint32_t mGLMaxArrayTextureLayers = 0;
  uint32_t mGLMaxRenderbufferSize = 0;

 public:
  GLuint MaxVertexAttribs() const { return mGLMaxVertexAttribs; }

  GLuint GLMaxTextureUnits() const { return mGLMaxTextureUnits; }

  float mGLAliasedLineWidthRange[2];
  float mGLAliasedPointSizeRange[2];

  bool IsFormatValidForFB(TexInternalFormat format) const;

 protected:
  // Represents current status of the context with respect to context loss.
  // That is, whether the context is lost, and what part of the context loss
  // process we currently are at.
  // This is used to support the WebGL spec's asyncronous nature in handling
  // context loss.
  enum class ContextStatus {
    // The context is stable; there either are none or we don't know of any.
    NotLost,
    // The context has been lost, but we have not yet sent an event to the
    // script informing it of this.
    LostAwaitingEvent,
    // The context has been lost, and we have sent the script an event
    // informing it of this.
    Lost,
    // The context is lost, an event has been sent to the script, and the
    // script correctly handled the event. We are waiting for the context to
    // be restored.
    LostAwaitingRestore
  };

  // -------------------------------------------------------------------------
  // WebGL extensions (implemented in WebGLContextExtensions.cpp)
  typedef EnumeratedArray<WebGLExtensionID, WebGLExtensionID::Max,
                          RefPtr<WebGLExtensionBase>>
      ExtensionsArrayType;

  ExtensionsArrayType mExtensions;

  // enable an extension. the extension should not be enabled before.
  void EnableExtension(WebGLExtensionID ext);

  // Enable an extension if it's supported. Return the extension on success.
  WebGLExtensionBase* EnableSupportedExtension(dom::CallerType callerType,
                                               WebGLExtensionID ext);

 public:
  // returns true if the extension has been enabled by calling getExtension.
  bool IsExtensionEnabled(WebGLExtensionID ext) const;

 protected:
  // returns true if the extension is supported for this caller type (this
  // decides what getSupportedExtensions exposes)
  bool IsExtensionSupported(dom::CallerType callerType,
                            WebGLExtensionID ext) const;
  bool IsExtensionSupported(WebGLExtensionID ext) const;

  static const char* GetExtensionString(WebGLExtensionID ext);

  nsTArray<GLenum> mCompressedTextureFormats;

  // -------------------------------------------------------------------------
  // WebGL 2 specifics (implemented in WebGL2Context.cpp)
 public:
  virtual bool IsWebGL2() const = 0;

  struct FailureReason {
    nsCString key;  // For reporting.
    nsCString info;

    FailureReason() {}

    template <typename A, typename B>
    FailureReason(const A& _key, const B& _info)
        : key(nsCString(_key)), info(nsCString(_info)) {}
  };

 protected:
  bool InitWebGL2(FailureReason* const out_failReason);

  bool CreateAndInitGL(bool forceEnabled,
                       std::vector<FailureReason>* const out_failReasons);

  void ThrowEvent_WebGLContextCreationError(const nsACString& text);

  // -------------------------------------------------------------------------
  // Validation functions (implemented in WebGLContextValidate.cpp)
  bool InitAndValidateGL(FailureReason* const out_failReason);

  bool ValidateBlendEquationEnum(GLenum cap, const char* info);
  bool ValidateBlendFuncEnumsCompatibility(GLenum sfactor, GLenum dfactor,
                                           const char* info);
  bool ValidateStencilOpEnum(GLenum action, const char* info);
  bool ValidateFaceEnum(GLenum face);
  bool ValidateTexInputData(GLenum type, js::Scalar::Type jsArrayType,
                            WebGLTexImageFunc func, WebGLTexDimensions dims);
  bool ValidateAttribPointer(bool integerMode, GLuint index, GLint size,
                             GLenum type, WebGLboolean normalized,
                             GLsizei stride, WebGLintptr byteOffset,
                             const char* info);
  bool ValidateStencilParamsForDrawCall() const;

  bool ValidateCopyTexImage(TexInternalFormat srcFormat,
                            TexInternalFormat dstformat, WebGLTexImageFunc func,
                            WebGLTexDimensions dims);

  bool ValidateTexImage(TexImageTarget texImageTarget, GLint level,
                        GLenum internalFormat, GLint xoffset, GLint yoffset,
                        GLint zoffset, GLint width, GLint height, GLint depth,
                        GLint border, GLenum format, GLenum type,
                        WebGLTexImageFunc func, WebGLTexDimensions dims);
  bool ValidateTexImageFormat(GLenum internalFormat, WebGLTexImageFunc func,
                              WebGLTexDimensions dims);
  bool ValidateTexImageType(GLenum type, WebGLTexImageFunc func,
                            WebGLTexDimensions dims);
  bool ValidateTexImageFormatAndType(GLenum format, GLenum type,
                                     WebGLTexImageFunc func,
                                     WebGLTexDimensions dims);
  bool ValidateCompTexImageInternalFormat(GLenum format, WebGLTexImageFunc func,
                                          WebGLTexDimensions dims);
  bool ValidateCopyTexImageInternalFormat(GLenum format, WebGLTexImageFunc func,
                                          WebGLTexDimensions dims);
  bool ValidateTexImageSize(TexImageTarget texImageTarget, GLint level,
                            GLint width, GLint height, GLint depth,
                            WebGLTexImageFunc func, WebGLTexDimensions dims);
  bool ValidateTexSubImageSize(GLint x, GLint y, GLint z, GLsizei width,
                               GLsizei height, GLsizei depth, GLsizei baseWidth,
                               GLsizei baseHeight, GLsizei baseDepth,
                               WebGLTexImageFunc func, WebGLTexDimensions dims);
  bool ValidateCompTexImageSize(GLint level, GLenum internalFormat,
                                GLint xoffset, GLint yoffset, GLsizei width,
                                GLsizei height, GLsizei levelWidth,
                                GLsizei levelHeight, WebGLTexImageFunc func,
                                WebGLTexDimensions dims);
  bool ValidateCompTexImageDataSize(GLint level, GLenum internalFormat,
                                    GLsizei width, GLsizei height,
                                    uint32_t byteLength, WebGLTexImageFunc func,
                                    WebGLTexDimensions dims);

  bool ValidateUniformLocationForProgram(WebGLUniformLocation* location,
                                         WebGLProgram* program);

  bool HasDrawBuffers() const {
    return IsWebGL2() ||
           IsExtensionEnabled(WebGLExtensionID::WEBGL_draw_buffers);
  }

  WebGLRefPtr<WebGLBuffer>* ValidateBufferSlot(GLenum target);

 public:
  WebGLBuffer* ValidateBufferSelection(GLenum target);

 protected:
  IndexedBufferBinding* ValidateIndexedBufferSlot(GLenum target, GLuint index);

  bool ValidateIndexedBufferBinding(
      GLenum target, GLuint index,
      WebGLRefPtr<WebGLBuffer>** const out_genericBinding,
      IndexedBufferBinding** const out_indexedBinding);

  bool ValidateNonNegative(const char* argName, int64_t val) {
    if (MOZ_UNLIKELY(val < 0)) {
      ErrorInvalidValue("`%s` must be non-negative.", argName);
      return false;
    }
    return true;
  }

 public:
  template <typename T>
  bool ValidateNonNull(const char* const argName,
                       const dom::Nullable<T>& maybe) {
    if (maybe.IsNull()) {
      ErrorInvalidValue("%s: Cannot be null.", argName);
      return false;
    }
    return true;
  }

  bool ValidateArrayBufferView(const dom::ArrayBufferView& view,
                               GLuint elemOffset, GLuint elemCountOverride,
                               uint8_t** const out_bytes,
                               size_t* const out_byteLen);

 protected:
  ////

  void Invalidate();
  void DestroyResourcesAndContext();

  // helpers

  bool ConvertImage(size_t width, size_t height, size_t srcStride,
                    size_t dstStride, const uint8_t* src, uint8_t* dst,
                    WebGLTexelFormat srcFormat, bool srcPremultiplied,
                    WebGLTexelFormat dstFormat, bool dstPremultiplied,
                    size_t dstTexelSize);

  //////
 public:
  bool ValidateObjectAllowDeleted(const char* const argName,
                                  const ObjectJS& object) const {
    if (object->Context() != this) {
      ErrorInvalidOperation(
          "%s: Object from different WebGL context (or older"
          " generation of this one) passed as argument.",
          argName);
      return false;
    }

    return true;
  }

  bool ValidateObject(const char* const argName,
                      const ObjectJS& object,
                      const bool isShaderOrProgram = false) {
    if (!ValidateObjectAllowDeleted(argName, object)) return false;

    if (isShaderOrProgram) {
      /* GLES 3.0.5 p45:
       * "Commands that accept shader or program object names will generate the
       *  error INVALID_VALUE if the provided name is not the name of either a
       *  shader or program object[.]"
       * Further, shaders and programs appear to be different from other
       * objects, in that their lifetimes are better defined. However, they also
       * appear to allow use of objects marked for deletion, and only reject
       * actually-destroyed objects.
       */
      if (object.IsDeleted()) {
        ErrorInvalidValue(
            "%s: Shader or program object argument cannot have been"
            " deleted.",
            argName);
        return false;
      }
    } else {
      if (object.IsDeleteRequested()) {
        ErrorInvalidOperation(
            "%s: Object argument cannot have been marked for"
            " deletion.",
            argName);
        return false;
      }
    }

    return true;
  }

  ////

  // Program and Shader are incomplete, so we can't inline the conversion to
  // WebGLDeletableObject here.
  bool ValidateObject(const char* const argName, const WebGLProgram& object);
  bool ValidateObject(const char* const argName, const WebGLShader& object);

  ////

  bool ValidateIsObject(const WebGLDeletableObject* object) const;
  bool ValidateDeleteObject(const WebGLDeletableObject* object);

  ////

 private:
  // -------------------------------------------------------------------------
  // Context customization points
  virtual WebGLVertexArray* CreateVertexArrayImpl();

 public:
  void ForceLoseContext(bool simulateLoss = false);

 protected:
  void ForceRestoreContext();

  nsTArray<WebGLRefPtr<WebGLTexture>> mBound2DTextures;
  nsTArray<WebGLRefPtr<WebGLTexture>> mBoundCubeMapTextures;
  nsTArray<WebGLRefPtr<WebGLTexture>> mBound3DTextures;
  nsTArray<WebGLRefPtr<WebGLTexture>> mBound2DArrayTextures;
  nsTArray<WebGLRefPtr<WebGLSampler>> mBoundSamplers;

  void ResolveTexturesForDraw() const;

  WebGLRefPtr<WebGLProgram> mCurrentProgram;
  RefPtr<const webgl::LinkedProgramInfo> mActiveProgramLinkInfo;

  bool ValidateFramebufferTarget(GLenum target);
  bool ValidateInvalidateFramebuffer(GLenum target,
                                     const dom::Sequence<GLenum>& attachments,
                                     ErrorResult* const out_rv,
                                     std::vector<GLenum>* const scopedVector,
                                     GLsizei* const out_glNumAttachments,
                                     const GLenum** const out_glAttachments);

  WebGLRefPtr<WebGLFramebuffer> mBoundDrawFramebuffer;
  WebGLRefPtr<WebGLFramebuffer> mBoundReadFramebuffer;
  WebGLRefPtr<WebGLRenderbuffer> mBoundRenderbuffer;
  WebGLRefPtr<WebGLVertexArray> mBoundVertexArray;

  LinkedList<WebGLBuffer> mBuffers;
  LinkedList<WebGLFramebuffer> mFramebuffers;
  LinkedList<WebGLProgram> mPrograms;
  LinkedList<WebGLQuery> mQueries;
  LinkedList<WebGLRenderbuffer> mRenderbuffers;
  LinkedList<WebGLSampler> mSamplers;
  LinkedList<WebGLShader> mShaders;
  LinkedList<WebGLSync> mSyncs;
  LinkedList<WebGLTexture> mTextures;
  LinkedList<WebGLTransformFeedback> mTransformFeedbacks;
  LinkedList<WebGLVertexArray> mVertexArrays;

  WebGLRefPtr<WebGLTransformFeedback> mDefaultTransformFeedback;
  WebGLRefPtr<WebGLVertexArray> mDefaultVertexArray;

  // PixelStore parameters
  uint32_t mPixelStore_UnpackImageHeight = 0;
  uint32_t mPixelStore_UnpackSkipImages = 0;
  uint32_t mPixelStore_UnpackRowLength = 0;
  uint32_t mPixelStore_UnpackSkipRows = 0;
  uint32_t mPixelStore_UnpackSkipPixels = 0;
  uint32_t mPixelStore_UnpackAlignment = 0;
  uint32_t mPixelStore_PackRowLength = 0;
  uint32_t mPixelStore_PackSkipRows = 0;
  uint32_t mPixelStore_PackSkipPixels = 0;
  uint32_t mPixelStore_PackAlignment = 0;

  CheckedUint32 GetUnpackSize(bool isFunc3D, uint32_t width, uint32_t height,
                              uint32_t depth, uint8_t bytesPerPixel);

  bool ValidatePackSize(uint32_t width, uint32_t height, uint8_t bytesPerPixel,
                        uint32_t* const out_rowStride,
                        uint32_t* const out_endOffset);

  GLenum mPixelStore_ColorspaceConversion = 0;
  bool mPixelStore_FlipY = false;
  bool mPixelStore_PremultiplyAlpha = false;
  bool mPixelStore_RequireFastPath = false;

  ////////////////////////////////////

 protected:
  GLuint mEmptyTFO;

  // Generic Vertex Attributes
  // Though CURRENT_VERTEX_ATTRIB is listed under "Vertex Shader State" in the
  // spec state tables, this isn't vertex shader /object/ state. This array is
  // merely state useful to vertex shaders, but is global state.
  std::vector<webgl::AttribBaseType> mGenericVertexAttribTypes;
  uint8_t mGenericVertexAttrib0Data[sizeof(float) * 4];
  CacheInvalidator mGenericVertexAttribTypeInvalidator;

  GLuint mFakeVertexAttrib0BufferObject = 0;
  size_t mFakeVertexAttrib0BufferObjectSize = 0;
  bool mFakeVertexAttrib0DataDefined = false;
  uint8_t mFakeVertexAttrib0Data[sizeof(float) * 4];

  JSObject* GetVertexAttribFloat32Array(JSContext* cx, GLuint index);
  JSObject* GetVertexAttribInt32Array(JSContext* cx, GLuint index);
  JSObject* GetVertexAttribUint32Array(JSContext* cx, GLuint index);

  GLint mStencilRefFront = 0;
  GLint mStencilRefBack = 0;
  GLuint mStencilValueMaskFront = 0;
  GLuint mStencilValueMaskBack = 0;
  GLuint mStencilWriteMaskFront = 0;
  GLuint mStencilWriteMaskBack = 0;
  uint8_t mColorWriteMask = 0;  // bitmask
  realGLboolean mDepthWriteMask = 0;
  GLfloat mColorClearValue[4];
  GLint mStencilClearValue = 0;
  GLfloat mDepthClearValue = 0.0;

  GLint mViewportX;
  GLint mViewportY;
  GLsizei mViewportWidth;
  GLsizei mViewportHeight;
  bool mAlreadyWarnedAboutViewportLargerThanDest;

  GLfloat mLineWidth = 0.0;

  WebGLContextLossHandler mContextLossHandler;
  bool mAllowContextRestore;
  bool mLastLossWasSimulated;
  ContextStatus mContextStatus = ContextStatus::NotLost;

  // Used for some hardware (particularly Tegra 2 and 4) that likes to
  // be Flushed while doing hundreds of draw calls.
  int mDrawCallsSinceLastFlush;

  mutable int mAlreadyGeneratedWarnings;
  int mMaxWarnings;
  bool mAlreadyWarnedAboutFakeVertexAttrib0;

  bool ShouldGenerateWarnings() const;

  bool ShouldGeneratePerfWarnings() const {
    return mNumPerfWarnings < mMaxPerfWarnings;
  }

  uint64_t mLastUseIndex;

  bool mNeedsFakeNoAlpha;
  bool mNeedsFakeNoDepth;
  bool mNeedsFakeNoStencil;
  bool mNeedsFakeNoStencil_UserFBs = false;

  mutable uint8_t mDriverColorMask = 0;
  bool mDriverDepthTest = false;
  bool mDriverStencilTest = false;

  bool mNeedsIndexValidation = false;

  const bool mAllowFBInvalidation;
#if defined(MOZ_WIDGET_ANDROID)
  UniquePtr<gl::GLScreenBuffer> mVRScreen;
#endif

  bool Has64BitTimestamps() const;

  // --

  const uint8_t mMsaaSamples;
  mutable gfx::IntSize mRequestedSize;
  mutable UniquePtr<gl::MozFramebuffer> mDefaultFB;
  mutable bool mDefaultFB_IsInvalid = false;
  mutable UniquePtr<gl::MozFramebuffer> mResolvedDefaultFB;

  // --

  bool EnsureDefaultFB();
  bool ValidateAndInitFB(const WebGLFramebuffer* fb);
  void DoBindFB(const WebGLFramebuffer* fb,
                GLenum target = LOCAL_GL_FRAMEBUFFER) const;

  bool BindCurFBForDraw();
  bool BindCurFBForColorRead(const webgl::FormatUsageInfo** out_format,
                             uint32_t* out_width, uint32_t* out_height);
  void DoColorMask(uint8_t bitmask) const;
  void BlitBackbufferToCurDriverFB() const;
  bool BindDefaultFBForRead();

  // --

 public:
  void LoseOldestWebGLContextIfLimitExceeded();
  void UpdateLastUseIndex();

  template <typename WebGLObjectType>
  JS::Value WebGLObjectAsJSValue(JSContext* cx, const WebGLObjectType*,
                                 ErrorResult& rv) const;
  template <typename WebGLObjectType>
  JSObject* WebGLObjectAsJSObject(JSContext* cx, const WebGLObjectType*,
                                  ErrorResult& rv) const;

 public:
  // console logging helpers
  void GenerateWarning(const char* fmt, ...) const MOZ_FORMAT_PRINTF(2, 3);
  void GenerateWarning(const char* fmt, va_list ap) const
      MOZ_FORMAT_PRINTF(2, 0);

  void GeneratePerfWarning(const char* fmt, ...) const MOZ_FORMAT_PRINTF(2, 3);

 public:
  UniquePtr<webgl::FormatUsageAuthority> mFormatUsage;

  virtual UniquePtr<webgl::FormatUsageAuthority> CreateFormatUsage(
      gl::GLContext* gl) const = 0;

  const decltype(mBound2DTextures)* TexListForElemType(GLenum elemType) const;

  void UpdateMaxDrawBuffers();

  // --
 private:
  webgl::AvailabilityRunnable* mAvailabilityRunnable = nullptr;

 public:
  webgl::AvailabilityRunnable* EnsureAvailabilityRunnable();

  // -

  // Friend list
  friend class ScopedCopyTexImageSource;
  friend class ScopedResolveTexturesForDraw;
  friend class ScopedUnpackReset;
  friend class webgl::TexUnpackBlob;
  friend class webgl::TexUnpackBytes;
  friend class webgl::TexUnpackImage;
  friend class webgl::TexUnpackSurface;
  friend struct webgl::UniformInfo;
  friend class WebGLTexture;
  friend class WebGLFBAttachPoint;
  friend class WebGLFramebuffer;
  friend class WebGLRenderbuffer;
  friend class WebGLProgram;
  friend class WebGLQuery;
  friend class WebGLBuffer;
  friend class WebGLSampler;
  friend class WebGLShader;
  friend class WebGLSync;
  friend class WebGLTransformFeedback;
  friend class WebGLUniformLocation;
  friend class WebGLVertexArray;
  friend class WebGLVertexArrayFake;
  friend class WebGLVertexArrayGL;
};

// used by DOM bindings in conjunction with GetParentObject
inline nsISupports* ToSupports(WebGLContext* webgl) {
  return static_cast<nsICanvasRenderingContextInternal*>(webgl);
}

// Returns `value` rounded to the next highest multiple of `multiple`.
// AKA PadToAlignment, StrideForAlignment.
template <typename V, typename M>
V RoundUpToMultipleOf(const V& value, const M& multiple) {
  return ((value + multiple - 1) / multiple) * multiple;
}

const char* GetEnumName(GLenum val, const char* defaultRet = "<unknown>");
std::string EnumString(GLenum val);

bool ValidateTexTarget(WebGLContext* webgl, uint8_t funcDims,
                       GLenum rawTexTarget, TexTarget* const out_texTarget,
                       WebGLTexture** const out_tex);
bool ValidateTexImageTarget(WebGLContext* webgl, uint8_t funcDims,
                            GLenum rawTexImageTarget,
                            TexImageTarget* const out_texImageTarget,
                            WebGLTexture** const out_tex);

class ScopedUnpackReset final : public gl::ScopedGLWrapper<ScopedUnpackReset> {
  friend struct gl::ScopedGLWrapper<ScopedUnpackReset>;

 private:
  const WebGLContext* const mWebGL;

 public:
  explicit ScopedUnpackReset(const WebGLContext* webgl);

 private:
  void UnwrapImpl();
};

class ScopedFBRebinder final : public gl::ScopedGLWrapper<ScopedFBRebinder> {
  friend struct gl::ScopedGLWrapper<ScopedFBRebinder>;

 private:
  const WebGLContext* const mWebGL;

 public:
  explicit ScopedFBRebinder(const WebGLContext* const webgl)
      : ScopedGLWrapper<ScopedFBRebinder>(webgl->gl), mWebGL(webgl) {}

 private:
  void UnwrapImpl();
};

class ScopedLazyBind final : public gl::ScopedGLWrapper<ScopedLazyBind> {
  friend struct gl::ScopedGLWrapper<ScopedLazyBind>;

  const GLenum mTarget;
  const WebGLBuffer* const mBuf;

 public:
  ScopedLazyBind(gl::GLContext* gl, GLenum target, const WebGLBuffer* buf);

 private:
  void UnwrapImpl();
};

////

bool Intersect(int32_t srcSize, int32_t read0, int32_t readSize,
               int32_t* out_intRead0, int32_t* out_intWrite0,
               int32_t* out_intSize);

uint64_t AvailGroups(uint64_t totalAvailItems, uint64_t firstItemOffset,
                     uint32_t groupSize, uint32_t groupStride);

////

class ScopedDrawCallWrapper final {
 public:
  WebGLContext& mWebGL;

  explicit ScopedDrawCallWrapper(WebGLContext& webgl);
  ~ScopedDrawCallWrapper();
};

namespace webgl {
class ScopedPrepForResourceClear final {
  const WebGLContext& webgl;

 public:
  explicit ScopedPrepForResourceClear(const WebGLContext&);
  ~ScopedPrepForResourceClear();
};
}  // namespace webgl

////

void ImplCycleCollectionTraverse(nsCycleCollectionTraversalCallback& callback,
                                 const std::vector<IndexedBufferBinding>& field,
                                 const char* name, uint32_t flags = 0);

void ImplCycleCollectionUnlink(std::vector<IndexedBufferBinding>& field);

}  // namespace mozilla

#endif

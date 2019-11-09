/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef CLIENTWEBGLCONTEXT_H_
#define CLIENTWEBGLCONTEXT_H_

#include "GLConsts.h"
#include "mozilla/dom/ImageData.h"
#include "mozilla/Range.h"
#include "nsICanvasRenderingContextInternal.h"
#include "nsWeakReference.h"
#include "nsWrapperCache.h"
#include "WebGLActiveInfo.h"
#include "mozilla/dom/WebGLRenderingContextBinding.h"
#include "mozilla/dom/WebGL2RenderingContextBinding.h"
#include "WebGLStrongTypes.h"
#include "WebGLTypes.h"

#include "mozilla/Logging.h"
#include "WebGLCrossProcessCommandQueue.h"

#include <memory>
#include <unordered_map>
#include <vector>

#ifndef WEBGL_BRIDGE_LOG_
#  define WEBGL_BRIDGE_LOG_(lvl, ...) \
    MOZ_LOG(mozilla::gWebGLBridgeLog, lvl, (__VA_ARGS__))
#  define WEBGL_BRIDGE_LOGV(...) \
    WEBGL_BRIDGE_LOG_(LogLevel::Verbose, __VA_ARGS__)
#  define WEBGL_BRIDGE_LOGD(...) WEBGL_BRIDGE_LOG_(LogLevel::Debug, __VA_ARGS__)
#  define WEBGL_BRIDGE_LOGI(...) WEBGL_BRIDGE_LOG_(LogLevel::Info, __VA_ARGS__)
#  define WEBGL_BRIDGE_LOGE(...) WEBGL_BRIDGE_LOG_(LogLevel::Error, __VA_ARGS__)
#endif  // WEBGL_BRIDGE_LOG_

namespace mozilla {

class ClientWebGLExtensionBase;

namespace dom {
class WebGLChild;
}

namespace layers {
class SharedSurfaceTextureClient;
}

namespace webgl {
class TexUnpackBlob;
class TexUnpackBytes;
}  // namespace webgl

////////////////////////////////////

class WebGLActiveInfoJS final : public nsWrapperCache {
 public:
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(WebGLActiveInfoJS)
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_NATIVE_CLASS(WebGLActiveInfoJS)

 private:
  const WeakPtr<const ClientWebGLContext> mParent;
 public:
  const uint32_t mElemCount; // `size`
  const GLenum mElemType;    // `type`
  const nsString mName;  // `name`, with any final "[0]".

  WebGLActiveInfoJS(const ClientWebGLContext&, uint32_t elemCount, GLenum elemType, const nsAString& name);

  // -
  // WebIDL attributes

  GLint Size() const { return static_cast<GLint>(mElemCount); }
  GLenum Type() const { return mElemType; }

  void GetName(nsString& retval) const {
    retval = mName;
    //CopyASCIItoUTF16(mBaseUserName, retval);
    //if (mIsArray) retval.AppendLiteral("[0]");
  }

  // -

  auto GetParentObject() const { return mParent.get(); }

private:
  virtual ~WebGLActiveInfoJS() {}
  virtual JSObject* WrapObject(JSContext*, JS::Handle<JSObject*>) override;
};

class WebGLShaderPrecisionFormatJS final : public nsWrapperCache {
 public:
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(WebGLShaderPrecisionFormatJS)
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_NATIVE_CLASS(WebGLShaderPrecisionFormatJS)

 private:
  const WeakPtr<const ClientWebGLContext> mParent;
 public:
  const webgl::ShaderPrecisionFormat mInfo;

  WebGLShaderPrecisionFormatJS(const ClientWebGLContext& webgl,
      const webgl::ShaderPrecisionFormat& info)
      : mParent(&webgl), mInfo(info) {}

  auto GetParentObject() const { return mParent.get(); }

private:
  virtual ~WebGLShaderPrecisionFormatJS() {}
  JSObject* WrapObject(JSContext*, JS::Handle<JSObject*>) override;

public:
  GLint RangeMin() const { return mInfo.rangeMin; }
  GLint RangeMax() const { return mInfo.rangeMax; }
  GLint Precision() const { return mInfo.precision; }
};

// -----------------------

struct WebGLProgramPreventDelete;

namespace webgl {

struct LinkResult;

class ContextGenerationInfo final {
public:
  ClientWebGLContext& mContext;
private:
  Atomic<ObjectId> mLastId;
public:
  std::shared_ptr<webgl::LinkResult> mActiveLinkResult;
  std::shared_ptr<WebGLProgramPreventDelete> mCurrentProgram;

  RefPtr<WebGLTransformFeedbackJS> mDefaultTfo;
  RefPtr<WebGLVertexArrayJS> mDefaultVao;

  std::unordered_map<GLenum, RefPtr<WebGLBufferJS>> mBoundBufferByTarget;
  std::vector<RefPtr<WebGLBufferJS>> mBoundUbos;
  RefPtr<WebGLFramebufferJS> mBoundDrawFb;
  RefPtr<WebGLFramebufferJS> mBoundReadFb;
  RefPtr<WebGLRenderbufferJS> mBoundRb;
  RefPtr<WebGLTransformFeedbackJS> mBoundTfo;
  std::unordered_map<GLenum, RefPtr<WebGLQueryJS>> mCurrentQueryByTarget;
  RefPtr<WebGLVertexArrayJS> mBoundVao;

  struct TexUnit final {
    RefPtr<WebGLSamplerJS> sampler;
    std::unordered_map<GLenum, RefPtr<WebGLTextureJS>> texByTarget;
  };
  uint32_t mActiveTexUnit = 0;
  std::vector<TexUnit> mTexUnits;

  bool mTfActiveAndNotPaused = false;

  struct GenericVertexAttrib final {
    webgl::AttribBaseType type = webgl::AttribBaseType::Float;
    uint8_t data[4*sizeof(float)] = {};
  };
  std::vector<GenericVertexAttrib> mGenericVertexAttribs;

  bool mColorWriteMask[4] = {true, true, true, true};
  int32_t mScissor[4] = {};
  int32_t mViewport[4] = {};
  float mClearColor[4] = {1, 1, 1, 1};
  float mBlendColor[4] = {1, 1, 1, 1};
  float mDepthRange[2] = {0, 1};

public:
  explicit ContextGenerationInfo(ClientWebGLContext& context) : mContext(context), mLastId(0) {}

  ObjectId NextId() {
    return mLastId += 1;
  }
};

class ObjectJS : public nsWrapperCache {
  friend ClientWebGLContext;
 public:
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(ObjectJS)
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_NATIVE_CLASS(ObjectJS)

  const std::weak_ptr<ContextGenerationInfo> mGeneration;
  const ObjectId mId;
 protected:
  bool mDeleteRequested = false;

 public:
  explicit ObjectJS(ClientWebGLContext&);

  ClientWebGLContext* Context() const {
    const auto locked = mGeneration.lock();
    if (!locked) return nullptr;
    return &(locked->mContext);
  }

  ClientWebGLContext* GetParentObject() const { return Context(); }

  bool IsUsable(const ClientWebGLContext&) const;

  bool ValidateUsable(const ClientWebGLContext& context, const char* const argName) const {
    if (MOZ_LIKELY( IsUsable(context) )) return true;
    WarnInvalidUse(context, argName);
    return false;
  }

 private:
  void WarnInvalidUse(const ClientWebGLContext&, const char* argName) const;

 public:
  virtual bool IsDeleted() const {
    return mDeleteRequested;
  }

 protected:
  virtual ~ObjectJS() = default;
};

} // namespace webgl

// -

class WebGLBufferJS final : public webgl::ObjectJS {
  friend class ClientWebGLContext;

  webgl::BufferKind mKind = webgl::BufferKind::Undefined; // !IsBuffer until Bind

private:
  JSObject* WrapObject(JSContext*, JS::Handle<JSObject*>) override;
};

class WebGLFramebufferJS final : public webgl::ObjectJS {
  friend class ClientWebGLContext;

  GLenum mTarget = 0; // !IsFramebuffer until Bind

  struct Attachment final {
    RefPtr<WebGLRenderbufferJS> rb;
    RefPtr<WebGLTextureJS> tex;
  };

  std::unordered_map<GLenum, Attachment> mAttachments;

 public:
  explicit WebGLFramebufferJS(ClientWebGLContext&);
private:
  JSObject* WrapObject(JSContext*, JS::Handle<JSObject*>) override;
};

struct WebGLProgramPreventDelete final {
  const RefPtr<WebGLProgramJS> js;
};


struct WebGLShaderPreventDelete;

class WebGLProgramJS final : public webgl::ObjectJS {
  friend class ClientWebGLContext;

  std::shared_ptr<WebGLProgramPreventDelete> mInnerRef;
  const std::weak_ptr<WebGLProgramPreventDelete> mInnerWeak;

  std::unordered_map<GLenum, std::shared_ptr<WebGLShaderPreventDelete>> mNextLink_Shaders;
  bool mLastValidate = false;
  mutable std::shared_ptr<webgl::LinkResult> mResult; // Never null, often defaulted.
  Maybe<std::unordered_map<std::string, RefPtr<WebGLUniformLocationJS>>> mUniformLocs;

 public:
  explicit WebGLProgramJS(ClientWebGLContext&);

  bool IsDeleted() const override {
    return !mInnerWeak.lock();
  }
private:
  JSObject* WrapObject(JSContext*, JS::Handle<JSObject*>) override;
};

class WebGLQueryJS final : public webgl::ObjectJS {
  friend class ClientWebGLContext;

  GLenum mTarget = 0; // !IsQuery until Bind
private:
  JSObject* WrapObject(JSContext*, JS::Handle<JSObject*>) override;
};

class WebGLRenderbufferJS final : public webgl::ObjectJS {
  friend class ClientWebGLContext;

  bool mHasBeenBound = false; // !IsRenderbuffer until Bind
private:
  JSObject* WrapObject(JSContext*, JS::Handle<JSObject*>) override;
};

class WebGLSamplerJS final : public webgl::ObjectJS {
  // IsSampler without Bind
 public:
  explicit WebGLSamplerJS(ClientWebGLContext&);
private:
  JSObject* WrapObject(JSContext*, JS::Handle<JSObject*>) override;
};

struct WebGLShaderPreventDelete final {
  const RefPtr<WebGLShaderJS> js;
};

class WebGLShaderJS final : public webgl::ObjectJS {
  friend class ClientWebGLContext;

  const GLenum type;
  std::shared_ptr<WebGLShaderPreventDelete> mInnerRef;
  const std::weak_ptr<WebGLShaderPreventDelete> mInnerWeak;
  nsCString mSource;

  mutable webgl::CompileResult mResult;


 public:
  explicit WebGLShaderJS(ClientWebGLContext&, GLenum type);

  bool IsDeleted() const override {
    return !mInnerWeak.lock();
  }
private:
  JSObject* WrapObject(JSContext*, JS::Handle<JSObject*>) override;
};

class WebGLSyncJS final : public webgl::ObjectJS {
  friend class ClientWebGLContext;

  bool mSignaled = false;

 public:
  explicit WebGLSyncJS(ClientWebGLContext&);
private:
  JSObject* WrapObject(JSContext*, JS::Handle<JSObject*>) override;
};

class WebGLTextureJS final : public webgl::ObjectJS {
  friend class ClientWebGLContext;

  GLenum mTarget = 0; // !IsTexture until Bind
private:
  JSObject* WrapObject(JSContext*, JS::Handle<JSObject*>) override;
};

class WebGLTransformFeedbackJS final : public webgl::ObjectJS {
  friend class ClientWebGLContext;

  bool mHasBeenBound = false; // !IsTransformFeedback until Bind
  bool mActiveOrPaused = false;
  std::vector<RefPtr<WebGLBufferJS>> mAttribBuffers;

 public:
  explicit WebGLTransformFeedbackJS(ClientWebGLContext&);
private:
  JSObject* WrapObject(JSContext*, JS::Handle<JSObject*>) override;
};

class WebGLUniformLocationJS final : public webgl::ObjectJS {
  friend class ClientWebGLContext;

  const std::weak_ptr<webgl::LinkResult> mParent;
  const uint32_t mLocation;

public:
  WebGLUniformLocationJS(ClientWebGLContext&,
        std::weak_ptr<webgl::LinkResult>, uint32_t loc);
private:
  JSObject* WrapObject(JSContext*, JS::Handle<JSObject*>) override;
};

class WebGLVertexArrayJS final : public webgl::ObjectJS {
  friend class ClientWebGLContext;

  bool mHasBeenBound = false; // !IsVertexArray until Bind
  RefPtr<WebGLBufferJS> mIndexBuffer;
  std::vector<RefPtr<WebGLBufferJS>> mAttribBuffers;

 public:
  explicit WebGLVertexArrayJS(ClientWebGLContext&);
private:
  JSObject* WrapObject(JSContext*, JS::Handle<JSObject*>) override;
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

  TexImageSourceAdapter(const dom::Nullable<dom::ArrayBufferView>* maybeView,
                        GLuint viewElemOffset) {
    if (!maybeView->IsNull()) {
      mView = &(maybeView->Value());
    }
    mViewElemOffset = viewElemOffset;
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

/**
 * Base class for all IDL implementations of WebGLContext
 */
class ClientWebGLContext final : public nsICanvasRenderingContextInternal,
                                 public SupportsWeakPtr<ClientWebGLContext>,
                                 public nsWrapperCache {
  friend class WebGLContextUserData;

 public:
  MOZ_DECLARE_WEAKREFERENCE_TYPENAME(ClientWebGLContext)

  // ----------------------------- Lifetime and DOM ---------------------------
 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS_AMBIGUOUS(
      ClientWebGLContext, nsICanvasRenderingContextInternal)

  JSObject* WrapObject(JSContext* cx,
                       JS::Handle<JSObject*> givenProto) override {
    if (mIsWebGL2)
      return dom::WebGLRenderingContext_Binding::Wrap(cx, this, givenProto);
    return dom::WebGL2RenderingContext_Binding::Wrap(cx, this, givenProto);
  }

  // -

 public:
  const bool mIsWebGL2;

  explicit ClientWebGLContext(bool webgl2);

 private:
  virtual ~ClientWebGLContext();

  uvec2 mRequestedSize;
  Maybe<uvec2> mDrawingBufferSize;
  const RefPtr<ClientWebGLExtensionLoseContext> mExtLoseContext;

  webgl::LossStatus mLossStatus = webgl::LossStatus::Ready;

  // -
 public:
  struct RemotingData final {
    // In the cross process case, the WebGL actor's ownership relationship looks
    // like this:
    // ---------------------------------------------------------------------
    // | ClientWebGLContext -> WebGLChild -> WebGLParent -> HostWebGLContext
    // ---------------------------------------------------------------------
    //
    // where 'A -> B' means "A owns B"
    RefPtr<mozilla::dom::WebGLChild> mWebGLChild;
    UniquePtr<ClientWebGLCommandSource> mCommandSource;
  };
  struct NotLostData final {
    std::shared_ptr<webgl::ContextGenerationInfo> generation;
    Maybe<RemotingData> outOfProcess;
    UniquePtr<HostWebGLContext> inProcess;
    webgl::InitContextResult info;
    std::array<RefPtr<ClientWebGLExtensionBase>,
               EnumValue(WebGLExtensionID::Max)>
        extensions;
  };

 private:
  Maybe<NotLostData> mNotLost;

  // -

 public:
  void LoseContext(webgl::ContextLossReason);
  void RestoreContext();
  void OnContextLoss(webgl::ContextLossReason);

 private:
  bool DispatchEvent(const nsAString&) const;
  void Event_webglcontextlost();
  void Event_webglcontextrestored();

  bool CreateHostContext();
  void ThrowEvent_WebGLContextCreationError(const std::string&) const;

 public:
  void MarkCanvasDirty() { Invalidate(); }

  mozilla::dom::WebGLChild* GetChild() const {
    if (!mNotLost) return nullptr;
    if (!mNotLost->outOfProcess) return nullptr;
    return mNotLost->outOfProcess->mWebGLChild.get();
  }

  // -------------------------------------------------------------------------
  // Client WebGL Object Tracking
  // -------------------------------------------------------------------------
 public:

 public:
  JS::Value ToJSValue(JSContext* cx, const MaybeWebGLVariant& aVariant,
                      ErrorResult& rv) const;

 protected:
  friend struct MaybeWebGLVariantMatcher;

  template <typename WebGLObjectType>
  JS::Value WebGLObjectAsJSValue(JSContext* cx,
                                 RefPtr<WebGLObjectType>&& object,
                                 ErrorResult& rv) const;

  template <typename WebGLObjectType>
  JS::Value WebGLObjectAsJSValue(JSContext* cx, const WebGLObjectType*,
                                 ErrorResult& rv) const;

  template <typename WebGLObjectType>
  JSObject* WebGLObjectAsJSObject(JSContext* cx, const WebGLObjectType*,
                                  ErrorResult& rv) const;

 public:
  // -------------------------------------------------------------------------
  // Binary data access/conversion for IPC
  // -------------------------------------------------------------------------
 protected:
  typedef dom::Float32ArrayOrUnrestrictedFloatSequence Float32ListU;
  typedef dom::Int32ArrayOrLongSequence Int32ListU;
  typedef dom::Uint32ArrayOrUnsignedLongSequence Uint32ListU;

  // -

  template<typename T>
  static Range<T> MakeRange(const dom::Sequence<T>& seq) {
    return {seq.Elements(), seq.Length()};
  }

  // abv = ArrayBufferView
  template<typename T>
  static auto MakeRangeAbv(const T& abv) -> Range<const typename T::element_type> {
    abv.ComputeLengthAndData();
    return {abv.DataAllowShared(), abv.LengthAllowShared()};
  }

  static Range<const float> MakeRange(const Float32ListU& list) {
    if (list.IsFloat32Array()) return MakeRangeAbv(list.GetAsFloat32Array());

    return MakeRange(list.GetAsUnrestrictedFloatSequence());
  }

  static Range<const int32_t> MakeRange(const Int32ListU& list) {
    if (list.IsInt32Array()) return MakeRangeAbv(list.GetAsInt32Array());

    return MakeRange(list.GetAsLongSequence());
  }

  static Range<const uint32_t> MakeRange(const Uint32ListU& list) {
    if (list.IsUint32Array()) return MakeRangeAbv(list.GetAsUint32Array());

    return MakeRange(list.GetAsUnsignedLongSequence());
  }

  // -

  MaybeWebGLTexUnpackVariant From(TexImageTarget target, GLsizei rawWidth,
                                  GLsizei rawHeight, GLsizei rawDepth,
                                  GLint border, const TexImageSource& src);

  MaybeWebGLTexUnpackVariant ClientFromDomElem(TexImageTarget target,
                                               uint32_t width, uint32_t height,
                                               uint32_t depth,
                                               const dom::Element& elem,
                                               ErrorResult* const out_error);

  MaybeWebGLTexUnpackVariant FromCompressed(
      TexImageTarget target, GLsizei rawWidth, GLsizei rawHeight,
      GLsizei rawDepth, GLint border, const TexImageSource& src,
      const Maybe<GLsizei>& expectedImageSize);

  // -------------------------------------------------------------------------
  // Client WebGL API call tracking and error message reporting
  // -------------------------------------------------------------------------
 public:
  // Remembers the WebGL function that is lowest on the stack for client-side
  // error generation.
  class FuncScope final {
   public:
    const ClientWebGLContext& mWebGL;
    const char* const mFuncName;
    const FuncScopeId mId;

    FuncScope(const ClientWebGLContext& webgl, const char* funcName)
        : mWebGL(webgl),
          mFuncName(funcName),
          mId(FuncScopeId::FuncScopeIdError) {
      // Only set if an "outer" scope hasn't already been set.
      if (!mWebGL.mFuncScope) {
        mWebGL.mFuncScope = this;
      }
    }

    FuncScope(const ClientWebGLContext* webgl, FuncScopeId aId)
        : mWebGL(*webgl), mFuncName(GetFuncScopeName(aId)), mId(aId) {
      mWebGL.mFuncScope = this;
    }

    ~FuncScope() {
      if (this == mWebGL.mFuncScope) {
        mWebGL.mFuncScope = nullptr;
      }
    }
  };

 protected:
  // The scope of the function at the top of the current WebGL function call
  // stack
  mutable FuncScope* mFuncScope = nullptr;

  const auto& CurFuncScope() const { return *mFuncScope; }
  FuncScopeId GetFuncScopeId() const {
    return mFuncScope ? mFuncScope->mId : FuncScopeId::FuncScopeIdError;
  }
  const char* FuncName() const {
    return mFuncScope ? mFuncScope->mFuncName : nullptr;
  }

 public:
  template <typename... Args>
  void EnqueueError(const GLenum error, const char* const format,
                    const Args&... args) const MOZ_FORMAT_PRINTF(3, 4) {
    MOZ_ASSERT(FuncName());
    nsCString text;
    text.AppendPrintf("WebGL warning: %s: ", FuncName());
    text.AppendPrintf(format, args...);

    EnqueueErrorImpl(error, text);
  }

  template <typename... Args>
  void EnqueueWarning(const char* const format, const Args&... args) const
      MOZ_FORMAT_PRINTF(2, 3) {
    EnqueueError(0, format, args...);
  }

  void EnqueueError_ArgEnum(const char* argName, GLenum val) const; // Cold code.

 private:
  void EnqueueErrorImpl(GLenum errorOrZero, const nsACString&) const;

 public:
  bool ValidateArrayBufferView(const dom::ArrayBufferView& view,
                               GLuint elemOffset, GLuint elemCountOverride,
                               const GLenum errorEnum,
                               uint8_t** const out_bytes,
                               size_t* const out_byteLen) const;

 protected:
  template <typename T>
  bool ValidateNonNull(const char* const argName,
                       const dom::Nullable<T>& maybe) const {
    if (maybe.IsNull()) {
      EnqueueError(LOCAL_GL_INVALID_VALUE, "%s: Cannot be null.", argName);
      return false;
    }
    return true;
  }

  bool ValidateNonNegative(const char* argName, int64_t val) const {
    if (MOZ_UNLIKELY(val < 0)) {
      EnqueueError(LOCAL_GL_INVALID_VALUE, "`%s` must be non-negative.",
                   argName);
      return false;
    }
    return true;
  }

  bool ValidateViewType(GLenum unpackType, const TexImageSource& src) const;

  bool ValidateExtents(GLsizei width, GLsizei height, GLsizei depth,
                       GLint border, uint32_t* const out_width,
                       uint32_t* const out_height, uint32_t* const out_depth) const;

  // -------------------------------------------------------------------------
  // nsICanvasRenderingContextInternal / nsAPostRefreshObserver
  // -------------------------------------------------------------------------
 public:
  already_AddRefed<layers::Layer> GetCanvasLayer(
      nsDisplayListBuilder* builder, layers::Layer* oldLayer,
      layers::LayerManager* manager) override;
  bool InitializeCanvasRenderer(nsDisplayListBuilder* aBuilder,
                                layers::CanvasRenderer* aRenderer) override;
  // Note that 'clean' here refers to its invalidation state, not the
  // contents of the buffer.
  bool IsContextCleanForFrameCapture() override {
    return !mCapturedFrameInvalidated;
  }
  void MarkContextClean() override { mInvalidated = false; }
  void MarkContextCleanForFrameCapture() override {
    mCapturedFrameInvalidated = false;
  }

  void OnMemoryPressure() override;
  NS_IMETHOD
  SetContextOptions(JSContext* cx, JS::Handle<JS::Value> options,
                    ErrorResult& aRvForDictionaryInit) override;
  NS_IMETHOD
  SetDimensions(int32_t width, int32_t height) override;
  bool UpdateWebRenderCanvasData(
      nsDisplayListBuilder* aBuilder,
      layers::WebRenderCanvasData* aCanvasData) override;

  bool UpdateCompositableHandle(LayerTransactionChild* aLayerTransaction,
                                CompositableHandle aHandle);

  // ------

  int32_t GetWidth() override { return AutoAssertCast(DrawingBufferSize().x); }
  int32_t GetHeight() override { return AutoAssertCast(DrawingBufferSize().y); }

  NS_IMETHOD InitializeWithDrawTarget(nsIDocShell*,
                                      NotNull<gfx::DrawTarget*>) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  NS_IMETHOD Reset() override {
    /* (InitializeWithSurface) */
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  UniquePtr<uint8_t[]> GetImageBuffer(int32_t* out_format) override;
  NS_IMETHOD GetInputStream(const char* mimeType,
                            const nsAString& encoderOptions,
                            nsIInputStream** out_stream) override;

  already_AddRefed<mozilla::gfx::SourceSurface> GetSurfaceSnapshot(
      gfxAlphaType* out_alphaType) override;

  void SetOpaqueValueFromOpaqueAttr(bool) override{};
  bool GetIsOpaque() override { return !mInitialOptions->alpha; }

  NS_IMETHOD SetIsIPC(bool) override { return NS_ERROR_NOT_IMPLEMENTED; }

  /**
   * An abstract base class to be implemented by callers wanting to be notified
   * that a refresh has occurred. Callers must ensure an observer is removed
   * before it is destroyed.
   */
  void DidRefresh() override;

  NS_IMETHOD Redraw(const gfxRect&) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  // ------

  void Invalidate();

 protected:
  layers::LayersBackend GetCompositorBackendType() const;

  bool mInvalidated = false;
  bool mCapturedFrameInvalidated = false;

  // -------------------------------------------------------------------------
  // WebGLRenderingContext Basic Properties and Methods
  // -------------------------------------------------------------------------
 public:
  dom::HTMLCanvasElement* GetCanvas() const { return mCanvasElement; }
  void Commit();
  void GetCanvas(
      dom::Nullable<dom::OwningHTMLCanvasElementOrOffscreenCanvas>& retval);

  GLsizei DrawingBufferWidth() {
    const FuncScope funcScope(*this, "drawingBufferWidth");
    return AutoAssertCast(DrawingBufferSize().x);
  }
  GLsizei DrawingBufferHeight() {
    const FuncScope funcScope(*this, "drawingBufferHeight");
    return AutoAssertCast(DrawingBufferSize().y);
  }
  void GetContextAttributes(dom::Nullable<dom::WebGLContextAttributes>& retval);

  void Present();

 private:
  const uvec2& DrawingBufferSize();
  bool HasAlphaSupport() { return mSurfaceInfo.supportsAlpha; }

  ICRData mSurfaceInfo;

  void AfterDrawCall() {
    if (!mNotLost) return;
    const auto& state = *(mNotLost->generation);
    const bool isBackbuffer = !state.mBoundDrawFb;
    if (isBackbuffer) {
      Invalidate();
    }
  }

  // -------------------------------------------------------------------------
  // Client-side helper methods.  Dispatch to a Host method.
  // -------------------------------------------------------------------------

  // ------------------------- GL State -------------------------
 public:
  bool IsContextLost() const;

  void Disable(GLenum cap);

  void Enable(GLenum cap);

  bool IsEnabled(GLenum cap);

  void GetParameter(JSContext* cx, GLenum pname,
                    JS::MutableHandle<JS::Value> retval, ErrorResult& rv);

  void GetBufferParameter(JSContext* cx, GLenum target, GLenum pname,
                          JS::MutableHandle<JS::Value> retval);

  void GetFramebufferAttachmentParameter(JSContext* cx, GLenum target,
                                         GLenum attachment, GLenum pname,
                                         JS::MutableHandle<JS::Value> retval,
                                         ErrorResult& rv);

  void GetRenderbufferParameter(JSContext* cx, GLenum target, GLenum pname,
                                JS::MutableHandle<JS::Value> retval);

  void GetIndexedParameter(JSContext* cx, GLenum target, GLuint index,
                           JS::MutableHandleValue retval, ErrorResult& rv);

  RefPtr<WebGLShaderPrecisionFormatJS> GetShaderPrecisionFormat(
      GLenum shadertype, GLenum precisiontype) const;

  void UseProgram(const WebGLProgramJS&);
  void ValidateProgram(const WebGLProgramJS&) const;

  // -

  RefPtr<WebGLBufferJS> CreateBuffer() const;
  RefPtr<WebGLFramebufferJS> CreateFramebuffer() const;
  RefPtr<WebGLProgramJS> CreateProgram() const;
  RefPtr<WebGLQueryJS> CreateQuery() const;
  RefPtr<WebGLRenderbufferJS> CreateRenderbuffer() const;
  RefPtr<WebGLSamplerJS> CreateSampler() const;
  RefPtr<WebGLShaderJS> CreateShader(GLenum type) const;
  RefPtr<WebGLSyncJS> FenceSync(GLenum condition, GLbitfield flags) const;
  RefPtr<WebGLTextureJS> CreateTexture() const;
  RefPtr<WebGLTransformFeedbackJS> CreateTransformFeedback() const;
  RefPtr<WebGLVertexArrayJS> CreateVertexArray() const;

  void DeleteBuffer(WebGLBufferJS*);
  void DeleteFramebuffer(WebGLFramebufferJS*);
  void DeleteProgram(WebGLProgramJS*);
  void DeleteQuery(WebGLQueryJS*) const;
  void DeleteRenderbuffer(WebGLRenderbufferJS*);
  void DeleteSampler(WebGLSamplerJS*) const;
  void DeleteShader(WebGLShaderJS*) const;
  void DeleteSync(WebGLSyncJS*) const;
  void DeleteTexture(WebGLTextureJS*);
  void DeleteTransformFeedback(WebGLTransformFeedbackJS*) const;
  void DeleteVertexArray(WebGLVertexArrayJS*) const;

  // -

  bool IsBuffer(const WebGLBufferJS*) const;
  bool IsFramebuffer(const WebGLFramebufferJS*) const;
  bool IsProgram(const WebGLProgramJS*) const;
  bool IsQuery(const WebGLQueryJS*) const;
  bool IsRenderbuffer(const WebGLRenderbufferJS*) const;
  bool IsSampler(const WebGLSamplerJS*) const;
  bool IsShader(const WebGLShaderJS*) const;
  bool IsSync(const WebGLSyncJS*) const;
  bool IsTexture(const WebGLTextureJS*) const;
  bool IsTransformFeedback(const WebGLTransformFeedbackJS*) const;
  bool IsVertexArray(const WebGLVertexArrayJS*) const;

  // -
  // WebGLProgramJS

 private:
  const webgl::LinkResult& GetLinkResult(const WebGLProgramJS&) const;

 public:
  void AttachShader(WebGLProgramJS&, const WebGLShaderJS&) const;
  void BindAttribLocation(WebGLProgramJS&, GLuint location, const nsAString& name) const;
  void DetachShader(WebGLProgramJS&, const WebGLShaderJS&) const;
  void GetAttachedShaders(const WebGLProgramJS&,
      dom::Nullable<nsTArray<RefPtr<WebGLShaderJS>>>& retval) const;
  void LinkProgram(WebGLProgramJS&) const;
  void TransformFeedbackVaryings(WebGLProgramJS&, const dom::Sequence<nsString>& varyings,
                                 GLenum bufferMode) const;
  void UniformBlockBinding(WebGLProgramJS&, GLuint blockIndex,
                           GLuint blockBinding) const;

  // Link result reflection
  RefPtr<WebGLActiveInfoJS> GetActiveAttrib(const WebGLProgramJS&, GLuint index) const;
  RefPtr<WebGLActiveInfoJS> GetActiveUniform(const WebGLProgramJS&, GLuint index) const;
  void GetActiveUniformBlockName(const WebGLProgramJS&, GLuint uniformBlockIndex,
                                 nsAString& retval) const;
  void GetActiveUniformBlockParameter(JSContext* cx, const WebGLProgramJS&,
                                      GLuint uniformBlockIndex, GLenum pname,
                                      JS::MutableHandle<JS::Value> retval,
                                      ErrorResult& rv) const;
  void GetActiveUniforms(JSContext*, const WebGLProgramJS&,
                         const dom::Sequence<GLuint>& uniformIndices,
                         GLenum pname, JS::MutableHandle<JS::Value> retval) const;
  GLint GetAttribLocation(const WebGLProgramJS&, const nsAString& name) const;
  GLint GetFragDataLocation(const WebGLProgramJS&, const nsAString& name) const;
  void GetProgramInfoLog(const WebGLProgramJS& prog, nsAString& retval) const;
  void GetProgramParameter(JSContext*, const WebGLProgram&, GLenum pname,
                           JS::MutableHandle<JS::Value> retval) const;
  RefPtr<WebGLActiveInfoJS> GetTransformFeedbackVarying(
      const WebGLProgramJS&, GLuint index) const;
  GLuint GetUniformBlockIndex(const WebGLProgramJS&, const nsAString& uniformBlockName) const;
  void GetUniformIndices(const WebGLProgramJS&,
                         const dom::Sequence<nsString>& uniformNames,
                         dom::Nullable<nsTArray<GLuint>>& retval) const;

  // WebGLUniformLocationJS
  RefPtr<WebGLUniformLocationJS> GetUniformLocation(const WebGLProgramJS&,
           const nsAString& name) const;
  void GetUniform(JSContext*, const WebGLProgramJS&, const WebGLUniformLocationJS&,
                  JS::MutableHandle<JS::Value> retval) const;

  // -
  // WebGLShaderJS

 private:
  const webgl::CompileResult& GetShaderResult(const WebGLShaderJS&) const;

 public:
  void CompileShader(WebGLShaderJS&) const;
  void GetShaderInfoLog(const WebGLShaderJS&, nsAString& retval) const;
  void GetShaderParameter(JSContext*, const WebGLShaderJS&,
                          GLenum pname, JS::MutableHandle<JS::Value> retval) const;
  void GetShaderSource(const WebGLShaderJS&, nsAString& retval) const;
  void GetTranslatedShaderSource(const WebGLShaderJS& shader, nsAString& retval) const;
  void ShaderSource(WebGLShaderJS&, const nsAString&) const;

  // -

  void BindFramebuffer(GLenum target, WebGLFramebufferJS*);

  void BlendColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a);

  // -

  void BlendEquation(GLenum mode) {
    BlendEquationSeparate(mode, mode);
  }
  void BlendFunc(GLenum sfactor, GLenum dfactor) {
    BlendFuncSeparate(sfactor, dfactor, sfactor, dfactor);
  }

  void BlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha);
  void BlendFuncSeparate(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha,
                         GLenum dstAlpha);

  // -

  GLenum CheckFramebufferStatus(GLenum target);

  void Clear(GLbitfield mask);

  // -

 private:
  void ClearBufferTv(GLenum buffer, GLint drawBuffer, webgl::AttribBaseType,
      const Range<const uint8_t>& view) const;

  template<typename T>
  void ClearBufferTv(GLenum buffer, GLint drawBuffer, const webgl::AttribBaseType type,
        const Range<T>& view, GLuint srcElemOffset) const {
    static_assert(sizeof(T) == 4);
    #error subrange
    ClearBufferv(buffer, drawBuffer, type, Range<const uint8_t>{view}, srcElemOffset);
  }

 public:
  void ClearBufferfv(GLenum buffer, GLint drawBuffer, const Float32ListU& list,
                     GLuint srcElemOffset) const {
    ClearBufferTv(buffer, drawBuffer, webgl::AttribBaseType::Float, MakeRange(list), srcElemOffset);
  }
  void ClearBufferiv(GLenum buffer, GLint drawBuffer, const Int32ListU& list,
                     GLuint srcElemOffset) const {
    ClearBufferTv(buffer, drawBuffer, webgl::AttribBaseType::Int, MakeRange(list), srcElemOffset);
  }
  void ClearBufferuiv(GLenum buffer, GLint drawBuffer, const Uint32ListU& list,
                      GLuint srcElemOffset) const {
    ClearBufferTv(buffer, drawBuffer, webgl::AttribBaseType::UInt, MakeRange(list), srcElemOffset);
  }

  // -

  void ClearBufferfi(GLenum buffer, GLint drawBuffer, GLfloat depth,
                     GLint stencil) const;

  void ClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a);

  void ClearDepth(GLclampf v);

  void ClearStencil(GLint v);

  void ColorMask(WebGLboolean r, WebGLboolean g, WebGLboolean b,
                 WebGLboolean a);

  void CullFace(GLenum face);

  void DepthFunc(GLenum func);

  void DepthMask(WebGLboolean b);

  void DepthRange(GLclampf zNear, GLclampf zFar);

  void Flush();

  void Finish();

  void FrontFace(GLenum mode);

  GLenum GetError();

  void Hint(GLenum target, GLenum mode);

  void LineWidth(GLfloat width);


  void PixelStorei(GLenum pname, GLint param);

  void PolygonOffset(GLfloat factor, GLfloat units);

  void SampleCoverage(GLclampf value, WebGLboolean invert);

  void Scissor(GLint x, GLint y, GLsizei width, GLsizei height);

  // -

  void StencilFunc(GLenum func, GLint ref, GLuint mask) {
    StencilFuncSeparate(LOCAL_GL_FRONT_AND_BACK, func, ref, mask);
  }
  void StencilMask(GLuint mask) {
    StencilMaskSeparate(LOCAL_GL_FRONT_AND_BACK, mask);
  }
  void StencilOp(GLenum sfail, GLenum dpfail, GLenum dppass) {
    StencilOpSeparate(LOCAL_GL_FRONT_AND_BACK, sfail, dpfail, dppass);
  }

  void StencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask);
  void StencilMaskSeparate(GLenum face, GLuint mask);
  void StencilOpSeparate(GLenum face, GLenum sfail, GLenum dpfail,
                         GLenum dppass);

  // -

  void Viewport(GLint x, GLint y, GLsizei width, GLsizei height);

  // ------------------------- Buffer Objects -------------------------
 public:

  void BindBuffer(GLenum target, const WebGLBufferJS*);

  // -

 private:
  void BindBufferRangeImpl(const GLenum target, const GLuint index,
                       const WebGLBufferJS* const buffer, const uint64_t offset,
                       const uint64_t size);
 public:
  void BindBufferBase(const GLenum target, const GLuint index,
                      const WebGLBufferJS* const buffer) {
    const FuncScope funcScope(*this, "bindBufferBase");
    if (IsContextLost()) return;

    BindBufferRangeImpl(target, index, buffer, 0, -1);
  }

  void BindBufferRange(const GLenum target, const GLuint index,
                       const WebGLBufferJS* const buffer, const WebGLintptr offset,
                       const WebGLsizeiptr size) {
    const FuncScope funcScope(*this, "bindBufferRange");
    if (IsContextLost()) return;

    if (buffer) {
      if (!ValidateNonNegative("offset", offset)) return;
      if (!ValidateNonNegative("size", size)) return;
    }

    BindBufferRangeImpl(target, index, buffer, static_cast<uint64_t>(offset),
        static_cast<uint64_t>(size));
  }

  // -

  void CopyBufferSubData(GLenum readTarget, GLenum writeTarget,
                         GLintptr readOffset, GLintptr writeOffset,
                         GLsizeiptr size);

  void BufferData(GLenum target, WebGLsizeiptr size, GLenum usage);
  void BufferData(GLenum target,
                  const dom::Nullable<dom::ArrayBuffer>& maybeSrc,
                  GLenum usage);
  void BufferData(GLenum target, const dom::ArrayBufferView& srcData,
                  GLenum usage, GLuint srcElemOffset = 0,
                  GLuint srcElemCountOverride = 0);

  void BufferSubData(GLenum target, WebGLsizeiptr dstByteOffset,
                     const dom::ArrayBufferView& src, GLuint srcElemOffset = 0,
                     GLuint srcElemCountOverride = 0);
  void BufferSubData(GLenum target, WebGLsizeiptr dstByteOffset,
                     const dom::ArrayBuffer& src);

  void GetBufferSubData(GLenum target, GLintptr srcByteOffset,
                        const dom::ArrayBufferView& dstData,
                        GLuint dstElemOffset, GLuint dstElemCountOverride);

  // -------------------------- Framebuffer Objects --------------------------

  void BlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                       GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                       GLbitfield mask, GLenum filter);

  void FramebufferRenderbuffer(GLenum target, GLenum attachEnum,
                               GLenum rbTarget,
                               const WebGLRenderbufferJS&) const;
  void FramebufferTexture2D(GLenum target, GLenum attachEnum,
                            GLenum texImageTarget,
                            const WebGLTextureJS&, GLint mipLevel) const;
  void FramebufferTextureLayer(GLenum target, GLenum attachment,
                               const WebGLTextureJS&,
                               GLint mipLevel, GLint zLayer) const;
  void FramebufferTextureMultiview(GLenum target, GLenum attachEnum,
                                   const WebGLTextureJS*,
                                   GLint mipLevel, GLint zLayerBase,
                                   GLsizei numViewLayers) const;

  void InvalidateFramebuffer(GLenum target,
                             const dom::Sequence<GLenum>& attachments,
                             ErrorResult& unused);
  void InvalidateSubFramebuffer(GLenum target,
                                const dom::Sequence<GLenum>& attachments,
                                GLint x, GLint y, GLsizei width, GLsizei height,
                                ErrorResult& unused);

  void ReadBuffer(GLenum mode);

  // ----------------------- Renderbuffer objects -----------------------
  void GetInternalformatParameter(JSContext* cx, GLenum target,
                                  GLenum internalformat, GLenum pname,
                                  JS::MutableHandleValue retval,
                                  ErrorResult& rv) const;

  void BindRenderbuffer(GLenum target, const WebGLRenderbufferJS*);

  void RenderbufferStorage(GLenum target, GLenum internalFormat, GLsizei width,
                           GLsizei height) const {
    RenderbufferStorageMultisample(target, 0, internalFormat, width, height);
  }

  void RenderbufferStorageMultisample(GLenum target, GLsizei samples,
                                      GLenum internalFormat, GLsizei width,
                                      GLsizei height) const;

  // --------------------------- Texture objects ---------------------------

  void ActiveTexture(GLenum texUnit);

  void BindTexture(GLenum texTarget, WebGLTextureJS*);


  void GenerateMipmap(GLenum texTarget) const;

  void GetTexParameter(JSContext* cx, GLenum texTarget, GLenum pname,
                       JS::MutableHandle<JS::Value> retval) const;

  void TexParameterf(GLenum texTarget, GLenum pname, GLfloat param);
  void TexParameteri(GLenum texTarget, GLenum pname, GLint param);

  // -

 private:
  void TexStorage(uint8_t funcDims, GLenum target, GLsizei levels, GLenum internalFormat,
                    ivec3 size) const;

 public:
  void TexStorage2D(GLenum target, GLsizei levels, GLenum internalFormat,
                    GLsizei width, GLsizei height) const {
    TexStorage(2, target, levels, internalFormat, {width, height, 1});
  }

  void TexStorage3D(GLenum target, GLsizei levels, GLenum internalFormat,
                    GLsizei width, GLsizei height, GLsizei depth) const {
    TexStorage(3, target, levels, internalFormat, {width, height, depth});
  }

  ////////////////////////////////////

  template <typename T>
  void TexImage2D(GLenum target, GLint level, GLenum internalFormat,
                  GLenum unpackFormat, GLenum unpackType, const T& src,
                  ErrorResult& out_error) {
    GLsizei width = 0;
    GLsizei height = 0;
    GLint border = 0;
    TexImage(2, target, level, internalFormat, {width, height, 1}, border,
               unpackFormat, unpackType, src, out_error);
  }

  template <typename T>
  void TexImage2D(GLenum target, GLint level, GLenum internalFormat,
                  GLsizei width, GLsizei height, GLint border,
                  GLenum unpackFormat, GLenum unpackType, const T& anySrc,
                  ErrorResult& out_error) {
    const TexImageSourceAdapter src(&anySrc, &out_error);
    TexImage(2, target, level, internalFormat, {width, height, 1}, border,
               unpackFormat, unpackType, src);
  }

  void TexImage2D(GLenum target, GLint level, GLenum internalFormat,
                  GLsizei width, GLsizei height, GLint border,
                  GLenum unpackFormat, GLenum unpackType,
                  const dom::ArrayBufferView& view, GLuint viewElemOffset,
                  ErrorResult&) {
    const TexImageSourceAdapter src(&view, viewElemOffset);
    TexImage(2, target, level, internalFormat, {width, height, 1}, border,
               unpackFormat, unpackType, src);
  }

  ////////////////////////////////////

 public:
  template <typename T>
  void TexSubImage2D(GLenum target, GLint level, GLint xOffset, GLint yOffset,
                     GLenum unpackFormat, GLenum unpackType, const T& src,
                     ErrorResult& out_error) {
    GLsizei width = 0;
    GLsizei height = 0;
    TexSubImage(2, target, level, {xOffset, yOffset, 0}, {width, height, 1}, unpackFormat,
                  unpackType, src, out_error);
  }

  template <typename T>
  void TexSubImage2D(GLenum target, GLint level, GLint xOffset, GLint yOffset,
                     GLsizei width, GLsizei height, GLenum unpackFormat,
                     GLenum unpackType, const T& anySrc,
                     ErrorResult& out_error) {
    const TexImageSourceAdapter src(&anySrc, &out_error);
    TexSubImage(2, target, level, {xOffset, yOffset, 0}, {width, height, 1}, unpackFormat,
                  unpackType, src);
  }

  void TexSubImage2D(GLenum target, GLint level, GLint xOffset, GLint yOffset,
                     GLsizei width, GLsizei height, GLenum unpackFormat,
                     GLenum unpackType, const dom::ArrayBufferView& view,
                     GLuint viewElemOffset, ErrorResult&) {
    const TexImageSourceAdapter src(&view, viewElemOffset);
    TexSubImage(2, target, level, {xOffset, yOffset, 0}, {width, height, 1}, unpackFormat,
                  unpackType, src);
  }

  ////////////////////////////////////

 public:
  template <typename T>
  void TexImage3D(GLenum target, GLint level, GLenum internalFormat,
                  GLsizei width, GLsizei height, GLsizei depth, GLint border,
                  GLenum unpackFormat, GLenum unpackType, const T& anySrc,
                  ErrorResult& out_error) {
    const TexImageSourceAdapter src(&anySrc, &out_error);
    TexImage(3, target, level, internalFormat, {width, height, depth}, border,
               unpackFormat, unpackType, src);
  }

  void TexImage3D(GLenum target, GLint level, GLenum internalFormat,
                  GLsizei width, GLsizei height, GLsizei depth, GLint border,
                  GLenum unpackFormat, GLenum unpackType,
                  const dom::ArrayBufferView& view, GLuint viewElemOffset,
                  ErrorResult&) {
    const TexImageSourceAdapter src(&view, viewElemOffset);
    TexImage(3, target, level, internalFormat, {width, height, depth}, border,
               unpackFormat, unpackType, src);
  }

  ////////////////////////////////////

 public:
  template <typename T>
  void TexSubImage3D(GLenum target, GLint level, GLint xOffset, GLint yOffset,
                     GLint zOffset, GLsizei width, GLsizei height,
                     GLsizei depth, GLenum unpackFormat, GLenum unpackType,
                     const T& anySrc, ErrorResult& out_error) {
    const TexImageSourceAdapter src(&anySrc, &out_error);
    TexSubImage(3, target, level, {xOffset, yOffset, zOffset}, {width, height,
                  depth}, unpackFormat, unpackType, src);
  }

  void TexSubImage3D(GLenum target, GLint level, GLint xOffset, GLint yOffset,
                     GLint zOffset, GLsizei width, GLsizei height,
                     GLsizei depth, GLenum unpackFormat, GLenum unpackType,
                     const dom::Nullable<dom::ArrayBufferView>& maybeSrcView,
                     GLuint srcElemOffset, ErrorResult&) {
    const TexImageSourceAdapter src(&maybeSrcView, srcElemOffset);
    TexSubImage(3, target, level, {xOffset, yOffset, zOffset}, {width, height,
                  depth}, unpackFormat, unpackType, src);
  }

  ////////////////////////////////////

 public:
  void CompressedTexImage2D(GLenum target, GLint level, GLenum internalFormat,
                            GLsizei width, GLsizei height, GLint border,
                            GLsizei imageSize, WebGLsizeiptr offset) {
    const FuncScope scope(this, FuncScopeId::compressedTexImage2D);
    const uint8_t funcDims = 2;
    const GLsizei depth = 1;
    const TexImageSourceAdapter src(&offset, 0, 0);
    CompressedTexImage(funcDims, target, level, internalFormat, {width, height,
                       depth}, border, src, Some(imageSize));
  }

  template <typename T>
  void CompressedTexImage2D(GLenum target, GLint level, GLenum internalFormat,
                            GLsizei width, GLsizei height, GLint border,
                            const T& anySrc, GLuint viewElemOffset = 0,
                            GLuint viewElemLengthOverride = 0) {
    const FuncScope scope(this, FuncScopeId::compressedTexImage2D);
    const uint8_t funcDims = 2;
    const GLsizei depth = 1;
    const TexImageSourceAdapter src(&anySrc, viewElemOffset,
                                    viewElemLengthOverride);
    CompressedTexImage(funcDims, target, level, internalFormat, {width, height,
                       depth}, border, src, Nothing());
  }

  void CompressedTexSubImage2D(GLenum target, GLint level, GLint xOffset,
                               GLint yOffset, GLsizei width, GLsizei height,
                               GLenum unpackFormat, GLsizei imageSize,
                               WebGLsizeiptr offset) {
    const FuncScope scope(this, FuncScopeId::compressedTexSubImage2D);
    const uint8_t funcDims = 2;
    const GLint zOffset = 0;
    const GLsizei depth = 1;
    const TexImageSourceAdapter src(&offset, 0, 0);
    CompressedTexSubImage(funcDims, target, level, {xOffset, yOffset, zOffset},
                          {width, height, depth}, unpackFormat, src,
                          Some(imageSize));
  }

  template <typename T>
  void CompressedTexSubImage2D(GLenum target, GLint level, GLint xOffset,
                               GLint yOffset, GLsizei width, GLsizei height,
                               GLenum unpackFormat, const T& anySrc,
                               GLuint viewElemOffset = 0,
                               GLuint viewElemLengthOverride = 0) {
    const FuncScope scope(this, FuncScopeId::compressedTexSubImage2D);
    const uint8_t funcDims = 2;
    const GLint zOffset = 0;
    const GLsizei depth = 1;
    const TexImageSourceAdapter src(&anySrc, viewElemOffset,
                                    viewElemLengthOverride);
    CompressedTexSubImage(funcDims, target, level, {xOffset, yOffset, zOffset},
                          {width, height, depth}, unpackFormat, src, Nothing());
  }

  ////////////////////////////////////

 public:
  void CompressedTexImage3D(GLenum target, GLint level, GLenum internalFormat,
                            GLsizei width, GLsizei height, GLsizei depth,
                            GLint border, GLsizei imageSize,
                            WebGLintptr offset) {
    const FuncScope scope(this, FuncScopeId::compressedTexImage3D);
    const uint8_t funcDims = 3;
    const TexImageSourceAdapter src(&offset, 0, 0);
    CompressedTexImage(funcDims, target, level, internalFormat, {width, height,
                       depth}, border, src, Some(imageSize));
  }

  template <typename T>
  void CompressedTexImage3D(GLenum target, GLint level, GLenum internalFormat,
                            GLsizei width, GLsizei height, GLsizei depth,
                            GLint border, const T& anySrc,
                            GLuint viewElemOffset = 0,
                            GLuint viewElemLengthOverride = 0) {
    const FuncScope scope(this, FuncScopeId::compressedTexImage3D);
    const uint8_t funcDims = 3;
    const TexImageSourceAdapter src(&anySrc, viewElemOffset,
                                    viewElemLengthOverride);
    CompressedTexImage(funcDims, target, level, internalFormat, {width, height,
                       depth}, border, src, Nothing());
  }

  void CompressedTexSubImage3D(GLenum target, GLint level, GLint xOffset,
                               GLint yOffset, GLint zOffset, GLsizei width,
                               GLsizei height, GLsizei depth,
                               GLenum unpackFormat, GLsizei imageSize,
                               WebGLintptr offset) {
    const FuncScope scope(this, FuncScopeId::compressedTexSubImage3D);
    const uint8_t funcDims = 3;
    const TexImageSourceAdapter src(&offset, 0, 0);
    CompressedTexSubImage(funcDims, target, level, {xOffset, yOffset, zOffset},
                          {width, height, depth}, unpackFormat, src,
                          Some(imageSize));
  }

  template <typename T>
  void CompressedTexSubImage3D(GLenum target, GLint level, GLint xOffset,
                               GLint yOffset, GLint zOffset, GLsizei width,
                               GLsizei height, GLsizei depth,
                               GLenum unpackFormat, const T& anySrc,
                               GLuint viewElemOffset = 0,
                               GLuint viewElemLengthOverride = 0) {
    const FuncScope scope(this, FuncScopeId::compressedTexSubImage3D);
    const uint8_t funcDims = 3;
    const TexImageSourceAdapter src(&anySrc, viewElemOffset,
                                    viewElemLengthOverride);
    CompressedTexSubImage(funcDims, target, level, {xOffset, yOffset, zOffset},
                          {width, height, depth}, unpackFormat, src, Nothing());
  }

  // -

  void CopyTexSubImage2D(GLenum target, GLint level, GLint xOffset,
                         GLint yOffset, GLint x, GLint y, GLsizei width,
                         GLsizei height) const {
    CopyTexSubImage(2, target, level, {xOffset, yOffset, 0}, {x, y}, {width, height});
  }

  void CopyTexSubImage3D(GLenum target, GLint level, GLint xOffset,
                         GLint yOffset, GLint zOffset, GLint x, GLint y,
                         GLsizei width, GLsizei height) const {
    CopyTexSubImage(3, target, level, {xOffset, yOffset, zOffset}, {x, y}, {width, height});
  }

 protected:
  // Primitive tex upload functions
  void TexImage(uint8_t funcDims, GLenum target, GLint level,
                GLenum internalFormat, ivec3 size, GLint border, GLenum unpackFormat,
                GLenum unpackType, const TexImageSource& src) const;
  void TexSubImage(uint8_t funcDims, GLenum target, GLint level, ivec3 offset, ivec3 size,
                   GLenum unpackFormat, GLenum unpackType,
                   const TexImageSource& src) const;
  void CompressedTexImage(uint8_t funcDims, GLenum target, GLint level,
                          GLenum internalFormat, ivec3 size, GLint border,
                          const TexImageSource& src,
                          const Maybe<GLsizei>& expectedImageSize) const;
  void CompressedTexSubImage(uint8_t funcDims, GLenum target, GLint level,
                             ivec3 offset, ivec3 size,
                             GLenum unpackFormat, const TexImageSource& src,
                             const Maybe<GLsizei>& expectedImageSize) const;
  void CopyTexImage2D(GLenum target, GLint level, GLenum internalFormat,
                      ivec2 srcOffset, ivec2 size, GLint border) const;
  void CopyTexSubImage(uint8_t funcDims, GLenum target, GLint level, ivec3 dstOffset,
                       ivec2 srcOffset, ivec2 size) const;

  // ------------------------ Uniforms and attributes ------------------------
 public:
  void GetVertexAttrib(JSContext* cx, GLuint index, GLenum pname,
                       JS::MutableHandle<JS::Value> retval, ErrorResult& rv);

 private:
  void UniformNTv(const WebGLUniformLocationJS* const loc, uint8_t n, webgl::UniformBaseType t,
    const Range<const uint8_t>& bytes) const;

  template<typename T>
  void UniformNTv(const WebGLUniformLocationJS* const loc, uint8_t n, webgl::UniformBaseType t,
          const Range<T>& range) const {
    UniformNTV(loc, n, t, Range<const uint8_t>(range));
  }

  template<typename T, size_t N>
  static Range<const T> MakeRange(T (&arr)[N]) {
    return {arr, N};
  }

  // -

  template<typename T>
  Maybe<Range<T>> ValidateSubrange(const Range<T>& data, size_t elemOffset, size_t elemLengthOverride = 0) const {
    auto ret = data;
    if (offset > ret.length()) {
      EnqueueError(LOCAL_GL_INVALID_VALUE, "`elemOffset` too large for `data`.");
      return {};
    }
    ret = {ret.begin() + offset, ret.end()};
    if (elemLengthOverride) {
      if (elemLengthOverride > ret.length()) {
        EnqueueError(LOCAL_GL_INVALID_VALUE,
          "`elemLengthOverride` too large for `data` and `elemOffset`.");
        return {};
      }
      ret = {ret.begin().get(), elemLengthOverride};
    }
    return Some(ret);
  }

 public:

  #define _(T,Type,BaseType) \
    void Uniform1 ## T(const WebGLUniformLocationJS* const loc, Type x) const { \
      const Type arr[] = { x }; \
      UniformNTv(loc, 1, BaseType, MakeRange(arr)); \
    } \
    void Uniform2 ## T(const WebGLUniformLocationJS* const loc, Type x, \
                   Type y) const { \
      const Type arr[] = { x, y }; \
      UniformNTv(loc, 2, BaseType, MakeRange(arr)); \
    } \
    void Uniform3 ## T(const WebGLUniformLocationJS* const loc, Type x, \
                   Type y, Type z) const { \
      const Type arr[] = { x, y, z }; \
      UniformNTv(loc, 3, BaseType, MakeRange(arr)); \
    } \
    void Uniform4 ## T(const WebGLUniformLocationJS* const loc, Type x, \
                   Type y, Type z, Type w) const { \
      const Type arr[] = { x, y, z, w }; \
      UniformNTv(loc, 4, BaseType, MakeRange(arr)); \
    }

  _(f,float,webgl::UniformBaseType::Float)
  _(i,int32_t,webgl::UniformBaseType::Int)
  _(ui,uint32_t,webgl::UniformBaseType::Uint)

  #undef _

  // -

  #define _(N,T,BaseType,TypeListU) \
    void Uniform ## N ## T ## v(const WebGLUniformLocationJS* const loc, const TypeListU& list) const { \
      UniformNTv(loc, N, BaseType, MakeRange(list)); \
    }

  _(1,f,webgl::UniformBaseType::Float,Float32ListU)
  _(2,f,webgl::UniformBaseType::Float,Float32ListU)
  _(3,f,webgl::UniformBaseType::Float,Float32ListU)
  _(4,f,webgl::UniformBaseType::Float,Float32ListU)
  _(1,i,webgl::UniformBaseType::Int,Int32ListU)
  _(2,i,webgl::UniformBaseType::Int,Int32ListU)
  _(3,i,webgl::UniformBaseType::Int,Int32ListU)
  _(4,i,webgl::UniformBaseType::Int,Int32ListU)
  _(1,ui,webgl::UniformBaseType::Uint,Uint32ListU)
  _(2,ui,webgl::UniformBaseType::Uint,Uint32ListU)
  _(3,ui,webgl::UniformBaseType::Uint,Uint32ListU)
  _(4,ui,webgl::UniformBaseType::Uint,Uint32ListU)

  #undef _

  // -

  private:
  void UniformMatrixAxBfv(uint8_t a, uint8_t b, const WebGLUniformLocationJS*, bool transpose,
                          const Range<const float>&, GLuint elemOffset, GLuint elemCountOverride) const;
  public:

#define _(X, A, B)                                                           \
  void UniformMatrix##X##fv(const WebGLUniformLocationJS* loc, bool transpose, \
                            const Float32ListU& list, GLuint elemOffset = 0,   \
                            GLuint elemCountOverride = 0) const { \
    UniformMatrixAxBfv(A, B, loc, transpose, MakeRange(list), elemOffset, elemCountOverride); \
  }

  _(2, 2, 2)
  _(2x3, 2, 3)
  _(2x4, 2, 4)

  _(3x2, 3, 2)
  _(3, 3, 3)
  _(3x4, 3, 4)

  _(4x2, 4, 2)
  _(4x3, 4, 3)
  _(4, 4, 4)

#undef _

  // -

  void EnableVertexAttribArray(GLuint index);

  void DisableVertexAttribArray(GLuint index);

  WebGLsizeiptr GetVertexAttribOffset(GLuint index, GLenum pname);

  // -

private:
  void VertexAttribNTv(GLuint index, uint8_t n, webgl::AttribBaseType,
      const Range<const uint8_t>&);

public:
  void VertexAttrib1f(GLuint index, GLfloat x) {
    VertexAttrib4f(index, x, 0, 0, 1);
  }
  void VertexAttrib2f(GLuint index, GLfloat x, GLfloat y) {
    VertexAttrib4f(index, x, y, 0, 1);
  }
  void VertexAttrib3f(GLuint index, GLfloat x, GLfloat y, GLfloat z) {
    VertexAttrib4f(index, x, y, z, 1);
  }
  void VertexAttrib4f(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w) {
    const float arr[4] = {x, y, z, w};
    VertexAttribNT(index, 4, webgl::AttribBaseType::Float, arr);
  }

  // -

  void VertexAttrib1fv(const GLuint index, const Float32ListU& list) {
    VertexAttrib4fv(index, list, 1);
  }

  void VertexAttrib2fv(const GLuint index, const Float32ListU& list) {
    VertexAttrib4fv(index, list, 2);
  }

  void VertexAttrib3fv(const GLuint index, const Float32ListU& list) {
    VertexAttrib4fv(index, list, 3);
  }

  void VertexAttrib4fv(GLuint index, const Float32ListU& list, uint8_t n = 4) {
    VertexAttribNTv(index, n, webgl::AttribBaseType::Float, MakeByteRange(list));
  }
  void VertexAttribI4iv(GLuint index, const Int32ListU& list) {
    VertexAttribNTv(index, 4, webgl::AttribBaseType::Int, MakeByteRange(list));
  }
  void VertexAttribI4uiv(GLuint index, const Uint32ListU& list) {
    VertexAttribNTv(index, 4, webgl::AttribBaseType::UInt, MakeByteRange(list));
  }

  void VertexAttribI4i(GLuint index, GLint x, GLint y, GLint z, GLint w) {
    const int32_t arr[4] = {x, y, z, w};
    VertexAttribNT(index, 4, webgl::AttribBaseType::Int, MakeByteRange(arr));
  }
  void VertexAttribI4ui(GLuint index, GLuint x, GLuint y, GLuint z, GLuint w) {
    const uint32_t arr[4] = {x, y, z, w};
    VertexAttribNT(index, 4, webgl::AttribBaseType::UInt, MakeByteRange(arr));
  }

  void VertexAttribIPointer(GLuint index, GLint size, GLenum type,
                            GLsizei stride, WebGLintptr byteOffset) const;
  void VertexAttribPointer(GLuint index, GLint size, GLenum type,
                           WebGLboolean normalized, GLsizei stride,
                           WebGLintptr byteOffset) const;

  // -------------------------------- Drawing -------------------------------
 public:
  void DrawArrays(GLenum mode, GLint first, GLsizei count) {
    DrawArraysInstanced(mode, first, count, 1, FuncScopeId::drawArrays);
  }

  void DrawElements(GLenum mode, GLsizei count, GLenum type,
                    WebGLintptr byteOffset) {
    DrawElementsInstanced(mode, count, type, byteOffset, 1,
                          FuncScopeId::drawElements);
  }

  void DrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count,
                         GLenum type, WebGLintptr byteOffset) {
    const FuncScope funcScope(*this, "drawRangeElements");
    if (end < start) {
      EnqueueError(LOCAL_GL_INVALID_VALUE, "end must be >= start.");
      return;
    }
    DrawElementsInstanced(mode, count, type, byteOffset, 1,
                          FuncScopeId::drawRangeElements);
  }

  // ------------------------------ Readback -------------------------------
 public:
  void ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                  GLenum format, GLenum type,
                  const dom::Nullable<dom::ArrayBufferView>& maybeView,
                  dom::CallerType aCallerType, ErrorResult& out_error) const {
    const FuncScope funcScope(*this, "readPixels");
    if (!ValidateNonNull("pixels", maybeView)) return;
    ReadPixels(x, y, width, height, format, type, maybeView.Value(), 0,
               aCallerType, out_error);
  }

  void ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                  GLenum format, GLenum type, WebGLsizeiptr offset,
                  dom::CallerType aCallerType, ErrorResult& out_error) const;

  void ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                  GLenum format, GLenum type,
                  const dom::ArrayBufferView& dstData, GLuint dstElemOffset,
                  dom::CallerType aCallerType, ErrorResult& out_error) const;

 protected:
  bool ReadPixels_SharedPrecheck(dom::CallerType aCallerType,
                                 ErrorResult& out_error) const;

  // ------------------------------ Vertex Array ------------------------------
 public:

  void BindVertexArray(WebGLVertexArrayJS*);

  void DrawArraysInstanced(GLenum mode, GLint first, GLsizei count,
                           GLsizei primcount,
                           FuncScopeId aFuncId = FuncScopeId::drawArrays);

  void DrawElementsInstanced(
      GLenum mode, GLsizei count, GLenum type, WebGLintptr offset,
      GLsizei primcount,
      FuncScopeId aFuncId = FuncScopeId::drawElementsInstanced);

  void VertexAttribDivisor(GLuint index, GLuint divisor);

  // --------------------------------- GL Query
  // ---------------------------------
 public:
  void GetQuery(JSContext*, GLenum target, GLenum pname,
                JS::MutableHandleValue retval) const;
  void GetQueryParameter(JSContext*, const WebGLQueryJS&,
                         GLenum pname, JS::MutableHandleValue retval) const;
  void BeginQuery(GLenum target, WebGLQueryJS&) const;
  void EndQuery(GLenum target) const;
  void QueryCounter(WebGLQueryJS&, GLenum target) const;

  // -------------------------------- Sampler -------------------------------

  void GetSamplerParameter(JSContext*, const WebGLSamplerJS&,
                           GLenum pname, JS::MutableHandleValue retval) const;

  void BindSampler(GLuint unit, const WebGLSamplerJS*);
  void SamplerParameteri(const WebGLSamplerJS&, GLenum pname, GLint param) const;
  void SamplerParameterf(const WebGLSamplerJS&, GLenum pname, GLfloat param) const;

  // ------------------------------- GL Sync ---------------------------------

  GLenum ClientWaitSync(const WebGLSyncJS&, GLbitfield flags,
                        GLuint64 timeout) const;
  void GetSyncParameter(JSContext*, const WebGLSyncJS&,
                        GLenum pname, JS::MutableHandleValue retval) const;
  void WaitSync(const WebGLSyncJS&, GLbitfield flags,
                GLint64 timeout) const;

  // -------------------------- Transform Feedback ---------------------------

  void BindTransformFeedback(GLenum target, WebGLTransformFeedbackJS*);
  void BeginTransformFeedback(GLenum primitiveMode) const;
  void EndTransformFeedback() const;
  void PauseTransformFeedback() const;
  void ResumeTransformFeedback() const;

  // ------------------------------ Extensions ------------------------------
 public:
  void GetSupportedExtensions(dom::Nullable<nsTArray<nsString>>& retval,
                              dom::CallerType callerType) const;

  bool IsSupported(WebGLExtensionID, dom::CallerType callerType =
                                         dom::CallerType::NonSystem) const;

  void GetExtension(JSContext* cx, const nsAString& name,
                    JS::MutableHandle<JSObject*> retval,
                    dom::CallerType callerType, ErrorResult& rv);

 protected:
  RefPtr<ClientWebGLExtensionBase> GetExtension(WebGLExtensionID ext,
                                                dom::CallerType callerType);
  void RequestExtension(WebGLExtensionID) const;

  // ---------------------------- Misc Extensions ----------------------------
 public:
  void DrawBuffers(const dom::Sequence<GLenum>& buffers);

  void GetSupportedProfilesASTC(
      dom::Nullable<nsTArray<nsString>>& retval) const;

  void MOZDebugGetParameter(JSContext* cx, GLenum pname,
                            JS::MutableHandle<JS::Value> retval,
                            ErrorResult& rv) const {
    GetParameter(cx, pname, retval, rv, true);
  }

  // -------------------------------------------------------------------------
  // Client-side methods.  Calls in the Host are forwarded to the client.
  // -------------------------------------------------------------------------
 public:
  void JsWarning(const std::string&) const;

  // -------------------------------------------------------------------------
  // The cross-process communication mechanism
  // -------------------------------------------------------------------------
 protected:
  template <size_t command, typename... Args>
  void DispatchAsync(Args&&... aArgs) const {
    const auto& oop = *mNotLost->outOfProcess;
    PcqStatus status = oop.mCommandSource->RunAsyncCommand(command, aArgs...);
    if (!IsSuccess(status)) {
      if (status == PcqStatus::PcqOOMError) {
        JsWarning("Ran out-of-memory during WebGL IPC.");
      }
      // Not much to do but shut down.  Since this was a Pcq failure and
      // may have been catastrophic, we don't try to revive it.  Make sure to
      // post "webglcontextlost"
      MOZ_ASSERT_UNREACHABLE(
          "TODO: Make this shut down the context, actors, everything.");
    }
  }

  template <size_t command, typename ReturnType, typename... Args>
  ReturnType DispatchSync(Args&&... aArgs) const {
    const auto& oop = *mNotLost->outOfProcess;
    ReturnType returnValue;
    PcqStatus status =
        oop.mCommandSource->RunSyncCommand(command, returnValue, aArgs...);

    if (!IsSuccess(status)) {
      if (status == PcqStatus::PcqOOMError) {
        JsWarning("Ran out-of-memory during WebGL IPC.");
      }
      // Not much to do but shut down.  Since this was a Pcq failure and
      // may have been catastrophic, we don't try to revive it.  Make sure to
      // post "webglcontextlost"
      MOZ_ASSERT_UNREACHABLE(
          "TODO: Make this shut down the context, actors, everything.");
    }
    return returnValue;
  }

  template <size_t command, typename... Args>
  void DispatchVoidSync(Args&&... aArgs) const {
    const auto& oop = *mNotLost->outOfProcess;
    const auto status =
        oop.mCommandSource->RunVoidSyncCommand(command, aArgs...);
    if (!IsSuccess(status)) {
      if (status == PcqStatus::PcqOOMError) {
        JsWarning("Ran out-of-memory during WebGL IPC.");
      }
      // Not much to do but shut down.  Since this was a Pcq failure and
      // may have been catastrophic, we don't try to revive it.  Make sure to
      // post "webglcontextlost"
      MOZ_ASSERT_UNREACHABLE(
          "TODO: Make this shut down the context, actors, everything.");
    }
  }

  template <typename ReturnType>
  friend struct WebGLClientDispatcher;

  // If we are running WebGL in this process then call the HostWebGLContext
  // method directly.  Otherwise, dispatch over IPC.
  template <typename MethodType, MethodType method, typename ReturnType,
            size_t Id, typename... Args>
  ReturnType Run(Args&&... aArgs) const;

  // -------------------------------------------------------------------------
  // Helpers for DOM operations, composition, actors, etc
  // -------------------------------------------------------------------------
 public:
  WebGLPixelStore GetPixelStore() { return mPixelStore; }
  const WebGLPixelStore GetPixelStore() const { return mPixelStore; }

 protected:
  bool IsHostOOP() const { return bool{mNotLost->outOfProcess}; }

  bool ShouldResistFingerprinting() const;

  void LoseOldestWebGLContextIfLimitExceeded();
  void UpdateLastUseIndex();

  // Prepare the context for capture before compositing
  void BeginComposition();

  // Clean up the context after captured for compositing
  void EndComposition();

  mozilla::dom::Document* GetOwnerDoc() const;

  uint64_t mLastUseIndex = 0;
  bool mResetLayer = true;
  Maybe<const WebGLContextOptions> mInitialOptions;
  WebGLPixelStore mPixelStore;
};

template <typename WebGLObjectType>
JS::Value ClientWebGLContext::WebGLObjectAsJSValue(
    JSContext* cx, RefPtr<WebGLObjectType>&& object, ErrorResult& rv) const {
  if (!object) return JS::NullValue();

  MOZ_ASSERT(this == object->GetParentObject());
  JS::Rooted<JS::Value> v(cx);
  JS::Rooted<JSObject*> wrapper(cx, GetWrapper());
  JSAutoRealm ar(cx, wrapper);
  if (!dom::GetOrCreateDOMReflector(cx, object, &v)) {
    rv.Throw(NS_ERROR_FAILURE);
    return JS::NullValue();
  }
  return v;
}

template <typename WebGLObjectType>
JS::Value ClientWebGLContext::WebGLObjectAsJSValue(
    JSContext* cx, const WebGLObjectType* object, ErrorResult& rv) const {
  if (!object) return JS::NullValue();

  MOZ_ASSERT(this == object->GetParentObject());
  JS::Rooted<JS::Value> v(cx);
  JS::Rooted<JSObject*> wrapper(cx, GetWrapper());
  JSAutoRealm ar(cx, wrapper);
  if (!dom::GetOrCreateDOMReflector(cx, const_cast<WebGLObjectType*>(object),
                                    &v)) {
    rv.Throw(NS_ERROR_FAILURE);
    return JS::NullValue();
  }
  return v;
}

template <typename WebGLObjectType>
JSObject* ClientWebGLContext::WebGLObjectAsJSObject(
    JSContext* cx, const WebGLObjectType* object, ErrorResult& rv) const {
  JS::Value v = WebGLObjectAsJSValue(cx, object, rv);
  if (v.isNull()) return nullptr;

  return &v.toObject();
}

// used by DOM bindings in conjunction with GetParentObject
inline nsISupports* ToSupports(ClientWebGLContext* webgl) {
  return static_cast<nsICanvasRenderingContextInternal*>(webgl);
}

const char* GetExtensionName(WebGLExtensionID);

}  // namespace mozilla

#endif  // CLIENTWEBGLCONTEXT_H_

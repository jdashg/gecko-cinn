/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ClientWebGLContext.h"

#include "ClientWebGLExtensions.h"
#include "HostWebGLContext.h"
#include "mozilla/dom/WebGLContextEvent.h"
#include "mozilla/dom/WorkerCommon.h"
#include "mozilla/EnumeratedRange.h"
#include "mozilla/ipc/Shmem.h"
#include "mozilla/layers/CompositorBridgeChild.h"
#include "mozilla/layers/ImageBridgeChild.h"
#include "mozilla/layers/LayerTransactionChild.h"
#include "mozilla/layers/OOPCanvasRenderer.h"
#include "mozilla/layers/TextureClientSharedSurface.h"
#include "mozilla/StaticPrefs_webgl.h"
#include "nsIGfxInfo.h"
#include "TexUnpackBlob.h"
#include "WebGLMethodDispatcher.h"
#include "WebGLChild.h"

namespace mozilla {

bool webgl::ObjectJS::IsUsable(const ClientWebGLContext& context) const {
  const auto& notLost = context.mNotLost;
  if (!notLost) return false;
  if (notLost->generation.get() != mGeneration.get()) return false;
  return !IsDeleted();
}

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_0(ClientWebGLRefCount)

NS_IMPL_CYCLE_COLLECTION_ROOT_NATIVE(ClientWebGLRefCount, AddRef)
NS_IMPL_CYCLE_COLLECTION_UNROOT_NATIVE(ClientWebGLRefCount, Release)

static bool GetJSScalarFromGLType(GLenum type,
                                  js::Scalar::Type* const out_scalarType) {
  switch (type) {
    case LOCAL_GL_BYTE:
      *out_scalarType = js::Scalar::Int8;
      return true;

    case LOCAL_GL_UNSIGNED_BYTE:
      *out_scalarType = js::Scalar::Uint8;
      return true;

    case LOCAL_GL_SHORT:
      *out_scalarType = js::Scalar::Int16;
      return true;

    case LOCAL_GL_HALF_FLOAT:
    case LOCAL_GL_HALF_FLOAT_OES:
    case LOCAL_GL_UNSIGNED_SHORT:
    case LOCAL_GL_UNSIGNED_SHORT_4_4_4_4:
    case LOCAL_GL_UNSIGNED_SHORT_5_5_5_1:
    case LOCAL_GL_UNSIGNED_SHORT_5_6_5:
      *out_scalarType = js::Scalar::Uint16;
      return true;

    case LOCAL_GL_UNSIGNED_INT:
    case LOCAL_GL_UNSIGNED_INT_2_10_10_10_REV:
    case LOCAL_GL_UNSIGNED_INT_5_9_9_9_REV:
    case LOCAL_GL_UNSIGNED_INT_10F_11F_11F_REV:
    case LOCAL_GL_UNSIGNED_INT_24_8:
      *out_scalarType = js::Scalar::Uint32;
      return true;
    case LOCAL_GL_INT:
      *out_scalarType = js::Scalar::Int32;
      return true;

    case LOCAL_GL_FLOAT:
      *out_scalarType = js::Scalar::Float32;
      return true;

    default:
      return false;
  }
}

ClientWebGLContext::ClientWebGLContext(const bool webgl2)
    : mIsWebGL2(webgl2),
      mExtLoseContext(new ClientWebGLExtensionLoseContext(this)) {}

ClientWebGLContext::~ClientWebGLContext() { RemovePostRefreshObserver(); }

bool ClientWebGLContext::UpdateCompositableHandle(
    LayerTransactionChild* aLayerTransaction, CompositableHandle aHandle) {
  // When running OOP WebGL (i.e. when we have a WebGLChild actor), tell the
  // host about the new compositable.  When running in-process, we don't need to
  // care.
  if (mNotLost->outOfProcess) {
    WEBGL_BRIDGE_LOGI("[%p] Setting CompositableHandle to %" PRIx64, this,
                      aHandle.Value());
    return mNotLost->outOfProcess->mWebGLChild->SendUpdateCompositableHandle(
        aLayerTransaction, aHandle);
  }
  return true;
}

void ClientWebGLContext::JsWarning(const std::string& utf8) const {
  if (!mCanvasElement) {
    return;
  }
  dom::AutoJSAPI api;
  if (!api.Init(mCanvasElement->OwnerDoc()->GetScopeObject())) {
    return;
  }
  const auto& cx = api.cx();
  JS::WarnUTF8(cx, "%s", utf8.c_str());
}

// ---------

bool ClientWebGLContext::DispatchEvent(const nsAString& eventName) const {
  const auto kCanBubble = CanBubble::eYes;
  const auto kIsCancelable = Cancelable::eYes;
  bool useDefaultHandler;

  if (mCanvasElement) {
    nsContentUtils::DispatchTrustedEvent(
        mCanvasElement->OwnerDoc(), static_cast<nsIContent*>(mCanvasElement),
        eventName, kCanBubble, kIsCancelable, &useDefaultHandler);
  } else {
    // OffscreenCanvas case
    RefPtr<Event> event = new Event(mOffscreenCanvas, nullptr, nullptr);
    event->InitEvent(eventName, kCanBubble, kIsCancelable);
    event->SetTrusted(true);
    useDefaultHandler = mOffscreenCanvas->DispatchEvent(
        *event, CallerType::System, IgnoreErrors());
  }
  return useDefaultHandler;
}

// -

void ClientWebGLContext::OnContextLoss(const webgl::ContextLossReason reason) {
  MOZ_ASSERT(NS_IsMainThread());
  mNotLost = {};  // Lost now!

  switch (reason) {
    case webgl::ContextLossReason::Guilty:
      mLossStatus = webgl::LossStatus::LostForever;
      break;

    case webgl::ContextLossReason::None:
      mLossStatus = webgl::LossStatus::Lost;
      break;

    case webgl::ContextLossReason::Manual:
      mLossStatus = webgl::LossStatus::LostManually;
      break;
  }

  const auto weak = WeakPtr<ClientWebGLContext>(this);
  const auto fnRun = [weak]() {
    const auto strong = RefPtr<ClientWebGLContext>(weak);
    if (!strong) return;
    strong->Event_webglcontextlost();
  };
  already_AddRefed<mozilla::Runnable> runnable =
      NS_NewRunnableFunction("enqueue Event_webglcontextlost", fnRun);
  NS_DispatchToCurrentThread(std::move(runnable));
}

void ClientWebGLContext::Event_webglcontextlost() {
  WEBGL_BRIDGE_LOGD("[%p] Posting webglcontextlost event", this);
  const bool useDefaultHandler =
      DispatchEvent(NS_LITERAL_STRING("webglcontextlost"));
  if (useDefaultHandler) {
    mLossStatus = webgl::LossStatus::LostForever;
  }

  if (mLossStatus != webgl::LossStatus::Lost) return;

  RestoreContext();
}

void ClientWebGLContext::RestoreContext() {
  MOZ_RELEASE_ASSERT(mLossStatus == webgl::LossStatus::Lost ||
                     mLossStatus == webgl::LossStatus::LostManually);

  const auto weak = WeakPtr<ClientWebGLContext>(this);
  const auto fnRun = [weak]() {
    const auto strong = RefPtr<ClientWebGLContext>(weak);
    if (!strong) return;
    strong->Event_webglcontextrestored();
  };
  already_AddRefed<mozilla::Runnable> runnable =
      NS_NewRunnableFunction("enqueue Event_webglcontextrestored", fnRun);
  NS_DispatchToCurrentThread(std::move(runnable));
}

void ClientWebGLContext::Event_webglcontextrestored() {
  mLossStatus = webgl::LossStatus::Ready;
  if (!CreateHostContext()) {
    mLossStatus = webgl::LossStatus::LostForever;
    return;
  }

  WEBGL_BRIDGE_LOGD("[%p] Posting webglcontextrestored event", this);
  (void)DispatchEvent(NS_LITERAL_STRING("webglcontextrestored"));
}

// ---------

void ClientWebGLContext::ThrowEvent_WebGLContextCreationError(
    const std::string& text) const {
  nsCString msg;
  msg.AppendPrintf("Failed to create WebGL context: %s", text.c_str());
  JsWarning(msg.BeginReading());

  RefPtr<EventTarget> target = mCanvasElement;
  if (!target && mOffscreenCanvas) {
    target = mOffscreenCanvas;
  } else if (!target) {
    return;
  }

  WEBGL_BRIDGE_LOGD("[%p] Posting webglcontextcreationerror event", this);
  const auto kEventName = NS_LITERAL_STRING("webglcontextcreationerror");

  dom::WebGLContextEventInit eventInit;
  // eventInit.mCancelable = true; // The spec says this, but it's silly.
  eventInit.mStatusMessage = NS_ConvertASCIItoUTF16(text.c_str());

  const RefPtr<WebGLContextEvent> event =
      WebGLContextEvent::Constructor(target, kEventName, eventInit);
  event->SetTrusted(true);

  target->DispatchEvent(*event);
}

// ---

// Dispatch a command to the host, using data in WebGLMethodDispatcher for
// information: e.g. to choose the right synchronization protocol.
template <typename ReturnType>
struct WebGLClientDispatcher {
  // non-const method
  template <size_t Id, typename... MethodArgs, typename... GivenArgs>
  static ReturnType Run(const ClientWebGLContext& c,
                        ReturnType (HostWebGLContext::*method)(MethodArgs...),
                        GivenArgs&&... aArgs) {
    // Non-void calls must be sync, otherwise what would we return?
    MOZ_ASSERT(WebGLMethodDispatcher::SyncType<Id>() == CommandSyncType::SYNC);
    return c.DispatchSync<Id, ReturnType>(
        static_cast<const MethodArgs&>(aArgs)...);
  }

  // const method
  template <size_t Id, typename... MethodArgs, typename... GivenArgs>
  static ReturnType Run(const ClientWebGLContext& c,
                        ReturnType (HostWebGLContext::*method)(MethodArgs...)
                            const,
                        GivenArgs&&... aArgs) {
    // Non-void calls must be sync, otherwise what would we return?
    MOZ_ASSERT(WebGLMethodDispatcher::SyncType<Id>() == CommandSyncType::SYNC);
    return c.DispatchSync<Id, ReturnType>(
        static_cast<const MethodArgs&>(aArgs)...);
  }
};

template <>
struct WebGLClientDispatcher<void> {
  // non-const method
  template <size_t Id, typename... MethodArgs, typename... GivenArgs>
  static void Run(const ClientWebGLContext& c,
                  void (HostWebGLContext::*method)(MethodArgs...),
                  GivenArgs&&... aArgs) {
    if (WebGLMethodDispatcher::SyncType<Id>() == CommandSyncType::SYNC) {
      c.DispatchVoidSync<Id>(static_cast<const MethodArgs&>(aArgs)...);
    } else {
      c.DispatchAsync<Id>(static_cast<const MethodArgs&>(aArgs)...);
    }
  }

  // const method
  template <size_t Id, typename... MethodArgs, typename... GivenArgs>
  static void Run(const ClientWebGLContext& c,
                  void (HostWebGLContext::*method)(MethodArgs...) const,
                  GivenArgs&&... aArgs) {
    if (WebGLMethodDispatcher::SyncType<Id>() == CommandSyncType::SYNC) {
      c.DispatchVoidSync<Id>(static_cast<const MethodArgs&>(aArgs)...);
    } else {
      c.DispatchAsync<Id>(static_cast<const MethodArgs&>(aArgs)...);
    }
  }
};

template <typename T>
inline T DefaultOrVoid() {
  return {};
}

template <>
inline void DefaultOrVoid<void>() {
  return;
}

// If we are running WebGL in this process then call the HostWebGLContext
// method directly.  Otherwise, dispatch over IPC.
template <
    typename MethodType, MethodType method,
    typename ReturnType = typename FunctionTypeTraits<MethodType>::ReturnType,
    size_t Id = WebGLMethodDispatcher::Id<MethodType, method>(),
    typename... Args>
ReturnType ClientWebGLContext::Run(Args&&... aArgs) const {
  if (!mNotLost) return DefaultOrVoid<ReturnType>();
  const auto& inProcessContext = mNotLost->inProcess;
  if (inProcessContext) {
    return ((inProcessContext.get())->*method)(std::forward<Args>(aArgs)...);
  }
  return WebGLClientDispatcher<ReturnType>::template Run<Id>(*this, method,
                                                             aArgs...);
}

// -------------------------------------------------------------------------
// Client-side helper methods.  Dispatch to a Host method.
// -------------------------------------------------------------------------

#define RPROC(_METHOD) \
  decltype(&HostWebGLContext::_METHOD), &HostWebGLContext::_METHOD

// ------------------------- Composition, etc -------------------------

void ClientWebGLContext::UpdateLastUseIndex() {
  static CheckedInt<uint64_t> sIndex = 0;

  sIndex++;

  // should never happen with 64-bit; trying to handle this would be riskier
  // than not handling it as the handler code would never get exercised.
  if (!sIndex.isValid())
    MOZ_CRASH("Can't believe it's been 2^64 transactions already!");
  mLastUseIndex = sIndex.value();
}

static uint8_t gWebGLLayerUserData;

class WebGLContextUserData : public LayerUserData {
 public:
  explicit WebGLContextUserData(HTMLCanvasElement* canvas) : mCanvas(canvas) {}

  /* PreTransactionCallback gets called by the Layers code every time the
   * WebGL canvas is going to be composited.
   */
  static void PreTransactionCallback(void* data) {
    ClientWebGLContext* webgl = static_cast<ClientWebGLContext*>(data);

    // Prepare the context for composition
    webgl->BeginComposition();
  }

  /** DidTransactionCallback gets called by the Layers code everytime the WebGL
   * canvas gets composite, so it really is the right place to put actions that
   * have to be performed upon compositing
   */
  static void DidTransactionCallback(void* data) {
    ClientWebGLContext* webgl = static_cast<ClientWebGLContext*>(data);

    // Clean up the context after composition
    webgl->EndComposition();
  }

 private:
  RefPtr<HTMLCanvasElement> mCanvas;
};

void ClientWebGLContext::BeginComposition() {
  // When running single-process WebGL, Present needs to be called in
  // BeginComposition so that it is done _before_ the CanvasRenderer to
  // Update attaches it for composition.
  // When running cross-process WebGL, Present needs to be called in
  // EndComposition so that it happens _after_ the OOPCanvasRenderer's
  // Update tells it what CompositableHost to use,
  if (mNotLost->inProcess) {
    WEBGL_BRIDGE_LOGI("[%p] Presenting", this);
    mNotLost->inProcess->Present();
  }
}

void ClientWebGLContext::EndComposition() {
  if (mNotLost->outOfProcess) {
    WEBGL_BRIDGE_LOGI("[%p] Presenting", this);
    Run<RPROC(Present)>();
  }

  // Mark ourselves as no longer invalidated.
  MarkContextClean();
  UpdateLastUseIndex();
}

void ClientWebGLContext::Present() {
  if (mNotLost) {
    Run<RPROC(Present)>();
  }
}

already_AddRefed<layers::Layer> ClientWebGLContext::GetCanvasLayer(
    nsDisplayListBuilder* builder, Layer* oldLayer, LayerManager* manager) {
  if (!mResetLayer && oldLayer && oldLayer->HasUserData(&gWebGLLayerUserData)) {
    RefPtr<layers::Layer> ret = oldLayer;
    return ret.forget();
  }

  WEBGL_BRIDGE_LOGI("[%p] Creating WebGL CanvasLayer/Renderer", this);

  RefPtr<CanvasLayer> canvasLayer = manager->CreateCanvasLayer();
  if (!canvasLayer) {
    NS_WARNING("CreateCanvasLayer returned null!");
    return nullptr;
  }

  WebGLContextUserData* userData = nullptr;
  if (builder->IsPaintingToWindow() && mCanvasElement) {
    userData = new WebGLContextUserData(mCanvasElement);
  }

  canvasLayer->SetUserData(&gWebGLLayerUserData, userData);

  CanvasRenderer* canvasRenderer = canvasLayer->CreateOrGetCanvasRenderer();
  if (!InitializeCanvasRenderer(builder, canvasRenderer)) return nullptr;

  uint32_t flags = HasAlphaSupport() ? 0 : Layer::CONTENT_OPAQUE;
  canvasLayer->SetContentFlags(flags);

  mResetLayer = false;

  return canvasLayer.forget();
}

bool ClientWebGLContext::UpdateWebRenderCanvasData(
    nsDisplayListBuilder* aBuilder, WebRenderCanvasData* aCanvasData) {
  CanvasRenderer* renderer = aCanvasData->GetCanvasRenderer();

  if (!mResetLayer && renderer) {
    return true;
  }

  WEBGL_BRIDGE_LOGI("[%p] Creating WebGL WR CanvasLayer/Renderer", this);
  renderer = aCanvasData->CreateCanvasRenderer();
  if (!InitializeCanvasRenderer(aBuilder, renderer)) {
    // Clear CanvasRenderer of WebRenderCanvasData
    aCanvasData->ClearCanvasRenderer();
    return false;
  }

  MOZ_ASSERT(renderer);
  mResetLayer = false;
  return true;
}

bool ClientWebGLContext::InitializeCanvasRenderer(
    nsDisplayListBuilder* aBuilder, CanvasRenderer* aRenderer) {
  const FuncScope funcScope(this, "<InitializeCanvasRenderer>");
  if (IsContextLost()) return false;

  Maybe<ICRData> icrData =
      Run<RPROC(InitializeCanvasRenderer)>(GetCompositorBackendType());

  if (!icrData) {
    return false;
  }

  mSurfaceInfo = *icrData;

  CanvasInitializeData data;
  if (aBuilder->IsPaintingToWindow() && mCanvasElement) {
    // Make the layer tell us whenever a transaction finishes (including
    // the current transaction), so we can clear our invalidation state and
    // start invalidating again. We need to do this for the layer that is
    // being painted to a window (there shouldn't be more than one at a time,
    // and if there is, flushing the invalidation state more often than
    // necessary is harmless).

    // The layer will be destroyed when we tear down the presentation
    // (at the latest), at which time this userData will be destroyed,
    // releasing the reference to the element.
    // The userData will receive DidTransactionCallbacks, which flush the
    // the invalidation state to indicate that the canvas is up to date.
    data.mPreTransCallback = WebGLContextUserData::PreTransactionCallback;
    data.mPreTransCallbackData = this;
    data.mDidTransCallback = WebGLContextUserData::DidTransactionCallback;
    data.mDidTransCallbackData = this;
  }

  MOZ_ASSERT(mCanvasElement);  // TODO: What to do here?  Is this about
                               // OffscreenCanvas?

  if (IsHostOOP()) {
    data.mOOPRenderer = mCanvasElement->GetOOPCanvasRenderer();
    MOZ_ASSERT(data.mOOPRenderer);
    MOZ_ASSERT((!data.mOOPRenderer->mContext) ||
               (data.mOOPRenderer->mContext == this));
    data.mOOPRenderer->mContext = this;
  } else {
    MOZ_ASSERT(mNotLost->inProcess);
    data.mGLContext = mNotLost->inProcess->GetWebGLContext()->gl;
  }

  data.mHasAlpha = mSurfaceInfo.hasAlpha;
  data.mIsGLAlphaPremult = mSurfaceInfo.isPremultAlpha || !data.mHasAlpha;
  data.mSize = mSurfaceInfo.size;

  aRenderer->Initialize(data);
  aRenderer->SetDirty();
  return true;
}

layers::LayersBackend ClientWebGLContext::GetCompositorBackendType() const {
  if (mCanvasElement) {
    return mCanvasElement->GetCompositorBackendType();
  } else if (mOffscreenCanvas) {
    return mOffscreenCanvas->GetCompositorBackendType();
  }

  return LayersBackend::LAYERS_NONE;
}

mozilla::dom::Document* ClientWebGLContext::GetOwnerDoc() const {
  MOZ_ASSERT(mCanvasElement);
  if (!mCanvasElement) {
    return nullptr;
  }
  return mCanvasElement->OwnerDoc();
}

void ClientWebGLContext::Commit() {
  if (mOffscreenCanvas) {
    mOffscreenCanvas->CommitFrameToCompositor();
  }
}

void ClientWebGLContext::GetCanvas(
    Nullable<dom::OwningHTMLCanvasElementOrOffscreenCanvas>& retval) {
  if (mCanvasElement) {
    MOZ_RELEASE_ASSERT(!mOffscreenCanvas, "GFX: Canvas is offscreen.");

    if (mCanvasElement->IsInNativeAnonymousSubtree()) {
      retval.SetNull();
    } else {
      retval.SetValue().SetAsHTMLCanvasElement() = mCanvasElement;
    }
  } else if (mOffscreenCanvas) {
    retval.SetValue().SetAsOffscreenCanvas() = mOffscreenCanvas;
  } else {
    retval.SetNull();
  }
}

void ClientWebGLContext::GetContextAttributes(
    dom::Nullable<dom::WebGLContextAttributes>& retval) {
  retval.SetNull();
  const FuncScope funcScope(this, "getContextAttributes");
  if (IsContextLost()) return;

  dom::WebGLContextAttributes& result = retval.SetValue();

  const auto& options = mNotLost->info.options;

  result.mAlpha.Construct(options.alpha);
  result.mDepth = options.depth;
  result.mStencil = options.stencil;
  result.mAntialias.Construct(options.antialias);
  result.mPremultipliedAlpha = options.premultipliedAlpha;
  result.mPreserveDrawingBuffer = options.preserveDrawingBuffer;
  result.mFailIfMajorPerformanceCaveat = options.failIfMajorPerformanceCaveat;
  result.mPowerPreference = options.powerPreference;
}

// -----------------------

NS_IMETHODIMP
ClientWebGLContext::SetDimensions(int32_t signedWidth, int32_t signedHeight) {
  const FuncScope funcScope(this, "<SetDimensions>");
  WEBGL_BRIDGE_LOGI("[%p] SetDimensions: (%d, %d)", this, signedWidth,
                    signedHeight);

  MOZ_ASSERT(mInitialOptions);

  const auto size = uvec2::From(signedWidth, signedHeight);
  if (!size) {
    EnqueueWarning(
        "Canvas size is too large (seems like a negative value wrapped)");
    return NS_ERROR_OUT_OF_MEMORY;
  }
  if (*size == mRequestedSize) return NS_OK;
  mRequestedSize = *size;
  mDrawingBufferSize = {};

  if (mNotLost) {
    Run<RPROC(Resize)>(*size);
    MarkCanvasDirty();
    return NS_OK;
  }

  if (mLossStatus != webgl::LossStatus::Ready) {
    MOZ_RELEASE_ASSERT(false);
    return NS_ERROR_FAILURE;
  }

  // -
  // Context (re-)creation

  if (!CreateHostContext()) {
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

bool ClientWebGLContext::CreateHostContext() {
  ClientWebGLContext::NotLostData notLost;

  auto res = [&]() -> Result<Ok, std::string> {
    auto options = *mInitialOptions;
    if (StaticPrefs::webgl_disable_fail_if_major_performance_caveat()) {
      options.failIfMajorPerformanceCaveat = false;
    }
    const bool resistFingerprinting = ShouldResistFingerprinting();
    const auto initDesc = webgl::InitContextDesc{
        mIsWebGL2, resistFingerprinting, mRequestedSize, options};

    // -

    if (!StaticPrefs::webgl_out_of_process()) {
      auto ownerData = HostWebGLContext::OwnerData{
          Some(this),
      };
      notLost.inProcess = HostWebGLContext::Create(std::move(ownerData),
                                                   initDesc, &notLost.info);
      return Ok();
    }

    // -

    ClientWebGLContext::RemotingData outOfProcess;

    auto* const cbc = CompositorBridgeChild::Get();
    MOZ_ASSERT(cbc);
    if (!cbc) {
      return Err("!CompositorBridgeChild::Get()");
    }

    // Construct the WebGL command queue, used to send commands from the client
    // process to the host for execution.  It takes a response queue that is
    // used to return responses to synchronous messages.
    // TODO: Be smarter in choosing these.
    static constexpr size_t CommandQueueSize = 256 * 1024;  // 256K
    static constexpr size_t ResponseQueueSize = 8 * 1024;   // 8K
    auto commandPcq = ProducerConsumerQueue::Create(cbc, CommandQueueSize);
    auto responsePcq = ProducerConsumerQueue::Create(cbc, ResponseQueueSize);
    if (!commandPcq || !responsePcq) {
      return Err("Failed to create command/response PCQ");
    }

    outOfProcess.mCommandSource = MakeUnique<ClientWebGLCommandSource>(
        std::move(commandPcq->mProducer), std::move(responsePcq->mConsumer));
    auto sink = MakeUnique<HostWebGLCommandSink>(
        std::move(commandPcq->mConsumer), std::move(responsePcq->mProducer));

    // Use the error/warning and command queues to construct a
    // ClientWebGLContext in this process and a HostWebGLContext
    // in the host process.
    outOfProcess.mWebGLChild = new dom::WebGLChild(*this);
    if (!cbc->SendPWebGLConstructor(outOfProcess.mWebGLChild.get(), initDesc,
                                    &notLost.info)) {
      return Err("SendPWebGLConstructor failed");
    }

    notLost.outOfProcess = Some(std::move(outOfProcess));
    return Ok();
  }();
  if (!res.isOk()) {
    notLost.info.error = res.unwrapErr();
  }
  if (notLost.info.error.size()) {
    ThrowEvent_WebGLContextCreationError(notLost.info.error);
    return false;
  }

  mNotLost = Some(std::move(notLost));
  return true;
}

// -------

const uvec2& ClientWebGLContext::DrawingBufferSize() {
  if (!mDrawingBufferSize) {
    mDrawingBufferSize = Some(Run<RPROC(DrawingBufferSize)>());
  }

  return *mDrawingBufferSize;
}

void ClientWebGLContext::OnMemoryPressure() {
  WEBGL_BRIDGE_LOGI("[%p] OnMemoryPressure", this);
  return Run<RPROC(OnMemoryPressure)>();
}

NS_IMETHODIMP
ClientWebGLContext::SetContextOptions(JSContext* cx,
                                      JS::Handle<JS::Value> options,
                                      ErrorResult& aRvForDictionaryInit) {
  MOZ_ASSERT(!mInitialOptions);

  WebGLContextAttributes attributes;
  if (!attributes.Init(cx, options)) {
    aRvForDictionaryInit.Throw(NS_ERROR_UNEXPECTED);
    return NS_ERROR_UNEXPECTED;
  }

  WebGLContextOptions newOpts;

  newOpts.stencil = attributes.mStencil;
  newOpts.depth = attributes.mDepth;
  newOpts.premultipliedAlpha = attributes.mPremultipliedAlpha;
  newOpts.preserveDrawingBuffer = attributes.mPreserveDrawingBuffer;
  newOpts.failIfMajorPerformanceCaveat =
      attributes.mFailIfMajorPerformanceCaveat;
  newOpts.powerPreference = attributes.mPowerPreference;
  newOpts.enableDebugRendererInfo =
      Preferences::GetBool("webgl.enable-debug-renderer-info", false);
  MOZ_ASSERT(mCanvasElement || mOffscreenCanvas);
  newOpts.shouldResistFingerprinting =
      mCanvasElement ?
                     // If we're constructed from a canvas element
          nsContentUtils::ShouldResistFingerprinting(GetOwnerDoc())
                     :
                     // If we're constructed from an offscreen canvas
          nsContentUtils::ShouldResistFingerprinting(
              mOffscreenCanvas->GetOwnerGlobal()->PrincipalOrNull());

  if (attributes.mAlpha.WasPassed()) {
    newOpts.alpha = attributes.mAlpha.Value();
  }
  if (attributes.mAntialias.WasPassed()) {
    newOpts.antialias = attributes.mAntialias.Value();
  }

  // Don't do antialiasing if we've disabled MSAA.
  if (!StaticPrefs::webgl_msaa_samples()) {
    newOpts.antialias = false;
  }

  mInitialOptions.emplace(newOpts);
  return NS_OK;
}

void ClientWebGLContext::DidRefresh() { Run<RPROC(DidRefresh)>(); }

already_AddRefed<mozilla::gfx::SourceSurface>
ClientWebGLContext::GetSurfaceSnapshot(gfxAlphaType* out_alphaType) {
  MOZ_ASSERT_UNREACHABLE("TODO: ClientWebGLContext::GetSurfaceSnapshot");
  return nullptr;
}

UniquePtr<uint8_t[]> ClientWebGLContext::GetImageBuffer(int32_t* out_format) {
  *out_format = 0;

  // Use GetSurfaceSnapshot() to make sure that appropriate y-flip gets applied
  gfxAlphaType any;
  RefPtr<SourceSurface> snapshot = GetSurfaceSnapshot(&any);
  if (!snapshot) return nullptr;

  RefPtr<DataSourceSurface> dataSurface = snapshot->GetDataSurface();

  const auto& premultAlpha = mNotLost->info.options.premultipliedAlpha;
  return gfxUtils::GetImageBuffer(dataSurface, premultAlpha, out_format);
}

NS_IMETHODIMP
ClientWebGLContext::GetInputStream(const char* mimeType,
                                   const nsAString& encoderOptions,
                                   nsIInputStream** out_stream) {
  // Use GetSurfaceSnapshot() to make sure that appropriate y-flip gets applied
  gfxAlphaType any;
  RefPtr<SourceSurface> snapshot = GetSurfaceSnapshot(&any);
  if (!snapshot) return NS_ERROR_FAILURE;

  RefPtr<DataSourceSurface> dataSurface = snapshot->GetDataSurface();
  const auto& premultAlpha = mNotLost->info.options.premultipliedAlpha;
  return gfxUtils::GetInputStream(dataSurface, premultAlpha, mimeType,
                                  encoderOptions, out_stream);
}

// ------------------------- Client WebGL Objects -------------------------

struct MaybeWebGLVariantMatcher {
  MaybeWebGLVariantMatcher(ClientWebGLContext* cxt, JSContext* cx,
                           ErrorResult* rv)
      : mCxt(cxt), mCx(cx), mRv(rv) {}

  JS::Value match(int32_t x) { return JS::NumberValue(x); }
  JS::Value match(int64_t x) { return JS::NumberValue(x); }
  JS::Value match(uint32_t x) { return JS::NumberValue(x); }
  JS::Value match(uint64_t x) { return JS::NumberValue(x); }
  JS::Value match(float x) { return JS::Float32Value(x); }
  JS::Value match(double x) { return JS::DoubleValue(x); }
  JS::Value match(bool x) { return JS::BooleanValue(x); }
  JS::Value match(const nsCString& x) {
    return StringValue(mCx, x.BeginReading(), *mRv);
  }
  JS::Value match(const nsString& x) { return StringValue(mCx, x, *mRv); }

  template <size_t Length>
  JS::Value match(const Array<int32_t, Length>& x) {
    JSObject* obj = dom::Int32Array::Create(
        mCx, mCxt, static_cast<const int32_t(&)[Length]>(x));
    if (!obj) {
      *mRv = NS_ERROR_OUT_OF_MEMORY;
      mCxt->EnqueueError(LOCAL_GL_OUT_OF_MEMORY, "ToJSValue: Out of memory.");
    }
    return JS::ObjectOrNullValue(obj);
  }

  template <size_t Length>
  JS::Value match(const Array<uint32_t, Length>& x) {
    JSObject* obj = dom::Uint32Array::Create(
        mCx, mCxt, static_cast<const uint32_t(&)[Length]>(x));
    if (!obj) {
      *mRv = NS_ERROR_OUT_OF_MEMORY;
      mCxt->EnqueueError(LOCAL_GL_OUT_OF_MEMORY, "ToJSValue: Out of memory.");
    }
    return JS::ObjectOrNullValue(obj);
  }

  template <size_t Length>
  JS::Value match(const Array<float, Length>& x) {
    JSObject* obj = dom::Float32Array::Create(
        mCx, mCxt, static_cast<const float(&)[Length]>(x));
    if (!obj) {
      *mRv = NS_ERROR_OUT_OF_MEMORY;
      mCxt->EnqueueError(LOCAL_GL_OUT_OF_MEMORY, "ToJSValue: Out of memory.");
    }
    return JS::ObjectOrNullValue(obj);
  }

  template <size_t Length>
  JS::Value match(const Array<bool, Length>& x) {
    JS::Rooted<JS::Value> obj(mCx);
    if (!dom::ToJSValue(mCx, static_cast<const bool(&)[Length]>(x), &obj)) {
      *mRv = NS_ERROR_OUT_OF_MEMORY;
      mCxt->EnqueueError(LOCAL_GL_OUT_OF_MEMORY, "ToJSValue: Out of memory.");
    }
    return obj;
  }

  JS::Value match(const nsTArray<uint32_t>& x) {
    JSObject* obj = dom::Uint32Array::Create(mCx, mCxt, x.Length(), &x[0]);
    if (!obj) {
      *mRv = NS_ERROR_OUT_OF_MEMORY;
      mCxt->EnqueueError(LOCAL_GL_OUT_OF_MEMORY, "ToJSValue: Out of memory.");
    }
    return JS::ObjectOrNullValue(obj);
  }

  JS::Value match(const nsTArray<int32_t>& x) {
    JSObject* obj = dom::Int32Array::Create(mCx, mCxt, x.Length(), &x[0]);
    if (!obj) {
      *mRv = NS_ERROR_OUT_OF_MEMORY;
      mCxt->EnqueueError(LOCAL_GL_OUT_OF_MEMORY, "ToJSValue: Out of memory.");
    }
    return JS::ObjectOrNullValue(obj);
  }

  JS::Value match(const nsTArray<float>& x) {
    JSObject* obj = dom::Float32Array::Create(mCx, mCxt, x.Length(), &x[0]);
    if (!obj) {
      *mRv = NS_ERROR_OUT_OF_MEMORY;
      mCxt->EnqueueError(LOCAL_GL_OUT_OF_MEMORY, "ToJSValue: Out of memory.");
    }
    return JS::ObjectOrNullValue(obj);
  }

  JS::Value match(const nsTArray<bool>& x) {
    JS::Rooted<JS::Value> obj(mCx);
    if (!dom::ToJSValue(mCx, &x[0], x.Length(), &obj)) {
      *mRv = NS_ERROR_OUT_OF_MEMORY;
      mCxt->EnqueueError(LOCAL_GL_OUT_OF_MEMORY, "ToJSValue: Out of memory.");
    }
    return obj;
  }

  template <typename WebGLClass>
  JS::Value match(const WebGLId<WebGLClass>& x) {
    return mCxt->WebGLObjectAsJSValue(mCx, mCxt->EnsureWebGLObject(x), *mRv);
  }

 private:
  // Create a JS::Value from a C string
  JS::Value StringValue(JSContext* cx, const char* chars, ErrorResult& rv) {
    JSString* str = JS_NewStringCopyZ(cx, chars);
    if (!str) {
      rv.Throw(NS_ERROR_OUT_OF_MEMORY);
      return JS::NullValue();
    }

    return JS::StringValue(str);
  }

  // Create a JS::Value from an nsAString
  JS::Value StringValue(JSContext* cx, const nsAString& str, ErrorResult& er) {
    JSString* jsStr = JS_NewUCStringCopyN(cx, str.BeginReading(), str.Length());
    if (!jsStr) {
      er.Throw(NS_ERROR_OUT_OF_MEMORY);
      return JS::NullValue();
    }

    return JS::StringValue(jsStr);
  }

  ClientWebGLContext* mCxt;
  JSContext* mCx;
  ErrorResult* mRv;
};

JS::Value ClientWebGLContext::ToJSValue(JSContext* cx,
                                        const MaybeWebGLVariant& aVariant,
                                        ErrorResult& rv) const {
  if (!aVariant) {
    return JS::NullValue();
  }
  return aVariant.ref().match(
      MaybeWebGLVariantMatcher(const_cast<ClientWebGLContext*>(this), cx, &rv));
}

// ------------------------- Create/Destroy/Is -------------------------

RefPtr<WebGLBufferJS> ClientWebGLContext::CreateBuffer() const {
  const FuncScope funcScope(*this, "createBuffer");
  if (IsContextLost()) return nullptr;

  return new WebGLBufferJS(*this);
}

RefPtr<WebGLFramebufferJS> ClientWebGLContext::CreateFramebuffer() const {
  const FuncScope funcScope(*this, "createFramebuffer");
  if (IsContextLost()) return nullptr;

  return new WebGLFramebufferJS(*this);
}

RefPtr<WebGLProgramJS> ClientWebGLContext::CreateProgram() const {
  const FuncScope funcScope(*this, "createProgram");
  if (IsContextLost()) return nullptr;

  return new WebGLProgramJS(*this);
}

RefPtr<WebGLQueryJS> ClientWebGLContext::CreateQuery() const {
  const FuncScope funcScope(*this, "createQuery");
  if (IsContextLost()) return nullptr;

  return new WebGLQueryJS(*this);
}

RefPtr<WebGLRenderbufferJS> ClientWebGLContext::CreateRenderbuffer() const {
  const FuncScope funcScope(*this, "createRenderbuffer");
  if (IsContextLost()) return nullptr;

  return new WebGLRenderbufferJS(*this);
}

RefPtr<WebGLSamplerJS> ClientWebGLContext::CreateSampler() const {
  const FuncScope funcScope(*this, "createSampler");
  if (IsContextLost()) return nullptr;

  return new WebGLSamplerJS(*this);
}

RefPtr<WebGLShaderJS> ClientWebGLContext::CreateShader(const GLenum type) const {
  const FuncScope funcScope(*this, "createShader");
  if (IsContextLost()) return nullptr;

  switch (type) {
   case LOCAL_GL_VERTEX_SHADER:
   case LOCAL_GL_FRAGMENT_SHADER:
    break;
   default:
    EnqueueError_ArgEnum("type", type);
    return nullptr;
  }

  return new WebGLShaderJS(*this, type);
}

RefPtr<WebGLSyncJS> ClientWebGLContext::FenceSync(const GLenum condition, const GLbitfield flags) const {
  const FuncScope funcScope(*this, "fenceSync");
  if (IsContextLost()) return nullptr;

  if (condition != LOCAL_GL_SYNC_GPU_COMMANDS_COMPLETE) {
    EnqueueError_ArgEnum("condition", condition);
    return nullptr;
  }

  if (flags) {
    EnqueueError(LOCAL_GL_INVALID_VALUE, "`flags` must be 0.");
    return nullptr;
  }

  return new WebGLSyncJS(*this);
}

RefPtr<WebGLTextureJS> ClientWebGLContext::CreateTexture() const {
  const FuncScope funcScope(*this, "createTexture");
  if (IsContextLost()) return nullptr;

  return new WebGLTextureJS(*this);
}

RefPtr<WebGLTransformFeedbackJS> ClientWebGLContext::CreateTransformFeedback() const {
  const FuncScope funcScope(*this, "createTransformFeedback");
  if (IsContextLost()) return nullptr;

  return new WebGLTransformFeedbackJS(*this);
}

RefPtr<WebGLVertexArrayJS> ClientWebGLContext::CreateVertexArray() const {
  const FuncScope funcScope(*this, "createVertexArray");
  if (IsContextLost()) return nullptr;

  return new WebGLVertexArrayJS(*this);
}

// -

void ClientWebGLContext::DeleteBuffer(WebGLBufferJS* const obj) const {
  const FuncScope funcScope(*this, "deleteBuffer");
  if (!IsBuffer(obj)) return;

  // Unbind from all bind points and bound containers

  // UBOs
  for (const auto& i : IntegerRange(mBoundUbos.size())) {
    if (mBoundUbos[i] == obj) {
      BindBufferBase(LOCAL_GL_UNIFORM_BUFFER, i, nullptr);
    }
  }

  // TFO only if not active
  if (!mBoundTfo->mActiveOrPaused) {
    const auto& buffers = mBoundTfo->mAttribBuffers;
    for (const auto& i : IntegerRange(buffers.size())) {
      if (buffers[i] == obj) {
        BindBufferBase(LOCAL_GL_TRANSFORM_FEEDBACK_BUFFER, i, nullptr);
      }
    }
  }

  // Generic/global bind points
  for (const auto& itr : mBoundBufferByTarget) {
    if (itr->second == obj) {
      BindBuffer(itr->first, nullptr);
    }
  }

  // VAO attachments
  if (mBoundVao->mIndexBuffer == obj) {
    BindBuffer(LOCAL_GL_ELEMENT_ARRAY_BUFFER, nullptr);
  }

  const auto& vaoBuffers = mBoundVao->mAttribBuffers;
  Maybe<WebGLBuffer*> toRestore;
  for (const auto& i : IntegerRange(vaoBuffers.size())) {
    if (vaoBuffers[i] == obj) {
      if (!toRestore) {
        toRestore = Some(mBoundBufferByTarget[LOCAL_GL_ARRAY_BUFFER]);
        if (*toRestore) {
          BindBuffer(LOCAL_GL_ARRAY_BUFFER, nullptr);
        }
      }
      VertexAttribPointer(i, 0, 0, false, 0, 0);
    }
  }
  if (toRestore && *toRestore) {
    BindBuffer(LOCAL_GL_ARRAY_BUFFER, *toRestore);
  }

  // -

  obj->mDeleteRequested = true;
  Run<RPROC(DeleteBuffer)>(obj->mId);
}

void ClientWebGLContext::DeleteFramebuffer(WebGLFramebufferJS* const obj) const {
  const FuncScope funcScope(*this, "deleteFramebuffer");
  if (!IsFramebuffer(obj)) return;

  // Unbind
  const auto fnDetach = [&](const GLenum target, const WebGLFramebufferJS* const fb) {
    if (!fb) return;
    BindFramebuffer(target, nullptr);
  };
  if (mBoundDrawFb == mBoundReadFb) {
    fnDetach(LOCAL_GL_FRAMEBUFFER, mBoundDrawFb.get());
  } else {
    fnDetach(LOCAL_GL_DRAW_FRAMEBUFFER, mBoundDrawFb.get());
    fnDetach(LOCAL_GL_READ_FRAMEBUFFER, mBoundReadFb.get());
  }

  obj->mDeleteRequested = true;
  Run<RPROC(DeleteFramebuffer)>(obj->mId);
}

void ClientWebGLContext::DeleteProgram(WebGLProgramJS* const obj) const {
  const FuncScope funcScope(*this, "deleteProgram");
  if (!IsProgram(obj)) return;

  // Don't unbind

  obj->mInnerRef = nullptr;
  Run<RPROC(DeleteProgram)>(obj->mId);
}

void ClientWebGLContext::DeleteQuery(WebGLQueryJS* const obj) const {
  const FuncScope funcScope(*this, "deleteQuery");
  if (!IsQuery(obj)) return;

  // Don't unbind

  obj->mDeleteRequested = true;
  Run<RPROC(DeleteQuery)>(obj->mId);
}

void ClientWebGLContext::DeleteRenderbuffer(WebGLRenderbufferJS* const obj) {
  const FuncScope funcScope(*this, "deleteRenderbuffer");
  if (!IsRenderbuffer(obj)) return;

  // Unbind
  if (mBoundRb == obj) {
    BindRenderbuffer(LOCAL_GL_RENDERBUFFER, nullptr);
  }

  obj->mDeleteRequested = true;
  Run<RPROC(DeleteRenderbuffer)>(obj->mId);
}

void ClientWebGLContext::DeleteSampler(WebGLSamplerJS* const obj) const {
  const FuncScope funcScope(*this, "deleteSampler");
  if (!IsSampler(obj)) return;

  // Unbind
  for (const auto& i : IntegerRange(mTexUnits.size())) {
    if (mTexUnits[i].sampler == obj) {
      BindSampler(i, nullptr);
    }
  }

  obj->mDeleteRequested = true;
  Run<RPROC(DeleteSampler)>(obj->mId);
}

void ClientWebGLContext::DeleteShader(WebGLShaderJS* const obj) const {
  const FuncScope funcScope(*this, "deleteShader");
  if (!IsShader(obj)) return;

  // Don't unbind

  obj->mInnerRef = nullptr;
  Run<RPROC(DeleteShader)>(obj->mId);
}

void ClientWebGLContext::DeleteSync(WebGLSyncJS* const obj) const {
  const FuncScope funcScope(*this, "deleteSync");
  if (!IsSync(obj)) return;

  // Nothing to unbind

  obj->mDeleteRequested = true;
  Run<RPROC(DeleteSync)>(obj->mId);
}

void ClientWebGLContext::DeleteTexture(WebGLTextureJS* const obj) const {
  const FuncScope funcScope(*this, "deleteTexture");
  if (!IsTexture(obj)) return;

  // Unbind
  const auto& target = obj->mTarget;
  if (target) {
    // Unbind from tex units
    Maybe<size_t> restoreTexUnit;
    for (const auto& i : IntegerRange(mTexUnits.size())) {
      if (mTexUnits[i][texByTarget] == obj) {
        if (!restoreTexUnit) {
          restoreTexUnit = Some(mActiveTexture);
        }
        ActiveTexture(LOCAL_GL_TEXTURE0 + i);
        BindTexture(target, nullptr);
      }
    }
    if (restoreTexUnit) {
      ActiveTexture(LOCAL_GL_TEXTURE0 + *restoreTexUnit);
    }

    // Unbind from bound FBs
    const auto fnDetach = [&](const GLenum target, const WebGLFramebufferJS* const fb) {
      if (!fb) return;
      for (const auto& itr : fb->mAttachments) {
        if (itr->second == obj) {
          FramebufferRenderbuffer(target, itr->first, LOCAL_GL_RENDERBUFFER, nullptr);
        }
      }
    };
    if (mBoundDrawFb == mBoundReadFb) {
      fnDetach(LOCAL_GL_FRAMEBUFFER, mBoundDrawFb.get());
    } else {
      fnDetach(LOCAL_GL_DRAW_FRAMEBUFFER, mBoundDrawFb.get());
      fnDetach(LOCAL_GL_READ_FRAMEBUFFER, mBoundReadFb.get());
    }
  }

  obj->mDeleteRequested = true;
  Run<RPROC(DeleteTexture)>(obj->mId);
}

void ClientWebGLContext::DeleteTransformFeedback(
    WebGLTransformFeedbackJS* const obj) const {
  const FuncScope funcScope(*this, "deleteTransformFeedback");
  if (!IsTransformFeedback(obj)) return;

  // Don't unbind

  obj->mDeleteRequested = true;
  Run<RPROC(DeleteTransformFeedback)>(obj->mId);
}


void ClientWebGLContext::DeleteVertexArray(WebGLVertexArrayJS* const obj) {
  const FuncScope funcScope(*this, "deleteVertexArray");
  if (!IsVertexArray(obj)) return;

  // Unbind
  if (mBoundVao == obj) {
    BindVertexArray(nullptr);
  }

  obj->mDeleteRequested = true;
  Run<RPROC(DeleteVertexArray)>(obj->mId);
}

// -

bool ClientWebGLContext::IsBuffer(const WebGLBufferJS* const obj) const {
  const FuncScope funcScope(*this, "isBuffer");
  if (IsContextLost()) return false;

  return obj && obj->IsUsable(*this) && obj->mKind != webgl::BufferKind::Undefined;
}

bool ClientWebGLContext::IsFramebuffer(const WebGLFramebufferJS* const obj) const {
  const FuncScope funcScope(*this, "isFramebuffer");
  if (IsContextLost()) return false;

  return obj && obj->IsUsable(*this) && obj->mTarget;
}

bool ClientWebGLContext::IsProgram(const WebGLProgramJS* const obj) const {
  const FuncScope funcScope(*this, "isProgram");
  if (IsContextLost()) return false;

  return obj && obj->IsUsable(*this);
}

bool ClientWebGLContext::IsQuery(const WebGLQueryJS* const obj) const {
  const FuncScope funcScope(*this, "isQuery");
  if (IsContextLost()) return false;

  return obj && obj->IsUsable(*this) && obj->mTarget;
}

bool ClientWebGLContext::IsRenderbuffer(const WebGLRenderbufferJS* const obj) const {
  const FuncScope funcScope(*this, "isRenderbuffer");
  if (IsContextLost()) return false;

  return obj && obj->IsUsable(*this) && obj->mHasBeenBound;
}

bool ClientWebGLContext::IsSampler(const WebGLSamplerJS* const obj) const {
  const FuncScope funcScope(*this, "isSampler");
  if (IsContextLost()) return false;

  return obj && obj->IsUsable(*this);
}

bool ClientWebGLContext::IsShader(const WebGLShaderJS* const obj) const {
  const FuncScope funcScope(*this, "isShader");
  if (IsContextLost()) return false;

  return obj && obj->IsUsable(*this);
}

bool ClientWebGLContext::IsSync(const WebGLSyncJS* const obj) const {
  const FuncScope funcScope(*this, "isSync");
  if (IsContextLost()) return false;

  return obj && obj->IsUsable(*this);
}

bool ClientWebGLContext::IsTexture(const WebGLTextureJS* const obj) const {
  const FuncScope funcScope(*this, "isTexture");
  if (IsContextLost()) return false;

  return obj && obj->IsUsable(*this) && obj->mTarget;
}

bool ClientWebGLContext::IsTransformFeedback(const WebGLTransformFeedbackJS* const obj) const {
  const FuncScope funcScope(*this, "isTransformFeedback");
  if (IsContextLost()) return false;

  return obj && obj->IsUsable(*this) && obj->mHasBeenBound;
}

bool ClientWebGLContext::IsVertexArray(const WebGLVertexArrayJS* const obj) const {
  const FuncScope funcScope(*this, "isVertexArray");
  if (IsContextLost()) return false;

  return obj && obj->IsUsable(*this) && obj->mHasBeenBound;
}

// ------------------------- GL State -------------------------
bool ClientWebGLContext::IsContextLost() const {
  if (!mNotLost) return true;
  return Run<RPROC(IsContextLost)>();
}

void ClientWebGLContext::Disable(GLenum cap) { Run<RPROC(Disable)>(cap); }

void ClientWebGLContext::Enable(GLenum cap) { Run<RPROC(Enable)>(cap); }

bool ClientWebGLContext::IsEnabled(GLenum cap) {
  return Run<RPROC(IsEnabled)>(cap);
}

void ClientWebGLContext::GetInternalformatParameter(
    JSContext* cx, GLenum target, GLenum internalformat, GLenum pname,
    JS::MutableHandleValue retval, ErrorResult& rv) const {
  Maybe<nsTArray<int32_t>> maybeArr =
      Run<RPROC(GetInternalformatParameter)>(target, internalformat, pname);
  if (!maybeArr) {
    retval.setObjectOrNull(nullptr);
    return;
  }

  nsTArray<int32_t>& arr = maybeArr.ref();
  // zero-length array indicates out-of-memory
  JSObject* obj = arr.Length()
                      ? dom::Int32Array::Create(cx, this, arr.Length(), &arr[0])
                      : nullptr;
  if (!obj) {
    rv = NS_ERROR_OUT_OF_MEMORY;
  }
  retval.setObjectOrNull(obj);
}

void ClientWebGLContext::GetParameter(JSContext* cx, GLenum pname,
                                      JS::MutableHandle<JS::Value> retval,
                                      ErrorResult& rv) {
  retval.set(ToJSValue(cx, Run<RPROC(GetParameter)>(pname), rv));
}

void ClientWebGLContext::GetBufferParameter(
    JSContext* cx, GLenum target, GLenum pname,
    JS::MutableHandle<JS::Value> retval) {
  ErrorResult unused;
  retval.set(
      ToJSValue(cx, Run<RPROC(GetBufferParameter)>(target, pname), unused));
}

void ClientWebGLContext::GetFramebufferAttachmentParameter(
    JSContext* cx, GLenum target, GLenum attachment, GLenum pname,
    JS::MutableHandle<JS::Value> retval, ErrorResult& rv) {
  retval.set(ToJSValue(
      cx,
      Run<RPROC(GetFramebufferAttachmentParameter)>(target, attachment, pname),
      rv));
}

void ClientWebGLContext::GetRenderbufferParameter(
    JSContext* cx, GLenum target, GLenum pname,
    JS::MutableHandle<JS::Value> retval) {
  ErrorResult unused;
  retval.set(ToJSValue(cx, Run<RPROC(GetRenderbufferParameter)>(target, pname),
                       unused));
}

void ClientWebGLContext::GetIndexedParameter(JSContext* cx, GLenum target,
                                             GLuint index,
                                             JS::MutableHandleValue retval,
                                             ErrorResult& rv) {
  ErrorResult unused;
  retval.set(
      ToJSValue(cx, Run<RPROC(GetIndexedParameter)>(target, index), unused));
}

void ClientWebGLContext::GetUniform(JSContext* const cx,
                                    const WebGLProgramJS& prog,
                                    const WebGLUniformLocationJS& loc,
                                    JS::MutableHandle<JS::Value> retval) const {
  ErrorResult ignored;
  retval.set(ToJSValue(cx, Run<RPROC(GetUniform)>(prog.mId, loc.mLoc), ignored));
}

RefPtr<WebGLShaderPrecisionFormatJS>
ClientWebGLContext::GetShaderPrecisionFormat(const GLenum shadertype,
                                             const GLenum precisiontype) const {
  const auto info = Run<RPROC(GetShaderPrecisionFormat)>(shadertype, precisiontype);
  if (!info) return nullptr;
  return new WebGLShaderPrecisionFormatJS(*this, info);
}

void ClientWebGLContext::BlendColor(GLclampf r, GLclampf g, GLclampf b,
                                    GLclampf a) {
  Run<RPROC(BlendColor)>(r, g, b, a);
}

void ClientWebGLContext::BlendEquationSeparate(GLenum modeRGB,
                                               GLenum modeAlpha) {
  Run<RPROC(BlendEquationSeparate)>(modeRGB, modeAlpha);
}

void ClientWebGLContext::BlendFuncSeparate(GLenum srcRGB, GLenum dstRGB,
                                           GLenum srcAlpha, GLenum dstAlpha) {
  Run<RPROC(BlendFuncSeparate)>(srcRGB, dstRGB, srcAlpha, dstAlpha);
}

GLenum ClientWebGLContext::CheckFramebufferStatus(GLenum target) {
  return Run<RPROC(CheckFramebufferStatus)>(target);
}

void ClientWebGLContext::Clear(GLbitfield mask) {
  Run<RPROC(Clear)>(mask);

  AfterDrawCall();
}

// -

void ClientWebGLContext::ClearBufferv(const GLenum buffer, const GLint drawBuffer,
    const webgl::AttribBaseType type, const Range<const uint8_t>& view,
    const GLuint srcElemOffset) const {
  const auto offset = CheckedInt<size_t>(sizeof(float)) * srcElemOffset;
  if (!offset.isValid() || offset.value() >= view.Length()) {
    EnqueueError(LOCAL_GL_INVALID_VALUE, "`srcElemOffset` larger than ArrayBufferView.");
    return;
  }

  const auto rb = RawBuffer<const uint8_t>{view.Length() - offset,
                                           view.Data() + offset};

  Run<RPROC(ClearBufferv)>(buffer, drawBuffer, rb);

  AfterDrawCall();
}

void ClientWebGLContext::ClearBufferfi(GLenum buffer, GLint drawBuffer,
                                       GLfloat depth, GLint stencil) {
  Run<RPROC(ClearBufferfi)>(buffer, drawBuffer, depth, stencil);

  AfterDrawCall();
}

// -

void ClientWebGLContext::ClearColor(GLclampf r, GLclampf g, GLclampf b,
                                    GLclampf a) {
  Run<RPROC(ClearColor)>(r, g, b, a);
}

void ClientWebGLContext::ClearDepth(GLclampf v) { Run<RPROC(ClearDepth)>(v); }

void ClientWebGLContext::ClearStencil(GLint v) { Run<RPROC(ClearStencil)>(v); }

void ClientWebGLContext::ColorMask(WebGLboolean r, WebGLboolean g,
                                   WebGLboolean b, WebGLboolean a) {
  Run<RPROC(ColorMask)>(r, g, b, a);
}

void ClientWebGLContext::CullFace(GLenum face) { Run<RPROC(CullFace)>(face); }

void ClientWebGLContext::DepthFunc(GLenum func) { Run<RPROC(DepthFunc)>(func); }

void ClientWebGLContext::DepthMask(WebGLboolean b) { Run<RPROC(DepthMask)>(b); }

void ClientWebGLContext::DepthRange(GLclampf zNear, GLclampf zFar) {
  Run<RPROC(DepthRange)>(zNear, zFar);
}

void ClientWebGLContext::Flush() { Run<RPROC(Flush)>(); }

void ClientWebGLContext::Finish() { Run<RPROC(Finish)>(); }

void ClientWebGLContext::FrontFace(GLenum mode) { Run<RPROC(FrontFace)>(mode); }

GLenum ClientWebGLContext::GetError() { return Run<RPROC(GetError)>(); }

void ClientWebGLContext::Hint(GLenum target, GLenum mode) {
  Run<RPROC(Hint)>(target, mode);
}

void ClientWebGLContext::LineWidth(GLfloat width) {
  Run<RPROC(LineWidth)>(width);
}

void ClientWebGLContext::PixelStorei(GLenum pname, GLint param) {
  mPixelStore = Run<RPROC(PixelStorei)>(pname, param);
}

void ClientWebGLContext::PolygonOffset(GLfloat factor, GLfloat units) {
  Run<RPROC(PolygonOffset)>(factor, units);
}

void ClientWebGLContext::SampleCoverage(GLclampf value, WebGLboolean invert) {
  Run<RPROC(SampleCoverage)>(value, invert);
}

void ClientWebGLContext::Scissor(GLint x, GLint y, GLsizei width,
                                 GLsizei height) {
  Run<RPROC(Scissor)>(x, y, width, height);
}

void ClientWebGLContext::StencilFuncSeparate(GLenum face, GLenum func,
                                             GLint ref, GLuint mask) {
  Run<RPROC(StencilFuncSeparate)>(face, func, ref, mask);
}

void ClientWebGLContext::StencilMaskSeparate(GLenum face, GLuint mask) {
  Run<RPROC(StencilMaskSeparate)>(face, mask);
}

void ClientWebGLContext::StencilOpSeparate(GLenum face, GLenum sfail,
                                           GLenum dpfail, GLenum dppass) {
  Run<RPROC(StencilOpSeparate)>(face, sfail, dpfail, dppass);
}

void ClientWebGLContext::Viewport(GLint x, GLint y, GLsizei width,
                                  GLsizei height) {
  Run<RPROC(Viewport)>(x, y, width, height);
}

// ------------------------- Buffer Objects -------------------------

Maybe<const webgl::ErrorInfo> ValidateBindBuffer(const GLenum target,
  const webgl::BufferKind curKind) {
  if (curKind == webgl::BufferKind::Undefined) return {};

  auto requiredKind = webgl::BufferKind::NonIndex;
  switch (target) {
    case LOCAL_GL_COPY_READ_BUFFER:
    case LOCAL_GL_COPY_WRITE_BUFFER:
      return {}; // Always ok

    case LOCAL_GL_ELEMENT_ARRAY_BUFFER:
      requiredKind = webgl::BufferKind::Index;
      break;

    default:
      break;
  }

  if (curKind != requiredKind) {
    const auto fnKindStr = [&](const webgl::BufferKind kind) {
      if (kind == webgl::BufferKind::Index) return "ELEMENT_ARRAY_BUFFER";
      return "non-ELEMENT_ARRAY_BUFFER";
    };
    const auto info = nsPrintfCString("Buffer previously bound to %s cannot be now bound to %s.",
                  fnKindStr(curKind), fnKindStr(requiredKind));
    return Some({LOCAL_GL_INVALID_OPERATION, info});
  }

  return {};
}

Maybe<const webgl::ErrorInfo> ValidateBindBufferRange(const GLenum target, const GLuint index,
                                    const bool isBuffer,
                                    const uint64_t offset, const uint64_t size,
                                    const webgl::InitContextResult& limits)
{
  switch (target) {
    case LOCAL_GL_TRANSFORM_FEEDBACK_BUFFER:
      if (index >= limits.maxTransformFeedbackSeparateAttribs) {
        const auto info = nsPrintfCString(
            "`index` (%u) must be less than MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS (%u).",
            index, limits.maxTransformFeedbackSeparateAttribs);
        return Some({LOCAL_GL_INVALID_VALUE, info});
      }

      if (isBuffer) {
        if (offset % 4 != 0 || size % 4 != 0) {
          const auto info = nsPrintfCString(
              "`offset` (%u) and `size` (%u) must both be aligned to 4 for"
              " TRANSFORM_FEEDBACK_BUFFER.",
              offset, size);
          return Some({LOCAL_GL_INVALID_VALUE, info});
        }
      }
      break;

    case LOCAL_GL_UNIFORM_BUFFER:
      if (index >= limits.maxUniformBufferBindings) {
        const auto info = nsPrintfCString(
            "`index` (%u) must be less than MAX_UNIFORM_BUFFER_BINDINGS (%u).",
            index, limits.maxUniformBufferBindings);
        return Some({LOCAL_GL_INVALID_VALUE, info});
      }

      if (isBuffer) {
        if (offset % limits.uniformBufferOffsetAlignment != 0) {
          const auto info = nsPrintfCString(
              "`offset` (%u) must be aligned to UNIFORM_BUFFER_OFFSET_ALIGNMENT (%u).",
              offset, limits.uniformBufferOffsetAlignment);
          return Some({LOCAL_GL_INVALID_VALUE, info});
        }
      }
      break;

    default: {
      const auto info = nsPrintfCString("Unrecognized `target`: 0x%04x", target);
      return Some({LOCAL_GL_INVALID_ENUM, info});
    }
  }

  return {};
}

// -

void ClientWebGLContext::BindBuffer(const GLenum target,
                                    const WebGLBufferJS* const buffer) {
  const FuncScope funcScope(*this, "bindBuffer");
  if (IsContextLost()) return;
  if (buffer && !buffer.ValidateUsable(*this, "buffer")) return;

  // -
  // Check for INVALID_ENUM

  auto& state = *(mNotLost->generation);
  auto* slot = &(state.mBoundVao->mIndexBuffer);
  if (target != LOCAL_GL_ELEMENT_ARRAY_BUFFER) {
    const auto itr = state.mBoundBufferByTarget.find(target);
    if (itr == state.mBoundBufferByTarget.end()) {
      EnqueueError_ArgEnum("target", target);
      return;
    }
    slot = &(itr->second);
  }

  // -

  auto kind = webgl::BufferKind::Undefined;
  if (buffer) {
    kind = buffer->mKind;
  }
  const auto err = ValidateBindBuffer(target, kind);
  if (err) {
    EnqueueError(err->type, err->info);
    return;
  }

  // -
  // Validation complete

  if (buffer && buffer->mKind == webgl::BufferKind::Undefined) {
    if (target == LOCAL_GL_ELEMENT_ARRAY_BUFFER) {
      buffer->mKind = webgl::BufferKind::Index;
    } else {
      buffer->mKind = webgl::BufferKind::NonIndex;
    }
  }
  *slot = buffer;

  // -

  Run<RPROC(BindBuffer)>(target, buffer ? buffer->mId : 0);
}

// -

void ClientWebGLContext::BindBufferRangeImpl(const GLenum target, const GLuint index,
                                         const WebGLBufferJS* const buffer,
                                         const uint64_t offset, const uint64_t size) {
  if (buffer && !buffer.ValidateUsable(*this, "buffer")) return;

  // -

  const auto& limits = mNotLost->info;
  auto err = ValidateBindBufferRange(target, index, bool(buffer), offset, size, limits);
  if (err) {
    EnqueueError(err->type, err->info);
    return;
  }

  // -

  auto kind = webgl::BufferKind::Undefined;
  if (buffer) {
    kind = buffer->mKind;
  }
  err = ValidateBindBuffer(target, kind);
  if (err) {
    EnqueueError(err->type, err->info);
    return;
  }

  // -
  // Validation complete

  if (buffer && buffer->mKind == webgl::BufferKind::Undefined) {
    buffer->mKind = webgl::BufferKind::NonIndex;
  }

  // -

  auto& state = *(mNotLost->generation);

  switch (target) {
    case LOCAL_GL_TRANSFORM_FEEDBACK_BUFFER:
      state.mBoundTfo->mAttribBuffers[index] = buffer;
      break;

    case LOCAL_GL_UNIFORM_BUFFER:
      state.mBoundUbos[index] = buffer;
      break;

    default:
      MOZ_CRASH("Bad `target`");
  }
  state.mBoundBufferByTarget[target] = buffer;

  // -

  Run<RPROC(BindBufferRange)>(target, index, buffer ? buffer->mId : 0, offset, size);
}

void ClientWebGLContext::GetBufferSubData(GLenum target, GLintptr srcByteOffset,
                                          const dom::ArrayBufferView& dstData,
                                          GLuint dstElemOffset,
                                          GLuint dstElemCountOverride) {
  if (!ValidateNonNegative("srcByteOffset", srcByteOffset)) return;

  uint8_t* bytes;
  size_t byteLen;
  if (!ValidateArrayBufferView(dstData, dstElemOffset, dstElemCountOverride,
                               LOCAL_GL_INVALID_VALUE, &bytes, &byteLen)) {
    return;
  }

  Maybe<UniquePtr<RawBuffer<>>> result =
      Run<RPROC(GetBufferSubData)>(target, srcByteOffset, byteLen);
  if (!result) {
    return;
  }
  MOZ_ASSERT(result.ref()->Length() == byteLen);
  memcpy(bytes, result.ref()->Data(), byteLen);
}

////

void ClientWebGLContext::BufferData(GLenum target, WebGLsizeiptr size,
                                    GLenum usage) {
  const FuncScope funcScope(this, "bufferData");
  if (!ValidateNonNegative("size", size)) return;

  UniqueBuffer zeroBuffer(calloc(size, 1));
  if (!zeroBuffer)
    return EnqueueError(LOCAL_GL_OUT_OF_MEMORY, "Failed to allocate zeros.");

  Run<RPROC(BufferData)>(
      target, RawBuffer<>(size_t(size), (uint8_t*)zeroBuffer.get()), usage);
}

void ClientWebGLContext::BufferData(
    GLenum target, const dom::Nullable<dom::ArrayBuffer>& maybeSrc,
    GLenum usage) {
  const FuncScope funcScope(this, "bufferData");
  if (!ValidateNonNull("src", maybeSrc)) return;
  const auto& src = maybeSrc.Value();

  src.ComputeLengthAndData();
  Run<RPROC(BufferData)>(
      target, RawBuffer<>(src.LengthAllowShared(), src.DataAllowShared()),
      usage);
}

void ClientWebGLContext::BufferData(GLenum target,
                                    const dom::ArrayBufferView& src,
                                    GLenum usage, GLuint srcElemOffset,
                                    GLuint srcElemCountOverride) {
  const FuncScope funcScope(this, "bufferData");
  uint8_t* bytes;
  size_t byteLen;
  if (!ValidateArrayBufferView(src, srcElemOffset, srcElemCountOverride,
                               LOCAL_GL_INVALID_VALUE, &bytes, &byteLen)) {
    return;
  }

  Run<RPROC(BufferData)>(target, RawBuffer<>(byteLen, bytes), usage);
}

////

void ClientWebGLContext::BufferSubData(GLenum target,
                                       WebGLsizeiptr dstByteOffset,
                                       const dom::ArrayBuffer& src) {
  const FuncScope funcScope(this, "bufferSubData");
  src.ComputeLengthAndData();
  Run<RPROC(BufferSubData)>(
      target, dstByteOffset,
      RawBuffer<>(src.LengthAllowShared(), src.DataAllowShared()));
}

void ClientWebGLContext::BufferSubData(GLenum target,
                                       WebGLsizeiptr dstByteOffset,
                                       const dom::ArrayBufferView& src,
                                       GLuint srcElemOffset,
                                       GLuint srcElemCountOverride) {
  const FuncScope funcScope(this, "bufferSubData");
  uint8_t* bytes;
  size_t byteLen;
  if (!ValidateArrayBufferView(src, srcElemOffset, srcElemCountOverride,
                               LOCAL_GL_INVALID_VALUE, &bytes, &byteLen)) {
    return;
  }

  Run<RPROC(BufferSubData)>(target, dstByteOffset, RawBuffer<>(byteLen, bytes));
}

void ClientWebGLContext::CopyBufferSubData(GLenum readTarget,
                                           GLenum writeTarget,
                                           GLintptr readOffset,
                                           GLintptr writeOffset,
                                           GLsizeiptr size) {
  Run<RPROC(CopyBufferSubData)>(readTarget, writeTarget, readOffset,
                                writeOffset, size);
}

// -------------------------- Framebuffer Objects --------------------------


Maybe<const webgl::ErrorInfo> ValidateBindFramebuffer(const bool isWebgl2, const GLenum target) {
  switch (target) {
    case LOCAL_GL_FRAMEBUFFER:
      return {};

    case LOCAL_GL_DRAW_FRAMEBUFFER:
    case LOCAL_GL_READ_FRAMEBUFFER:
      if (isWebgl2) return {};
      break;

    default:
      break;
  }
  const auto info = nsPrintfCString("Bad `target`: 0x%04x", target)};
  return Some({LOCAL_GL_INVALID_ENUM, info});
}

void ClientWebGLContext::BindFramebuffer(const GLenum target,
                                         const WebGLFramebufferJS* const fb) {
  const FuncScope funcScope(*this, "bindFramebuffer");
  if (IsContextLost()) return;
  if (fb && !fb->ValidateUsable(*this, "fb")) return;

  const auto err = ValidateBindFramebuffer(IsWebGL2(), target);
  if (err) {
    EnqueueError(err.type, err.info);
    return;
  }

  // -

  auto& state = *(mNotLost->generation);

  switch (target) {
   case LOCAL_GL_FRAMEBUFFER:
    state.mBoundDrawFb = fb;
    state.mBoundReadFb = fb;
    break;

  case LOCAL_GL_DRAW_FRAMEBUFFER:
    state.mBoundDrawFb = fb;
    break;
  case LOCAL_GL_READ_FRAMEBUFFER:
    state.mBoundReadFb = fb;
    break;

  default:
    MOZ_CRASH();
  }

  // -

  Run<RPROC(BindFramebuffer)>(target, fb ? fb->mId : 0);
}

void ClientWebGLContext::FramebufferRenderbuffer(
    const GLenum target, const GLenum attachEnum, const GLenum rbTarget,
    const WebGLRenderbufferJS* const rb) const {
  if (rbTarget != LOCAL_GL_RENDERBUFFER) {
    EnqueueError(LOCAL_GL_INVALID_ENUM, "`rbTarget` must be RENDERBUFFER.");
    return;
  }
  Run<RPROC(FramebufferAttach)>(target, attachEnum, LOCAL_GL_RENDERBUFFER, rb ? rb->mId : 0,
                                0, 0, 0);
}

void ClientWebGLContext::FramebufferTexture2D(GLenum target, GLenum attachEnum,
                                              GLenum texImageTarget,
                                              const WebGLTextureJS* const tex,
                                              GLint mipLevel) const {
  Run<RPROC(FramebufferAttach)>(target, attachEnum, texImageTarget, tex ? tex->mId : 0,
                                mipLevel, 0, -1);
}

void ClientWebGLContext::FramebufferTextureLayer(
    GLenum target, GLenum attachEnum, const WebGLTextureJS* const tex,
    GLint mipLevel, GLint zLayer) const {
  Run<RPROC(FramebufferAttach)>(target, attachEnum, tex.mTarget, tex ? tex->mId : 0,
                                mipLevel, zLayer, 0);
}

void ClientWebGLContext::FramebufferTextureMultiview(
    const GLenum target, const GLenum attachEnum,
    const WebGLTextureJS* const tex, const GLint mipLevel,
    const GLint zLayerBase, const GLsizei numViewLayers) const {
  if (tex) {
    if (numViewLayers < 1) {
      EnqueueError(LOCAL_GL_INVALID_VALUE, "`numViewLayers` must be >= 1.");
      return;
    }
  }

  Run<RPROC(FramebufferAttach)>(target, attachEnum, tex.mTarget, tex ? tex->mId : 0,
                                mipLevel, zLayerBase, numViewLayers);
}

// -

void ClientWebGLContext::BlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1,
                                         GLint srcY1, GLint dstX0, GLint dstY0,
                                         GLint dstX1, GLint dstY1,
                                         GLbitfield mask, GLenum filter) {
  Run<RPROC(BlitFramebuffer)>(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1,
                              dstY1, mask, filter);

  AfterDrawCall();
}

void ClientWebGLContext::InvalidateFramebuffer(
    GLenum target, const dom::Sequence<GLenum>& attachments,
    ErrorResult& unused) {
  Run<RPROC(InvalidateFramebuffer)>(target, nsTArray<GLenum>(attachments));

  // Never invalidate the backbuffer, so never needs AfterDrawCall.
}

void ClientWebGLContext::InvalidateSubFramebuffer(
    GLenum target, const dom::Sequence<GLenum>& attachments, GLint x, GLint y,
    GLsizei width, GLsizei height, ErrorResult& unused) {
  Run<RPROC(InvalidateSubFramebuffer)>(target, nsTArray<GLenum>(attachments), x,
                                       y, width, height);

  // Never invalidate the backbuffer, so never needs AfterDrawCall.
}

void ClientWebGLContext::ReadBuffer(GLenum mode) {
  Run<RPROC(ReadBuffer)>(mode);
}

// ----------------------- Renderbuffer objects -----------------------

void ClientWebGLContext::BindRenderbuffer(const GLenum target,
      const WebGLRenderbufferJS* const rb) {
  const FuncScope funcScope(*this, "bindRenderbuffer");
  if (IsContextLost()) return;
  if (rb && !rb->ValidateUsable(*this, "rb")) return;

  if (target != LOCAL_GL_RENDERBUFFER) {
    EnqueueError_ArgEnum("target", target);
    return;
  }

  mBoundRb = rb;
  if (rb) {
    rb->mHasBeenBound = true;
  }
}

void ClientWebGLContext::RenderbufferStorageMultisample(GLenum target,
                                                        GLsizei samples,
                                                        GLenum internalFormat,
                                                        GLsizei width,
                                                        GLsizei height) const {
  const FuncScope funcScope(*this, "renderbufferStorageMultisample");
  if (IsContextLost()) return;

  if (target != LOCAL_GL_RENDERBUFFER) {
    EnqueueError_ArgEnum("target", target);
    return;
  }

  const auto& state = *(mNotLost->generation);

  const auto& rb = state.mBoundRb;
  if (!rb) {
    EnqueueError(LOCAL_GL_INVALID_OPERATION, "No renderbuffer bound");
    return;
  }

  Run<RPROC(RenderbufferStorageMultisample)>(rb->mId, samples, internalFormat, width, height);
}

// --------------------------- Texture objects ---------------------------

void ClientWebGLContext::ActiveTexture(const GLenum texUnitEnum) {
  const FuncScope funcScope(*this, "activeTexture");
  if (IsContextLost()) return;

  if (texUnitEnum < LOCAL_GL_TEXTURE0) {
    EnqueueError(LOCAL_GL_INVALID_VALUE, "`texture` (0x%04x) must be >= TEXTURE0 (0x%04x).",
                texUnitEnum, LOCAL_GL_TEXTURE0);
    return;
  }

  const auto texUnit = texUnitEnum - LOCAL_GL_TEXTURE0;

  auto& state = *(mNotLost->generation);
  if (texUnit >= state.mTexUnits.size()) {
    EnqueueError(LOCAL_GL_INVALID_VALUE,
      "TEXTURE%u must be < MAX_COMBINED_TEXTURE_IMAGE_UNITS (%tu).",
      texUnit, state.mTexUnits.size());
    return;
  }

  //-

  state.mActiveTexUnit = texUnit;
  Run<RPROC(ActiveTexture)>(texUnit);
}

void ClientWebGLContext::BindTexture(const GLenum texTarget,
                                     const WebGLTextureJS* const tex) {
  const FuncScope funcScope(*this, "bindTexture");
  if (IsContextLost()) return;
  if (tex && !tex->ValidateUsable(*this, "tex")) return;

  const bool valid = [&]() {
    switch (texTarget) {
      case LOCAL_GL_TEXTURE_2D:
      case LOCAL_GL_TEXTURE_CUBE_MAP:
        return true;

      case LOCAL_GL_TEXTURE_2D_ARRAY:
      case LOCAL_GL_TEXTURE_3D:
        return IsWebGL2();

      default:
        return false;
    }
  }();
  if (!valid) {
    EnqueueError_ArgEnum("texTarget", texTarget);
    return;
  }

  if (tex && tex->mTarget) {
    if (texTarget != tex->mTarget) {
      EnqueueError(LOCAL_GL_INVALID_OPERATION,
              "Texture previously bound to %s cannot be bound now to %s.",
              EnumString(tex->mTarget).c_str(), EnumString(texTarget).c_str());
      return;
    }
  }

  const auto& state = *(mNotLost->generation);
  auto& texUnit = state.mTexUnits[state.mActiveTexUnit];
  if (tex) {
    texUnit.texByTarget[texTarget] = tex;
  }

  Run<RPROC(BindTexture)>(texTarget, tex ? tex->mId : 0);
}

void ClientWebGLContext::GenerateMipmap(GLenum texTarget) {
  Run<RPROC(GenerateMipmap)>(texTarget);
}

void ClientWebGLContext::CopyTexImage2D(GLenum target, GLint level,
                                        GLenum internalFormat, GLint x, GLint y,
                                        GLsizei rawWidth, GLsizei rawHeight,
                                        GLint border) {
  uint32_t width, height, depth;
  if (!ValidateExtents(rawWidth, rawHeight, 1, border, &width, &height,
                       &depth)) {
    return;
  }

  Run<RPROC(CopyTexImage2D)>(target, level, internalFormat, x, y, width, height,
                             depth);
}

void ClientWebGLContext::GetTexParameter(JSContext* cx, GLenum texTarget,
                                         GLenum pname,
                                         JS::MutableHandle<JS::Value> retval) {
  ErrorResult ignored;
  retval.set(
      ToJSValue(cx, Run<RPROC(GetTexParameter)>(texTarget, pname), ignored));
}

void ClientWebGLContext::TexParameterf(GLenum texTarget, GLenum pname,
                                       GLfloat param) {
  Run<RPROC(TexParameter_base)>(texTarget, pname, FloatOrInt(param));
}

void ClientWebGLContext::TexParameteri(GLenum texTarget, GLenum pname,
                                       GLint param) {
  Run<RPROC(TexParameter_base)>(texTarget, pname, FloatOrInt(param));
}

void ClientWebGLContext::TexStorage(uint8_t funcDims, GLenum target, GLsizei levels, GLenum internalFormat,
                    GLsizei width, GLsizei height, GLsizei depth) const {
  Run<RPROC(TexStorage)>(funcDims, target, levels, internalFormat, width,
                         height, depth);
}

////////////////////////////////////

static inline bool DoesJSTypeMatchUnpackType(GLenum unpackType,
                                             js::Scalar::Type jsType) {
  switch (unpackType) {
    case LOCAL_GL_BYTE:
      return jsType == js::Scalar::Type::Int8;

    case LOCAL_GL_UNSIGNED_BYTE:
      return jsType == js::Scalar::Type::Uint8 ||
             jsType == js::Scalar::Type::Uint8Clamped;

    case LOCAL_GL_SHORT:
      return jsType == js::Scalar::Type::Int16;

    case LOCAL_GL_UNSIGNED_SHORT:
    case LOCAL_GL_UNSIGNED_SHORT_4_4_4_4:
    case LOCAL_GL_UNSIGNED_SHORT_5_5_5_1:
    case LOCAL_GL_UNSIGNED_SHORT_5_6_5:
    case LOCAL_GL_HALF_FLOAT:
    case LOCAL_GL_HALF_FLOAT_OES:
      return jsType == js::Scalar::Type::Uint16;

    case LOCAL_GL_INT:
      return jsType == js::Scalar::Type::Int32;

    case LOCAL_GL_UNSIGNED_INT:
    case LOCAL_GL_UNSIGNED_INT_2_10_10_10_REV:
    case LOCAL_GL_UNSIGNED_INT_10F_11F_11F_REV:
    case LOCAL_GL_UNSIGNED_INT_5_9_9_9_REV:
    case LOCAL_GL_UNSIGNED_INT_24_8:
      return jsType == js::Scalar::Type::Uint32;

    case LOCAL_GL_FLOAT:
      return jsType == js::Scalar::Type::Float32;

    default:
      return false;
  }
}

////////////////////////////////////

bool ClientWebGLContext::ValidateViewType(GLenum unpackType,
                                          const TexImageSource& src) {
  if (!src.mView) return true;
  const auto& view = *(src.mView);

  const auto& jsType = view.Type();
  if (!DoesJSTypeMatchUnpackType(unpackType, jsType)) {
    EnqueueError(LOCAL_GL_INVALID_OPERATION,
                 "ArrayBufferView type not compatible with `type`.");
    return false;
  }

  return true;
}

////////////////////////////////////

void ClientWebGLContext::TexImage2D(GLenum target, GLint level,
                                    GLenum internalFormat, GLsizei width,
                                    GLsizei height, GLint border,
                                    GLenum unpackFormat, GLenum unpackType,
                                    const TexImageSource& src) {
  const FuncScope scope(this, FuncScopeId::texImage2D);
  const uint8_t funcDims = 2;
  const GLsizei depth = 1;

  if (!ValidateViewType(unpackType, src)) {
    return;
  }

  MaybeWebGLTexUnpackVariant blob =
      From(target, width, height, depth, border, src);
  if (!blob) {
    return;
  }

  Run<RPROC(TexImage)>(funcDims, target, level, internalFormat, width, height,
                       depth, border, unpackFormat, unpackType, std::move(blob),
                       GetFuncScopeId());
}

////////////////////////////////////

void ClientWebGLContext::TexSubImage2D(GLenum target, GLint level,
                                       GLint xOffset, GLint yOffset,
                                       GLsizei width, GLsizei height,
                                       GLenum unpackFormat, GLenum unpackType,
                                       const TexImageSource& src) {
  const FuncScope scope(this, FuncScopeId::texSubImage2D);
  const uint8_t funcDims = 2;
  const GLint zOffset = 0;
  const GLsizei depth = 1;

  if (!ValidateViewType(unpackType, src)) {
    return;
  }

  MaybeWebGLTexUnpackVariant blob = From(target, width, height, depth, 0, src);
  if (!blob) {
    return;
  }

  Run<RPROC(TexSubImage)>(funcDims, target, level, xOffset, yOffset, zOffset,
                          width, height, depth, unpackFormat, unpackType,
                          std::move(blob), GetFuncScopeId());
}

////////////////////////////////////

void ClientWebGLContext::TexImage3D(GLenum target, GLint level,
                                    GLenum internalFormat, GLsizei width,
                                    GLsizei height, GLsizei depth, GLint border,
                                    GLenum unpackFormat, GLenum unpackType,
                                    const TexImageSource& src) {
  const FuncScope scope(this, FuncScopeId::texImage3D);
  const uint8_t funcDims = 3;

  MaybeWebGLTexUnpackVariant blob =
      From(target, width, height, depth, border, src);
  if (!blob) {
    return;
  }
  Run<RPROC(TexImage)>(funcDims, target, level, internalFormat, width, height,
                       depth, border, unpackFormat, unpackType, std::move(blob),
                       GetFuncScopeId());
}

////////////////////////////////////

void ClientWebGLContext::TexSubImage3D(GLenum target, GLint level,
                                       GLint xOffset, GLint yOffset,
                                       GLint zOffset, GLsizei width,
                                       GLsizei height, GLsizei depth,
                                       GLenum unpackFormat, GLenum unpackType,
                                       const TexImageSource& src) {
  const FuncScope scope(this, FuncScopeId::texSubImage3D);
  const uint8_t funcDims = 3;

  MaybeWebGLTexUnpackVariant blob = From(target, width, height, depth, 0, src);
  if (!blob) {
    return;
  }
  Run<RPROC(TexSubImage)>(funcDims, target, level, xOffset, yOffset, zOffset,
                          width, height, depth, unpackFormat, unpackType,
                          std::move(blob), GetFuncScopeId());
}

////////////////////////////////////

void ClientWebGLContext::CopyTexSubImage2D(GLenum target, GLint level,
                                           GLint xOffset, GLint yOffset,
                                           GLint x, GLint y, GLsizei rawWidth,
                                           GLsizei rawHeight) {
  const FuncScope scope(this, FuncScopeId::copyTexSubImage2D);
  const uint8_t funcDims = 2;
  const GLint zOffset = 0;

  uint32_t width, height, depth;
  if (!ValidateExtents(rawWidth, rawHeight, 1, 0, &width, &height, &depth)) {
    return;
  }

  Run<RPROC(CopyTexSubImage)>(funcDims, target, level, xOffset, yOffset,
                              zOffset, x, y, width, height, depth,
                              GetFuncScopeId());
}

////////////////////////////////////

void ClientWebGLContext::CopyTexSubImage3D(GLenum target, GLint level,
                                           GLint xOffset, GLint yOffset,
                                           GLint zOffset, GLint x, GLint y,
                                           GLsizei rawWidth,
                                           GLsizei rawHeight) {
  const FuncScope scope(this, FuncScopeId::copyTexSubImage3D);
  const uint8_t funcDims = 3;

  uint32_t width, height, depth;
  if (!ValidateExtents(rawWidth, rawHeight, 1, 0, &width, &height, &depth)) {
    return;
  }

  Run<RPROC(CopyTexSubImage)>(funcDims, target, level, xOffset, yOffset,
                              zOffset, x, y, width, height, depth,
                              GetFuncScopeId());
}

void ClientWebGLContext::TexImage(uint8_t funcDims, GLenum target, GLint level,
                                  GLenum internalFormat, GLsizei width,
                                  GLsizei height, GLsizei depth, GLint border,
                                  GLenum unpackFormat, GLenum unpackType,
                                  const TexImageSource& src,
                                  FuncScopeId aFuncId) {
  const FuncScope scope(this, FuncScopeId::texImage2D);
  MaybeWebGLTexUnpackVariant blob =
      From(target, width, height, depth, border, src);
  if (!blob) {
    return;
  }
  Run<RPROC(TexImage)>(funcDims, target, level, internalFormat, width, height,
                       depth, border, unpackFormat, unpackType, std::move(blob),
                       aFuncId);
}

void ClientWebGLContext::TexSubImage(uint8_t funcDims, GLenum target,
                                     GLint level, GLint xOffset, GLint yOffset,
                                     GLint zOffset, GLsizei width,
                                     GLsizei height, GLsizei depth,
                                     GLenum unpackFormat, GLenum unpackType,
                                     const TexImageSource& src,
                                     FuncScopeId aFuncId) {
  MaybeWebGLTexUnpackVariant blob = From(target, width, height, depth, 0, src);
  if (!blob) {
    return;
  }
  Run<RPROC(TexSubImage)>(funcDims, target, level, xOffset, yOffset, zOffset,
                          width, height, depth, unpackFormat, unpackType,
                          std::move(blob), aFuncId);
}

void ClientWebGLContext::CompressedTexImage(
    uint8_t funcDims, GLenum target, GLint level, GLenum internalFormat,
    GLsizei width, GLsizei height, GLsizei depth, GLint border,
    const TexImageSource& src, const Maybe<GLsizei>& expectedImageSize,
    FuncScopeId aFuncId) {
  MaybeWebGLTexUnpackVariant blob = FromCompressed(
      target, width, height, depth, border, src, expectedImageSize);
  if (!blob) {
    return;
  }
  Run<RPROC(CompressedTexImage)>(funcDims, target, level, internalFormat, width,
                                 height, depth, border, std::move(blob),
                                 expectedImageSize, aFuncId);
}

void ClientWebGLContext::CompressedTexSubImage(
    uint8_t funcDims, GLenum target, GLint level, GLint xOffset, GLint yOffset,
    GLint zOffset, GLsizei width, GLsizei height, GLsizei depth,
    GLenum unpackFormat, const TexImageSource& src,
    const Maybe<GLsizei>& expectedImageSize, FuncScopeId aFuncId) {
  MaybeWebGLTexUnpackVariant blob =
      FromCompressed(target, width, height, depth, 0, src, expectedImageSize);
  if (!blob) {
    return;
  }
  Run<RPROC(CompressedTexSubImage)>(
      funcDims, target, level, xOffset, yOffset, zOffset, width, height, depth,
      unpackFormat, std::move(blob), expectedImageSize, aFuncId);
}

// ------------------- Programs and shaders --------------------------------

void ClientWebGLContext::UseProgram(const WebGLProgramJS* const prog) {
  const FuncScope funcScope(*this, "useProgram");
  if (IsContextLost()) return;
  if (prog && !prog->ValidateUsable(*this, "prog")) return;

  auto& state = *(mNotLost->generation);

  if (state.mTfActiveAndNotPaused) {
    EnqueueError(LOCAL_GL_INVALID_OPERATION, "Transform feedback is active and not paused.");
    return;
  }

  if (prog) {
    const auto& res = GetProgramResult(*prog);
    if (!res.success) {
      EnqueueError(LOCAL_GL_INVALID_OPERATION, "Program be linked successfully.");
      return;
    }
  }

  // -

  state.mCurrentProgram = prog;

  Run<RPROC(UseProgram)>(prog);
}

void ClientWebGLContext::ValidateProgram(const WebGLProgramJS& prog) {
  Run<RPROC(ValidateProgram)>(prog.mId);
}

// ------------------------ Uniforms and attributes ------------------------

void ClientWebGLContext::GetVertexAttrib(JSContext* cx, GLuint index,
                                         GLenum pname,
                                         JS::MutableHandle<JS::Value> retval,
                                         ErrorResult& rv) {
  retval.set(ToJSValue(cx, Run<RPROC(GetVertexAttrib)>(index, pname), rv));
}

void ClientWebGLContext::UniformNTV(const WebGLUniformLocation* const loc, const uint8_t n,
              const webgl::AttribBaseType t, const bool v, const Range<const uint8_t>& bytes) const {
  if (!loc) return;
  Run<RPROC(UniformNTV)>(loc->mId, n, t, v, bytes);
}

void ClientWebGLContext::UniformMatrixAxBfv(const uint8_t a, const uint8_t b,
  const WebGLUniformLocationJS* const loc, bool transpose,
                        const Range<const float>& data, GLuint elemOffset, GLuint elemCountOverride) const;
  if (!loc) return;
  auto len = data.Length();
  if (elemOffset > len) {
    EnqueueError(LOCAL_GL_INVALID_VALUE, "`elemOffset` too large for `data`.");
    return;
  }
  len

  Run<RPROC(UniformMatrixAxBfv)>(a, b, loc->mId, transpose, n, t, v, bytes);
}

// -

void ClientWebGLContext::BindVertexArray(const WebGLVertexArrayJS* const vao) {
  const FuncScope funcScope(*this, "bindVertexArray");
  if (IsContextLost()) return;
  if (vao && !vao->ValidateUsable(*this, "vao")) return;

  if (vao) {
    vao->mHasBeenBound = true;
    mBoundVao = vao;
  } else {
    mBoundVao = mDefaultVao;
  }

  Run<RPROC(BindVertexArray)>(vao ? vao->mId : 0);
}

void ClientWebGLContext::EnableVertexAttribArray(GLuint index) {
  Run<RPROC(EnableVertexAttribArray)>(index);
}

void ClientWebGLContext::DisableVertexAttribArray(GLuint index) {
  Run<RPROC(DisableVertexAttribArray)>(index);
}

WebGLsizeiptr ClientWebGLContext::GetVertexAttribOffset(GLuint index,
                                                        GLenum pname) {
  return Run<RPROC(GetVertexAttribOffset)>(index, pname);
}

void ClientWebGLContext::VertexAttrib1fv(GLuint index,
                                         const Float32ListU& list) {
  const FuncScope funcScope(this, FuncScopeId::vertexAttrib1fv);
  const auto& arr = Float32Arr::From(list);
  if (!ValidateAttribArraySetter(1, arr.elemCount)) return;

  Run<RPROC(VertexAttrib4f)>(index, arr.elemBytes[0], 0, 0, 1,
                             GetFuncScopeId());
}

void ClientWebGLContext::VertexAttrib2fv(GLuint index,
                                         const Float32ListU& list) {
  const FuncScope funcScope(this, FuncScopeId::vertexAttrib2fv);
  const auto& arr = Float32Arr::From(list);
  if (!ValidateAttribArraySetter(2, arr.elemCount)) return;

  Run<RPROC(VertexAttrib4f)>(index, arr.elemBytes[0], arr.elemBytes[1], 0, 1,
                             GetFuncScopeId());
}

void ClientWebGLContext::VertexAttrib3fv(GLuint index,
                                         const Float32ListU& list) {
  const FuncScope funcScope(this, FuncScopeId::vertexAttrib3fv);
  const auto& arr = Float32Arr::From(list);
  if (!ValidateAttribArraySetter(3, arr.elemCount)) return;

  Run<RPROC(VertexAttrib4f)>(index, arr.elemBytes[0], arr.elemBytes[1],
                             arr.elemBytes[2], 1, GetFuncScopeId());
}

void ClientWebGLContext::VertexAttrib4fv(GLuint index,
                                         const Float32ListU& list) {
  const FuncScope funcScope(this, FuncScopeId::vertexAttrib4fv);
  const auto& arr = Float32Arr::From(list);
  if (!ValidateAttribArraySetter(4, arr.elemCount)) return;

  Run<RPROC(VertexAttrib4f)>(index, arr.elemBytes[0], arr.elemBytes[1],
                             arr.elemBytes[2], arr.elemBytes[3],
                             GetFuncScopeId());
}

void ClientWebGLContext::VertexAttribIPointer(GLuint index, GLint size,
                                              GLenum type, GLsizei stride,
                                              WebGLintptr byteOffset) {
  const bool isFuncInt = true;
  const bool normalized = false;
  Run<RPROC(VertexAttribAnyPointer)>(isFuncInt, index, size, type, normalized,
                                     stride, byteOffset,
                                     FuncScopeId::vertexAttribIPointer);
}

void ClientWebGLContext::VertexAttrib4f(GLuint index, GLfloat x, GLfloat y,
                                        GLfloat z, GLfloat w,
                                        FuncScopeId aFuncId) {
  Run<RPROC(VertexAttrib4f)>(index, x, y, z, w, aFuncId);
}

void ClientWebGLContext::VertexAttribI4i(GLuint index, GLint x, GLint y,
                                         GLint z, GLint w,
                                         FuncScopeId aFuncId) {
  Run<RPROC(VertexAttribI4i)>(index, x, y, z, w, aFuncId);
}

void ClientWebGLContext::VertexAttribI4ui(GLuint index, GLuint x, GLuint y,
                                          GLuint z, GLuint w,
                                          FuncScopeId aFuncId) {
  Run<RPROC(VertexAttribI4ui)>(index, x, y, z, w, aFuncId);
}

void ClientWebGLContext::VertexAttribI4iv(GLuint index,
                                          const Int32ListU& list) {
  FuncScope scope(this, FuncScopeId::vertexAttribI4iv);

  const auto& arr = Int32Arr::From(list);
  if (!ValidateAttribArraySetter(4, arr.elemCount)) return;

  const auto& itr = arr.elemBytes;
  Run<RPROC(VertexAttribI4i)>(index, itr[0], itr[1], itr[2], itr[3],
                              FuncScopeId::vertexAttribI4iv);
}

void ClientWebGLContext::VertexAttribI4uiv(GLuint index,
                                           const Uint32ListU& list) {
  FuncScope scope(this, FuncScopeId::vertexAttribI4uiv);

  const auto& arr = Uint32Arr::From(list);
  if (!ValidateAttribArraySetter(4, arr.elemCount)) return;

  const auto& itr = arr.elemBytes;
  Run<RPROC(VertexAttribI4ui)>(index, itr[0], itr[1], itr[2], itr[3],
                               FuncScopeId::vertexAttribI4uiv);
}

void ClientWebGLContext::VertexAttribDivisor(GLuint index, GLuint divisor) {
  Run<RPROC(VertexAttribDivisor)>(index, divisor);
}

void ClientWebGLContext::VertexAttribPointer(GLuint index, GLint size,
                                             GLenum type,
                                             WebGLboolean normalized,
                                             GLsizei stride,
                                             WebGLintptr byteOffset) {
  const bool isFuncInt = false;
  Run<RPROC(VertexAttribAnyPointer)>(isFuncInt, index, size, type, normalized,
                                     stride, byteOffset,
                                     FuncScopeId::vertexAttribPointer);
}

// -------------------------------- Drawing -------------------------------

void ClientWebGLContext::DrawArraysInstanced(GLenum mode, GLint first,
                                             GLsizei count, GLsizei primcount,
                                             FuncScopeId aFuncId) {
  Run<RPROC(DrawArraysInstanced)>(mode, first, count, primcount, aFuncId);
  AfterDrawCall();
}

void ClientWebGLContext::DrawElementsInstanced(GLenum mode, GLsizei count,
                                               GLenum type, WebGLintptr offset,
                                               GLsizei primcount,
                                               FuncScopeId aFuncId) {
  Run<RPROC(DrawElementsInstanced)>(mode, count, type, offset, primcount,
                                    aFuncId);
  AfterDrawCall();
}

// ------------------------------ Readback -------------------------------
void ClientWebGLContext::ReadPixels(GLint x, GLint y, GLsizei width,
                                    GLsizei height, GLenum format, GLenum type,
                                    WebGLsizeiptr offset,
                                    dom::CallerType aCallerType,
                                    ErrorResult& out_error) {
  const FuncScope funcScope(this, "readPixels");
  if (!ReadPixels_SharedPrecheck(aCallerType, out_error)) return;
  Run<RPROC(ReadPixels1)>(x, y, width, height, format, type, offset);
}

void ClientWebGLContext::ReadPixels(GLint x, GLint y, GLsizei width,
                                    GLsizei height, GLenum format, GLenum type,
                                    const dom::ArrayBufferView& dstData,
                                    GLuint dstElemOffset,
                                    dom::CallerType aCallerType,
                                    ErrorResult& out_error) {
  const FuncScope funcScope(this, "readPixels");
  if (!ReadPixels_SharedPrecheck(aCallerType, out_error)) return;

  ////

  js::Scalar::Type reqScalarType;
  if (!GetJSScalarFromGLType(type, &reqScalarType)) {
    nsCString name;
    WebGLContext::EnumName(type, &name);
    EnqueueError(LOCAL_GL_INVALID_ENUM, "type: invalid enum value %s",
                 name.BeginReading());
    return;
  }

  const auto& viewElemType = dstData.Type();
  if (viewElemType != reqScalarType) {
    EnqueueError(LOCAL_GL_INVALID_OPERATION,
                 "`pixels` type does not match `type`.");
    return;
  }

  uint8_t* bytes;
  size_t byteLen;
  if (!ValidateArrayBufferView(dstData, dstElemOffset, 0,
                               LOCAL_GL_INVALID_VALUE, &bytes, &byteLen)) {
    return;
  }

  Maybe<UniquePtr<RawBuffer<>>> result =
      Run<RPROC(ReadPixels2)>(x, y, width, height, format, type, byteLen);
  if (!result) {
    return;
  }
  MOZ_ASSERT(result.ref()->Length() == byteLen);
  memcpy(bytes, result.ref()->Data(), byteLen);
}

bool ClientWebGLContext::ReadPixels_SharedPrecheck(CallerType aCallerType,
                                                   ErrorResult& out_error) {
  if (mCanvasElement && mCanvasElement->IsWriteOnly() &&
      aCallerType != CallerType::System) {
    EnqueueWarning("readPixels: Not allowed");
    out_error.Throw(NS_ERROR_DOM_SECURITY_ERR);
    return false;
  }

  return true;
}

// --------------------------------- GL Query ---------------------------------
void ClientWebGLContext::GetQuery(JSContext* cx, GLenum target, GLenum pname,
                                  JS::MutableHandleValue retval) const {
  const FuncScope funcScope(*this, "getQuery");
  if (IsContextLost()) return;

  if (pname != LOCAL_GL_CURRENT_QUERY) {
    EnqueueError(LOCAL_GL_INVALID_ENUM, "`pname` must be CURRENT_QUERY.");
    return;
  }

  const auto& itr = mCurrentQueryByTarget.find(target);
  if (itr == mCurrentQueryByTarget.end()) {
    EnqueueError_ArgEnum("target", target);
    return;
  }
  const auto& query = itr->second;

  ErrorResult ignored;
  retval.set(WebGLObjectAsJSValue(cx, query.get(), ignored));
}

void ClientWebGLContext::GetQueryParameter(
    JSContext*, const WebGLQueryJS& query, const GLenum pname,
    JS::MutableHandleValue retval) const {
  const FuncScope funcScope(*this, "getQueryParameter");
  if (IsContextLost()) return;
  if (!query.ValidateUsable(*this, "query")) return;

  auto& res = query.mResult;

  retval.set([&]() -> JS::Value {
    switch (pname) {
      case LOCAL_GL_QUERY_RESULT_AVAILABLE:
        return JS::BooleanValue(bool{res});
      case LOCAL_GL_QUERY_RESULT: {
        if (!res) {
          res = Run<RPROC(GetQueryResult)>(query);
        }
        if (!res) {
          EnqueueError(LOCAL_GL_INVALID_OPERATION, "Query result not yet available.");
          return JS::NullValue();
        }
        return JS::NumberValue(*res);
      }
      default:
        EnqueueError_ArgEnum("pname", pname);
        return JS::NullValue();
    }
  }());
}

void ClientWebGLContext::BeginQuery(const GLenum target,
                                    const WebGLQueryJS& query) {
  const FuncScope funcScope(*this, "beginQuery");
  if (IsContextLost()) return;
  if (!query.ValidateUsable(*this, "query")) return;

  const auto& itr = mCurrentQueryByTarget.find(target);
  if (itr == mCurrentQueryByTarget.end()) {
    EnqueueError_ArgEnum("target", target);
    return;
  }
  auto& querySlot = itr->second;

  if (query.mTarget && query.mTarget != target) {
    EnqueueError(LOCAL_GL_INVALID_OPERATION, "`query` cannot be changed to a different target.");
    return;
  }

  querySlot = query;
  query.mTarget = target;

  Run<RPROC(BeginQuery)>(target, query);
}

void ClientWebGLContext::EndQuery(const GLenum target) {
  const FuncScope funcScope(*this, "endQuery");
  if (IsContextLost()) return;

  const auto& itr = mCurrentQueryByTarget.find(target);
  if (itr == mCurrentQueryByTarget.end()) {
    EnqueueError_ArgEnum("target", target);
    return;
  }
  auto& querySlot = itr->second;
  querySlot = nullptr;

  Run<RPROC(EndQuery)>(target);
}

void ClientWebGLContext::QueryCounter(WebGLQueryJS& query,
                                      const GLenum target) const {
  const FuncScope funcScope(*this, "queryCounter");
  if (IsContextLost()) return;
  if (!query.ValidateUsable(*this, "query")) return;

  if (target != LOCAL_GL_TIMESTAMP) {
    EnqueueError(LOCAL_GL_INVALID_ENUM, "`target` must be TIMESTAMP.");
    return;
  }

  if (query.mTarget && query.mTarget != target) {
    EnqueueError(LOCAL_GL_INVALID_OPERATION, "`query` cannot be changed to a different target.");
    return;
  }
  query.mTarget = target;

  Run<RPROC(QueryCounter)>(query.mId);
}

// -------------------------------- Sampler -------------------------------
void ClientWebGLContext::GetSamplerParameter(
    JSContext* cx, const WebGLSamplerJS& sampler, const GLenum pname,
    JS::MutableHandleValue retval) const {
  const FuncScope funcScope(*this, "getSamplerParameter");
  if (IsContextLost()) return;
  if (!sampler.ValidateUsable(*this, "sampler")) return;

  ErrorResult ignored;
  retval.set(
      ToJSValue(cx, Run<RPROC(GetSamplerParameter)>(sampler.mId, pname), ignored));
}

void ClientWebGLContext::BindSampler(const GLuint unit,
                                     const WebGLSamplerJS* const sampler) {
  const FuncScope funcScope(*this, "bindSampler");
  if (IsContextLost()) return;
  if (sampler && !sampler->ValidateUsable(*this, "sampler")) return;

  if (unit >= mTexUnits.size()) {
    EnqueueError(LOCAL_GL_INVALID_VALUE, "`unit` (%u) larger than %tu.", unit, mTexUnits.size());
    return;
  }

  // -

  mTexUnits[unit].sampler = sampler;

  Run<RPROC(BindSampler)>(unit, sampler);
}

void ClientWebGLContext::SamplerParameteri(
    const WebGLSamplerJS& sampler, const GLenum pname, const GLint param) const {
  const FuncScope funcScope(*this, "samplerParameteri");
  if (IsContextLost()) return;
  if (!sampler.ValidateUsable(*this, "sampler")) return;

  Run<RPROC(SamplerParameteri)>(sampler.mId, pname, param);
}

void ClientWebGLContext::SamplerParameterf(
    const WebGLSamplerJS& sampler, const GLenum pname, const GLfloat param) const {
  const FuncScope funcScope(*this, "samplerParameterf");
  if (IsContextLost()) return;
  if (!sampler.ValidateUsable(*this, "sampler")) return;

  Run<RPROC(SamplerParameterf)>(sampler.mId, pname, param);
}

// ------------------------------- GL Sync ---------------------------------

void ClientWebGLContext::GetSyncParameter(JSContext* const cx,
                                          const WebGLSyncJS& sync,
                                          const GLenum pname,
                                          JS::MutableHandleValue retval) const {
  const FuncScope funcScope(*this, "getSyncParameter");
  if (IsContextLost()) return;
  if (!sync.ValidateUsable(*this, "sync")) return;

  retval.set([&]() -> JS::Value {
    switch (pname) {
      case LOCAL_GL_OBJECT_TYPE:
        return JS::NumberValue(LOCAL_GL_SYNC_FENCE);
      case LOCAL_GL_SYNC_CONDITION:
        return JS::NumberValue(LOCAL_GL_SYNC_GPU_COMMANDS_COMPLETE);
      case LOCAL_GL_SYNC_FLAGS:
        return JS::NumberValue(0);
      case LOCAL_GL_SYNC_STATUS:
        return JS::NumberValue(sync.mStatus);
      default:
        EnqueueError_ArgEnum("pname", pname);
        return JS::NullValue();
    }
  }());
}

GLenum ClientWebGLContext::ClientWaitSync(const WebGLSyncJS& sync,
                                          const GLbitfield flags, const GLuint64 timeout) const {
  const FuncScope funcScope(*this, "clientWaitSync");
  if (IsContextLost()) return;
  if (!sync.ValidateUsable(*this, "sync")) return;

  return Run<RPROC(ClientWaitSync)>(sync.mId, flags, timeout);
}

void ClientWebGLContext::WaitSync(const WebGLSyncJS& sync,
                                  const GLbitfield flags, const GLint64 timeout) const {
  const FuncScope funcScope(*this, "waitSync");
  if (IsContextLost()) return;
  if (!sync.ValidateUsable(*this, "sync")) return;

  if (flags != 0) {
    EnqueueError(LOCAL_GL_INVALID_VALUE, "`flags` must be 0.");
    return;
  }
  if (timeout != LOCAL_GL_TIMEOUT_IGNORED) {
    EnqueueError(LOCAL_GL_INVALID_VALUE, "`timeout` must be TIMEOUT_IGNORED.");
    return;
  }

  JsWarning("waitSync is a no-op.");
}

// -------------------------- Transform Feedback ---------------------------

void ClientWebGLContext::BindTransformFeedback(
    const GLenum target, WebGLTransformFeedbackJS& tf) {
  const FuncScope funcScope(*this, "bindTransformFeedback");
  if (IsContextLost()) return;
  if (!tf.ValidateUsable(*this, "tf")) return;

  if (target != LOCAL_GL_TRANSFORM_FEEDBACK) {
    EnqueueError(LOCAL_GL_INVALID_ENUM, "`target` must be TRANSFORM_FEEDBACK.");
    return;
  }
  if (mTfActiveAndNotPaused) {
    EnqueueError(LOCAL_GL_INVALID_OPERATION,
                 "Current Transform Feedback object is active and not paused.");
    return;
  }

  tf.mTarget = target;
  mBoundTfo = &tf;

  Run<RPROC(BindTransformFeedback)>(mBoundTfo);
}

void ClientWebGLContext::BeginTransformFeedback(const GLenum primMode) {
  const FuncScope funcScope(*this, "beginTransformFeedback");
  if (IsContextLost()) return;

  if (mBoundTfo->mActiveOrPaused) {
    EnqueueError(LOCAL_GL_INVALID_OPERATION, "Transform Feedback is already active or paused.");
    return;
  }
  MOZ_ASSERT(!mTfActiveAndNotPaused);

  if (!mActiveLinkResult) {
    EnqueueError(LOCAL_GL_INVALID_OPERATION, "No program in use.");
    return;
  }

  const auto& tfBufferCount = mActiveLinkResult->tfBufferNum;
  if (!tfBufferCount) {
    EnqueueError(LOCAL_GL_INVALID_OPERATION, "Program does not use Transform Feedback.");
    return;
  }

  const auto& buffers = mBoundTfo->mAttribBuffers;
  for (const auto& i : IntegerRange(tfBufferCount)) {
    if (!buffers[i]) {
      EnqueueError(LOCAL_GL_INVALID_OPERATION, "Transform Feedback buffer %u is null.", i);
      return;
    }
  }

  switch (primMode) {
   case LOCAL_GL_POINTS:
   case LOCAL_GL_LINES:
   case LOCAL_GL_TRIANGLES:
    break;
   default:
    EnqueueError(LOCAL_GL_INVALID_ENUM, "`primitiveMode` must be POINTS, LINES< or TRIANGLES.");
    return;
  }

  // -

  mBoundTfo->mActiveOrPaused = true;
  mBoundTfo->mRequiredLinkInfo = mActiveLinkResult;
  mTfActiveAndNotPaused = true;
  Run<RPROC(BeginTransformFeedback)>(primMode);
}

void ClientWebGLContext::EndTransformFeedback() {
  const FuncScope funcScope(*this, "endTransformFeedback");
  if (IsContextLost()) return;

  if (!mBoundTfo->mActiveOrPaused) {
    EnqueueError(LOCAL_GL_INVALID_OPERATION, "Transform Feedback is not active or paused.");
    return;
  }
  mBoundTfo->mActiveOrPaused = false;
  mBoundTfo->mRequiredLinkInfo = nullptr;
  mTfActiveAndNotPaused = false;
  Run<RPROC(EndTransformFeedback)>();
}

void ClientWebGLContext::PauseTransformFeedback() {
  const FuncScope funcScope(*this, "pauseTransformFeedback");
  if (IsContextLost()) return;

  if (mBoundTfo->mActiveOrPaused) {
    EnqueueError(LOCAL_GL_INVALID_OPERATION, "Transform Feedback is not active.");
    return;
  }
  if (!mTfActiveAndNotPaused) {
    EnqueueError(LOCAL_GL_INVALID_OPERATION, "Transform Feedback is already paused.");
    return;
  }
  mTfActiveAndNotPaused = false;
  Run<RPROC(PauseTransformFeedback)>();
}

void ClientWebGLContext::ResumeTransformFeedback() {
  const FuncScope funcScope(*this, "resumeTransformFeedback");
  if (IsContextLost()) return;

  if (mBoundTfo->mActiveOrPaused) {
    EnqueueError(LOCAL_GL_INVALID_OPERATION, "Transform Feedback is not active.");
    return;
  }
  if (mTfActiveAndNotPaused) {
    EnqueueError(LOCAL_GL_INVALID_OPERATION, "Transform Feedback is not paused.");
    return;
  }
  if (mActiveLinkResult != mBoundTfo->mRequiredLinkInfo) {
    EnqueueError(LOCAL_GL_INVALID_OPERATION,
                 "Cannot Resume Transform Feedback with a program link result different"
                 " from when Begin was called.");
    return;
  }
  mTfActiveAndNotPaused = true;
  Run<RPROC(ResumeTransformFeedback)>();
}

// ---------------------------- Misc Extensions ----------------------------
void ClientWebGLContext::DrawBuffers(const dom::Sequence<GLenum>& buffers) {
  Run<RPROC(DrawBuffers)>(nsTArray<uint32_t>(buffers));
}

void ClientWebGLContext::LoseContext(const webgl::ContextLossReason reason) {
  Run<RPROC(LoseContext)>(reason);
}

void ClientWebGLContext::MOZDebugGetParameter(
    JSContext* cx, GLenum pname, JS::MutableHandle<JS::Value> retval,
    ErrorResult& rv) const {
  retval.set(ToJSValue(cx, Run<RPROC(MOZDebugGetParameter)>(pname), rv));
}

void ClientWebGLContext::EnqueueErrorImpl(const GLenum error,
                                          const nsACString& text) const {
  if (!mNotLost) {
    JsWarning(text.BeginReading());
    return;
  }
  Run<RPROC(GenerateError)>(error, text.BeginReading());
}

void ClientWebGLContext::RequestExtension(const WebGLExtensionID ext) const {
  Run<RPROC(RequestExtension)>(ext);
}

#undef RPROC

// -

static bool IsExtensionForbiddenForCaller(const WebGLExtensionID ext,
                                          const dom::CallerType callerType) {
  if (callerType == dom::CallerType::System) return false;

  if (StaticPrefs::webgl_enable_privileged_extensions()) return false;

  switch (ext) {
    case WebGLExtensionID::MOZ_debug:
      return true;

    default:
      return false;
  }
}

bool ClientWebGLContext::IsSupported(const WebGLExtensionID ext,
                                     const dom::CallerType callerType) const {
  if (IsExtensionForbiddenForCaller(ext, callerType)) return false;

  const auto& supportedExts = mNotLost->info.supportedExtensions;
  return supportedExts[ext];
}

void ClientWebGLContext::GetSupportedExtensions(
    dom::Nullable<nsTArray<nsString>>& retval,
    const dom::CallerType callerType) const {
  retval.SetNull();
  if (!mNotLost) return;

  auto& retarr = retval.SetValue();
  for (const auto i : MakeEnumeratedRange(WebGLExtensionID::Max)) {
    if (!IsSupported(i, callerType)) continue;

    const auto& extStr = GetExtensionName(i);
    retarr.AppendElement(NS_ConvertUTF8toUTF16(extStr));
  }
}

// -

void ClientWebGLContext::GetSupportedProfilesASTC(
    dom::Nullable<nsTArray<nsString>>& retval) const {
  retval.SetNull();
  if (!mNotLost) return;

  auto& retarr = retval.SetValue();
  retarr.AppendElement(NS_LITERAL_STRING("ldr"));
  if (mNotLost->info.astcHdr) {
    retarr.AppendElement(NS_LITERAL_STRING("hdr"));
  }
}

// -

bool ClientWebGLContext::ShouldResistFingerprinting() const {
  if (NS_IsMainThread()) {
    if (mCanvasElement) {
      // If we're constructed from a canvas element
      return nsContentUtils::ShouldResistFingerprinting(GetOwnerDoc());
    }
    // if (mOffscreenCanvas->GetOwnerGlobal()) {
    //  // If we're constructed from an offscreen canvas
    //  return nsContentUtils::ShouldResistFingerprinting(
    //      mOffscreenCanvas->GetOwnerGlobal()->PrincipalOrNull());
    //}
    // Last resort, just check the global preference
    return nsContentUtils::ShouldResistFingerprinting();
  }
  dom::WorkerPrivate* workerPrivate = dom::GetCurrentThreadWorkerPrivate();
  MOZ_ASSERT(workerPrivate);
  return nsContentUtils::ShouldResistFingerprinting(workerPrivate);
}

// ---------------------------

void ClientWebGLContext::EnqueueError_ArgEnum(const char* const argName, const GLenum val) const {
  EnqueueError(LOCAL_GL_INVALID_ENUM, "Bad `%s`: 0x%04x", argName, val);
}

// -
// WebGLProgramJS

void ClientWebGLContext::AttachShader(WebGLProgramJS& prog,
                                      const WebGLShaderJS& shader) const {
  const FuncScope funcScope(*this, "attachShader");
  if (IsContextLost()) return;
  if (!prog.ValidateUsable(*this, "program")) return;
  if (!shader.ValidateUsable(*this, "shader")) return;

  const auto& itr = prog.mNextLink_Shaders.find(shader.mType);
  if (itr == prog.mNextLink_Shaders.end()) {
    MOZ_CRASH("Bad `shader.mType`");
  }
  auto& shaderSlot = itr->second;

  if (shaderSlot) {
    if (&shader == shaderSlot->js) {
      EnqueueError(LOCAL_GL_INVALID_OPERATION,
          "`shader` is already attached.");
    } else {
      EnqueueError(LOCAL_GL_INVALID_OPERATION,
          "Only one of each type of"
          " shader may be attached to a program.");
    }
    return;
  }
  shaderSlot = shader.mInnerWeak;
  MOZ_ASSERT(shaderSlot);
}

void ClientWebGLContext::BindAttribLocation(const WebGLProgramJS& prog,
                                            const GLuint location,
                                            const nsAString& name) const {
  const auto& nameU8 = NS_ConvertUTF16toUTF8(name);
  Run<RPROC(BindAttribLocation)>(prog, location, nameU8);
}

void ClientWebGLContext::DetachShader(WebGLProgramJS& prog,
                                      const WebGLShaderJS& shader) const {
  const FuncScope funcScope(*this, "detachShader");
  if (IsContextLost()) return;
  if (!prog.ValidateUsable(*this, "program")) return;
  if (!shader.ValidateUsable(*this, "shader")) return;

  const auto& itr = prog.mNextLink_Shaders.find(shader.mType);
  if (itr == prog.mNextLink_Shaders.end()) {
    MOZ_CRASH("Bad `shader.mType`");
  }
  auto& shaderSlot = itr->second;

  if (!shaderSlot || shaderSlot->js != &shader) {
    EnqueueError(LOCAL_GL_INVALID_OPERATION,
        "`shader` is not attached.");
    return;
  }
  shaderSlot = nullptr;
}

void ClientWebGLContext::GetAttachedShaders(const WebGLProgramJS& prog,
    dom::Nullable<nsTArray<RefPtr<WebGLShaderJS>>>& retval) const {
  const FuncScope funcScope(*this, "getAttachedShaders");
  if (IsContextLost()) return;
  if (!prog.ValidateUsable(*this, "program")) return;

  auto& arr = retval.SetValue();
  for (const auto& pair : prog.mNextLink_Shaders) {
    const auto& shader = pair.second->js;
    arr.Append(shader);
  }
}

void ClientWebGLContext::LinkProgram(WebGLProgramJS& prog) const {
  const FuncScope funcScope(*this, "linkProgram");
  if (IsContextLost()) return;
  if (!prog.ValidateUsable(*this, "program")) return;

  shader.mResult = std::make_shared<webgl::ProgramResult>();
  shader.mUniformLocs = {};
  Run<RPROC(LinkProgram)>(prog.mId);
}

void ClientWebGLContext::TransformFeedbackVaryings(WebGLProgramJS& prog,
                            const dom::Sequence<nsString>& varyings,
                                 const GLenum bufferMode) const
{
  std::vector<nsCString> varyingsU8;
  varyingsU8.reserve(varyings.Length());
  for (const auto& cur : varyings) {
    varyingsU8.push_back(std::move(NS_ConvertUTF16toUTF8(cur)));
  }

  Run<RPROC(TransformFeedbackVaryings)>(prog.mId, varyingsU8, bufferMode);
}

void ClientWebGLContext::UniformBlockBinding(WebGLProgramJS& prog, const GLuint blockIndex,
                           const GLuint blockBinding) const {
  Run<RPROC(UniformBlockBinding)>(prog.mId, blockIndex, blockBinding);
}

// WebGLProgramJS link result reflection

RefPtr<WebGLActiveInfoJS> ClientWebGLContext::GetActiveAttrib(const WebGLProgramJS& prog,
    const GLuint index) const {
  const FuncScope funcScope(*this, "getActiveAttrib");
  if (IsContextLost()) return nullptr;
  if (!prog.ValidateUsable(*this, "program")) return nullptr;

  const auto& res = GetProgramResult(prog);
  const auto& list = res.activeAttribs;
  if (index >= list.size()) {
    EnqueueError(LOCAL_GL_INVALID_VALUE, "`index` too large.");
    return nullptr;
  }

  return list[index];
}

RefPtr<WebGLActiveInfoJS> ClientWebGLContext::GetActiveUniform(const WebGLProgramJS& prog,
    const GLuint index) const {
  const FuncScope funcScope(*this, "getActiveUniform");
  if (IsContextLost()) return nullptr;
  if (!prog.ValidateUsable(*this, "program")) return nullptr;

  const auto& res = GetProgramResult(prog);
  const auto& list = res.activeUniforms;
  if (index >= list.size()) {
    EnqueueError(LOCAL_GL_INVALID_VALUE, "`index` too large.");
    return nullptr;
  }

  return list[index];
}

void ClientWebGLContext::GetActiveUniformBlockName(const WebGLProgramJS& prog, const GLuint index,
                               nsAString& retval) const {
  const FuncScope funcScope(*this, "getActiveUniformBlockName");
  if (IsContextLost()) return;
  if (!prog.ValidateUsable(*this, "program")) return;

  const auto& res = GetProgramResult(prog);
  const auto& list = res.activeUniformBlocks;
  if (index >= list.size()) {
    EnqueueError(LOCAL_GL_INVALID_VALUE, "`index` too large.");
    return;
  }

  const auto& block = list[index];
  retval = NS_ConvertUTF8toUTF16(block.name);
}

void ClientWebGLContext::GetActiveUniformBlockParameter(JSContext* const cx,
        const WebGLProgramJS& prog,
                                    const GLuint index, const GLenum pname,
                                    JS::MutableHandle<JS::Value> retval,
                                    ErrorResult& rv) const {
  const FuncScope funcScope(*this, "getActiveUniformBlockParameter");
  if (IsContextLost()) return;
  if (!prog.ValidateUsable(*this, "program")) return;

  const auto& res = GetProgramResult(prog);
  const auto& list = res.activeUniformBlocks;
  if (index >= list.size()) {
    EnqueueError(LOCAL_GL_INVALID_VALUE, "`index` too large.");
    return;
  }

  const auto& block = list[index];

  retval.set([&]() -> JS::Value {
    switch (pname) {
      case LOCAL_GL_UNIFORM_BLOCK_BINDING:
        return JS::NumberValue(block.binding);

      case LOCAL_GL_UNIFORM_BLOCK_DATA_SIZE:
        return JS::NumberValue(block.dataSize);

      case LOCAL_GL_UNIFORM_BLOCK_ACTIVE_UNIFORMS:
        return JS::NumberValue(block.activeUniforms.size());

      case LOCAL_GL_UNIFORM_BLOCK_ACTIVE_UNIFORMS: {
        std::vector<GLuint> indices;
        indices.reserve(block.activeUniforms.size());
        for (const auto& cur : block.activeUniforms) {
          indices.push_back(cur.index);
        }
        JS::RootedObject obj(cx, dom::Uint32Array::Create(cx, this, indices.size(), indices.data()));
        if (!obj) {
          rv = NS_ERROR_OUT_OF_MEMORY;
        }
        return JS::ObjectOrNullValue(obj);
      }

      case LOCAL_GL_UNIFORM_REFERENCED_BY_VERTEX_SHADER:
        return JS::NumberValue(block.referencedByVertShader);

      case LOCAL_GL_UNIFORM_REFERENCED_BY_FRAGMENT_SHADER:
        return JS::NumberValue(block.referencedByFragShader);

      default:
        EnqueueError_ArgEnum("pname", pname);
        return JS::NullValue();
    }
  }());
}

void ClientWebGLContext::GetActiveUniforms(JSContext* const cx, const WebGLProgramJS&,
                       const dom::Sequence<GLuint>& uniformIndices,
                       const GLenum pname, JS::MutableHandle<JS::Value> retval) const {
  const FuncScope funcScope(*this, "getActiveUniforms");
  if (IsContextLost()) return;
  if (!prog.ValidateUsable(*this, "program")) return;

  const auto& res = GetProgramResult(prog);
  const auto& list = res.activeUniforms;

  const auto count = uniformIndices.Length();
  JS::Rooted<JSObject*> array(cx, JS_NewArrayObject(cx, count));
  if (!array) return; // Just bail.

  for (const auto& i : IntegerRange(count)) {
    const auto index = uniformIndices[i];
    if (index >= list.size()) {
      EnqueueError(LOCAL_GL_INVALID_VALUE, "`uniformIndices[%u]`: `%u` too large.", i, index);
      return;
    }
    const auto& uniform = list[index];

    JS::RootedValue value(cx);
    switch (pname) {
      case LOCAL_GL_UNIFORM_TYPE:
        value = JS::NumberValue(uniform.type);
        break;

      case LOCAL_GL_UNIFORM_SIZE:
        value = JS::NumberValue(uniform.size);
        break;

      case LOCAL_GL_UNIFORM_BLOCK_INDEX:
        value = JS::NumberValue(uniform.blockIndex);
        break;

      case LOCAL_GL_UNIFORM_OFFSET:
        value = JS::NumberValue(uniform.offset);
        break;

      case LOCAL_GL_UNIFORM_ARRAY_STRIDE:
        value = JS::NumberValue(uniform.arrayStride);
        break;

      case LOCAL_GL_UNIFORM_MATRIX_STRIDE:
        value = JS::NumberValue(uniform.matrixStride);
        break;

      case LOCAL_GL_UNIFORM_IS_ROW_MAJOR:
        value = JS::BooleanValue(uniform.isRowMajor);
        break;

      default:
        EnqueueError_ArgEnum("pname", pname);
        return;
    }
    if (!JS_DefineElement(cx, array, i, value, JSPROP_ENUMERATE)) return;
  }

  retval.setObject(*array);
}

RefPtr<WebGLActiveInfoJS> ClientWebGLContext::GetTransformFeedbackVarying(const WebGLProgramJS& prog,
    const GLuint index) const {
  const FuncScope funcScope(*this, "getTransformFeedbackVarying");
  if (IsContextLost()) return nullptr;
  if (!prog.ValidateUsable(*this, "program")) return nullptr;

  const auto& res = GetProgramResult(prog);
  const auto& list = res.activeTfVaryings;
  if (index >= list.size()) {
    EnqueueError(LOCAL_GL_INVALID_VALUE, "`index` too large.");
    return nullptr;
  }

  return list[index];
}

GLint ClientWebGLContext::GetAttribLocation(const WebGLProgramJS& prog,
                                            const nsAString& name) const {
  const FuncScope funcScope(*this, "getAttribLocation");
  if (IsContextLost()) return -1;
  if (!prog.ValidateUsable(*this, "program")) return -1;

  const nsCString nameU8 = std::move(NS_ConvertUTF16toUTF8(name));
  const auto& res = GetProgramResult(prog);
  for (const auto& cur : res.activeAttribs) {
    if (cur.mName == nameU8) return cur.mLoc;
  }

  return -1;
}

GLint ClientWebGLContext::GetFragDataLocation(const WebGLProgramJS& prog,
                                              const nsAString& name) const {
  const FuncScope funcScope(*this, "getFragDataLocation");
  if (IsContextLost()) return -1;
  if (!prog.ValidateUsable(*this, "program")) return -1;

  const auto nameU8 = std::string(NS_ConvertUTF16toUTF8(name).BeginReading());
  const auto& res = GetProgramResult(prog);
  const auto itr = res.fragDataLocByName.find(nameU8);
  if (itr == res.fragDataLocByName.end()) return -1;
  return static_cast<GLint>(itr->second);
}

GLuint ClientWebGLContext::GetUniformBlockIndex(
    const WebGLProgramJS& prog, const nsAString& blockName) const {
  const FuncScope funcScope(*this, "getUniformBlockIndex");
  if (IsContextLost()) return;
  if (!prog.ValidateUsable(*this, "program")) return;

  const auto nameU8 = std::string(NS_ConvertUTF16toUTF8(blockName).BeginReading());

  const auto& res = GetProgramResult(prog);
  const auto& list = res.activeUniformBlocks;
  for (const auto& i : IntegerRange(list.size()) {
    const auto& cur = list[i];
    if (cur.name == nameU8) {
      return i;
    }
  }
  return LOCAL_GL_INVALID_INDEX;
}

void ClientWebGLContext::GetUniformIndices(
    const WebGLProgramJS& prog,
    const dom::Sequence<nsString>& uniformNames,
    dom::Nullable<nsTArray<GLuint>>& retval) const {
  const FuncScope funcScope(*this, "getUniformIndices");
  if (IsContextLost()) return;
  if (!prog.ValidateUsable(*this, "program")) return;

  const auto& res = GetProgramResult(prog);
  auto ret = nsTArray<GLuint>(uniformNames.Length());

  for (const auto& uniformName : uniformNames) {
    const auto nameU8 = std::string(NS_ConvertUTF16toUTF8(uniformName).BeginReading());

    for (const auto& cur : res.activeUniforms) {
      if (cur.name == nameU8) {
        uint32_t index = LOCAL_GL_INVALID_INDEX;
        if (cur.block != -1) {
          index = static_cast<uint32_t>(cur.block);
        }
        ret.AppendElement(index);
        continue;
      }
    }
  }
  retval.SetValue(std::move(ret));
}


RefPtr<WebGLUniformLocationJS>
ClientWebGLContext::GetUniformLocation(const WebGLProgramJS& prog,
                                       const nsAString& name) const {
  const FuncScope funcScope(*this, "getUniformLocation");
  if (IsContextLost()) return nullptr;
  if (!prog.ValidateUsable(*this, "program")) return nullptr;

  if (!mUniformLocs) {
    mUniformLocs.emplace();

    const auto& res = GetProgramResult(prog);
    ostringstream locName;
    RefPtr<WebGLUniformLocationJS> loc;
    for (const auto& activeUniform : res.activeUniforms) {
      if (activeUniform.block != -1) continue;

      for (const auto& pair : activeUniform.locByIndex) {
        locName.str(activeUniform.name);
        if (pair.first != UINT32_MAX) {
          locName << "[" << pair.first << "]";
        }
        loc = new WebGLUniformLocationJS(*this, res, pair->second);
        mUniformLocs->insert({locName.str(), loc});
      }
    }
  }

  const auto nameU8 = std::string(NS_ConvertUTF16toUTF8(name).BeginReading());
  const auto itr = prog.mUniformLocs->find(nameU8);
  if (itr == prog.mUniformLocs->end()) return nullptr;
  return itr->second;
}

void ClientWebGLContext::GetProgramInfoLog(const WebGLProgramJS& prog, nsAString& retval) const {
  const FuncScope funcScope(*this, "getProgramInfoLog");
  if (IsContextLost()) return;
  if (!prog.ValidateUsable(*this, "program")) return;

  const auto& res = GetProgramResult(prog);
  retval = NS_ConvertUTF8toUTF16(res.log);
}

void ClientWebGLContext::GetProgramParameter(JSContext* const js, const WebGLProgram& prog,
      const GLenum pname, JS::MutableHandle<JS::Value> retval) const {
  const FuncScope funcScope(*this, "getProgramParameter");
  if (IsContextLost()) return;
  if (!prog.ValidateUsable(*this, "program")) return;

  retval.set([&]() -> JS::Value {
    switch (pname) {
      case LOCAL_GL_DELETE_STATUS:
        // "Is flagged for deletion?"
        return JS::BooleanValue(!prog.mInnerRef);
      case LOCAL_GL_VALIDATE_STATUS:
        return JS::BooleanValue(prog.mLastValidate);
      case LOCAL_GL_ATTACHED_SHADERS:
        return JS::NumberValue(prog.mNextLink_Shaders.size());
      default:
        break;
    }

    const auto& res = GetProgramResult(prog);

    switch (pname) {
      case LOCAL_GL_LINK_STATUS:
        return JS::BooleanValue(res.success);

      case LOCAL_GL_ACTIVE_ATTRIBUTES:
        return JS::NumberValue(res.activeAttribs.size());

      case LOCAL_GL_ACTIVE_UNIFORMS:
        return JS::NumberValue(res.activeUniforms.size());

      case LOCAL_GL_TRANSFORM_FEEDBACK_BUFFER_MODE:
        return JS::NumberValue(res.tfBufferMode);

      case LOCAL_GL_TRANSFORM_FEEDBACK_VARYINGS:
        return JS::NumberValue(res.activeTfVaryings.size());

      case LOCAL_GL_ACTIVE_UNIFORM_BLOCKS:
        return JS::NumberValue(res.activeUniformBlocks.size());

      default:
        EnqueueError_ArgEnum("pname", pname);
        return JS::NullValue();
    }
  }());
}

// -
// WebGLShaderJS

void ClientWebGLContext::CompileShader(WebGLShaderJS& shader) const {
  const FuncScope funcScope(*this, "getShaderInfoLog");
  if (IsContextLost()) return;
  if (!shader.ValidateUsable(*this, "shader")) return;

  shader.mResult = {};
  Run<RPROC(CompileShader)>(shader.mId);
}

void ClientWebGLContext::GetShaderInfoLog(const WebGLShaderJS& shader, nsAString& retval) const {
  const FuncScope funcScope(*this, "getShaderInfoLog");
  if (IsContextLost()) return;
  if (!shader.ValidateUsable(*this, "shader")) return;

  const auto& result = GetShaderResult(shader);
  retval = NS_ConvertUTF8toUTF16(result.log);
}

void ClientWebGLContext::GetShaderParameter(JSContext* const cx, const WebGLShaderJS& shader,
     const GLenum pname, JS::MutableHandle<JS::Value> retval) const {
  const FuncScope funcScope(*this, "getShaderParameter");
  if (IsContextLost()) return;
  if (!shader.ValidateUsable(*this, "shader")) return;

  retval.set([&]() -> JS::Value {
    switch (pname) {
      case LOCAL_GL_SHADER_TYPE:
        return JS::NumberValue(shader.mType);

      case LOCAL_GL_DELETE_STATUS: // "Is flagged for deletion?"
        return JS::BooleanValue(!shader.mInnerRef);

      case LOCAL_GL_COMPILE_STATUS: {
        const auto& result = GetShaderResult(shader);
        return JS::BooleanValue(result.success);
      }

      default:
        EnqueueError_ArgEnum("pname", pname);
        return JS::NullValue();
    }
  }());
}

void ClientWebGLContext::GetShaderSource(const WebGLShaderJS& shader, nsAString& retval) const {
  const FuncScope funcScope(*this, "getShaderSource");
  if (IsContextLost()) return;
  if (!shader.ValidateUsable(*this, "shader")) return;

  retval = NS_ConvertUTF8toUTF16(shader.mSource);
}

void ClientWebGLContext::GetTranslatedShaderSource(const WebGLShaderJS& shader, nsAString& retval) const {
  const FuncScope funcScope(*this, "getTranslatedShaderSource");
  if (IsContextLost()) return;
  if (!shader.ValidateUsable(*this, "shader")) return;

  const auto& result = GetShaderResult(shader);
  retval = NS_ConvertUTF8toUTF16(result.translatedSource);
}

void ClientWebGLContext::ShaderSource(WebGLShaderJS& shader, const nsAString& source) const {
  const FuncScope funcScope(*this, "detachShader");
  if (IsContextLost()) return;
  if (!shader.ValidateUsable(*this, "shader")) return;

  shader.mSource = NS_ConvertUTF16toUTF8(source);
  Run<RPROC(ShaderSource)>(shader.mId, shader.mSource);
}

const webgl::ShaderResult& ClientWebGLContext::GetShaderResult(const WebGLShaderJS& shader) const {
  if (shader.mResult.pending) {
    shader.mResult = Run<RPROC(GetShaderResult)>(shader.mId);
  }
  return shader.mResult;
}

// ---------------------------

WebGLActiveInfoJS::WebGLActiveInfoJS(const ClientWebGLContext& parent,
       const uint32_t elemCount, const GLenum elemType, const nsAString& name)
       : mParent(parent), mElemCount(elemCount), mElemType(elemType), mName(name) {}

// -

WebGLFramebufferJS::WebGLFramebufferJS(ClientWebGLContext& webgl) : webgl::ObjectJS(webgl)
{
  (void)mAttachments[LOCAL_GL_DEPTH_ATTACHMENT];
  (void)mAttachments[LOCAL_GL_STENCIL_ATTACHMENT];
  if (!webgl.IsWebGL2()) {
    (void)mAttachments[LOCAL_GL_DEPTH_STENCIL_ATTACHMENT];
  }

  const auto& limits = webgl.Limits();
  for (const auto& i : IntegerRange(limits.maxColorAttachments)) {
    (void)mAttachments[LOCAL_GL_COLOR_ATTACHMENT0 + i];
  }
}

WebGLProgramJS::WebGLProgramJS(ClientWebGLContext& webgl) : webgl::ObjectJS(webgl),
  mInnerRef(std::make_shared<WebGLProgramPreventDelete>(this)), mInnerWeak(mInnerRef)
{
  (void)mNextLink_Shaders[LOCAL_GL_VERTEX_SHADER];
  (void)mNextLink_Shaders[LOCAL_GL_FRAGMENT_SHADER];

  mResult = std::make_shared<webgl::LinkResult>();
}

WebGLSamplerJS::WebGLSamplerJS(ClientWebGLContext& webgl) : webgl::ObjectJS(webgl)
{
  mTarget = 1; // IsSampler should immediately return true.
}

WebGLShaderJS::WebGLShaderJS(ClientWebGLContext& webgl) : webgl::ObjectJS(webgl),
  mInnerRef(std::make_shared<WebGLShaderPreventDelete>(this)), mInnerWeak(mInnerRef)
{
}

WebGLTransformFeedbackJS::WebGLTransformFeedbackJS(ClientWebGLContext& webgl) :
  webgl::ObjectJS(webgl), mBuffers(webgl.Limits().maxTfoBuffers)
{ }

WebGLVertexArrayJS::WebGLVertexArrayJS(ClientWebGLContext& webgl) :
  webgl::ObjectJS(webgl), mAttribBuffers(webgl.Limits().maxVertexBuffers)
{ }

// -

#define _(WebGLType) \
  JSObject* WebGLType##JS::WrapObject(JSContext* const cx, \
                                      JS::Handle<JSObject*> givenProto) { \
    return dom::WebGLType##_Binding::Wrap(cx, this, givenProto); \
  }

_(WebGLActiveInfo)
_(WebGLBuffer)
_(WebGLFramebuffer)
_(WebGLProgram)
_(WebGLQuery)
_(WebGLRenderbuffer)
_(WebGLSampler)
_(WebGLShader)
_(WebGLShaderPrecisionFormat)
_(WebGLSync)
_(WebGLTexture)
_(WebGLTransformFeedback)
_(WebGLUniformLocation)
_(WebGLVertexArray)

#undef _

// -

template<typename T>
void ImplCycleCollectionTraverse(nsCycleCollectionTraversalCallback& callback,
                                 const std::vector<T>& field,
                                 const char* name, uint32_t flags) {
  for (const auto& cur : field) {
    ImplCycleCollectionTraverse(callback, cur, name, flags);
  }
}

template<typename K, typename V>
void ImplCycleCollectionTraverse(nsCycleCollectionTraversalCallback& callback,
                                 const std::unordered_map<K,RefPtr<V>& field,
                                 const char* name, uint32_t flags) {
  for (const auto& pair : field) {
    ImplCycleCollectionTraverse(callback, pair.first, name, flags);
    ImplCycleCollectionTraverse(callback, pair.second, name, flags);
  }
}

void ImplCycleCollectionUnlink(std::vector<IndexedBufferBinding>& field) {
  field.clear();
}

// -

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_0(WebGLActiveInfoJS)
NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_0(WebGLBufferJS)
NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(WebGLFramebufferJS, mAttachments)
NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(WebGLProgramJS, mAttachments)
NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_0(WebGLQueryJS)
NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_0(WebGLSamplerJS)
NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_0(WebGLShaderJS)
NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_0(WebGLSyncJS)
NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_0(WebGLTextureJS)
NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(WebGLTransformFeedbackJS, mAttribs)
NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_0(WebGLUniformLocationJS)
NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(WebGLVertexArrayJS, mIndexBuffer, mAttribs)

// -----------------------------

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(ClientWebGLContext)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsICanvasRenderingContextInternal)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports,
                                   nsICanvasRenderingContextInternal)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(ClientWebGLContext)
NS_IMPL_CYCLE_COLLECTING_RELEASE(ClientWebGLContext)

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_0(ClientWebGLContext)

}  // namespace mozilla
